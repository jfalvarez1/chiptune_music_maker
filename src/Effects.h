#pragma once

/*
 * ChiptuneTracker - Effects Module
 *
 * All audio effects for chiptune processing.
 * Designed for zero-allocation in audio thread.
 */

#include "Types.h"
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace ChiptuneTracker {

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
// Flanger - Short delay modulation for jet-plane/swoosh effect
// ============================================================================
class Flanger {
public:
    float rate = 0.5f;          // LFO rate (Hz) - 0.1 to 10
    float depth = 0.005f;       // Delay modulation depth (seconds) - 0.001 to 0.01
    float feedback = 0.5f;      // Feedback amount (-0.95 to +0.95)
    float mix = 0.5f;           // Dry/wet mix (0.0 to 1.0)

    void init(int sampleRate) {
        m_sampleRate = sampleRate;
        m_writePos = 0;
        m_delayBuffer.fill(0.0f);
        m_phase = 0.0f;
    }

    float process(float input) {
        // LFO for delay modulation
        float lfo = std::sin(m_phase * TWO_PI);
        m_phase += rate / m_sampleRate;
        if (m_phase >= 1.0f) m_phase -= 1.0f;

        // Variable delay time (1-10ms typically for flanger)
        float delayTime = 0.001f + depth * (lfo + 1.0f) * 0.5f;
        int delaySamples = (int)(delayTime * m_sampleRate);
        delaySamples = std::clamp(delaySamples, 1, BUFFER_SIZE - 1);

        // Read from delay buffer with linear interpolation
        int readPos = (m_writePos - delaySamples + BUFFER_SIZE) % BUFFER_SIZE;
        float delayed = m_delayBuffer[readPos];

        // Write to delay buffer with feedback
        m_delayBuffer[m_writePos] = input + delayed * feedback;
        m_writePos = (m_writePos + 1) % BUFFER_SIZE;

        // Mix dry and wet signals
        return input * (1.0f - mix) + delayed * mix;
    }

    void reset() {
        m_delayBuffer.fill(0.0f);
        m_writePos = 0;
        m_phase = 0.0f;
    }

private:
    // A flanger only ever needs a few milliseconds of delay (1ms + depth, where
    // depth maxes out at 10ms). 8192 samples is ~170ms at 48kHz - far more than
    // enough, and small enough that eight of these fit comfortably in memory.
    // (This was 96000 floats = 384KB per instance, which overflowed the stack
    //  once eight EffectsChains were held by value in the Sequencer.)
    static constexpr int BUFFER_SIZE = 8192;
    std::array<float, BUFFER_SIZE> m_delayBuffer = {};
    int m_writePos = 0;
    float m_phase = 0.0f;
    int m_sampleRate = 48000;
};

// ============================================================================
// Reverb - Schroeder-style algorithmic reverb for spacious sound
// ============================================================================
class Reverb {
public:
    static constexpr int MAX_COMB_SIZE = 4410;    // 100ms at 44.1kHz
    static constexpr int MAX_ALLPASS_SIZE = 1764; // 40ms at 44.1kHz
    static constexpr int NUM_COMBS = 8;
    static constexpr int NUM_ALLPASS = 4;

    float roomSize = 0.7f;      // 0.0 to 1.0 (small to large room)
    float damping = 0.4f;       // 0.0 to 1.0 (bright to dark)
    float mix = 0.35f;          // Dry/wet (0.0 = dry, 1.0 = full wet)
    float width = 1.0f;         // Stereo width (0.0 = mono, 1.0 = full stereo)
    float predelay = 0.02f;     // Pre-delay in seconds (room size simulation)

    Reverb() {
        // Initialize comb filters with prime-number-based delays for richness
        // These values create a dense, natural-sounding reverb
        m_combDelays = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116};
        m_allpassDelays = {225, 556, 441, 341};

        for (int i = 0; i < NUM_COMBS; ++i) {
            m_combBuffers[i].resize(MAX_COMB_SIZE, 0.0f);
            m_combFilters[i] = 0.0f;
        }
        for (int i = 0; i < NUM_ALLPASS; ++i) {
            m_allpassBuffers[i].resize(MAX_ALLPASS_SIZE, 0.0f);
        }
        m_predelayBuffer.resize(static_cast<int>(0.1f * 44100.0f), 0.0f);
    }

    void setSampleRate(float sr) {
        m_sampleRate = sr;
        // Rescale delays for sample rate
        float ratio = sr / 44100.0f;
        m_scaledCombDelays = {
            static_cast<int>(1557 * ratio), static_cast<int>(1617 * ratio),
            static_cast<int>(1491 * ratio), static_cast<int>(1422 * ratio),
            static_cast<int>(1277 * ratio), static_cast<int>(1356 * ratio),
            static_cast<int>(1188 * ratio), static_cast<int>(1116 * ratio)
        };
        m_scaledAllpassDelays = {
            static_cast<int>(225 * ratio), static_cast<int>(556 * ratio),
            static_cast<int>(441 * ratio), static_cast<int>(341 * ratio)
        };
        m_predelayBuffer.resize(static_cast<int>(0.1f * sr), 0.0f);
    }

    // Process mono input, returns stereo pair
    std::pair<float, float> processStereo(float input) {
        // Pre-delay
        int predelaySamples = static_cast<int>(predelay * m_sampleRate);
        predelaySamples = std::min(predelaySamples, static_cast<int>(m_predelayBuffer.size()) - 1);

        int predelayReadIdx = (m_predelayWriteIdx - predelaySamples + m_predelayBuffer.size()) % m_predelayBuffer.size();
        float predelayed = m_predelayBuffer[predelayReadIdx];
        m_predelayBuffer[m_predelayWriteIdx] = input;
        m_predelayWriteIdx = (m_predelayWriteIdx + 1) % m_predelayBuffer.size();

        // Process through parallel comb filters
        float combOutL = 0.0f;
        float combOutR = 0.0f;
        float feedback = roomSize * 0.85f + 0.1f;  // Scale to useful range

        for (int i = 0; i < NUM_COMBS; ++i) {
            int delay = m_scaledCombDelays[i];
            delay = std::min(delay, MAX_COMB_SIZE - 1);

            int readIdx = (m_combWriteIdx[i] - delay + MAX_COMB_SIZE) % MAX_COMB_SIZE;
            float delayed = m_combBuffers[i][readIdx];

            // Lowpass filter in feedback path for damping (darker = more damping)
            m_combFilters[i] = delayed * (1.0f - damping) + m_combFilters[i] * damping;

            // Write with feedback
            m_combBuffers[i][m_combWriteIdx[i]] = predelayed + m_combFilters[i] * feedback;
            m_combWriteIdx[i] = (m_combWriteIdx[i] + 1) % MAX_COMB_SIZE;

            // Distribute to stereo (alternating L/R with some mixing)
            if (i % 2 == 0) {
                combOutL += delayed;
                combOutR += delayed * 0.6f;
            } else {
                combOutR += delayed;
                combOutL += delayed * 0.6f;
            }
        }

        combOutL /= NUM_COMBS;
        combOutR /= NUM_COMBS;

        // Process through series allpass filters for diffusion
        float allpassOut = (combOutL + combOutR) * 0.5f;
        for (int i = 0; i < NUM_ALLPASS; ++i) {
            int delay = m_scaledAllpassDelays[i];
            delay = std::min(delay, MAX_ALLPASS_SIZE - 1);

            int readIdx = (m_allpassWriteIdx[i] - delay + MAX_ALLPASS_SIZE) % MAX_ALLPASS_SIZE;
            float delayed = m_allpassBuffers[i][readIdx];

            float temp = -allpassOut * 0.5f + delayed;
            m_allpassBuffers[i][m_allpassWriteIdx[i]] = allpassOut + delayed * 0.5f;
            m_allpassWriteIdx[i] = (m_allpassWriteIdx[i] + 1) % MAX_ALLPASS_SIZE;

            allpassOut = temp;
        }

        // Apply stereo width
        float wetL = combOutL * width + allpassOut * (1.0f - width * 0.5f);
        float wetR = combOutR * width + allpassOut * (1.0f - width * 0.5f);

        // Mix dry and wet
        float outL = input * (1.0f - mix) + wetL * mix;
        float outR = input * (1.0f - mix) + wetR * mix;

        return {outL, outR};
    }

    // Simple mono process (averages stereo output)
    float process(float input) {
        auto [left, right] = processStereo(input);
        return (left + right) * 0.5f;
    }

    void reset() {
        for (int i = 0; i < NUM_COMBS; ++i) {
            std::fill(m_combBuffers[i].begin(), m_combBuffers[i].end(), 0.0f);
            m_combFilters[i] = 0.0f;
            m_combWriteIdx[i] = 0;
        }
        for (int i = 0; i < NUM_ALLPASS; ++i) {
            std::fill(m_allpassBuffers[i].begin(), m_allpassBuffers[i].end(), 0.0f);
            m_allpassWriteIdx[i] = 0;
        }
        std::fill(m_predelayBuffer.begin(), m_predelayBuffer.end(), 0.0f);
        m_predelayWriteIdx = 0;
    }

private:
    float m_sampleRate = 44100.0f;

    // Comb filters (parallel)
    std::array<std::vector<float>, NUM_COMBS> m_combBuffers;
    std::array<float, NUM_COMBS> m_combFilters = {};
    std::array<int, NUM_COMBS> m_combWriteIdx = {};
    std::array<int, NUM_COMBS> m_combDelays;
    std::array<int, NUM_COMBS> m_scaledCombDelays;

    // Allpass filters (series)
    std::array<std::vector<float>, NUM_ALLPASS> m_allpassBuffers;
    std::array<int, NUM_ALLPASS> m_allpassWriteIdx = {};
    std::array<int, NUM_ALLPASS> m_allpassDelays;
    std::array<int, NUM_ALLPASS> m_scaledAllpassDelays;

    // Pre-delay buffer
    std::vector<float> m_predelayBuffer;
    int m_predelayWriteIdx = 0;
};

