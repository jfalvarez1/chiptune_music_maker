#pragma once

/*
 * ChiptuneTracker - Instrument macro editor
 *
 * The visual editor for the step sequences described in Macros.h. Kept in
 * its own translation unit rather than added to UI.h, which is already past
 * half a megabyte; a self-contained window with one entry point is far
 * easier to reason about and to change.
 *
 * The interaction model is the one every tracker uses and nothing else
 * does: drag across a bar graph to draw the sequence. That directness is
 * the whole appeal - you can see the shape of a pluck and adjust it by
 * hand, rather than typing numbers into a table.
 */

#include "Types.h"
#include "Macros.h"
#include "Sequencer.h"

#include "imgui.h"

#include <string>
#include <algorithm>

namespace ChiptuneTracker {
namespace macroui {

// ============================================================================
// One macro lane: a draggable bar graph with loop and release markers
// ============================================================================
struct LaneSpec {
    const char* title;
    const char* help;
    int valueMin;
    int valueMax;
    ImU32 barColor;
    ImU32 barColorHot;
};

// Draws the bar graph and edits `macro` in place. Returns true if the user
// changed anything, so the caller can re-sync the channel to the synth.
inline bool DrawMacroLane(const char* id, Macro& macro, const LaneSpec& spec,
                          ArpeggioMacro* arpeggio = nullptr) {
    bool changed = false;

    ImGui::PushID(id);

    // ---- Header row ----------------------------------------------------
    if (ImGui::Checkbox("Enabled", &macro.enabled)) changed = true;
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", spec.help);

    ImGui::SameLine();
    int length = static_cast<int>(macro.steps.size());
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderInt("Length", &length, 0, Macro::MAX_STEPS)) {
        macro.steps.resize(static_cast<size_t>(std::max(0, length)),
                           macro.steps.empty() ? 0 : macro.steps.back());
        if (arpeggio) arpeggio->fixed.resize(macro.steps.size(), 0u);
        if (macro.loopStart >= length) macro.loopStart = -1;
        if (macro.releaseStep >= length) macro.releaseStep = -1;
        changed = true;
    }

    if (macro.steps.empty()) {
        ImGui::TextDisabled("Empty - drag the Length slider to add steps.");
        ImGui::PopID();
        return changed;
    }

    // ---- The bar graph -------------------------------------------------
    const int count = static_cast<int>(macro.steps.size());
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const float graphHeight = 140.0f;
    const float stepWidth = std::max(6.0f, availWidth / float(count));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 graphSize(stepWidth * count, graphHeight);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(origin,
                        ImVec2(origin.x + graphSize.x, origin.y + graphSize.y),
                        IM_COL32(18, 18, 24, 255), 3.0f);

    // Zero line, for macros that go negative
    const int range = spec.valueMax - spec.valueMin;
    const float zeroFrac = range != 0
        ? float(spec.valueMax) / float(range)
        : 0.0f;
    const float zeroY = origin.y + graphSize.y * std::clamp(zeroFrac, 0.0f, 1.0f);
    if (spec.valueMin < 0) {
        draw->AddLine(ImVec2(origin.x, zeroY), ImVec2(origin.x + graphSize.x, zeroY),
                      IM_COL32(90, 90, 110, 255));
    }

    ImGui::InvisibleButton("##graph", graphSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    int hoverIndex = -1;
    if (hovered || active) {
        const float localX = ImGui::GetIO().MousePos.x - origin.x;
        hoverIndex = std::clamp(int(localX / stepWidth), 0, count - 1);
    }

    // Drag to draw. Holding the mouse paints across steps, which is how you
    // sketch a decay curve in one gesture.
    if (active && hoverIndex >= 0) {
        const float localY = ImGui::GetIO().MousePos.y - origin.y;
        const float frac = 1.0f - std::clamp(localY / graphSize.y, 0.0f, 1.0f);
        const int value = spec.valueMin + int(std::lround(frac * float(range)));
        const int clamped = std::clamp(value, spec.valueMin, spec.valueMax);
        if (macro.steps[static_cast<size_t>(hoverIndex)] != clamped) {
            macro.steps[static_cast<size_t>(hoverIndex)] = clamped;
            changed = true;
        }
    }

    // Bars
    for (int i = 0; i < count; ++i) {
        const int value = macro.steps[static_cast<size_t>(i)];
        const float frac = range != 0
            ? float(value - spec.valueMin) / float(range)
            : 0.5f;
        const float x0 = origin.x + stepWidth * i + 1.0f;
        const float x1 = origin.x + stepWidth * (i + 1) - 1.0f;
        const float y = origin.y + graphSize.y * (1.0f - std::clamp(frac, 0.0f, 1.0f));

        const bool isHot = (i == hoverIndex);
        ImU32 color = isHot ? spec.barColorHot : spec.barColor;

        // A fixed arpeggio step is drawn in a distinct colour, because the
        // difference is audible and invisible otherwise.
        if (arpeggio && arpeggio->isFixedAt(i)) {
            color = isHot ? IM_COL32(255, 210, 120, 255) : IM_COL32(220, 160, 60, 255);
        }

        const float base = (spec.valueMin < 0) ? zeroY : origin.y + graphSize.y;
        draw->AddRectFilled(ImVec2(x0, std::min(y, base)),
                            ImVec2(x1, std::max(y, base)), color, 1.0f);
    }

    // Loop and release markers
    auto drawMarker = [&](int index, ImU32 color, const char* label) {
        if (index < 0 || index >= count) return;
        const float x = origin.x + stepWidth * index;
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + graphSize.y), color, 2.0f);
        draw->AddText(ImVec2(x + 2.0f, origin.y + 2.0f), color, label);
    };
    drawMarker(macro.loopStart, IM_COL32(120, 220, 255, 255), "L");
    drawMarker(macro.releaseStep, IM_COL32(255, 140, 200, 255), "R");

