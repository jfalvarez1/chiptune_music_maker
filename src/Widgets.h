#pragma once

/*
 * ChiptuneTracker - Custom widget vocabulary
 *
 * Stock ImGui controls are excellent for tools and unmistakable for what
 * they are. An audio application needs its own hardware-derived vocabulary:
 * knobs that read at a glance, meters that show peak and hold, toggles that
 * look switched rather than ticked.
 *
 * Everything here draws on the ImDrawList, so it inherits the active
 * theme's accent colour and works under all of them. Each widget follows
 * the ImGui contract - an id, a rect, an InvisibleButton for interaction,
 * and a bool return meaning "the value changed this frame" - so they drop
 * into existing layouts without ceremony.
 *
 * Kept separate from UI.h deliberately. These are reusable primitives with
 * no knowledge of Project, Sequencer or any application state, and they
 * should stay that way.
 */

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <string>

namespace ChiptuneTracker {
namespace widgets {

// ============================================================================
// Colour helpers
// ============================================================================

inline ImU32 withAlpha(ImU32 color, float alpha) {
    const ImU32 a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return (color & 0x00FFFFFFu) | (a << IM_COL32_A_SHIFT);
}

inline ImU32 lerpColor(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        ca.x + (cb.x - ca.x) * t,
        ca.y + (cb.y - ca.y) * t,
        ca.z + (cb.z - ca.z) * t,
        ca.w + (cb.w - ca.w) * t));
}

// The theme's accent, taken from the button colour so widgets follow along
// when the user switches themes.
inline ImU32 accentColor() {
    return ImGui::GetColorU32(ImGuiCol_ButtonActive);
}

// A soft outer glow. Concentric rounded rects rather than a real blur -
// cheap, and at these sizes indistinguishable from one.
inline void drawGlow(ImDrawList* draw, ImVec2 min, ImVec2 max,
                     ImU32 color, float rounding, float strength = 1.0f, int layers = 4) {
    for (int i = layers; i > 0; --i) {
        const float spread = float(i) * 1.6f;
        const float alpha = 0.10f * strength / float(i);
        draw->AddRect(ImVec2(min.x - spread, min.y - spread),
                      ImVec2(max.x + spread, max.y + spread),
                      withAlpha(color, alpha), rounding + spread, 0, 1.6f);
    }
}

// ============================================================================
// Knob
//
// Vertical drag, the convention every plugin uses. Shift for fine control,
// double-click to return to the default.
// ============================================================================
inline bool Knob(const char* label, float* value, float min, float max,
                 float defaultValue, float radius = 22.0f,
                 const char* format = "%.2f") {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImGui::PushID(label);

    const float labelHeight = ImGui::GetTextLineHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float diameter = radius * 2.0f;

    ImGui::InvisibleButton("##knob", ImVec2(diameter, diameter + labelHeight * 2.0f + 4.0f));

    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    bool changed = false;

    if (active && io.MouseDelta.y != 0.0f) {
        // 200px of travel covers the full range; shift slows it by 5x for
        // the fine adjustments that matter on a filter cutoff.
        const float speed = (max - min) / (io.KeyShift ? 1000.0f : 200.0f);
        *value = std::clamp(*value - io.MouseDelta.y * speed, min, max);
        changed = true;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *value = defaultValue;
        changed = true;
    }

    const ImVec2 center(origin.x + radius, origin.y + radius);
    const float t = (max > min) ? std::clamp((*value - min) / (max - min), 0.0f, 1.0f) : 0.0f;

    // Sweep from 135 degrees round to 405, the standard knob range
    constexpr float ANGLE_MIN = 2.356194f;   // 135 deg
    constexpr float ANGLE_MAX = 7.068583f;   // 405 deg
    const float angle = ANGLE_MIN + (ANGLE_MAX - ANGLE_MIN) * t;

    const ImU32 accent = accentColor();
    const ImU32 trackColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 bodyTop = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
    const ImU32 bodyBottom = ImGui::GetColorU32(ImGuiCol_FrameBg);

    if (active || hovered) {
        drawGlow(draw, ImVec2(center.x - radius, center.y - radius),
                 ImVec2(center.x + radius, center.y + radius),
                 accent, radius, active ? 1.4f : 0.8f);
    }

    // Track
    draw->PathArcTo(center, radius - 2.0f, ANGLE_MIN, ANGLE_MAX, 48);
    draw->PathStroke(trackColor, 0, 4.0f);

    // Filled portion
    draw->PathArcTo(center, radius - 2.0f, ANGLE_MIN, angle, 48);
    draw->PathStroke(accent, 0, 4.0f);

    // Body, with a vertical gradient so it reads as a physical cap
    const float bodyRadius = radius - 7.0f;
    draw->AddCircleFilled(center, bodyRadius, bodyBottom, 32);
    draw->AddRectFilledMultiColor(
        ImVec2(center.x - bodyRadius, center.y - bodyRadius),
        ImVec2(center.x + bodyRadius, center.y),
        withAlpha(bodyTop, 0.55f), withAlpha(bodyTop, 0.55f),
        withAlpha(bodyTop, 0.0f), withAlpha(bodyTop, 0.0f));
    draw->AddCircle(center, bodyRadius, withAlpha(accent, 0.35f), 32, 1.0f);

    // Pointer
    const ImVec2 tip(center.x + std::cos(angle) * (bodyRadius - 3.0f),
                     center.y + std::sin(angle) * (bodyRadius - 3.0f));
    const ImVec2 tail(center.x + std::cos(angle) * (bodyRadius * 0.35f),
                      center.y + std::sin(angle) * (bodyRadius * 0.35f));
    draw->AddLine(tail, tip, IM_COL32(255, 255, 255, 230), 2.4f);

    // Label and value
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    char valueText[64];
    std::snprintf(valueText, sizeof(valueText), format, *value);

    auto centeredText = [&](const char* text, float y, ImU32 color) {
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(center.x - size.x * 0.5f, y), color, text);
    };
    centeredText(label, origin.y + diameter + 2.0f, dimColor);
    centeredText(valueText, origin.y + diameter + 2.0f + labelHeight, textColor);

    ImGui::PopID();
    return changed;
}

