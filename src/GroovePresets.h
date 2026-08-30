#pragma once

/*
 * ChiptuneTracker - Groove presets
 *
 * The community research was specific about rhythm: nobody asks how to
 * program a beat, they ask why theirs feels dead - "i can't pinpoint what it
 * is that makes everyone else's tracks sound organic rather than a
 * mechanical loop." The tools that fix it (swing, humanize) have been on
 * Project all along, buried behind sliders whose right values you had to
 * already know.
 *
 * A preset is a named feel: press Swung, hear swing. Each is one click,
 * fully described, and only sets the same fields the sliders set - so a
 * preset is a starting point the sliders then refine, not a mode.
 *
 * Pure data, no ImGui, testable.
 */

#include "Types.h"

namespace ChiptuneTracker {

struct GroovePreset {
    const char* name;
    const char* description;

    float swing;            // 0..1
    float swingGrid;        // 0.5 = 8ths, 0.25 = 16ths
    bool humanize;
    float humanizeAmount;   // timing wobble, beats
    float humanizeVelocity; // velocity wobble, 0..1
};

inline const GroovePreset* groovePresets(int& countOut) {
    static const GroovePreset PRESETS[] = {
        {"Machine",
         "Perfectly on the grid. Classic chiptune is exactly this tight -\n"
         "the hardware had no choice, and the sound is the precision.",
         0.0f, 0.5f, false, 0.02f, 0.1f},

        {"Tight",
         "On the grid, with a hair of human wobble. Still reads as\n"
         "precise; stops reading as a metronome.",
         0.0f, 0.5f, true, 0.008f, 0.06f},

        {"Loose",
         "No swing, plenty of drift. A band that rehearsed once.",
         0.0f, 0.5f, true, 0.025f, 0.18f},

        {"Swung",
         "Eighth-note swing at a classic ratio. The default answer to\n"
         "\"why does everyone else's groove feel better than mine\".",
         0.5f, 0.5f, true, 0.01f, 0.10f},

        {"Hard Swing",
         "Heavy triplet feel on the eighths. Jazz and old-school hip hop\n"
         "territory - unmistakable, and too much for most chiptune.",
         0.8f, 0.5f, true, 0.012f, 0.12f},

        {"Lofi Drag",
         "Sixteenth swing plus a lot of drift, always slightly behind.\n"
         "The deliberately imperfect feel lofi is named for.",
         0.35f, 0.25f, true, 0.03f, 0.20f},
    };

    countOut = static_cast<int>(sizeof(PRESETS) / sizeof(PRESETS[0]));
    return PRESETS;
}

// Sets only the fields the sliders below it set, so a preset is a starting
// point rather than a mode there is no way back out of.
inline void applyGroovePreset(Project& project, const GroovePreset& preset) {
    project.swing = preset.swing;
    project.swingGrid = preset.swingGrid;
    project.humanize = preset.humanize;
    project.humanizeAmount = preset.humanizeAmount;
    project.humanizeVelocity = preset.humanizeVelocity;
}

// Which preset the current settings match, or -1 when the sliders have been
// taken somewhere custom - so the UI can highlight honestly rather than
// claiming a preset that is no longer true.
inline int matchGroovePreset(const Project& project) {
    int count = 0;
    const GroovePreset* presets = groovePresets(count);
    for (int i = 0; i < count; ++i) {
        const GroovePreset& preset = presets[i];
        const bool swingMatch =
            std::fabs(project.swing - preset.swing) < 0.001f &&
            (preset.swing == 0.0f ||
             std::fabs(project.swingGrid - preset.swingGrid) < 0.001f);
        const bool humanizeMatch =
            project.humanize == preset.humanize &&
            (!preset.humanize ||
             (std::fabs(project.humanizeAmount - preset.humanizeAmount) < 0.001f &&
              std::fabs(project.humanizeVelocity - preset.humanizeVelocity) < 0.001f));
        if (swingMatch && humanizeMatch) return i;
    }
    return -1;
}

} // namespace ChiptuneTracker