// ============================================================================
// Stereo Widener - Creates wide stereo image for lush synthwave pads
// Uses Haas effect and mid/side processing
// ============================================================================
class StereoWidener {
public:
    static constexpr int MAX_DELAY_SAMPLES = 2205;  // 50ms at 44.1kHz

    float width = 0.5f;              // 0.0 = mono, 1.0 = ultra wide
    float haasDelay = 0.015f;        // Haas effect delay in seconds (10-30ms)
    float mix = 0.5f;                // Dry/wet

    StereoWidener() : m_buffer(MAX_DELAY_SAMPLES, 0.0f) {}

    void setSampleRate(float sr) {
        m_sampleRate = sr;
    }

    // Process mono input to stereo output
    std::pair<float, float> process(float input) {
        // Haas delay for one channel
        int delaySamples = static_cast<int>(haasDelay * m_sampleRate);
        delaySamples = std::min(delaySamples, MAX_DELAY_SAMPLES - 1);

        int readIdx = (m_writeIdx - delaySamples + MAX_DELAY_SAMPLES) % MAX_DELAY_SAMPLES;
        float delayed = m_buffer[readIdx];

        m_buffer[m_writeIdx] = input;
        m_writeIdx = (m_writeIdx + 1) % MAX_DELAY_SAMPLES;

        // Mid/Side processing
        float mid = input;
        float side = (input - delayed) * width;

        // Convert back to L/R
        float left = mid + side;
        float right = mid - side;

        // Apply mix
        float dryL = input;
        float dryR = input;

        return {
            dryL * (1.0f - mix) + left * mix,
            dryR * (1.0f - mix) + right * mix
        };
    }

