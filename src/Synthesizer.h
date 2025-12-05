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

    // Per-voice oscillator type (allows different sounds per note)
    OscillatorType oscillatorType = OscillatorType::Pulse;

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
        oscillatorType = OscillatorType::Pulse;
    }
};

// ============================================================================
// Helper: Check if oscillator type is a drum
// ============================================================================
inline bool isDrumType(OscillatorType type) {
    switch (type) {
        case OscillatorType::Kick:
        case OscillatorType::Kick808:
        case OscillatorType::KickHard:
        case OscillatorType::KickSoft:
        case OscillatorType::Snare:
        case OscillatorType::Snare808:
        case OscillatorType::SnareRim:
        case OscillatorType::Clap:
        case OscillatorType::HiHat:
        case OscillatorType::HiHatOpen:
        case OscillatorType::HiHatPedal:
        case OscillatorType::Tom:
        case OscillatorType::TomLow:
        case OscillatorType::TomHigh:
        case OscillatorType::Crash:
        case OscillatorType::Ride:
        case OscillatorType::Cowbell:
        case OscillatorType::Clave:
        case OscillatorType::Conga:
        case OscillatorType::Maracas:
        case OscillatorType::Tambourine:
            return true;
        default:
            return false;
    }
}

// Get drum decay time in seconds for BPM-based duration calculation
inline float getDrumDecayTime(OscillatorType type) {
    switch (type) {
        case OscillatorType::Kick:       return 0.5f;
        case OscillatorType::Kick808:    return 0.8f;
        case OscillatorType::KickHard:   return 0.3f;
        case OscillatorType::KickSoft:   return 0.6f;
        case OscillatorType::Snare:      return 0.15f;
        case OscillatorType::Snare808:   return 0.25f;
        case OscillatorType::SnareRim:   return 0.08f;
        case OscillatorType::Clap:       return 0.25f;
        case OscillatorType::HiHat:      return 0.1f;
        case OscillatorType::HiHatOpen:  return 0.5f;
        case OscillatorType::HiHatPedal: return 0.05f;
        case OscillatorType::Tom:        return 0.5f;
        case OscillatorType::TomLow:     return 0.6f;
        case OscillatorType::TomHigh:    return 0.4f;
        case OscillatorType::Crash:      return 1.5f;
        case OscillatorType::Ride:       return 0.8f;
        case OscillatorType::Cowbell:    return 0.3f;
        case OscillatorType::Clave:      return 0.05f;
        case OscillatorType::Conga:      return 0.4f;
        case OscillatorType::Maracas:    return 0.1f;
        case OscillatorType::Tambourine: return 0.2f;
        default:                         return 0.5f;
    }
}

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

    // Trigger a note (with optional fade parameters and oscillator type)
    void noteOn(int note, float velocity, float time,
                float fadeInSec = 0.0f, float fadeOutSec = 0.0f, float durationSec = 0.0f,
                OscillatorType oscType = OscillatorType::Pulse) {
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

            // Per-note oscillator type
            v.oscillatorType = oscType;
        }
    }

    // Release a note
    void noteOff(int note, float time) {
        for (auto& v : m_voices) {
            if (v.active && v.note == note && v.envStage != Voice::EnvStage::Release) {
                // Drums always play their full decay - ignore noteOff entirely
                if (isDrumType(v.oscillatorType)) {
                    continue;  // Let drum play out naturally
                }

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
                // Drums always play their full decay - let them continue
                if (isDrumType(v.oscillatorType)) {
                    continue;  // Let drum play out naturally
                }

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

            // Check if this is a drum sound (drums have their own internal envelope)
            bool isDrum = isDrumType(voice.oscillatorType);

            // Generate oscillator sample
            float sample = generateOscillator(voice);

            // Apply envelope (skip ADSR for drums - they have internal envelopes)
            float envGain = 1.0f;
            if (isDrum) {
                // Drums manage their own envelope internally
                // Just update envTime for the drum generators
                voice.envTime += 1.0f / m_sampleRate;
                // Deactivate drum voice after it's finished (based on decay time)
                float maxDrumTime = getDrumDecayTime(voice.oscillatorType) * 3.0f;  // 3x decay time
                if (voice.envTime > maxDrumTime) {
                    voice.active = false;
                }
            } else {
                envGain = processEnvelope(voice);
            }

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

        // Use per-voice oscillator type (allows different sounds per note)
        switch (voice.oscillatorType) {
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

            // Kicks
            case OscillatorType::Kick:
                sample = generateKick(voice);
                break;
            case OscillatorType::Kick808:
                sample = generateKick808(voice);
                break;
            case OscillatorType::KickHard:
                sample = generateKickHard(voice);
                break;
            case OscillatorType::KickSoft:
                sample = generateKickSoft(voice);
                break;

            // Snares
            case OscillatorType::Snare:
                sample = generateSnare(voice);
                break;
            case OscillatorType::Snare808:
                sample = generateSnare808(voice);
                break;
            case OscillatorType::SnareRim:
                sample = generateSnareRim(voice);
                break;
            case OscillatorType::Clap:
                sample = generateClap(voice);
                break;

            // Hi-Hats
            case OscillatorType::HiHat:
                sample = generateHiHat(voice);
                break;
            case OscillatorType::HiHatOpen:
                sample = generateHiHatOpen(voice);
                break;
            case OscillatorType::HiHatPedal:
                sample = generateHiHatPedal(voice);
                break;

            // Toms
            case OscillatorType::Tom:
                sample = generateTom(voice);
                break;
            case OscillatorType::TomLow:
                sample = generateTomLow(voice);
                break;
            case OscillatorType::TomHigh:
                sample = generateTomHigh(voice);
                break;

            // Cymbals
            case OscillatorType::Crash:
                sample = generateCrash(voice);
                break;
            case OscillatorType::Ride:
                sample = generateRide(voice);
                break;

            // Percussion
            case OscillatorType::Cowbell:
                sample = generateCowbell(voice);
                break;
            case OscillatorType::Clave:
                sample = generateClave(voice);
                break;
            case OscillatorType::Conga:
                sample = generateConga(voice);
                break;
            case OscillatorType::Maracas:
                sample = generateMaracas(voice);
                break;
            case OscillatorType::Tambourine:
                sample = generateTambourine(voice);
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
    // Drum Synthesis - Classic chiptune/8-bit style
    // ========================================================================

    // Kick drum - deep "boom" with pitch sweep (808/NES style)
    float generateKick(Voice& voice) {
        float noteTime = voice.envTime;

        // Single clean exponential decay - no double envelope!
        float envelope = std::exp(-noteTime * 8.0f);

        // Dramatic pitch sweep from ~150Hz down to ~45Hz
        float startFreq = 150.0f;
        float endFreq = 45.0f;
        float pitchEnv = std::exp(-noteTime * 40.0f);  // Fast pitch drop
        float freq = endFreq + (startFreq - endFreq) * pitchEnv;

        // Generate sine wave with pitch sweep
        float sample = std::sin(voice.phase * TWO_PI);

        // Update phase increment for pitch sweep
        voice.phaseIncrement = freq / m_sampleRate;

        // Add slight distortion/saturation for punch
        sample = std::tanh(sample * 1.8f);

        return sample * envelope;
    }

    // Snare drum - sharp "tsk" sound with noise
    float generateSnare(Voice& voice) {
        float noteTime = voice.envTime;

        // Very fast decay for sharp "tsk" sound
        float envelope = std::exp(-noteTime * 35.0f);

        // Sharp click/transient at the very start
        float click = 0.0f;
        if (noteTime < 0.002f) {
            click = (1.0f - noteTime / 0.002f) * 0.7f;
        }

        // High-frequency noise - clock LFSR multiple times for brighter sound
        float noise = 0.0f;
        for (int i = 0; i < 6; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;  // Short mode for brighter sound
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        // Small tonal "pop" for the body
        float toneEnv = std::exp(-noteTime * 50.0f);
        float tone = std::sin(voice.phase * TWO_PI * 3.0f) * toneEnv * 0.2f;

        return (click + noise * 0.6f + tone) * envelope;
    }

    // Hi-hat - metallic "tsss" with ring (short mode LFSR)
    float generateHiHat(Voice& voice) {
        float noteTime = voice.envTime;

        // Very fast decay - classic closed hi-hat
        float envelope = std::exp(-noteTime * 50.0f);

        // Multiple square waves at inharmonic frequencies for metallic ring
        float metallic = 0.0f;
        metallic += (std::fmod(voice.phase * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;
        metallic += (std::fmod(voice.phase * 1.47f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;
        metallic += (std::fmod(voice.phase * 1.83f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;

        // Short-mode LFSR noise for that classic metallic hi-hat sound
        float noise = 0.0f;
        for (int i = 0; i < 8; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;  // Short mode = metallic
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        // High frequency phase for brightness
        voice.phaseIncrement = 800.0f / m_sampleRate;

        return (noise * 0.6f + metallic * 0.4f) * envelope;
    }

    // Tom drum - pitched "bom" with resonance
    float generateTom(Voice& voice) {
        float noteTime = voice.envTime;

        // Medium decay with slight sustain
        float envelope = std::exp(-noteTime * 8.0f);

        // Pitch drops for that classic tom sound
        float pitchEnv = std::exp(-noteTime * 20.0f);
        float pitchMult = 1.0f + 1.2f * pitchEnv;  // More dramatic pitch drop

        // Main tone
        float sample = std::sin(voice.phase * pitchMult * TWO_PI);

        // Add second harmonic for body
        float harmonic = std::sin(voice.phase * pitchMult * TWO_PI * 2.0f) * 0.3f;

        // Soft attack click
        float click = 0.0f;
        if (noteTime < 0.004f) {
            click = (1.0f - noteTime / 0.004f) * 0.3f;
        }

        // Slight saturation for warmth
        sample = std::tanh((sample + harmonic) * 1.2f);

        return (sample * 0.8f + click) * envelope;
    }

    // ========================================================================
    // Additional Drum Types
    // ========================================================================

    // Kick808 - Deep 808 kick with more sub-bass
    float generateKick808(Voice& voice) {
        float noteTime = voice.envTime;

        // Longer decay for that deep 808 rumble
        float envelope = std::exp(-noteTime * 5.0f);

        // Lower frequencies for sub-bass
        float startFreq = 120.0f;
        float endFreq = 35.0f;
        float pitchEnv = std::exp(-noteTime * 25.0f);
        float freq = endFreq + (startFreq - endFreq) * pitchEnv;

        float sample = std::sin(voice.phase * TWO_PI);
        voice.phaseIncrement = freq / m_sampleRate;

        // Soft saturation for warmth
        sample = std::tanh(sample * 1.3f);

        return sample * envelope;
    }

    // KickHard - Punchy, tight kick
    float generateKickHard(Voice& voice) {
        float noteTime = voice.envTime;

        // Fast decay for punch
        float envelope = std::exp(-noteTime * 15.0f);

        // Higher start frequency for more attack
        float startFreq = 200.0f;
        float endFreq = 55.0f;
        float pitchEnv = std::exp(-noteTime * 60.0f);
        float freq = endFreq + (startFreq - endFreq) * pitchEnv;

        float sample = std::sin(voice.phase * TWO_PI);
        voice.phaseIncrement = freq / m_sampleRate;

        // Hard click transient
        float click = 0.0f;
        if (noteTime < 0.003f) {
            click = (1.0f - noteTime / 0.003f) * 0.5f;
        }

        // More distortion for punch
        sample = std::tanh(sample * 2.5f);

        return (sample + click) * envelope;
    }

    // KickSoft - Soft, warm kick
    float generateKickSoft(Voice& voice) {
        float noteTime = voice.envTime;

        // Medium decay
        float envelope = std::exp(-noteTime * 7.0f);

        // Lower, gentler sweep
        float startFreq = 100.0f;
        float endFreq = 40.0f;
        float pitchEnv = std::exp(-noteTime * 20.0f);
        float freq = endFreq + (startFreq - endFreq) * pitchEnv;

        float sample = std::sin(voice.phase * TWO_PI);
        voice.phaseIncrement = freq / m_sampleRate;

        // Minimal distortion for softness
        sample = std::tanh(sample * 1.1f);

        return sample * envelope;
    }

    // Snare808 - Classic 808 snare, more tonal
    float generateSnare808(Voice& voice) {
        float noteTime = voice.envTime;

        float envelope = std::exp(-noteTime * 20.0f);

        // Tonal body - two detuned oscillators
        float tone1 = std::sin(voice.phase * TWO_PI * 180.0f / voice.frequency);
        float tone2 = std::sin(voice.phase * TWO_PI * 330.0f / voice.frequency);
        float tonal = (tone1 + tone2 * 0.7f) * std::exp(-noteTime * 25.0f);

        // Noise component
        float noise = 0.0f;
        for (int i = 0; i < 4; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.5f;

        return (tonal * 0.6f + noise * 0.4f) * envelope;
    }

    // SnareRim - Rimshot, clicky
    float generateSnareRim(Voice& voice) {
        float noteTime = voice.envTime;

        // Very fast decay
        float envelope = std::exp(-noteTime * 60.0f);

        // Sharp click
        float click = 0.0f;
        if (noteTime < 0.001f) {
            click = (1.0f - noteTime / 0.001f);
        }

        // High frequency ping
        float ping = std::sin(voice.phase * TWO_PI * 4.0f) * std::exp(-noteTime * 80.0f);

        return (click * 0.7f + ping * 0.3f) * envelope;
    }

    // Clap - Hand clap (multiple noise bursts)
    float generateClap(Voice& voice) {
        float noteTime = voice.envTime;

        // Multiple bursts effect
        float burstEnv = 0.0f;
        float burstTime = 0.015f;  // Time between bursts
        for (int i = 0; i < 4; ++i) {
            float burstStart = i * burstTime;
            float localTime = noteTime - burstStart;
            if (localTime >= 0.0f && localTime < burstTime) {
                burstEnv += std::exp(-localTime * 100.0f) * (1.0f - i * 0.15f);
            }
        }

        // Final tail
        float tail = std::exp(-noteTime * 15.0f) * 0.5f;

        // Noise
        float noise = 0.0f;
        for (int i = 0; i < 6; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        return noise * (burstEnv + tail) * 0.6f;
    }

    // HiHatOpen - Open hi-hat, longer decay
    float generateHiHatOpen(Voice& voice) {
        float noteTime = voice.envTime;

        // Longer decay than closed
        float envelope = std::exp(-noteTime * 10.0f);

        // Metallic frequencies
        float metallic = 0.0f;
        metallic += (std::fmod(voice.phase * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;
        metallic += (std::fmod(voice.phase * 1.47f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;
        metallic += (std::fmod(voice.phase * 1.83f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;
        metallic += (std::fmod(voice.phase * 2.17f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;

        // Noise
        float noise = 0.0f;
        for (int i = 0; i < 8; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        voice.phaseIncrement = 800.0f / m_sampleRate;

        return (noise * 0.5f + metallic * 0.5f) * envelope;
    }

    // HiHatPedal - Very short, muted
    float generateHiHatPedal(Voice& voice) {
        float noteTime = voice.envTime;

        // Ultra-fast decay
        float envelope = std::exp(-noteTime * 100.0f);

        // Minimal metallic
        float metallic = (std::fmod(voice.phase * 1.5f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.3f;

        // Muted noise
        float noise = 0.0f;
        for (int i = 0; i < 4; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.5f;

        voice.phaseIncrement = 600.0f / m_sampleRate;

        return (noise + metallic) * envelope;
    }

    // TomLow - Floor tom
    float generateTomLow(Voice& voice) {
        float noteTime = voice.envTime;

        float envelope = std::exp(-noteTime * 6.0f);

        // Lower base frequency
        float pitchEnv = std::exp(-noteTime * 15.0f);
        float basePitch = 80.0f;  // Lower than mid tom
        float freq = basePitch * (1.0f + 0.8f * pitchEnv);

        float sample = std::sin(voice.phase * TWO_PI);
        voice.phaseIncrement = freq / m_sampleRate;

        float harmonic = std::sin(voice.phase * TWO_PI * 2.0f) * 0.2f;
        sample = std::tanh((sample + harmonic) * 1.3f);

        return sample * envelope;
    }

    // TomHigh - High tom
    float generateTomHigh(Voice& voice) {
        float noteTime = voice.envTime;

        float envelope = std::exp(-noteTime * 12.0f);

        // Higher base frequency
        float pitchEnv = std::exp(-noteTime * 25.0f);
        float basePitch = 200.0f;  // Higher than mid tom
        float freq = basePitch * (1.0f + 0.6f * pitchEnv);

        float sample = std::sin(voice.phase * TWO_PI);
        voice.phaseIncrement = freq / m_sampleRate;

        float harmonic = std::sin(voice.phase * TWO_PI * 2.0f) * 0.25f;
        sample = std::tanh((sample + harmonic) * 1.2f);

        return sample * envelope;
    }

    // Crash - Crash cymbal
    float generateCrash(Voice& voice) {
        float noteTime = voice.envTime;

        // Long decay
        float envelope = std::exp(-noteTime * 3.0f);

        // Initial burst
        float attack = (noteTime < 0.01f) ? (1.0f + 2.0f * (1.0f - noteTime / 0.01f)) : 1.0f;

        // Complex metallic frequencies
        float metallic = 0.0f;
        metallic += (std::fmod(voice.phase * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        metallic += (std::fmod(voice.phase * 1.34f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        metallic += (std::fmod(voice.phase * 1.87f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.12f;
        metallic += (std::fmod(voice.phase * 2.43f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.1f;
        metallic += (std::fmod(voice.phase * 3.17f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.08f;

        // Noise
        float noise = 0.0f;
        for (int i = 0; i < 10; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        voice.phaseIncrement = 1000.0f / m_sampleRate;

        return (noise * 0.4f + metallic * 0.6f) * envelope * attack;
    }

    // Ride - Ride cymbal
    float generateRide(Voice& voice) {
        float noteTime = voice.envTime;

        // Medium-long decay with sustain
        float envelope = std::exp(-noteTime * 5.0f);

        // Ping attack
        float ping = std::sin(voice.phase * TWO_PI * 3.0f) * std::exp(-noteTime * 30.0f) * 0.4f;

        // Metallic sustain
        float metallic = 0.0f;
        metallic += (std::fmod(voice.phase * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.12f;
        metallic += (std::fmod(voice.phase * 1.5f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.12f;
        metallic += (std::fmod(voice.phase * 2.1f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.1f;

        // Light noise
        float noise = 0.0f;
        for (int i = 0; i < 6; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.2f;

        voice.phaseIncrement = 900.0f / m_sampleRate;

        return (ping + metallic + noise) * envelope;
    }

    // Cowbell - 808 cowbell
    float generateCowbell(Voice& voice) {
        float noteTime = voice.envTime;

        float envelope = std::exp(-noteTime * 15.0f);

        // Two detuned square waves (classic 808 cowbell frequencies)
        float freq1 = 587.0f;  // D5
        float freq2 = 845.0f;  // Slightly sharp G#5

        float osc1 = (std::fmod(voice.phase * freq1 / voice.frequency, 1.0f) < 0.5f) ? 1.0f : -1.0f;
        float osc2 = (std::fmod(voice.phase * freq2 / voice.frequency, 1.0f) < 0.5f) ? 1.0f : -1.0f;

        float sample = (osc1 + osc2 * 0.7f) * 0.4f;

        return sample * envelope;
    }

    // Clave - Wood block click
    float generateClave(Voice& voice) {
        float noteTime = voice.envTime;

        // Very short
        float envelope = std::exp(-noteTime * 100.0f);

        // High frequency sine burst
        float freq = 2500.0f;
        float sample = std::sin(voice.phase * TWO_PI * freq / voice.frequency);

        // Sharp attack
        float attack = (noteTime < 0.001f) ? 1.0f : std::exp(-(noteTime - 0.001f) * 200.0f);

        return sample * envelope * attack * 0.7f;
    }

    // Conga - Conga drum
    float generateConga(Voice& voice) {
        float noteTime = voice.envTime;

        float envelope = std::exp(-noteTime * 12.0f);

        // Pitch envelope
        float pitchEnv = std::exp(-noteTime * 30.0f);
        float basePitch = 250.0f;
        float freq = basePitch * (1.0f + 0.5f * pitchEnv);

        float sample = std::sin(voice.phase * TWO_PI);
        voice.phaseIncrement = freq / m_sampleRate;

        // Add some harmonics for body
        float harm2 = std::sin(voice.phase * TWO_PI * 2.0f) * 0.3f;
        float harm3 = std::sin(voice.phase * TWO_PI * 3.0f) * 0.15f;

        sample = std::tanh((sample + harm2 + harm3) * 1.4f);

        // Slap attack
        float slap = 0.0f;
        if (noteTime < 0.005f) {
            slap = (1.0f - noteTime / 0.005f) * 0.4f;
        }

        return (sample + slap) * envelope;
    }

    // Maracas - Shaker
    float generateMaracas(Voice& voice) {
        float noteTime = voice.envTime;

        // Fast decay
        float envelope = std::exp(-noteTime * 50.0f);

        // High-frequency noise
        float noise = 0.0f;
        for (int i = 0; i < 12; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 6)) & 1;  // Long mode for variety
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        // High-pass effect (reduce low frequencies)
        noise *= 0.5f;

        return noise * envelope;
    }

    // Tambourine - Jingly metallic
    float generateTambourine(Voice& voice) {
        float noteTime = voice.envTime;

        float envelope = std::exp(-noteTime * 25.0f);

        // Multiple metallic jingles
        float jingle = 0.0f;
        jingle += (std::fmod(voice.phase * 2.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        jingle += (std::fmod(voice.phase * 2.73f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        jingle += (std::fmod(voice.phase * 3.41f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.12f;
        jingle += (std::fmod(voice.phase * 4.17f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.1f;

        // Noise component
        float noise = 0.0f;
        for (int i = 0; i < 8; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.3f;

        voice.phaseIncrement = 1200.0f / m_sampleRate;

        return (jingle + noise) * envelope;
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
