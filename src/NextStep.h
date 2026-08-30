#pragma once

/*
 * ChiptuneTracker - What to do next
 *
 * The most common way a project dies is documented and specific: someone
 * writes an eight-bar loop, likes it, and cannot turn it into a song. People
 * describe having hundreds of unfinished loops on disk. What they ask for is
 * not encouragement or theory - it is a concrete next thing to do.
 *
 * So this reads the project and names one. Not a tutorial and not a wizard:
 * a single line that changes as the piece changes, and that can be turned
 * off and never seen again.
 *
 * The suggestions deliberately push toward finishing rather than toward
 * elaborating, because adding another layer to a loop is the failure mode,
 * not the cure.
 *
 * Pure and ImGui-free, so every rule is testable against a project.
 */

#include "Types.h"

#include <algorithm>
#include <cmath>

namespace ChiptuneTracker {

struct NextStep {
    const char* headline = "";
    const char* detail = "";
    bool valid = false;
};

// What a project currently contains, in the terms the advice is phrased in.
struct ProjectShape {
    int noteCount = 0;
    int drumNotes = 0;
    int bassNotes = 0;      // pitched and low
    int melodyNotes = 0;    // pitched and not low
    int patternsWithNotes = 0;
    int clipCount = 0;
    float arrangedBeats = 0.0f;   // how far the last clip reaches
    int channelsUsed = 0;
};

// Below this is bass register rather than melody: middle C.
//
// C3 was the first guess and was wrong. A template in A minor puts its bass
// on A3, above C3, so a project with a perfectly good bassline was told to
// go and write one. Middle C is the conventional split and every genre's
// bass part falls below it.
inline constexpr int BASS_PITCH_CEILING = 60;

inline bool isDrumOscillator(OscillatorType type) {
    // Every drum voice sits at the end of the enum, after the synths.
    return type >= OscillatorType::Kick;
}

inline ProjectShape describeProject(const Project& project) {
    ProjectShape shape;

    for (const Pattern& pattern : project.patterns) {
        if (!pattern.notes.empty()) ++shape.patternsWithNotes;

        for (const Note& note : pattern.notes) {
            ++shape.noteCount;
            if (isDrumOscillator(note.oscillatorType)) {
                ++shape.drumNotes;
            } else if (note.pitch < BASS_PITCH_CEILING) {
                ++shape.bassNotes;
            } else {
                ++shape.melodyNotes;
            }
        }
    }

    bool channelSeen[Project::MAX_CHANNELS] = {};
    for (const Clip& clip : project.arrangement) {
        ++shape.clipCount;
        shape.arrangedBeats = std::max(shape.arrangedBeats,
                                       clip.startBeat + clip.lengthBeats);
        if (clip.channelIndex >= 0 && clip.channelIndex < Project::MAX_CHANNELS) {
            channelSeen[clip.channelIndex] = true;
        }
    }
    for (bool used : channelSeen) {
        if (used) ++shape.channelsUsed;
    }

    return shape;
}

// Eight bars in 4/4. The length at which a loop stops being a sketch and
// starts being the thing someone gets stuck on.
inline constexpr float LOOP_TRAP_BEATS = 32.0f;

/*
 * One thing to do next.
 *
 * Ordered so the earliest unmet need wins: there is no use suggesting an
 * arrangement to someone who has not written a bassline yet.
 */
inline NextStep suggestNextStep(const Project& project) {
    const ProjectShape shape = describeProject(project);
    NextStep step;
    step.valid = true;

    if (shape.noteCount == 0) {
        step.headline = "Start with four bars";
        step.detail = "File > New From Template drops in drums, bass and a "
                      "melody you can change. Or draw notes straight into the "
                      "piano roll.";
        return step;
    }

    if (shape.drumNotes == 0) {
        step.headline = "Add drums";
        step.detail = "Tools > Drum Pattern Generator writes a bar in the genre "
                      "you picked. Almost nothing sounds finished without one.";
        return step;
    }

    if (shape.bassNotes == 0) {
        step.headline = "Add a bassline";
        step.detail = "Tools > Bass Generator follows the root of each bar. A "
                      "loop with drums and no bass sounds thin no matter what "
                      "else you put on it.";
        return step;
    }

    if (shape.melodyNotes == 0) {
        step.headline = "Write a melody";
        step.detail = "Turn on Snap to Scale in Tools, then draw in the piano "
                      "roll - every note you place will be in key.";
        return step;
    }

    // Everything is present. From here the advice is about finishing, because
    // adding another layer to a loop is the trap rather than the way out.
    if (shape.arrangedBeats < LOOP_TRAP_BEATS) {
        if (shape.patternsWithNotes < 2) {
            step.headline = "Make a variation";
            step.detail = "Duplicate a pattern and change a few notes, rather "
                          "than adding another layer. Two patterns you can "
                          "alternate is already a song structure.";
            return step;
        }

        step.headline = "Arrange what you have";
        step.detail = "Drag your patterns along the timeline to build an intro, "
                      "a main section and an ending. Muting a channel for four "
                      "bars is enough to make a section sound different.";
        return step;
    }

    if (shape.channelsUsed < 3) {
        step.headline = "Give it some width";
        step.detail = "You have a structure. Try a counter-melody on another "
                      "channel, or pan two parts apart in the Mixer.";
        return step;
    }

    step.headline = "Sounds like a song";
    step.detail = "Set a loop range over any section to check it in isolation, "
                  "then export to WAV from the File panel.";
    return step;
}

} // namespace ChiptuneTracker
