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
    float baseFrequency = 440.0f;  // Original frequency (before effects)

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
    float realTimeElapsed = 0.0f;  // Real time elapsed since noteOn (for preview cutoff)

    // Fade in/out (in seconds)
    float fadeInDuration = 0.0f;
    float fadeOutDuration = 0.0f;
    float noteDuration = 0.0f;  // Total note duration in seconds (0 = unknown/manual release)

    // Per-voice oscillator type (allows different sounds per note)
    OscillatorType oscillatorType = OscillatorType::Pulse;

    // Per-note effects
    float vibratoDepth = 0.0f;      // 0.0 to 1.0 (semitones of pitch wobble)
    float vibratoSpeed = 5.0f;      // Hz (default 5 Hz vibrato rate)
    float vibratoPhase = 0.0f;      // Current vibrato LFO phase
    int arpeggioX = 0;              // First semitone offset (0-15)
    int arpeggioY = 0;              // Second semitone offset (0-15)
    int arpeggioStep = 0;           // Current arpeggio step (0, 1, or 2)
    float arpeggioTimer = 0.0f;     // Timer for arpeggio stepping
    float slideTarget = 0.0f;       // Target frequency for portamento (0 = no slide)
    float slideSpeed = 0.0f;        // Slide speed (semitones per second)

    void reset() {
        active = false;
        phase = 0.0f;
        envStage = EnvStage::Off;
        envLevel = 0.0f;
        envTime = 0.0f;
        realTimeElapsed = 0.0f;
        lfsr = 0x0001;
        fadeInDuration = 0.0f;
        fadeOutDuration = 0.0f;
        noteDuration = 0.0f;
        oscillatorType = OscillatorType::Pulse;
        vibratoDepth = 0.0f;
        vibratoSpeed = 5.0f;
        vibratoPhase = 0.0f;
        arpeggioX = 0;
        arpeggioY = 0;
        arpeggioStep = 0;
        arpeggioTimer = 0.0f;
        slideTarget = 0.0f;
        slideSpeed = 0.0f;
        baseFrequency = 440.0f;
    }
};

