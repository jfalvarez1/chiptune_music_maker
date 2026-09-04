#pragma once

/*
 * ChiptuneTracker - Pitch shifting, formant shifting and autotune
 *
 * A phase vocoder. Windowed FFT frames go in, each bin's TRUE frequency is
 * recovered from how its phase moved between frames, the bins are moved to
 * new positions, and the result is resynthesised by accumulating phase and
 * overlap-adding.
 *
 * The part everyone gets wrong is the phase, so it is worth stating plainly.
 *
 * An FFT bin does not sit at exactly its centre frequency. A 440 Hz tone in
 * a 44.1 kHz, 1024-point analysis lands between bins 10 and 11, and the
 * energy in bin 10 is really at 440 Hz, not at bin 10's 430.7 Hz. What tells
 * you the difference is how far the phase advanced since the previous frame:
 * expected advance is (bin centre) * (hop / rate) * 2pi, and the excess -
 * wrapped into +/-pi - is the deviation. Recovering that per bin is the
 * whole reason a phase vocoder sounds like the note rather than like a
 * comb filter, and skipping it is why naive implementations produce that
 * metallic, phasey artefact people call "the vocoder sound".
 *
 * On resynthesis the phase must be ACCUMULATED, not copied. Each output bin
 * carries a running phase advanced by its shifted true frequency every hop.
 * Copying the analysis phase across leaves adjacent frames incoherent and
 * the overlap-add cancels unpredictably.
 *
 * The window is applied twice - once on analysis, once on synthesis - and
 * with a Hann window at 4x overlap that sums to a constant 1.5, which is
 * divided out. Getting that constant wrong is a gain error that changes with
 * the overlap, which is a maddening thing to chase.
 *
 * LATENCY. One window, reported through IEffect::latencySamples(). This is
 * not free and it is not hideable: you cannot know a bin's true frequency
 * without seeing two frames of it. At 1024 samples and 44.1 kHz that is
 * 23 ms, which is why these are labelled as studio effects rather than
 * things to monitor through while playing.
 *
 * NOT CHIP-AUTHENTIC. A 2A03 could not do any of this. Per the ground rules,
 * every one of these ships off by default.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "FFT.h"

namespace ChiptuneTracker {

// ============================================================================
// The shared phase-vocoder frame machinery
// ============================================================================
/*
 * Both the pitch shifter and the formant shifter need the same thing: run a
 * windowed FFT at a fixed hop, let a subclass rearrange the spectrum, and
 * overlap-add the result. The difference between them is only what they do
 * to the bins, so that is the only part left virtual.
 *
 * Everything is preallocated. process() runs in the audio thread and does no
 * allocation, no locks and no I/O.
 */
class PhaseVocoder {
public:
    // 1024 at 44.1 kHz is 23 ms, which resolves down to about 43 Hz per bin -
    // enough to separate the partials of a bass note. Shorter smears pitch;
    // longer smears transients and costs more latency.
    static constexpr int WINDOW = 1024;
    static constexpr int HOP = WINDOW / 4;        // 75% overlap
    static constexpr int BINS = WINDOW / 2 + 1;

    virtual ~PhaseVocoder() = default;

