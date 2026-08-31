#pragma once

/*
 * ChiptuneTracker - Analog drum modelling
 *
 * The tracker already has 21 drum oscillators - Kick, Kick808, Snare, HiHat
 * and the rest - and they are good, and they are also fixed. Each is a
 * hardcoded arrangement of an envelope and an oscillator with no editable
 * parameters at all: you can pick Kick808 or KickHard, and that is the whole
 * of the control you have over a kick.
 *
 * This models the same three voices the way the analogue machines actually
 * made them, with the parameters exposed. It is not a replacement for the
 * existing drums - those stay, they are what most projects use, and several
 * genre kits are built on them - it is the editable version for when the
 * fixed one is nearly right.
 *
 * How the real circuits work, since that is what is being modelled:
 *
 * KICK. A sine whose pitch sweeps down fast from a few hundred hertz to the
 * fundamental, with a separate, much faster amplitude decay on the attack.
 * The pitch sweep is the "thump" and the click on top is a short burst of
 * the sweep's very beginning. The 808's long tail is just a very slow
 * amplitude decay on a nearly-static sine, which is why it works as a bass
 * note.
 *
 * SNARE. Two things summed: a tuned shell, modelled as two detuned sine
 * bodies about a fifth apart, and the snares themselves, which are noise
 * through a bandpass. The mix between them is the single most useful control
 * a snare has, and the reason a snare can be made to sit anywhere from a
 * rimshot to a clap.
 *
 * HI-HAT. On the real 808 this is six square oscillators at inharmonic
 * ratios through a high-pass, which is why it has that particular metallic
 * character rather than sounding like filtered noise. The ratios here are
 * the 808's.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ChiptuneTracker {

enum class DrumVoiceType : uint8_t { Kick = 0, Snare, HiHat, Count };

// ============================================================================
// Parameters
// ============================================================================
struct DrumModelConfig {
    DrumVoiceType voice = DrumVoiceType::Kick;

    // ---- Shared ----------------------------------------------------------
    float tuneHz = 55.0f;        // the body's fundamental
    float decaySeconds = 0.35f;  // amplitude decay
    float level = 0.9f;

    // How much the attack transient is emphasised. This is the click on a
    // kick and the stick on a snare, and it is what makes a drum cut through
    // a mix rather than merely being loud.
    float snap = 0.3f;

    // ---- Kick -------------------------------------------------------------
    // How far above the fundamental the pitch sweep starts, in semitones,
    // and how fast it falls. Together these are the thump.
    float pitchSweepSemitones = 36.0f;
    float pitchSweepSeconds = 0.05f;

    // ---- Snare ------------------------------------------------------------
    // 0 is all shell, 1 is all snares. The most useful control a snare has.
    float noiseMix = 0.5f;
    float noiseTone = 0.5f;      // bandpass centre, 0 dark .. 1 bright

    // ---- Hi-hat -----------------------------------------------------------
    float hatHighpass = 0.6f;    // 0 lets the body through, 1 is all sizzle

    // Drums are mostly played at one dynamic; this decides how much velocity
    // is allowed to matter.
    float velocityToLevel = 0.7f;
};

// ============================================================================
// The voice
// ============================================================================
class DrumModelVoice {
public:
    void trigger(const DrumModelConfig& config, float velocity, float engineRate,
                 uint32_t seed) {
        m_engineRate = (engineRate > 0.0f) ? engineRate : 44100.0f;
        m_time = 0.0f;
        m_phase = 0.0f;
        m_phase2 = 0.0f;
        for (float& phase : m_hatPhases) phase = 0.0f;
        m_bandpassState1 = 0.0f;
        m_bandpassState2 = 0.0f;
        m_highpassPrevIn = 0.0f;
        m_highpassPrevOut = 0.0f;
        m_rng = (seed == 0u) ? 0x1234567u : seed;

        const float clamped = std::clamp(velocity, 0.0f, 1.0f);
        m_level = 1.0f - config.velocityToLevel * (1.0f - clamped);
        m_playing = true;
    }

    void stop() { m_playing = false; }
    bool isPlaying() const { return m_playing; }

    float process(const DrumModelConfig& config) {
        if (!m_playing) return 0.0f;

        const float step = 1.0f / m_engineRate;
        const float decay = std::max(0.005f, config.decaySeconds);

        // Exponential, not linear: a linear decay sounds like a fade, and no
        // drum decays linearly. Stopping at four time constants rather than
        // running forever, because below -35 dB it is silence that still
        // costs a voice slot.
        const float envelope = std::exp(-m_time / (decay * 0.25f));
        if (m_time > decay * 4.0f) { m_playing = false; return 0.0f; }

        float out = 0.0f;
        switch (config.voice) {
            case DrumVoiceType::Snare:  out = snare(config, step); break;
            case DrumVoiceType::HiHat:  out = hat(config, step); break;
            case DrumVoiceType::Kick:
            default:                    out = kick(config, step); break;
        }

        // The attack transient. A very short second envelope on top of the
        // main one - the click of a kick, the stick of a snare.
        const float transient = std::exp(-m_time * 900.0f) * config.snap;

        m_time += step;
        return (out * envelope + out * transient) * config.level * m_level;
    }

private:
    float kick(const DrumModelConfig& config, float step) {
        // The pitch sweep. Exponential in semitones, so it sounds like a
        // pitch falling rather than a frequency falling - those are not the
        // same shape and only one of them is a kick drum.
        const float sweepTime = std::max(0.001f, config.pitchSweepSeconds);
        const float through = std::exp(-m_time / (sweepTime * 0.4f));
        const float semitones = config.pitchSweepSemitones * through;
        const float frequency = config.tuneHz * std::pow(2.0f, semitones / 12.0f);

        m_phase += frequency * step;
        if (m_phase >= 1.0f) m_phase -= std::floor(m_phase);
        return std::sin(6.28318530718f * m_phase);
    }

    float snare(const DrumModelConfig& config, float step) {
        // The shell: two sine bodies about a fifth apart, which is what a
        // tuned drum head with two dominant modes sounds like.
        m_phase += config.tuneHz * step;
        m_phase2 += config.tuneHz * 1.48f * step;
        if (m_phase >= 1.0f) m_phase -= std::floor(m_phase);
        if (m_phase2 >= 1.0f) m_phase2 -= std::floor(m_phase2);

        const float shell = 0.6f * std::sin(6.28318530718f * m_phase) +
                            0.4f * std::sin(6.28318530718f * m_phase2);

        // The snares: noise through a bandpass. A two-pole state variable,
        // which is cheap and has the resonant character the real thing does.
        const float noise = random11();
        const float centre = 400.0f + config.noiseTone * 4000.0f;
        const float f = std::clamp(2.0f * std::sin(3.14159265359f * centre * step),
                                   0.0f, 1.4f);
        const float q = 0.4f;
        const float high = noise - m_bandpassState2 - q * m_bandpassState1;
        m_bandpassState1 += f * high;
        m_bandpassState2 += f * m_bandpassState1;
        const float band = m_bandpassState1;

        const float mix = std::clamp(config.noiseMix, 0.0f, 1.0f);
        return shell * (1.0f - mix) + band * mix;
    }

    float hat(const DrumModelConfig& config, float step) {
        // The 808's six square oscillators at inharmonic ratios. These
        // specific ratios are why an 808 hat sounds like an 808 hat and not
        // like filtered noise.
        static constexpr float RATIOS[6] = {
            1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f
        };

        float sum = 0.0f;
        for (int i = 0; i < 6; ++i) {
            m_hatPhases[static_cast<size_t>(i)] +=
                config.tuneHz * 6.0f * RATIOS[i] * step;
            if (m_hatPhases[static_cast<size_t>(i)] >= 1.0f) {
                m_hatPhases[static_cast<size_t>(i)] -=
                    std::floor(m_hatPhases[static_cast<size_t>(i)]);
            }
            sum += (m_hatPhases[static_cast<size_t>(i)] < 0.5f) ? 1.0f : -1.0f;
        }
        sum /= 6.0f;

        // One-pole high-pass. Without it the stack of squares has far too
        // much low end and reads as a buzz rather than a hat.
        const float amount = 0.5f + 0.49f * std::clamp(config.hatHighpass, 0.0f, 1.0f);
        const float out = amount * (m_highpassPrevOut + sum - m_highpassPrevIn);
        m_highpassPrevIn = sum;
        m_highpassPrevOut = out;
        return out;
    }

    // Its own generator, for the same reason the granular voice has one:
    // rand() is not guaranteed reentrant and carries hidden shared state.
    float random11() {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return (static_cast<float>(m_rng & 0x00FFFFFFu) /
                static_cast<float>(0x00800000u)) - 1.0f;
    }

    float m_engineRate = 44100.0f;
    float m_time = 0.0f;
    float m_phase = 0.0f;
    float m_phase2 = 0.0f;
    std::array<float, 6> m_hatPhases{};
    float m_bandpassState1 = 0.0f;
    float m_bandpassState2 = 0.0f;
    float m_highpassPrevIn = 0.0f;
    float m_highpassPrevOut = 0.0f;
    float m_level = 1.0f;
    bool m_playing = false;
    uint32_t m_rng = 0x1234567u;
};

// ============================================================================
// Validation
// ============================================================================
inline void clampDrumModel(DrumModelConfig& config) {
    auto sane = [](float value, float lo, float hi, float fallback) {
        if (!std::isfinite(value)) return fallback;
        return std::max(lo, std::min(value, hi));
    };

    if (static_cast<int>(config.voice) < 0 ||
        config.voice >= DrumVoiceType::Count) {
        config.voice = DrumVoiceType::Kick;
    }

    // The floor is not cosmetic: a tune of zero makes the hat's six
    // oscillators all sit at DC, which is silence that looks like a bug.
    config.tuneHz = sane(config.tuneHz, 20.0f, 2000.0f, 55.0f);
    config.decaySeconds = sane(config.decaySeconds, 0.005f, 10.0f, 0.35f);
    config.level = sane(config.level, 0.0f, 2.0f, 0.9f);
    config.snap = sane(config.snap, 0.0f, 1.0f, 0.3f);
    config.pitchSweepSemitones = sane(config.pitchSweepSemitones, 0.0f, 72.0f, 36.0f);
    config.pitchSweepSeconds = sane(config.pitchSweepSeconds, 0.001f, 2.0f, 0.05f);
    config.noiseMix = sane(config.noiseMix, 0.0f, 1.0f, 0.5f);
    config.noiseTone = sane(config.noiseTone, 0.0f, 1.0f, 0.5f);
    config.hatHighpass = sane(config.hatHighpass, 0.0f, 1.0f, 0.6f);
    config.velocityToLevel = sane(config.velocityToLevel, 0.0f, 1.0f, 0.7f);
}

} // namespace ChiptuneTracker
