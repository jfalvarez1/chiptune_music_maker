#pragma once

/*
 * ChiptuneTracker - Sample Management
 *
 * Handles loading, storing, and managing audio samples
 * - WAV, MP3, OGG support via miniaudio
 * - Pitch-shifting playback
 * - Loop points
 * - Sample pool management
 */

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

// miniaudio is already included in main.cpp with MINIAUDIO_IMPLEMENTATION
// We just need the header here
#include "../vendor/miniaudio/miniaudio.h"

namespace ChiptuneTracker {

// ============================================================================
// Sample - Single audio sample (one-shot or loop)
// ============================================================================
struct Sample {
    std::string name;
    std::string filepath;

    std::vector<float> audioData; // Mono audio (interleaved stereo converted to mono)
    int sampleRate = 48000;
    float lengthSeconds = 0.0f;

    // Playback settings
    bool loop = false;
    float loopStartSeconds = 0.0f;
    float loopEndSeconds = 0.0f; // 0 = end of sample

    // Pitch settings
    int rootNote = 60; // C4 - for pitch-shifting

    // Metadata
    bool isLoaded = false;

    Sample() = default;
    Sample(const std::string& n, const std::string& fp)
        : name(n), filepath(fp) {}
};

// ============================================================================
// SamplePool - Manages all loaded samples
// ============================================================================
class SamplePool {
public:
    static constexpr int MAX_SAMPLES = 256;

    /*
     * The capacity is reserved once, up front, and never grows.
     *
     * The audio thread reads m_samples[id] while the UI thread can be
     * loading a new sample. Without this reserve a push_back would
     * reallocate the outer vector out from under the reader - a
     * use-after-free in the audio callback, which is the worst place to
     * have one. Reserving costs a few KB of headers; the audio data itself
     * is owned by each Sample.
     */
    SamplePool() { m_samples.reserve(MAX_SAMPLES); }

    // Load a sample from file (WAV, MP3, OGG)
    int loadSample(const std::string& filepath) {
        // Check if already loaded
        for (size_t i = 0; i < m_samples.size(); i++) {
            if (m_samples[i].filepath == filepath) {
                return (int)i; // Already loaded
            }
        }

        // Check max samples
        if (m_samples.size() >= MAX_SAMPLES) {
            return -1; // Too many samples
        }

        // Load using miniaudio decoder
        ma_decoder decoder;
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000); // Mono, 48kHz

        if (ma_decoder_init_file(filepath.c_str(), &config, &decoder) != MA_SUCCESS) {
            return -1; // Failed to load
        }

        Sample sample;
        sample.filepath = filepath;
        sample.sampleRate = decoder.outputSampleRate;

        // Extract filename from path
        size_t lastSlash = filepath.find_last_of("/\\");
        sample.name = (lastSlash != std::string::npos) ?
                      filepath.substr(lastSlash + 1) : filepath;

        // Read entire file into buffer
        ma_uint64 frameCount = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);

        if (frameCount == 0 || frameCount > 48000 * 60 * 5) { // Max 5 minutes
            ma_decoder_uninit(&decoder);
            return -1;
        }

        sample.audioData.resize((size_t)frameCount);
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, sample.audioData.data(), frameCount, &framesRead);

        sample.lengthSeconds = (float)framesRead / (float)sample.sampleRate;
        sample.loopEndSeconds = sample.lengthSeconds;
        sample.isLoaded = true;

        // Normalize audio to prevent clipping
        float maxAbs = 0.0f;
        for (float s : sample.audioData) {
            maxAbs = std::max(maxAbs, std::abs(s));
        }
        if (maxAbs > 0.001f) {
            float normFactor = 0.95f / maxAbs;
            for (float& s : sample.audioData) {
                s *= normFactor;
            }
        }

        ma_decoder_uninit(&decoder);

        m_samples.push_back(sample);
        return (int)m_samples.size() - 1;
    }

    // Get sample by ID
    int count() const { return static_cast<int>(m_samples.size()); }

    void renameSample(int id, const std::string& name) {
        if (id >= 0 && id < static_cast<int>(m_samples.size())) {
            m_samples[static_cast<size_t>(id)].name = name;
        }
    }

    // Register audio that came from somewhere other than a file - a
    // recording, or a test fixture. Returns the id, or -1 when full.
    int addSample(const Sample& sample) {
        if (static_cast<int>(m_samples.size()) >= MAX_SAMPLES) return -1;
        m_samples.push_back(sample);
        return static_cast<int>(m_samples.size()) - 1;
    }

    const Sample* getSample(int id) const {
        if (id >= 0 && id < (int)m_samples.size()) {
            return &m_samples[id];
        }
        return nullptr;
    }

    // Get mutable sample for editing settings
    Sample* getSampleMutable(int id) {
        if (id >= 0 && id < (int)m_samples.size()) {
            return &m_samples[id];
        }
        return nullptr;
    }

    // Get all samples
    const std::vector<Sample>& getAllSamples() const {
        return m_samples;
    }

    // Remove sample
    void removeSample(int id) {
        if (id >= 0 && id < (int)m_samples.size()) {
            m_samples.erase(m_samples.begin() + id);
        }
    }

    // Clear all samples
    void clear() {
        m_samples.clear();
    }

    // Get sample count
    int getCount() const {
        return (int)m_samples.size();
    }

