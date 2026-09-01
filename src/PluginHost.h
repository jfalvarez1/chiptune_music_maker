#pragma once

// ============================================================================
// Plugin hosting
//
// One interface over VST3, VST2 and CLAP, so the rest of the program never
// asks which format something is. A hosted plugin is an IEffect like any
// other - Task A left processBlock() and latencySamples() on that interface
// for exactly this, because a plugin is block-native and reports its own
// latency while every built-in is neither.
//
// WHAT IS HERE AND WHAT IS NOT.
//
// Everything that does not need a vendor SDK is here and is tested:
// discovery, the scan cache, the format-agnostic descriptor and parameter
// model, the lock-free parameter queue, instantiation through a registered
// loader, the audio path, project persistence, and what happens when a
// project is opened on a machine that does not have the plugin.
//
// The per-format loaders are not, because none of the three SDKs can be
// built here:
//
//   VST3 - Steinberg's SDK is a large separate download under a dual
//          GPLv3/proprietary licence. Vendoring it is a licensing decision,
//          not a technical one.
//   VST2 - Steinberg withdrew the SDK in 2018. It cannot be legally
//          obtained or redistributed. A VST2 loader cannot ship.
//   CLAP - MIT and header-only, so this is the one that can be finished
//          without a licensing question. It still needs its headers
//          vendored, and a real .clap binary to test against - and an
//          untested audio-thread binary loader is worse than none.
//
// So the seam is real rather than a stub: a loader is REGISTERED, and the
// tests register one and drive the whole path through it - scan, load,
// instantiate, process audio, automate a parameter, save, reload. Adding a
// format later means writing one function and registering it; nothing above
// this line changes.
//
// THE RULE THAT SHAPES ALL OF IT: a plugin lives outside the project, so it
// can always be missing. A project that opens on a machine without the
// plugin must keep the plugin's settings, keep its place in the chain, and
// say so - exactly as a moved sample does. Silently dropping it would throw
// away work that the user cannot get back.
// ============================================================================

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <filesystem>

#include "Effects.h"     // IEffect, which a hosted plugin becomes

namespace ChiptuneTracker {

// ============================================================================
// Formats
//
// PluginFormat and PluginSlot are declared in Types.h, because ChannelConfig
// holds them and this header cannot be reached from there - it needs IEffect
// from Effects.h, and Effects.h includes Types.h. What lives here is
// everything that OPERATES on them.
// ============================================================================

/*
 * Which format a file on disk claims to be, by extension.
 *
 * A .dll is only a VST2 if it is inside a directory that plugins live in,
 * which the scanner decides - on Windows a .dll is the extension of
 * everything, and treating every one as a plugin would mean trying to load
 * the C runtime.
 */
inline PluginFormat pluginFormatFromPath(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (extension == ".vst3") return PluginFormat::VST3;
    if (extension == ".clap") return PluginFormat::CLAP;
    if (extension == ".dll")  return PluginFormat::VST2;
    return PluginFormat::Unknown;
}

// ============================================================================
// What a scan found
// ============================================================================
struct PluginDescriptor {
    PluginFormat format = PluginFormat::Unknown;

    std::string path;      // the file or bundle on disk
    std::string name;      // what the plugin calls itself
    std::string vendor;
    std::string version;

    /*
     * The plugin's own stable identifier - a VST3 FUID, a VST2 four-char
     * code, a CLAP id string.
     *
     * A project refers to a plugin by this AND by path, because either can
     * change on its own: a plugin moves between machines while keeping its
     * id, and a plugin updates in place while keeping its path. Matching on
     * the id first and falling back to the path finds it in both cases.
     */
    std::string uid;

    bool isInstrument = false;   // rather than an effect
    bool hasEditor = false;

    // Filled in by the scan so the cache can tell whether a file changed
    // without opening it.
    std::uintmax_t fileSize = 0;
    std::int64_t modifiedTime = 0;

    bool valid() const {
        return format != PluginFormat::Unknown && !path.empty();
    }
};

// ============================================================================
// Parameters
// ============================================================================
struct PluginParameter {
    std::string name;
    std::string label;      // the unit: dB, Hz, %

    uint32_t id = 0;        // the plugin's own id, which need not be the index
    float defaultValue = 0.0f;
    float value = 0.0f;     // always normalised 0..1, whatever the plugin's range

