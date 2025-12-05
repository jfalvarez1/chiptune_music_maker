#pragma once

/*
 * ChiptuneTracker - Synthesizer Module
 *
 * Configurable oscillator with PolyBLEP antialiasing,
 * ADSR envelope, and per-voice state.
 */

#include "Types.h"
#include "Effects.h"
#include <cmath>
#include <array>

namespace ChiptuneTracker {

// ============================================================================
// Voice State (Single playing note)
// ============================================================================
struct Voice {
    bool active = false;
    int note = 60;
    float velocity = 1.0f;
    float frequency = 440.0f;

    // Oscillator state
    float phase = 0.0f;
    float phaseIncrement = 0.0f;

    // LFSR for noise
    uint16_t lfsr = 0x0001;

    // Envelope state
    enum class EnvStage { Attack, Decay, Sustain, Release, Off };
    EnvStage envStage = EnvStage::Off;
    float envLevel = 0.0f;
    float envTime = 0.0f;

    // Note timing
    float startTime = 0.0f;
    float releaseTime = 0.0f;

    // Fade in/out (in seconds)
    float fadeInDuration = 0.0f;
    float fadeOutDuration = 0.0f;
    float noteDuration = 0.0f;  // Total note duration in seconds (0 = unknown/manual release)

    void reset() {
        active = false;
        phase = 0.0f;
        envStage = EnvStage::Off;
        envLevel = 0.0f;
        envTime = 0.0f;
        lfsr = 0x0001;
        fadeInDuration = 0.0f;
        fadeOutDuration = 0.0f;
        noteDuration = 0.0f;
    }
};

// ============================================================================
// Synthesizer (Per-channel)
// ============================================================================
class Synthesizer {
public:
    static constexpr int MAX_VOICES = 8;  // Polyphony

    Synthesizer() {
        for (auto& voice : m_voices) {
            voice.reset();
        }
    }

    void setSampleRate(float sr) {
        m_sampleRate = sr;
        m_effects.setSampleRate(sr);
    }

    void setConfig(const OscillatorConfig& osc, const Envelope& env) {
        m_oscConfig = osc;
        m_envelope = env;
    }

    // Trigger a note (with optional fade parameters)
    void noteOn(int note, float velocity, float time,
                float fadeInSec = 0.0f, float fadeOutSec = 0.0f, float durationSec = 0.0f) {
        // Find free voice or steal oldest
        int voiceIndex = -1;
        float oldestTime = time;

        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!m_voices[i].active) {
                voiceIndex = i;
                break;
            }
            if (m_voices[i].startTime < oldestTime) {
                oldestTime = m_voices[i].startTime;
                voiceIndex = i;
            }
        }

