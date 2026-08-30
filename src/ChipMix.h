#pragma once

/*
 * ChiptuneTracker - Non-linear channel mixing and the console output filters
 *
 * We sum channels linearly: `left += sample * gain`. A real 2A03 does not.
 * Its two pulse channels share one non-linear DAC, and the triangle, noise
 * and DMC share another, so a channel gets quieter as its neighbours get
 * louder. Two pulses at full volume are 1.73x one pulse, not 2x.
 *
 * That matters musically in two ways, and both are things people complain
 * about in NES-alike tools:
 *
 *   1. Channels duck each other. Adding noise makes the triangle quieter.
 *      It is automatic compression that a linear mixer simply does not have,
 *      which is why linear tools sound flatter as voices stack up.
 *   2. The triangle is about 1.65x a pulse at the same level on hardware,
 *      against 1.13x when summed linearly - roughly 3.3 dB. That is why the
 *      bass "sits wrong" in emulations.
 *
 * What this is and is not. The engine is float and bipolar; the APU is
 * integer and unipolar, and its non-linearity is a waveshaper that also
 * dirties the waveform. Applying the transfer curve to our samples directly
 * would rectify them. So this derives a *gain* from the curve, using the
 * combined level of each group, and applies that gain to the ordinary linear
 * sum. It reproduces the ducking and the group balance - the two audible
 * consequences - and it does not claim to be a bit-exact APU model.
 *
 * Curves are the ones from the NESdev wiki's APU Mixer page:
 *
 *   pulse_out = 95.88 / (8128 / (pulse1 + pulse2) + 100)
 *   tnd_out   = 159.79 / (1 / (triangle/8227 + noise/12241 + dmc/22638) + 100)
 *
 * Off by default. Most channels here host a supersaw or a sample, which a
 * 2A03 never had, and grouping those as "pulse" would be meaningless.
 */

#include "Types.h"

#include <cmath>
#include <cstdint>

namespace ChiptuneTracker {

// Which DAC a channel would have shared on real hardware. Anything the 2A03
// never had passes through untouched rather than being forced into a group
// it does not belong in.
enum class ChipMixGroup : uint8_t {
    Linear = 0,
    Pulse,
    Triangle,
    Noise
};

inline ChipMixGroup chipMixGroupFor(OscillatorType type) {
    switch (type) {
        case OscillatorType::Pulse:    return ChipMixGroup::Pulse;
        case OscillatorType::Triangle: return ChipMixGroup::Triangle;
        case OscillatorType::Noise:    return ChipMixGroup::Noise;
        default:                       return ChipMixGroup::Linear;
    }
}

// A channel at magnitude 1.0 stands in for a hardware channel at full
// volume, which is level 15.
inline constexpr float CHIP_FULL_LEVEL = 15.0f;

// pulse1 + pulse2, each 0..15.
inline float nesPulseCurve(float levelSum) {
    if (levelSum <= 0.0f) return 0.0f;
    return 95.88f / (8128.0f / levelSum + 100.0f);
}

// triangle and noise 0..15, dmc 0..127. We have no DMC channel, so it is
// left at zero, but the parameter is here because holding the DMC level is a
// documented way to duck the triangle deliberately.
inline float nesTndCurve(float triangleLevel, float noiseLevel,
                         float dmcLevel = 0.0f) {
    const float accum = triangleLevel / 8227.0f
                      + noiseLevel / 12241.0f
                      + dmcLevel / 22638.0f;
    if (accum <= 0.0f) return 0.0f;
    return 159.79f / (1.0f / accum + 100.0f);
}

// One pulse channel at full volume. Everything is normalised against this,
// so a single pulse passes at unity and the triangle comes out louder than
// it would linearly - which is the point.
inline float nesUnityReference() {
    return nesPulseCurve(CHIP_FULL_LEVEL);
}

struct ChipMixGains {
    float pulse = 1.0f;
    float tnd = 1.0f;
};

/*
 * Gains to apply to each group, given the summed magnitude of the channels
 * currently in it. Magnitudes are the ordinary 0..1 sample magnitudes; a
 * group with nothing in it gets unity so the caller need not special-case it.
 */
inline ChipMixGains computeChipMixGains(float pulseMagSum,
                                        float triangleMagSum,
                                        float noiseMagSum) {
    ChipMixGains gains;
    const float unity = nesUnityReference();

    if (pulseMagSum > 1e-6f) {
        const float level = pulseMagSum * CHIP_FULL_LEVEL;
        const float linear = pulseMagSum * unity;
        gains.pulse = nesPulseCurve(level) / linear;
    }

    const float tndMagSum = triangleMagSum + noiseMagSum;
    if (tndMagSum > 1e-6f) {
        const float out = nesTndCurve(triangleMagSum * CHIP_FULL_LEVEL,
                                      noiseMagSum * CHIP_FULL_LEVEL);
        // Divided by the group's linear magnitude, so the result is a gain
        // rather than a level. The triangle ends up louder than a pulse at
        // the same magnitude, which is the hardware balance we are after.
        gains.tnd = (out / unity) / tndMagSum;
    }

    // A pathological input must never hand the audio thread a NaN.
    if (!std::isfinite(gains.pulse)) gains.pulse = 1.0f;
    if (!std::isfinite(gains.tnd)) gains.tnd = 1.0f;
    return gains;
}

/*
 * The console's output filters.
 *
 * The NES runs its mixed output through two high-passes and a low-pass. The
 * 440 Hz high-pass is the one everybody forgets, and its absence is why
 * emulations sound bass-heavy; the 14 kHz low-pass is why they sound harsh
 * without it. A Famicom is gentler - one high-pass at 37 Hz and no
 * low-pass - which is a large part of why the two machines sound different
 * through the same ROM.
 *
 * First-order sections, matching the hardware's single-pole RC networks.
 */
class ChipFilterChain {
public:
    enum class Mode : uint8_t {
        NES = 0,      // 90 Hz HP -> 440 Hz HP -> 14 kHz LP
        Famicom       // 37 Hz HP only
    };

