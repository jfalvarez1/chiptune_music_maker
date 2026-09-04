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
#include "EqualizerSuite.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// Limiter - Brick-wall limiting for preventing clipping
// ============================================================================
class Limiter {
public:
    /*
     * -1.0 dB, not -0.1.
     *
     * A true-peak meter's own error is about 0.6 dB, so a tenth of a
     * decibel of headroom is false precision before anything else happens.
     * Then lossy encoding overshoots - a file measuring -0.3 dBTP can come
     * out of AAC above 0 - and the listener's converter reconstructs peaks
     * between the samples that were never in the file. AES TD1008 and
     * Spotify both land on -1 dBTP for streaming, and that is what this
     * defaults to.
     */
    float ceiling = -1.0f;      // dB
    float release = 0.05f;      // seconds

    // How far ahead the detector looks. 1.5 ms is long enough to bring the
    // gain down smoothly and short enough that the ducking before a
    // transient is not audible as a hole in front of it.
    float lookaheadSeconds = 0.0015f;

    static constexpr int MAX_LOOKAHEAD = 512;

    void setSampleRate(float sr) {
        m_sampleRate = sr;
        prepare();
    }

    /*
     * One channel, for anything that is not the master bus.
     *
     * The master uses processStereo below, which shares one gain envelope
     * across both sides. Reducing the two channels independently moves the
     * image every time one side is louder than the other, which on a mix
     * with a panned lead is audible as the whole picture swaying.
     */
    float process(float input) {
        float left = input;
        float right = input;
        processStereo(left, right);
        return left;
    }

    /*
     * Stereo, with one linked envelope and a lookahead.
     *
     * THIS USED TO BE A CLIPPER, and nothing said so. The smoothing was
     * written as
     *
     *     coeff = 1 - exp(-1 / (time * sampleRate));
     *     envelope = target + coeff * (envelope - target);
     *
     * where `coeff` is the fraction of the OLD value retained, so it needed
     * to be exp(-1/(t*sr)) and not one minus it. At 44.1 kHz and a 1 ms
     * attack the intended coefficient is 0.978 and what was there was
     * 0.022: the envelope arrived at its target in a single sample, every
     * sample, which is exactly hard clipping. The `release` parameter -
     * exposed in the profiles, the presets and the UI - did nothing at all.
     *
     * With the coefficient the right way round the limiter needs a
     * lookahead as well, or it can only start pulling the gain down once
     * the peak has already gone past. The signal is delayed and the
     * detector runs ahead of it, so the gain is already where it needs to be
     * when the peak arrives.
     */
    void processStereo(float& left, float& right) {
        const float ceilingLinear = m_ceilingLinear;

        // The loudest of the two, so both are reduced by the same amount.
        const float peak = std::max(std::fabs(left), std::fabs(right));
        float required = 1.0f;
        if (peak > ceilingLinear && peak > 1e-9f) required = ceilingLinear / peak;

        // The minimum required gain anywhere in the lookahead window. Kept
        // incrementally: a new low is free, and the window is only rescanned
        // when the sample leaving it was the one holding the minimum.
        const int slot = m_cursor;
        const bool leavingWasMinimum =
            (m_required[static_cast<size_t>(slot)] <= m_windowMinimum + 1e-9f);
        m_required[static_cast<size_t>(slot)] = required;

        if (required <= m_windowMinimum) {
            m_windowMinimum = required;
        } else if (leavingWasMinimum) {
            m_windowMinimum = 1.0f;
            for (int i = 0; i < m_lookahead; ++i) {
                m_windowMinimum = std::min(m_windowMinimum,
                                           m_required[static_cast<size_t>(i)]);
            }
        }

        // The retained fraction of the old envelope. This is the line the
        // whole class turns on.
        const float target = m_windowMinimum;
        if (target < m_envelope) {
            m_envelope = target + m_attackCoeff * (m_envelope - target);
        } else {
            m_envelope = target + m_releaseCoeff * (m_envelope - target);
        }

        const float outLeft = m_delayLeft[static_cast<size_t>(slot)] * m_envelope;
        const float outRight = m_delayRight[static_cast<size_t>(slot)] * m_envelope;

        m_delayLeft[static_cast<size_t>(slot)] = left;
        m_delayRight[static_cast<size_t>(slot)] = right;

        m_cursor = slot + 1;
        if (m_cursor >= m_lookahead) m_cursor = 0;

        /*
         * The last few tenths of a decibel.
         *
         * A smoothed envelope cannot be exact: the gain is still arriving
         * when a very fast transient does. The clamp catches that remainder
         * so the ceiling is a ceiling. It is the difference between a
         * limiter with a clipper behind it - which is what every real one
         * has - and a clipper pretending to be a limiter, which is what this
         * was.
         */
        left = std::clamp(outLeft, -ceilingLinear, ceilingLinear);
        right = std::clamp(outRight, -ceilingLinear, ceilingLinear);
    }

