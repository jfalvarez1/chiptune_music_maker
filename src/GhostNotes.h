#pragma once

/*
 * ChiptuneTracker - Cross-channel ghost notes
 *
 * Writing a bassline against a lead you cannot see is the normal case in an
 * eight-channel chiptune, and the piano roll shows exactly one pattern. So
 * you either memorise the other parts or you guess.
 *
 * There is a wrinkle worth stating, because it decides the semantics: a
 * Pattern has no channel. Notes carry no channel either. A channel is bound
 * only when a Clip places a pattern on the timeline. So "the other channels'
 * notes" is not a property of the pattern being edited - it is a question
 * about the arrangement, and it only has an answer once the pattern has been
 * placed somewhere.
 *
 * This resolves that question and returns nothing when the pattern is not on
 * the timeline, rather than inventing an answer. The UI says so plainly.
 *
 * Pure and ImGui-free, so the overlap arithmetic is testable.
 */

#include "Types.h"

#include <vector>
#include <algorithm>

namespace ChiptuneTracker {

// A note from another channel, translated into the edited pattern's own
// timebase so the piano roll can draw it without further arithmetic.
// startTime may be negative: a note can begin before the edited pattern does.
struct GhostNote {
    float startTime = 0.0f;
    float duration  = 0.0f;
    int   pitch      = 60;
    int   channelIndex = 0;
};

/*
 * Collect the notes playing on other channels while this pattern plays.
 *
 * The first clip that references the pattern defines the reference
 * placement. A pattern placed several times has several sets of neighbours;
 * showing all of them at once would be a mess of overlapping ghosts, so the
 * first placement wins and the rest are ignored.
 *
 * maxNotes bounds the work: a dense song could otherwise produce thousands
 * of ghosts for a single frame, and past a few hundred they are visual noise
 * anyway.
 */
inline std::vector<GhostNote> collectGhostNotes(const Project& project,
                                                int patternIndex,
                                                size_t maxNotes = 512) {
    std::vector<GhostNote> ghosts;

    if (patternIndex < 0 ||
        patternIndex >= static_cast<int>(project.patterns.size())) {
        return ghosts;
    }

    // Where is this pattern placed? Without a placement there is no
    // "meanwhile", and so nothing to show.
    const Clip* reference = nullptr;
    for (const Clip& clip : project.arrangement) {
        if (clip.patternIndex == patternIndex) {
            reference = &clip;
            break;
        }
    }
    if (reference == nullptr) return ghosts;

    const float refStart = reference->startBeat;
    const float refEnd = refStart + reference->lengthBeats;

    for (const Clip& clip : project.arrangement) {
        if (&clip == reference) continue;

        // Same channel means it cannot sound at the same time as the edited
        // pattern anyway, so it is not a part to write against.
        if (clip.channelIndex == reference->channelIndex) continue;

        if (clip.patternIndex < 0 ||
            clip.patternIndex >= static_cast<int>(project.patterns.size())) {
            continue;
        }

        // Does this clip overlap the window at all?
        const float clipStart = clip.startBeat;
        const float clipEnd = clipStart + clip.lengthBeats;
        if (clipEnd <= refStart || clipStart >= refEnd) continue;

        const Pattern& pattern = project.patterns[static_cast<size_t>(clip.patternIndex)];
        const float offset = clipStart - refStart;

        for (const Note& note : pattern.notes) {
            if (ghosts.size() >= maxNotes) return ghosts;

            GhostNote ghost;
            ghost.startTime = note.startTime + offset;
            ghost.duration = note.duration;
            // The sounding pitch, not the stored one - a ghost showing where
            // the neighbour's notes are written rather than where they sound
            // would be worse than no ghost at all.
            ghost.pitch = note.pitch + clip.transpose;
            ghost.channelIndex = clip.channelIndex;
            if (ghost.pitch < 0 || ghost.pitch > 127) continue;

            // Drop anything that finishes before the window opens or starts
            // after it closes; it would be drawn off-screen regardless.
            const float ghostEnd = ghost.startTime + ghost.duration;
            const float windowLength = refEnd - refStart;
            if (ghostEnd <= 0.0f || ghost.startTime >= windowLength) continue;

            ghosts.push_back(ghost);
        }
    }

    return ghosts;
}

} // namespace ChiptuneTracker
