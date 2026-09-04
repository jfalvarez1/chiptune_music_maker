#pragma once

/*
 * ChiptuneTracker - Equalisers
 *
 * The tracker had one EQ: three fixed bands with adjustable gain and centre
 * frequency. It is enough to fix a dull channel and not enough to do
 * anything else.
 *
 * Four more, each of which exists because it does something the others
 * cannot:
 *
 *   TILT      one control. Turn it up and the top rises while the bottom
 *             falls, pivoting around a centre frequency. It is the fastest
 *             way to make something brighter or darker without deciding
 *             which band to touch, and it is the control most people
 *             actually want most of the time.
 *
 *   GRAPHIC   ten fixed octave bands. Not subtle, and very quick to read -
 *             the shape of the curve is the shape of the sliders.
 *
 *   MID-SIDE  the same EQ applied differently to what is common between the
 *             channels and what differs. Cutting bass from the sides alone
 *             tightens a mix without thinning it; brightening the sides
 *             widens without making the centre harsh. Neither is possible
 *             with any left-right EQ.
 *
 *   DYNAMIC   a band whose gain depends on how loud that band currently is.
 *             A static cut is always cutting; a dynamic one only acts when
 *             the problem is there, so a boxy note gets fixed without the
 *             whole part going thin.
 *
 * And a note on the one that is NOT here:
 *
 *   LINEAR PHASE would need an FFT of a few thousand points to be worth
 *   having, which is 50-100 ms of latency on every channel that uses it. The
 *   phase-vocoder effects already carry 23 ms and are labelled studio
 *   effects because of it. Nothing in this program compensates for latency
 *   yet - a channel using one simply runs late against the others - so
 *   adding a worse offender would make the mix drift further apart, not
 *   less. It belongs after latency compensation exists, and it is recorded
 *   as a gap in the test plan rather than shipped broken.
 *
 * All of these are minimum-phase biquads, allocation-free, and safe at every
 * setting. None is chip-authentic; all ship off.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ChiptuneTracker {

// ============================================================================
// A biquad, and the coefficients for the shapes used here
// ============================================================================
namespace eq {

inline constexpr float PI = 3.14159265358979323846f;

struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;

    // Direct Form II transposed: fewer state variables than DF-I and better
    // behaved in float, which matters when ten of these are in series.
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = 0.0f; z2 = 0.0f; }

    float process(float input) {
        const float out = b0 * input + z1;
        z1 = b1 * input - a1 * out + z2;
        z2 = b2 * input - a2 * out;
        return out;
    }

    void setPassthrough() {
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
    }

    /*
     * A peaking bell. The RBJ cookbook forms, which are the ones everybody
     * uses because they are numerically well behaved and their parameters
     * mean what their names say.
     *
     * The frequency is clamped below Nyquist: tan() runs to infinity at
     * Nyquist, and a band nudged past it by a high sample-rate project would
     * produce infinite coefficients rather than a gentle failure.
     */
    void setPeaking(float frequency, float q, float gainDb, float sampleRate) {
        const float rate = (sampleRate > 1.0f) ? sampleRate : 44100.0f;
        const float f = std::clamp(frequency, 10.0f, rate * 0.49f);
        const float safeQ = std::clamp(q, 0.1f, 18.0f);

        const float A = std::pow(10.0f, std::clamp(gainDb, -36.0f, 36.0f) / 40.0f);
        const float omega = 2.0f * PI * f / rate;
        const float alpha = std::sin(omega) / (2.0f * safeQ);
        const float cosw = std::cos(omega);

        const float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosw) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    void setLowShelf(float frequency, float gainDb, float sampleRate) {
        const float rate = (sampleRate > 1.0f) ? sampleRate : 44100.0f;
        const float f = std::clamp(frequency, 10.0f, rate * 0.49f);
        const float A = std::pow(10.0f, std::clamp(gainDb, -36.0f, 36.0f) / 40.0f);
        const float omega = 2.0f * PI * f / rate;
        const float cosw = std::cos(omega);
        const float sinw = std::sin(omega);
        const float beta = std::sqrt(A) / 0.707f;
        const float alpha = sinw / 2.0f * beta;

        const float a0 = (A + 1.0f) + (A - 1.0f) * cosw + 2.0f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw + 2.0f * std::sqrt(A) * alpha) / a0;
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw) / a0;
        b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * std::sqrt(A) * alpha) / a0;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw) / a0;
        a2 = ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * std::sqrt(A) * alpha) / a0;
    }

    void setHighShelf(float frequency, float gainDb, float sampleRate) {
        const float rate = (sampleRate > 1.0f) ? sampleRate : 44100.0f;
        const float f = std::clamp(frequency, 10.0f, rate * 0.49f);
        const float A = std::pow(10.0f, std::clamp(gainDb, -36.0f, 36.0f) / 40.0f);
        const float omega = 2.0f * PI * f / rate;
        const float cosw = std::cos(omega);
        const float sinw = std::sin(omega);
        const float beta = std::sqrt(A) / 0.707f;
        const float alpha = sinw / 2.0f * beta;

        const float a0 = (A + 1.0f) - (A - 1.0f) * cosw + 2.0f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * std::sqrt(A) * alpha) / a0;
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw) / a0;
        b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * std::sqrt(A) * alpha) / a0;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * std::sqrt(A) * alpha) / a0;
    }

    /*
     * A second-order high-pass, RBJ.
     *
     * Added for K-weighting, whose second stage is a high-pass at 38 Hz with
     * Q 0.5 - the part of BS.1770 that stops low frequencies counting
     * towards loudness for more than they are heard as. Useful anywhere a
     * rumble filter is wanted, which is why it lives here rather than beside
     * the meter.
     */
    void setHighPass(float frequency, float q, float sampleRate) {
        const float rate = (sampleRate > 1.0f) ? sampleRate : 44100.0f;
        const float f = std::clamp(frequency, 1.0f, rate * 0.49f);
        const float safeQ = std::clamp(q, 0.1f, 18.0f);
        const float omega = 2.0f * PI * f / rate;
        const float cosw = std::cos(omega);
        const float alpha = std::sin(omega) / (2.0f * safeQ);

        const float a0 = 1.0f + alpha;
        b0 = ((1.0f + cosw) * 0.5f) / a0;
        b1 = (-(1.0f + cosw)) / a0;
        b2 = ((1.0f + cosw) * 0.5f) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    // A bandpass used only for measurement - the dynamic EQ's detector needs
    // to know how loud one band is without altering the audio.
    void setBandpass(float frequency, float q, float sampleRate) {
        const float rate = (sampleRate > 1.0f) ? sampleRate : 44100.0f;
        const float f = std::clamp(frequency, 10.0f, rate * 0.49f);
        const float safeQ = std::clamp(q, 0.1f, 18.0f);
        const float omega = 2.0f * PI * f / rate;
        const float alpha = std::sin(omega) / (2.0f * safeQ);
        const float cosw = std::cos(omega);

        const float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b1 = 0.0f;
        b2 = -alpha / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }
};

} // namespace eq