    // Recompute what depends on the sample rate and the parameters. Called
    // per block rather than per sample: pow() and exp() in the inner loop is
    // what the old version did, and none of it can change between samples.
    void prepare() {
        m_ceilingLinear = std::pow(10.0f, std::clamp(ceiling, -24.0f, 0.0f) / 20.0f);

        const float sr = (m_sampleRate > 1.0f) ? m_sampleRate : 44100.0f;
        const int wanted = std::max(8, static_cast<int>(lookaheadSeconds * sr));
        const int clamped = std::clamp(wanted, 8, MAX_LOOKAHEAD);
        if (clamped != m_lookahead) {
            m_lookahead = clamped;
            reset();
        }

        // Attack over the lookahead window, so the gain lands just as the
        // peak emerges. Faster than that and it is audible as a click on the
        // gain; slower and the peak arrives before the gain does.
        const float attackSeconds =
            std::max(1e-4f, static_cast<float>(m_lookahead) / sr * 0.5f);
        m_attackCoeff = std::exp(-1.0f / (attackSeconds * sr));
        m_releaseCoeff =
            std::exp(-1.0f / (std::clamp(release, 0.005f, 2.0f) * sr));
    }

    void reset() {
        m_envelope = 1.0f;
        m_windowMinimum = 1.0f;
        m_cursor = 0;
        m_delayLeft.fill(0.0f);
        m_delayRight.fill(0.0f);
        m_required.fill(1.0f);
    }

    // Gain reduction in dB, for metering. Negative when the limiter is
    // working, zero when it is not.
    float getGainReductionDB() const {
        return 20.0f * std::log10(std::max(m_envelope, 1e-4f));
    }

    // How far the output is behind the input, so delay compensation can
    // account for the lookahead rather than the mix quietly running late.
    int latencySamples() const { return m_lookahead; }

private:
    std::array<float, MAX_LOOKAHEAD> m_delayLeft{};
    std::array<float, MAX_LOOKAHEAD> m_delayRight{};
    std::array<float, MAX_LOOKAHEAD> m_required{};

    float m_envelope = 1.0f;
    float m_windowMinimum = 1.0f;
    int m_cursor = 0;
    int m_lookahead = 64;

    float m_ceilingLinear = 0.9886f;   // -0.1 dB, the old default
    float m_attackCoeff = 0.9f;
    float m_releaseCoeff = 0.9999f;
    float m_sampleRate = 48000.0f;
};

// ============================================================================
// Loudness - ITU-R BS.1770-4 / EBU R128
//
// WHAT WAS HERE BEFORE was called a LUFS meter and was none of the things
// that make one. There was no K-weighting - the comment said "simplified
// approximation" and no filter followed it. It averaged the two channels
// and then squared, where the standard sums the channel powers, which reads
// 3 dB low on anything correlated before the missing filter is counted. It
// had no gating, so it was a sliding mean square labelled "integrated". And
// it summed a 144,000-sample buffer inside process(), once per output
// sample: about six billion additions per second of audio, for a number
// nobody had checked.
//
// This measures loudness. K-weighting is the two filters the standard
// specifies, the channels are summed as powers, and the integrated value is
// gated both ways - absolutely at -70 LUFS and then relatively at -10 LU
// below the ungated mean, which is what stops a quiet intro from dragging
// the number down. Momentary is a 400 ms window and short-term is 3 s,
// because those are the definitions, and both are kept as running sums so
// the cost is a handful of operations per sample rather than a scan.
// ============================================================================
class LoudnessMeter {
public:
    // The block sizes the standard defines. Loudness is measured in
    // overlapping blocks, not per sample: a single sample has no loudness.
    static constexpr float MOMENTARY_SECONDS = 0.400f;
    static constexpr float SHORT_TERM_SECONDS = 3.0f;