        if (voiceIndex >= 0) {
            Voice& v = m_voices[voiceIndex];
            v.active = true;
            v.note = note;
            v.velocity = velocity;
            v.frequency = noteToFrequency(note);

            // Apply detune
            float detuneMult = std::pow(2.0f, m_oscConfig.detune / 1200.0f);
            v.frequency *= detuneMult;

            v.phaseIncrement = v.frequency / m_sampleRate;
            v.phase = m_oscConfig.phase;
            v.startTime = time;
            v.envStage = Voice::EnvStage::Attack;
            v.envTime = 0.0f;
            v.envLevel = 0.0f;
            v.lfsr = 0x0001;

            // Fade parameters
            v.fadeInDuration = fadeInSec;
            v.fadeOutDuration = fadeOutSec;
            v.noteDuration = durationSec;
        }
    }

    // Release a note
    void noteOff(int note, float time) {
        for (auto& v : m_voices) {
            if (v.active && v.note == note && v.envStage != Voice::EnvStage::Release) {
                v.envStage = Voice::EnvStage::Release;
                v.releaseTime = time;
                v.envTime = 0.0f;
            }
        }
    }

    // All notes off
    void allNotesOff() {
        for (auto& v : m_voices) {
            if (v.active) {
                v.envStage = Voice::EnvStage::Release;
                v.envTime = 0.0f;
            }
        }
    }

    // Generate one sample (called from audio thread)
    float process(float time) {
        float output = 0.0f;

        for (auto& voice : m_voices) {
            if (!voice.active) continue;

            // Generate oscillator sample
            float sample = generateOscillator(voice);

            // Apply envelope
            float envGain = processEnvelope(voice);

            // Apply fade in/out
            float fadeGain = calculateFadeGain(voice, time);

            sample *= envGain * voice.velocity * fadeGain;

            output += sample;
        }

        // Apply vibrato (global for this synth)
        if (m_vibratoEnabled) {
            // Vibrato modulates all voices' pitch - handled in frequency calc
        }

        // Apply effects chain
        output = m_effects.process(output, time);

        return output;
    }

    // Calculate fade in/out gain for a voice
    float calculateFadeGain(const Voice& voice, float currentTime) const {
        float elapsed = currentTime - voice.startTime;
        float fadeGain = 1.0f;

        // Fade in
        if (voice.fadeInDuration > 0.0f && elapsed < voice.fadeInDuration) {
            fadeGain *= elapsed / voice.fadeInDuration;
        }

        // Fade out (only if we know the note duration)
        if (voice.noteDuration > 0.0f && voice.fadeOutDuration > 0.0f) {
            float timeUntilEnd = voice.noteDuration - elapsed;
            if (timeUntilEnd < voice.fadeOutDuration && timeUntilEnd > 0.0f) {
                fadeGain *= timeUntilEnd / voice.fadeOutDuration;
            } else if (timeUntilEnd <= 0.0f) {
                fadeGain = 0.0f;
            }
        }

        return std::max(0.0f, std::min(1.0f, fadeGain));
    }

    // Accessors
    EffectsChain& effects() { return m_effects; }
    Vibrato& vibrato() { return m_vibrato; }
    Arpeggiator& arpeggiator() { return m_arpeggiator; }

    void setVibratoEnabled(bool enabled) { m_vibratoEnabled = enabled; }
    void setArpeggiatorEnabled(bool enabled) { m_arpeggiatorEnabled = enabled; }

    bool isActive() const {
        for (const auto& v : m_voices) {
            if (v.active) return true;
        }
        return false;
    }