    bool automatable = true;
    int stepCount = 0;      // 0 = continuous; >0 = that many discrete steps
};

/*
 * Parameter moves from the UI to the audio thread.
 *
 * A fixed-capacity single-producer/single-consumer ring, because a knob
 * being turned must not allocate, lock, or block the audio thread - and
 * because the audio thread cannot wait for the UI to finish writing.
 *
 * When it overflows the OLDEST change is dropped, not the newest. For a
 * parameter, the newest value is the only one that matters: dropping the
 * newest would leave the plugin permanently at a stale value, which is
 * exactly the wrong failure.
 */
class ParameterChangeQueue {
public:
    struct Change {
        int index = -1;
        float value = 0.0f;
    };

    static constexpr size_t CAPACITY = 512;   // power of two, for the mask

    void push(int index, float value) {
        const size_t write = m_write.load(std::memory_order_relaxed);
        const size_t read = m_read.load(std::memory_order_acquire);

        if (write - read >= CAPACITY) {
            // Full. Drop the oldest by advancing the read cursor, so the
            // newest value is the one that survives.
            m_read.store(read + 1, std::memory_order_release);
            ++m_dropped;
        }

        m_buffer[write & (CAPACITY - 1)] = Change{index, value};
        m_write.store(write + 1, std::memory_order_release);
    }

    bool pop(Change& out) {
        const size_t read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire)) return false;
        out = m_buffer[read & (CAPACITY - 1)];
        m_read.store(read + 1, std::memory_order_release);
        return true;
    }

    size_t pending() const {
        return m_write.load(std::memory_order_acquire) -
               m_read.load(std::memory_order_acquire);
    }

    size_t dropped() const { return m_dropped; }

    void clear() {
        m_read.store(m_write.load(std::memory_order_acquire),
                     std::memory_order_release);
    }

private:
    Change m_buffer[CAPACITY];
    std::atomic<size_t> m_write{0};
    std::atomic<size_t> m_read{0};
    size_t m_dropped = 0;
};

// ============================================================================
// Why a plugin did not load
// ============================================================================
enum class PluginLoadError : uint8_t {
    Ok = 0,
    FileMissing,          // the path is not there any more
    FormatUnsupported,    // no loader is registered for this format
    SdkNotBuilt,          // the format's loader was not compiled in
    EntryPointMissing,    // the binary is not a plugin of that format
    WrongArchitecture,    // 32-bit plugin, 64-bit host or the reverse
    InitialisationFailed, // it loaded and then refused to start
};

/*
 * Said in a way a musician can act on.
 *
 * "InitialisationFailed" tells somebody nothing. Which of these they see
 * decides whether they go looking for the file, install something, or give
 * up on that plugin - so each one names the thing to do.
 */
inline const char* pluginLoadErrorText(PluginLoadError error) {
    switch (error) {
        case PluginLoadError::Ok:
            return "Loaded";
        case PluginLoadError::FileMissing:
            return "The plugin file is not where the project expects it. "
                   "Its settings are kept - point it at the file to get it back.";
        case PluginLoadError::FormatUnsupported:
            return "This build cannot host that plugin format.";
        case PluginLoadError::SdkNotBuilt:
            return "Support for this format was not built into this copy.";
        case PluginLoadError::EntryPointMissing:
            return "The file is not a plugin of the format its name suggests.";
        case PluginLoadError::WrongArchitecture:
            return "The plugin is built for a different architecture than "
                   "this program.";
        case PluginLoadError::InitialisationFailed:
            return "The plugin loaded but refused to start.";
        default:
            return "Unknown";
    }
}

// ============================================================================
// A loaded plugin
// ============================================================================

/*
 * One running plugin.
 *
 * An IEffect, so it drops into an effects slot beside the built-ins and the
 * mixer needs no idea it is anything else. processBlock() is the one it
 * overrides: a plugin is block-native, and calling it per-sample would be
 * both wrong and ruinously slow.
 *
 * Everything on this interface that the audio thread calls -
 * processBlock(), latencySamples(), and the queue drain inside them - must
 * not allocate, lock or touch the filesystem. Everything else is UI-thread
 * only.
 */
class IPluginInstance : public IEffect {
public:
    ~IPluginInstance() override = default;