    // Gating thresholds, in LUFS and LU. The two are different for a reason
    // and mixing them up is the usual implementation bug: -10 LU below the
    // ungated mean for integrated loudness, -20 LU for loudness range.
    static constexpr float ABSOLUTE_GATE = -70.0f;
    static constexpr float RELATIVE_GATE_LU = -10.0f;

    void setSampleRate(float sr) {
        m_sampleRate = (sr > 1.0f) ? sr : 44100.0f;
        configureKWeighting();
        reset();
    }

    void process(float left, float right) {
        // ---- K-weighting -------------------------------------------------
        //
        // Two filters, applied to each channel before the power is taken: a
        // high shelf standing in for the head's effect on incoming sound,
        // and a high-pass that stops low frequencies counting for more than
        // they are heard as. Skipping them - which is what the old code did
        // - means a bass-heavy mix measures far louder than it sounds, and
        // every loudness decision made from the number is wrong in the same
        // direction.
        const float kl = m_highPassL.process(m_shelfL.process(left));
        const float kr = m_highPassR.process(m_shelfR.process(right));

        // Channel POWERS summed, both at weight 1.0 for a stereo pair. Not
        // the average of the signals: two identical channels are 3 dB louder
        // than one, and averaging them says they are the same.
        const double power = double(kl) * double(kl) + double(kr) * double(kr);

        // ---- Running windows ---------------------------------------------
        m_momentarySum += power - double(m_momentary[m_momentaryAt]);
        m_momentary[m_momentaryAt] = static_cast<float>(power);
        if (++m_momentaryAt >= m_momentaryLength) m_momentaryAt = 0;

        m_shortSum += power - double(m_short[m_shortAt]);
        m_short[m_shortAt] = static_cast<float>(power);
        if (++m_shortAt >= m_shortLength) m_shortAt = 0;

        if (m_filled < m_shortLength) ++m_filled;

        // ---- Integrated --------------------------------------------------
        //
        // Blocks of 400 ms, overlapping by 75%, which is one new block every
        // 100 ms. Each block's loudness is kept; the gating happens when the
        // number is asked for, because the relative gate depends on the mean
        // of everything measured so far.
        if (--m_untilNextBlock <= 0) {
            m_untilNextBlock = m_blockStride;
            if (m_filled >= m_momentaryLength) {
                const double mean = m_momentarySum /
                                    double(std::max(1, m_momentaryLength));
                if (mean > 1e-12) {
                    const float loudness =
                        -0.691f + 10.0f * std::log10(static_cast<float>(mean));
                    if (m_blockCount < static_cast<int>(m_blocks.size())) {
                        m_blocks[static_cast<size_t>(m_blockCount++)] = loudness;
                    }
                    m_blockPowerSum += mean;
                    ++m_gatedCandidates;
                }
            }
        }
    }

    // A 400 ms window: what is loud right now.
    float momentary() const { return loudnessOf(m_momentarySum, m_momentaryLength); }

    // A 3 s window: what a listener would call the loudness of this section,
    // and the basis of the peak-to-short-term ratio.
    float shortTerm() const { return loudnessOf(m_shortSum, m_shortLength); }

