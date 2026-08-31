#pragma once

/*
 * ChiptuneTracker - Grid snap
 *
 * The snap step was hardcoded as `std::floor(beat * 4.0f) / 4.0f` in fourteen
 * separate places in UI.h, which meant every note landed on a 1/16th and
 * nothing else was writable - no triplets, so no shuffle and no 6/8, and no
 * way to place a note off the grid deliberately.
 *
 * This is the one place that decides where a beat lands. It keeps floor()
 * rather than rounding to nearest so that at the default 1/16 setting every
 * existing gesture behaves exactly as it did before; only the step changes.
 *
 * ImGui-free and Project-free, so the snapping can be tested headlessly.
 */

#include <cmath>
#include <cstdint>

#include "TempoMap.h"

namespace ChiptuneTracker {

// Divisions are named by note value, the way a musician reads them, but the
// engine counts beats - and one beat is a quarter note. So "1/16" is a
// quarter of a beat, which is what the old hardcoded constant produced.
enum class SnapDivision : uint8_t {
    Off = 0,
    Bar,
    Half,             // 1/2 note  = 2 beats
    Quarter,          // 1/4 note  = 1 beat
    Eighth,           // 1/8 note  = 1/2 beat
    Sixteenth,        // 1/16 note = 1/4 beat   <- the old hardcoded default
    ThirtySecond,     // 1/32 note = 1/8 beat
    TripletQuarter,   // quarter triplet  = 2/3 beat
    TripletEighth,    // eighth triplet   = 1/3 beat
    TripletSixteenth, // sixteenth triplet = 1/6 beat
    Count
};

inline constexpr SnapDivision DEFAULT_SNAP = SnapDivision::Sixteenth;

// How many beats one grid step spans. Bar is the only division that depends
// on the time signature; everything else is an absolute note value.
inline float snapStepBeats(SnapDivision division, int beatsPerMeasure = 4) {
    switch (division) {
        case SnapDivision::Bar:
            return (beatsPerMeasure > 0) ? static_cast<float>(beatsPerMeasure) : 4.0f;
        case SnapDivision::Half:             return 2.0f;
        case SnapDivision::Quarter:          return 1.0f;
        case SnapDivision::Eighth:           return 0.5f;
        case SnapDivision::Sixteenth:        return 0.25f;
        case SnapDivision::ThirtySecond:     return 0.125f;
        case SnapDivision::TripletQuarter:   return 2.0f / 3.0f;
        case SnapDivision::TripletEighth:    return 1.0f / 3.0f;
        case SnapDivision::TripletSixteenth: return 1.0f / 6.0f;
        case SnapDivision::Off:
        default:
            return 0.0f;   // 0 means "do not quantise"
    }
}

inline const char* snapLabel(SnapDivision division) {
    switch (division) {
        case SnapDivision::Off:              return "Off";
        case SnapDivision::Bar:              return "Bar";
        case SnapDivision::Half:             return "1/2";
        case SnapDivision::Quarter:          return "1/4";
        case SnapDivision::Eighth:           return "1/8";
        case SnapDivision::Sixteenth:        return "1/16";
        case SnapDivision::ThirtySecond:     return "1/32";
        case SnapDivision::TripletQuarter:   return "1/4T";
        case SnapDivision::TripletEighth:    return "1/8T";
        case SnapDivision::TripletSixteenth: return "1/16T";
        default:                             return "?";
    }
}

// Snap a beat position down onto the grid.
//
// Off returns the value untouched, which is how a user places a note
// deliberately between grid lines. A non-finite input would poison every
// downstream calculation, so it is caught here rather than at each call site.
inline float snapBeat(float beat, SnapDivision division, int beatsPerMeasure = 4) {
    if (!std::isfinite(beat)) return 0.0f;

    const float step = snapStepBeats(division, beatsPerMeasure);
    if (step <= 0.0f) return beat;

    return std::floor(beat / step) * step;
}

// Snap to the closest grid line rather than the one below. Used for gestures
// where the cursor sits over a target rather than defining its left edge -
// dragging a loop end, for instance.
inline float snapBeatNearest(float beat, SnapDivision division, int beatsPerMeasure = 4) {
    if (!std::isfinite(beat)) return 0.0f;

    const float step = snapStepBeats(division, beatsPerMeasure);
    if (step <= 0.0f) return beat;

    return std::floor(beat / step + 0.5f) * step;
}

// A duration must never snap to zero, or a note becomes unselectable and
// unplayable. The floor is one 1/32 note, matching the old clamp in UI.h.
inline float snapDuration(float beats, SnapDivision division, int beatsPerMeasure = 4) {
    const float step = snapStepBeats(division, beatsPerMeasure);
    const float minimum = (step > 0.0f) ? step : 0.0625f;
    const float snapped = snapBeat(beats, division, beatsPerMeasure);
    return (snapped < minimum) ? minimum : snapped;
}

/*
 * Snap through a meter map.
 *
 * Every division except Bar is an absolute note value and needs no map. Bar
 * does: a song that starts in 4/4 and moves to 3/4 has bar lines at 32, 35,
 * 38 - and dividing by a single bar length puts every one of them after the
 * change in the wrong place. Snapping to a grid line the ruler did not draw
 * reads as the grid being broken, so both walk the same barStartAtBeat().
 *
 * The overloads take the map explicitly rather than a Project, so Snap.h
 * stays free of Project and testable on its own.
 */
inline float snapBeatMapped(float beat, SnapDivision division,
                            const TempoMap& map, int baseNumerator,
                            int baseDenominator = 4) {
    if (!std::isfinite(beat)) return 0.0f;
    if (division != SnapDivision::Bar) {
        return snapBeat(beat, division, baseNumerator);
    }
    return map.barStartAtBeat(std::max(0.0f, beat), baseNumerator, baseDenominator);
}

inline float snapBeatNearestMapped(float beat, SnapDivision division,
                                   const TempoMap& map, int baseNumerator,
                                   int baseDenominator = 4) {
    if (!std::isfinite(beat)) return 0.0f;
    if (division != SnapDivision::Bar) {
        return snapBeatNearest(beat, division, baseNumerator);
    }

    // Nearest, not below: the bar before and the bar after are different
    // lengths across a meter change, so this compares the two candidates
    // rather than assuming a fixed step either side.
    const float clamped = std::max(0.0f, beat);
    const int bar = map.barAtBeat(clamped, baseNumerator, baseDenominator);
    const float here = map.beatOfBar(bar, baseNumerator, baseDenominator);
    const float next = map.beatOfBar(bar + 1, baseNumerator, baseDenominator);
    return ((clamped - here) <= (next - clamped)) ? here : next;
}

// Step through the divisions with the bracket keys. Wraps, and skips nothing,
// so holding a key cycles the whole list.
inline SnapDivision cycleSnap(SnapDivision division, int delta) {
    const int count = static_cast<int>(SnapDivision::Count);
    int index = static_cast<int>(division) + delta;
    while (index < 0) index += count;
    return static_cast<SnapDivision>(index % count);
}

} // namespace ChiptuneTracker