    virtual const PluginDescriptor& descriptor() const = 0;

    virtual int parameterCount() const = 0;
    virtual const PluginParameter& parameter(int index) const = 0;

    // UI thread. Queues the change; the audio thread applies it.
    virtual void setParameter(int index, float normalised) = 0;

    /*
     * The plugin's own opaque state.
     *
     * Saved as well as the parameter values because a plugin's state is
     * usually more than its parameters - a sampler's loaded file, a synth's
     * wavetable - and restoring only the parameters would silently lose it.
     */
    virtual bool saveState(std::string& out) const { (void)out; return false; }
    virtual bool loadState(const std::string& state) { (void)state; return false; }

    // A hosted plugin is stereo; the built-in chain is mono until pan.
    virtual void processStereo(float* left, float* right, int frames) = 0;

    /*
     * The mono path, for a chain that has not split into stereo yet.
     *
     * Default implementation feeds the same buffer to both sides and takes
     * the left back, which is what a mono insert means. A plugin that
     * genuinely differs can override it.
     */
    void processBlock(float* buffer, int frames, float startTime,
                      float secondsPerFrame) override {
        (void)startTime;
        (void)secondsPerFrame;
        if (buffer == nullptr || frames <= 0) return;
        processStereo(buffer, buffer, frames);
    }

    // Never called on the audio thread; a plugin in a slot is processed by
    // block. Present only because IEffect requires it.
    float process(float input, float time) override {
        (void)time;
        return input;
    }
};

// ============================================================================
// The loader seam
// ============================================================================

/*
 * How a format gets hosted.
 *
 * A loader is a function that turns a descriptor into a running instance,
 * or explains why it could not. Registering one is the entire cost of
 * adding a format: nothing above this point knows VST3 from CLAP.
 *
 * Kept as registered functions rather than #ifdef'd calls so a format can
 * be added without editing this file, and so the tests can register one and
 * drive the whole path with no SDK present.
 */
using PluginLoaderFn = std::function<std::unique_ptr<IPluginInstance>(
    const PluginDescriptor&, float sampleRate, PluginLoadError&)>;

/*
 * How a format is recognised on disk.
 *
 * Separate from the loader because scanning must be cheap and must not run
 * plugin code: a scan opens hundreds of files, and instantiating each one
 * to find out its name is how a DAW takes four minutes to start.
 */
using PluginProbeFn = std::function<bool(const std::string& path,
                                         PluginDescriptor& out)>;

class PluginFormatRegistry {
public:
    struct Entry {
        PluginFormat format = PluginFormat::Unknown;
        PluginLoaderFn load;
        PluginProbeFn probe;
    };

    void registerFormat(PluginFormat format, PluginLoaderFn load,
                        PluginProbeFn probe) {
        for (Entry& entry : m_entries) {
            if (entry.format == format) {
                entry.load = std::move(load);
                entry.probe = std::move(probe);
                return;
            }
        }
        m_entries.push_back(Entry{format, std::move(load), std::move(probe)});
    }

    void clear() { m_entries.clear(); }

    bool supports(PluginFormat format) const {
        for (const Entry& entry : m_entries) {
            if (entry.format == format && entry.load) return true;
        }
        return false;
    }

    const Entry* find(PluginFormat format) const {
        for (const Entry& entry : m_entries) {
            if (entry.format == format) return &entry;
        }
        return nullptr;
    }

    size_t size() const { return m_entries.size(); }

    /*
     * Instantiate, or say why not.
     *
     * The file is checked first, so a moved plugin is reported as moved
     * rather than as whatever the format's loader happens to say when it is
     * handed a path that is not there.
     */
    std::unique_ptr<IPluginInstance> create(const PluginDescriptor& descriptor,
                                            float sampleRate,
                                            PluginLoadError& error) const {
        error = PluginLoadError::Ok;

        if (!descriptor.valid()) {
            error = PluginLoadError::FormatUnsupported;
            return nullptr;
        }

        std::error_code fsError;
        if (!std::filesystem::exists(descriptor.path, fsError) || fsError) {
            error = PluginLoadError::FileMissing;
            return nullptr;
        }

        const Entry* entry = find(descriptor.format);
        if (entry == nullptr || !entry->load) {
            // Told apart deliberately: a format nobody ever registered is a
            // different problem from one this build left out, and only one
            // of them is worth the user going looking for another build.
            error = (descriptor.format == PluginFormat::Unknown)
                        ? PluginLoadError::FormatUnsupported
                        : PluginLoadError::SdkNotBuilt;
            return nullptr;
        }

        std::unique_ptr<IPluginInstance> instance =
            entry->load(descriptor, sampleRate, error);
        if (instance == nullptr && error == PluginLoadError::Ok) {
            // A loader that returns nothing without saying why still has to
            // produce an error, or the UI shows "Loaded" beside nothing.
            error = PluginLoadError::InitialisationFailed;
        }
        return instance;
    }