// ============================================================================
// Level meter
//
// Green through amber to red, with a peak line that falls back slowly -
// the behaviour every mixer has, because an instantaneous bar is unreadable
// at audio rates.
// ============================================================================
struct MeterState {
    float displayLevel = 0.0f;
    float peakLevel = 0.0f;
    float peakHoldTime = 0.0f;
};

inline void LevelMeter(const char* label, float level, MeterState& state,
                       ImVec2 size = ImVec2(14.0f, 120.0f), bool vertical = true) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    level = std::isfinite(level) ? std::clamp(level, 0.0f, 1.5f) : 0.0f;

    // Fast attack, slow release: the ear's own behaviour, and it makes
    // transients visible without the bar flickering.
    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    if (level > state.displayLevel) {
        state.displayLevel = level;
    } else {
        state.displayLevel += (level - state.displayLevel) * std::min(1.0f, dt * 12.0f);
    }

    if (state.displayLevel >= state.peakLevel) {
        state.peakLevel = state.displayLevel;
        state.peakHoldTime = 1.2f;
    } else {
        state.peakHoldTime -= dt;
        if (state.peakHoldTime <= 0.0f) {
            state.peakLevel += (state.displayLevel - state.peakLevel) * std::min(1.0f, dt * 3.0f);
        }
    }

    ImGui::PushID(label);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##meter", size);

    const ImVec2 min = origin;
    const ImVec2 max(origin.x + size.x, origin.y + size.y);

    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);

    auto colorAt = [](float v) {
        if (v < 0.6f) return lerpColor(IM_COL32(60, 200, 110, 255), IM_COL32(180, 220, 70, 255), v / 0.6f);
        if (v < 0.9f) return lerpColor(IM_COL32(180, 220, 70, 255), IM_COL32(240, 170, 50, 255), (v - 0.6f) / 0.3f);
        return lerpColor(IM_COL32(240, 170, 50, 255), IM_COL32(240, 70, 60, 255),
                         std::min(1.0f, (v - 0.9f) / 0.1f));
    };

    const float fill = std::clamp(state.displayLevel, 0.0f, 1.0f);
    if (fill > 0.001f) {
        if (vertical) {
            const float y = max.y - size.y * fill;
            draw->AddRectFilledMultiColor(
                ImVec2(min.x + 1.0f, y), ImVec2(max.x - 1.0f, max.y - 1.0f),
                colorAt(fill), colorAt(fill), colorAt(0.0f), colorAt(0.0f));
        } else {
            const float x = min.x + size.x * fill;
            draw->AddRectFilledMultiColor(
                ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(x, max.y - 1.0f),
                colorAt(0.0f), colorAt(fill), colorAt(fill), colorAt(0.0f));
        }
    }

    // Peak line, and a glow when it is clipping
    const float peak = std::clamp(state.peakLevel, 0.0f, 1.0f);
    if (peak > 0.01f) {
        const ImU32 peakColor = state.peakLevel >= 0.99f
            ? IM_COL32(255, 90, 80, 255) : IM_COL32(240, 240, 245, 200);
        if (vertical) {
            const float y = max.y - size.y * peak;
            draw->AddLine(ImVec2(min.x + 1.0f, y), ImVec2(max.x - 1.0f, y), peakColor, 2.0f);
        } else {
            const float x = min.x + size.x * peak;
            draw->AddLine(ImVec2(x, min.y + 1.0f), ImVec2(x, max.y - 1.0f), peakColor, 2.0f);
        }
        if (state.peakLevel >= 0.99f) {
            drawGlow(draw, min, max, IM_COL32(255, 70, 60, 255), 3.0f, 1.2f, 3);
        }
    }

    draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), 3.0f);
    ImGui::PopID();
}