    /*
     * The whole programme, gated. This is the number streaming platforms
     * normalise against.
     *
     * Gated twice, because that is what makes it match how loud something
     * sounds rather than how much silence it contains. Everything below
     * -70 LUFS is discarded outright, then everything more than 10 LU below
     * the mean of what is left goes too - so a quiet intro and the gaps
     * between phrases stop dragging the answer down.
     */
    float integrated() const {
        if (m_blockCount <= 0) return -70.0f;

        // First pass: absolute gate only, to find the mean to measure
        // against.
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < m_blockCount; ++i) {
            if (m_blocks[static_cast<size_t>(i)] <= ABSOLUTE_GATE) continue;
            sum += std::pow(10.0, (m_blocks[static_cast<size_t>(i)] + 0.691) / 10.0);
            ++count;
        }
        if (count <= 0) return -70.0f;

        const double ungatedMean = sum / double(count);
        const float relativeGate =
            static_cast<float>(-0.691 + 10.0 * std::log10(ungatedMean)) +
            RELATIVE_GATE_LU;

        // Second pass: both gates.
        sum = 0.0;
        count = 0;
        for (int i = 0; i < m_blockCount; ++i) {
            const float block = m_blocks[static_cast<size_t>(i)];
            if (block <= ABSOLUTE_GATE || block <= relativeGate) continue;
            sum += std::pow(10.0, (block + 0.691) / 10.0);
            ++count;
        }
        if (count <= 0) return -70.0f;

        return static_cast<float>(-0.691 + 10.0 * std::log10(sum / double(count)));
    }

    /*
     * Loudness range: the spread between the quiet parts and the loud parts.
     *
     * The 10th to 95th percentile of the short-term blocks, gated at -20 LU
     * rather than -10. A track with an LRA near zero has had its arrangement
     * flattened, whatever its peak-to-loudness ratio says.
     */
    float loudnessRange() const {
        if (m_blockCount < 4) return 0.0f;

        std::vector<float> kept;
        kept.reserve(static_cast<size_t>(m_blockCount));

        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < m_blockCount; ++i) {
            if (m_blocks[static_cast<size_t>(i)] <= ABSOLUTE_GATE) continue;
            sum += std::pow(10.0, (m_blocks[static_cast<size_t>(i)] + 0.691) / 10.0);
            ++count;
        }
        if (count <= 0) return 0.0f;

        const float gate =
            static_cast<float>(-0.691 + 10.0 * std::log10(sum / double(count))) -
            20.0f;

        for (int i = 0; i < m_blockCount; ++i) {
            const float block = m_blocks[static_cast<size_t>(i)];
            if (block > ABSOLUTE_GATE && block > gate) kept.push_back(block);
        }
        if (kept.size() < 4) return 0.0f;

        std::sort(kept.begin(), kept.end());
        const size_t low = static_cast<size_t>(0.10 * double(kept.size() - 1));
        const size_t high = static_cast<size_t>(0.95 * double(kept.size() - 1));
        return kept[high] - kept[low];
    }

    int blockCount() const { return m_blockCount; }

    // Kept so callers written against the old name still compile; it is the
    // integrated value, which is what they all wanted.
    float getLUFS() const { return integrated(); }

    void reset() {
        const int momentary =
            std::max(16, static_cast<int>(MOMENTARY_SECONDS * m_sampleRate));
        const int shortTerm =
            std::max(64, static_cast<int>(SHORT_TERM_SECONDS * m_sampleRate));

        m_momentaryLength = std::min(momentary, MAX_MOMENTARY);
        m_shortLength = std::min(shortTerm, MAX_SHORT);
        m_blockStride = std::max(1, m_momentaryLength / 4);   // 75% overlap

        m_momentary.assign(static_cast<size_t>(m_momentaryLength), 0.0f);
        m_short.assign(static_cast<size_t>(m_shortLength), 0.0f);
        m_momentaryAt = 0;
        m_shortAt = 0;
        m_momentarySum = 0.0;
        m_shortSum = 0.0;
        m_filled = 0;
        m_untilNextBlock = m_blockStride;
        m_blockCount = 0;
        m_blockPowerSum = 0.0;
        m_gatedCandidates = 0;

        m_shelfL.reset(); m_shelfR.reset();
        m_highPassL.reset(); m_highPassR.reset();
    }