    void configure(float sampleRate) {
        m_sampleRate = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
        if (m_window.size() != WINDOW) {
            m_plan.resize(WINDOW);
            m_window.resize(WINDOW);
            for (int i = 0; i < WINDOW; ++i) {
                m_window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
                    6.28318530718f * static_cast<float>(i) /
                    static_cast<float>(WINDOW - 1)));
            }
            m_real.assign(WINDOW, 0.0f);
            m_imag.assign(WINDOW, 0.0f);
            m_magnitude.assign(BINS, 0.0f);
            m_frequency.assign(BINS, 0.0f);
            m_outMagnitude.assign(BINS, 0.0f);
            m_outFrequency.assign(BINS, 0.0f);
            m_lastPhase.assign(BINS, 0.0f);
            m_sumPhase.assign(BINS, 0.0f);
            m_input.assign(WINDOW, 0.0f);
            m_output.assign(WINDOW * 2, 0.0f);
        }
        reset();
    }

    void reset() {
        std::fill(m_lastPhase.begin(), m_lastPhase.end(), 0.0f);
        std::fill(m_sumPhase.begin(), m_sumPhase.end(), 0.0f);
        std::fill(m_input.begin(), m_input.end(), 0.0f);
        std::fill(m_output.begin(), m_output.end(), 0.0f);
        m_filled = 0;
        m_outputRead = 0;
        m_outputWrite = 0;
        m_primed = 0;
    }

    int latency() const { return WINDOW; }

    /*
     * One sample in, one sample out, delayed by a window.
     *
     * Samples accumulate until a hop's worth is ready; then a frame is
     * analysed, transformed and overlap-added into the output ring, and the
     * ring is read from a window behind the write head.
     */
    float process(float input) {
        if (m_window.size() != WINDOW) return input;

        m_input[static_cast<size_t>(m_filled++)] = input;

        if (m_filled >= WINDOW) {
            analyseFrame();
            // Slide by one hop and keep the overlap.
            std::copy(m_input.begin() + HOP, m_input.end(), m_input.begin());
            m_filled = WINDOW - HOP;
        }

        /*
         * Silence until a full window has been through, rather than the
         * partial overlap-add that would otherwise ramp in.
         *
         * The read head does not move during priming, and that is the whole
         * point. It used to advance from the first sample, so it ran AHEAD
         * of the overlap-add: index i was read at time i, while the frame
         * that completes index i is not added until time WINDOW-1+i rounded
         * down to a hop. Every read landed on a slot nothing had written
         * yet, and the output was whatever the ring held from its previous
         * lap - which is why the effect worked at all, and why it worked one
         * whole extra ring lap late.
         *
         * Measured rather than reasoned about: correlating a signal against
         * the output gave a group delay of 2048 samples, exactly twice the
         * window the effect reported. Every vocoder effect - pitch shift,
         * formant shift, autotune - was 46 ms late and told the mixer it was
         * 23 ms late. Holding the read head back until priming ends makes
         * the delay a genuine WINDOW, which is both what latency() claims
         * and half of what it used to cost.
         */
        if (m_primed < WINDOW) { ++m_primed; return 0.0f; }

        const size_t size = m_output.size();
        const size_t readAt = m_outputRead % size;
        const float out = m_output[readAt];
        m_output[readAt] = 0.0f;      // consumed; ready to be added into again
        ++m_outputRead;

        return out;
    }

protected:
    /*
     * Rearrange the spectrum.
     *
     * `magnitude` and `frequency` are the analysed bins - frequency in Hz,
     * recovered from the phase advance. Write into `outMagnitude` and
     * `outFrequency`, both already cleared.
     */
    virtual void transformSpectrum(const std::vector<float>& magnitude,
                                   const std::vector<float>& frequency,
                                   std::vector<float>& outMagnitude,
                                   std::vector<float>& outFrequency) = 0;

    float sampleRate() const { return m_sampleRate; }
    static float binWidth(float sampleRate) {
        return sampleRate / static_cast<float>(WINDOW);
    }

