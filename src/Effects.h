#pragma once

/*
 * ChiptuneTracker - Effects Module
 *
 * All audio effects for chiptune processing.
 * Designed for zero-allocation in audio thread.
 */

#include <cmath>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace ChiptuneTracker {

constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 6.28318530718f;

// ============================================================================
// Bitcrusher - Reduce bit depth and sample rate
// ============================================================================
class Bitcrusher {
public:
    float bitDepth = 8.0f;          // 1 to 16 bits
    float sampleRateReduction = 1.0f; // 1.0 = no reduction, higher = more reduction

    float process(float input) {
        // Sample rate reduction
        m_sampleCounter += 1.0f;
        if (m_sampleCounter >= sampleRateReduction) {
            m_sampleCounter -= sampleRateReduction;
            m_heldSample = input;
        }

        // Bit depth reduction
        float steps = std::pow(2.0f, bitDepth);
        float crushed = std::round(m_heldSample * steps) / steps;

        return crushed;
    }

    void reset() {
        m_heldSample = 0.0f;
        m_sampleCounter = 0.0f;
    }

private:
    float m_heldSample = 0.0f;
    float m_sampleCounter = 0.0f;
};

// ============================================================================
// Distortion - Various saturation types
// ============================================================================
enum class DistortionType : uint8_t {
    Tanh,       // Soft clipping
    HardClip,   // Digital hard clip
    Foldback,   // Wave folding
    Asymmetric  // Tube-like asymmetric
};

class Distortion {
public:
    DistortionType type = DistortionType::Tanh;
    float drive = 1.0f;             // 1.0 to 10.0
    float mix = 1.0f;               // Dry/wet (0.0 to 1.0)

    float process(float input) {
        float driven = input * drive;
        float distorted = 0.0f;

        switch (type) {
            case DistortionType::Tanh:
                distorted = std::tanh(driven);
                break;

            case DistortionType::HardClip:
                distorted = std::max(-1.0f, std::min(1.0f, driven));
                break;

            case DistortionType::Foldback:
                // Fold signal back when it exceeds threshold
                while (driven > 1.0f || driven < -1.0f) {
                    if (driven > 1.0f) driven = 2.0f - driven;
                    if (driven < -1.0f) driven = -2.0f - driven;
                }
                distorted = driven;
                break;

            case DistortionType::Asymmetric:
                // Tube-like: soft clip positive, harder clip negative
                if (driven >= 0.0f) {
                    distorted = std::tanh(driven);
                } else {
                    distorted = std::tanh(driven * 1.5f) / 1.5f;
                }
                break;
        }

        return input * (1.0f - mix) + distorted * mix;
    }
};

// ============================================================================
// Arpeggiator - Cycle through chord notes
// ============================================================================
enum class ArpMode : uint8_t {
    Up,
    Down,
    UpDown,
    Random,
    AsPlayed
};

class Arpeggiator {
public:
    ArpMode mode = ArpMode::Up;
    float rate = 8.0f;              // Steps per beat
    int octaves = 1;                // Octave range (1-4)

    // Call this to set chord notes (semitone offsets)
    void setChord(const std::array<int, 4>& notes, int count) {
        m_chordNotes = notes;
        m_chordSize = count;
    }

    // Returns semitone offset to add to base note
    int process(float beat) {
        if (m_chordSize == 0) return 0;

        float step = beat * rate;
        int totalSteps = m_chordSize * octaves;

        int currentStep = static_cast<int>(step) % (totalSteps * 2); // For up/down

        int noteIndex, octaveOffset;

        switch (mode) {
            case ArpMode::Up:
                currentStep = static_cast<int>(step) % totalSteps;
                noteIndex = currentStep % m_chordSize;
                octaveOffset = (currentStep / m_chordSize) * 12;
                break;

            case ArpMode::Down:
                currentStep = static_cast<int>(step) % totalSteps;
                currentStep = totalSteps - 1 - currentStep;
                noteIndex = currentStep % m_chordSize;
                octaveOffset = (currentStep / m_chordSize) * 12;
                break;

            case ArpMode::UpDown:
                if (currentStep >= totalSteps) {
                    currentStep = (totalSteps * 2) - currentStep - 1;
                }
                noteIndex = currentStep % m_chordSize;
                octaveOffset = (currentStep / m_chordSize) * 12;
                break;

            default:
                noteIndex = 0;
                octaveOffset = 0;
                break;
        }

        return m_chordNotes[noteIndex] + octaveOffset;
    }

private:
    std::array<int, 4> m_chordNotes = {0, 4, 7, 12}; // Default: major chord
    int m_chordSize = 3;
};

