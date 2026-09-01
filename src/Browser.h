#pragma once

// ============================================================================
// The browser
//
// One docked place to find the things you put into a song: the samples this
// project already holds, the instrument engines and their presets, and the
// audio and project files on disk. Anything in it can be dragged onto the
// timeline or onto a channel.
//
// Before this, a sample could only be reached through a file dialog and an
// instrument only through a dropdown in the Channel Editor - which means
// that finding something required already knowing it existed. A browser is
// the answer to "what have I got?", which is a different question from
// "load this specific file", and only one of them had an answer.
//
// The scanning and the filtering live here, free of ImGui, so both can be
// tested directly. The panel that draws them is in UI.h with the rest.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>
#include <vector>

#include <filesystem>

#include "ActionRegistry.h"   // fuzzyScore, shared with the command palette

namespace ChiptuneTracker {

enum class BrowserTab {
    Samples,        // what this project already holds
    Instruments,    // oscillator types and engines
    Presets,        // the shipped engine presets
    Files,          // audio and project files on disk
    Count
};

inline const char* browserTabName(BrowserTab tab) {
    switch (tab) {
        case BrowserTab::Samples:     return "Samples";
        case BrowserTab::Instruments: return "Instruments";
        case BrowserTab::Presets:     return "Presets";
        case BrowserTab::Files:       return "Files";
        default:                      return "?";
    }
}

enum class BrowserEntryKind {
    Sample,
    Instrument,
    Preset,
    File,
    Directory,
};

struct BrowserEntry {
    BrowserEntryKind kind = BrowserEntryKind::File;

    std::string name;     // what is shown
    std::string detail;   // the grey second column: length, type, size
    std::string path;     // for files; empty otherwise