private:
    std::vector<Sample> m_samples;
};

// ============================================================================
// SampleOscillator - Plays back samples with pitch-shifting
// ============================================================================
class SampleOscillator {
public:
    /*
     * Start a sample.
     *
     * `engineRate` is the rate the mixer runs at, and leaving it out was a
     * real, shipping bug: the read step was the pitch ratio alone, with no
     * term for the difference between the sample's rate and the engine's.
     * The pool decodes everything to 48 kHz and the engine usually runs at
     * 44.1, so every sample played 8.8% fast - nearly a semitone and a half
     * sharp, and every sample instrument was out of tune with the synths.
     *
     * It defaults to 44100 so existing call sites keep compiling, but a
     * caller that knows its rate should pass it.
     */
    void trigger(const Sample* sample, int midiNote, float velocity,
                 float engineRate = 44100.0f) {
        m_sample = sample;
        m_position = 0.0;
        m_playing = true;
        m_velocity = velocity;

        if (!sample || !sample->isLoaded) {
            m_playing = false;
            return;
        }

        // Calculate pitch shift ratio
        float semitones = (float)midiNote - (float)sample->rootNote;
        const float pitchRatio = std::pow(2.0f, semitones / 12.0f);

        const float rateRatio = (engineRate > 0.0f)
            ? (float)sample->sampleRate / engineRate : 1.0f;
        m_pitchRatio = pitchRatio * rateRatio;
    }

    void release() {
        if (m_sample && !m_sample->loop) {
            // For non-looping samples, let them finish naturally
            // We could add ADSR envelope here if needed
        }
    }

    float process() {
        if (!m_playing || !m_sample || !m_sample->isLoaded) {
            return 0.0f;
        }

        // Check bounds
        if (m_position >= m_sample->audioData.size()) {
            if (m_sample->loop) {
                // Loop back to loop start
                int loopStartSample = (int)(m_sample->loopStartSeconds * m_sample->sampleRate);
                m_position = (double)loopStartSample;
            } else {
                m_playing = false;
                return 0.0f;
            }
        }

        // Linear interpolation for pitch-shifted playback
        int pos = (int)m_position;
        float frac = (float)(m_position - pos);

        if (pos >= (int)m_sample->audioData.size() - 1) {
            if (m_sample->loop) {
                int loopStartSample = (int)(m_sample->loopStartSeconds * m_sample->sampleRate);
                m_position = (double)loopStartSample;
                pos = (int)m_position;
                frac = (float)(m_position - pos);
            } else {
                m_playing = false;
                return 0.0f;
            }
        }

        float s0 = m_sample->audioData[pos];
        float s1 = m_sample->audioData[pos + 1];
        float output = s0 + frac * (s1 - s0);

        m_position += m_pitchRatio; // Advance playback position

        return output * m_velocity;
    }

    bool isPlaying() const {
        return m_playing;
    }

    void stop() {
        m_playing = false;
    }

private:
    const Sample* m_sample = nullptr;
    double m_position = 0.0;
    float m_pitchRatio = 1.0f;
    float m_velocity = 1.0f;
    bool m_playing = false;
};

} // namespace ChiptuneTracker