private:
    // Wrap into -pi..pi. The phase deviation is only meaningful once folded
    // into that range; without it every bin reads as wildly detuned.
    static float wrapPhase(float phase) {
        constexpr float PI = 3.14159265358979323846f;
        constexpr float TWO_PI = 6.28318530717958647692f;
        phase = std::fmod(phase + PI, TWO_PI);
        if (phase < 0.0f) phase += TWO_PI;
        return phase - PI;
    }

    void analyseFrame() {
        constexpr float TWO_PI = 6.28318530717958647692f;
        const float expectedAdvance = TWO_PI * static_cast<float>(HOP) /
                                      static_cast<float>(WINDOW);
        const float freqPerBin = m_sampleRate / static_cast<float>(WINDOW);

        for (int i = 0; i < WINDOW; ++i) {
            m_real[static_cast<size_t>(i)] =
                m_input[static_cast<size_t>(i)] * m_window[static_cast<size_t>(i)];
            m_imag[static_cast<size_t>(i)] = 0.0f;
        }
        m_plan.transform(m_real.data(), m_imag.data());

        for (int bin = 0; bin < BINS; ++bin) {
            const size_t b = static_cast<size_t>(bin);
            const float re = m_real[b];
            const float im = m_imag[b];

            m_magnitude[b] = 2.0f * std::sqrt(re * re + im * im);
            const float phase = std::atan2(im, re);

            /*
             * The bin's true frequency.
             *
             * How far the phase advanced, minus how far a signal exactly at
             * the bin centre would have advanced, wrapped into +/-pi, gives
             * the deviation. Without this every bin is assumed to sit at its
             * centre frequency, and the result is the metallic phasey sound
             * that gives naive pitch shifters away.
             */
            float delta = phase - m_lastPhase[b];
            m_lastPhase[b] = phase;

            delta -= static_cast<float>(bin) * expectedAdvance;
            delta = wrapPhase(delta);

            const float deviation = delta * static_cast<float>(WINDOW) /
                                    (static_cast<float>(HOP) * TWO_PI);
            m_frequency[b] = (static_cast<float>(bin) + deviation) * freqPerBin;
        }

        std::fill(m_outMagnitude.begin(), m_outMagnitude.end(), 0.0f);
        std::fill(m_outFrequency.begin(), m_outFrequency.end(), 0.0f);
        transformSpectrum(m_magnitude, m_frequency, m_outMagnitude, m_outFrequency);

        // ---- Resynthesis --------------------------------------------------
        for (int bin = 0; bin < BINS; ++bin) {
            const size_t b = static_cast<size_t>(bin);

            // Phase is ACCUMULATED from the bin's assigned frequency, not
            // copied from the analysis. Copying leaves consecutive frames
            // incoherent and the overlap-add cancels unpredictably.
            const float deviation = m_outFrequency[b] / freqPerBin -
                                    static_cast<float>(bin);
            const float advance = static_cast<float>(bin) * expectedAdvance +
                                  deviation * static_cast<float>(HOP) * TWO_PI /
                                  static_cast<float>(WINDOW);
            m_sumPhase[b] = wrapPhase(m_sumPhase[b] + advance);

            const float magnitude = m_outMagnitude[b] * 0.5f;
            m_real[b] = magnitude * std::cos(m_sumPhase[b]);
            m_imag[b] = magnitude * std::sin(m_sumPhase[b]);
        }

        // Hermitian symmetry, so the inverse transform is real.
        for (int bin = BINS; bin < WINDOW; ++bin) {
            const size_t mirror = static_cast<size_t>(WINDOW - bin);
            m_real[static_cast<size_t>(bin)] = m_real[mirror];
            m_imag[static_cast<size_t>(bin)] = -m_imag[mirror];
        }

        // Inverse by conjugation: conj, forward, conj, scale.
        for (int i = 0; i < WINDOW; ++i) m_imag[static_cast<size_t>(i)] *= -1.0f;
        m_plan.transform(m_real.data(), m_imag.data());

        /*
         * Window again on the way out, and divide by the overlap sum.
         *
         * A Hann window applied twice at 75% overlap sums to a constant 1.5.
         * Getting that constant wrong is a gain error that changes whenever
         * the overlap does, which is an unpleasant thing to track down.
         */
        constexpr float OVERLAP_SUM = 1.5f;
        const float scale = 1.0f / (static_cast<float>(WINDOW) * OVERLAP_SUM);
        const size_t size = m_output.size();

        for (int i = 0; i < WINDOW; ++i) {
            const size_t at = (m_outputWrite + static_cast<size_t>(i)) % size;
            m_output[at] += m_real[static_cast<size_t>(i)] *
                            m_window[static_cast<size_t>(i)] * scale;
        }
        m_outputWrite += HOP;
    }

    DSP::FFTPlan m_plan;
    std::vector<float> m_window;
    std::vector<float> m_real, m_imag;
    std::vector<float> m_magnitude, m_frequency;
    std::vector<float> m_outMagnitude, m_outFrequency;
    std::vector<float> m_lastPhase, m_sumPhase;
    std::vector<float> m_input, m_output;

    float m_sampleRate = 44100.0f;
    int m_filled = 0;
    size_t m_outputRead = 0;
    size_t m_outputWrite = 0;
    int m_primed = 0;
};