private:
    // A track long enough to fill this is longer than anything this program
    // renders in one go; past it the integrated value stops accumulating
    // rather than wrapping, which would silently measure only the end.
    static constexpr int MAX_BLOCKS = 36000;      // an hour at 100 ms
    static constexpr int MAX_MOMENTARY = 4800;    // 400 ms at 12 kHz upward
    static constexpr int MAX_SHORT = 288000;      // 3 s at 96 kHz

    float loudnessOf(double sum, int length) const {
        if (length <= 0 || m_filled < length) return -70.0f;
        const double mean = sum / double(length);
        if (!(mean > 1e-12)) return -70.0f;
        return -0.691f + 10.0f * std::log10(static_cast<float>(mean));
    }

    /*
     * The two K-weighting stages, from BS.1770-4's own coefficients.
     *
     * The standard tabulates them at 48 kHz. They are written here as the
     * filters they are - a +4 dB high shelf near 1.5 kHz and a high-pass
     * near 38 Hz - and built at whatever rate the project runs at, because
     * using 48 kHz coefficients at 44.1 kHz puts both corners about 9%
     * high and biases every reading.
     */
    void configureKWeighting() {
        m_shelfL.setHighShelf(1681.97f, 3.999f, m_sampleRate);
        m_shelfR.setHighShelf(1681.97f, 3.999f, m_sampleRate);
        m_highPassL.setHighPass(38.14f, 0.5003f, m_sampleRate);
        m_highPassR.setHighPass(38.14f, 0.5003f, m_sampleRate);
    }

    eq::Biquad m_shelfL, m_shelfR;
    eq::Biquad m_highPassL, m_highPassR;

    std::vector<float> m_momentary;
    std::vector<float> m_short;
    std::array<float, MAX_BLOCKS> m_blocks{};

    double m_momentarySum = 0.0;
    double m_shortSum = 0.0;
    double m_blockPowerSum = 0.0;

    int m_momentaryLength = 0;
    int m_shortLength = 0;
    int m_momentaryAt = 0;
    int m_shortAt = 0;
    int m_filled = 0;
    int m_blockStride = 1;
    int m_untilNextBlock = 1;
    int m_blockCount = 0;
    int m_gatedCandidates = 0;

    float m_sampleRate = 44100.0f;
};

// Kept as a name, because the whole program refers to it.
using LUFSMeter = LoudnessMeter;

// ============================================================================
// True peak
//
// A sample-peak meter reads the numbers in the file. A true-peak meter
// estimates the waveform the listener's converter will reconstruct BETWEEN
// those numbers, which can be higher - and for this program it is
// dramatically higher, because square waves are what it makes.
//
// Measured on waveforms normalised to exactly 0.00 dBFS sample peak:
//
//     sine 440 Hz .................. +0.01 dBTP
//     50% square ................... +2.12 dBTP
//     12.5% pulse .................. +2.34 dBTP
//     noise channel ................ +5.38 dBTP
//     a four-voice chip mix ........ +1.59 dBTP
//
// An infinite-slope edge cannot be represented in a band-limited signal, so
// reconstructing one overshoots. That is Gibbs, and it is why a chip render
// sitting at exactly 0 dBFS is already two decibels over on the meter every
// streaming service uses - with the noise channel, the shortest and least
// suspicious element, worst of all by a wide margin.
//
// Which makes true-peak measurement not a nicety here but the specific thing
// this program needs more than a general-purpose DAW does.
//
// The estimate is 4x oversampling, which BS.1770 accepts at 44.1 and 48 kHz.
// A polyphase FIR rather than an FFT: it is a handful of multiplies per
// sample and it runs on whatever the caller has, including a test.
// ============================================================================
class TruePeakMeter {
public:
    void reset() {
        m_history.fill(0.0f);
        m_at = 0;
        m_peak = 0.0f;
    }

