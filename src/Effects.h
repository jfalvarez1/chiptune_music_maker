#pragma once

/*
 * ChiptuneTracker - Effects Module
 *
 * All audio effects for chiptune processing.
 * Designed for zero-allocation in audio thread.
 */

#include "Types.h"
#include "PitchShift.h"
#include "Reverbs.h"
#include "Convolution.h"
#include "EqualizerSuite.h"
#include "DelayCompensation.h"
#include <cmath>
#include <cstring>
#include <array>
#include <atomic>
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

    void setResonance(float value) {
        resonance = value;
        updateCoefficients();
    }

    /*
     * Recompute from whatever `cutoff` and `resonance` currently hold.
     *
     * process() reads the CACHED coefficients, so assigning the public
     * fields directly does nothing at all - and that is exactly what
     * applyEffectsConfig was doing. The per-channel filter cutoff and
     * resonance controls had never moved the sound: the coefficients were
     * whatever setSampleRate last computed, which is the 1000 Hz default.
     *
     * The fields stay public because a great deal of code reads them, but
     * every writer must now either use the setters or call this.
     */
    void refresh() { updateCoefficients(); }

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
        /*
         * A Chamberlin state-variable filter is only stable while its
         * frequency coefficient stays under about 1; past that the feedback
         * around the two integrators diverges and the output is NaN within a
         * few dozen samples.
         *
         * 2*sin(pi*fc/fs) passes 1 at roughly fs/6 - about 7 kHz at 44.1 -
         * so any cutoff above that blew the filter up. It never showed
         * because the coefficients were stale: the filter was pinned at its
         * 1000 Hz default no matter what the control said. The moment that
         * was fixed, a 16 kHz cutoff produced NaN, and a NaN in a channel's
         * insert rack is silence from then on.
         *
         * Clamping the coefficient rather than the cutoff means the control
         * still runs to 20 kHz and simply stops opening further near the
         * top, which is what a listener would expect from "fully open".
         */
        if (!std::isfinite(cutoff) || !std::isfinite(resonance)) {
            m_f = 0.5f;
            m_q = 1.0f;
            return;
        }

        const float safeRate = (m_sampleRate > 1.0f) ? m_sampleRate : 44100.0f;
        const float safeCutoff = std::max(1.0f, std::min(cutoff, safeRate * 0.49f));
        m_f = std::min(2.0f * std::sin(PI * safeCutoff / safeRate), 1.0f);

        // Also bounded below: q is the damping, and at zero the filter
        // self-oscillates and runs away just as surely.
        m_q = std::max(0.05f, 1.0f - std::clamp(resonance, 0.0f, 1.0f) * 0.9f);
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

    /*
     * Which reverb this is.
     *
     * Room is the original Schroeder-Moorer arrangement below, kept exactly
     * as it was: it is the default, every existing project uses it, and
     * there is a test asserting the rack still matches a frozen copy of the
     * pre-rack chain. A "better" room would silently change every project
     * ever saved. The other five run through the shared tank instead.
     */
    ReverbAlgorithm algorithm = ReverbAlgorithm::Room;
    AdvancedReverb tank;

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
        // The tank sizes its own delay lines here, on the UI thread. It is
        // the only place it allocates.
        tank.prepare(sr, algorithm);

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

    // Re-size the tank for a new algorithm. UI thread only: this
    // reallocates, which is why it is not done inside processStereo when the
    // algorithm happens to differ.
    void setAlgorithm(ReverbAlgorithm wanted) {
        if (wanted == algorithm && tank.algorithm() == wanted) return;
        algorithm = wanted;
        tank.prepare(m_sampleRate, wanted);
    }

    // Process mono input, returns stereo pair
    std::pair<float, float> processStereo(float input) {
        /*
         * Anything but Room goes to the tank.
         *
         * Checked before the pre-delay rather than after, because the tank
         * has its own - running both would double it, and a 40 ms pre-delay
         * where the user asked for 20 is the sort of thing that is very hard
         * to hear as a bug and very easy to hear as "this reverb is wrong".
         */
        if (algorithm != ReverbAlgorithm::Room) {
            tank.size = roomSize;
            tank.damping = damping;
            tank.predelay = predelay;
            tank.width = width;

            const auto [wetL, wetR] = tank.processStereo(input);
            return {input * (1.0f - mix) + wetL * mix,
                    input * (1.0f - mix) + wetR * mix};
        }

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

        // Gain reduction.
        //
        // threshold is a linear level (0..1) and ratio is >= 1. Guard both:
        // a zero or negative threshold makes target negative, so gain comes
        // out negative and the "compressor" inverts and amplifies instead.
        const float safeThreshold = std::max(1e-4f, threshold);
        const float safeRatio = std::max(1.0f, ratio);

        float gain = 1.0f;
        if (m_envelope > safeThreshold) {
            float over = m_envelope - safeThreshold;
            float compressed = over / safeRatio;
            float target = safeThreshold + compressed;
            gain = target / m_envelope;
        }

        // A compressor only ever attenuates. Never let the computed gain go
        // negative or above unity, whatever the parameters say.
        gain = std::max(0.0f, std::min(gain, 1.0f));

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
// ============================================================================
// Insert rack
//
// The chain used to be seventeen members, seventeen bools, and a processing
// order hardcoded into process(). Every new effect cost another member,
// another bool and another hand-placed line - and nobody could put the
// reverb before the distortion.
//
// It is now a reorderable rack of slots over the same effect instances.
// Three deliberate constraints:
//
//   - ORDER is the rack's business; ON/OFF is not. Enablement stays in the
//     ChannelConfig *Enabled flags, because that is what every preset writes
//     - applyGenreEffects(), the Song Starter, the Channel Editor. Moving
//     bypass into the rack would mean a preset setting reverbEnabled = true
//     no longer enables reverb.
//   - Fixed capacity, no allocation. The instances are members as before and
//     the order is a fixed array; the audio thread allocates nothing.
//   - IEffect is shaped for Task J. A hosted VST3/CLAP plugin has to be able
//     to be a slot, which is why processBlock() and latencySamples() are here
//     even though no built-in overrides them.
// ============================================================================

enum class EffectType : uint8_t {
    EQ = 0,
    TapeSaturation,
    Formant,
    Compressor,
    Bitcrusher,
    Distortion,
    Filter,
    RingMod,
    Tremolo,
    Phaser,
    Flanger,
    Chorus,
    Delay,
    Reverb,

    // ---- Pitch and time (Task G1) ---------------------------------------
    //
    // Appended, never inserted: these tokens are part of the .ctp format and
    // CLASSIC_EFFECT_ORDER is the migration target for every existing file.
    // Appending leaves an old project's rack exactly as it was, with the
    // three new slots at the end and switched off.
    //
    // None of these is chip-authentic - a 2A03 could not begin to do them -
    // so per the ground rules all three default to off.
    PitchShift,
    FormantShift,
    AutoTune,

    // Convolution (Task G3). Appended like the rest: the tokens are part of
    // the .ctp format and the classic order is what every existing file
    // migrates onto.
    Convolution,

    // Equalisers (Task G4). Mid-side is not here: it needs stereo, and the
    // channel chain is mono until the pan - it lives on the master bus,
    // which is where a mid-side EQ belongs anyway.
    TiltEq,
    GraphicEq,
    DynamicEq,

    Count
};

inline constexpr int EFFECT_TYPE_COUNT = static_cast<int>(EffectType::Count);

// Stable tokens. These are part of the .ctp format: renaming one silently
// reinterprets an existing file, so they do not change.
inline const char* effectTypeId(EffectType type) {
    switch (type) {
        case EffectType::EQ:             return "eq";
        case EffectType::TapeSaturation: return "tape";
        case EffectType::Formant:        return "formant";
        case EffectType::Compressor:     return "comp";
        case EffectType::Bitcrusher:     return "bitcrush";
        case EffectType::Distortion:     return "dist";
        case EffectType::Filter:         return "filter";
        case EffectType::RingMod:        return "ringmod";
        case EffectType::Tremolo:        return "tremolo";
        case EffectType::Phaser:         return "phaser";
        case EffectType::Flanger:        return "flanger";
        case EffectType::Chorus:         return "chorus";
        case EffectType::Delay:          return "delay";
        case EffectType::Reverb:         return "reverb";
        case EffectType::PitchShift:     return "pitchshift";
        case EffectType::FormantShift:   return "formantshift";
        case EffectType::AutoTune:       return "autotune";
        case EffectType::Convolution:    return "convolution";
        case EffectType::TiltEq:         return "tilteq";
        case EffectType::GraphicEq:      return "graphiceq";
        case EffectType::DynamicEq:      return "dynamiceq";
        default:                         return "?";
    }
}

inline const char* effectDisplayName(EffectType type) {
    switch (type) {
        case EffectType::EQ:             return "3-Band EQ";
        case EffectType::TapeSaturation: return "Tape Saturation";
        case EffectType::Formant:        return "Formant Filter";
        case EffectType::Compressor:     return "Compressor";
        case EffectType::Bitcrusher:     return "Bitcrusher";
        case EffectType::Distortion:     return "Distortion";
        case EffectType::Filter:         return "Filter";
        case EffectType::RingMod:        return "Ring Modulator";
        case EffectType::Tremolo:        return "Tremolo";
        case EffectType::Phaser:         return "Phaser";
        case EffectType::Flanger:        return "Flanger";
        case EffectType::Chorus:         return "Chorus";
        case EffectType::Delay:          return "Delay";
        case EffectType::Reverb:         return "Reverb";
        case EffectType::PitchShift:     return "Pitch Shift";
        case EffectType::FormantShift:   return "Formant Shift";
        case EffectType::AutoTune:       return "Auto-Tune";
        case EffectType::Convolution:    return "Convolution Reverb";
        case EffectType::TiltEq:         return "Tilt EQ";
        case EffectType::GraphicEq:      return "Graphic EQ";
        case EffectType::DynamicEq:      return "Dynamic EQ";
        default:                         return "Unknown";
    }
}

inline bool effectTypeFromId(const char* id, EffectType& out) {
    if (id == nullptr) return false;
    for (int i = 0; i < EFFECT_TYPE_COUNT; ++i) {
        const EffectType candidate = static_cast<EffectType>(i);
        if (std::strcmp(effectTypeId(candidate), id) == 0) {
            out = candidate;
            return true;
        }
    }
    return false;
}

/*
 * One processor in a slot.
 *
 * process() runs on the audio thread: no allocation, no locks, no I/O.
 *
 * processBlock() and latencySamples() exist for Task J. A plugin is
 * block-native and reports its own latency; a built-in is neither, so the
 * defaults cover every effect in this file and no built-in overrides them.
 */
class IEffect {
public:
    virtual ~IEffect() = default;

    virtual const char* typeId() const = 0;
    virtual float process(float input, float time) = 0;
    virtual void setSampleRate(float sampleRate) { (void)sampleRate; }
    virtual void reset() {}

    // Default is the per-sample loop; a hosted plugin overrides this instead.
    virtual void processBlock(float* buffer, int frames, float startTime,
                              float secondsPerFrame) {
        if (buffer == nullptr) return;
        for (int i = 0; i < frames; ++i) {
            buffer[i] = process(buffer[i],
                                startTime + static_cast<float>(i) * secondsPerFrame);
        }
    }

    virtual int latencySamples() const { return 0; }
};

// ---- Adapters ---------------------------------------------------------------
//
// Each holds a pointer to the concrete effect rather than owning it, so that
// every existing `fx.bitcrusher.bits` call site keeps compiling unchanged.
// The pointers are rebound on copy; see EffectsChain::bindAdapters.

#define CHIPTUNE_SIMPLE_FX(ClassName, Impl, Token, Call)                      \
    class ClassName final : public IEffect {                                  \
    public:                                                                   \
        Impl* target = nullptr;                                               \
        const char* typeId() const override { return Token; }                 \
        float process(float input, float time) override {                     \
            (void)time;                                                       \
            return target ? (Call) : input;                                   \
        }                                                                     \
    }

CHIPTUNE_SIMPLE_FX(EqFx,         ThreeBandEQ,    "eq",       target->process(input));
CHIPTUNE_SIMPLE_FX(TapeFx,       TapeSaturation, "tape",     target->process(input));
CHIPTUNE_SIMPLE_FX(FormantFx,    FormantFilter,  "formant",  target->process(input));
CHIPTUNE_SIMPLE_FX(CompressorFx, Compressor,     "comp",     target->process(input));
CHIPTUNE_SIMPLE_FX(BitcrusherFx, Bitcrusher,     "bitcrush", target->process(input));
CHIPTUNE_SIMPLE_FX(DistortionFx, Distortion,     "dist",     target->process(input));
CHIPTUNE_SIMPLE_FX(FilterFx,     Filter,         "filter",   target->process(input));
CHIPTUNE_SIMPLE_FX(FlangerFx,    Flanger,        "flanger",  target->process(input));
CHIPTUNE_SIMPLE_FX(DelayFx,      Delay,          "delay",    target->process(input));
CHIPTUNE_SIMPLE_FX(ReverbFx,     Reverb,         "reverb",   target->process(input));

#undef CHIPTUNE_SIMPLE_FX

// The four that need `time`, kept explicit rather than macro'd - two of them
// do something other than pass the sample straight through.

class RingModFx final : public IEffect {
public:
    RingModulator* target = nullptr;
    const char* typeId() const override { return "ringmod"; }
    float process(float input, float time) override {
        return target ? target->process(input, time) : input;
    }
};

class TremoloFx final : public IEffect {
public:
    Tremolo* target = nullptr;
    const char* typeId() const override { return "tremolo"; }
    float process(float input, float time) override {
        // Multiplies rather than replaces - the chain did `output *=` and
        // the identity test would catch any drift from that.
        return target ? input * target->process(time) : input;
    }
};

class PhaserFx final : public IEffect {
public:
    Phaser* target = nullptr;
    const char* typeId() const override { return "phaser"; }
    float process(float input, float time) override {
        return target ? target->process(input, time) : input;
    }
};

/*
 * The phase-vocoder effects.
 *
 * Each reports a window of latency, which is real and unavoidable: a bin's
 * true frequency cannot be known without seeing two frames of it. That is
 * what latencySamples() is for, and it is why these are studio effects
 * rather than things to monitor through while playing.
 *
 * The dry/wet mix is applied here rather than inside the vocoder, so a
 * bypassed-by-mix effect still runs its analysis and does not click when the
 * mix is brought back up.
 */

/*
 * Mixing a dry path against a wet one that arrives late.
 *
 * This is the part that made the reported latency a half-truth. The wet
 * signal comes out of a phase vocoder a window later than it went in; the
 * dry signal used to be the raw input, on time. So at any mix between 0 and
 * 1 the effect emitted the same sound twice, 23 ms apart - an audible flam
 * on transients, and a comb filter on anything sustained.
 *
 * It also made delay compensation impossible to get right, because the
 * channel had no single latency to compensate for: part of it was late and
 * part of it was not.
 *
 * Holding the dry path back to meet the wet one fixes both. The effect then
 * has one honest latency, which is what latencySamples() reports and what
 * the mixer delays every other channel by.
 *
 * The line is sized from latencySamples() on each call. That is a compare in
 * the steady state; it only does work at the instant the latency changes,
 * which is when the effect is switched on and is already the moment a click
 * is expected. It never allocates.
 */
class LatentDryMix {
public:
    float blend(float input, float wet, float mix, int latencySamples) {
        m_dry.setDelay(latencySamples);
        const float dry = m_dry.process(input);
        const float amount = std::clamp(mix, 0.0f, 1.0f);
        return dry * (1.0f - amount) + wet * amount;
    }
    void reset() { m_dry.clear(); }

private:
    DryPathDelay m_dry;
};

class PitchShiftFx final : public IEffect {
public:
    PitchShifter* target = nullptr;
    const char* typeId() const override { return "pitchshift"; }
    float process(float input, float time) override {
        (void)time;
        if (target == nullptr) return input;
        const float wet = target->process(input);
        return m_mix.blend(input, wet, target->mix, latencySamples());
    }
    void setSampleRate(float sampleRate) override {
        if (target) target->configure(sampleRate);
    }
    void reset() override {
        if (target) target->reset();
        m_mix.reset();
    }
    int latencySamples() const override {
        return target ? target->latency() : 0;
    }

private:
    LatentDryMix m_mix;
};

class FormantShiftFx final : public IEffect {
public:
    FormantShifter* target = nullptr;
    const char* typeId() const override { return "formantshift"; }
    float process(float input, float time) override {
        (void)time;
        if (target == nullptr) return input;
        const float wet = target->process(input);
        return m_mix.blend(input, wet, target->mix, latencySamples());
    }
    void setSampleRate(float sampleRate) override {
        if (target) target->configure(sampleRate);
    }
    void reset() override {
        if (target) target->reset();
        m_mix.reset();
    }
    int latencySamples() const override {
        return target ? target->latency() : 0;
    }

private:
    LatentDryMix m_mix;
};

class AutoTuneFx final : public IEffect {
public:
    AutoTune* target = nullptr;
    const char* typeId() const override { return "autotune"; }
    float process(float input, float time) override {
        (void)time;
        if (target == nullptr) return input;
        const float wet = target->process(input);
        return m_mix.blend(input, wet, target->mix, latencySamples());
    }
    void setSampleRate(float sampleRate) override {
        if (target) target->configure(sampleRate);
    }
    void reset() override {
        if (target) target->reset();
        m_mix.reset();
    }
    int latencySamples() const override {
        return target ? target->latency() : 0;
    }

private:
    LatentDryMix m_mix;
};

/*
 * Convolution.
 *
 * The engine allocates nothing until an impulse response is attached, which
 * is what keeps this affordable: the frequency-domain delay line is roughly
 * 700 KB per second of response, and thirty-two channels plus four buses all
 * holding one would be a hundred megabytes for an effect that is almost
 * always used on a single send.
 */
class ConvolutionFx final : public IEffect {
public:
    ConvolutionEngine* target = nullptr;

    // Read through a pointer rather than held here, because the adapters are
    // private to EffectsChain and applyEffectsConfig writes the parameters.
    const float* mix = nullptr;

    const char* typeId() const override { return "convolution"; }
    float process(float input, float time) override {
        (void)time;
        if (target == nullptr || !target->active()) return input;
        const float wet = target->process(input);
        const float amount = (mix != nullptr) ? *mix : 0.35f;
        return m_mix.blend(input, wet, amount, latencySamples());
    }
    void reset() override {
        if (target) target->reset();
        m_mix.reset();
    }
    int latencySamples() const override {
        return target ? target->latency() : 0;
    }

private:
    LatentDryMix m_mix;
};

class TiltEqFx final : public IEffect {
public:
    TiltEQ* target = nullptr;
    const char* typeId() const override { return "tilteq"; }
    float process(float input, float time) override {
        (void)time;
        return target ? target->process(input) : input;
    }
    void setSampleRate(float sampleRate) override {
        if (target) target->configure(sampleRate);
    }
    void reset() override { if (target) target->reset(); }
};

class GraphicEqFx final : public IEffect {
public:
    GraphicEQ* target = nullptr;
    const char* typeId() const override { return "graphiceq"; }
    float process(float input, float time) override {
        (void)time;
        return target ? target->process(input) : input;
    }
    void setSampleRate(float sampleRate) override {
        if (target) target->configure(sampleRate);
    }
    void reset() override { if (target) target->reset(); }
};

class DynamicEqFx final : public IEffect {
public:
    DynamicEQ* target = nullptr;
    const char* typeId() const override { return "dynamiceq"; }
    float process(float input, float time) override {
        (void)time;
        return target ? target->process(input) : input;
    }
    void setSampleRate(float sampleRate) override {
        if (target) target->configure(sampleRate);
    }
    void reset() override { if (target) target->reset(); }
};

class ChorusFx final : public IEffect {
public:
    Chorus* target = nullptr;
    const char* typeId() const override { return "chorus"; }
    float process(float input, float time) override {
        return target ? target->process(input, time) : input;
    }
};

// The order every version before the rack used, verbatim. It is the default
// for a new channel and the migration target for every existing .ctp file,
// so it must not be reordered casually.
inline constexpr EffectType CLASSIC_EFFECT_ORDER[] = {
    EffectType::EQ,
    EffectType::TapeSaturation,
    EffectType::Formant,
    EffectType::Compressor,
    EffectType::Bitcrusher,
    EffectType::Distortion,
    EffectType::Filter,
    EffectType::RingMod,
    EffectType::Tremolo,
    EffectType::Phaser,
    EffectType::Flanger,
    EffectType::Chorus,
    EffectType::Delay,
    EffectType::Reverb,

    // The pitch and time effects sit at the END of the classic order, after
    // reverb. That is not where a mixing engineer would put a pitch shifter,
    // but it is where they have to go: the order is what every existing file
    // migrates onto, and moving reverb off the end would change how every
    // one of those projects sounds. A user who wants them earlier drags them.
    EffectType::PitchShift,
    EffectType::FormantShift,
    EffectType::AutoTune,
    EffectType::Convolution,
    EffectType::TiltEq,
    EffectType::GraphicEq,
    EffectType::DynamicEq,
};
inline constexpr int CLASSIC_EFFECT_COUNT =
    static_cast<int>(sizeof(CLASSIC_EFFECT_ORDER) / sizeof(CLASSIC_EFFECT_ORDER[0]));

static_assert(CLASSIC_EFFECT_COUNT == EFFECT_TYPE_COUNT,
              "the classic order must list every effect exactly once");

struct EffectsChain {
    // Room for the rack to grow without another format change.
    static constexpr int MAX_SLOTS = MAX_FX_SLOTS;

    // Effect instances
    Bitcrusher bitcrusher;
    Distortion distortion;
    Filter filter;
    ThreeBandEQ eq;
    Compressor compressor;
    FormantFilter formant;
    Delay delay;
    Chorus chorus;
    Tremolo tremolo;
    Phaser phaser;
    Flanger flanger;
    RingModulator ringMod;
    Sidechain sidechain;
    Reverb reverb;
    StereoWidener stereoWidener;
    TapeSaturation tapeSaturation;
    Unison unison;

    // The phase-vocoder effects. Each carries a window of FFT state, which
    // is why they are here rather than being created per note.
    PitchShifter pitchShifter;
    FormantShifter formantShifter;
    AutoTune autoTune;

    // Holds no buffers until an impulse response is attached.
    ConvolutionEngine convolution;

    TiltEQ tiltEq;
    GraphicEQ graphicEq;
    DynamicEQ dynamicEq;

    // Which response is currently attached, so a sync can tell whether
    // prepare() - which reallocates the whole delay line - is actually
    // needed. Without this every UI interaction would reallocate.
    const PartitionedIR* attachedIR = nullptr;

    // Enable flags. Authoritative: presets write these, the rack reads them.
    bool bitcrusherEnabled = false;
    bool distortionEnabled = false;
    bool filterEnabled = false;
    bool eqEnabled = false;
    bool compressorEnabled = false;
    bool formantEnabled = false;
    bool delayEnabled = false;
    bool chorusEnabled = false;
    bool tremoloEnabled = false;
    bool phaserEnabled = false;
    bool flangerEnabled = false;
    bool ringModEnabled = false;
    bool sidechainEnabled = false;
    bool reverbEnabled = false;
    bool stereoWidenerEnabled = false;
    bool tapeSaturationEnabled = false;

    // Off by default, and they have to be: none of these is anything a 2A03
    // could do, and the ground rule is that non-chip DSP ships silent.
    bool pitchShiftEnabled = false;
    bool formantShiftEnabled = false;
    bool autoTuneEnabled = false;
    bool convolutionEnabled = false;
    float convolutionMix = 0.35f;
    bool tiltEqEnabled = false;
    bool graphicEqEnabled = false;
    bool dynamicEqEnabled = false;

    int sidechainSource = -1;  // Source channel index (-1 = none)

    // ---- The rack ----------------------------------------------------------
    //
    // Order and count live in one struct because they must be published
    // together: two separate members cannot be swapped atomically, and a
    // reader that saw a new order with an old count could walk off the end.
    struct RackOrder {
        std::array<EffectType, MAX_SLOTS> slots{};
        int count = 0;
    };
    // Per-effect wet/dry. 1.0 is bit-exact passthrough of the wet signal -
    // the blend is skipped entirely rather than computing dry*0 + wet*1.
    std::array<float, EFFECT_TYPE_COUNT> mix{};

    EffectsChain() {
        bindAdapters();
        resetOrderToClassic();
        mix.fill(1.0f);
    }

    // The permutation the audio thread should use for this sample. Acquired
    // once per call so a reorder landing mid-chain cannot split a sample
    // across two different orders.
    const RackOrder& rack() const {
        return m_orders[m_active.load(std::memory_order_acquire)];
    }

    // Copying rebinds, because the adapters hold pointers into the object
    // being copied. Without this a copied chain would drive the original.
    EffectsChain(const EffectsChain& other) { copyFrom(other); }
    EffectsChain& operator=(const EffectsChain& other) {
        if (this != &other) copyFrom(other);
        return *this;
    }

    void resetOrderToClassic() {
        // Both buffers, so a later publish cannot resurrect a stale order.
        for (int buffer = 0; buffer < 2; ++buffer) {
            m_orders[buffer].count = CLASSIC_EFFECT_COUNT;
            for (int i = 0; i < CLASSIC_EFFECT_COUNT; ++i) {
                m_orders[buffer].slots[i] = CLASSIC_EFFECT_ORDER[i];
            }
            for (int i = CLASSIC_EFFECT_COUNT; i < MAX_SLOTS; ++i) {
                m_orders[buffer].slots[i] = EffectType::EQ;
            }
        }
        m_active.store(0, std::memory_order_release);
    }

    bool isClassicOrder() const {
        const RackOrder& current = rack();
        if (current.count != CLASSIC_EFFECT_COUNT) return false;
        for (int i = 0; i < current.count; ++i) {
            if (current.slots[i] != CLASSIC_EFFECT_ORDER[i]) return false;
        }
        return true;
    }

    // Replace the whole order in one publish. Used by the loader and by the
    // chain presets, neither of which wants to emit a burst of moveSlot
    // calls the audio thread would hear one at a time.
    void setOrder(const EffectType* slots, int count) {
        if (slots == nullptr || count <= 0 || count > MAX_SLOTS) return;
        const int current = m_active.load(std::memory_order_relaxed);
        const int next = 1 - current;
        RackOrder& target = m_orders[next];
        target.count = count;
        for (int i = 0; i < count; ++i) target.slots[i] = slots[i];
        for (int i = count; i < MAX_SLOTS; ++i) target.slots[i] = EffectType::EQ;
        m_active.store(next, std::memory_order_release);
    }

    /*
     * The adapter for a type, or nullptr when there is none.
     *
     * Public so a test can walk every EffectType and assert each one
     * resolves. A missing arm below is invisible at runtime - the rack skips
     * the slot and the effect just never runs - so the guard has to be
     * external.
     */
    IEffect* effectFor(EffectType type) { return lookup(type); }

    /*
     * How late this chain's output is, in samples.
     *
     * The sum over the effects that are actually enabled, because a
     * convolution reverb that is switched off costs nothing and must not
     * make the rest of the song wait for it.
     *
     * This is what delay compensation needs and what nothing was
     * calculating: each effect knew its own latency, the interface exposed
     * it, and no caller ever added it up.
     */
    int latencySamples() {
        int total = 0;
        const RackOrder& order = rack();

        for (int slot = 0; slot < order.count; ++slot) {
            const EffectType type = order.slots[slot];
            if (!isEnabled(type)) continue;

            IEffect* effect = lookup(type);
            if (effect != nullptr) total += effect->latencySamples();
        }
        return total;
    }

    IEffect* lookup(EffectType type) {
        switch (type) {
            case EffectType::EQ:             return &eqFx;
            case EffectType::TapeSaturation: return &tapeFx;
            case EffectType::Formant:        return &formantFx;
            case EffectType::Compressor:     return &compressorFx;
            case EffectType::Bitcrusher:     return &bitcrusherFx;
            case EffectType::Distortion:     return &distortionFx;
            case EffectType::Filter:         return &filterFx;
            case EffectType::RingMod:        return &ringModFx;
            case EffectType::Tremolo:        return &tremoloFx;
            case EffectType::Phaser:         return &phaserFx;
            case EffectType::Flanger:        return &flangerFx;
            case EffectType::Chorus:         return &chorusFx;
            case EffectType::Delay:          return &delayFx;
            case EffectType::Reverb:         return &reverbFx;
            case EffectType::PitchShift:     return &pitchShiftFx;
            case EffectType::FormantShift:   return &formantShiftFx;
            case EffectType::AutoTune:       return &autoTuneFx;
            case EffectType::Convolution:    return &convolutionFx;
            case EffectType::TiltEq:         return &tiltEqFx;
            case EffectType::GraphicEq:      return &graphicEqFx;
            case EffectType::DynamicEq:      return &dynamicEqFx;
            // A type with no arm here is silently skipped by the rack, with
            // no error anywhere - the effect simply never runs. That is
            // exactly what happened when these three were added, so there is
            // a test asserting every EffectType resolves to an adapter.
            default:                         return nullptr;
        }
    }

    bool isEnabled(EffectType type) const {
        switch (type) {
            case EffectType::EQ:             return eqEnabled;
            case EffectType::TapeSaturation: return tapeSaturationEnabled;
            case EffectType::Formant:        return formantEnabled;
            case EffectType::Compressor:     return compressorEnabled;
            case EffectType::Bitcrusher:     return bitcrusherEnabled;
            case EffectType::Distortion:     return distortionEnabled;
            case EffectType::Filter:         return filterEnabled;
            case EffectType::RingMod:        return ringModEnabled;
            case EffectType::Tremolo:        return tremoloEnabled;
            case EffectType::Phaser:         return phaserEnabled;
            case EffectType::Flanger:        return flangerEnabled;
            case EffectType::Chorus:         return chorusEnabled;
            case EffectType::Delay:          return delayEnabled;
            case EffectType::Reverb:         return reverbEnabled;
            case EffectType::PitchShift:     return pitchShiftEnabled;
            case EffectType::FormantShift:   return formantShiftEnabled;
            case EffectType::AutoTune:       return autoTuneEnabled;
            case EffectType::Convolution:    return convolutionEnabled;
            case EffectType::TiltEq:         return tiltEqEnabled;
            case EffectType::GraphicEq:      return graphicEqEnabled;
            case EffectType::DynamicEq:      return dynamicEqEnabled;
            default:                         return false;
        }
    }

    void setEnabled(EffectType type, bool on) {
        switch (type) {
            case EffectType::EQ:             eqEnabled = on; break;
            case EffectType::TapeSaturation: tapeSaturationEnabled = on; break;
            case EffectType::Formant:        formantEnabled = on; break;
            case EffectType::Compressor:     compressorEnabled = on; break;
            case EffectType::Bitcrusher:     bitcrusherEnabled = on; break;
            case EffectType::Distortion:     distortionEnabled = on; break;
            case EffectType::Filter:         filterEnabled = on; break;
            case EffectType::RingMod:        ringModEnabled = on; break;
            case EffectType::Tremolo:        tremoloEnabled = on; break;
            case EffectType::Phaser:         phaserEnabled = on; break;
            case EffectType::Flanger:        flangerEnabled = on; break;
            case EffectType::Chorus:         chorusEnabled = on; break;
            case EffectType::Delay:          delayEnabled = on; break;
            case EffectType::Reverb:         reverbEnabled = on; break;
            case EffectType::PitchShift:     pitchShiftEnabled = on; break;
            case EffectType::FormantShift:   formantShiftEnabled = on; break;
            case EffectType::AutoTune:       autoTuneEnabled = on; break;
            case EffectType::Convolution:    convolutionEnabled = on; break;
            case EffectType::TiltEq:         tiltEqEnabled = on; break;
            case EffectType::GraphicEq:      graphicEqEnabled = on; break;
            case EffectType::DynamicEq:      dynamicEqEnabled = on; break;
            default: break;
        }
    }

    // Move a slot, keeping every other slot's relative order. Used by the
    // drag-to-reorder UI; bounds-checked because a drag can end anywhere.
    void moveSlot(int from, int to) {
        const int current = m_active.load(std::memory_order_relaxed);
        const RackOrder& source = m_orders[current];

        if (from < 0 || from >= source.count) return;
        if (to < 0 || to >= source.count || to == from) return;

        // Build the new permutation in the buffer nobody is reading, then
        // publish it with a single store. The audio thread never sees the
        // half-shifted state that duplicated an effect and drove the output
        // to amplitude 50 before this existed.
        const int next = 1 - current;
        RackOrder& target = m_orders[next];
        target = source;

        const EffectType moved = source.slots[from];
        if (from < to) {
            for (int i = from; i < to; ++i) target.slots[i] = source.slots[i + 1];
        } else {
            for (int i = from; i > to; --i) target.slots[i] = source.slots[i - 1];
        }
        target.slots[to] = moved;

        m_active.store(next, std::memory_order_release);
    }

    void setSampleRate(float sr) {
        filter.setSampleRate(sr);
        eq.setSampleRate(sr);
        compressor.setSampleRate(sr);
        formant.setSampleRate(sr);
        delay.setSampleRate(sr);
        chorus.setSampleRate(sr);
        flanger.init((int)sr);
        sidechain.setSampleRate(sr);
        reverb.setSampleRate(sr);
        stereoWidener.setSampleRate(sr);
        tapeSaturation.setSampleRate(sr);

        // The phase vocoders allocate their windows and FFT plans here.
        // configure() is the only place they do, so it has to be reached -
        // an unconfigured vocoder passes its input straight through, which
        // looks exactly like the effect being switched off.
        tiltEq.configure(sr);
        graphicEq.configure(sr);
        dynamicEq.configure(sr);

        pitchShifter.configure(sr);
        formantShifter.configure(sr);
        autoTune.configure(sr);
    }

    float process(float input, float time) {
        float output = input;

        // One acquire per sample: the whole chain runs on one permutation.
        const RackOrder& active = rack();

        for (int i = 0; i < active.count; ++i) {
            const EffectType type = active.slots[i];
            if (!isEnabled(type)) continue;

            IEffect* effect = lookup(type);
            if (effect == nullptr) continue;

            const float wet = effect->process(output, time);
            const float amount = mix[static_cast<size_t>(type)];

            // The >= 1.0f branch is not an optimisation - it is what makes
            // the default rack bit-identical to the old fixed chain.
            output = (amount >= 1.0f) ? wet : (output + (wet - output) * amount);
        }

        // Stereo widener is applied by the Sequencer, which owns the L/R pair.
        return output;
    }

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
        eq.reset();
        compressor.reset();
        formant.reset();
        delay.reset();
        stereoWidener.reset();
        tapeSaturation.reset();
        chorus.reset();
        phaser.reset();
        flanger.reset();
        sidechain.reset();
        reverb.reset();
    }

private:
    RackOrder m_orders[2];
    std::atomic<int> m_active{0};

    EqFx eqFx; TapeFx tapeFx; FormantFx formantFx; CompressorFx compressorFx;
    BitcrusherFx bitcrusherFx; DistortionFx distortionFx; FilterFx filterFx;
    RingModFx ringModFx; TremoloFx tremoloFx; PhaserFx phaserFx;
    FlangerFx flangerFx; ChorusFx chorusFx; DelayFx delayFx; ReverbFx reverbFx;
    PitchShiftFx pitchShiftFx; FormantShiftFx formantShiftFx;
    AutoTuneFx autoTuneFx; ConvolutionFx convolutionFx;
    TiltEqFx tiltEqFx; GraphicEqFx graphicEqFx; DynamicEqFx dynamicEqFx;

    void bindAdapters() {
        eqFx.target = &eq;
        tapeFx.target = &tapeSaturation;
        formantFx.target = &formant;
        compressorFx.target = &compressor;
        bitcrusherFx.target = &bitcrusher;
        distortionFx.target = &distortion;
        filterFx.target = &filter;
        ringModFx.target = &ringMod;
        tremoloFx.target = &tremolo;
        phaserFx.target = &phaser;
        flangerFx.target = &flanger;
        chorusFx.target = &chorus;
        delayFx.target = &delay;
        reverbFx.target = &reverb;
        pitchShiftFx.target = &pitchShifter;
        formantShiftFx.target = &formantShifter;
        autoTuneFx.target = &autoTune;
        convolutionFx.target = &convolution;
        convolutionFx.mix = &convolutionMix;
        tiltEqFx.target = &tiltEq;
        graphicEqFx.target = &graphicEq;
        dynamicEqFx.target = &dynamicEq;
    }

    void copyFrom(const EffectsChain& other) {
        bitcrusher = other.bitcrusher; distortion = other.distortion;
        filter = other.filter; eq = other.eq; compressor = other.compressor;
        formant = other.formant; delay = other.delay; chorus = other.chorus;
        tremolo = other.tremolo; phaser = other.phaser; flanger = other.flanger;
        ringMod = other.ringMod; sidechain = other.sidechain; reverb = other.reverb;
        stereoWidener = other.stereoWidener; tapeSaturation = other.tapeSaturation;
        unison = other.unison;

        // The vocoders' PARAMETERS, not their FFT state. Copying a running
        // analysis into another chain would splice one channel's spectrum
        // onto another's.
        pitchShifter.semitones = other.pitchShifter.semitones;
        pitchShifter.mix = other.pitchShifter.mix;
        formantShifter.semitones = other.formantShifter.semitones;
        formantShifter.mix = other.formantShifter.mix;
        autoTune.scaleMask = other.autoTune.scaleMask;
        autoTune.rootNote = other.autoTune.rootNote;
        autoTune.strength = other.autoTune.strength;
        autoTune.minimumMagnitude = other.autoTune.minimumMagnitude;
        autoTune.mix = other.autoTune.mix;

        pitchShiftEnabled = other.pitchShiftEnabled;
        formantShiftEnabled = other.formantShiftEnabled;
        autoTuneEnabled = other.autoTuneEnabled;

        // The mix, not the engine: an attached response and a running delay
        // line belong to the chain they were prepared for.
        convolutionEnabled = other.convolutionEnabled;
        convolutionMix = other.convolutionMix;

        tiltEqEnabled = other.tiltEqEnabled;
        graphicEqEnabled = other.graphicEqEnabled;
        dynamicEqEnabled = other.dynamicEqEnabled;

        bitcrusherEnabled = other.bitcrusherEnabled;
        distortionEnabled = other.distortionEnabled;
        filterEnabled = other.filterEnabled;
        eqEnabled = other.eqEnabled;
        compressorEnabled = other.compressorEnabled;
        formantEnabled = other.formantEnabled;
        delayEnabled = other.delayEnabled;
        chorusEnabled = other.chorusEnabled;
        tremoloEnabled = other.tremoloEnabled;
        phaserEnabled = other.phaserEnabled;
        flangerEnabled = other.flangerEnabled;
        ringModEnabled = other.ringModEnabled;
        sidechainEnabled = other.sidechainEnabled;
        reverbEnabled = other.reverbEnabled;
        stereoWidenerEnabled = other.stereoWidenerEnabled;
        tapeSaturationEnabled = other.tapeSaturationEnabled;
        sidechainSource = other.sidechainSource;

        m_orders[0] = other.m_orders[0];
        m_orders[1] = other.m_orders[1];
        m_active.store(other.m_active.load(std::memory_order_acquire),
                       std::memory_order_release);
        mix = other.mix;

        // Last, and the reason this function exists at all.
        bindAdapters();
    }
};

} // namespace ChiptuneTracker
