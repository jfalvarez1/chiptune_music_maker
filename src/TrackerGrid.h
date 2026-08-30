#pragma once

/*
 * ChiptuneTracker - Tracker grid
 *
 * The tracker view was read-only and, worse, wrong: it looked for the first
 * note in the selected pattern whose start rounded to the current step and
 * printed it into all eight channel columns, ignoring channel entirely. Its
 * own comment admitted "This is a simplification". The program is called a
 * tracker.
 *
 * The structural problem behind it. A Pattern is a flat list of notes with
 * no channel, and a Note carries no channel either - a channel is bound only
 * when a Clip places a pattern on the timeline. So a pattern on its own
 * simply does not contain the information a tracker row needs.
 *
 * Rather than migrate the file format and every pattern in every project,
 * this reads the arrangement: a tracker row is a moment in the song, and
 * each column asks "which pattern is playing on this channel right now, and
 * what does it have at this instant?". That is what a tracker has always
 * been - the order list and the pattern data seen together - and it costs no
 * migration at all.
 *
 * Pure and ImGui-free, so the resolution and the edits are testable.
 */

#include "Types.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ChiptuneTracker {

// Which pattern is sounding on a channel at a given point in the song.
struct TrackerSlot {
    int clipIndex = -1;
    int patternIndex = -1;
    float localBeat = 0.0f;   // position within that pattern
    bool valid = false;
};

// What is in one cell of the grid.
struct TrackerCell {
    bool hasNote = false;
    int noteIndex = -1;        // index into the owning pattern's note list
    int patternIndex = -1;
    int pitch = 60;
    float velocity = 1.0f;
    OscillatorType oscillator = OscillatorType::Pulse;
};

/*
 * Find the clip covering this channel at this beat.
 *
 * Later clips win where two overlap. The arrangement permits overlapping
 * clips on one channel and the sequencer plays both; the grid can only show
 * one, and the most recently placed is the better guess at what the user
 * means.
 */
inline TrackerSlot resolveTrackerSlot(const Project& project, int channel,
                                      float absBeat) {
    TrackerSlot slot;
    if (channel < 0 || channel >= Project::MAX_CHANNELS) return slot;
    if (!std::isfinite(absBeat)) return slot;

    for (int i = static_cast<int>(project.arrangement.size()) - 1; i >= 0; --i) {
        const Clip& clip = project.arrangement[static_cast<size_t>(i)];
        if (clip.channelIndex != channel) continue;
        if (clip.patternIndex < 0 ||
            clip.patternIndex >= static_cast<int>(project.patterns.size())) {
            continue;
        }
        if (absBeat < clip.startBeat) continue;
        if (absBeat >= clip.startBeat + clip.lengthBeats) continue;

        slot.clipIndex = i;
        slot.patternIndex = clip.patternIndex;
        slot.localBeat = absBeat - clip.startBeat;
        slot.valid = true;
        return slot;
    }
    return slot;
}

/*
 * The note occupying a row.
 *
 * A row covers the half-open span [localBeat, localBeat + stepBeats), so
 * every note belongs to exactly one row and none is shown twice. Rounding to
 * the nearest row instead would make two notes a hair apart fight over one
 * cell, and would hide one of them.
 */
inline int findNoteInStep(const Pattern& pattern, float localBeat, float stepBeats) {
    if (stepBeats <= 0.0f) return -1;

    const float lo = localBeat;
    const float hi = localBeat + stepBeats;

    for (size_t i = 0; i < pattern.notes.size(); ++i) {
        const float start = pattern.notes[i].startTime;
        if (start >= lo && start < hi) return static_cast<int>(i);
    }
    return -1;
}

inline TrackerCell readTrackerCell(const Project& project, int channel,
                                   float absBeat, float stepBeats) {
    TrackerCell cell;

    const TrackerSlot slot = resolveTrackerSlot(project, channel, absBeat);
    if (!slot.valid) return cell;

    cell.patternIndex = slot.patternIndex;

    const Pattern& pattern = project.patterns[static_cast<size_t>(slot.patternIndex)];
    const int noteIndex = findNoteInStep(pattern, slot.localBeat, stepBeats);
    if (noteIndex < 0) return cell;

    const Note& note = pattern.notes[static_cast<size_t>(noteIndex)];
    cell.hasNote = true;
    cell.noteIndex = noteIndex;
    cell.pitch = note.pitch;
    cell.velocity = note.velocity;
    cell.oscillator = note.oscillatorType;
    return cell;
}