// ============================================================================
// Pitch shifter
// ============================================================================
class PitchShifter : public PhaseVocoder {
public:
    // Semitones. +/-24 is two octaves either way, past which a phase vocoder
    // is producing an effect rather than a transposition.
    float semitones = 0.0f;
    float mix = 1.0f;

    float ratio() const { return std::pow(2.0f, semitones / 12.0f); }

protected:
    void transformSpectrum(const std::vector<float>& magnitude,
                           const std::vector<float>& frequency,
                           std::vector<float>& outMagnitude,
                           std::vector<float>& outFrequency) override {
        const float shift = ratio();

        /*
         * Move each bin to bin * ratio and scale its frequency to match.
         *
         * Bins land between output bins, so several can map to the same
         * target; their magnitudes ADD and the loudest one's frequency wins,
         * which is what stops two partials averaging into a frequency
         * neither of them had.
         */
        for (int bin = 0; bin < BINS; ++bin) {
            const size_t b = static_cast<size_t>(bin);
            const int target = static_cast<int>(static_cast<float>(bin) * shift + 0.5f);
            if (target < 0 || target >= BINS) continue;

            const size_t t = static_cast<size_t>(target);
            if (magnitude[b] > outMagnitude[t]) {
                outFrequency[t] = frequency[b] * shift;
            }
            outMagnitude[t] += magnitude[b];
        }
    }
};

// ============================================================================
// Formant shifter
// ============================================================================
/*
 * Moves the spectral ENVELOPE without moving the partials.
 *
 * Pitch and formants are separable: the partials say what note is being
 * sung, and the envelope over them - the resonances of the throat and mouth
 * - says what size the singer is. A pitch shifter moves both, which is why
 * shifting a voice up an octave produces a chipmunk rather than a soprano.
 * Moving the envelope alone changes the apparent size of the singer at
 * constant pitch, and the two together let you transpose a voice and keep it
 * sounding like the same person.
 *
 * The envelope is estimated by smoothing the magnitude spectrum over a wide
 * span of bins. That is cruder than the cepstral liftering a dedicated
 * vocoder would use, and it is enough here: what matters is that the
 * estimate is smooth compared to the partial spacing, so dividing it out
 * leaves the partials and multiplying a shifted one back moves the
 * resonances without touching them.
 */
class FormantShifter : public PhaseVocoder {
public:
    float semitones = 0.0f;
    float mix = 1.0f;

protected:
    void transformSpectrum(const std::vector<float>& magnitude,
                           const std::vector<float>& frequency,
                           std::vector<float>& outMagnitude,
                           std::vector<float>& outFrequency) override {
        const float shift = std::pow(2.0f, semitones / 12.0f);

        if (m_envelope.size() != magnitude.size()) {
            m_envelope.assign(magnitude.size(), 0.0f);
        }

        // A wide moving average. The span has to be broad compared with the
        // spacing between partials or the "envelope" follows the partials
        // themselves and dividing it out removes the very thing it should
        // be leaving alone.
        constexpr int SPAN = 12;
        for (int bin = 0; bin < BINS; ++bin) {
            float sum = 0.0f;
            int count = 0;
            for (int k = bin - SPAN; k <= bin + SPAN; ++k) {
                if (k < 0 || k >= BINS) continue;
                sum += magnitude[static_cast<size_t>(k)];
                ++count;
            }
            m_envelope[static_cast<size_t>(bin)] =
                (count > 0) ? sum / static_cast<float>(count) : 0.0f;
        }

        for (int bin = 0; bin < BINS; ++bin) {
            const size_t b = static_cast<size_t>(bin);

            // Where this bin's envelope value should be read from, so the
            // envelope moves and the partials do not.
            const int source = static_cast<int>(static_cast<float>(bin) / shift + 0.5f);
            const float shiftedEnvelope =
                (source >= 0 && source < BINS)
                    ? m_envelope[static_cast<size_t>(source)] : 0.0f;

            const float here = m_envelope[b];
            // The guard is not decoration: quiet bins have near-zero
            // envelopes, and dividing by one amplifies noise into a screech.
            const float gain = (here > 1e-6f) ? (shiftedEnvelope / here) : 0.0f;

            outMagnitude[b] = magnitude[b] * std::min(gain, 8.0f);
            outFrequency[b] = frequency[b];   // partials stay exactly put
        }
    }

private:
    std::vector<float> m_envelope;
};

