#pragma once

/*
 * ChiptuneTracker - Note expansion
 *
 * Four fields on Note were declared, saved to the .ctp file and loaded back
 * out again, but never once read by the synth or the sequencer: noteDelay,
 * noteCut, retriggerCount and echoRepeats. We were paying the storage and
 * serialisation cost for nothing. These are the classic tracker commands -
 * EDxx note delay, ECxx note cut, Qxy retrigger - and they are how chiptune
 * gets flams, stutters and pseudo-delay out of a fixed number of channels.
 *
 * A note is not necessarily one sound. Expanding it into a list of triggers
 * is the whole of the feature, so that is what this header does, and the
 * sequencer just plays whatever comes back.
 *
 * Runs on the audio thread, so: no allocation, no locks, fixed-size output.
 * Pure and Project-free, so every combination is testable headlessly.
 */

#include "Types.h"

#include <cmath>
#include <cstring>
#include <cstdint>

namespace ChiptuneTracker {

// One sounding hit. Absolute beats, so the sequencer compares against the
// playhead without further arithmetic.
struct NoteTrigger {
    float startBeat = 0.0f;
    float endBeat   = 0.0f;
    float velocity  = 1.0f;
};

// A retrigger of 8 with 4 echoes is already an unmusical wall of sound; this
// cap exists to bound the stack buffer, not to constrain anyone.
inline constexpr int MAX_NOTE_TRIGGERS = 24;

// Guard rails for values that arrive from a hand-edited file. A zero or
// negative interval would emit every trigger at the same instant.
inline constexpr float MIN_TRIGGER_INTERVAL = 1.0f / 64.0f;

/*
 * Expand one note into the hits it should actually produce.
 *
 * originBeat is the clip's start beat (0 for pattern preview), so the
 * returned triggers are in the same timebase as the playhead.
 *
 * Order of operations matters and follows tracker convention:
 *   1. noteDelay shifts the whole note later
 *   2. noteCut shortens it
 *   3. retrigger chops what remains into repeated hits
 *   4. echo adds decaying copies of the note after it
 *
 * Returns the number of triggers written, always at least 1.
 */
inline int expandNote(const Note& note, float originBeat,
                      NoteTrigger* out, int maxOut) {
    if (out == nullptr || maxOut <= 0) return 0;

    const float rawStart = originBeat + note.startTime + std::fmax(0.0f, note.noteDelay);
    const float start = std::isfinite(rawStart) ? rawStart : originBeat;

    // noteCut shortens the note; it never lengthens it, and a cut longer than
    // the note is simply no cut at all.
    float duration = std::isfinite(note.duration) ? note.duration : 0.0f;
    if (duration < 0.0f) duration = 0.0f;
    if (note.noteCut > 0.0f && note.noteCut < duration) {
        duration = note.noteCut;
    }

    const float velocity = std::isfinite(note.velocity) ? note.velocity : 1.0f;

    int count = 0;
    auto emit = [&](float hitStart, float hitEnd, float hitVelocity) {
        if (count >= maxOut) return;
        if (hitEnd <= hitStart) return;
        out[count].startBeat = hitStart;
        out[count].endBeat   = hitEnd;
        out[count].velocity  = (hitVelocity > 0.0f) ? hitVelocity : 0.0f;
        ++count;
    };

    // ---- Retrigger ---------------------------------------------------------
    //
    // The note is chopped into count+1 evenly spaced hits. Each hit lasts
    // one interval, except that none may sound past the note's own end -
    // otherwise a retrigger on a short note rings out over the next one.
    const int retriggers = (note.retriggerCount > 0) ? note.retriggerCount : 0;
    const float retriggerSpeed = note.retriggerSpeed;

    if (retriggers > 0 && retriggerSpeed >= MIN_TRIGGER_INTERVAL && duration > 0.0f) {
        const float noteEnd = start + duration;
        for (int i = 0; i <= retriggers; ++i) {
            const float hitStart = start + static_cast<float>(i) * retriggerSpeed;
            if (hitStart >= noteEnd) break;
            const float hitEnd = std::fmin(hitStart + retriggerSpeed, noteEnd);
            emit(hitStart, hitEnd, velocity);
        }
        // A retrigger interval longer than the note leaves only the first hit,
        // which is correct - but if the loop emitted nothing at all (a zero
        // length note), fall through to the plain note below.
    }

    if (count == 0) {
        emit(start, start + duration, velocity);
    }

    // ---- Echo --------------------------------------------------------------
    //
    // Decaying copies of the original note, not of the retrigger burst -
    // echoing a stutter would multiply into hundreds of hits for no musical
    // gain. Copies stop early once they fall below audibility.
    const int echoes = (note.echoRepeats > 0) ? note.echoRepeats : 0;
    const float echoDelay = note.echoDelay;
    if (echoes > 0 && echoDelay >= MIN_TRIGGER_INTERVAL && duration > 0.0f) {
        const float decay = (std::isfinite(note.echoDecay) && note.echoDecay > 0.0f)
                          ? std::fmin(note.echoDecay, 1.0f) : 0.5f;
        float echoVelocity = velocity;
        for (int i = 1; i <= echoes; ++i) {
            echoVelocity *= decay;
            if (echoVelocity < 0.001f) break;   // inaudible; stop spending voices
            const float hitStart = start + static_cast<float>(i) * echoDelay;
            emit(hitStart, hitStart + duration, echoVelocity);
        }
    }

    return count;
}

// Does this note sound on this pass through the loop?
//
// The result has to be identical for the note-on and the note-off, which
// happen in different calls several thousand samples apart - so this is a
// hash rather than a running RNG. Feeding it the loop pass counter re-rolls
// every repeat, which is the entire point; feeding it the pitch and the
// beat keeps two notes on the same step independent of each other.
inline bool noteShouldSound(const Note& note, float absStartBeat, uint32_t pass) {
    // Checked on the bits rather than with a comparison: this project builds
    // with fast floating point, which lets the compiler fold !(x < 1.0f) into
    // x >= 1.0f - false for NaN, so a NaN probability would silence the note
    // instead of playing it. A silent note with no visible cause is the worst
    // possible failure here.
    uint32_t bits = 0;
    std::memcpy(&bits, &note.probability, sizeof(bits));
    const bool isNaN = ((bits & 0x7F800000u) == 0x7F800000u) && (bits & 0x007FFFFFu);
    if (isNaN) return true;

    if (note.probability >= 1.0f) return true;
    if (note.probability <= 0.0f) return false;

    uint32_t h = static_cast<uint32_t>(static_cast<int32_t>(absStartBeat * 256.0f));
    h ^= static_cast<uint32_t>(note.pitch) * 2654435761u;
    h ^= pass + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;

    return static_cast<float>(h & 0xFFFFu) / 65535.0f < note.probability;
}

// True when a note needs expanding at all. The overwhelming majority do not,
// and the sequencer's inner loop runs per sample block over every note in
// every clip, so it is worth not paying for the general path.
inline bool noteNeedsExpansion(const Note& note) {
    return note.noteDelay > 0.0f
        || note.noteCut > 0.0f
        || note.retriggerCount > 0
        || note.echoRepeats > 0;
}

} // namespace ChiptuneTracker
