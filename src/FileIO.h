#pragma once

/*
 * ChiptuneTracker - File I/O and Audio Export
 *
 * Handles saving/loading projects and exporting audio
 */

#include "Types.h"
#include "Sequencer.h"
#include "MIDIExport.h"
#include "ProjectValidation.h"
#include "OscillatorNames.h"
#include "ProjectSerializer.h"
#include "Generators.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

// Windows file dialogs
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#else
#include <sys/stat.h>   // mkdir, for ensureDirectoryExists
#endif

namespace ChiptuneTracker {

// ============================================================================
// Project File Format (.ctp - Chiptune Tracker Project)
// ============================================================================

// Project save/load lives in ProjectSerializer.h - the format, its field
// tables and its validation are one concern, and keeping them out of here
// stops this file from growing into a grab bag of unrelated I/O.
inline bool saveProject(const Project& project, const std::string& filepath) {
    return saveProjectFile(project, filepath);
}

inline bool loadProject(Project& project, const std::string& filepath) {
    return loadProjectFile(project, filepath);
}

// ============================================================================
// WAV Export
// ============================================================================

#pragma pack(push, 1)
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;  // PCM
    uint16_t numChannels = 2;  // Stereo
    uint32_t sampleRate = 44100;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
};
#pragma pack(pop)

// Render project to audio buffer
inline bool renderToBuffer(Project& project, Sequencer& seq,
                           std::vector<float>& leftBuffer,
                           std::vector<float>& rightBuffer,
                           float durationBeats) {
    const float sampleRate = 44100.0f;

    // Guard the inputs before they reach a size_t cast. A non-positive or
    // non-finite duration used to produce a negative float, and casting that
    // to size_t is undefined - in practice a colossal length that took the
    // process down inside resize().
    if (!std::isfinite(durationBeats) || durationBeats <= 0.0f) return false;

    float bpm = project.bpm;
    if (!std::isfinite(bpm) || bpm <= 0.0f) bpm = 120.0f;

    const float durationSeconds = durationBeats * 60.0f / bpm;

    // Cap the render so a runaway song length cannot exhaust memory.
    // 2 hours of stereo float at 44.1kHz is already far beyond any real use.
    constexpr size_t MAX_RENDER_SAMPLES = static_cast<size_t>(44100) * 60 * 60 * 2;
    const double requested = double(durationSeconds) * double(sampleRate) + 44100.0;
    if (!(requested > 0.0)) return false;

    size_t totalSamples = static_cast<size_t>(std::min(requested, double(MAX_RENDER_SAMPLES)));
    if (totalSamples == 0) return false;

    leftBuffer.resize(totalSamples);
    rightBuffer.resize(totalSamples);

    // Reset sequencer
    seq.stop();
    seq.setPosition(0.0f);
    seq.play();

    // Render in chunks
    const uint32_t chunkSize = 512;
    std::vector<float> tempLeft(chunkSize);
    std::vector<float> tempRight(chunkSize);

    size_t samplesRendered = 0;
    while (samplesRendered < totalSamples) {
        uint32_t samplesToRender = std::min(chunkSize, static_cast<uint32_t>(totalSamples - samplesRendered));

        seq.process(tempLeft.data(), tempRight.data(), samplesToRender);

        for (uint32_t i = 0; i < samplesToRender; ++i) {
            leftBuffer[samplesRendered + i] = tempLeft[i];
            rightBuffer[samplesRendered + i] = tempRight[i];
        }

        samplesRendered += samplesToRender;
    }

    seq.stop();
    return true;
}

// Creates a directory if it is not already there, including parents.
// Deliberately not std::filesystem: that crashed on this toolchain's static
// runtime when called early, and this is all the app needs.
inline bool ensureDirectoryExists(const std::string& path) {
    if (path.empty()) return true;

    std::string partial;
    partial.reserve(path.size());

    for (size_t i = 0; i <= path.size(); ++i) {
        const bool atEnd = (i == path.size());
        const char c = atEnd ? '\0' : path[i];

        if (atEnd || c == '/' || c == '\\') {
            // Skip a bare drive letter like "C:" - it always exists and
            // trying to create it fails.
            if (!partial.empty() && partial.back() != ':') {
#ifdef _WIN32
                CreateDirectoryA(partial.c_str(), nullptr);
#else
                mkdir(partial.c_str(), 0755);
#endif
            }
            if (atEnd) break;
        }
        partial += c;
    }

#ifdef _WIN32
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    return true;
#endif
}