// ============================================================================
// Autotune
// ============================================================================
/*
 * Detect the pitch, decide which scale note it should be, and shift it there.
 *
 * The retune SPEED is the whole character of the effect. Slow, it is a
 * corrective that leaves the performance intact - a singer's natural drift
 * into a note survives, and only the settled pitch is nudged. Instant, the
 * pitch snaps between scale degrees with no glide at all, and that
 * discontinuity is the sound the effect became famous for. Both are
 * legitimate, and the difference between them is one time constant.
 *
 * Pitch detection here is on the phase vocoder's own spectrum rather than a
 * separate YIN pass: the strongest low partial's true frequency is already
 * computed by the analysis, so this costs nothing extra. It is less robust
 * than YIN on a noisy signal, which is the right trade for something that
 * has to run inside an insert chain.
 */
class AutoTune : public PhaseVocoder {
public:
    // Which of the twelve pitch classes are allowed, as a bitmask from C.
    // Chromatic by default, which corrects nothing but the wildest notes.
    uint16_t scaleMask = 0x0FFF;
    int rootNote = 0;              // 0 = C

    // 0 is no correction, 1 is instant. Between them it glides.
    float strength = 0.5f;

    // Below this the detector is looking at noise, and correcting noise
    // produces a warble on every breath and consonant.
    float minimumMagnitude = 0.02f;

    float mix = 1.0f;

    float detectedHz() const { return m_detected; }
    float correctionSemitones() const { return m_correction; }

protected:
    void transformSpectrum(const std::vector<float>& magnitude,
                           const std::vector<float>& frequency,
                           std::vector<float>& outMagnitude,
                           std::vector<float>& outFrequency) override {
        // ---- Find the fundamental ------------------------------------------
        float best = 0.0f;
        float bestHz = 0.0f;
        const float top = std::min(2000.0f, sampleRate() * 0.4f);

        for (int bin = 1; bin < BINS; ++bin) {
            const size_t b = static_cast<size_t>(bin);
            if (frequency[b] <= 40.0f || frequency[b] > top) continue;
            if (magnitude[b] > best) { best = magnitude[b]; bestHz = frequency[b]; }
        }

        float shift = 1.0f;
        if (best >= minimumMagnitude && bestHz > 0.0f) {
            m_detected = bestHz;

            const float midi = 69.0f + 12.0f * std::log2(bestHz / 440.0f);
            const float target = nearestAllowed(midi);
            const float error = target - midi;

            // Glide toward the correction rather than jumping to it. The
            // time constant is the effect's whole character.
            const float amount = std::clamp(strength, 0.0f, 1.0f);
            m_correction += (error * amount - m_correction) *
                            (0.15f + 0.85f * amount);
            shift = std::pow(2.0f, m_correction / 12.0f);
        } else {
            // Unvoiced: let the correction decay rather than freezing, so a
            // held bend does not snap back the instant a consonant passes.
            m_correction *= 0.995f;
            shift = std::pow(2.0f, m_correction / 12.0f);
        }

        for (int bin = 0; bin < BINS; ++bin) {
            const size_t b = static_cast<size_t>(bin);
            const int target = static_cast<int>(static_cast<float>(bin) * shift + 0.5f);
            if (target < 0 || target >= BINS) continue;
            const size_t t = static_cast<size_t>(target);
            if (magnitude[b] > outMagnitude[t]) {
                outFrequency[t] = frequency[b] * shift;
            }
            outMagnitude[t] += magnitude[b];
        }
    }

private:
    // The nearest MIDI note whose pitch class the scale allows. Searched
    // outward so a note exactly between two allowed degrees resolves down,
    // which is arbitrary but has to be decided rather than left to
    // floating-point luck.
    float nearestAllowed(float midi) const {
        const uint16_t mask = (scaleMask == 0u) ? 0x0FFFu : scaleMask;
        const int centre = static_cast<int>(std::lround(midi));

        for (int distance = 0; distance <= 12; ++distance) {
            for (int direction : {-1, 1}) {
                const int candidate = centre + distance * direction;
                if (candidate < 0 || candidate > 127) continue;
                int pitchClass = (candidate - rootNote) % 12;
                if (pitchClass < 0) pitchClass += 12;
                if (mask & (1u << pitchClass)) return static_cast<float>(candidate);
                if (distance == 0) break;   // both directions are the same note
            }
        }
        return midi;
    }