    void reset() {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        m_writeIdx = 0;
    }

private:
    std::vector<float> m_buffer;
    int m_writeIdx = 0;
    float m_sampleRate = 44100.0f;
};

// ============================================================================
// Tape Saturation - Warm analog character (classic 80s tape sound)
// Models tape compression and harmonic saturation
// ============================================================================
class TapeSaturation {
public:
    float drive = 1.5f;              // 1.0 = clean, 3.0 = heavily saturated
    float warmth = 0.5f;             // High frequency roll-off (0.0-1.0)
    float compression = 0.3f;        // Soft compression amount
    float mix = 0.5f;                // Dry/wet

    void setSampleRate(float sr) {
        m_sampleRate = sr;
        // Update filter coefficient for warmth
        float freq = 8000.0f - warmth * 5000.0f;  // 8kHz to 3kHz rolloff
        m_filterCoef = 1.0f - std::exp(-TWO_PI * freq / sr);
    }

    float process(float input) {
        // Tape compression (soft knee)
        float compressed = input;
        float threshold = 0.5f;
        if (std::abs(compressed) > threshold) {
            float over = std::abs(compressed) - threshold;
            float reduction = over * compression;
            compressed = (compressed > 0 ? 1 : -1) * (threshold + over - reduction);
        }

        // Apply drive
        float driven = compressed * drive;

        // Tape saturation curve (asymmetric for even harmonics)
        float saturated;
        if (driven >= 0) {
            // Positive half: gentler saturation
            saturated = std::tanh(driven * 0.9f);
        } else {
            // Negative half: slightly harder (adds even harmonics)
            saturated = std::tanh(driven * 1.1f) * 0.95f;
        }

        // Add subtle 2nd harmonic (tape characteristic)
        saturated += std::sin(input * PI) * 0.05f * drive;

        // Warmth filter (lowpass)
        m_filterState = m_filterState + m_filterCoef * (saturated - m_filterState);
        float warm = m_filterState * warmth + saturated * (1.0f - warmth);

        // Normalize output level
        warm *= 0.7f / std::max(0.5f, drive * 0.5f);

        return input * (1.0f - mix) + warm * mix;
    }

