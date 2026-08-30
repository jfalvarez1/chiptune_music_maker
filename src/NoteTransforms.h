#pragma once

/*
 * ChiptuneTracker - Selection transforms
 *
 * Transpose did not exist anywhere in this program. Not on a selection, not
 * on a pattern, not on the song - in a music tool. It is the most basic
 * editing operation there is: write a bassline once, move it to fit the
 * chord.
 *
 * Kept pure and vector-based so the transforms can be tested without a UI
 * and reused by the piano roll, the tracker view and any generator.
 */

#include "Types.h"
#include "Scales.h"

#include <vector>
#include <algorithm>
#include <cmath>

namespace ChiptuneTracker {

// MIDI's range. A note pushed outside it is not playable, so transposing
// has to decide what to do about it rather than silently wrapping.
inline constexpr int MIN_PITCH = 0;
inline constexpr int MAX_PITCH = 127;

struct TransformResult {
    int changed = 0;   // notes actually modified
    int blocked = 0;   // notes left alone because the result was unplayable

    bool didAnything() const { return changed > 0; }
};

// Transpose the given notes by a number of semitones.
//
// safeMode skips any note that would leave the playable range instead of
// clamping it. Clamping looks like it worked and quietly collapses a chord
// onto one pitch; skipping is visible and reversible. Renoise makes the same
// distinction, and defaults it on.
inline TransformResult transposeNotes(std::vector<Note>& notes,
                                      const std::vector<int>& indices,
                                      int semitones,
                                      bool safeMode = true) {
    TransformResult result;
    if (semitones == 0) return result;

    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(notes.size())) continue;

        const int target = notes[static_cast<size_t>(index)].pitch + semitones;

        if (target < MIN_PITCH || target > MAX_PITCH) {
            if (safeMode) {
                ++result.blocked;
                continue;
            }
            notes[static_cast<size_t>(index)].pitch =
                std::max(MIN_PITCH, std::min(target, MAX_PITCH));
        } else {
            notes[static_cast<size_t>(index)].pitch = target;
        }
        ++result.changed;
    }
    return result;
}

// Transpose every note in a pattern.
inline TransformResult transposePattern(Pattern& pattern, int semitones,
                                        bool safeMode = true) {
    std::vector<int> all(pattern.notes.size());
    for (size_t i = 0; i < pattern.notes.size(); ++i) all[i] = static_cast<int>(i);
    return transposeNotes(pattern.notes, all, semitones, safeMode);
}

// Pull the given notes onto a scale. This is what the "Snap to Scale"
// checkbox promised while nothing read it.
inline TransformResult snapNotesToScale(std::vector<Note>& notes,
                                        const std::vector<int>& indices,
                                        int scaleRoot, int scaleType) {
    TransformResult result;
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(notes.size())) continue;

        Note& note = notes[static_cast<size_t>(index)];
        const int snapped = snapToScale(note.pitch, scaleRoot, scaleType);
        if (snapped != note.pitch &&
            snapped >= MIN_PITCH && snapped <= MAX_PITCH) {
            note.pitch = snapped;
            ++result.changed;
        }
    }
    return result;
}

// Mirror pitches around a centre note. Renoise's Mirror - a cheap way to get
// a variation on a melody that still fits the key.
inline TransformResult invertNotes(std::vector<Note>& notes,
                                   const std::vector<int>& indices,
                                   int centrePitch) {
    TransformResult result;
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(notes.size())) continue;

        Note& note = notes[static_cast<size_t>(index)];
        const int mirrored = 2 * centrePitch - note.pitch;
        if (mirrored < MIN_PITCH || mirrored > MAX_PITCH) {
            ++result.blocked;
            continue;
        }
        note.pitch = mirrored;
        ++result.changed;
    }
    return result;
}

// Reverse the selection in time, keeping it inside the span it already
// occupied so a reversed phrase does not wander off the grid.
inline TransformResult reverseNotesInTime(std::vector<Note>& notes,
                                          const std::vector<int>& indices) {
    TransformResult result;
    if (indices.size() < 2) return result;

    float lo = std::numeric_limits<float>::max();
    float hi = -std::numeric_limits<float>::max();
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(notes.size())) continue;
        const Note& note = notes[static_cast<size_t>(index)];
        lo = std::min(lo, note.startTime);
        hi = std::max(hi, note.startTime + note.duration);
    }
    if (!(hi > lo)) return result;

    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(notes.size())) continue;
        Note& note = notes[static_cast<size_t>(index)];
        // Reflect the note's own span, so its end becomes its start.
        note.startTime = lo + hi - (note.startTime + note.duration);
        ++result.changed;
    }
    return result;
}

} // namespace ChiptuneTracker
