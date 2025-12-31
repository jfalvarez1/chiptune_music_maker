#pragma once

/*
 * ChiptuneTracker - Master Bus Effects
 *
 * Professional mastering-grade effects for the final mix:
 * - Master EQ (for final tonal balance)
 * - Master Compressor (glue compression)
 * - Limiter (prevent clipping, competitive loudness)
 * - LUFS Metering (loudness standards for streaming)
 */

#include "Effects.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace ChiptuneTracker {

// ============================================================================
// Limiter - Brick-wall limiting for preventing clipping
// ============================================================================
class Limiter {
public:
    float ceiling = -0.1f;      // dB (typically -0.1 to -0.3 dB to prevent clipping)
    float release = 0.05f;      // Seconds (fast release for transparent limiting)

    void setSampleRate(float sr) {
        m_sampleRate = sr;
    }

    float process(float input) {
        // Convert ceiling from dB to linear
        float ceilingLinear = std::pow(10.0f, ceiling / 20.0f);

        // Calculate input level
        float inputLevel = std::abs(input);

        // Calculate gain reduction needed
        float gainReduction = 1.0f;
        if (inputLevel > ceilingLinear) {
            gainReduction = ceilingLinear / inputLevel;
        }

        // Smooth gain reduction with fast attack, medium release
        float attackCoeff = 1.0f - std::exp(-1.0f / (0.001f * m_sampleRate)); // 1ms attack
        float releaseCoeff = 1.0f - std::exp(-1.0f / (release * m_sampleRate));

        if (gainReduction < m_envelope) {
            // Attack (fast)
            m_envelope = gainReduction + attackCoeff * (m_envelope - gainReduction);
        } else {
            // Release (slower)
            m_envelope = gainReduction + releaseCoeff * (m_envelope - gainReduction);
        }

        return input * m_envelope;
    }

    void reset() {
        m_envelope = 1.0f;
    }

    // Get current gain reduction in dB (for metering)
    float getGainReductionDB() const {
        return 20.0f * std::log10(m_envelope + 0.0001f);
    }

private:
    float m_envelope = 1.0f;
    float m_sampleRate = 48000.0f;
};

// ============================================================================
// LUFS Meter - ITU-R BS.1770 loudness metering for streaming platforms
// ============================================================================
class LUFSMeter {
public:
    void setSampleRate(float sr) {
        m_sampleRate = sr;
        reset();
    }

    void process(float left, float right) {
        // K-weighting filter (simplified approximation)
        // High shelf boost at high frequencies
        // NOTE: Full ITU-R BS.1770 requires two-stage filtering
        // This is a simplified version for real-time metering

        float sample = (left + right) * 0.5f; // Mono average

        // Simple moving average for loudness
        m_buffer[m_bufferPos] = sample * sample; // Power (squared)
        m_bufferPos = (m_bufferPos + 1) % BUFFER_SIZE;

        // Calculate average power over ~3 seconds
        float sumPower = 0.0f;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            sumPower += m_buffer[i];
        }
        float avgPower = sumPower / BUFFER_SIZE;

        // Convert to LUFS (simplified formula)
        // LUFS = -0.691 + 10 * log10(avgPower)
        if (avgPower > 1e-10f) {
            m_currentLUFS = -0.691f + 10.0f * std::log10(avgPower);
        } else {
            m_currentLUFS = -70.0f; // Silence
        }
    }

    float getLUFS() const {
        return m_currentLUFS;
    }

    void reset() {
        m_buffer.fill(0.0f);
        m_bufferPos = 0;
        m_currentLUFS = -70.0f;
    }

private:
    static constexpr int BUFFER_SIZE = 144000; // ~3 seconds at 48kHz
    std::array<float, BUFFER_SIZE> m_buffer = {};
    int m_bufferPos = 0;
    float m_currentLUFS = -70.0f;
    float m_sampleRate = 48000.0f;
};

// ============================================================================
// Master Effects Chain - Final processing before output
// ============================================================================
struct MasterEffects {
    // Effects instances
    ThreeBandEQ eq;
    Compressor compressor;
    Limiter limiter;
    LUFSMeter lufsMeter;

    // Enable flags
    bool eqEnabled = false;
    bool compressorEnabled = false;
    bool limiterEnabled = true;  // Usually always on for safety

