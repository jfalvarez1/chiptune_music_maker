#pragma once

// ============================================================================
// Optional tidying, after the notes exist
//
// Two things people ask for once they have hummed or beatboxed a part in:
// "keep it in key" and "make it sit on the beat". Both are genuinely useful
// and both are easy to do so aggressively that the result stops sounding
// like the person who played it.
//
// So everything here is OFF by default and every operation reports what it
// changed. The rule throughout: a correction that is confidently wrong is
// worse than no correction, because the user has to find and undo it, and
// they may not notice until much later.
//
// AUTO-TUNE, for a hummed melody. Snapping every note to a scale flattens
// the blue notes and passing tones that make a line worth keeping, so it is
// gated three ways - only when the key is confidently known, only on notes
// short enough to be passing rather than structural, and only when the note
// is close enough to a scale tone to have been aiming at one. A deliberate
// chromatic note stays.
//
// STAY ON BEAT, for a beatboxed groove. Quantising is in Groove.h; what is
// here is the part that comes after - filling in a hit that was clearly
// meant to be there. Beatboxing a steady hi-hat and dropping one is the
// single most common thing that happens, and it is obvious to a listener.
//
// Filling in invents a note the user did not play, which is a real
// intrusion, so it is the most conservative operation here: it only acts on
// a pattern already established as steady, only where exactly one hit is
// missing from it, and it never adds more than a fraction of what was
// played.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <vector>

#include "Types.h"
#include "Scales.h"
#include "Snap.h"

namespace ChiptuneTracker {

// ============================================================================
// Keeping a hummed line in key
// ============================================================================

struct KeySnapOptions {
    bool enabled = false;      // off until asked for

    int scaleRoot = 0;         // 0 = C
    int scaleType = 0;         // index into Scales.h

    /*
     * How close a note must already be to a scale tone to be pulled onto it.
     *
     * A semitone is the largest this can sensibly be - beyond that, moving
     * the note changes which note it is rather than tuning it. One semitone
     * catches the genuinely wrong notes and leaves anything further out,
     * which was either deliberate or an octave error that snapping cannot
     * fix anyway.
     */
    int maxDistanceSemitones = 1;

    /*
     * Notes longer than this are left alone.
     *
     * A long out-of-scale note is a structural choice - a blue note, a
     * suspension, the colour of the whole phrase. A short one is far more
     * likely to be a passing artefact of detection. Snapping the long ones
     * is what makes an auto-tuned part sound like the character has been
     * ironed out of it.
     */
    float maxDurationBeats = 1.0f;

    /*
     * How sure the key detection has to be.
     *
     * A short hum is often genuinely ambiguous between relative major and
     * minor, and snapping to a confidently wrong key destroys the take. The
     * margin between the best key and the runner-up is what says whether
     * there is an answer at all.
     */
    float minKeyConfidence = 0.05f;
};

struct KeySnapReport {
    int considered = 0;
    int moved = 0;
    int leftLong = 0;        // too long to be a passing note
    int leftFar = 0;         // too far out to have been aiming at a scale tone
    bool ranAtAll = false;   // false when the key was not confident enough
};

/*
 * Pull the out-of-key notes onto the scale, and leave the rest.
 *
 * Works on a copy of the pitches only - timing is untouched, because a note
 * being out of key says nothing about whether it was played in the right
 * place.
 */
inline KeySnapReport snapNotesToKey(std::vector<Note>& notes,
                                    const KeySnapOptions& options,
                                    float keyConfidence) {
    KeySnapReport report;
    if (!options.enabled) return report;

    if (keyConfidence < options.minKeyConfidence) {
        // Deliberately does nothing rather than guessing. The caller shows
        // this, so the user learns the key was unclear instead of wondering
        // why the button did nothing.
        return report;
    }
    report.ranAtAll = true;

    for (Note& note : notes) {
        ++report.considered;

        if (isNoteInScale(note.pitch, options.scaleRoot, options.scaleType)) {
            continue;
        }

        if (note.duration > options.maxDurationBeats) {
            ++report.leftLong;
            continue;
        }

        const int snapped = snapToScale(note.pitch, options.scaleRoot,
                                        options.scaleType);
        if (std::abs(snapped - note.pitch) > options.maxDistanceSemitones) {
            ++report.leftFar;
            continue;
        }

        note.pitch = std::clamp(snapped, 0, 127);
        ++report.moved;
    }
    return report;
}

// ============================================================================
// Filling in a beatboxed hit that was clearly meant to be there
// ============================================================================

struct DrumFillOptions {
    bool enabled = false;      // off until asked for

