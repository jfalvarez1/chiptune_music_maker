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
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

// Windows file dialogs
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
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

// Export to WAV file
inline bool exportWav(Project& project, Sequencer& seq, const std::string& filepath, float durationBeats) {
    std::vector<float> leftBuffer, rightBuffer;

    if (!renderToBuffer(project, seq, leftBuffer, rightBuffer, durationBeats)) {
        return false;
    }

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    size_t numSamples = leftBuffer.size();

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

    file.close();
    return true;
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
// MIDI Export
// ============================================================================

inline bool exportProjectToMIDI(const Project& project, const std::string& filepath) {
    return MIDIExporter::exportToMIDI(project, filepath);
}

} // namespace ChiptuneTracker