    bool probe(const std::string& path, PluginDescriptor& out) const {
        const PluginFormat format = pluginFormatFromPath(path);
        const Entry* entry = find(format);
        if (entry == nullptr || !entry->probe) return false;
        return entry->probe(path, out);
    }

private:
    std::vector<Entry> m_entries;
};

inline PluginFormatRegistry& pluginFormats() {
    static PluginFormatRegistry registry;
    return registry;
}

// ============================================================================
// Finding plugins
// ============================================================================

/*
 * Where plugins live on this platform.
 *
 * The standard locations, so somebody who has never opened the settings
 * still finds what they already own. Returned rather than scanned so the
 * caller can show them, and so a test can scan somewhere else entirely.
 */
inline std::vector<std::string> defaultPluginDirectories() {
    std::vector<std::string> directories;

#ifdef _WIN32
    auto fromEnvironment = [&](const char* variable, const char* suffix) {
        const char* root = std::getenv(variable);
        if (root == nullptr) return;
        directories.push_back(std::string(root) + suffix);
    };

    fromEnvironment("ProgramFiles", "\\Common Files\\VST3");
    fromEnvironment("ProgramFiles", "\\Common Files\\CLAP");
    fromEnvironment("ProgramFiles", "\\VSTPlugins");
    fromEnvironment("ProgramFiles", "\\Steinberg\\VSTPlugins");
    fromEnvironment("LOCALAPPDATA", "\\Programs\\Common\\VST3");
    fromEnvironment("LOCALAPPDATA", "\\Programs\\Common\\CLAP");
#else
    directories.push_back("/usr/lib/vst3");
    directories.push_back("/usr/local/lib/vst3");
    directories.push_back("/usr/lib/clap");
    directories.push_back("/usr/local/lib/clap");
#endif

    return directories;
}

struct ScanResult {
    std::vector<PluginDescriptor> found;
    int filesExamined = 0;
    int directoriesMissing = 0;
    int rejected = 0;        // right extension, not actually a plugin
};

/*
 * Walk directories and list what is there.
 *
 * A VST3 on Windows is a *directory* named Something.vst3 with the binary
 * inside it, and a CLAP is a plain file - so a scan cannot simply look for
 * files, and treating the bundle's contents as separate plugins would list
 * every one of them several times.
 *
 * Depth-limited on purpose. Plugin folders are shallow, and a user who
 * points this at their whole drive should get a slow scan rather than one
 * that never finishes.
 */
inline void scanPluginDirectory(const std::string& directoryPath,
                                const PluginFormatRegistry& registry,
                                ScanResult& result, int maxDepth = 4) {
    std::error_code error;
    if (!std::filesystem::is_directory(directoryPath, error) || error) {
        ++result.directoriesMissing;
        return;
    }

    std::filesystem::recursive_directory_iterator iterator(
        directoryPath,
        std::filesystem::directory_options::skip_permission_denied, error);
    if (error) {
        ++result.directoriesMissing;
        return;
    }

    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) break;

        if (iterator.depth() >= maxDepth) {
            iterator.disable_recursion_pending();
        }

        const std::filesystem::path path = iterator->path();
        const PluginFormat format = pluginFormatFromPath(path.string());
        if (format == PluginFormat::Unknown) continue;

        std::error_code itemError;
        const bool isDirectory = iterator->is_directory(itemError);

        // A .vst3 bundle is a directory; do not descend into it, or every
        // file inside gets examined and the binary is listed again.
        if (isDirectory) {
            if (format == PluginFormat::VST3) iterator.disable_recursion_pending();
            else continue;
        }

        ++result.filesExamined;

        PluginDescriptor descriptor;
        descriptor.format = format;
        descriptor.path = path.string();
        descriptor.name = path.stem().string();

        if (!isDirectory) {
            const std::uintmax_t size = iterator->file_size(itemError);
            if (!itemError) descriptor.fileSize = size;
        }

        /*
         * Ask the format's probe for the real name, if there is one.
         *
         * Without a loader registered the file is still listed, under its
         * filename - somebody looking at the list needs to see that their
         * plugins were found even when this build cannot host them, or the
         * empty list reads as "you have no plugins".
         */
        if (registry.find(format) != nullptr) {
            PluginDescriptor probed = descriptor;
            if (registry.probe(descriptor.path, probed)) {
                probed.path = descriptor.path;
                probed.format = format;
                probed.fileSize = descriptor.fileSize;
                descriptor = probed;
            } else {
                ++result.rejected;
                continue;
            }
        }

        result.found.push_back(descriptor);
    }
}

