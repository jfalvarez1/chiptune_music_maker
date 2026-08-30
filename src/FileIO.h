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

inline std::string oscillatorTypeToString(OscillatorType type) {
    switch (type) {
        case OscillatorType::Pulse: return "Pulse";
        case OscillatorType::Triangle: return "Triangle";
        case OscillatorType::Sawtooth: return "Sawtooth";
        case OscillatorType::Sine: return "Sine";
        case OscillatorType::Noise: return "Noise";
        case OscillatorType::Supersaw: return "Supersaw";
        case OscillatorType::Custom: return "Custom";
        // Synths
        case OscillatorType::SynthLead: return "SynthLead";
        case OscillatorType::SynthPad: return "SynthPad";
        case OscillatorType::SynthBass: return "SynthBass";
        case OscillatorType::SynthPluck: return "SynthPluck";
        case OscillatorType::SynthArp: return "SynthArp";
        case OscillatorType::SynthOrgan: return "SynthOrgan";
        case OscillatorType::SynthStrings: return "SynthStrings";
        case OscillatorType::SynthBrass: return "SynthBrass";
        case OscillatorType::SynthChip: return "SynthChip";
        case OscillatorType::SynthBell: return "SynthBell";
        
        // Additional Basses
        case OscillatorType::SynthwaveBass: return "SynthwaveBass";
        case OscillatorType::AcidBass: return "AcidBass";
        case OscillatorType::SubBass808: return "SubBass808";
        
        // Synthwave
        case OscillatorType::SynthwaveLead: return "SynthwaveLead";
        case OscillatorType::SynthwavePad: return "SynthwavePad";
        case OscillatorType::SynthwaveArp: return "SynthwaveArp";
        case OscillatorType::SynthwaveChord: return "SynthwaveChord";
        case OscillatorType::SynthwaveFM: return "SynthwaveFM";
        // Techno
        case OscillatorType::TechnoStab: return "TechnoStab";
        case OscillatorType::Hoover: return "Hoover";
        case OscillatorType::RaveChord: return "RaveChord";
        case OscillatorType::Reese: return "Reese";
        // Hip Hop
        case OscillatorType::LoFiKeys: return "LoFiKeys";
        case OscillatorType::VinylNoise: return "VinylNoise";
        case OscillatorType::TrapLead: return "TrapLead";
        // Extra
        case OscillatorType::GatedPad: return "GatedPad";
        case OscillatorType::PolySynth: return "PolySynth";
        case OscillatorType::SyncLead: return "SyncLead";

        // Drums
        case OscillatorType::Kick: return "Kick";
        case OscillatorType::Kick808: return "Kick808";
        case OscillatorType::KickHard: return "KickHard";
        case OscillatorType::KickSoft: return "KickSoft";
        case OscillatorType::Snare: return "Snare";
        case OscillatorType::Snare808: return "Snare808";
        case OscillatorType::SnareRim: return "SnareRim";
        case OscillatorType::Clap: return "Clap";
        case OscillatorType::HiHat: return "HiHat";
        case OscillatorType::HiHatOpen: return "HiHatOpen";
        case OscillatorType::HiHatPedal: return "HiHatPedal";
        case OscillatorType::Tom: return "Tom";
        case OscillatorType::TomLow: return "TomLow";
        case OscillatorType::TomHigh: return "TomHigh";
        case OscillatorType::Crash: return "Crash";
        case OscillatorType::Ride: return "Ride";
        case OscillatorType::Cowbell: return "Cowbell";
        case OscillatorType::Clave: return "Clave";
        case OscillatorType::Conga: return "Conga";
        case OscillatorType::Maracas: return "Maracas";
        case OscillatorType::Tambourine: return "Tambourine";
        // Reggaeton
        case OscillatorType::ReggaetonBass: return "ReggaetonBass";
        case OscillatorType::LatinBrass: return "LatinBrass";
        case OscillatorType::Guira: return "Guira";
        case OscillatorType::Bongo: return "Bongo";
        case OscillatorType::Timbale: return "Timbale";
        case OscillatorType::Dembow808: return "Dembow808";
        case OscillatorType::DembowSnare: return "DembowSnare";
        // High-Accuracy Recreations
        case OscillatorType::Vocoder: return "Vocoder";
        case OscillatorType::KavinskyBass: return "KavinskyBass";
        default: return "Pulse";
    }
}