    void reset() {
        m_filterState = 0.0f;
    }

private:
    float m_sampleRate = 44100.0f;
    float m_filterCoef = 0.1f;
    float m_filterState = 0.0f;
};

// ============================================================================
// Unison - Multiple detuned voices for thick synthwave sound
// ============================================================================
class Unison {
public:
    int voices = 5;                  // Number of unison voices (1-7)
    float detune = 0.15f;            // Detune amount in semitones (0.0-0.5)
    float stereoSpread = 0.7f;       // How wide to spread voices (0.0-1.0)

    // Calculate detune multiplier for each voice
    // Returns array of {leftGain, rightGain, pitchMultiplier} for each voice
    struct VoiceParams {
        float leftGain;
        float rightGain;
        float pitchMult;
    };

    std::array<VoiceParams, 7> getVoiceParams() const {
        std::array<VoiceParams, 7> params = {};
        int numVoices = std::min(7, std::max(1, voices));

        for (int i = 0; i < numVoices; ++i) {
            // Spread voices evenly from -detune to +detune
            float detuneOffset;
            float panPos;

            if (numVoices == 1) {
                detuneOffset = 0.0f;
                panPos = 0.0f;
            } else {
                // Voice position from -1 to 1
                float pos = (static_cast<float>(i) / (numVoices - 1)) * 2.0f - 1.0f;
                detuneOffset = pos * detune;
                panPos = pos * stereoSpread;
            }

            // Convert semitone detune to pitch multiplier
            params[i].pitchMult = std::pow(2.0f, detuneOffset / 12.0f);

            // Convert pan position to left/right gains (constant power)
            float pan01 = (panPos + 1.0f) * 0.5f;  // Convert -1..1 to 0..1
            params[i].leftGain = std::cos(pan01 * PI * 0.5f);
            params[i].rightGain = std::sin(pan01 * PI * 0.5f);
        }

        return params;
    }
};

// ============================================================================
// Sidechain Compressor - Duck signal based on another source (e.g., kick)
// ============================================================================
class Sidechain {
public:
    float threshold = 0.3f;         // Sidechain signal level to trigger ducking
    float amount = 0.8f;            // How much to duck (0.0 = none, 1.0 = full silence)
    float attack = 0.005f;          // Attack time in seconds (how fast to duck)
    float release = 0.15f;          // Release time in seconds (how fast to return)
    float sampleRate = 44100.0f;

