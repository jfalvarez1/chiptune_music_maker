#define NOMINMAX
#define MA_ENCODING_ENABLED
#define MINIAUDIO_IMPLEMENTATION
#include "../vendor/miniaudio/miniaudio.h"

#include "AudioRecorder.h" // For AudioRecorder::saveToWav declaration
#include <fstream>         // For std::ofstream
#include <string>          // For std::string
#include <vector>          // For std::vector
#include <cstdint>         // For int16_t, int32_t
#include <algorithm>       // For std::min, std::max
#include <iostream>        // For std::cerr

// WAV header struct
#pragma pack(push, 1)
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;  // PCM
    uint16_t numChannels = 1;  // Mono
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
};
#pragma pack(pop)

// Implementation of AudioRecorder::saveToWav (Manual WAV writer)
bool AudioRecorder::saveToWav(const std::string& filepath, int sampleRate, const std::vector<float>& samples) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "saveToWav: Failed to open file " << filepath << std::endl;
        return false;
    }

    if (samples.empty()) {
        std::cerr << "saveToWav: Samples vector is empty for file " << filepath << std::endl;
        file.close();
        return false;
    }

    WavHeader header;
    header.sampleRate = sampleRate;
    header.numChannels = 1; // Assuming mono
    header.bitsPerSample = 16;
    header.blockAlign = header.numChannels * header.bitsPerSample / 8;
    header.byteRate = header.sampleRate * header.blockAlign;
    header.dataSize = static_cast<uint32_t>(samples.size() * header.blockAlign);
    header.fileSize = 36 + header.dataSize;

    std::cerr << "saveToWav: Writing WAV header (size: " << sizeof(WavHeader) << ", dataSize: " << header.dataSize << ", sampleRate: " << header.sampleRate << ", samples.size: " << samples.size() << ")" << std::endl;
    file.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
    if (!file.good()) {
        std::cerr << "saveToWav: Error writing header to file " << filepath << std::endl;
        file.close();
        return false;
    }

    for (float s : samples) {
        // Convert float [-1, 1] to int16_t
        int16_t sample16 = static_cast<int16_t>((std::max)(-1.0f, (std::min)(1.0f, s)) * 32767.0f);
        file.write(reinterpret_cast<const char*>(&sample16), sizeof(int16_t));
    }
    if (!file.good()) {
        std::cerr << "saveToWav: Error writing samples to file " << filepath << std::endl;
        file.close();
        return false;
    }

    std::cerr << "saveToWav: Successfully wrote " << samples.size() << " samples to " << filepath << std::endl;
    file.close();
    return true;
}