inline OscillatorType stringToOscillatorType(const std::string& str) {
    if (str == "Pulse") return OscillatorType::Pulse;
    if (str == "Triangle") return OscillatorType::Triangle;
    if (str == "Sawtooth") return OscillatorType::Sawtooth;
    if (str == "Sine") return OscillatorType::Sine;
    if (str == "Noise") return OscillatorType::Noise;
    if (str == "Supersaw") return OscillatorType::Supersaw;
    if (str == "Custom") return OscillatorType::Custom;
    // Synths
    if (str == "SynthLead") return OscillatorType::SynthLead;
    if (str == "SynthPad") return OscillatorType::SynthPad;
    if (str == "SynthBass") return OscillatorType::SynthBass;
    if (str == "SynthPluck") return OscillatorType::SynthPluck;
    if (str == "SynthArp") return OscillatorType::SynthArp;
    if (str == "SynthOrgan") return OscillatorType::SynthOrgan;
    if (str == "SynthStrings") return OscillatorType::SynthStrings;
    if (str == "SynthBrass") return OscillatorType::SynthBrass;
    if (str == "SynthChip") return OscillatorType::SynthChip;
    if (str == "SynthBell") return OscillatorType::SynthBell;
    
    // Additional Basses
    if (str == "SynthwaveBass") return OscillatorType::SynthwaveBass;
    if (str == "AcidBass") return OscillatorType::AcidBass;
    if (str == "SubBass808") return OscillatorType::SubBass808;
    
    // Synthwave
    if (str == "SynthwaveLead") return OscillatorType::SynthwaveLead;
    if (str == "SynthwavePad") return OscillatorType::SynthwavePad;
    if (str == "SynthwaveArp") return OscillatorType::SynthwaveArp;
    if (str == "SynthwaveChord") return OscillatorType::SynthwaveChord;
    if (str == "SynthwaveFM") return OscillatorType::SynthwaveFM;
    // Techno
    if (str == "TechnoStab") return OscillatorType::TechnoStab;
    if (str == "Hoover") return OscillatorType::Hoover;
    if (str == "RaveChord") return OscillatorType::RaveChord;
    if (str == "Reese") return OscillatorType::Reese;
    // Hip Hop
    if (str == "LoFiKeys") return OscillatorType::LoFiKeys;
    if (str == "VinylNoise") return OscillatorType::VinylNoise;
    if (str == "TrapLead") return OscillatorType::TrapLead;
    // Extra
    if (str == "GatedPad") return OscillatorType::GatedPad;
    if (str == "PolySynth") return OscillatorType::PolySynth;
    if (str == "SyncLead") return OscillatorType::SyncLead;

    // Drums
    if (str == "Kick") return OscillatorType::Kick;
    if (str == "Kick808") return OscillatorType::Kick808;
    if (str == "KickHard") return OscillatorType::KickHard;
    if (str == "KickSoft") return OscillatorType::KickSoft;
    if (str == "Snare") return OscillatorType::Snare;
    if (str == "Snare808") return OscillatorType::Snare808;
    if (str == "SnareRim") return OscillatorType::SnareRim;
    if (str == "Clap") return OscillatorType::Clap;
    if (str == "HiHat") return OscillatorType::HiHat;
    if (str == "HiHatOpen") return OscillatorType::HiHatOpen;
    if (str == "HiHatPedal") return OscillatorType::HiHatPedal;
    if (str == "Tom") return OscillatorType::Tom;
    if (str == "TomLow") return OscillatorType::TomLow;
    if (str == "TomHigh") return OscillatorType::TomHigh;
    if (str == "Crash") return OscillatorType::Crash;
    if (str == "Ride") return OscillatorType::Ride;
    if (str == "Cowbell") return OscillatorType::Cowbell;
    if (str == "Clave") return OscillatorType::Clave;
    if (str == "Conga") return OscillatorType::Conga;
    if (str == "Maracas") return OscillatorType::Maracas;
    if (str == "Tambourine") return OscillatorType::Tambourine;
    // Reggaeton
    if (str == "ReggaetonBass") return OscillatorType::ReggaetonBass;
    if (str == "LatinBrass") return OscillatorType::LatinBrass;
    if (str == "Guira") return OscillatorType::Guira;
    if (str == "Bongo") return OscillatorType::Bongo;
    if (str == "Timbale") return OscillatorType::Timbale;
    if (str == "Dembow808") return OscillatorType::Dembow808;
    if (str == "DembowSnare") return OscillatorType::DembowSnare;
    // High-Accuracy Recreations
    if (str == "Vocoder") return OscillatorType::Vocoder;
    if (str == "KavinskyBass") return OscillatorType::KavinskyBass;
    return OscillatorType::Pulse;
}