inline ScanResult scanPluginDirectories(const std::vector<std::string>& directories,
                                        const PluginFormatRegistry& registry) {
    ScanResult result;
    for (const std::string& directory : directories) {
        scanPluginDirectory(directory, registry, result);
    }

    // Sorted by name, then path: a plugin installed in two places is a real
    // situation, and the two must not swap order between runs.
    std::sort(result.found.begin(), result.found.end(),
              [](const PluginDescriptor& a, const PluginDescriptor& b) {
                  if (a.name != b.name) return a.name < b.name;
                  return a.path < b.path;
              });
    return result;
}

// ============================================================================
// The scan cache
// ============================================================================

/*
 * What the last scan found, so startup does not repeat it.
 *
 * Scanning is slow - hundreds of files, and for formats with a real probe,
 * loading each binary. Doing it on every launch is how a DAW earns a
 * reputation for taking a minute to open.
 *
 * The cache is a convenience, never a source of truth: anything in it is
 * re-checked against the file on disk before being used, because the whole
 * point of a cache of things outside the project is that they move.
 */
inline const char* PLUGIN_CACHE_FILENAME = "chiptune-plugins.ini";

inline std::string pluginCachePath(const std::string& directory = std::string()) {
    if (directory.empty()) return PLUGIN_CACHE_FILENAME;
    return directory + "/" + PLUGIN_CACHE_FILENAME;
}

// Fields are tab-separated because a plugin path may contain spaces and
// very nearly always does on Windows.
inline bool savePluginCache(const std::vector<PluginDescriptor>& plugins,
                            const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "# ChiptuneTracker plugin scan cache\n";
    file << "# Safe to delete - it is rebuilt by scanning again.\n";
    for (const PluginDescriptor& plugin : plugins) {
        // Tabs are the separator, so anything containing one would corrupt
        // the line. Replaced rather than escaped: a tab in a plugin name is
        // not worth a quoting scheme.
        auto clean = [](std::string text) {
            for (char& c : text) {
                if (c == '\t' || c == '\n' || c == '\r') c = ' ';
            }
            return text;
        };

        file << pluginFormatToken(plugin.format) << '\t'
             << clean(plugin.path) << '\t'
             << clean(plugin.name) << '\t'
             << clean(plugin.vendor) << '\t'
             << clean(plugin.version) << '\t'
             << clean(plugin.uid) << '\t'
             << (plugin.isInstrument ? 1 : 0) << '\t'
             << (plugin.hasEditor ? 1 : 0) << '\t'
             << plugin.fileSize << '\n';
    }
    return file.good();
}

inline bool loadPluginCache(std::vector<PluginDescriptor>& plugins,
                            const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    plugins.clear();

    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> fields;
        size_t start = 0;
        while (true) {
            const size_t tab = line.find('\t', start);
            if (tab == std::string::npos) {
                fields.push_back(line.substr(start));
                break;
            }
            fields.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }

        // A short line is from an older or newer version. Skipped rather
        // than half-read, which would produce a descriptor pointing at
        // nothing.
        if (fields.size() < 9) continue;

        PluginDescriptor plugin;
        plugin.format = pluginFormatFromToken(fields[0]);
        plugin.path = fields[1];
        plugin.name = fields[2];
        plugin.vendor = fields[3];
        plugin.version = fields[4];
        plugin.uid = fields[5];
        plugin.isInstrument = (fields[6] == "1");
        plugin.hasEditor = (fields[7] == "1");
        plugin.fileSize = std::strtoull(fields[8].c_str(), nullptr, 10);

        if (!plugin.valid()) continue;
        plugins.push_back(plugin);
    }
    return true;
}