// ============================================================================
// Tilt
// ============================================================================
/*
 * One knob. Positive tilts bright, negative tilts dark, pivoting around a
 * centre frequency: a low shelf down and a high shelf up by the same amount.
 * The pivot means the overall level barely moves, which is what makes it
 * usable as a quick judgement rather than a level change in disguise.
 */
class TiltEQ {
public:
    float tiltDb = 0.0f;       // -12..12
    float centreHz = 700.0f;

    void configure(float sampleRate) {
        m_sampleRate = sampleRate;
        update();
    }

    void set(float tilt, float centre) {
        if (tilt == tiltDb && centre == centreHz) return;
        tiltDb = tilt;
        centreHz = centre;
        update();
    }

    void reset() { m_low.reset(); m_high.reset(); }

    float process(float input) {
        return m_high.process(m_low.process(input));
    }

private:
    void update() {
        const float amount = std::clamp(tiltDb, -12.0f, 12.0f);
        m_low.setLowShelf(std::clamp(centreHz, 50.0f, 8000.0f), -amount, m_sampleRate);
        m_high.setHighShelf(std::clamp(centreHz, 50.0f, 8000.0f), amount, m_sampleRate);
    }

    eq::Biquad m_low, m_high;
    float m_sampleRate = 44100.0f;
};