/*
 * How much resolution a WAV keeps.
 *
 * 16 is the CD format and what every player reads. 24 is what you want if
 * the file is going anywhere else to be worked on - it has 48 dB more room
 * below the noise floor, which is the difference between a fade that ends
 * cleanly and one that ends in a staircase.
 */
enum class WavBitDepth : int { Sixteen = 16, TwentyFour = 24 };

/*
 * Writes a stereo float buffer as PCM.
 *
 * TWO THINGS CHANGED FROM WHAT THIS USED TO DO, and both were audible only
 * in quiet passages, which is where they matter most.
 *
 * It truncated toward zero rather than rounding. That is a half-LSB DC-ish
 * bias on every sample and it doubles the quantisation error, for nothing -
 * rounding costs one addition.
 *
 * And there was no dither. Quantisation error on a signal that is not
 * changing much is not noise; it is correlated with the signal, which is
 * what makes a long fade break up into audible steps instead of
 * disappearing smoothly. A triangular dither - two independent rectangular
 * values summed - decorrelates it, at the cost of a noise floor about 1.8 dB
 * higher and inaudible at 16 bits.
 *
 * Dither is applied at 16 bits and not at 24, deliberately: at 24 the
 * quantisation step is already 48 dB below anything a converter will
 * resolve, so dithering there only adds noise to a file that is on its way
 * somewhere else to be processed further.
 */
inline bool writeWavFile(const std::string& filepath,
                         const std::vector<float>& leftBuffer,
                         const std::vector<float>& rightBuffer,
                         WavBitDepth depth = WavBitDepth::Sixteen) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    const size_t numSamples = std::min(leftBuffer.size(), rightBuffer.size());
    const int bits = static_cast<int>(depth);
    const int bytesPerSample = bits / 8;

    WavHeader header;
    header.numChannels = 2;
    header.sampleRate = 44100;
    header.bitsPerSample = static_cast<uint16_t>(bits);
    header.blockAlign = static_cast<uint16_t>(header.numChannels * bytesPerSample);
    header.byteRate = header.sampleRate * header.blockAlign;
    header.dataSize = static_cast<uint32_t>(numSamples * header.blockAlign);
    header.fileSize = 36 + header.dataSize;

    file.write(reinterpret_cast<char*>(&header), sizeof(header));

    /*
     * A deterministic dither.
     *
     * Hashed from the sample index rather than drawn from a generator, so
     * exporting the same project twice produces the same file. A dither
     * that changed every run would make two exports of an unedited project
     * differ, which turns "did my change do anything" into a question
     * nobody can answer by comparing files.
     */
    auto ditherAt = [](size_t index, int channel) {
        auto hash = [](uint32_t x) {
            x ^= x >> 16; x *= 0x7FEB352Du;
            x ^= x >> 15; x *= 0x846CA68Bu;
            x ^= x >> 16;
            return x;
        };
        const uint32_t a = hash(static_cast<uint32_t>(index) * 2u +
                                static_cast<uint32_t>(channel));
        const uint32_t b = hash(a ^ 0x9E3779B9u);
        // Two rectangular values summed is a triangular distribution, which
        // is the one that decorrelates the error rather than merely masking
        // it.
        const float ra = static_cast<float>(a & 0xFFFFu) / 65535.0f - 0.5f;
        const float rb = static_cast<float>(b & 0xFFFFu) / 65535.0f - 0.5f;
        return ra + rb;
    };

    const bool dithering = (depth == WavBitDepth::Sixteen);
    const float fullScale = (depth == WavBitDepth::Sixteen) ? 32767.0f : 8388607.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        for (int channel = 0; channel < 2; ++channel) {
            const std::vector<float>& source = (channel == 0) ? leftBuffer
                                                              : rightBuffer;
            const float input = std::max(-1.0f, std::min(1.0f, source[i]));
            float value = input * fullScale;

            /*
             * Digital silence stays silent.
             *
             * A purist dither runs continuously, including over silence -
             * that is what linearises the quantiser. But a music export is
             * mostly bookended by exact zeros, and dithering those puts a
             * bed of noise under the lead-in and the tail of every file,
             * where there was none. It also means a file of silence is no
             * longer a file of silence, which anything downstream that trims
             * or detects silence would then get wrong.
             *
             * So the dither applies to signal and not to nothing. The
             * discontinuity that introduces is at the level of one LSB, four
             * places below anything the material has, and is the better
             * trade.
             */
            if (dithering && input != 0.0f) value += ditherAt(i, channel);

            // Rounded, not truncated. Truncation biases every sample toward
            // zero by up to a full step.
            long quantised = std::lround(value);
            const long limit = (depth == WavBitDepth::Sixteen) ? 32767L : 8388607L;
            quantised = std::max(-limit - 1, std::min(limit, quantised));

            if (depth == WavBitDepth::Sixteen) {
                const int16_t out = static_cast<int16_t>(quantised);
                file.write(reinterpret_cast<const char*>(&out), sizeof(int16_t));
            } else {
                // 24-bit is three bytes, little-endian, signed. There is no
                // int24_t, so it is written a byte at a time.
                const int32_t out = static_cast<int32_t>(quantised);
                const uint8_t bytes[3] = {
                    static_cast<uint8_t>(out & 0xFF),
                    static_cast<uint8_t>((out >> 8) & 0xFF),
                    static_cast<uint8_t>((out >> 16) & 0xFF),
                };
                file.write(reinterpret_cast<const char*>(bytes), 3);
            }
        }
    }

    const bool ok = file.good();
    file.close();
    return ok;
}

