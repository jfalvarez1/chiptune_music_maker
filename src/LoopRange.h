#pragma once

/*
 * ChiptuneTracker - Loop window
 *
 * PlaybackState has carried loopStart and loopEnd since the beginning, and
 * Sequencer::setLoop() has always accepted them - but setLoop was called
 * exactly once, at startup, with (false, 0, 16), and the playback loop never
 * read loopEnd at all: it wrapped at getPatternEndTime() instead. So the loop
 * range was both unsettable from the UI and unhonoured by the engine.
 *
 * Looping a two-bar section to iterate on it is the central motion of writing
 * a chiptune, so this resolves the question the playback loop actually needs
 * answered: given a user range that may or may not be set, and whatever
 * content exists, which beats do we play?
 *
 * Pure, so the wrap arithmetic can be tested without an audio device.
 */

#include <cmath>

namespace ChiptuneTracker {

// The span playback repeats over. `valid` is false when there is nothing to
// play at all - an empty pattern - in which case the caller should stop
// rather than divide by a zero-length window.
struct LoopWindow {
    float start = 0.0f;
    float end   = 0.0f;
    bool  valid = false;

    float length() const { return end - start; }
    bool contains(float beat) const { return beat >= start && beat < end; }
};

// A range shorter than this is a mis-drag, not an intent to loop, and would
// otherwise spin the playhead at audio rate.
inline constexpr float MIN_LOOP_BEATS = 0.0625f;

// A user-drawn range wins when it is set and long enough; otherwise fall back
// to looping over whatever content exists, which is the old behaviour.
inline LoopWindow resolveLoopWindow(bool rangeActive, float rangeStart,
                                    float rangeEnd, float contentEnd) {
    LoopWindow window;

    if (rangeActive && std::isfinite(rangeStart) && std::isfinite(rangeEnd)) {
        const float lo = (rangeStart < rangeEnd) ? rangeStart : rangeEnd;
        const float hi = (rangeStart < rangeEnd) ? rangeEnd : rangeStart;
        if (hi - lo >= MIN_LOOP_BEATS) {
            window.start = (lo > 0.0f) ? lo : 0.0f;
            window.end   = hi;
            window.valid = true;
            return window;
        }
    }

    if (std::isfinite(contentEnd) && contentEnd > 0.0f) {
        window.start = 0.0f;
        window.end   = contentEnd;
        window.valid = true;
    }
    return window;
}

// Fold a beat back into the window. A single subtraction is not enough: a
// long audio block, or a window shortened while playing, can leave the
// playhead several window lengths past the end. This is the same class of bug that
// let oscillator phase run away to 2.4e7.
inline float wrapIntoWindow(float beat, const LoopWindow& window) {
    if (!window.valid) return beat;

    const float length = window.length();
    if (length <= 0.0f) return window.start;

    if (beat < window.start) {
        const float behind = window.start - beat;
        return window.end - std::fmod(behind, length);
    }
    if (beat >= window.end) {
        const float past = beat - window.start;
        return window.start + std::fmod(past, length);
    }
    return beat;
}

// Where the playhead should go when the user hits play. Dropping it inside
// the loop means pressing play always audibly does something, rather than
// starting outside the range and silently running until it happens to enter.
inline float clampStartBeat(float beat, const LoopWindow& window, bool looping) {
    if (!looping || !window.valid) return beat;
    if (beat < window.start || beat >= window.end) return window.start;
    return beat;
}

} // namespace ChiptuneTracker