// ============================================================================
// Graphic
// ============================================================================
/*
 * Ten fixed octave bands, ISO centres. Not subtle and very quick to read:
 * the shape of the curve is the shape of the sliders.
 */
class GraphicEQ {
public:
    static constexpr int BANDS = 10;
    static constexpr std::array<float, BANDS> CENTRES = {
        31.0f, 62.0f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    std::array<float, BANDS> gainDb{};

    void configure(float sampleRate) {
        m_sampleRate = sampleRate;
        update();
    }

    // Recomputing ten biquads is not something to do per sample, so the
    // caller says when the gains changed.
    void update() {
        for (int i = 0; i < BANDS; ++i) {
            const float gain = std::clamp(gainDb[static_cast<size_t>(i)], -18.0f, 18.0f);
            if (std::fabs(gain) < 0.01f) {
                // Exactly flat rather than a bell of 0 dB: a cascade of ten
                // near-unity biquads still accumulates a little phase and a
                // little numerical noise, and a graphic EQ sitting at zero
                // should be doing nothing at all.
                m_band[static_cast<size_t>(i)].setPassthrough();
            } else {
                // Q of 1.41 is a bit under one octave, so adjacent bands
                // overlap slightly and a straight line of sliders gives a
                // straight response rather than a row of bumps.
                m_band[static_cast<size_t>(i)].setPeaking(
                    CENTRES[static_cast<size_t>(i)], 1.41f, gain, m_sampleRate);
            }
        }
    }

    void reset() { for (eq::Biquad& band : m_band) band.reset(); }

    float process(float input) {
        float value = input;
        for (eq::Biquad& band : m_band) value = band.process(value);
        return value;
    }

private:
    std::array<eq::Biquad, BANDS> m_band;
    float m_sampleRate = 44100.0f;
};

// ============================================================================
// Mid-side
// ============================================================================
/*
 * The same three bands applied separately to the mid and the side.
 *
 * Mid is what the two channels have in common, side is what differs. Cutting
 * bass from the side alone tightens a mix without thinning it - bass is
 * nearly always mono in practice, so side bass is mostly phase trouble.
 * Brightening the side alone widens without making the centre harsh. Neither
 * is reachable with any left-right EQ, which is the whole reason this exists.
 */
class MidSideEQ {
public:
    struct Band { float frequency; float gainDb; float q; };

    std::array<Band, 3> mid{{{120.0f, 0.0f, 0.9f},
                             {1000.0f, 0.0f, 0.9f},
                             {6000.0f, 0.0f, 0.9f}}};
    std::array<Band, 3> side{{{120.0f, 0.0f, 0.9f},
                              {1000.0f, 0.0f, 0.9f},
                              {6000.0f, 0.0f, 0.9f}}};

    void configure(float sampleRate) {
        m_sampleRate = sampleRate;
        update();
    }

    void update() {
        for (int i = 0; i < 3; ++i) {
            const size_t index = static_cast<size_t>(i);
            if (std::fabs(mid[index].gainDb) < 0.01f) {
                m_mid[index].setPassthrough();
            } else {
                m_mid[index].setPeaking(mid[index].frequency, mid[index].q,
                                        mid[index].gainDb, m_sampleRate);
            }
            if (std::fabs(side[index].gainDb) < 0.01f) {
                m_side[index].setPassthrough();
            } else {
                m_side[index].setPeaking(side[index].frequency, side[index].q,
                                         side[index].gainDb, m_sampleRate);
            }
        }
    }

    void reset() {
        for (eq::Biquad& band : m_mid) band.reset();
        for (eq::Biquad& band : m_side) band.reset();
    }