// Export the full mix to a WAV file.
inline bool exportWav(Project& project, Sequencer& seq,
                      const std::string& filepath, float durationBeats,
                      WavBitDepth depth = WavBitDepth::Sixteen) {
    std::vector<float> leftBuffer, rightBuffer;
    if (!renderToBuffer(project, seq, leftBuffer, rightBuffer, durationBeats)) {
        return false;
    }
    return writeWavFile(filepath, leftBuffer, rightBuffer, depth);
}

// ============================================================================
// MP3 Export (uses LAME encoder)
// ============================================================================

// Check if LAME is available on the system
inline bool isLameAvailable() {
#ifdef _WIN32
    // Try to find lame.exe in PATH
    char buffer[MAX_PATH];
    DWORD result = SearchPathA(NULL, "lame.exe", NULL, MAX_PATH, buffer, NULL);
    return result > 0;
#else
    // On Linux/Mac, check if lame is in PATH
    return system("which lame > /dev/null 2>&1") == 0;
#endif
}

// Check if FFmpeg is available on the system
inline bool isFFmpegAvailable() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD result = SearchPathA(NULL, "ffmpeg.exe", NULL, MAX_PATH, buffer, NULL);
    return result > 0;
#else
    return system("which ffmpeg > /dev/null 2>&1") == 0;
#endif
}

// Export to MP3 file (requires LAME or FFmpeg)
/*
 * FLAC, via whatever encoder is on the machine.
 *
 * Lossless, so unlike the MP3 path there is no quality setting to get wrong
 * and nothing to argue about - the file is the mix. It matters because it is
 * what an archive or a mastering hand-off wants, and because a chiptune
 * compresses extremely well: square waves are about as predictable as audio
 * gets, so a FLAC of one is often a third the size of the WAV.
 *
 * The temporary WAV is written at 24 bits. A lossless format that threw away
 * eight bits on the way in would be lossless about the wrong thing.
 *
 * No bundled encoder, for the same reason there is no bundled MP3 one:
 * miniaudio decodes FLAC and does not encode it, and vendoring libFLAC to
 * write a file most people will never export is a large dependency for a
 * small feature. If neither encoder is present this says so, plainly, rather
 * than failing silently.
 */