/*
 * Write a note into the grid.
 *
 * If nothing is placed on this channel at this beat there is nowhere for the
 * note to live, so a pattern and a clip are created to hold it - one bar
 * long, aligned to the bar. Refusing to type outside an existing clip would
 * make the tracker unusable on an empty project, which is exactly where
 * someone starts.
 *
 * Returns the index of the written note, or -1 if it could not be placed.
 */
inline int writeTrackerNote(Project& project, int channel, float absBeat,
                            float stepBeats, int pitch, OscillatorType oscillator,
                            float velocity = 0.8f) {
    if (channel < 0 || channel >= Project::MAX_CHANNELS) return -1;
    if (pitch < 0 || pitch > 127) return -1;
    if (!std::isfinite(absBeat) || absBeat < 0.0f) return -1;
    if (stepBeats <= 0.0f) return -1;

    TrackerSlot slot = resolveTrackerSlot(project, channel, absBeat);

    if (!slot.valid) {
        if (static_cast<int>(project.patterns.size()) >= Project::MAX_PATTERNS) {
            return -1;   // no slots left; the caller reports this
        }

        const float barLength = (project.beatsPerMeasure > 0)
                              ? static_cast<float>(project.beatsPerMeasure) : 4.0f;
        const float barStart = std::floor(absBeat / barLength) * barLength;

        Pattern created;
        created.length = static_cast<int>(barLength);
        created.name = project.channels[static_cast<size_t>(channel)].name +
                       " " + std::to_string(static_cast<int>(barStart / barLength) + 1);
        project.patterns.push_back(created);

        Clip clip;
        clip.patternIndex = static_cast<int>(project.patterns.size()) - 1;
        clip.channelIndex = channel;
        clip.startBeat = barStart;
        clip.lengthBeats = barLength;
        project.arrangement.push_back(clip);

        slot.clipIndex = static_cast<int>(project.arrangement.size()) - 1;
        slot.patternIndex = clip.patternIndex;
        slot.localBeat = absBeat - barStart;
        slot.valid = true;
    }

    Pattern& pattern = project.patterns[static_cast<size_t>(slot.patternIndex)];

    // Replacing rather than stacking: typing over a row in a tracker
    // overwrites it, and a column holds one note per row by definition.
    const int existing = findNoteInStep(pattern, slot.localBeat, stepBeats);
    if (existing >= 0) {
        Note& note = pattern.notes[static_cast<size_t>(existing)];
        note.pitch = pitch;
        note.oscillatorType = oscillator;
        note.velocity = velocity;
        return existing;
    }

    if (static_cast<int>(pattern.notes.size()) >= Pattern::MAX_NOTES) return -1;

    Note note;
    note.pitch = pitch;
    note.startTime = slot.localBeat;
    note.duration = stepBeats;
    note.oscillatorType = oscillator;
    note.velocity = velocity;
    pattern.notes.push_back(note);

    // A note typed past the pattern's stated end must extend it, or it will
    // be written, saved, and never heard.
    const float noteEnd = note.startTime + note.duration;
    if (noteEnd > static_cast<float>(pattern.length)) {
        pattern.length = static_cast<int>(std::ceil(noteEnd));
    }

    return static_cast<int>(pattern.notes.size()) - 1;
}

// Remove whatever occupies this cell. Returns true if something was removed.
inline bool clearTrackerNote(Project& project, int channel, float absBeat,
                             float stepBeats) {
    const TrackerSlot slot = resolveTrackerSlot(project, channel, absBeat);
    if (!slot.valid) return false;

    Pattern& pattern = project.patterns[static_cast<size_t>(slot.patternIndex)];
    const int noteIndex = findNoteInStep(pattern, slot.localBeat, stepBeats);
    if (noteIndex < 0) return false;

    pattern.notes.erase(pattern.notes.begin() + noteIndex);
    return true;
}

// How many rows the grid spans, given the song length and the row resolution.
inline int trackerRowCount(const Project& project, float stepBeats) {
    if (stepBeats <= 0.0f) return 0;

    // The song length is the floor, but a clip dragged past it must still be
    // reachable - otherwise notes exist that cannot be edited.
    float end = project.songLength;
    for (const Clip& clip : project.arrangement) {
        end = std::max(end, clip.startBeat + clip.lengthBeats);
    }
    if (!(end > 0.0f)) return 0;

    return static_cast<int>(std::ceil(end / stepBeats));
}

} // namespace ChiptuneTracker