    void setSampleRate(float sr) {
        sampleRate = sr;
    }

    // Update envelope from sidechain source signal
    void updateEnvelope(float sidechainInput) {
        float absInput = std::abs(sidechainInput);

        // Envelope follower with separate attack/release
        if (absInput > m_envelope) {
            // Attack - rising quickly
            float attackCoef = std::exp(-1.0f / (attack * sampleRate + 0.001f));
            m_envelope = attackCoef * m_envelope + (1.0f - attackCoef) * absInput;
        } else {
            // Release - falling slowly
            float releaseCoef = std::exp(-1.0f / (release * sampleRate + 0.001f));
            m_envelope = releaseCoef * m_envelope + (1.0f - releaseCoef) * absInput;
        }
    }

    // Process main signal with current envelope
    float process(float input) {
        // Calculate gain reduction based on how much envelope exceeds threshold
        float overThreshold = std::max(0.0f, m_envelope - threshold);
        float gainReduction = overThreshold / (1.0f - threshold + 0.001f);  // Normalize to 0-1
        gainReduction = std::min(1.0f, gainReduction);  // Clamp

        // Apply ducking
        float gain = 1.0f - (gainReduction * amount);
        return input * gain;
    }

    // Get current envelope level (for visualization)
    float getEnvelope() const { return m_envelope; }

    // Get current gain reduction (for visualization)
    float getGainReduction() const {
        float overThreshold = std::max(0.0f, m_envelope - threshold);
        float gainReduction = overThreshold / (1.0f - threshold + 0.001f);
        return std::min(1.0f, gainReduction) * amount;
    }

    void reset() {
        m_envelope = 0.0f;
    }

private:
    float m_envelope = 0.0f;
};

// ============================================================================
// Compressor - Standard dynamic range compression
// ============================================================================
class Compressor {
public:
    float threshold = 0.5f;         // 0.0 to 1.0
    float ratio = 4.0f;             // 1.0 to 20.0
    float attack = 0.01f;           // Seconds
    float release = 0.1f;           // Seconds
    float makeupGain = 1.0f;        // Linear gain (1.0 = 0dB)

    void setSampleRate(float sr) {
        m_sampleRate = sr;
    }

    float process(float input) {
        float absInput = std::abs(input);
        
        // Envelope follower
        if (absInput > m_envelope) {
            float attackCoef = std::exp(-1.0f / (attack * m_sampleRate + 0.001f));
            m_envelope = attackCoef * m_envelope + (1.0f - attackCoef) * absInput;
        } else {
            float releaseCoef = std::exp(-1.0f / (release * m_sampleRate + 0.001f));
            m_envelope = releaseCoef * m_envelope + (1.0f - releaseCoef) * absInput;
        }

        // Gain reduction
        float gain = 1.0f;
        if (m_envelope > threshold) {
            float over = m_envelope - threshold;
            float compressed = over / ratio;
            float target = threshold + compressed;
            gain = target / m_envelope;
        }

        return input * gain * makeupGain;
    }

    void reset() {
        m_envelope = 0.0f;
    }

private:
    float m_sampleRate = 44100.0f;
    float m_envelope = 0.0f;
};

// ============================================================================
// ThreeBandEQ - Low/Mid/High shelf and peak filters
// ============================================================================
class ThreeBandEQ {
public:
    float lowGain = 1.0f;   // Low shelf gain
    float midGain = 1.0f;   // Peaking gain
    float highGain = 1.0f;  // High shelf gain
    float lowFreq = 200.0f;
    float midFreq = 1000.0f;
    float highFreq = 5000.0f;

    void setSampleRate(float sr) {
        m_sampleRate = sr;
        // Simple biquad implementation or state variable would be better, 
        // but for zero-allocation efficiently, let's use 3 simple 1-pole filters logic for now
        // or actually standard shelving filters are better.
        // Implementing basic 3-band isolate logic:
    }