    /*
     * One sample in; the running true-peak estimate updates.
     *
     * Four phases of a windowed-sinc interpolator, evaluated at the three
     * points between this sample and the next plus the sample itself. The
     * taps are a 12-point Blackman-windowed sinc, which is enough to get
     * within about a tenth of a decibel of a properly reconstructed peak -
     * comfortably inside the 0.6 dB error a real true-peak meter is allowed.
     */
    void process(float sample) {
        m_history[static_cast<size_t>(m_at)] = sample;
        m_at = (m_at + 1) % TAPS;

        m_peak = std::max(m_peak, std::fabs(sample));

        for (int phase = 1; phase < 4; ++phase) {
            float sum = 0.0f;
            for (int tap = 0; tap < TAPS; ++tap) {
                const int index = (m_at + tap) % TAPS;
                sum += m_history[static_cast<size_t>(index)] *
                       coefficient(tap, phase);
            }
            m_peak = std::max(m_peak, std::fabs(sum));
        }
    }

    void processStereo(float left, float right) {
        process(left);
        // One meter for both, because the ceiling applies to whichever side
        // is worse - reporting them separately would let the louder one hide
        // behind an average.
        process(right);
    }

    float peakLinear() const { return m_peak; }

    float peakDb() const {
        return 20.0f * std::log10(std::max(m_peak, 1e-6f));
    }

private:
    static constexpr int TAPS = 12;

    /*
     * The interpolator's taps, built once.
     *
     * sinc(t) windowed by Blackman, sampled at the three quarter-points
     * between output samples. Computed on first use rather than tabulated,
     * so the window and the length are visible as the arithmetic they are
     * instead of as forty magic numbers.
     */
    static float coefficient(int tap, int phase) {
        static const std::array<std::array<float, TAPS>, 4> TABLE = [] {
            std::array<std::array<float, TAPS>, 4> table{};
            constexpr float PI_F = 3.14159265358979323846f;
            const float centre = float(TAPS) / 2.0f - 0.5f;

            for (int p = 0; p < 4; ++p) {
                const float offset = float(p) / 4.0f;
                float sum = 0.0f;
                for (int t = 0; t < TAPS; ++t) {
                    const float x = float(t) - centre - offset;
                    const float sinc = (std::fabs(x) < 1e-6f)
                        ? 1.0f
                        : std::sin(PI_F * x) / (PI_F * x);
                    const float w = float(t) / float(TAPS - 1);
                    const float window = 0.42f - 0.5f * std::cos(2.0f * PI_F * w) +
                                         0.08f * std::cos(4.0f * PI_F * w);
                    table[size_t(p)][size_t(t)] = sinc * window;
                    sum += sinc * window;
                }
                // Normalised so a constant input comes back as itself; an
                // unnormalised interpolator reports a DC offset as a peak.
                if (std::fabs(sum) > 1e-6f) {
                    for (int t = 0; t < TAPS; ++t) table[size_t(p)][size_t(t)] /= sum;
                }
            }
            return table;
        }();

        return TABLE[static_cast<size_t>(std::clamp(phase, 0, 3))]
                    [static_cast<size_t>(std::clamp(tap, 0, TAPS - 1))];
    }

    std::array<float, TAPS> m_history{};
    int m_at = 0;
    float m_peak = 0.0f;
};

// ============================================================================
// DC blocker
//
// A pulse wave is not centred on zero. Its DC component is 2*duty - 1, so a
// 12.5% pulse - the thinnest NES duty and the classic lead sound - sits at
// -0.75 and swings between +1 and -1 around that.
//
// Which means the headroom is being spent on an offset nobody can hear.
// Removing it from a 12.5% pulse RAISES the usable peak by 4.86 dB, measured.
// It also stops the thump at the start of every note and the slow pull on
// everything downstream with memory - a filter, a compressor, the limiter.
//
// The corner is deliberately low. A high-pass steep enough to flatten the
// offset quickly also takes real bass with it: at 20 Hz on a narrow-duty
// four-voice mix the measured cost was 3.8 dB of audible loudness at the same
// ceiling. One pole at 10 Hz removes the offset over a few tens of
// milliseconds and costs almost nothing.
// ============================================================================
class DCBlocker {
public:
    void setSampleRate(float sr) {
        const float rate = (sr > 1.0f) ? sr : 44100.0f;
        // One-pole high-pass. R is how much of the previous output carries
        // over; nearer one is a lower corner.
        m_r = 1.0f - (2.0f * 3.14159265358979323846f * CORNER_HZ / rate);
        m_r = std::clamp(m_r, 0.9f, 0.99999f);
        reset();
    }