// ============================================================================
// Helper: Check if oscillator type is a synth preset
// ============================================================================
inline bool isSynthType(OscillatorType type) {
    switch (type) {
        case OscillatorType::SynthLead:
        case OscillatorType::SynthPad:
        case OscillatorType::SynthBass:
        case OscillatorType::SynthPluck:
        case OscillatorType::SynthArp:
        case OscillatorType::SynthOrgan:
        case OscillatorType::SynthStrings:
        case OscillatorType::SynthBrass:
        case OscillatorType::SynthChip:
        case OscillatorType::SynthBell:
        case OscillatorType::SynthwaveLead:
        case OscillatorType::SynthwaveBass:
        case OscillatorType::SynthwavePad:
        case OscillatorType::SynthwaveArp:
        case OscillatorType::SynthwaveChord:
        case OscillatorType::SynthwaveFM:
        // Techno/Electronic
        case OscillatorType::AcidBass:
        case OscillatorType::TechnoStab:
        case OscillatorType::Hoover:
        case OscillatorType::RaveChord:
        case OscillatorType::Reese:
        // Hip Hop
        case OscillatorType::SubBass808:
        case OscillatorType::LoFiKeys:
        case OscillatorType::VinylNoise:
        case OscillatorType::TrapLead:
        // Additional Synthwave
        case OscillatorType::GatedPad:
        case OscillatorType::PolySynth:
        case OscillatorType::SyncLead:
        // Reggaeton synths
        case OscillatorType::ReggaetonBass:
        case OscillatorType::LatinBrass:
            return true;
        default:
            return false;
    }
}

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
        // Reggaeton drums
        case OscillatorType::Guira:
        case OscillatorType::Bongo:
        case OscillatorType::Timbale:
        case OscillatorType::Dembow808:
        case OscillatorType::DembowSnare:
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
        // Reggaeton drums
        case OscillatorType::Guira:       return 0.15f;
        case OscillatorType::Bongo:       return 0.3f;
        case OscillatorType::Timbale:     return 0.12f;  // Shorter, cutting
        case OscillatorType::Dembow808:   return 0.4f;   // Tighter
        case OscillatorType::DembowSnare: return 0.12f;  // Tight, clap-like
        default:                          return 0.5f;
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
                OscillatorType oscType = OscillatorType::Pulse,
                float vibrato = 0.0f, int arpeggio = 0, float slide = 0.0f) {
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
            v.baseFrequency = v.frequency;  // Store original frequency

            // Apply detune (only for non-drums)
            if (!isDrumType(oscType)) {
                float detuneMult = std::pow(2.0f, m_oscConfig.detune / 1200.0f);
                v.frequency *= detuneMult;
                v.baseFrequency *= detuneMult;
                v.phaseIncrement = v.frequency / m_sampleRate;
            } else {
                // Drums: initialize phaseIncrement to a sensible default (will be overridden by drum generators)
                v.phaseIncrement = 150.0f / m_sampleRate;  // Typical kick start frequency
            }
            v.phase = m_oscConfig.phase;
            v.startTime = time;
            v.envStage = Voice::EnvStage::Attack;
            v.envTime = 0.0f;
            v.envLevel = 0.0f;
            v.realTimeElapsed = 0.0f;
            v.lfsr = 0x0001;

            // Fade parameters
            v.fadeInDuration = fadeInSec;
            v.fadeOutDuration = fadeOutSec;
            v.noteDuration = durationSec;

            // Per-note oscillator type
            v.oscillatorType = oscType;

            // Per-note effects
            v.vibratoDepth = vibrato;       // 0.0 to 1.0 (1.0 = 1 semitone wobble)
            v.vibratoSpeed = 5.0f;          // 5 Hz default
            v.vibratoPhase = 0.0f;

            // Arpeggio: packed as 0xXY (X = first offset, Y = second offset)
            v.arpeggioX = (arpeggio >> 4) & 0x0F;  // Upper nibble
            v.arpeggioY = arpeggio & 0x0F;          // Lower nibble
            v.arpeggioStep = 0;
            v.arpeggioTimer = 0.0f;

            // Slide/portamento (semitones to slide from start)
            if (slide != 0.0f) {
                // slide is semitones offset - calculate target
                v.slideTarget = v.baseFrequency;
                // Start at offset frequency, slide to base
                v.frequency = v.baseFrequency * std::pow(2.0f, slide / 12.0f);
                v.slideSpeed = std::abs(slide) * 4.0f;  // Speed proportional to distance
            } else {
                v.slideTarget = 0.0f;
                v.slideSpeed = 0.0f;
            }
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
        float dt = 1.0f / m_sampleRate;

        for (auto& voice : m_voices) {
            if (!voice.active) continue;

            // Check if this is a drum sound (drums have their own internal envelope)
            bool isDrum = isDrumType(voice.oscillatorType);

            // Apply per-note effects to frequency (before oscillator generation)
            float effectFreq = voice.baseFrequency;

            // 1. Apply portamento/slide effect
            if (voice.slideTarget > 0.0f && voice.slideSpeed > 0.0f) {
                float diff = voice.slideTarget - voice.frequency;
                if (std::abs(diff) > 0.1f) {
                    // Slide towards target
                    float slideAmount = voice.slideSpeed * dt * voice.baseFrequency * 0.1f;
                    if (diff > 0) {
                        voice.frequency = std::min(voice.frequency + slideAmount, voice.slideTarget);
                    } else {
                        voice.frequency = std::max(voice.frequency - slideAmount, voice.slideTarget);
                    }
                } else {
                    voice.frequency = voice.slideTarget;
                    voice.slideTarget = 0.0f;  // Slide complete
                }
                effectFreq = voice.frequency;
            }

            // 2. Apply arpeggio effect (classic tracker-style 0xy command)
            if (voice.arpeggioX > 0 || voice.arpeggioY > 0) {
                // Step through: base note -> +X semitones -> +Y semitones
                float arpFreq = voice.baseFrequency;
                switch (voice.arpeggioStep) {
                    case 0: arpFreq = voice.baseFrequency; break;
                    case 1: arpFreq = voice.baseFrequency * std::pow(2.0f, voice.arpeggioX / 12.0f); break;
                    case 2: arpFreq = voice.baseFrequency * std::pow(2.0f, voice.arpeggioY / 12.0f); break;
                }
                effectFreq = arpFreq;

                // Advance arpeggio timer (step at ~15 Hz for classic tracker feel)
                voice.arpeggioTimer += dt;
                if (voice.arpeggioTimer >= 0.067f) {  // ~15 steps per second
                    voice.arpeggioTimer = 0.0f;
                    voice.arpeggioStep = (voice.arpeggioStep + 1) % 3;
                }
            }

            // 3. Apply vibrato effect (pitch wobble)
            if (voice.vibratoDepth > 0.0f) {
                // Update vibrato LFO phase
                voice.vibratoPhase += voice.vibratoSpeed * dt;
                if (voice.vibratoPhase >= 1.0f) voice.vibratoPhase -= 1.0f;

                // Calculate vibrato modulation (sine wave, +/- semitones)
                float vibratoMod = std::sin(voice.vibratoPhase * 2.0f * PI) * voice.vibratoDepth;
                effectFreq *= std::pow(2.0f, vibratoMod / 12.0f);
            }

            // Update phase increment with modified frequency (skip for drums - they manage their own)
            if (!isDrum) {
                voice.phaseIncrement = effectFreq / m_sampleRate;
            }

            // Generate oscillator sample
            float sample = generateOscillator(voice);

            // Apply envelope (skip ADSR for drums - they have internal envelopes)
            float envGain = 1.0f;
            if (isDrum) {
                // Drums manage their own envelope internally
                // Just update envTime for the drum generators
                voice.envTime += dt;
                // Deactivate drum voice after it's finished (based on decay time)
                float maxDrumTime = getDrumDecayTime(voice.oscillatorType) * 3.0f;  // 3x decay time
                if (voice.envTime > maxDrumTime) {
                    voice.active = false;
                }
            } else {
                // Track real time elapsed (independent of playback state and envelope stages)
                voice.realTimeElapsed += dt;

                // Auto-release synth notes when their duration is reached (for preview)
                if (voice.noteDuration > 0.0f) {
                    if (voice.realTimeElapsed >= voice.noteDuration && voice.envStage != Voice::EnvStage::Release) {
                        voice.envStage = Voice::EnvStage::Release;
                        voice.envTime = 0.0f;  // Reset envelope time for release phase
                    }
                    // Hard cutoff: deactivate after duration + short release time
                    if (voice.realTimeElapsed >= voice.noteDuration + 0.2f) {
                        voice.active = false;
                        continue;  // Skip processing this voice
                    }
                }
                envGain = processEnvelope(voice);
            }

            // Apply fade in/out
            float fadeGain = calculateFadeGain(voice, time);

            sample *= envGain * voice.velocity * fadeGain;

            output += sample;
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

            case OscillatorType::Supersaw:
                sample = generateSupersaw(voice);
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

            // Synth Presets
            case OscillatorType::SynthLead:
                sample = generateSynthLead(voice);
                break;
            case OscillatorType::SynthPad:
                sample = generateSynthPad(voice);
                break;
            case OscillatorType::SynthBass:
                sample = generateSynthBass(voice);
                break;
            case OscillatorType::SynthPluck:
                sample = generateSynthPluck(voice);
                break;
            case OscillatorType::SynthArp:
                sample = generateSynthArp(voice);
                break;
            case OscillatorType::SynthOrgan:
                sample = generateSynthOrgan(voice);
                break;
            case OscillatorType::SynthStrings:
                sample = generateSynthStrings(voice);
                break;
            case OscillatorType::SynthBrass:
                sample = generateSynthBrass(voice);
                break;
            case OscillatorType::SynthChip:
                sample = generateSynthChip(voice);
                break;
            case OscillatorType::SynthBell:
                sample = generateSynthBell(voice);
                break;

            // Synthwave Presets
            case OscillatorType::SynthwaveLead:
                sample = generateSynthwaveLead(voice);
                break;
            case OscillatorType::SynthwaveBass:
                sample = generateSynthwaveBass(voice);
                break;
            case OscillatorType::SynthwavePad:
                sample = generateSynthwavePad(voice);
                break;
            case OscillatorType::SynthwaveArp:
                sample = generateSynthwaveArp(voice);
                break;
            case OscillatorType::SynthwaveChord:
                sample = generateSynthwaveChord(voice);
                break;
            case OscillatorType::SynthwaveFM:
                sample = generateSynthwaveFM(voice);
                break;

            // Techno/Electronic Presets
            case OscillatorType::AcidBass:
                sample = generateAcidBass(voice);
                break;
            case OscillatorType::TechnoStab:
                sample = generateTechnoStab(voice);
                break;
            case OscillatorType::Hoover:
                sample = generateHoover(voice);
                break;
            case OscillatorType::RaveChord:
                sample = generateRaveChord(voice);
                break;
            case OscillatorType::Reese:
                sample = generateReese(voice);
                break;

            // Hip Hop Presets
            case OscillatorType::SubBass808:
                sample = generateSubBass808(voice);
                break;
            case OscillatorType::LoFiKeys:
                sample = generateLoFiKeys(voice);
                break;
            case OscillatorType::VinylNoise:
                sample = generateVinylNoise(voice);
                break;
            case OscillatorType::TrapLead:
                sample = generateTrapLead(voice);
                break;

            // Additional Synthwave
            case OscillatorType::GatedPad:
                sample = generateGatedPad(voice);
                break;
            case OscillatorType::PolySynth:
                sample = generatePolySynth(voice);
                break;
            case OscillatorType::SyncLead:
                sample = generateSyncLead(voice);
                break;

            // Reggaeton Instruments
            case OscillatorType::ReggaetonBass:
                sample = generateReggaetonBass(voice);
                break;
            case OscillatorType::LatinBrass:
                sample = generateLatinBrass(voice);
                break;
            case OscillatorType::Guira:
                sample = generateGuira(voice);
                break;
            case OscillatorType::Bongo:
                sample = generateBongo(voice);
                break;
            case OscillatorType::Timbale:
                sample = generateTimbale(voice);
                break;
            case OscillatorType::Dembow808:
                sample = generateDembow808(voice);
                break;
            case OscillatorType::DembowSnare:
                sample = generateDembowSnare(voice);
                break;
        }

        // Advance phase (use voice.phaseIncrement, not captured dt, so drums' late-set values work)
        voice.phase += voice.phaseIncrement;
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

    // Supersaw - 7 detuned sawtooth oscillators for massive sound
    float generateSupersaw(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // 7 oscillators with carefully chosen detuning (in semitones equivalent)
        // Center oscillator + 3 pairs at increasing detune
        const float detunes[] = { 0.0f, -0.015f, 0.015f, -0.030f, 0.030f, -0.008f, 0.008f };
        const float gains[] = { 1.0f, 0.8f, 0.8f, 0.6f, 0.6f, 0.9f, 0.9f };

        float mix = 0.0f;
        for (int i = 0; i < 7; ++i) {
            // Each oscillator has slight phase offset based on detune
            float phase = std::fmod(t * (1.0f + detunes[i]) + std::abs(detunes[i]) * 0.5f + 1.0f, 1.0f);

            // Generate saw with PolyBLEP
            float saw = 2.0f * phase - 1.0f;
            saw -= polyBlep(phase, dt * (1.0f + detunes[i]));

            mix += saw * gains[i];
        }

        // Normalize and add slight warmth
        return std::tanh(mix * 0.14f * 1.3f);
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

        // Set phaseIncrement for tonal component (~200Hz body)
        voice.phaseIncrement = 200.0f / m_sampleRate;

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

        // Small tonal "pop" for the body (~200Hz)
        float toneEnv = std::exp(-noteTime * 50.0f);
        float tone = std::sin(voice.phase * TWO_PI) * toneEnv * 0.25f;

        return (click + noise * 0.6f + tone) * envelope;
    }

    // Hi-hat - metallic "tsss" with ring (short mode LFSR)
    float generateHiHat(Voice& voice) {
        float noteTime = voice.envTime;

        // Set fixed phase increment for hi-hat (must be FIRST, before using phase)
        voice.phaseIncrement = 800.0f / m_sampleRate;

        // Fast decay with high-frequency emphasis at start for immediate "tss" attack
        // The decay is tuned so even short notes have the characteristic hi-hat sound
        float envelope = std::exp(-noteTime * 40.0f);

        // Immediate high-frequency burst at the very start (the "t" of "tss")
        float attack = noteTime < 0.005f ? (1.0f - noteTime / 0.005f) * 0.5f : 0.0f;

        // Use envTime-based phase for metallic sound (independent of MIDI note pitch)
        // Higher frequencies for brighter, more cutting "tss" sound
        float metallicPhase = noteTime * 1200.0f;  // 1200Hz base frequency (brighter)

        // Multiple square waves at inharmonic frequencies for metallic ring
        float metallic = 0.0f;
        metallic += (std::fmod(metallicPhase * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        metallic += (std::fmod(metallicPhase * 1.47f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        metallic += (std::fmod(metallicPhase * 1.83f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        metallic += (std::fmod(metallicPhase * 2.67f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.1f;  // Extra high harmonic

        // Short-mode LFSR noise for that classic metallic hi-hat sound
        // Generate more noise samples per audio sample for denser, brighter noise
        float noise = 0.0f;
        for (int i = 0; i < 12; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;  // Short mode = metallic
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        // Mix with more noise for that sizzly character, plus the attack burst
        return (noise * 0.65f + metallic * 0.35f) * envelope + attack * noise;
    }

    // Tom drum - pitched "bom" with resonance
    float generateTom(Voice& voice) {
        float noteTime = voice.envTime;

        // Pitch sweep: starts at ~180Hz, drops to ~100Hz
        float pitchEnv = std::exp(-noteTime * 20.0f);
        float freq = 100.0f + 80.0f * pitchEnv;  // 180Hz -> 100Hz
        voice.phaseIncrement = freq / m_sampleRate;

        // Medium decay with slight sustain
        float envelope = std::exp(-noteTime * 8.0f);

        // Main tone
        float sample = std::sin(voice.phase * TWO_PI);

        // Add second harmonic for body
        float harmonic = std::sin(voice.phase * TWO_PI * 2.0f) * 0.3f;

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

        // Set phaseIncrement for 808 snare (~180Hz body)
        voice.phaseIncrement = 180.0f / m_sampleRate;

        float envelope = std::exp(-noteTime * 20.0f);

        // Tonal body - two detuned oscillators using envTime for frequency
        float tone1 = std::sin(noteTime * 180.0f * TWO_PI);
        float tone2 = std::sin(noteTime * 330.0f * TWO_PI);
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

        // Set phaseIncrement for rimshot ping (~1000Hz)
        voice.phaseIncrement = 1000.0f / m_sampleRate;

        // Very fast decay
        float envelope = std::exp(-noteTime * 60.0f);

        // Sharp click
        float click = 0.0f;
        if (noteTime < 0.001f) {
            click = (1.0f - noteTime / 0.001f);
        }

        // High frequency ping using envTime for consistent pitch
        float ping = std::sin(noteTime * 1000.0f * TWO_PI) * std::exp(-noteTime * 80.0f);

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

        // Set fixed phase increment (must be FIRST)
        voice.phaseIncrement = 800.0f / m_sampleRate;

        // Longer decay than closed
        float envelope = std::exp(-noteTime * 10.0f);

        // Use envTime-based phase for consistent metallic sound
        float metallicPhase = noteTime * 800.0f;

        // Metallic frequencies
        float metallic = 0.0f;
        metallic += (std::fmod(metallicPhase * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;
        metallic += (std::fmod(metallicPhase * 1.47f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;
        metallic += (std::fmod(metallicPhase * 1.83f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;
        metallic += (std::fmod(metallicPhase * 2.17f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;

        // Noise
        float noise = 0.0f;
        for (int i = 0; i < 8; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        return (noise * 0.5f + metallic * 0.5f) * envelope;
    }

    // HiHatPedal - Very short, muted but still sounds like a hi-hat
    float generateHiHatPedal(Voice& voice) {
        float noteTime = voice.envTime;

        // Set fixed phase increment (must be FIRST)
        voice.phaseIncrement = 900.0f / m_sampleRate;

        // Fast decay but not so fast that it sounds like a click
        float envelope = std::exp(-noteTime * 60.0f);

        // Quick attack burst for immediate "tick" character
        float attack = noteTime < 0.003f ? (1.0f - noteTime / 0.003f) * 0.4f : 0.0f;

        // Use envTime-based phase for consistent metallic sound
        float metallicPhase = noteTime * 900.0f;

        // Metallic with higher frequencies for brightness
        float metallic = 0.0f;
        metallic += (std::fmod(metallicPhase * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.2f;
        metallic += (std::fmod(metallicPhase * 1.6f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;

        // Denser noise for that "chick" sound
        float noise = 0.0f;
        for (int i = 0; i < 8; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.6f;

        return (noise * 0.6f + metallic * 0.4f) * envelope + attack * noise;
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

        // Set fixed phase increment (must be FIRST)
        voice.phaseIncrement = 1000.0f / m_sampleRate;

        // Long decay
        float envelope = std::exp(-noteTime * 3.0f);

        // Initial burst
        float attack = (noteTime < 0.01f) ? (1.0f + 2.0f * (1.0f - noteTime / 0.01f)) : 1.0f;

        // Use envTime-based phase for consistent metallic sound
        float metallicPhase = noteTime * 1000.0f;

        // Complex metallic frequencies
        float metallic = 0.0f;
        metallic += (std::fmod(metallicPhase * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        metallic += (std::fmod(metallicPhase * 1.34f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        metallic += (std::fmod(metallicPhase * 1.87f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.12f;
        metallic += (std::fmod(metallicPhase * 2.43f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.1f;
        metallic += (std::fmod(metallicPhase * 3.17f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.08f;

        // Noise
        float noise = 0.0f;
        for (int i = 0; i < 10; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        return (noise * 0.4f + metallic * 0.6f) * envelope * attack;
    }

    // Ride - Ride cymbal
    float generateRide(Voice& voice) {
        float noteTime = voice.envTime;

        // Set fixed phase increment (must be FIRST)
        voice.phaseIncrement = 900.0f / m_sampleRate;

        // Medium-long decay with sustain
        float envelope = std::exp(-noteTime * 5.0f);

        // Use envTime-based phase for consistent sound
        float metallicPhase = noteTime * 900.0f;

        // Ping attack
        float ping = std::sin(metallicPhase * TWO_PI * 3.0f) * std::exp(-noteTime * 30.0f) * 0.4f;

        // Metallic sustain
        float metallic = 0.0f;
        metallic += (std::fmod(metallicPhase * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.12f;
        metallic += (std::fmod(metallicPhase * 1.5f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.12f;
        metallic += (std::fmod(metallicPhase * 2.1f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.1f;

        // Light noise
        float noise = 0.0f;
        for (int i = 0; i < 6; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.2f;

        return (ping + metallic + noise) * envelope;
    }

    // Cowbell - 808 cowbell
    float generateCowbell(Voice& voice) {
        float noteTime = voice.envTime;

        float envelope = std::exp(-noteTime * 15.0f);

        // Two detuned square waves at fixed frequencies (classic 808 cowbell)
        float freq1 = 587.0f;  // D5
        float freq2 = 845.0f;  // Slightly sharp G#5

        // Use envTime-based oscillators for consistent pitch
        float osc1 = (std::fmod(noteTime * freq1, 1.0f) < 0.5f) ? 1.0f : -1.0f;
        float osc2 = (std::fmod(noteTime * freq2, 1.0f) < 0.5f) ? 1.0f : -1.0f;

        float sample = (osc1 + osc2 * 0.7f) * 0.4f;

        return sample * envelope;
    }

    // Clave - Wood block click
    float generateClave(Voice& voice) {
        float noteTime = voice.envTime;

        // Set phaseIncrement for clave (~2500Hz)
        voice.phaseIncrement = 2500.0f / m_sampleRate;

        // Very short
        float envelope = std::exp(-noteTime * 100.0f);

        // High frequency sine burst using envTime
        float sample = std::sin(noteTime * 2500.0f * TWO_PI);

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

        // Set fixed phase increment (must be FIRST)
        voice.phaseIncrement = 1200.0f / m_sampleRate;

        float envelope = std::exp(-noteTime * 25.0f);

        // Use envTime-based phase for consistent jingle sound
        float jinglePhase = noteTime * 1200.0f;

        // Multiple metallic jingles
        float jingle = 0.0f;
        jingle += (std::fmod(jinglePhase * 2.0f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        jingle += (std::fmod(jinglePhase * 2.73f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.15f;
        jingle += (std::fmod(jinglePhase * 3.41f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.12f;
        jingle += (std::fmod(jinglePhase * 4.17f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.1f;

        // Noise component
        float noise = 0.0f;
        for (int i = 0; i < 8; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.3f;

        return (jingle + noise) * envelope;
    }

    // ========================================================================
    // Synth Preset Generators
    // ========================================================================

    // Lead - Detuned sawtooth waves for a thick, cutting lead
    float generateSynthLead(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Two detuned saws (slight detune for thickness)
        float detune = 0.003f;  // 3 cents detune
        float saw1 = 2.0f * t - 1.0f;
        float saw2 = 2.0f * std::fmod(t + detune, 1.0f) - 1.0f;

        // PolyBLEP correction
        saw1 -= polyBlep(t, dt);
        saw2 -= polyBlep(std::fmod(t + detune, 1.0f), dt);

        // Mix with slight sub oscillator
        float sub = std::sin(t * TWO_PI * 0.5f) * 0.3f;

        return (saw1 + saw2) * 0.4f + sub;
    }

    // Pad - Soft, atmospheric sound with slow attack feel
    float generateSynthPad(Voice& voice) {
        float t = voice.phase;

        // Multiple detuned sine/triangle waves for soft pad
        float wave1 = std::sin(t * TWO_PI);
        float wave2 = std::sin(t * TWO_PI * 1.002f);  // Slight detune
        float wave3 = std::sin(t * TWO_PI * 0.998f);  // Slight detune other way
        float wave4 = std::sin(t * TWO_PI * 2.0f) * 0.3f;  // Octave up, quieter

        // Gentle triangle for warmth
        float tri = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);

        return (wave1 + wave2 + wave3 + wave4 + tri * 0.2f) * 0.25f;
    }

    // Bass - Deep punchy bass with sub and harmonics
    float generateSynthBass(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Sub sine (fundamental)
        float sub = std::sin(t * TWO_PI) * 0.6f;

        // Saw for harmonics
        float saw = 2.0f * t - 1.0f;
        saw -= polyBlep(t, dt);

        // Square for punch
        float sq = (t < 0.5f) ? 1.0f : -1.0f;
        sq += polyBlep(t, dt);
        sq -= polyBlep(std::fmod(t + 0.5f, 1.0f), dt);

        return sub + saw * 0.25f + sq * 0.15f;
    }

    // Pluck - Short, plucky sound with fast decay feel
    float generateSynthPluck(Voice& voice) {
        float t = voice.phase;

        // Bright triangle with harmonics
        float tri = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);

        // Add some upper harmonics for brightness
        float harm2 = std::sin(t * TWO_PI * 2.0f) * 0.3f;
        float harm3 = std::sin(t * TWO_PI * 3.0f) * 0.15f;

        return tri * 0.7f + harm2 + harm3;
    }

    // Arp - Crisp pulse wave perfect for arpeggios
    float generateSynthArp(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // 25% duty cycle pulse (bright and crisp)
        float width = 0.25f;
        float pulse = (t < width) ? 1.0f : -1.0f;
        pulse += polyBlep(t, dt);
        pulse -= polyBlep(std::fmod(t + (1.0f - width), 1.0f), dt);

        return pulse * 0.7f;
    }

    // Organ - Classic organ with additive harmonics
    float generateSynthOrgan(Voice& voice) {
        float t = voice.phase;

        // Drawbar-style additive synthesis
        float fundamental = std::sin(t * TWO_PI) * 0.8f;         // 8'
        float octaveUp = std::sin(t * TWO_PI * 2.0f) * 0.6f;     // 4'
        float twelfth = std::sin(t * TWO_PI * 3.0f) * 0.4f;      // 2 2/3'
        float twoOctUp = std::sin(t * TWO_PI * 4.0f) * 0.3f;     // 2'
        float subOctave = std::sin(t * TWO_PI * 0.5f) * 0.4f;    // 16'

        return (fundamental + octaveUp + twelfth + twoOctUp + subOctave) * 0.35f;
    }

    // Strings - Detuned ensemble with lush character
    float generateSynthStrings(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Multiple detuned saw waves for string ensemble
        float detunes[] = { 0.0f, 0.004f, -0.004f, 0.008f, -0.008f };
        float mix = 0.0f;

        for (float detune : detunes) {
            float phase = std::fmod(t + detune + 1.0f, 1.0f);
            float saw = 2.0f * phase - 1.0f;
            saw -= polyBlep(phase, dt);
            mix += saw;
        }

        return mix * 0.15f;  // Scale down because we're adding 5 oscillators
    }

    // Brass - Brassy stab with rich harmonics
    float generateSynthBrass(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Saw as base
        float saw = 2.0f * t - 1.0f;
        saw -= polyBlep(t, dt);

        // Square for body
        float sq = (t < 0.5f) ? 1.0f : -1.0f;
        sq += polyBlep(t, dt);
        sq -= polyBlep(std::fmod(t + 0.5f, 1.0f), dt);

        // Add harmonics for brass character
        float harm3 = std::sin(t * TWO_PI * 3.0f) * 0.2f;

        return (saw * 0.5f + sq * 0.3f + harm3) * 0.8f;
    }

    // Chip - Classic 12.5% pulse for authentic chiptune
    float generateSynthChip(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Classic NES 12.5% duty cycle
        float width = 0.125f;
        float pulse = (t < width) ? 1.0f : -1.0f;
        pulse += polyBlep(t, dt);
        pulse -= polyBlep(std::fmod(t + (1.0f - width), 1.0f), dt);

        return pulse * 0.8f;
    }

    // Bell - FM-like bell/chime sound
    float generateSynthBell(Voice& voice) {
        float t = voice.phase;

        // Simple FM-like synthesis for bell tones
        // Carrier with inharmonic modulator
        float modRatio = 3.5f;  // Inharmonic for bell-like quality
        float modIndex = 2.0f;

        float modulator = std::sin(t * TWO_PI * modRatio);
        float carrier = std::sin(t * TWO_PI + modulator * modIndex);

        // Add higher partial for shimmer
        float shimmer = std::sin(t * TWO_PI * 5.0f) * 0.15f;

        return (carrier * 0.7f + shimmer) * 0.8f;
    }

    // ========================================================================
    // Synthwave Preset Generators
    // ========================================================================

    // SynthwaveLead - Bright PWM lead with warmth (classic 80s lead)
    float generateSynthwaveLead(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Animated PWM (pulse width modulation)
        float pwmLfo = 0.3f + 0.2f * std::sin(voice.envTime * 4.0f);  // PWM between 0.1 and 0.5

        // Main pulse with animated width
        float pulse = (t < pwmLfo) ? 1.0f : -1.0f;
        pulse += polyBlep(t, dt);
        pulse -= polyBlep(std::fmod(t + (1.0f - pwmLfo), 1.0f), dt);

        // Add bright saw for cut-through
        float saw = 2.0f * t - 1.0f;
        saw -= polyBlep(t, dt);

        // Slight octave-up for brightness
        float octUp = std::sin(t * TWO_PI * 2.0f) * 0.15f;

        return (pulse * 0.5f + saw * 0.3f + octUp) * 0.75f;
    }

    // SynthwaveBass - Deep 808-style saw bass with sub
    float generateSynthwaveBass(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Strong sub sine (foundation)
        float sub = std::sin(t * TWO_PI) * 0.7f;

        // Saw for harmonics and grit
        float saw = 2.0f * t - 1.0f;
        saw -= polyBlep(t, dt);

        // Slight detuned saw for thickness
        float detune = 0.005f;
        float saw2 = 2.0f * std::fmod(t + detune, 1.0f) - 1.0f;
        saw2 -= polyBlep(std::fmod(t + detune, 1.0f), dt);

        // Soft saturation for warmth
        float mix = sub + (saw + saw2) * 0.25f;
        return std::tanh(mix * 1.2f) * 0.8f;
    }

    // SynthwavePad - Warm lush evolving pad (classic synthwave atmosphere)
    float generateSynthwavePad(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Multiple detuned saws (supersaw-lite)
        float detunes[] = { 0.0f, 0.006f, -0.006f, 0.012f, -0.012f, 0.003f, -0.003f };
        float mix = 0.0f;

        for (float detune : detunes) {
            float phase = std::fmod(t + detune + 1.0f, 1.0f);
            float saw = 2.0f * phase - 1.0f;
            saw -= polyBlep(phase, dt);
            mix += saw;
        }

        // Slow LFO for movement
        float lfo = 0.3f * std::sin(voice.envTime * 1.5f);

        // Add soft sine layer
        float sine = std::sin(t * TWO_PI) * 0.3f;

        return (mix * 0.12f + sine * (1.0f + lfo * 0.3f)) * 0.7f;
    }

    // SynthwaveArp - Crisp sequence/arp sound (tight and punchy)
    float generateSynthwaveArp(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Tight 12.5% pulse (classic arp sound)
        float width = 0.125f;
        float pulse = (t < width) ? 1.0f : -1.0f;
        pulse += polyBlep(t, dt);
        pulse -= polyBlep(std::fmod(t + (1.0f - width), 1.0f), dt);

        // Add a bit of saw for edge
        float saw = 2.0f * t - 1.0f;
        saw -= polyBlep(t, dt);

        // Octave up sine for shimmer
        float shimmer = std::sin(t * TWO_PI * 2.0f) * 0.1f;

        return (pulse * 0.6f + saw * 0.2f + shimmer) * 0.8f;
    }

    // SynthwaveChord - Polyphonic stab for chords (80s poly synth)
    float generateSynthwaveChord(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // PWM with slow modulation
        float pwm = 0.45f + 0.1f * std::sin(voice.envTime * 2.0f);

        // Two slightly detuned pulses
        float pulse1 = (t < pwm) ? 1.0f : -1.0f;
        pulse1 += polyBlep(t, dt);
        pulse1 -= polyBlep(std::fmod(t + (1.0f - pwm), 1.0f), dt);

        float phase2 = std::fmod(t + 0.004f, 1.0f);
        float pulse2 = (phase2 < pwm) ? 1.0f : -1.0f;
        pulse2 += polyBlep(phase2, dt);
        pulse2 -= polyBlep(std::fmod(phase2 + (1.0f - pwm), 1.0f), dt);

        // Add sine for warmth
        float sine = std::sin(t * TWO_PI) * 0.2f;

        // Subtle chorus-like detuning
        float chorus = std::sin(t * TWO_PI * 1.003f) * 0.1f;

        return ((pulse1 + pulse2) * 0.35f + sine + chorus) * 0.7f;
    }

    // SynthwaveFM - Classic DX7-style FM brass/keys
    float generateSynthwaveFM(Voice& voice) {
        float t = voice.phase;

        // Classic FM algorithm: 2 operators
        // Modulator -> Carrier

        // Operator 2 (modulator) - ratio 2:1 or 3:1 for brass
        float modRatio = 2.0f;
        float modIndex = 2.5f + 0.5f * std::sin(voice.envTime * 3.0f);  // Animated mod index

        float modulator = std::sin(t * TWO_PI * modRatio);

        // Operator 1 (carrier)
        float carrier = std::sin(t * TWO_PI + modulator * modIndex);

        // Add second carrier at slight detune for thickness
        float carrier2 = std::sin(t * TWO_PI * 1.002f + modulator * modIndex * 0.8f);

        // Optional: Add brightness with higher ratio modulator
        float brightMod = std::sin(t * TWO_PI * 4.0f) * 0.3f;
        float bright = std::sin(t * TWO_PI * 2.0f + brightMod) * 0.15f;

        return (carrier * 0.5f + carrier2 * 0.3f + bright) * 0.75f;
    }

    // ========================================================================
    // Techno/Electronic Generators
    // ========================================================================

    // AcidBass - TB-303 style resonant bass with filter sweep
    float generateAcidBass(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Base sawtooth
        float saw = 2.0f * t - 1.0f;
        saw -= polyBlep(t, dt);

        // Resonant filter simulation with envelope
        float filterEnv = std::exp(-voice.envTime * 4.0f);  // Fast decay
        float cutoff = 0.2f + 0.6f * filterEnv;  // Filter opens then closes

        // Simple resonant lowpass approximation
        static float filterState = 0.0f;
        float resonance = 0.85f;
        filterState += cutoff * (saw - filterState + resonance * (filterState - filterState));
        float filtered = filterState + (saw - filterState) * cutoff;

        // Add some squelch/accent
        float accent = 1.0f + filterEnv * 0.5f;

        return std::tanh(filtered * accent * 1.5f) * 0.8f;
    }

    // TechnoStab - Short chord stab
    float generateTechnoStab(Voice& voice) {
        float t = voice.phase;

        // Minor chord stab (root, minor 3rd, 5th)
        float root = std::sin(t * TWO_PI);
        float minor3rd = std::sin(t * TWO_PI * 1.189f);  // ~3 semitones up
        float fifth = std::sin(t * TWO_PI * 1.498f);     // ~7 semitones up

        // Add saw for edge
        float saw = (2.0f * t - 1.0f) * 0.3f;

        // Quick decay for stab effect
        float stab = std::exp(-voice.envTime * 8.0f);

        return ((root + minor3rd * 0.8f + fifth * 0.6f) * 0.25f + saw) * stab * 0.9f;
    }

    // Hoover - Classic rave hoover/reese sound
    float generateHoover(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Multiple detuned saws (the classic hoover sound)
        const float detunes[] = { 0.0f, -0.03f, 0.03f, -0.06f, 0.06f, -0.01f, 0.01f };
        float mix = 0.0f;

        for (int i = 0; i < 7; ++i) {
            float phase = std::fmod(t * (1.0f + detunes[i]) + i * 0.1f, 1.0f);
            float saw = 2.0f * phase - 1.0f;
            mix += saw;
        }

        // Add some PWM for movement
        float pwm = std::sin(voice.envTime * 3.0f) * 0.2f + 0.5f;
        float pulse = (t < pwm) ? 1.0f : -1.0f;

        // Pitch bend down effect (characteristic hoover portamento)
        float bend = std::exp(-voice.envTime * 0.5f);

        return std::tanh((mix * 0.12f + pulse * 0.15f) * (0.7f + bend * 0.3f)) * 0.85f;
    }

    // RaveChord - Rave piano/organ chord
    float generateRaveChord(Voice& voice) {
        float t = voice.phase;

        // Major chord (root, major 3rd, 5th, octave)
        float root = std::sin(t * TWO_PI);
        float major3rd = std::sin(t * TWO_PI * 1.26f);   // ~4 semitones
        float fifth = std::sin(t * TWO_PI * 1.498f);     // ~7 semitones
        float octave = std::sin(t * TWO_PI * 2.0f);

        // Add some brightness with higher harmonics
        float bright = std::sin(t * TWO_PI * 3.0f) * 0.2f + std::sin(t * TWO_PI * 4.0f) * 0.1f;

        // Organ-like attack
        float attack = 1.0f - std::exp(-voice.envTime * 20.0f);

        return (root * 0.4f + major3rd * 0.3f + fifth * 0.25f + octave * 0.15f + bright) * attack * 0.6f;
    }

    // Reese - Detuned saw bass (drum & bass style)
    float generateReese(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Two heavily detuned saws
        float phase1 = t;
        float phase2 = std::fmod(t * 1.008f + 0.3f, 1.0f);  // Slight detune

        float saw1 = 2.0f * phase1 - 1.0f;
        saw1 -= polyBlep(phase1, dt);

        float saw2 = 2.0f * phase2 - 1.0f;
        saw2 -= polyBlep(phase2, dt * 1.008f);

        // Modulate the detune amount over time (the "reese" wobble)
        float wobble = std::sin(voice.envTime * 4.0f) * 0.003f;
        float phase3 = std::fmod(t * (1.0f + wobble) + 0.6f, 1.0f);
        float saw3 = 2.0f * phase3 - 1.0f;

        return std::tanh((saw1 + saw2 + saw3) * 0.4f) * 0.85f;
    }

    // ========================================================================
    // Hip Hop Generators
    // ========================================================================

    // SubBass808 - Deep 808 sub bass
    float generateSubBass808(Voice& voice) {
        float t = voice.phase;

        // Pure sub sine with slight harmonics
        float sub = std::sin(t * TWO_PI);

        // Add subtle second harmonic for warmth
        float second = std::sin(t * TWO_PI * 2.0f) * 0.15f;

        // Pitch drop at start (808 characteristic)
        float pitchEnv = 1.0f + std::exp(-voice.envTime * 15.0f) * 0.3f;
        float dropped = std::sin(t * TWO_PI * pitchEnv);

        // Soft saturation
        return std::tanh((dropped * 0.8f + sub * 0.2f + second) * 1.2f) * 0.9f;
    }

    // LoFiKeys - Dusty lo-fi piano/rhodes
    float generateLoFiKeys(Voice& voice) {
        float t = voice.phase;

        // Electric piano-like FM
        float modulator = std::sin(t * TWO_PI * 7.0f);
        float carrier = std::sin(t * TWO_PI + modulator * 0.8f);

        // Add second voice for richness
        float carrier2 = std::sin(t * TWO_PI * 2.0f + modulator * 0.4f);

        // Bell-like decay
        float decay = std::exp(-voice.envTime * 3.0f);

        // Add noise/dust
        float noise = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * 0.02f;

        // Bit crush effect for lo-fi
        float sample = (carrier * 0.6f + carrier2 * 0.3f) * decay + noise;
        sample = std::floor(sample * 16.0f) / 16.0f;  // Reduce bit depth

        return sample * 0.7f;
    }

    // VinylNoise - Vinyl crackle texture
    float generateVinylNoise(Voice& voice) {
        // Continuous vinyl texture
        float noise = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f);

        // Crackle (occasional pops)
        float crackle = 0.0f;
        if (rand() % 1000 < 3) {  // Occasional pop
            crackle = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * 0.5f;
        }

        // Rumble (low frequency content)
        float rumble = std::sin(voice.phase * TWO_PI * 0.1f) * 0.1f;

        // High-pass the noise for hiss
        static float hissFilter = 0.0f;
        hissFilter = hissFilter * 0.95f + noise * 0.05f;
        float hiss = noise - hissFilter;

        return (hiss * 0.3f + crackle + rumble) * 0.4f;
    }

    // TrapLead - Trap-style plucky lead
    float generateTrapLead(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Square wave base
        float square = (t < 0.5f) ? 1.0f : -1.0f;
        square += polyBlep(t, dt);
        square -= polyBlep(std::fmod(t + 0.5f, 1.0f), dt);

        // Add portamento/pitch slide feel
        float pitchEnv = 1.0f + std::exp(-voice.envTime * 20.0f) * 0.15f;

        // Plucky envelope
        float pluck = std::exp(-voice.envTime * 6.0f);

        // Add some harmonics
        float octave = std::sin(t * TWO_PI * 2.0f * pitchEnv) * 0.3f;

        return (square * 0.6f + octave) * pluck * 0.8f;
    }

    // ========================================================================
    // Additional Synthwave Generators
    // ========================================================================

    // GatedPad - Rhythmic gated pad
    float generateGatedPad(Voice& voice) {
        float t = voice.phase;

        // Lush pad base (multiple detuned sines)
        float pad1 = std::sin(t * TWO_PI);
        float pad2 = std::sin(t * TWO_PI * 1.002f);
        float pad3 = std::sin(t * TWO_PI * 0.998f);
        float pad4 = std::sin(t * TWO_PI * 2.0f) * 0.3f;  // Octave

        float pad = (pad1 + pad2 + pad3) * 0.3f + pad4;

        // Gate effect (rhythmic amplitude modulation)
        // Simulate 1/8 note gate at ~120 BPM (4 gates per second)
        float gateFreq = 4.0f;
        float gate = (std::sin(voice.envTime * TWO_PI * gateFreq) > 0.0f) ? 1.0f : 0.2f;

        // Smooth the gate slightly
        static float gateSmooth = 1.0f;
        gateSmooth += (gate - gateSmooth) * 0.1f;

        return pad * gateSmooth * 0.7f;
    }

    // PolySynth - Rich polyphonic synth
    float generatePolySynth(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;

        // Saw + Pulse combo
        float saw = 2.0f * t - 1.0f;
        saw -= polyBlep(t, dt);

        float pwm = std::sin(voice.envTime * 2.0f) * 0.15f + 0.5f;
        float pulse = (t < pwm) ? 1.0f : -1.0f;

        // Sub oscillator
        float sub = std::sin(t * TWO_PI * 0.5f) * 0.4f;

        // Detuned layer
        float detunedPhase = std::fmod(t * 1.003f + 0.25f, 1.0f);
        float detuned = 2.0f * detunedPhase - 1.0f;

        // Filter-like envelope
        float brightness = 0.3f + 0.7f * std::exp(-voice.envTime * 2.0f);

        return ((saw * 0.3f + pulse * 0.25f + detuned * 0.2f) * brightness + sub) * 0.7f;
    }

    // SyncLead - Hard sync lead sound
    float generateSyncLead(Voice& voice) {
        float t = voice.phase;

        // Hard sync: slave oscillator resets when master completes cycle
        // Simulate by using master as reset trigger
        float masterFreq = 1.0f;
        float slaveRatio = 2.5f + std::sin(voice.envTime * 3.0f) * 0.5f;  // Animated ratio

        float masterPhase = t;
        float slavePhase = std::fmod(t * slaveRatio, 1.0f);

        // Slave waveform (saw)
        float slave = 2.0f * slavePhase - 1.0f;

        // Add brightness based on sync ratio
        float harmonic = std::sin(slavePhase * TWO_PI * 2.0f) * 0.3f;

        // Characteristic sync "bark" at attack
        float bark = std::exp(-voice.envTime * 10.0f) * 0.3f;

        return (slave * 0.6f + harmonic + bark * std::sin(t * TWO_PI * 5.0f)) * 0.75f;
    }

    // ========================================================================
    // Reggaeton Instrument Generators (Research-based authentic sounds)
    // ========================================================================

    // ReggaetonBass - Deep 808-style bass with lo-fi character and pitch sweep
    // Research: "808 bass", "lo-fi quality 12-bit", "very low pitched"
    float generateReggaetonBass(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;
        float noteTime = voice.envTime;

        // Dramatic pitch drop (808-style) - more pronounced for reggaeton
        float pitchEnv = std::exp(-noteTime * 18.0f);
        float pitchMult = 1.0f + pitchEnv * 0.6f;  // Starts higher, drops fast

        // Strong sub sine (foundation) - lower octave
        float sub = std::sin(t * TWO_PI * 0.5f) * 0.6f;

        // Main tone with pitch sweep
        float main = std::sin(t * TWO_PI * pitchMult);

        // Add subtle saw for presence (lo-fi filtered)
        float saw = 2.0f * t - 1.0f;
        saw -= polyBlep(t, dt);
        // Low-pass simulation: reduce high harmonics
        saw *= 0.3f;

        // Lo-fi bit crushing simulation (quantize to fewer levels)
        float mix = main * 0.5f + sub * 0.35f + saw * 0.1f;
        // Simulate 12-bit by quantizing
        float bitDepth = 256.0f;  // 8-bit equivalent for crunch
        mix = std::floor(mix * bitDepth) / bitDepth;

        // Heavy saturation for that thick reggaeton punch
        return std::tanh(mix * 2.5f) * 0.9f;
    }

    // LatinBrass - Punchy brass stab for reggaeton hooks and perreo breaks
    // Research: Needs fast attack, bright opening that closes
    float generateLatinBrass(Voice& voice) {
        float t = voice.phase;
        float dt = voice.phaseIncrement;
        float noteTime = voice.envTime;

        // Fast attack, quick decay for stab feel
        float stabEnv = std::exp(-noteTime * 8.0f);

        // Main sawtooth for brass character
        float saw = 2.0f * t - 1.0f;
        saw -= polyBlep(t, dt);

        // Square for body and punch
        float sq = (t < 0.5f) ? 1.0f : -1.0f;
        sq += polyBlep(t, dt);
        sq -= polyBlep(std::fmod(t + 0.5f, 1.0f), dt);

        // Brass harmonics (odd harmonics for brass character)
        float harm3 = std::sin(t * TWO_PI * 3.0f) * 0.35f;
        float harm5 = std::sin(t * TWO_PI * 5.0f) * 0.2f;
        float harm7 = std::sin(t * TWO_PI * 7.0f) * 0.1f;

        // Brightness envelope - opens up quickly then closes for stab
        float brightness = 0.3f + 0.7f * std::exp(-noteTime * 6.0f);

        // Detuned layer for thickness (typical of latin brass sections)
        float phase2 = std::fmod(t + 0.007f, 1.0f);
        float saw2 = 2.0f * phase2 - 1.0f;
        float phase3 = std::fmod(t - 0.005f, 1.0f);
        float saw3 = 2.0f * phase3 - 1.0f;

        float mix = (saw * 0.35f + sq * 0.15f + saw2 * 0.12f + saw3 * 0.1f +
                    harm3 + harm5 + harm7) * brightness;

        // Punchy saturation
        return std::tanh(mix * 1.8f) * stabEnv * 0.85f;
    }

    // Guira - Scraped metal percussion (essential dembow "tsss-tsss")
    // Research: High-frequency metallic scrape, rhythmic quality
    float generateGuira(Voice& voice) {
        float noteTime = voice.envTime;

        // Set fixed phase increment for metallic character
        voice.phaseIncrement = 2500.0f / m_sampleRate;

        // Two-stage envelope: initial attack + scraping tail
        float attackEnv = std::exp(-noteTime * 60.0f);  // Sharp initial hit
        float scrapeEnv = std::exp(-noteTime * 25.0f);  // Longer scrape
        float envelope = attackEnv * 0.4f + scrapeEnv * 0.6f;

        // High-frequency metallic components (guira has bright, cutting sound)
        float scrapePhase = noteTime * 3500.0f;  // Faster = brighter

        // Multiple inharmonic frequencies for authentic metallic scrape
        float metallic = 0.0f;
        metallic += (std::fmod(scrapePhase * 1.0f, 1.0f) < 0.3f ? 1.0f : -1.0f) * 0.18f;
        metallic += (std::fmod(scrapePhase * 1.41f, 1.0f) < 0.4f ? 1.0f : -1.0f) * 0.15f;
        metallic += (std::fmod(scrapePhase * 2.23f, 1.0f) < 0.35f ? 1.0f : -1.0f) * 0.12f;
        metallic += (std::fmod(scrapePhase * 3.17f, 1.0f) < 0.45f ? 1.0f : -1.0f) * 0.1f;
        metallic += (std::fmod(scrapePhase * 4.73f, 1.0f) < 0.5f ? 1.0f : -1.0f) * 0.08f;

        // High-frequency noise for scraping texture
        float noise = 0.0f;
        for (int i = 0; i < 8; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        noise = ((voice.lfsr & 1) ? 1.0f : -1.0f);

        // Emphasize high frequencies by mixing more metallic than noise
        float sample = metallic * 0.55f + noise * 0.45f;

        // Add slight resonance/ring
        float ring = std::sin(noteTime * TWO_PI * 4500.0f) * 0.15f * scrapeEnv;

        return (sample + ring) * envelope * 0.75f;
    }

    // Bongo - Latin bongo with authentic membrane resonance
    // Research: "Characteristic tone", two-drum (macho/hembra) quality
    float generateBongo(Voice& voice) {
        float noteTime = voice.envTime;

        // Two-stage envelope for realistic membrane
        float attackEnv = std::exp(-noteTime * 35.0f);  // Sharp attack
        float bodyEnv = std::exp(-noteTime * 12.0f);    // Body resonance
        float envelope = attackEnv * 0.3f + bodyEnv * 0.7f;

        // Pitch envelope for that characteristic bongo "pon/tok"
        float pitchEnv = std::exp(-noteTime * 40.0f);
        float basePitch = 380.0f;  // Macho (higher) bongo pitch
        float freq = basePitch * (1.0f + 0.5f * pitchEnv);

        // Main membrane tone
        voice.phaseIncrement = freq / m_sampleRate;
        float tone = std::sin(voice.phase * TWO_PI);

        // Membrane harmonics (slightly inharmonic like real drum heads)
        float harm2 = std::sin(voice.phase * TWO_PI * 1.59f) * 0.4f;  // Inharmonic
        float harm3 = std::sin(voice.phase * TWO_PI * 2.14f) * 0.25f; // Inharmonic
        float harm4 = std::sin(voice.phase * TWO_PI * 2.65f) * 0.15f; // Inharmonic

        // Sharp slap transient (hand strike)
        float slap = 0.0f;
        if (noteTime < 0.004f) {
            // Noise burst for slap
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
            slap = ((voice.lfsr & 1) ? 1.0f : -1.0f) * (1.0f - noteTime / 0.004f) * 0.5f;
        }

        // Body resonance (shell)
        float body = std::sin(voice.phase * TWO_PI * 0.5f) * 0.2f;

        float sample = tone + harm2 + harm3 + harm4 + body;
        sample = std::tanh(sample * 1.2f);

        return (sample + slap) * envelope * 0.8f;
    }

    // Timbale - Short cutting square-wave based percussion
    // Research: "Basic square-wave with tiny decay", "short, cutting percussive sound"
    float generateTimbale(Voice& voice) {
        float noteTime = voice.envTime;

        // Very fast decay - tight and cutting
        float envelope = std::exp(-noteTime * 45.0f);

        // High pitch with slight drop
        float basePitch = 800.0f;  // Bright, cutting
        float pitchEnv = std::exp(-noteTime * 50.0f);
        float freq = basePitch * (1.0f + 0.25f * pitchEnv);

        voice.phaseIncrement = freq / m_sampleRate;

        // Main square wave (as per research)
        float sq = (voice.phase < 0.5f) ? 1.0f : -1.0f;

        // Metallic ring components (timbales have distinct ring)
        float ring1 = std::sin(voice.phase * TWO_PI * 2.71f) * 0.25f;  // Inharmonic
        float ring2 = std::sin(voice.phase * TWO_PI * 4.13f) * 0.15f;  // Inharmonic
        float ring3 = std::sin(voice.phase * TWO_PI * 5.89f) * 0.1f;   // High shimmer

        // Very sharp attack click (stick hit)
        float click = 0.0f;
        if (noteTime < 0.0015f) {
            click = (1.0f - noteTime / 0.0015f) * 0.7f;
        }

        float sample = sq * 0.4f + ring1 + ring2 + ring3;

        return (sample + click) * envelope * 0.7f;
    }

    // Dembow808 - Lo-fi kick for dembow rhythm
    // Research: "unpitched", "deep, dull, round", "lo-fi 12-bit", "cuts off before bass"
    float generateDembow808(Voice& voice) {
        float noteTime = voice.envTime;

        // Tighter envelope - cuts off before tonal bass emerges
        float envelope = std::exp(-noteTime * 10.0f);
        // Gate it shorter
        if (noteTime > 0.15f) envelope *= std::exp(-(noteTime - 0.15f) * 20.0f);

        // Quick pitch sweep - becomes unpitched quickly
        float startFreq = 90.0f;   // Lower start (less pitched)
        float endFreq = 35.0f;     // Deep sub
        float pitchEnv = std::exp(-noteTime * 50.0f);  // Faster sweep
        float freq = endFreq + (startFreq - endFreq) * pitchEnv;

        // Generate sine wave with pitch sweep
        voice.phaseIncrement = freq / m_sampleRate;
        float sample = std::sin(voice.phase * TWO_PI);

        // Add second harmonic for body (not brightness)
        float harm2 = std::sin(voice.phase * TWO_PI * 2.0f) * 0.15f;

        // Lo-fi processing (12-bit style quantization)
        float mix = sample + harm2;
        float bitDepth = 128.0f;  // Coarser quantization for lo-fi
        mix = std::floor(mix * bitDepth) / bitDepth;

        // Subtle "thump" attack (not clicky - dull and round)
        float thump = 0.0f;
        if (noteTime < 0.008f) {
            thump = std::sin(noteTime * 800.0f) * (1.0f - noteTime / 0.008f) * 0.25f;
        }

        // Warm, dull saturation (not harsh)
        mix = std::tanh(mix * 1.3f);

        return (mix + thump) * envelope * 0.9f;
    }

    // DembowSnare - Tight clap-like snare for dembow rhythm
    // Research: "short and tight - no tail", "closer to a clap", "1kHz-3kHz emphasis"
    float generateDembowSnare(Voice& voice) {
        float noteTime = voice.envTime;

        // Very tight envelope - no tail
        float envelope = std::exp(-noteTime * 35.0f);
        // Hard gate after short time
        if (noteTime > 0.08f) envelope *= std::exp(-(noteTime - 0.08f) * 50.0f);

        // Multiple noise bursts (clap-like - multiple hands hitting)
        float clap = 0.0f;

        // First burst
        if (noteTime < 0.015f) {
            for (int i = 0; i < 6; ++i) {
                uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
                voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
            }
            clap += ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.5f;
        }
        // Second burst (slightly delayed for clap character)
        if (noteTime > 0.008f && noteTime < 0.025f) {
            for (int i = 0; i < 4; ++i) {
                uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
                voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
            }
            clap += ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.4f;
        }
        // Third burst
        if (noteTime > 0.015f && noteTime < 0.035f) {
            for (int i = 0; i < 3; ++i) {
                uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
                voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
            }
            clap += ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.3f;
        }

        // Tonal body in the 1-3kHz range
        float bodyFreq = 1800.0f;  // Center of 1-3kHz
        voice.phaseIncrement = bodyFreq / m_sampleRate;
        float body = std::sin(voice.phase * TWO_PI) * 0.3f;
        // Add slightly off harmonics for snare character
        float body2 = std::sin(voice.phase * TWO_PI * 1.4f) * 0.15f;
        float body3 = std::sin(voice.phase * TWO_PI * 2.3f) * 0.1f;

        // Continuous filtered noise layer
        for (int i = 0; i < 5; ++i) {
            uint16_t feedback = ((voice.lfsr >> 0) ^ (voice.lfsr >> 1)) & 1;
            voice.lfsr = (voice.lfsr >> 1) | (feedback << 14);
        }
        float noise = ((voice.lfsr & 1) ? 1.0f : -1.0f) * 0.35f;

        float sample = clap + body + body2 + body3 + noise;

        // Compression simulation (make it punchy)
        sample = std::tanh(sample * 2.0f);

        return sample * envelope * 0.75f;
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