// ============================================================================
// Toggle switch
//
// Reads as on or off from across the room, which a checkbox does not.
// ============================================================================
inline bool ToggleSwitch(const char* label, bool* value, float width = 42.0f) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::PushID(label);

    const float height = ImGui::GetFrameHeight() * 0.82f;
    const float radius = height * 0.5f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##toggle", ImVec2(width, height));
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        *value = !*value;
        changed = true;
    }

    // Animate the knob rather than snapping - the motion is what makes the
    // state change legible.
    const float target = *value ? 1.0f : 0.0f;
    const float storageKey = ImGui::GetID("##anim") * 1.0f;
    (void)storageKey;
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID animId = ImGui::GetID("##animpos");
    float t = storage->GetFloat(animId, target);
    t += (target - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
    if (std::fabs(t - target) < 0.002f) t = target;
    storage->SetFloat(animId, t);

    // The off state needs a visibly lighter track than the window behind it,
    // or the widget reads as a lone circle - a radio button, not a switch.
    const ImU32 offColor = lerpColor(ImGui::GetColorU32(ImGuiCol_FrameBg),
                                     IM_COL32(255, 255, 255, 255), 0.10f);
    const ImU32 onColor = accentColor();
    const ImU32 track = lerpColor(offColor, onColor, t);

    if (t > 0.05f) {
        drawGlow(draw, origin, ImVec2(origin.x + width, origin.y + height),
                 onColor, radius, t, 3);
    }

    const ImVec2 trackMax(origin.x + width, origin.y + height);
    draw->AddRectFilled(origin, trackMax, track, radius);
    draw->AddRect(origin, trackMax,
                  lerpColor(ImGui::GetColorU32(ImGuiCol_Border), onColor, t),
                  radius, 0, 1.5f);

    // The knob dims when off so the two states differ in more than position
    const float knobX = origin.x + radius + (width - height) * t;
    draw->AddCircleFilled(ImVec2(knobX, origin.y + radius), radius - 2.5f,
                          lerpColor(IM_COL32(150, 152, 162, 255),
                                    IM_COL32(250, 250, 254, 255), t), 24);

    // Label to the right, as with a checkbox
    if (label && label[0] != '\0' && label[0] != '#') {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
    }

    ImGui::PopID();
    return changed;
}

// ============================================================================
// Section heading
//
// A labelled rule. Cheap, and it does more for the sense of structure in a
// dense panel than another collapsing header would.
// ============================================================================
inline void SectionHeader(const char* label) {
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImU32 accent = accentColor();

    // Accent bar to the left of the text
    draw->AddRectFilled(ImVec2(pos.x, pos.y + 2.0f),
                        ImVec2(pos.x + 3.0f, pos.y + ImGui::GetTextLineHeight()),
                        accent, 1.5f);

    ImGui::Indent(9.0f);
    ImGui::TextUnformatted(label);
    ImGui::Unindent(9.0f);

    const ImVec2 after = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    draw->AddRectFilledMultiColor(
        ImVec2(after.x, after.y - 2.0f), ImVec2(after.x + width, after.y - 1.0f),
        withAlpha(accent, 0.5f), withAlpha(accent, 0.0f),
        withAlpha(accent, 0.0f), withAlpha(accent, 0.5f));

    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

// ============================================================================
// Accent button
//
// A gradient fill and a glow on hover. Use it for the one primary action in
// a panel - if everything is accented, nothing is.
// ============================================================================
inline bool AccentButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (size.x <= 0.0f) {
        size.x = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }
    if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    ImGui::InvisibleButton("##accent", size);
    const bool pressed = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const ImU32 accent = accentColor();
    const ImU32 top = held ? lerpColor(accent, IM_COL32(0, 0, 0, 255), 0.2f)
                           : (hovered ? lerpColor(accent, IM_COL32(255, 255, 255, 255), 0.18f)
                                      : accent);
    const ImU32 bottom = lerpColor(top, IM_COL32(0, 0, 0, 255), 0.28f);

    const ImVec2 max(origin.x + size.x, origin.y + size.y);
    const float rounding = ImGui::GetStyle().FrameRounding;

    if (hovered) drawGlow(draw, origin, max, accent, rounding, held ? 1.5f : 1.0f);

    draw->AddRectFilledMultiColor(origin, max, top, top, bottom, bottom);
    draw->AddRect(origin, max, withAlpha(IM_COL32(255, 255, 255, 255), 0.18f), rounding);

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(origin.x + (size.x - textSize.x) * 0.5f,
                         origin.y + (size.y - textSize.y) * 0.5f),
                  IM_COL32(255, 255, 255, 240), label);

    ImGui::PopID();
    return pressed;
}

} // namespace widgets
} // namespace ChiptuneTracker
