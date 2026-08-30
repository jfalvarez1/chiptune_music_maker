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

// Writes a stereo float buffer as 16-bit PCM. Shared by the mixdown export
// and the per-channel stem export, so both produce identical files.
inline bool writeWavFile(const std::string& filepath,
                         const std::vector<float>& leftBuffer,
                         const std::vector<float>& rightBuffer) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    size_t numSamples = std::min(leftBuffer.size(), rightBuffer.size());

    WavHeader header;
    header.numChannels = 2;
    header.sampleRate = 44100;
    header.bitsPerSample = 16;
    header.blockAlign = header.numChannels * header.bitsPerSample / 8;
    header.byteRate = header.sampleRate * header.blockAlign;
    header.dataSize = static_cast<uint32_t>(numSamples * header.blockAlign);
    header.fileSize = 36 + header.dataSize;

    file.write(reinterpret_cast<char*>(&header), sizeof(header));

    // Write interleaved 16-bit samples
    for (size_t i = 0; i < numSamples; ++i) {
        // Clamp and convert to 16-bit
        float l = std::max(-1.0f, std::min(1.0f, leftBuffer[i]));
        float r = std::max(-1.0f, std::min(1.0f, rightBuffer[i]));

        int16_t left16 = static_cast<int16_t>(l * 32767.0f);
        int16_t right16 = static_cast<int16_t>(r * 32767.0f);

        file.write(reinterpret_cast<char*>(&left16), sizeof(int16_t));
        file.write(reinterpret_cast<char*>(&right16), sizeof(int16_t));
    }

    const bool ok = file.good();
    file.close();
    return ok;
}

// Export the full mix to a WAV file.
inline bool exportWav(Project& project, Sequencer& seq,
                      const std::string& filepath, float durationBeats) {
    std::vector<float> leftBuffer, rightBuffer;
    if (!renderToBuffer(project, seq, leftBuffer, rightBuffer, durationBeats)) {
        return false;
    }
    return writeWavFile(filepath, leftBuffer, rightBuffer);
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