private:
    // ========================================================================
    // Oscillator Generation
    // ========================================================================
    float generateOscillator(Voice& voice) {
        float sample = 0.0f;
        const float t = voice.phase;
        const float dt = voice.phaseIncrement;

        switch (m_oscConfig.type) {
            case OscillatorType::Pulse:
                sample = generatePulse(t, dt, m_oscConfig.pulseWidth);
                break;

            case OscillatorType::Triangle:
                sample = generateTriangle(t, m_oscConfig.triangleSlope);
                break;

            case OscillatorType::Sawtooth:
                sample = generateSawtooth(t, dt);
                break;

            case OscillatorType::Sine:
                sample = std::sin(t * TWO_PI);
                break;

            case OscillatorType::Noise:
                sample = generateNoise(voice);
                break;

            case OscillatorType::Custom:
                sample = generateTriangle(t, m_oscConfig.triangleSlope);
                break;
        }

        // Advance phase
        voice.phase += dt;
        if (voice.phase >= 1.0f) {
            voice.phase -= 1.0f;
        }

        return sample;
    }

    // Pulse wave with variable duty cycle and PolyBLEP
    float generatePulse(float t, float dt, float width) {
        float sample = (t < width) ? 1.0f : -1.0f;

        // PolyBLEP at rising edge (t = 0)
        sample += polyBlep(t, dt);
        // PolyBLEP at falling edge (t = width)
        sample -= polyBlep(std::fmod(t - width + 1.0f, 1.0f), dt);

        return sample;
    }

    // Triangle with adjustable slope
    float generateTriangle(float t, float slope) {
        // slope: 0.0 = saw down, 0.5 = triangle, 1.0 = saw up
        if (slope < 0.001f) slope = 0.001f;
        if (slope > 0.999f) slope = 0.999f;

        if (t < slope) {
            return -1.0f + 2.0f * (t / slope);
        } else {
            return 1.0f - 2.0f * ((t - slope) / (1.0f - slope));
        }
    }

    // Sawtooth with PolyBLEP
    float generateSawtooth(float t, float dt) {
        float sample = 2.0f * t - 1.0f;
        sample -= polyBlep(t, dt);
        return sample;
    }

    // LFSR Noise (NES-style)
    float generateNoise(Voice& voice) {
        // Clock LFSR based on frequency
        static float noiseAccum = 0.0f;
        noiseAccum += voice.phaseIncrement * 16.0f;

        while (noiseAccum >= 1.0f) {
            noiseAccum -= 1.0f;

            uint16_t feedback;
            if (m_oscConfig.noiseShortMode) {
                // Short mode: bits 0 and 1 (more metallic)
                feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            } else {
                // Long mode: bits 0 and 6 (white noise)
                feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 6)) & 1;
            }
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }

        return (voice.lfsr & 1) ? 1.0f : -1.0f;
    }

    // PolyBLEP antialiasing
    float polyBlep(float t, float dt) {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0f;
        } else if (t > 1.0f - dt) {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    // ========================================================================
    // Envelope Processing
    // ========================================================================
    float processEnvelope(Voice& voice) {
        float deltaTime = 1.0f / m_sampleRate;
        voice.envTime += deltaTime;

        switch (voice.envStage) {
            case Voice::EnvStage::Attack:
                if (m_envelope.attack > 0.0f) {
                    voice.envLevel = voice.envTime / m_envelope.attack;
                    if (voice.envLevel >= 1.0f) {
                        voice.envLevel = 1.0f;
                        voice.envStage = Voice::EnvStage::Decay;
                        voice.envTime = 0.0f;
                    }
                } else {
                    voice.envLevel = 1.0f;
                    voice.envStage = Voice::EnvStage::Decay;
                    voice.envTime = 0.0f;
                }
                break;

            case Voice::EnvStage::Decay:
                if (m_envelope.decay > 0.0f) {
                    float t = voice.envTime / m_envelope.decay;
                    voice.envLevel = 1.0f - t * (1.0f - m_envelope.sustain);
                    if (t >= 1.0f) {
                        voice.envLevel = m_envelope.sustain;
                        voice.envStage = Voice::EnvStage::Sustain;
                    }
                } else {
                    voice.envLevel = m_envelope.sustain;
                    voice.envStage = Voice::EnvStage::Sustain;
                }
                break;

            case Voice::EnvStage::Sustain:
                voice.envLevel = m_envelope.sustain;
                break;

            case Voice::EnvStage::Release:
                if (m_envelope.release > 0.0f) {
                    float t = voice.envTime / m_envelope.release;
                    voice.envLevel = m_envelope.sustain * (1.0f - t);
                    if (t >= 1.0f) {
                        voice.envLevel = 0.0f;
                        voice.envStage = Voice::EnvStage::Off;
                        voice.active = false;
                    }
                } else {
                    voice.envLevel = 0.0f;
                    voice.envStage = Voice::EnvStage::Off;
                    voice.active = false;
                }
                break;

            case Voice::EnvStage::Off:
                voice.envLevel = 0.0f;
                voice.active = false;
                break;
        }

        return voice.envLevel;
    }

private:
    float m_sampleRate = 44100.0f;
    std::array<Voice, MAX_VOICES> m_voices;

    OscillatorConfig m_oscConfig;
    Envelope m_envelope;

    EffectsChain m_effects;
    Vibrato m_vibrato;
    Arpeggiator m_arpeggiator;

    bool m_vibratoEnabled = false;
    bool m_arpeggiatorEnabled = false;
};

} // namespace ChiptuneTracker