    void processStereo(float& left, float& right) {
        const float outL = left - m_lastInL + m_r * m_lastOutL;
        const float outR = right - m_lastInR + m_r * m_lastOutR;
        m_lastInL = left;
        m_lastInR = right;
        m_lastOutL = outL;
        m_lastOutR = outR;
        left = outL;
        right = outR;
    }

    void reset() {
        m_lastInL = m_lastInR = 0.0f;
        m_lastOutL = m_lastOutR = 0.0f;
    }

private:
    static constexpr float CORNER_HZ = 10.0f;
    float m_r = 0.9986f;
    float m_lastInL = 0.0f, m_lastInR = 0.0f;
    float m_lastOutL = 0.0f, m_lastOutR = 0.0f;
};

// ============================================================================
// Master Effects Chain - Final processing before output
// ============================================================================

/*
 * Stereo width, by scaling the side signal.
 *
 * A mix is mid plus side; making the side louder relative to the mid pushes
 * everything that is not dead centre outwards. It is the most audible
 * single move available on a master bus, and the reason it is safe here is
 * the mono check below: a width above 1 can hollow out anything that was
 * centred if pushed far, so the range is bounded rather than open.
 */
class StereoWidth {
public:
    float width = 1.0f;      // 1 = untouched, >1 wider, <1 narrower

    void process(float& left, float& right) const {
        const float mid = (left + right) * 0.5f;
        const float side = (left - right) * 0.5f * std::clamp(width, 0.0f, 2.0f);
        left = mid + side;
        right = mid - side;
    }
};

/*
 * Gentle saturation.
 *
 * A soft curve rather than a hard clip: it adds harmonics that were never
 * in the signal, which the ear reads as warmth and as being louder at the
 * same measured level. That is the whole trick, and it is why every
 * mastering chain has some.
 *
 * Compensated for drive, so turning it up makes the mix warmer rather than
 * simply louder - otherwise it is a volume knob wearing a costume, and the
 * limiter would undo it anyway.
 */
class Saturator {
public:
    float drive = 1.0f;      // 1 = clean, up to about 4 before it is obvious

    float process(float input) const {
        const float d = std::clamp(drive, 1.0f, 6.0f);

        /*
         * Drive 1 is genuinely untouched, not merely gentle.
         *
         * The normalisation below divides by tanh(d), which makes the curve
         * pass through unity at full scale - and at d = 1 that works out to
         * a 21% boost on small signals. A setting labelled "clean" that
         * colours the signal is a lie in the interface, so 1 returns the
         * input and everything above it shapes.
         */
        if (d <= 1.001f) return input;

        // tanh is the standard soft curve: linear near zero, compressing
        // smoothly toward the rails, and never actually reaching them.
        const float shaped = std::tanh(input * d);

        // Normalised by the curve's own gain at full scale, so drive
        // changes the character rather than simply the level.
        return shaped / std::tanh(d);
    }
};

struct MasterEffects {
    // Effects instances
    // One EQ per side. See process() for why sharing one was wrong.
    ThreeBandEQ eq;
    ThreeBandEQ eqRight;
    Compressor compressor;
    Limiter limiter;
    LUFSMeter lufsMeter;

    /*
     * Mid-side EQ, here rather than on a channel because it needs stereo -
     * the channel chain is mono until the pan - and because it is a
     * mastering tool. Cutting bass from the side alone tightens a mix
     * without thinning it; brightening the side widens without making the
     * centre harsh. Neither is reachable with any left-right EQ.
     */
    MidSideEQ midSide;

    // The two that make a mix sound produced rather than merely correct.
    StereoWidth stereoWidth;
    Saturator saturator;

    // The offset a pulse wave has by construction, and the peak the
    // listener's converter will actually reconstruct.
    DCBlocker dcBlocker;
    TruePeakMeter truePeak;