    // Live readout of the step under the cursor
    if (hovered && hoverIndex >= 0) {
        ImGui::SetTooltip("Step %d = %d%s", hoverIndex,
                          macro.steps[static_cast<size_t>(hoverIndex)],
                          (arpeggio && arpeggio->isFixedAt(hoverIndex)) ? "  (fixed)" : "");
    }

    // Right-click a step to toggle its fixed flag (arpeggio only)
    if (arpeggio && hovered && hoverIndex >= 0 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        arpeggio->fixed.resize(macro.steps.size(), 0u);
        uint8_t& flag = arpeggio->fixed[static_cast<size_t>(hoverIndex)];
        flag = flag ? 0u : 1u;
        changed = true;
    }

    // ---- Loop / release controls ---------------------------------------
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderInt("Loop point", &macro.loopStart, -1, count - 1)) changed = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Where the sequence jumps back to when it runs out.\n"
                          "-1 holds the last value instead.");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderInt("Release point", &macro.releaseStep, -1, count - 1)) changed = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Where the sequence jumps when the note is released.\n"
                          "-1 carries on from wherever it was.");
    }

    if (arpeggio) {
        ImGui::TextDisabled("Right-click a bar to make that step fixed "
                            "(absolute) instead of relative to the note.");
    }

    ImGui::PopID();
    return changed;
}

} // namespace macroui

// ============================================================================
// The window
// ============================================================================
inline void DrawMacroEditor(Project& project, UIState& ui, Sequencer& seq) {
    if (!ui.showMacroEditor) return;

    ImGui::SetNextWindowSize(ImVec2(560, 460), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(320, 200), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Instrument Macros", &ui.showMacroEditor)) {
        ImGui::End();
        return;
    }

    const int ch = std::clamp(ui.selectedChannel, 0, Project::MAX_CHANNELS - 1);
    ChannelConfig& config = project.channels[ch];
    InstrumentMacros& macros = config.macros;

    bool changed = false;

    // ---- Channel and presets -------------------------------------------
    ImGui::Text("Channel %d - %s", ch + 1, config.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (ImGui::BeginCombo("##preset", "Load preset...")) {
        for (const MacroPreset& preset : macroPresets()) {
            if (ImGui::Selectable(preset.name)) {
                macros = preset.macros;
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", preset.description);
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear all")) {
        macros.clear();
        changed = true;
    }

    // ---- Rate ------------------------------------------------------------
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::SliderFloat("Macro rate", &macros.rateHz, 1.0f, 240.0f, "%.0f steps/sec")) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How fast every macro advances.\n"
                          "60 matches the NES frame counter - the rate the\n"
                          "classic fast-arpeggio sound is tuned to.");
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("4-bit volume", &config.quantizeVolume4Bit)) changed = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Quantise output to the 16 levels real hardware had.\n"
                          "A surprisingly large part of the chiptune character.");
    }

    ImGui::Separator();

    // ---- Lanes -----------------------------------------------------------
    if (ImGui::BeginTabBar("##macroTabs")) {
        if (ImGui::BeginTabItem("Volume")) {
            ui.macroEditorTab = 0;
            const macroui::LaneSpec spec{
                "Volume", "4-bit level per step. Replaces the ADSR envelope "
                          "while it is enabled.",
                0, 15, IM_COL32(90, 200, 120, 255), IM_COL32(140, 255, 170, 255)};
            changed |= macroui::DrawMacroLane("vol", macros.volume, spec);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Arpeggio")) {
            ui.macroEditorTab = 1;
            const macroui::LaneSpec spec{
                "Arpeggio", "Semitone offsets. Cycling 0/4/7 fakes a major "
                            "chord on a single channel - the defining "
                            "chiptune trick.",
                -24, 24, IM_COL32(120, 170, 255, 255), IM_COL32(170, 210, 255, 255)};
            changed |= macroui::DrawMacroLane("arp", macros.arpeggio, spec, &macros.arpeggio);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Duty")) {
            ui.macroEditorTab = 2;
            const macroui::LaneSpec spec{
                "Duty", "Pulse width per step: 0 = 12.5%, 1 = 25%, 2 = 50%, "
                        "3 = 75%. Sweeping this gives the PWM shimmer.",
                0, 3, IM_COL32(220, 160, 90, 255), IM_COL32(255, 200, 130, 255)};
            changed |= macroui::DrawMacroLane("duty", macros.duty, spec);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Pitch")) {
            ui.macroEditorTab = 3;
            const macroui::LaneSpec spec{
                "Pitch", "Fine pitch in 1/16ths of a semitone. Use it for "
                         "drift, wobble, and the downward dive of a laser.",
                -64, 64, IM_COL32(200, 130, 220, 255), IM_COL32(235, 175, 255, 255)};
            changed |= macroui::DrawMacroLane("pitch", macros.pitch, spec);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();

    // ---- Audition --------------------------------------------------------
    if (ImGui::Button("Preview note")) {
        seq.previewNote(60, 1.0f, config.oscillator.type, 1.0f);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Plays middle C on this channel with the macros applied.");

    // Any edit has to reach the synth, or the editor would be another
    // control that looks connected and is not.
    if (changed) {
        seq.updateChannelConfigs();
    }

    ImGui::End();
}

} // namespace ChiptuneTracker