    void configure(float sampleRate, Mode mode) {
        m_sampleRate = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
        m_mode = mode;

        if (mode == Mode::NES) {
            m_hp1Coeff = highPassCoeff(90.0f);
            m_hp2Coeff = highPassCoeff(440.0f);
            m_lpCoeff = lowPassCoeff(14000.0f);
            m_useSecondHighPass = true;
            m_useLowPass = true;
        } else {
            m_hp1Coeff = highPassCoeff(37.0f);
            m_hp2Coeff = 0.0f;
            m_lpCoeff = 0.0f;
            m_useSecondHighPass = false;
            m_useLowPass = false;
        }
        reset();
    }

    void reset() {
        for (int c = 0; c < 2; ++c) {
            m_hp1In[c] = m_hp1Out[c] = 0.0f;
            m_hp2In[c] = m_hp2Out[c] = 0.0f;
            m_lpOut[c] = 0.0f;
        }
    }

    void process(float& left, float& right) {
        left = processOne(left, 0);
        right = processOne(right, 1);
    }

private:
    // y[n] = a * (y[n-1] + x[n] - x[n-1])
    float highPassCoeff(float cutoffHz) const {
        const float rc = 1.0f / (2.0f * 3.14159265358979f * cutoffHz);
        const float dt = 1.0f / m_sampleRate;
        return rc / (rc + dt);
    }

    // y[n] = y[n-1] + b * (x[n] - y[n-1])
    float lowPassCoeff(float cutoffHz) const {
        const float rc = 1.0f / (2.0f * 3.14159265358979f * cutoffHz);
        const float dt = 1.0f / m_sampleRate;
        return dt / (rc + dt);
    }

    float processOne(float x, int channel) {
        if (!std::isfinite(x)) x = 0.0f;

        float y = m_hp1Coeff * (m_hp1Out[channel] + x - m_hp1In[channel]);
        m_hp1In[channel] = x;
        m_hp1Out[channel] = y;

        if (m_useSecondHighPass) {
            const float in = y;
            y = m_hp2Coeff * (m_hp2Out[channel] + in - m_hp2In[channel]);
            m_hp2In[channel] = in;
            m_hp2Out[channel] = y;
        }

        if (m_useLowPass) {
            m_lpOut[channel] += m_lpCoeff * (y - m_lpOut[channel]);
            y = m_lpOut[channel];
        }

        return y;
    }

    float m_sampleRate = 44100.0f;
    Mode m_mode = Mode::NES;

    float m_hp1Coeff = 0.0f;
    float m_hp2Coeff = 0.0f;
    float m_lpCoeff = 0.0f;
    bool m_useSecondHighPass = true;
    bool m_useLowPass = true;

    float m_hp1In[2] = {0.0f, 0.0f};
    float m_hp1Out[2] = {0.0f, 0.0f};
    float m_hp2In[2] = {0.0f, 0.0f};
    float m_hp2Out[2] = {0.0f, 0.0f};
    float m_lpOut[2] = {0.0f, 0.0f};
};

} // namespace ChiptuneTracker