    float m_detected = 0.0f;
    float m_correction = 0.0f;
};

// ============================================================================
// Offline time stretch
// ============================================================================
/*
 * Deliberately NOT an insert effect.
 *
 * Time stretching changes how long audio lasts, and an insert effect gets
 * one sample per sample: there is nowhere for the extra time to go, and a
 * stretch of anything but 1.0 would either starve or grow without bound.
 * Every DAW that offers "time stretch" on a live channel is really offering
 * a delay-based approximation, which is a different effect.
 *
 * So this operates on a whole buffer, offline, which is where it is actually
 * wanted: fitting an imported break to the project tempo.
 *
 * The method is overlap-add resynthesis at a different hop - the same idea
 * as the phase vocoder above, without needing the phase bookkeeping, because
 * for a pure length change the grains can simply be crossfaded. Correlation
 * is not searched for; that would be WSOLA and better on percussion, and it
 * is a fair amount more code for a first version.
 */
inline std::vector<float> timeStretch(const std::vector<float>& input,
                                      float ratio) {
    if (input.size() < 4) return input;
    if (!std::isfinite(ratio)) return input;

    // Bounded: a ratio near zero produces an output longer than any
    // reasonable memory, and this is called from the UI thread on a buffer
    // the user chose.
    ratio = std::clamp(ratio, 0.25f, 4.0f);
    if (std::fabs(ratio - 1.0f) < 1e-4f) return input;

    constexpr int GRAIN = 2048;
    constexpr int OVERLAP = GRAIN / 2;
    const int analysisHop = OVERLAP;
    const int synthesisHop = std::max(1, static_cast<int>(
        static_cast<float>(analysisHop) * ratio));

    const size_t outputSize = static_cast<size_t>(
        static_cast<float>(input.size()) * ratio) + GRAIN;
    std::vector<float> output(outputSize, 0.0f);
    std::vector<float> weight(outputSize, 0.0f);

    std::vector<float> window(GRAIN);
    for (int i = 0; i < GRAIN; ++i) {
        window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            6.28318530718f * static_cast<float>(i) / static_cast<float>(GRAIN - 1)));
    }

    size_t readAt = 0;
    size_t writeAt = 0;
    while (readAt + GRAIN < input.size() && writeAt + GRAIN < outputSize) {
        for (int i = 0; i < GRAIN; ++i) {
            const float w = window[static_cast<size_t>(i)];
            output[writeAt + static_cast<size_t>(i)] +=
                input[readAt + static_cast<size_t>(i)] * w;
            weight[writeAt + static_cast<size_t>(i)] += w;
        }
        readAt += static_cast<size_t>(analysisHop);
        writeAt += static_cast<size_t>(synthesisHop);
    }

    // Divide by the accumulated window rather than by a constant. The
    // overlap is not an integer fraction at an arbitrary ratio, so the sum
    // varies along the buffer - and assuming a constant leaves an audible
    // ripple at the grain rate.
    for (size_t i = 0; i < output.size(); ++i) {
        if (weight[i] > 1e-4f) output[i] /= weight[i];
    }

    output.resize(std::min(output.size(),
                           static_cast<size_t>(static_cast<float>(input.size()) * ratio) + 1));
    return output;
}

} // namespace ChiptuneTracker
