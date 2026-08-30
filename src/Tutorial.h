#pragma once

/*
 * ChiptuneTracker - Guided first track
 *
 * A lesson, not a wizard. The distinction matters and drove every decision
 * here: a wizard does the work and teaches nothing; a preset is inflexible
 * by definition. Each step of this names a goal - "draw at least four
 * notes, any notes you like" - and then watches the actual project until
 * the goal is true. The user does everything themselves, with the real
 * tools, in any order the step allows, and their track at the end is
 * theirs, not a template's.
 *
 * Consequences of that design:
 *
 *   - Completion is detected, never claimed. A step's condition reads the
 *     live Project and playback state. There is no "I did it" button to
 *     press without doing it - but there IS a Skip, because a tutorial you
 *     cannot leave is a hostage situation.
 *   - Steps are latched: once a goal has been met it stays met, so deleting
 *     notes afterwards does not un-complete a lesson and trap the user.
 *   - Informational steps (welcome, orientation) complete on Next; action
 *     steps complete on evidence. The two are visually distinct.
 *
 * Pure logic and data here, no ImGui, so every step's condition is
 * testable against a constructed project.
 */

#include "Types.h"
#include "NextStep.h"

#include <cstdint>

namespace ChiptuneTracker {

// Everything a step condition may read. Assembled by the caller each frame;
// the two bools are latches the caller owns, because "has pressed play" is
// an event, not a state the Project remembers.
struct TutorialContext {
    const Project* project = nullptr;
    bool hasPlayed = false;        // playback has run at some point
    bool loopRangeActive = false;  // a loop range is currently set
    bool projectSaved = false;     // the project has a file path
};

enum class TutorialStepKind : uint8_t {
    Info,      // completes on Next
    Action     // completes when the condition is met
};

struct TutorialStep {
    const char* title;
    const char* body;              // what to do, and why it matters
    const char* hint;              // where to look - shown smaller
    const char* focusWindow;       // window to raise when the step begins
    TutorialStepKind kind;
    bool (*isComplete)(const TutorialContext&);
};

namespace tutorial_detail {

inline ProjectShape shapeOf(const TutorialContext& context) {
    if (context.project == nullptr) return ProjectShape{};
    return describeProject(*context.project);
}

inline bool hasMelodyNotes(const TutorialContext& c) {
    return shapeOf(c).melodyNotes >= 4;
}
inline bool hasPlayed(const TutorialContext& c) { return c.hasPlayed; }
inline bool hasDrums(const TutorialContext& c) {
    return shapeOf(c).drumNotes >= 3;
}
inline bool hasBass(const TutorialContext& c) {
    return shapeOf(c).bassNotes >= 2;
}
inline bool hasSecondPattern(const TutorialContext& c) {
    return shapeOf(c).patternsWithNotes >= 2;
}
inline bool hasArrangement(const TutorialContext& c) {
    const ProjectShape shape = shapeOf(c);
    return shape.clipCount >= 2 && shape.arrangedBeats >= 8.0f;
}
inline bool hasLoopRange(const TutorialContext& c) { return c.loopRangeActive; }
inline bool hasSaved(const TutorialContext& c) { return c.projectSaved; }

} // namespace tutorial_detail

/*
 * The lesson. Ten steps from empty project to saved track.
 *
 * The thresholds are deliberately low - four notes, three drum hits, two
 * bars arranged. The lesson's job is to walk the whole road once, not to
 * demand quality on the first trip; quality comes from the second track,
 * which the user will start on their own.
 */
inline const TutorialStep* tutorialSteps(int& countOut) {
    using namespace tutorial_detail;

    static const TutorialStep STEPS[] = {
        {"Your first chiptune",
         "This lesson walks you from an empty project to a saved track. You "
         "do every step yourself, with the real tools - the lesson just "
         "watches your project and ticks each goal off as you reach it, "
         "however you choose to reach it.",
         "Close this window at any time; nothing is lost. Reopen it from "
         "Help > Lesson.",
         nullptr, TutorialStepKind::Info, nullptr},

        {"Meet the piano roll",
         "The grid in the middle is the piano roll: time runs left to right, "
         "pitch runs bottom to top. Chiptune melodies live an octave or two "
         "above middle C, where the pulse waves shine.",
         "The C labels on the left edge mark the octaves.",
         "Piano Roll", TutorialStepKind::Info, nullptr},

        {"Draw a melody",
         "Click in the piano roll to place notes - at least four, any notes "
         "you like. Around C5 and above is a good register. If they clash, "
         "tick In Key in the toolbar just above the roll - every note you "
         "place will then land in the scale shown beside it.",
         "Wrong note? Right-click erases. Ctrl+Z undoes.",
         "Piano Roll", TutorialStepKind::Action, hasMelodyNotes},

        {"Hear it",
         "Press PLAY in the Transport, or tap Space. Listen to what you "
         "made. This loop - draw, listen, adjust - is the whole craft; "
         "everything else is detail.",
         "Notes too long or short? Drag their right edge.",
         nullptr, TutorialStepKind::Action, hasPlayed},

        {"Add drums",
         "Chiptune drums are noise bursts, and they carry everything. Open "
         "the Tools tab and use the Quick Start buttons or the Drum Pattern "
         "Generator - or pick a Kick from the Sound Palette and place hits "
         "by hand on the beats. Three hits is enough to start.",
         "A kick on each beat is never wrong.",
         nullptr, TutorialStepKind::Action, hasDrums},

        {"Add a bass",
         "A bassline below middle C makes the melody sound intentional. The "
         "Triangle channel is the classic chiptune bass - the NES gave it no "
         "volume control, so it just sings. Two notes to start; the root of "
         "your melody is the safe choice.",
         "Quick Start's Root Notes or Octave Pump both work here.",
         nullptr, TutorialStepKind::Action, hasBass},

        {"Make a variation",
         "Here is where most first tracks stall: a loop that never becomes "
         "a song. Duplicate your pattern (or make a new one) and change a "
         "few notes - the end of the melody, or the drums. Two patterns you "
         "can alternate is already a song structure.",
         "The Mirror and Reverse buttons make quick variations too.",
         nullptr, TutorialStepKind::Action, hasSecondPattern},

        {"Arrange it",
         "Switch to the Arrange view and place your patterns on the "
         "timeline - pattern one, pattern two, back to one. Drag a pattern "
         "from the list, or double-click a track. Two placements is enough "
         "to hear a structure.",
         "A clip can also be transposed: select it and use the Transpose "
         "control - the same pattern a few semitones up reads as a new "
         "section.",
         "Arrangement", TutorialStepKind::Action, hasArrangement},

        {"Loop a section",
         "Drag on the strip under the arrangement tracks to set a loop "
         "range over the part you are working on. Playback stays inside it, "
         "so you can polish two bars without replaying the whole track.",
         "Click the strip once to clear the range again.",
         "Arrangement", TutorialStepKind::Action, hasLoopRange},

        {"Save your track",
         "Save it - File > Save, or the Save button in the File panel. It "
         "is a .ctp file you can reopen, and WAV or MP3 export sits right "
         "beside it when you want to share what you made.",
         "That is the whole road. The second track is where it gets good.",
         nullptr, TutorialStepKind::Action, hasSaved},
    };

    countOut = static_cast<int>(sizeof(STEPS) / sizeof(STEPS[0]));
    return STEPS;
}

/*
 * Progress. Owned by the UI layer, advanced here.
 *
 * completedMask latches finished steps by bit, so a condition that later
 * turns false again (the user deletes their drums) cannot un-complete a
 * step and walk the lesson backwards underneath them.
 */
struct TutorialProgress {
    bool active = false;
    int step = 0;
    uint32_t completedMask = 0;

