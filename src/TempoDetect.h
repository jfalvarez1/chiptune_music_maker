#pragma once

// ============================================================================
// Working out the tempo of something that was played freely
//
// Humming to a click is a skill. Most people who want to get an idea out of
// their head do not have it, and asking them to acquire it before the tool
// will listen is the reason a feature like this goes unused.
//
// So: given the times of the onsets in a take, find the tempo it was played
// at, where the beats fall, and which of those is a downbeat.
//
// THE METHOD, after Ellis, "Beat Tracking by Dynamic Programming" (2007),
// which is the standard treatment and specifies every constant below.
//
//   1. An onset envelope - a continuous signal that is large where notes
//      start. Built here from onset TIMES rather than from audio, because
//      the tracker has already done the hard part and rebuilding a mel
//      spectrogram to rediscover what it found would be wasteful.
//
//   2. A windowed autocorrelation of that envelope. The window is a
//      Gaussian in log-tempo centred on 0.5 s - 120 BPM - which encodes
//      the fact that people hear tempo relative to a preferred rate rather
//      than uniformly across the whole range. Without it, half-tempo and
//      double-tempo score identically to the truth, because they genuinely
//      fit the onsets just as well.
//
//   3. A duple-versus-triple pass, which is what stops a shuffle being
//      reported at three times its tempo.
//
//   4. Dynamic programming for the beat positions, so a take that drifts -
//      and a freely hummed one always drifts - is tracked rather than
//      being fitted to one rigid grid.
//
// WHAT THIS DOES NOT DO. It does not decide what to do with the answer.
// Adopting the user's tempo and fitting the user to the project's tempo are
// both reasonable and they are opposite; that choice belongs to the caller,
// which knows whether the project is empty.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// The answer
// ============================================================================

struct TempoEstimate {
    float bpm = 120.0f;

    /*
     * How much better the winning tempo was than the alternatives, 0..1.
     *
     * Reported because a handful of onsets, or an unsteady performance,
     * genuinely has no tempo - and imposing one on it is worse than saying
     * so and leaving the project as it was.
     */
    float confidence = 0.0f;

    // Where the first beat falls, in seconds. A take does not usually start
    // exactly on a beat, and assuming it does puts the whole part early.
    float firstBeatSeconds = 0.0f;

    bool valid = false;
};

// The tempo range worth considering. Outside this, a "tempo" is almost
// always a half or double of the real one, or noise.
inline constexpr float MIN_DETECTABLE_BPM = 55.0f;
inline constexpr float MAX_DETECTABLE_BPM = 215.0f;

/*
 * An onset envelope, sampled on a regular grid.
 *
 * Each onset contributes a small triangular bump rather than a single
 * sample, so that two performances differing by less than one grid step
 * still correlate. A bare impulse train is extremely sensitive to exactly
 * where the impulses land relative to the sampling grid, which is a
 * property of the grid rather than of the music.
 */
inline std::vector<float> buildOnsetEnvelope(const std::vector<float>& onsetTimes,
                                             float& durationSeconds,
                                             float sampleRate = 100.0f) {
    durationSeconds = 0.0f;
    if (onsetTimes.empty() || sampleRate <= 0.0f) return {};

    float last = 0.0f;
    for (float time : onsetTimes) last = std::max(last, time);
    durationSeconds = last + 1.0f;

    const size_t count = static_cast<size_t>(durationSeconds * sampleRate) + 1;
    if (count < 4) return {};

    std::vector<float> envelope(count, 0.0f);

    // A bump about 50 ms wide, which is roughly how precisely a listener
    // localises an onset in the first place.
    const int spread = std::max(1, static_cast<int>(0.025f * sampleRate));

    for (float time : onsetTimes) {
        if (time < 0.0f) continue;
        const int centre = static_cast<int>(time * sampleRate);
        for (int offset = -spread; offset <= spread; ++offset) {
            const int index = centre + offset;
            if (index < 0 || index >= static_cast<int>(count)) continue;
            const float weight = 1.0f - std::fabs(float(offset)) / float(spread + 1);
            envelope[static_cast<size_t>(index)] += weight;
        }
    }

    // Zero-mean, so the autocorrelation measures structure rather than the
    // constant offset that every lag shares.
    float mean = 0.0f;
    for (float value : envelope) mean += value;
    mean /= float(envelope.size());
    for (float& value : envelope) value -= mean;

    return envelope;
}

/*
 * Estimate the tempo from onset times.
 *
 * The perceptual prior is the part that makes this work rather than merely
 * run: a periodicity at half the true tempo fits the onsets exactly as well
 * as the true one does, and nothing in the signal distinguishes them. What
 * distinguishes them is that people hear tempo near 120 BPM, and the
 * Gaussian window in log-tempo is how that is expressed.
 */