/*
 * Which cached entries still describe a file that is there and unchanged.
 *
 * Size is checked as well as existence, because a plugin updated in place
 * keeps its path, and a cached name and parameter list from the old version
 * would be quietly wrong.
 */
inline std::vector<PluginDescriptor> validateCache(
        const std::vector<PluginDescriptor>& cached, int* staleCount = nullptr) {
    std::vector<PluginDescriptor> good;
    int stale = 0;

    for (const PluginDescriptor& plugin : cached) {
        std::error_code error;
        if (!std::filesystem::exists(plugin.path, error) || error) {
            ++stale;
            continue;
        }

        // A VST3 bundle is a directory and has no meaningful size.
        if (plugin.fileSize > 0) {
            const std::uintmax_t size = std::filesystem::file_size(plugin.path, error);
            if (error || size != plugin.fileSize) { ++stale; continue; }
        }
        good.push_back(plugin);
    }

    if (staleCount != nullptr) *staleCount = stale;
    return good;
}

// ============================================================================
// Matching a saved slot to what is installed here
// ============================================================================

/*
 * Match a slot against what was found on this machine.
 *
 * By uid first, then by path, because either can change independently: a
 * project moved between machines keeps the uid while the path changes, and
 * a plugin updated in place keeps the path while, occasionally, the uid
 * does not. Trying both finds it in either case; trying one does not.
 */