    // Simple 3-band splitter/combiner
    float process(float input) {
        // 1-pole Lowpass for low band
        float f_low = 2.0f * std::sin(PI * lowFreq / m_sampleRate);
        m_low += f_low * (input - m_low);

        // 1-pole Highpass for high band
        float f_high = 2.0f * std::sin(PI * highFreq / m_sampleRate);
        m_high += f_high * (input - m_high);
        float h_content = input - m_high; // This is actually lowpassed at high freq? 
        // Wait, correct 1-pole HP is: y[n] = (1-a)*x[n] + a*y[n-1]? No.
        // Let's use state variable for cleaner bands.
        
        // Actually, simple LPF/HPF combo:
        // Low content = LPF(input)
        // High content = input - LPF_high(input)
        // Mid content = input - Low - High
        
        // Refined simple crossover:
        m_lpf1 += f_low * (input - m_lpf1);
        float lowPart = m_lpf1;
        
        m_lpf2 += f_high * (input - m_lpf2);
        float highPart = input - m_lpf2;
        
        float midPart = input - lowPart - highPart;
        
        return lowPart * lowGain + midPart * midGain + highPart * highGain;
    }

    void reset() {
        m_lpf1 = 0.0f;
        m_lpf2 = 0.0f;
        m_low = 0.0f;
        m_high = 0.0f;
    }

private:
    float m_sampleRate = 44100.0f;
    float m_low = 0.0f;
    float m_high = 0.0f;
    float m_lpf1 = 0.0f;
    float m_lpf2 = 0.0f;
};

// ============================================================================
// Formant Filter - Simulates vowel sounds (Vocal/Talkbox effect)
// ============================================================================
class FormantFilter {
public:
    enum class Vowel : int { A, E, I, O, U };
    
    Vowel vowel = Vowel::O;
    float resonance = 5.0f; // Q factor
    float mix = 1.0f;
    float gain = 1.0f;

    void setSampleRate(float sr) {
        m_sampleRate = sr;
    }

    float process(float input) {
        float output = 0.0f;
        
        // Formant frequencies for Male voice (approx)
        // A: 600, 1000, 2500
        // E: 400, 1600, 2700
        // I: 250, 2000, 3000
        // O: 400, 800,  2250
        // U: 290, 600,  2150
        
        float f1, f2, f3;
        float g1=1.0f, g2=0.5f, g3=0.2f; // Relative gains
        
        switch (vowel) {
            case Vowel::A: f1=600; f2=1000; f3=2500; break;
            case Vowel::E: f1=400; f2=1600; f3=2700; break;
            case Vowel::I: f1=250; f2=2000; f3=3000; break;
            case Vowel::O: f1=400; f2=800;  f3=2250; break;
            case Vowel::U: f1=290; f2=600;  f3=2150; break;
            default:       f1=600; f2=1000; f3=2500; break;
        }
        
        // Run 3 parallel Bandpass filters
        output += runBPF(input, 0, f1, g1);
        output += runBPF(input, 1, f2, g2);
        output += runBPF(input, 2, f3, g3);
        
        // Makeup gain usually needed for BPFs
        output *= 3.0f * gain;
        
        return input * (1.0f - mix) + output * mix;
    }

    void reset() {
        for(int i=0; i<3; ++i) {
            m_states[i][0] = 0.0f;
            m_states[i][1] = 0.0f;
        }
    }

private:
    float m_sampleRate = 44100.0f;
    // State Variable Filter states: [band][0=band, 1=low]
    float m_states[3][2] = {{0,0}, {0,0}, {0,0}}; 
    
    float runBPF(float input, int bandIdx, float freq, float bandGain) {
        float f = 2.0f * std::sin(PI * freq / m_sampleRate);
        float q = 1.0f / resonance;
        
        float low = m_states[bandIdx][1] + f * m_states[bandIdx][0];
        float high = input - low - q * m_states[bandIdx][0];
        float band = f * high + m_states[bandIdx][0];
        
        m_states[bandIdx][0] = band;
        m_states[bandIdx][1] = low;
        
        return band * bandGain;
    }
};