    /*
     * What the entry means, by kind:
     *   Sample     - the id in the project's sample pool
     *   Instrument - the OscillatorType value
     *   Preset     - an index into the preset table for `engine`
     * Kept as a plain int because it crosses an ImGui drag payload, which
     * is a memcpy of bytes and cannot carry anything with a constructor.
     */
    int id = -1;
    int engine = -1;      // for presets: which engine the index belongs to
};

/*
 * What crosses a drag.
 *
 * ImGui copies a payload byte for byte, so this has to stay a POD with no
 * pointers in it - a std::string here would be copied as a pointer into a
 * buffer that may already be gone by the time it is dropped.
 */
struct BrowserPayload {
    BrowserEntryKind kind = BrowserEntryKind::File;
    int id = -1;
    int engine = -1;
    char path[320] = {};
};

inline const char* BROWSER_PAYLOAD_TYPE = "CHIPTUNE_BROWSER";

inline BrowserPayload makePayload(const BrowserEntry& entry) {
    BrowserPayload payload;
    payload.kind = entry.kind;
    payload.id = entry.id;
    payload.engine = entry.engine;

    // Truncate rather than overrun. A path this long is pathological, and
    // losing the drag is better than writing past the buffer.
    const size_t length = std::min(entry.path.size(), sizeof(payload.path) - 1);
    std::copy(entry.path.begin(), entry.path.begin() + static_cast<long>(length),
              payload.path);
    payload.path[length] = '\0';
    return payload;
}

// ============================================================================
// Disk
// ============================================================================

inline bool isAudioExtension(const std::string& extension) {
    std::string lower;
    for (char c : extension) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower == ".wav" || lower == ".mp3" || lower == ".flac" ||
           lower == ".ogg" || lower == ".aiff" || lower == ".aif";
}

inline bool isProjectExtension(const std::string& extension) {
    std::string lower;
    for (char c : extension) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower == ".ctp" || lower == ".mid" || lower == ".midi";
}

inline std::string humanSize(std::uintmax_t bytes) {
    char text[32];
    if (bytes >= 1024u * 1024u) {
        snprintf(text, sizeof(text), "%.1f MB",
                 double(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(text, sizeof(text), "%.0f KB", double(bytes) / 1024.0);
    } else {
        snprintf(text, sizeof(text), "%llu B",
                 static_cast<unsigned long long>(bytes));
    }
    return text;
}

/*
 * List one directory: sub-directories first, then the files worth opening.
 *
 * Called on demand rather than every frame. Walking a directory is a
 * syscall per entry, and a browser pointed at a sample library on a network
 * drive would otherwise spend the whole frame budget in the filesystem.
 *
 * Every filesystem call takes the error_code overload: a directory that
 * vanishes mid-scan, or one that cannot be read, is a normal thing for a
 * browser to meet and must not throw out of a draw call.
 */
inline void scanDirectory(const std::string& directoryPath,
                          std::vector<BrowserEntry>& out,
                          bool includeParent = true) {
    out.clear();

    std::error_code error;

    /*
     * Made absolute first.
     *
     * A bare relative name like "samples" has no parent path at all, so the
     * way out would silently not be offered - a folder you can enter and
     * cannot leave. Absolute paths always have one, right up to the root,
     * where parent_path() returns the root itself and the check below stops.
     */
    std::filesystem::path root = std::filesystem::absolute(directoryPath, error);
    if (error) root = std::filesystem::path(directoryPath);

    if (!std::filesystem::is_directory(root, error) || error) return;

    if (includeParent && root.has_parent_path() &&
        root.parent_path() != root) {
        BrowserEntry up;
        up.kind = BrowserEntryKind::Directory;
        up.name = "..";
        up.path = root.parent_path().string();
        out.push_back(up);
    }

    std::vector<BrowserEntry> directories;
    std::vector<BrowserEntry> files;

    auto iterator = std::filesystem::directory_iterator(
        root, std::filesystem::directory_options::skip_permission_denied, error);
    if (error) return;

    for (const auto& item : iterator) {
        std::error_code itemError;

        const std::string name = item.path().filename().string();
        if (!name.empty() && name[0] == '.') continue;   // hidden

        if (item.is_directory(itemError) && !itemError) {
            BrowserEntry entry;
            entry.kind = BrowserEntryKind::Directory;
            entry.name = name;
            entry.path = item.path().string();
            directories.push_back(entry);
            continue;
        }

        const std::string extension = item.path().extension().string();
        const bool audio = isAudioExtension(extension);
        if (!audio && !isProjectExtension(extension)) continue;

        BrowserEntry entry;
        entry.kind = BrowserEntryKind::File;
        entry.name = name;
        entry.path = item.path().string();

        const std::uintmax_t size = item.file_size(itemError);
        entry.detail = itemError ? std::string() : humanSize(size);
        files.push_back(entry);
    }

    auto byName = [](const BrowserEntry& a, const BrowserEntry& b) {
        return a.name < b.name;
    };
    std::sort(directories.begin(), directories.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    out.insert(out.end(), directories.begin(), directories.end());
    out.insert(out.end(), files.begin(), files.end());
}

// ============================================================================
// Filtering
// ============================================================================

/*
 * Narrow a list to what matches `query`, best first.
 *
 * The same fuzzy matcher as the command palette, so typing behaves the same
 * way in both places - a browser that demanded exact substrings while the
 * palette accepted initials would feel like two different programs.
 *
 * Directories are kept regardless of score when the query is empty, and
 * ranked with everything else when it is not: hiding the way out of a
 * folder because its name does not match what you are looking for is a
 * dead end.
 */
inline std::vector<BrowserEntry> filterEntries(
        const std::vector<BrowserEntry>& entries, const std::string& query) {
    if (query.empty()) return entries;

    struct Scored { int score; size_t index; };
    std::vector<Scored> scored;

    for (size_t i = 0; i < entries.size(); ++i) {
        // ".." is how you leave; it stays put whatever is typed.
        if (entries[i].name == "..") {
            scored.push_back(Scored{1 << 20, i});
            continue;
        }
        const int score = fuzzyScore(query, entries[i].name);
        if (score < 0) continue;
        scored.push_back(Scored{score, i});
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b) {
                         return a.score > b.score;
                     });

    std::vector<BrowserEntry> result;
    result.reserve(scored.size());
    for (const Scored& item : scored) result.push_back(entries[item.index]);
    return result;
}

// ============================================================================
// Panel state
// ============================================================================
struct BrowserState {
    BrowserTab tab = BrowserTab::Samples;
    char search[64] = {};

    std::string directory;              // where the Files tab is pointed
    std::vector<BrowserEntry> files;    // cached; refreshed on navigation
    bool filesScanned = false;

    int selected = -1;

    // What a double-click did last, so the panel can say so. A browser that
    // silently succeeds looks broken.
    std::string lastAction;
};

} // namespace ChiptuneTracker