    void setSampleRate(float sr) {
        eq.setSampleRate(sr);
        compressor.setSampleRate(sr);
        limiter.setSampleRate(sr);
        lufsMeter.setSampleRate(sr);
    }

    // Process stereo signal
    void process(float& left, float& right) {
        // EQ first (tonal shaping)
        if (eqEnabled) {
            left = eq.process(left);
            right = eq.process(right);
        }

        // Glue compression (subtle, transparent)
        if (compressorEnabled) {
            left = compressor.process(left);
            right = compressor.process(right);
        }

        // Limiter last (prevent clipping)
        if (limiterEnabled) {
            left = limiter.process(left);
            right = limiter.process(right);
        }

        // LUFS metering (always on, doesn't affect signal)
        lufsMeter.process(left, right);
    }

    void reset() {
        eq.reset();
        compressor.reset();
        limiter.reset();
        lufsMeter.reset();
    }

    // Get loudness for display
    float getLUFS() const {
        return lufsMeter.getLUFS();
    }

    // Get limiter gain reduction for metering
    float getLimiterGainReductionDB() const {
        return limiter.getGainReductionDB();
    }
};

// ============================================================================
// Master Bus Presets - Common mastering settings
// ============================================================================
namespace MasterPresets {

    inline void applyLoudnessPreset(MasterEffects& fx, const char* preset) {
        if (strcmp(preset, "Spotify") == 0) {
            // Target: -14 LUFS integrated
            fx.compressorEnabled = true;
            fx.compressor.threshold = -12.0f;
            fx.compressor.ratio = 2.5f;
            fx.compressor.attack = 0.01f;
            fx.compressor.release = 0.1f;
            fx.compressor.makeupGain = 2.0f;

            fx.limiterEnabled = true;
            fx.limiter.ceiling = -0.3f;
            fx.limiter.release = 0.05f;
        }
        else if (strcmp(preset, "Apple Music") == 0) {
            // Target: -16 LUFS integrated
            fx.compressorEnabled = true;
            fx.compressor.threshold = -14.0f;
            fx.compressor.ratio = 2.0f;
            fx.compressor.attack = 0.01f;
            fx.compressor.release = 0.12f;
            fx.compressor.makeupGain = 1.5f;

            fx.limiterEnabled = true;
            fx.limiter.ceiling = -0.5f;
            fx.limiter.release = 0.08f;
        }
        else if (strcmp(preset, "YouTube") == 0) {
            // Target: -13 LUFS integrated
            fx.compressorEnabled = true;
            fx.compressor.threshold = -11.0f;
            fx.compressor.ratio = 3.0f;
            fx.compressor.attack = 0.01f;
            fx.compressor.release = 0.1f;
            fx.compressor.makeupGain = 2.5f;

            fx.limiterEnabled = true;
            fx.limiter.ceiling = -0.2f;
            fx.limiter.release = 0.05f;
        }
        else if (strcmp(preset, "SoundCloud") == 0) {
            // Target: -11 LUFS integrated (louder platform)
            fx.compressorEnabled = true;
            fx.compressor.threshold = -10.0f;
            fx.compressor.ratio = 3.5f;
            fx.compressor.attack = 0.008f;
            fx.compressor.release = 0.09f;
            fx.compressor.makeupGain = 3.0f;

            fx.limiterEnabled = true;
            fx.limiter.ceiling = -0.1f;
            fx.limiter.release = 0.04f;
        }
        else if (strcmp(preset, "CD Master") == 0) {
            // Target: -9 LUFS integrated (hot mastering)
            fx.compressorEnabled = true;
            fx.compressor.threshold = -8.0f;
            fx.compressor.ratio = 4.0f;
            fx.compressor.attack = 0.005f;
            fx.compressor.release = 0.08f;
            fx.compressor.makeupGain = 4.0f;

            fx.limiterEnabled = true;
            fx.limiter.ceiling = -0.1f;
            fx.limiter.release = 0.03f;
        }
        else if (strcmp(preset, "Off") == 0) {
            // No mastering
            fx.compressorEnabled = false;
            fx.limiterEnabled = false;
        }
    }

} // namespace MasterPresets

} // namespace ChiptuneTracker