// ============================================================================
// Vibrato - Pitch modulation
// ============================================================================
class Vibrato {
public:
    float rate = 5.0f;              // Hz
    float depth = 0.5f;             // Semitones

    // Returns pitch multiplier
    float process(float time) {
        float lfo = std::sin(time * rate * TWO_PI);
        float semitones = lfo * depth;
        return std::pow(2.0f, semitones / 12.0f);
    }
};

// ============================================================================
// Tremolo - Volume modulation
// ============================================================================
class Tremolo {
public:
    float rate = 4.0f;              // Hz
    float depth = 0.5f;             // 0.0 to 1.0

    float process(float time) {
        float lfo = std::sin(time * rate * TWO_PI);
        return 1.0f - depth * 0.5f * (lfo + 1.0f);
    }
};

// ============================================================================
// Delay - Echo effect (heap-allocated to avoid stack overflow)
// ============================================================================
class Delay {
public:
    static constexpr int MAX_DELAY_SAMPLES = 44100; // 1 second at 44.1kHz

    float delayTime = 0.25f;        // Seconds
    float feedback = 0.4f;          // 0.0 to 0.95
    float mix = 0.3f;               // Dry/wet

    Delay() : m_buffer(MAX_DELAY_SAMPLES, 0.0f) {}

    void setSampleRate(float sr) {
        m_sampleRate = sr;
    }

    float process(float input) {
        int delaySamples = static_cast<int>(delayTime * m_sampleRate);
        delaySamples = std::min(delaySamples, MAX_DELAY_SAMPLES - 1);

        // Read from delay buffer
        int readIndex = (m_writeIndex - delaySamples + MAX_DELAY_SAMPLES) % MAX_DELAY_SAMPLES;
        float delayed = m_buffer[readIndex];

        // Write to delay buffer (input + feedback)
        m_buffer[m_writeIndex] = input + delayed * feedback;
        m_writeIndex = (m_writeIndex + 1) % MAX_DELAY_SAMPLES;

        return input * (1.0f - mix) + delayed * mix;
    }

    void reset() {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        m_writeIndex = 0;
    }

private:
    std::vector<float> m_buffer;
    int m_writeIndex = 0;
    float m_sampleRate = 44100.0f;
};

// ============================================================================
// Filter - Low/High pass resonant filter
// ============================================================================
enum class FilterType : uint8_t {
    LowPass,
    HighPass,
    BandPass
};

class Filter {
public:
    FilterType type = FilterType::LowPass;
    float cutoff = 1000.0f;         // Hz
    float resonance = 0.5f;         // 0.0 to 1.0

    void setSampleRate(float sr) {
        m_sampleRate = sr;
        updateCoefficients();
    }

    void setCutoff(float freq) {
        cutoff = freq;
        updateCoefficients();
    }

    float process(float input) {
        // State variable filter
        float highpass = input - m_lowpass - m_bandpass * m_q;
        m_bandpass += m_f * highpass;
        m_lowpass += m_f * m_bandpass;

        switch (type) {
            case FilterType::LowPass:  return m_lowpass;
            case FilterType::HighPass: return highpass;
            case FilterType::BandPass: return m_bandpass;
        }
        return input;
    }

    void reset() {
        m_lowpass = 0.0f;
        m_bandpass = 0.0f;
    }

private:
    void updateCoefficients() {
        m_f = 2.0f * std::sin(PI * cutoff / m_sampleRate);
        m_q = 1.0f - resonance * 0.9f; // Prevent self-oscillation
    }

    float m_sampleRate = 44100.0f;
    float m_f = 0.1f;
    float m_q = 0.5f;
    float m_lowpass = 0.0f;
    float m_bandpass = 0.0f;
};

// ============================================================================
// Chorus - Detuned doubling effect (heap-allocated buffer)
// ============================================================================
class Chorus {
public:
    static constexpr int MAX_CHORUS_SAMPLES = 4410; // 100ms at 44.1kHz

    float rate = 0.5f;              // LFO Hz
    float depth = 0.005f;           // Delay modulation (seconds)
    float mix = 0.5f;