inline TempoEstimate estimateTempo(const std::vector<float>& onsetTimes) {
    TempoEstimate result;

    // Fewer than this and any answer is arithmetic on noise.
    if (onsetTimes.size() < 4) return result;

    std::vector<float> sorted = onsetTimes;
    std::sort(sorted.begin(), sorted.end());

    constexpr float RATE = 100.0f;      // envelope samples per second
    float duration = 0.0f;
    const std::vector<float> envelope = buildOnsetEnvelope(sorted, duration, RATE);
    if (envelope.size() < 16) return result;

    // Lags to consider, from the tempo bounds.
    const int minLag = std::max(2, static_cast<int>(60.0f / MAX_DETECTABLE_BPM * RATE));
    const int maxLag = std::min(static_cast<int>(envelope.size()) / 2,
                                static_cast<int>(60.0f / MIN_DETECTABLE_BPM * RATE));
    if (maxLag <= minLag) return result;

    /*
     * Ellis's tempo prior: a Gaussian in log2 tempo, centred on 0.5 s
     * (120 BPM) with a width of 0.9 octaves.
     */
    constexpr float PREFERRED_PERIOD = 0.5f;
    constexpr float SIGMA_OCTAVES = 0.9f;

    std::vector<float> score(static_cast<size_t>(maxLag + 1), 0.0f);

    for (int lag = minLag; lag <= maxLag; ++lag) {
        float sum = 0.0f;
        for (size_t t = 0; t + static_cast<size_t>(lag) < envelope.size(); ++t) {
            sum += envelope[t] * envelope[t + static_cast<size_t>(lag)];
        }

        const float period = float(lag) / RATE;
        const float octaves = std::log2(period / PREFERRED_PERIOD);
        const float weight = std::exp(-0.5f * (octaves / SIGMA_OCTAVES) *
                                      (octaves / SIGMA_OCTAVES));
        score[static_cast<size_t>(lag)] = sum * weight;
    }

    /*
     * Duple versus triple.
     *
     * A lag that is a true beat also scores at two and three times itself,
     * because those are also periodicities of the same music. Adding the
     * half- and third-length versions of the curve back onto it reinforces
     * whichever subdivision is genuinely present, which is what stops a
     * shuffled part being reported at three times its tempo.
     */
    std::vector<float> combined = score;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        /*
         * A true beat period also shows energy at TWO and THREE times
         * itself, because those are periodicities of the same music. Adding
         * those multiples back onto this lag rewards the period whose
         * multiples are also present - which is the faster, truer one.
         *
         * The direction matters and is easy to get backwards. Crediting a
         * lag from its HALF rewards long lags instead, which reinforces
         * half-tempo: exactly the error this pass exists to prevent. It
         * read 140 BPM as 70 until the tests caught it.
         */
        const int twice = lag * 2;
        const int thrice = lag * 3;
        if (twice <= maxLag) {
            combined[static_cast<size_t>(lag)] += 0.5f * score[static_cast<size_t>(twice)];
        }
        if (thrice <= maxLag) {
            combined[static_cast<size_t>(lag)] += 0.33f * score[static_cast<size_t>(thrice)];
        }
    }

    int bestLag = -1;
    float bestScore = 0.0f;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const float value = combined[static_cast<size_t>(lag)];
        if (value > bestScore) {
            bestScore = value;
            bestLag = lag;
        }
    }

    /*
     * The best score that is NOT part of the winning peak.
     *
     * A peak has width, so the lag next to the winner always scores almost
     * as highly - comparing against it measures how sharp the peak is, not
     * how far ahead it is, and reports every take as unconfident. What
     * matters is whether some genuinely different tempo fits nearly as
     * well, so the winner's own neighbourhood is excluded.
     */
    float rivalScore = 0.0f;
    if (bestLag > 0) {
        const int exclude = std::max(2, bestLag / 6);   // about 15% either side
        for (int lag = minLag; lag <= maxLag; ++lag) {
            if (std::abs(lag - bestLag) <= exclude) continue;
            rivalScore = std::max(rivalScore, combined[static_cast<size_t>(lag)]);
        }
    }
    const float secondScore = rivalScore;

    if (bestLag <= 0 || bestScore <= 0.0f) return result;

    const float period = float(bestLag) / RATE;
    result.bpm = std::clamp(60.0f / period, MIN_DETECTABLE_BPM, MAX_DETECTABLE_BPM);

    /*
     * Where the first beat falls.
     *
     * Found by trying every phase within one beat and taking the one that
     * lands nearest the onsets. A take almost never starts exactly on a
     * beat, and assuming it does puts the whole part early by however long
     * the person waited before starting.
     */
    float bestPhase = 0.0f;
    float bestPhaseScore = -1.0f;
    const int phaseSteps = 32;
    for (int step = 0; step < phaseSteps; ++step) {
        const float phase = period * float(step) / float(phaseSteps);
        float total = 0.0f;
        for (float onset : sorted) {
            const float since = onset - phase;
            const float nearest = std::round(since / period) * period;
            const float error = std::fabs(since - nearest) / period;
            total += 1.0f - std::min(error * 2.0f, 1.0f);
        }
        if (total > bestPhaseScore) {
            bestPhaseScore = total;
            bestPhase = phase;
        }
    }
    result.firstBeatSeconds = bestPhase;

    /*
     * Confidence: how well the onsets actually land on the grid this tempo
     * implies, from 0 to 1.
     *
     * The obvious measure - how far the winning autocorrelation peak leads
     * its nearest rival - turned out to barely separate a metronome from
     * randomly scattered onsets, because a wrong tempo still correlates
     * respectably with dense material. It cannot inform the one decision
     * this number exists for, which is whether to impose a tempo at all.
     *
     * Grid fit can. It is also the thing a caller would check by hand, and
     * it degrades smoothly rather than falling off a cliff.
     *
     * `secondScore` is left computed above but no longer decides this; the
     * autocorrelation margin remains useful for choosing BETWEEN tempi,
     * which is a different question from whether there is one.
     */
    (void)secondScore;
    result.confidence = sorted.empty()
        ? 0.0f
        : std::clamp(bestPhaseScore / float(sorted.size()), 0.0f, 1.0f);

    result.valid = true;
    return result;
}