    /*
     * How steady a pattern has to be before anything is inferred from it.
     *
     * Measured as the spread of the gaps between hits, relative to the
     * typical gap. A groove that is genuinely irregular has no pattern to
     * complete, and filling one in would be inventing a part rather than
     * repairing one.
     */
    float maxGapVariation = 0.22f;

    // At least this many hits of one drum before its pattern is believed.
    int minHits = 4;

    /*
     * A ceiling on invention, as a fraction of what was actually played.
     *
     * Adding a note the user did not play is a real intrusion. This bounds
     * it: a take that is mostly holes is not a groove with gaps, it is a
     * different groove, and completing it would produce something nobody
     * played.
     */
    float maxAddedFraction = 0.25f;
};

struct DrumFillReport {
    int added = 0;
    int examined = 0;      // how many drum voices had enough hits to judge
    bool ranAtAll = false;
};

/*
 * Complete a steady pattern where exactly one hit is missing.
 *
 * Per drum, because a kick and a hi-hat have different patterns and judging
 * them together would find a period belonging to neither.
 *
 * Only a single missing hit is filled - a gap of about twice the
 * established spacing. A gap of three or more is a rest the person meant,
 * not a mistake, and filling those turns a groove with space in it into a
 * machine pattern.
 */
inline DrumFillReport fillDrumGaps(std::vector<Note>& notes,
                                   const DrumFillOptions& options) {
    DrumFillReport report;
    if (!options.enabled || notes.empty()) return report;
    report.ranAtAll = true;

    // Group by pitch: for drums the pitch is which drum it is.
    std::vector<int> pitches;
    for (const Note& note : notes) {
        if (std::find(pitches.begin(), pitches.end(), note.pitch) == pitches.end()) {
            pitches.push_back(note.pitch);
        }
    }

    const int ceiling = std::max(1, static_cast<int>(
        float(notes.size()) * options.maxAddedFraction));

    std::vector<Note> added;

    for (int pitch : pitches) {
        std::vector<const Note*> voice;
        for (const Note& note : notes) {
            if (note.pitch == pitch) voice.push_back(&note);
        }
        if (static_cast<int>(voice.size()) < options.minHits) continue;

        std::sort(voice.begin(), voice.end(),
                  [](const Note* a, const Note* b) {
                      return a->startTime < b->startTime;
                  });

        // The gaps, and how consistent they are.
        std::vector<float> gaps;
        for (size_t i = 1; i < voice.size(); ++i) {
            const float gap = voice[i]->startTime - voice[i - 1]->startTime;
            if (gap > 1e-4f) gaps.push_back(gap);
        }
        if (gaps.size() < 2) continue;

        ++report.examined;

        // The typical gap is the median: one long rest in the middle of an
        // otherwise steady pattern must not move it, and a mean would.
        std::vector<float> sorted = gaps;
        std::sort(sorted.begin(), sorted.end());
        const float period = sorted[sorted.size() / 2];
        if (period <= 1e-4f) continue;

        /*
         * How steady it is, judged only on the gaps that are not holes.
         *
         * A gap of about twice the period is the hole being looked for, so
         * including it in the steadiness measure would make every pattern
         * with a hole look too irregular to repair - which is exactly
         * backwards.
         */
        float variation = 0.0f;
        int counted = 0;
        for (float gap : gaps) {
            if (gap > period * 1.5f) continue;
            variation += std::fabs(gap - period) / period;
            ++counted;
        }
        if (counted == 0) continue;
        variation /= float(counted);

        if (variation > options.maxGapVariation) continue;   // not steady enough

        for (size_t i = 1; i < voice.size(); ++i) {
            if (static_cast<int>(added.size()) >= ceiling) break;

            const float gap = voice[i]->startTime - voice[i - 1]->startTime;

            // About two periods: one hit missing. Anything wider is a rest.
            if (gap < period * 1.6f || gap > period * 2.4f) continue;

            Note filled = *voice[i - 1];
            filled.startTime = voice[i - 1]->startTime + gap * 0.5f;

            // Quieter than its neighbours, so a filled hit sits under the
            // played ones rather than pretending to be one of them.
            filled.velocity = std::clamp(
                (voice[i - 1]->velocity + voice[i]->velocity) * 0.5f * 0.85f,
                0.05f, 1.0f);
            added.push_back(filled);
        }
    }

    for (const Note& note : added) notes.push_back(note);
    report.added = static_cast<int>(added.size());

    if (report.added > 0) {
        std::sort(notes.begin(), notes.end(),
                  [](const Note& a, const Note& b) {
                      if (a.startTime != b.startTime) return a.startTime < b.startTime;
                      return a.pitch < b.pitch;
                  });
    }
    return report;
}

} // namespace ChiptuneTracker