// Save project to file
inline bool saveProject(const Project& project, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "CHIPTUNE_PROJECT v1\n";
    file << "NAME " << project.name << "\n";
    file << "BPM " << project.bpm << "\n";
    file << "BEATS_PER_MEASURE " << project.beatsPerMeasure << "\n";
    file << "MASTER_VOLUME " << project.masterVolume << "\n";
    file << "SONG_LENGTH " << project.songLength << "\n";
    file << "\n";

    // Save patterns
    for (size_t p = 0; p < project.patterns.size(); ++p) {
        const Pattern& pattern = project.patterns[p];
        file << "PATTERN \"" << pattern.name << "\" " << pattern.length << "\n";

        for (const Note& note : pattern.notes) {
            file << "NOTE "
                 << note.pitch << " "
                 << std::fixed << std::setprecision(4)
                 << note.startTime << " "
                 << note.duration << " "
                 << note.velocity << " "
                 << oscillatorTypeToString(note.oscillatorType) << " "
                 << note.fadeIn << " "
                 << note.fadeOut << "\n";
        }

        file << "END_PATTERN\n\n";
    }

    file << "END_PROJECT\n";
    file.close();
    return true;
}

// Load project from file
inline bool loadProject(Project& project, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string line;

    // Read header
    std::getline(file, line);
    if (line.find("CHIPTUNE_PROJECT") == std::string::npos) {
        return false;
    }

    // Clear existing patterns
    project.patterns.clear();

    Pattern* currentPattern = nullptr;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "NAME") {
            std::getline(iss >> std::ws, project.name);
        }
        else if (cmd == "BPM") {
            iss >> project.bpm;
        }
        else if (cmd == "BEATS_PER_MEASURE") {
            iss >> project.beatsPerMeasure;
        }
        else if (cmd == "MASTER_VOLUME") {
            iss >> project.masterVolume;
        }
        else if (cmd == "SONG_LENGTH") {
            iss >> project.songLength;
        }
        else if (cmd == "PATTERN") {
            // Parse pattern name in quotes
            size_t firstQuote = line.find('"');
            size_t lastQuote = line.rfind('"');
            std::string patternName = "Pattern";
            int length = 16;

            if (firstQuote != std::string::npos && lastQuote != firstQuote) {
                patternName = line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                // Get length after closing quote
                std::string remainder = line.substr(lastQuote + 1);
                std::istringstream lenStream(remainder);
                lenStream >> length;
            }

            project.patterns.push_back(Pattern());
            currentPattern = &project.patterns.back();
            currentPattern->name = patternName;
            currentPattern->length = length;
        }
        else if (cmd == "NOTE" && currentPattern) {
            Note note;
            std::string oscTypeStr;
            iss >> note.pitch >> note.startTime >> note.duration
                >> note.velocity >> oscTypeStr >> note.fadeIn >> note.fadeOut;
            note.oscillatorType = stringToOscillatorType(oscTypeStr);
            currentPattern->notes.push_back(note);
        }
        else if (cmd == "END_PATTERN") {
            currentPattern = nullptr;
        }
        else if (cmd == "END_PROJECT") {
            break;
        }
    }

    // Ensure at least one pattern exists
    if (project.patterns.empty()) {
        project.patterns.push_back(Pattern());
        project.patterns[0].name = "Pattern 1";
    }

    // A project file is untrusted input: it may be truncated, hand-edited or
    // written by an older version. Anything that reaches the audio thread has
    // to be inside a range the mixer and sequencer can actually work with,
    // otherwise a bad number becomes silence, a hang, or a crash later on.
    clampProjectToValidRanges(project);

    file.close();
    return true;
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