// ============================================================================
// Effects Chain - Combines all effects for a channel
// ============================================================================
struct EffectsChain {
    // Effect instances
    Bitcrusher bitcrusher;
    Distortion distortion;
    Filter filter;
    ThreeBandEQ eq;                 // NEW
    Compressor compressor;          // NEW
    FormantFilter formant;          // NEW: For vocoder sounds
    Delay delay;
    Chorus chorus;
    Tremolo tremolo;
    Phaser phaser;
    Flanger flanger;                // NEW: Jet-plane swoosh effect for synthwave
    RingModulator ringMod;
    Sidechain sidechain;
    Reverb reverb;
    StereoWidener stereoWidener;    // NEW: For wide synthwave pads
    TapeSaturation tapeSaturation;  // NEW: For warm analog character
    Unison unison;                  // NEW: For thick synthwave sounds

    // Enable flags
    bool bitcrusherEnabled = false;
    bool distortionEnabled = false;
    bool filterEnabled = false;
    bool eqEnabled = false;         // NEW
    bool compressorEnabled = false; // NEW
    bool formantEnabled = false;    // NEW
    bool delayEnabled = false;
    bool chorusEnabled = false;
    bool tremoloEnabled = false;
    bool phaserEnabled = false;
    bool flangerEnabled = false;        // NEW
    bool ringModEnabled = false;
    bool sidechainEnabled = false;
    bool reverbEnabled = false;
    bool stereoWidenerEnabled = false;   // NEW
    bool tapeSaturationEnabled = false;  // NEW
    int sidechainSource = -1;  // Source channel index (-1 = none)

    void setSampleRate(float sr) {
        filter.setSampleRate(sr);
        eq.setSampleRate(sr);           // NEW
        compressor.setSampleRate(sr);   // NEW
        formant.setSampleRate(sr);      // NEW
        delay.setSampleRate(sr);
        chorus.setSampleRate(sr);
        flanger.init((int)sr);          // NEW
        sidechain.setSampleRate(sr);
        reverb.setSampleRate(sr);
        stereoWidener.setSampleRate(sr);    // NEW
        tapeSaturation.setSampleRate(sr);   // NEW
    }

    float process(float input, float time) {
        float output = input;

        // Process in order: EQ -> Saturation -> Formant -> Compressor -> Filter -> Modulation -> Time-based -> Reverb
        if (eqEnabled)         output = eq.process(output);             // NEW
        if (tapeSaturationEnabled) output = tapeSaturation.process(output);
        if (formantEnabled)    output = formant.process(output);        // NEW: Formant shaping early
        if (compressorEnabled) output = compressor.process(output);     // NEW
        if (bitcrusherEnabled) output = bitcrusher.process(output);
        if (distortionEnabled) output = distortion.process(output);
        if (filterEnabled)     output = filter.process(output);
        if (ringModEnabled)    output = ringMod.process(output, time);
        if (tremoloEnabled)    output *= tremolo.process(time);
        if (phaserEnabled)     output = phaser.process(output, time);
        if (flangerEnabled)    output = flanger.process(output);        // NEW
        if (chorusEnabled)     output = chorus.process(output, time);
        if (delayEnabled)      output = delay.process(output);
        if (reverbEnabled)     output = reverb.process(output);
        // Note: Stereo widener is processed in Sequencer for proper L/R handling

        return output;
    }

    // Process with stereo output (for stereo widener)
    std::pair<float, float> processStereo(float input, float time) {
        float mono = process(input, time);

        if (stereoWidenerEnabled) {
            return stereoWidener.process(mono);
        }

        return {mono, mono};
    }

    void reset() {
        bitcrusher.reset();
        filter.reset();
        eq.reset();             // NEW
        compressor.reset();     // NEW
        formant.reset();        // NEW
        delay.reset();
        stereoWidener.reset();       // NEW
        tapeSaturation.reset();      // NEW
        chorus.reset();
        phaser.reset();
        flanger.reset();        // NEW
        sidechain.reset();
        reverb.reset();
    }
};

} // namespace ChiptuneTracker
