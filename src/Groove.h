#pragma once

// ============================================================================
// Quantising a performance without flattening it
//
// Snapping every note hard onto a grid is the fastest way to make a played
// part sound programmed. The push and pull of a real performance - a snare
// landing a few milliseconds late, a hi-hat run breathing - is most of what
// makes it sound like a person, and a 100% snap deletes all of it.
//
// Three things separate quantisation that helps from quantisation that
// ruins:
//
//   STRENGTH. Move the note a fraction of the way to the grid rather than
//   all of it. Somewhere around half tightens the timing while leaving the
//   feel intact, and it is a dial rather than a switch.
//
//   RANGE. A note far from the grid is usually deliberate - a flam, a grace
//   note, a drag, the front of a fill - and dragging it in is the failure
//   people notice most. Beyond a set distance, leave it alone.
//
//   THE RIGHT GRID. Quantising a triplet passage to sixteenths destroys it.
//   Rather than assuming a grid, measure which one the performance actually
//   fits and use that.
//
// All of it is non-destructive by construction: these are pure functions
// from the original times, so the strength dial can be moved back and forth
// and re-derived from the same take. Nothing here overwrites what was
// played - the caller keeps the raw hits and re-runs the conversion.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <vector>

#include "Snap.h"

namespace ChiptuneTracker {

// ============================================================================
// Partial quantisation
// ============================================================================

/*
 * Move `beat` toward `target` by `strength`, unless it is too far away.
 *
 *   strength 0    leave it exactly where it was played
 *   strength 1    put it on the grid
 *   strength 0.5  halve the error, which tightens without flattening
 *
 * `range` is a fraction of one grid step. A note further than that from the
 * grid is treated as deliberate and left alone entirely: at 0.5 everything
 * is in range, because the nearest grid point is never more than half a
 * step away, and lower values progressively protect the notes that were
 * furthest out.
 */
inline float quantizePartial(float beat, float target, float step,
                             float strength, float range) {
    if (!std::isfinite(beat)) return 0.0f;
    if (!std::isfinite(target) || step <= 0.0f) return beat;

    strength = std::clamp(strength, 0.0f, 1.0f);
    range = std::clamp(range, 0.0f, 1.0f);

    if (strength <= 0.0f) return beat;

    // Too far from the grid to have been aiming at it.
    const float error = target - beat;
    if (std::fabs(error) > range * step) return beat;

    return beat + error * strength;
}

// ============================================================================
// Which grid the performance is actually on
// ============================================================================

struct GridEstimate {
    SnapDivision division = SnapDivision::Sixteenth;
    float fit = 0.0f;        // mean error as a fraction of a step; lower is better
    bool confident = false;  // whether it beat the alternatives by enough to trust
};

/*
 * Score how well a set of onsets sits on one grid.
 *
 * The mean distance to the nearest grid point, divided by the step - the
 * normalisation is essential, because in absolute terms a finer grid always
 * fits better and an unnormalised score would pick 1/32 every time.
 *
 * A perfect fit is 0; randomly placed notes average 0.25.
 */
inline float gridFit(const std::vector<float>& beats, SnapDivision division,
                     int beatsPerMeasure) {
    const float step = snapStepBeats(division, beatsPerMeasure);
    if (step <= 0.0f || beats.empty()) return 1.0f;

    float total = 0.0f;
    for (float beat : beats) {
        const float nearest = std::floor(beat / step + 0.5f) * step;
        total += std::fabs(beat - nearest);
    }
    return (total / float(beats.size())) / step;
}

/*
 * Pick the grid a performance was played on.
 *
 * Straight divisions and triplets both, because the whole point is to catch
 * the triplet passage that a fixed sixteenth grid would destroy. A small
 * penalty per halving pushes ties toward the coarser, more musical answer -
 * without it a 1/32 grid wins every close call, and a part quantised to
 * 1/32 has effectively not been quantised at all.
 */
inline GridEstimate detectGrid(const std::vector<float>& beats,
                               int beatsPerMeasure = 4) {
    GridEstimate best;
    if (beats.size() < 3) return best;   // too little to tell anything from

    static const SnapDivision CANDIDATES[] = {
        SnapDivision::Quarter,
        SnapDivision::Eighth,
        SnapDivision::Sixteenth,
        SnapDivision::ThirtySecond,
        SnapDivision::TripletEighth,
        SnapDivision::TripletSixteenth,
    };

    float bestScore = 1e9f;
    float secondScore = 1e9f;

    for (SnapDivision division : CANDIDATES) {
        const float step = snapStepBeats(division, beatsPerMeasure);
        if (step <= 0.0f) continue;

        // The complexity penalty: finer grids must fit better by a margin,
        // not merely fit better.
        const float penalty = 0.02f * std::log2(1.0f / step);
        const float score = gridFit(beats, division, beatsPerMeasure) + penalty;

        if (score < bestScore) {
            secondScore = bestScore;
            bestScore = score;
            best.division = division;
            best.fit = gridFit(beats, division, beatsPerMeasure);
        } else if (score < secondScore) {
            secondScore = score;
        }
    }

    /*
     * Trusted only when it is both a good fit and a clear winner.
     *
     * A performance that fits everything equally badly has no grid, and
     * quantising it to whichever candidate scraped ahead is worse than
     * leaving it alone. The caller can then keep its own setting.
     */
    best.confident = (best.fit < 0.18f) && (secondScore - bestScore > 0.01f);
    return best;
}

// ============================================================================
// Swing
// ============================================================================

/*
 * Measure how far the offbeats are pushed, as a ratio.
 *
 *   0.5   straight - the offbeat sits exactly halfway
 *   0.667 triplet swing, the classic two-against-one
 *
 * Measured rather than imposed, because the real range is wide - published
 * jazz ratios run from 1:1 to 3:1, wider at slow tempos - and a hardcoded
 * 0.667 is wrong more often than it is right.
 *
 * Returns 0.5 when there is nothing to measure, which is the neutral answer
 * rather than a guess.
 */
inline float detectSwing(const std::vector<float>& beats, float pairStep = 1.0f) {
    if (beats.size() < 4 || pairStep <= 0.0f) return 0.5f;

    float total = 0.0f;
    int counted = 0;

    for (float beat : beats) {
        // Where this onset sits inside its own pair of subdivisions, 0..1.
        const float phase = (beat / pairStep) - std::floor(beat / pairStep);

        /*
         * Only the offbeats say anything about swing.
         *
         * A note near the downbeat carries no information - it sits at 0
         * whether the part swings or not - so including them would drag
         * every measurement toward 0.5 and report a swung part as straight.
         */
        if (phase < 0.25f || phase > 0.9f) continue;
        total += phase;
        ++counted;
    }

    if (counted < 2) return 0.5f;
    return std::clamp(total / float(counted), 0.4f, 0.8f);
}

/*
 * The grid position for a beat, with swing applied to the offbeats.
 *
 * Straight grids leave every position alone; a swing ratio above neutral
 * pushes the second of each pair later by that fraction, which is what
 * makes a quantised part keep the shuffle it was played with instead of
 * being straightened onto the nearest sixteenth.
 */
inline float swungGridPoint(float beat, float step, float swingRatio) {
    if (step <= 0.0f) return beat;

    const float pair = step * 2.0f;
    const float pairIndex = std::floor(beat / pair);
    const float within = beat - pairIndex * pair;

    // The two candidate positions in this pair: the downbeat, and the
    // offbeat wherever the swing puts it.
    const float downbeat = pairIndex * pair;
    const float offbeat = downbeat + pair * std::clamp(swingRatio, 0.1f, 0.9f);

    // Whichever is nearer, plus the start of the next pair so a note at the
    // very end of one snaps forward rather than back.
    const float nextDownbeat = downbeat + pair;

    const float toDown = std::fabs(within);
    const float toOff = std::fabs(beat - offbeat);
    const float toNext = std::fabs(beat - nextDownbeat);

    if (toDown <= toOff && toDown <= toNext) return downbeat;
    if (toOff <= toNext) return offbeat;
    return nextDownbeat;
}

} // namespace ChiptuneTracker