    // "Has pressed play" is an event, latched here rather than in a static
    // in the panel - a restart must reset it, and state a reset must clear
    // may not live outside the thing being reset. That static was exactly
    // how a finished step leaked into the next run of the lesson.
    bool hasPlayed = false;

    // Which step last pulled window focus, so each step raises its window
    // once. Lives here for the same reason.
    int focusedStep = -1;

    bool stepDone(int index) const {
        if (index < 0 || index > 31) return false;
        return (completedMask & (1u << index)) != 0;
    }
};

// Advance the latches and, when the current step's goal is met, report it -
// the UI decides when to move on, so the user sees the tick before the text
// changes. Returns true when the current step is complete.
inline bool updateTutorial(TutorialProgress& progress, const TutorialContext& context) {
    if (!progress.active) return false;

    int count = 0;
    const TutorialStep* steps = tutorialSteps(count);
    if (progress.step < 0) progress.step = 0;
    if (progress.step >= count) return false;

    const TutorialStep& current = steps[progress.step];

    if (current.kind == TutorialStepKind::Action &&
        current.isComplete != nullptr && current.isComplete(context)) {
        progress.completedMask |= (1u << progress.step);
    }

    return progress.stepDone(progress.step) ||
           current.kind == TutorialStepKind::Info;
}

} // namespace ChiptuneTracker