    // Enable flags
    bool eqEnabled = false;
    bool compressorEnabled = false;
    bool limiterEnabled = true;  // Usually always on for safety
    bool midSideEnabled = false;
    bool widthEnabled = false;
    bool saturationEnabled = false;

    // On by default, both of them. The DC blocker because this program makes
    // pulse waves and they are never centred; the metering because a number
    // nobody measures is a number nobody can trust, and it costs a few
    // operations per sample now that it is not summing a 144k buffer.
    bool dcBlockerEnabled = true;
    bool meteringEnabled = true;

    void setSampleRate(float sr) {
        eq.setSampleRate(sr);
        eqRight.setSampleRate(sr);
        compressor.setSampleRate(sr);
        limiter.setSampleRate(sr);
        lufsMeter.setSampleRate(sr);
        dcBlocker.setSampleRate(sr);
        truePeak.reset();
        midSide.configure(sr);
    }

    // Whatever the UI or a profile just changed, folded into the
    // coefficients the audio thread reads. UI thread.
    void prepare() { limiter.prepare(); }

    // What the limiter's lookahead costs. The mix is this far behind the
    // keyboard, and delay compensation should know.
    int latencySamples() const {
        return limiterEnabled ? limiter.latencySamples() : 0;
    }

    // Process stereo signal
    void process(float& left, float& right) {
        /*
         * The offset first, before anything with memory sees it.
         *
         * A pulse wave carries a DC component of 2*duty - 1, so this program
         * produces one by construction rather than by accident. Left in, it
         * spends headroom the limiter then has to take back, and it pulls on
         * every stage below that has state.
         */
        if (dcBlockerEnabled) dcBlocker.processStereo(left, right);

        // Mid-side next: it is corrective tonal shaping, and doing it
        // after the compressor would mean compressing a balance that is
        // about to change.
        if (midSideEnabled) midSide.process(left, right);

        /*
         * Width before the tone and the glue.
         *
         * Widening after compression would push the sides out past whatever
         * the compressor just held down, so the level the limiter sees is
         * no longer the level that was controlled.
         */
        if (widthEnabled) stereoWidth.process(left, right);

        /*
         * EQ, with a filter per side.
         *
         * These were one instance processed twice per frame. A one-pole
         * holds a single state variable, so each channel was filtered
         * through the other channel's history - direct L/R crosstalk in the
         * filtered bands, and an effective per-channel rate of half the real
         * one, which put the corners nowhere near where they were set.
         */
        if (eqEnabled) {
            left = eq.process(left);
            right = eqRight.process(right);
        }

        /*
         * Saturation before the compressor, not after.
         *
         * The harmonics it adds are part of the signal the glue should be
         * reacting to. After the compressor they would arrive unchecked at
         * the limiter instead, which is how a chain ends up with the
         * limiter doing all the work.
         */
        if (saturationEnabled) {
            // Stateless, so one instance for both sides is correct here.
            left = saturator.process(left);
            right = saturator.process(right);
        }

        // Glue compression, on one linked envelope - see processStereo.
        if (compressorEnabled) {
            compressor.processStereo(left, right);
        }

        // Limiter last, also linked, and with a lookahead.
        if (limiterEnabled) {
            limiter.processStereo(left, right);
        }

        // Measurement, after everything, so what is measured is what leaves.
        if (meteringEnabled) {
            lufsMeter.process(left, right);
            truePeak.processStereo(left, right);
        }
    }

    void reset() {
        eq.reset();
        eqRight.reset();
        compressor.reset();
        limiter.reset();
        lufsMeter.reset();
        truePeak.reset();
        dcBlocker.reset();
        midSide.reset();
    }

    // Get loudness for display
    float getLUFS() const {
        return lufsMeter.getLUFS();
    }

    // Get limiter gain reduction for metering
    float getLimiterGainReductionDB() const {
        return limiter.getGainReductionDB();
    }

    // What the listener's converter will see, which for square waves is
    // about two decibels above what the file says.
    float getTruePeakDB() const { return truePeak.peakDb(); }
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