inline bool exportFlac(Project& project, Sequencer& seq,
                       const std::string& filepath, float durationBeats,
                       std::string* messageOut = nullptr) {
    const std::string tempWavPath = filepath + ".temp.wav";

    if (!exportWav(project, seq, tempWavPath, durationBeats,
                   WavBitDepth::TwentyFour)) {
        if (messageOut) *messageOut = "Could not render the mix.";
        return false;
    }

    bool success = false;

    if (isFFmpegAvailable()) {
        // -c:a flac is explicit rather than relying on the extension, and
        // -compression_level 8 is the highest that is still fast enough not
        // to be noticed on a song-length file.
        const std::string command =
            "ffmpeg -y -i \"" + tempWavPath + "\" -c:a flac "
            "-compression_level 8 \"" + filepath + "\" -loglevel quiet";
        success = (system(command.c_str()) == 0);
        if (messageOut) {
            *messageOut = success ? "Exported FLAC."
                                  : "ffmpeg was found but the encode failed.";
        }
    } else if (messageOut) {
        *messageOut =
            "FLAC export needs ffmpeg on the PATH. Nothing else is required - "
            "install it and this works, with no settings to choose.";
    }

#ifdef _WIN32
    DeleteFileA(tempWavPath.c_str());
#else
    std::remove(tempWavPath.c_str());
#endif

    return success;
}

inline bool exportMp3(Project& project, Sequencer& seq, const std::string& filepath,
                      float durationBeats, int bitrate = 192) {
    // First, export to a temporary WAV file
    std::string tempWavPath = filepath + ".temp.wav";

    if (!exportWav(project, seq, tempWavPath, durationBeats)) {
        return false;
    }

    bool success = false;
    std::string command;

#ifdef _WIN32
    // Windows: Try LAME first, then FFmpeg
    if (isLameAvailable()) {
        // LAME command: lame -b <bitrate> input.wav output.mp3
        command = "lame -b " + std::to_string(bitrate) + " --quiet \"" +
                  tempWavPath + "\" \"" + filepath + "\"";
        success = (system(command.c_str()) == 0);
    }
    else if (isFFmpegAvailable()) {
        // FFmpeg command: ffmpeg -i input.wav -b:a <bitrate>k output.mp3
        command = "ffmpeg -y -i \"" + tempWavPath + "\" -b:a " +
                  std::to_string(bitrate) + "k \"" + filepath + "\" -loglevel quiet";
        success = (system(command.c_str()) == 0);
    }
    else {
        // No encoder available - try using Windows PowerShell with .NET
        // This is a fallback that may work on some Windows systems
        MessageBoxA(NULL,
            "MP3 export requires LAME or FFmpeg.\n\n"
            "Please install one of the following:\n"
            "- LAME: https://lame.sourceforge.io/\n"
            "- FFmpeg: https://ffmpeg.org/\n\n"
            "Add the executable to your system PATH.",
            "MP3 Encoder Not Found", MB_OK | MB_ICONWARNING);

        // Clean up temp file
        DeleteFileA(tempWavPath.c_str());
        return false;
    }
#else
    // Linux/Mac
    if (isLameAvailable()) {
        command = "lame -b " + std::to_string(bitrate) + " --quiet \"" +
                  tempWavPath + "\" \"" + filepath + "\"";
        success = (system(command.c_str()) == 0);
    }
    else if (isFFmpegAvailable()) {
        command = "ffmpeg -y -i \"" + tempWavPath + "\" -b:a " +
                  std::to_string(bitrate) + "k \"" + filepath + "\" -loglevel quiet";
        success = (system(command.c_str()) == 0);
    }
    else {
        // Clean up and return failure
        remove(tempWavPath.c_str());
        return false;
    }
#endif

    // Clean up temporary WAV file
#ifdef _WIN32
    DeleteFileA(tempWavPath.c_str());
#else
    remove(tempWavPath.c_str());
#endif

    return success;
}

// Get MP3 encoder status message
inline std::string getMp3EncoderStatus() {
    if (isLameAvailable()) {
        return "LAME encoder available";
    }
    else if (isFFmpegAvailable()) {
        return "FFmpeg encoder available";
    }
    else {
        return "No MP3 encoder found (install LAME or FFmpeg)";
    }
}