inline int findPluginForSlot(const PluginSlot& slot,
                             const std::vector<PluginDescriptor>& available) {
    if (!slot.uid.empty()) {
        for (size_t i = 0; i < available.size(); ++i) {
            if (available[i].uid == slot.uid &&
                available[i].format == slot.format) {
                return static_cast<int>(i);
            }
        }
    }
    /*
     * Compared as paths rather than as strings.
     *
     * The same file is written "C:/x/y.clap" by a project saved on one
     * machine and "C:\\x\\y.clap" by a scan on Windows, and a plain string
     * comparison calls those two different plugins - so a project would
     * report its plugin missing while it sat in the list.
     */
    const std::filesystem::path wanted =
        std::filesystem::path(slot.path).lexically_normal();
    for (size_t i = 0; i < available.size(); ++i) {
        if (std::filesystem::path(available[i].path).lexically_normal() == wanted) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ============================================================================
// The manager
// ============================================================================

/*
 * Everything the program knows about plugins right now.
 *
 * Deliberately not a singleton the audio thread reaches into: instances are
 * created and destroyed here on the UI thread, and handed to the audio side
 * as a pointer that is published once. Loading a plugin inside the audio
 * callback would mean a filesystem read on the real-time thread.
 */
class PluginManager {
public:
    void setRegistry(PluginFormatRegistry* registry) { m_registry = registry; }

    PluginFormatRegistry& registry() {
        return m_registry != nullptr ? *m_registry : pluginFormats();
    }
    const PluginFormatRegistry& registry() const {
        return m_registry != nullptr ? *m_registry : pluginFormats();
    }

    const std::vector<PluginDescriptor>& available() const { return m_available; }

    void setSearchPaths(std::vector<std::string> paths) {
        m_searchPaths = std::move(paths);
    }
    const std::vector<std::string>& searchPaths() const { return m_searchPaths; }

    // The directories to scan: the standard ones, plus anything the user
    // added.
    std::vector<std::string> effectiveSearchPaths() const {
        std::vector<std::string> paths = defaultPluginDirectories();
        paths.insert(paths.end(), m_searchPaths.begin(), m_searchPaths.end());
        return paths;
    }

    ScanResult scan() {
        ScanResult result = scanPluginDirectories(effectiveSearchPaths(), registry());
        m_available = result.found;
        m_scanned = true;
        return result;
    }

    bool scanned() const { return m_scanned; }

    /*
     * Load the cache, keeping only what is still on disk.
     *
     * Returns false when there was no cache to read, so the caller can
     * decide to scan rather than silently showing an empty list.
     */
    bool loadCache(const std::string& path, int* staleCount = nullptr) {
        std::vector<PluginDescriptor> cached;
        if (!loadPluginCache(cached, path)) return false;
        m_available = validateCache(cached, staleCount);
        m_scanned = true;
        return true;
    }

    bool saveCache(const std::string& path) const {
        return savePluginCache(m_available, path);
    }

    /*
     * Bring a slot to life.
     *
     * Returns nullptr and an error rather than throwing, because every
     * caller is either drawing a frame or opening a project, and neither
     * can do anything useful with an exception.
     */
    std::unique_ptr<IPluginInstance> instantiate(const PluginSlot& slot,
                                                 float sampleRate,
                                                 PluginLoadError& error) {
        error = PluginLoadError::Ok;
        if (slot.empty()) {
            error = PluginLoadError::FormatUnsupported;
            return nullptr;
        }

        PluginDescriptor descriptor;
        const int index = findPluginForSlot(slot, m_available);
        if (index >= 0) {
            descriptor = m_available[static_cast<size_t>(index)];
        } else {
            // Not in the list, but the file may still be exactly where the
            // project says. Worth trying: a plugin the user never scanned
            // for should still open with the project that uses it.
            descriptor.format = slot.format;
            descriptor.path = slot.path;
            descriptor.uid = slot.uid;
            descriptor.name = slot.name;
        }

        std::unique_ptr<IPluginInstance> instance =
            registry().create(descriptor, sampleRate, error);
        if (instance == nullptr) return nullptr;

        // Settings first, then parameters: a plugin's opaque state usually
        // sets its parameters too, so applying the state afterwards would
        // undo the values the project saved.
        if (!slot.state.empty()) instance->loadState(slot.state);

        const int count = instance->parameterCount();
        for (size_t i = 0; i < slot.parameterValues.size() &&
                           static_cast<int>(i) < count; ++i) {
            instance->setParameter(static_cast<int>(i), slot.parameterValues[i]);
        }
        return instance;
    }

    // Capture a running plugin back into the project.
    static PluginSlot captureSlot(const IPluginInstance& instance) {
        PluginSlot slot;
        const PluginDescriptor& descriptor = instance.descriptor();
        slot.format = descriptor.format;
        slot.path = descriptor.path;
        slot.uid = descriptor.uid;
        slot.name = descriptor.name;

        slot.parameterValues.reserve(
            static_cast<size_t>(std::max(0, instance.parameterCount())));
        for (int i = 0; i < instance.parameterCount(); ++i) {
            slot.parameterValues.push_back(instance.parameter(i).value);
        }

        std::string state;
        if (instance.saveState(state)) slot.state = state;
        return slot;
    }

private:
    PluginFormatRegistry* m_registry = nullptr;
    std::vector<PluginDescriptor> m_available;
    std::vector<std::string> m_searchPaths;
    bool m_scanned = false;
};

// ============================================================================
// A channel's plugins, on the audio thread
//
// The mixer is per-sample and a plugin is per-block, so something has to
// bridge the two. This does, by filling a fixed buffer and running the
// plugins once it is full.
//
// That costs exactly one block of latency, which is reported rather than
// hidden - an insert that quietly delays one channel and not the others is
// a phase problem the user cannot see and cannot fix.
//
// Instances are created and destroyed on the UI thread only. The audio
// thread reads m_active and the instance pointers, and does nothing that
// allocates, locks or touches a file.
// ============================================================================
class PluginChain {
public:
    static constexpr int BLOCK = 128;
    static constexpr int MAX_PLUGINS = 8;

    /*
     * Whether there is anything to do.
     *
     * Checked by the mixer before doing any of this, so a project with no
     * plugins - which is every project in a build with no loader - pays one
     * predictable branch per channel and nothing else.
     */
    bool active() const { return m_active.load(std::memory_order_acquire); }

    void setSampleRate(float sampleRate) {
        m_sampleRate = sampleRate;
        for (auto& instance : m_instances) {
            if (instance) instance->setSampleRate(sampleRate);
        }
    }

    /*
     * Build the chain from a channel's saved slots. UI thread only.
     *
     * Returns how many actually loaded. The rest stay in the project and
     * are reported by the UI: a plugin that could not be found must not
     * quietly vanish from the chain, or reopening the project on the
     * machine that does have it would restore nothing.
     */
    int build(const std::vector<PluginSlot>& slots, PluginManager& manager,
              std::vector<PluginLoadError>* errorsOut = nullptr) {
        // Silence first, so the audio thread stops using the instances
        // before they are destroyed.
        m_active.store(false, std::memory_order_release);
        m_instances.clear();
        if (errorsOut != nullptr) errorsOut->clear();

        int loaded = 0;
        for (const PluginSlot& slot : slots) {
            if (static_cast<int>(m_instances.size()) >= MAX_PLUGINS) break;

            PluginLoadError error = PluginLoadError::Ok;
            std::unique_ptr<IPluginInstance> instance;
            if (slot.enabled) {
                instance = manager.instantiate(slot, m_sampleRate, error);
            }
            if (errorsOut != nullptr) errorsOut->push_back(error);

            if (instance) {
                instance->setSampleRate(m_sampleRate);
                ++loaded;
            }
            // A failed slot is still pushed, as a null. Keeping the
            // position means a plugin that comes back later lands where it
            // was rather than at the end of the chain.
            m_instances.push_back(std::move(instance));
        }

        m_bypass.assign(slots.size(), false);
        for (size_t i = 0; i < slots.size() && i < m_bypass.size(); ++i) {
            m_bypass[i] = slots[i].bypassed;
        }

        m_fill = 0;
        m_readOffset = 0;
        m_left.assign(BLOCK, 0.0f);
        m_right.assign(BLOCK, 0.0f);
        m_outLeft.assign(BLOCK, 0.0f);
        m_outRight.assign(BLOCK, 0.0f);

        m_active.store(loaded > 0, std::memory_order_release);
        return loaded;
    }

    void clear() {
        m_active.store(false, std::memory_order_release);
        m_instances.clear();
        m_bypass.clear();
        m_fill = 0;
        m_readOffset = 0;
    }

    int size() const { return static_cast<int>(m_instances.size()); }

    IPluginInstance* at(int index) {
        if (index < 0 || index >= static_cast<int>(m_instances.size())) return nullptr;
        return m_instances[static_cast<size_t>(index)].get();
    }

    void setBypassed(int index, bool bypassed) {
        if (index < 0 || index >= static_cast<int>(m_bypass.size())) return;
        m_bypass[static_cast<size_t>(index)] = bypassed;
    }

    // One block of delay, so a channel with plugins can be aligned with one
    // without.
    int latencySamples() const {
        if (!active()) return 0;
        int total = BLOCK;
        for (const auto& instance : m_instances) {
            if (instance) total += instance->latencySamples();
        }
        return total;
    }

    /*
     * One sample in, one sample out - delayed by a block.
     *
     * Audio thread. Fills the input buffer, and when it is full runs every
     * plugin over it and swaps that to the output. What comes back is the
     * previous block, which is where the latency comes from and why it is
     * reported rather than concealed.
     */
    float processSample(float input) {
        if (!active()) return input;

        m_left[static_cast<size_t>(m_fill)] = input;
        m_right[static_cast<size_t>(m_fill)] = input;
        ++m_fill;

        const float out = m_outLeft[static_cast<size_t>(m_readOffset)];
        ++m_readOffset;

        if (m_fill >= BLOCK) {
            runBlock();
            m_fill = 0;
            m_readOffset = 0;
        }
        return out;
    }

private:
    void runBlock() {
        for (size_t i = 0; i < m_instances.size(); ++i) {
            IPluginInstance* instance = m_instances[i].get();
            if (instance == nullptr) continue;
            if (i < m_bypass.size() && m_bypass[i]) continue;
            instance->processStereo(m_left.data(), m_right.data(), BLOCK);
        }
        m_outLeft.swap(m_left);
        m_outRight.swap(m_right);
    }

    std::vector<std::unique_ptr<IPluginInstance>> m_instances;
    std::vector<bool> m_bypass;

    std::vector<float> m_left, m_right;
    std::vector<float> m_outLeft, m_outRight;

    int m_fill = 0;
    int m_readOffset = 0;
    float m_sampleRate = 44100.0f;

    std::atomic<bool> m_active{false};
};

} // namespace ChiptuneTracker