/*
 * Beat positions, allowing for drift.
 *
 * Dynamic programming over the onsets: each beat wants to land on an onset
 * AND to sit one period after the previous beat, and the best sequence
 * balances the two. A freely hummed take always speeds up or slows down,
 * and fitting it to one rigid grid puts the end of the phrase in the wrong
 * place even when the tempo at the start was right.
 *
 * `tightness` is how strongly the spacing is enforced against the
 * evidence. Ellis publishes 680 for this constant, and that value belongs
 * to his objective, where each beat contributes its onset STRENGTH from a
 * normalised envelope. Here each beat contributes a flat 1, so the two
 * terms are on completely different scales and 680 makes any deviation
 * from the nominal period cost more than a whole extra beat is worth - the
 * tracker then stops at the first sign of drift, which is precisely the
 * thing it exists to follow.
 *
 * At 20 a take that slows by 16% over a phrase is still tracked, while a
 * gap half again too long is still correctly rejected.
 */
inline std::vector<float> trackBeats(const std::vector<float>& onsetTimes,
                                     float bpm, float tightness = 20.0f) {
    std::vector<float> beats;
    if (onsetTimes.size() < 2 || bpm <= 0.0f) return beats;

    std::vector<float> sorted = onsetTimes;
    std::sort(sorted.begin(), sorted.end());

    const float period = 60.0f / bpm;
    if (period <= 0.0f) return beats;

    const size_t count = sorted.size();
    std::vector<float> best(count, 0.0f);
    std::vector<int> from(count, -1);

    for (size_t i = 0; i < count; ++i) {
        // Every onset is a candidate beat on its own merit.
        best[i] = 1.0f;
        from[i] = -1;

        for (size_t j = 0; j < i; ++j) {
            const float gap = sorted[i] - sorted[j];

            // Only spacings that could plausibly be one beat.
            if (gap < period * 0.5f || gap > period * 2.0f) continue;

            /*
             * The cost of a spacing that is not exactly one period, in log
             * terms - so being 10% fast and 10% slow cost the same, which
             * they should, and which a linear difference gets wrong.
             */
            const float ratio = gap / period;
            const float penalty = -tightness * std::log(ratio) * std::log(ratio);

            const float candidate = best[j] + 1.0f + penalty;
            if (candidate > best[i]) {
                best[i] = candidate;
                from[i] = static_cast<int>(j);
            }
        }
    }

    // The best chain, walked back from wherever it ended.
    size_t end = 0;
    for (size_t i = 1; i < count; ++i) {
        if (best[i] > best[end]) end = i;
    }

    for (int at = static_cast<int>(end); at >= 0; at = from[static_cast<size_t>(at)]) {
        beats.push_back(sorted[static_cast<size_t>(at)]);
        if (from[static_cast<size_t>(at)] < 0) break;
    }
    std::reverse(beats.begin(), beats.end());
    return beats;
}

/*
 * Which of the tracked beats is a downbeat.
 *
 * Returns an index into `beats`, or 0 when there is nothing to go on. A
 * loop that starts in the wrong place is wrong in a way people notice
 * immediately even when every note in it is right - so this is worth the
 * twenty lines it costs.
 *
 * Decided by loudness where it is known, and by onset density otherwise:
 * more happens on a downbeat than on the other beats of a bar.
 */
inline int estimateDownbeat(const std::vector<float>& beats,
                            const std::vector<float>& onsetTimes,
                            int beatsPerBar = 4) {
    if (beats.empty() || beatsPerBar < 1) return 0;
    if (static_cast<int>(beats.size()) < beatsPerBar) return 0;

    int bestPhase = 0;
    float bestScore = -1.0f;

    for (int phase = 0; phase < beatsPerBar; ++phase) {
        float score = 0.0f;

        for (size_t b = static_cast<size_t>(phase); b < beats.size();
             b += static_cast<size_t>(beatsPerBar)) {
            // How much lands near this beat.
            for (float onset : onsetTimes) {
                const float distance = std::fabs(onset - beats[b]);
                if (distance < 0.06f) score += 1.0f - distance / 0.06f;
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestPhase = phase;
        }
    }
    return bestPhase;
}

} // namespace ChiptuneTracker