// ============================================================================
// Windows File Dialogs
// ============================================================================

#ifdef _WIN32

inline std::string openFileDialog(const char* filter, const char* defaultExt) {
    char filename[MAX_PATH] = "";

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
}

inline std::string saveFileDialog(const char* filter, const char* defaultExt) {
    char filename[MAX_PATH] = "";

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
}

#else

// Fallback for non-Windows (just use hardcoded paths for now)
inline std::string openFileDialog(const char*, const char*) {
    return "";
}

inline std::string saveFileDialog(const char*, const char*) {
    return "";
}

#endif

// ============================================================================
// Stem Export
//
// One WAV per channel, each rendered with the other seven muted. That is
// what "stems" means to anyone who will open them: eight files that sum
// back to the mix, ready to drop into another DAW.
//
// Rendering per channel rather than tapping the mixer keeps every
// per-channel effect intact - the widener, the sidechain and the channel
// delay all behave exactly as they do in the full mix.
// ============================================================================
struct StemExportResult {
    int written = 0;
    int skipped = 0;                    // silent channels, not an error
    std::vector<std::string> failures;  // channels whose file could not be written
};

inline StemExportResult exportStems(Project& project, Sequencer& seq,
                                    const std::string& directory,
                                    float durationBeats,
                                    bool skipSilentChannels = true) {
    StemExportResult result;

    // Remember the mute and solo state so the project is untouched
    // afterwards - exporting must not quietly remix the song.
    std::array<bool, Project::MAX_CHANNELS> savedMuted{};
    std::array<bool, Project::MAX_CHANNELS> savedSolo{};
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        savedMuted[ch] = project.channels[ch].muted;
        savedSolo[ch] = project.channels[ch].solo;
    }

    std::string prefix = directory;
    if (!prefix.empty()) {
        const char last = prefix.back();
        if (last != '/' && last != '\\') prefix += '/';

        // Create the target if it does not exist. Picking a new folder in a
        // save dialog is the normal way to do this, and without it every
        // stem silently fails to write.
        ensureDirectoryExists(directory);
    }

    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        // Solo is the honest way to isolate: it goes through exactly the
        // same mixer path a listener would hear.
        for (int other = 0; other < Project::MAX_CHANNELS; ++other) {
            project.channels[other].muted = false;
            project.channels[other].solo = (other == ch);
        }
        seq.updateChannelConfigs();

        std::vector<float> left, right;
        if (!renderToBuffer(project, seq, left, right, durationBeats)) {
            result.failures.push_back("channel " + std::to_string(ch + 1) +
                                      ": render failed");
            continue;
        }

        if (skipSilentChannels) {
            float peak = 0.0f;
            for (size_t i = 0; i < left.size(); ++i) {
                peak = std::max({peak, std::fabs(left[i]), std::fabs(right[i])});
            }
            // Below this a stem is silence, and eight silent files help
            // nobody.
            if (peak < 1e-4f) {
                ++result.skipped;
                continue;
            }
        }

        // Two-digit index so the files sort in channel order.
        char filename[256];
        std::snprintf(filename, sizeof(filename), "%sstem_%02d_%s.wav",
                      prefix.c_str(), ch + 1,
                      project.channels[ch].name.empty()
                          ? "channel"
                          : project.channels[ch].name.c_str());

        // Channel names are user text and may contain path separators.
        std::string path(filename);
        for (size_t i = prefix.size(); i < path.size(); ++i) {
            const char c = path[i];
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|') {
                path[i] = '_';
            }
        }

        if (writeWavFile(path, left, right)) {
            ++result.written;
        } else {
            result.failures.push_back("channel " + std::to_string(ch + 1) +
                                      ": could not write " + path);
        }
    }

    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        project.channels[ch].muted = savedMuted[ch];
        project.channels[ch].solo = savedSolo[ch];
    }
    seq.updateChannelConfigs();

    return result;
}

// ============================================================================
// MIDI Export
// ============================================================================

inline bool exportProjectToMIDI(const Project& project, const std::string& filepath) {
    return MIDIExporter::exportToMIDI(project, filepath);
}

} // namespace ChiptuneTracker