    Chorus() : m_buffer(MAX_CHORUS_SAMPLES, 0.0f) {}

    void setSampleRate(float sr) {
        m_sampleRate = sr;
    }

    float process(float input, float time) {
        // LFO modulates delay time
        float lfo = std::sin(time * rate * TWO_PI);
        float modulatedDelay = 0.01f + depth * (lfo + 1.0f);

        int delaySamples = static_cast<int>(modulatedDelay * m_sampleRate);
        delaySamples = std::min(delaySamples, MAX_CHORUS_SAMPLES - 1);

        // Read from buffer
        int readIndex = (m_writeIndex - delaySamples + MAX_CHORUS_SAMPLES) % MAX_CHORUS_SAMPLES;
        float delayed = m_buffer[readIndex];

        // Write to buffer
        m_buffer[m_writeIndex] = input;
        m_writeIndex = (m_writeIndex + 1) % MAX_CHORUS_SAMPLES;

        return input * (1.0f - mix) + delayed * mix;
    }

    void reset() {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        m_writeIndex = 0;
    }

private:
    std::vector<float> m_buffer;
    int m_writeIndex = 0;
    float m_sampleRate = 44100.0f;
};

// ============================================================================
// Ring Modulator - Multiply with carrier frequency
// ============================================================================
class RingModulator {
public:
    float frequency = 440.0f;       // Carrier frequency
    float mix = 0.5f;

    float process(float input, float time) {
        float carrier = std::sin(time * frequency * TWO_PI);
        float modulated = input * carrier;
        return input * (1.0f - mix) + modulated * mix;
    }
};

// ============================================================================
// Phaser - All-pass filter sweep
// ============================================================================
class Phaser {
public:
    float rate = 0.3f;              // LFO Hz
    float depth = 0.7f;
    float feedback = 0.5f;
    int stages = 4;                 // Number of all-pass stages

    float process(float input, float time) {
        float lfo = std::sin(time * rate * TWO_PI);
        float modulation = 0.1f + depth * 0.4f * (lfo + 1.0f);

        float output = input + m_feedback * feedback;

        for (int i = 0; i < stages && i < 8; ++i) {
            // Simple all-pass filter
            float coef = (1.0f - modulation) / (1.0f + modulation);
            float newOutput = coef * (output - m_allpass[i]) + m_delay[i];
            m_delay[i] = output;
            m_allpass[i] = newOutput;
            output = newOutput;
        }

        m_feedback = output;
        return (input + output) * 0.5f;
    }

    void reset() {
        m_allpass.fill(0.0f);
        m_delay.fill(0.0f);
        m_feedback = 0.0f;
    }

private:
    std::array<float, 8> m_allpass = {};
    std::array<float, 8> m_delay = {};
    float m_feedback = 0.0f;
};

// ============================================================================
// Effects Chain - Combines all effects for a channel
// ============================================================================
struct EffectsChain {
    // Effect instances
    Bitcrusher bitcrusher;
    Distortion distortion;
    Filter filter;
    Delay delay;
    Chorus chorus;
    Tremolo tremolo;
    Phaser phaser;
    RingModulator ringMod;

    // Enable flags
    bool bitcrusherEnabled = false;
    bool distortionEnabled = false;
    bool filterEnabled = false;
    bool delayEnabled = false;
    bool chorusEnabled = false;
    bool tremoloEnabled = false;
    bool phaserEnabled = false;
    bool ringModEnabled = false;

    void setSampleRate(float sr) {
        filter.setSampleRate(sr);
        delay.setSampleRate(sr);
        chorus.setSampleRate(sr);
    }

    float process(float input, float time) {
        float output = input;

        // Process in order: saturation -> filter -> modulation -> time-based
        if (bitcrusherEnabled) output = bitcrusher.process(output);
        if (distortionEnabled) output = distortion.process(output);
        if (filterEnabled)     output = filter.process(output);
        if (ringModEnabled)    output = ringMod.process(output, time);
        if (tremoloEnabled)    output *= tremolo.process(time);
        if (phaserEnabled)     output = phaser.process(output, time);
        if (chorusEnabled)     output = chorus.process(output, time);
        if (delayEnabled)      output = delay.process(output);

        return output;
    }

    void reset() {
        bitcrusher.reset();
        filter.reset();
        delay.reset();
        chorus.reset();
        phaser.reset();
    }
};

} // namespace ChiptuneTracker