    /*
     * Stereo in, stereo out.
     *
     * The encode and decode are the standard sum and difference. The 0.5 on
     * the way back is what makes the round trip exact when neither side is
     * touched - drop it and every mid-side stage doubles the level, which is
     * a very easy thing to leave in and blame on the EQ.
     */
    void process(float& left, float& right) {
        float m = (left + right) * 0.5f;
        float s = (left - right) * 0.5f;

        for (eq::Biquad& band : m_mid) m = band.process(m);
        for (eq::Biquad& band : m_side) s = band.process(s);

        left = m + s;
        right = m - s;
    }

private:
    std::array<eq::Biquad, 3> m_mid, m_side;
    float m_sampleRate = 44100.0f;
};

// ============================================================================
// Dynamic
// ============================================================================
/*
 * A band whose gain depends on how loud that band currently is.
 *
 * A static cut is always cutting. A dynamic one acts only when the problem is
 * actually present, so a note that turns boxy on the loud hits gets fixed
 * without the quiet ones going thin.
 *
 * The detector is a bandpass tap, not the full-band level - which is the
 * whole point. Ducking a 300 Hz band because a cymbal got loud would be a
 * compressor with extra steps.
 */
class DynamicEQ {
public:
    float frequency = 300.0f;
    float q = 1.2f;
    float thresholdDb = -24.0f;
    float rangeDb = -6.0f;      // negative cuts, positive boosts
    float attack = 0.010f;
    float release = 0.120f;

    void configure(float sampleRate) {
        m_sampleRate = (sampleRate > 1.0f) ? sampleRate : 44100.0f;
        update();
    }

    void update() {
        m_detector.setBandpass(frequency, q, m_sampleRate);
        m_appliedDb = 1e9f;      // force the filter to be rebuilt
    }

    void reset() {
        m_detector.reset();
        m_shaper.reset();
        m_envelope = 0.0f;
    }

    float process(float input) {
        // How loud this band is right now.
        const float banded = m_detector.process(input);
        const float level = std::fabs(banded);

        const float coefficient = (level > m_envelope)
            ? std::exp(-1.0f / (std::max(1e-4f, attack) * m_sampleRate))
            : std::exp(-1.0f / (std::max(1e-4f, release) * m_sampleRate));
        m_envelope = level + coefficient * (m_envelope - level);

        const float levelDb = 20.0f * std::log10(std::max(1e-6f, m_envelope));
        const float over = levelDb - thresholdDb;

        // Above the threshold the band moves toward its full range; below it
        // does nothing at all. Scaled over 12 dB so it eases in rather than
        // switching, which would be audible as a click on every transient.
        const float amount = std::clamp(over / 12.0f, 0.0f, 1.0f);
        const float wantDb = rangeDb * amount;

        /*
         * Rebuild only when the gain has actually moved.
         *
         * Recomputing a biquad involves a sin, a cos and a pow, and doing
         * that every sample would cost more than the rest of the channel put
         * together. A twentieth of a decibel is far below audible.
         */
        if (std::fabs(wantDb - m_appliedDb) > 0.05f) {
            m_shaper.setPeaking(frequency, q, wantDb, m_sampleRate);
            m_appliedDb = wantDb;
        }
        return m_shaper.process(input);
    }

    float currentGainDb() const { return m_appliedDb; }

private:
    eq::Biquad m_detector, m_shaper;
    float m_sampleRate = 44100.0f;
    float m_envelope = 0.0f;
    float m_appliedDb = 0.0f;
};

// ============================================================================
// Validation
// ============================================================================
inline void clampTiltEQ(float& tiltDb, float& centreHz) {
    if (!std::isfinite(tiltDb)) tiltDb = 0.0f;
    if (!std::isfinite(centreHz)) centreHz = 700.0f;
    tiltDb = std::clamp(tiltDb, -12.0f, 12.0f);
    centreHz = std::clamp(centreHz, 50.0f, 8000.0f);
}

inline void clampGraphicEQ(std::array<float, GraphicEQ::BANDS>& gains) {
    for (float& gain : gains) {
        if (!std::isfinite(gain)) gain = 0.0f;
        gain = std::clamp(gain, -18.0f, 18.0f);
    }
}

} // namespace ChiptuneTracker
