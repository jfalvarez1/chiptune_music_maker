#pragma once

/*
 * ChiptuneTracker - Workspace layouts
 *
 * The app has nineteen windows. Left to ImGui's per-window default
 * positions they overlap, spill off the bottom of a small display, and
 * leave large empty gaps on a large one - which is the single biggest
 * reason the interface reads as unfinished, ahead of any question of
 * colour or widget styling.
 *
 * This module computes a real layout from the current display size: every
 * panel gets a place, panels tile rather than stack, and the arrangement
 * scales from a laptop to an ultrawide.
 *
 * Layouts are applied by name through ImGui::SetWindowPos/SetWindowSize,
 * which addresses a window before it is drawn. That deliberately avoids
 * touching the nineteen Draw functions - they keep their own
 * ImGuiCond_FirstUseEver defaults for the case where no layout has been
 * applied, and this overrides them when the user asks.
 *
 * Three workspaces, because the panels you want in front of you differ
 * completely between writing a part, designing a sound, and balancing a
 * mix.
 */

#include "Types.h"

#include "imgui.h"

#include <algorithm>

namespace ChiptuneTracker {

enum class Workspace : uint8_t {
    Compose,      // Piano roll front and centre, palette and pattern list
    SoundDesign,  // Channel editor, macros, wavetable, spectrum
    Mix           // Mixer, automation, spectrum, master
};

namespace layout {

// Places a window by name. Applied for one frame with ImGuiCond_Always, so
// it overrides whatever the window remembered.
inline void place(const char* name, ImVec2 pos, ImVec2 size) {
    ImGui::SetWindowPos(name, pos, ImGuiCond_Always);
    ImGui::SetWindowSize(name, size, ImGuiCond_Always);
}

// Windows that are not part of a workspace are pushed off to a consistent
// spot rather than left wherever they were, so toggling one on does not
// drop it in the middle of the piano roll.
inline void park(const char* name, ImVec2 pos, ImVec2 size) {
    place(name, pos, size);
}

struct Metrics {
    float menuHeight;
    float pad;
    float left;
    float top;
    float width;
    float height;
};

inline Metrics computeMetrics(ImVec2 display) {
    Metrics m;
    m.menuHeight = ImGui::GetFrameHeight();
    m.pad = 8.0f;
    m.left = m.pad;
    m.top = m.menuHeight + m.pad;
    m.width = std::max(640.0f, display.x - m.pad * 2.0f);
    m.height = std::max(480.0f, display.y - m.top - m.pad);
    return m;
}

} // namespace layout

// ============================================================================
// Apply a workspace layout for the current display size
// ============================================================================
inline void ApplyWorkspaceLayout(Workspace workspace, ImVec2 display) {
    using namespace layout;

    const Metrics m = computeMetrics(display);

    // A single strip along the top carries transport, file actions and the
    // view switcher. These are toolbars, not panels, and giving them a full
    // row keeps them out of the working area.
    // Not thirds: Transport holds a handful of controls, while File carries
    // seven buttons and Views five, and both were being clipped.
    const float stripHeight = 96.0f;
    const float stripSpace = m.width - m.pad * 2.0f;
    const float transportWidth = stripSpace * 0.27f;
    const float fileWidth = stripSpace * 0.38f;
    const float viewsWidth = stripSpace - transportWidth - fileWidth;

    place("Transport", ImVec2(m.left, m.top), ImVec2(transportWidth, stripHeight));
    place("File", ImVec2(m.left + transportWidth + m.pad, m.top),
          ImVec2(fileWidth, stripHeight));
    place("Views", ImVec2(m.left + transportWidth + fileWidth + m.pad * 2.0f, m.top),
          ImVec2(viewsWidth, stripHeight));

    const float bodyTop = m.top + stripHeight + m.pad;
    const float bodyHeight = m.height - stripHeight - m.pad;

    // Side column widths scale with the display so an ultrawide gives the
    // extra space to the editor rather than to the inspector panels.
    const float sideWidth = std::clamp(display.x * 0.17f, 210.0f, 320.0f);

    switch (workspace) {
        case Workspace::Compose: {
            // Left: sound palette above the pattern list.
            const float paletteHeight = bodyHeight * 0.62f;
            place("Sound Palette", ImVec2(m.left, bodyTop),
                  ImVec2(sideWidth, paletteHeight));
            place("Patterns", ImVec2(m.left, bodyTop + paletteHeight + m.pad),
                  ImVec2(sideWidth, bodyHeight - paletteHeight - m.pad));

            // Right: note editor above the channel editor.
            const float rightX = m.left + m.width - sideWidth;
            const float noteHeight = bodyHeight * 0.34f;
            place("Note Editor", ImVec2(rightX, bodyTop),
                  ImVec2(sideWidth, noteHeight));
            place("Channel Editor", ImVec2(rightX, bodyTop + noteHeight + m.pad),
                  ImVec2(sideWidth, bodyHeight - noteHeight - m.pad));

            // Centre: the editor itself, filling everything left over.
            const float centreX = m.left + sideWidth + m.pad;
            const float centreWidth = m.width - sideWidth * 2.0f - m.pad * 2.0f;
            for (const char* view : {"Piano Roll", "Tracker", "Arrangement",
                                     "Mixer", "Pad Controller"}) {
                place(view, ImVec2(centreX, bodyTop), ImVec2(centreWidth, bodyHeight));
            }

            // Panels not in this workspace sit centred, ready to be opened.
            park("Tools", ImVec2(centreX + centreWidth * 0.25f, bodyTop + 40.0f),
                 ImVec2(centreWidth * 0.5f, bodyHeight * 0.6f));
            park("Instrument Macros", ImVec2(centreX + 40.0f, bodyTop + 40.0f),
                 ImVec2(std::min(620.0f, centreWidth), std::min(470.0f, bodyHeight)));
            park("Wavetable Editor", ImVec2(centreX + 80.0f, bodyTop + 60.0f),
                 ImVec2(std::min(560.0f, centreWidth), std::min(520.0f, bodyHeight)));
            park("Spectrum Analyzer", ImVec2(centreX + 120.0f, bodyTop + 80.0f),
                 ImVec2(std::min(520.0f, centreWidth), 260.0f));
            park("Automation", ImVec2(centreX + 60.0f, bodyTop + 100.0f),
                 ImVec2(std::min(600.0f, centreWidth), 300.0f));
            park("MIDI Input", ImVec2(centreX + 160.0f, bodyTop + 120.0f),
                 ImVec2(360.0f, 280.0f));
            break;
        }

        case Workspace::SoundDesign: {
            // Sound design wants the instrument editors large and the score
            // small - you are listening to one note, not reading a part.
            const float colWidth = (m.width - m.pad * 2.0f) / 3.0f;
            const float topHeight = bodyHeight * 0.56f;
            const float bottomHeight = bodyHeight - topHeight - m.pad;

            place("Channel Editor", ImVec2(m.left, bodyTop),
                  ImVec2(colWidth, topHeight));
            place("Instrument Macros", ImVec2(m.left + colWidth + m.pad, bodyTop),
                  ImVec2(colWidth, topHeight));
            place("Wavetable Editor",
                  ImVec2(m.left + (colWidth + m.pad) * 2.0f, bodyTop),
                  ImVec2(colWidth, topHeight));

            const float bottomTop = bodyTop + topHeight + m.pad;
            place("Sound Palette", ImVec2(m.left, bottomTop),
                  ImVec2(colWidth, bottomHeight));
            place("Spectrum Analyzer", ImVec2(m.left + colWidth + m.pad, bottomTop),
                  ImVec2(colWidth, bottomHeight));

            const float lastX = m.left + (colWidth + m.pad) * 2.0f;
            for (const char* view : {"Piano Roll", "Tracker", "Arrangement",
                                     "Mixer", "Pad Controller"}) {
                place(view, ImVec2(lastX, bottomTop), ImVec2(colWidth, bottomHeight));
            }

            park("Patterns", ImVec2(m.left + 40.0f, bodyTop + 40.0f),
                 ImVec2(240.0f, 300.0f));
            park("Note Editor", ImVec2(m.left + 80.0f, bodyTop + 60.0f),
                 ImVec2(300.0f, 320.0f));
            park("Tools", ImVec2(m.left + 120.0f, bodyTop + 80.0f),
                 ImVec2(420.0f, 400.0f));
            park("Automation", ImVec2(m.left + 160.0f, bodyTop + 100.0f),
                 ImVec2(560.0f, 300.0f));
            park("MIDI Input", ImVec2(m.left + 200.0f, bodyTop + 120.0f),
                 ImVec2(360.0f, 280.0f));
            break;
        }

        case Workspace::Mix: {
            // Mixing wants the meters wide and the analysers visible.
            const float mixerHeight = bodyHeight * 0.46f;
            const float lowerTop = bodyTop + mixerHeight + m.pad;
            const float lowerHeight = bodyHeight - mixerHeight - m.pad;
            const float halfWidth = (m.width - m.pad) * 0.5f;

            place("Mixer", ImVec2(m.left, bodyTop), ImVec2(m.width, mixerHeight));

            place("Spectrum Analyzer", ImVec2(m.left, lowerTop),
                  ImVec2(halfWidth, lowerHeight));
            place("Automation", ImVec2(m.left + halfWidth + m.pad, lowerTop),
                  ImVec2(halfWidth, lowerHeight));

            place("Channel Editor", ImVec2(m.left + m.width - 320.0f, bodyTop + 40.0f),
                  ImVec2(310.0f, mixerHeight - 80.0f));

            for (const char* view : {"Piano Roll", "Tracker", "Arrangement",
                                     "Pad Controller"}) {
                park(view, ImVec2(m.left + 60.0f, bodyTop + 40.0f),
                     ImVec2(m.width * 0.7f, bodyHeight * 0.7f));
            }
            park("Sound Palette", ImVec2(m.left + 40.0f, bodyTop + 40.0f),
                 ImVec2(240.0f, 400.0f));
            park("Patterns", ImVec2(m.left + 80.0f, bodyTop + 60.0f),
                 ImVec2(240.0f, 300.0f));
            park("Note Editor", ImVec2(m.left + 120.0f, bodyTop + 80.0f),
                 ImVec2(300.0f, 320.0f));
            park("Tools", ImVec2(m.left + 160.0f, bodyTop + 100.0f),
                 ImVec2(420.0f, 400.0f));
            park("Instrument Macros", ImVec2(m.left + 200.0f, bodyTop + 120.0f),
                 ImVec2(560.0f, 440.0f));
            park("Wavetable Editor", ImVec2(m.left + 240.0f, bodyTop + 140.0f),
                 ImVec2(520.0f, 460.0f));
            park("MIDI Input", ImVec2(m.left + 280.0f, bodyTop + 160.0f),
                 ImVec2(360.0f, 280.0f));
            break;
        }
    }

    // These two are standalone tools that never belong in a workspace grid.
    park("Voice to Note Converter", ImVec2(m.left + 200.0f, m.top + 120.0f),
         ImVec2(520.0f, 420.0f));
}

inline const char* WorkspaceName(Workspace workspace) {
    switch (workspace) {
        case Workspace::Compose:     return "Compose";
        case Workspace::SoundDesign: return "Sound Design";
        case Workspace::Mix:         return "Mix";
    }
    return "Compose";
}

} // namespace ChiptuneTracker
