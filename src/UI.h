#pragma once

/*
 * ChiptuneTracker - UI Components
 *
 * ImGui-based views for the DAW:
 *   - Piano Roll Editor
 *   - Tracker View
 *   - Arrangement Timeline
 *   - Mixer
 *   - Effects Rack
 */

#include "imgui.h"
#include "Types.h"
#include "Sequencer.h"
#include "FileIO.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace ChiptuneTracker {

// Note names for display
const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

inline std::string noteToString(int midiNote) {
    int octave = midiNote / 12 - 1;
    int note = midiNote % 12;
    char buf[8];
    snprintf(buf, sizeof(buf), "%s%d", NOTE_NAMES[note], octave);
    return buf;
}

// Colors for channels
const ImU32 CHANNEL_COLORS[] = {
    IM_COL32(255, 100, 100, 255),  // Pulse 1 - Red
    IM_COL32(100, 255, 100, 255),  // Pulse 2 - Green
    IM_COL32(100, 100, 255, 255),  // Triangle - Blue
    IM_COL32(255, 255, 100, 255),  // Sawtooth - Yellow
    IM_COL32(255, 100, 255, 255),  // Sine - Magenta
    IM_COL32(100, 255, 255, 255),  // Noise - Cyan
    IM_COL32(255, 180, 100, 255),  // Pulse 3 - Orange
    IM_COL32(180, 100, 255, 255),  // Custom - Purple
};

// Global clipboard for copy/paste notes (stores relative positions)
static std::vector<Note> g_NoteClipboard;
static float g_ClipboardBaseTime = 0.0f;
static int g_ClipboardBasePitch = 60;

// Global drag state for oscillator type
static int g_DraggedOscillatorType = -1;

// Global state for selected palette item (for click-to-place)
static int g_SelectedPaletteItem = -1;  // -1 = none selected

// Global undo/redo history
static UndoHistory g_UndoHistory;

// ============================================================================
// Theme System
// ============================================================================

// Matrix rain effect state
struct MatrixColumn {
    float y = 0.0f;
    float speed = 0.0f;
    int length = 0;
    std::string chars;
};
static std::vector<MatrixColumn> g_MatrixColumns;
static bool g_MatrixInitialized = false;

// Synthwave chaser state
static float g_ChaserOffset = 0.0f;
static float g_ChaserColorPhase = 0.0f;  // For color cycling
static float g_CyberpunkPulse = 0.0f;    // For Cyberpunk pulsing effects
static float g_DataStreamOffset = 0.0f;  // For Cyberpunk data streams

// Theme-specific colors for piano roll
struct ThemePianoRollColors {
    ImU32 keyWhite;
    ImU32 keyBlack;
    ImU32 gridLine;
    ImU32 gridLineMeasure;
    ImU32 gridLinePattern;
    ImU32 noteDefault;
    ImU32 noteSelected;
    ImU32 playhead;
    ImU32 background;
};

static ThemePianoRollColors g_PianoRollColors;

// Apply a theme to ImGui style
inline void ApplyTheme(Theme theme) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Common style settings
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 2.0f;
    style.FrameBorderSize = 1.0f;

    switch (theme) {
        case Theme::Stock:
            // Default dark theme
            colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.33f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.20f, 0.35f, 0.55f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.45f, 0.70f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.52f, 0.80f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.20f, 0.35f, 0.55f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.45f, 0.70f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.52f, 0.80f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.52f, 0.80f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.62f, 0.90f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.25f, 0.40f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.45f, 0.70f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.38f, 0.60f, 1.00f);

            // Piano roll colors
            g_PianoRollColors.keyWhite = IM_COL32(60, 60, 65, 255);
            g_PianoRollColors.keyBlack = IM_COL32(40, 40, 45, 255);
            g_PianoRollColors.gridLine = IM_COL32(50, 50, 55, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(100, 100, 110, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(150, 80, 80, 255);
            g_PianoRollColors.noteDefault = IM_COL32(80, 140, 200, 255);
            g_PianoRollColors.noteSelected = IM_COL32(100, 200, 255, 255);
            g_PianoRollColors.playhead = IM_COL32(255, 100, 100, 255);
            g_PianoRollColors.background = IM_COL32(30, 30, 35, 255);
            break;

        case Theme::Cyberpunk:
            // Cyberpunk 2077 inspired - neon yellow, hot pink, electric blue
            colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.04f, 0.07f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.03f, 0.03f, 0.05f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.06f, 0.10f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.99f, 0.93f, 0.04f, 0.50f);  // Neon yellow
            colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.08f, 0.20f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.10f, 0.25f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.02f, 0.10f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.50f, 0.05f, 0.30f, 1.00f);  // Hot pink
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.04f, 0.12f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(1.00f, 0.16f, 0.43f, 0.60f);  // Hot pink
            colors[ImGuiCol_HeaderHovered] = ImVec4(1.00f, 0.16f, 0.43f, 0.80f);
            colors[ImGuiCol_HeaderActive] = ImVec4(1.00f, 0.16f, 0.43f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.02f, 0.85f, 0.91f, 0.40f);  // Cyan
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.02f, 0.85f, 0.91f, 0.70f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.99f, 0.93f, 0.04f, 0.90f);  // Yellow on click
            colors[ImGuiCol_SliderGrab] = ImVec4(0.99f, 0.93f, 0.04f, 1.00f);  // Neon yellow
            colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.16f, 0.43f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.99f, 0.93f, 0.04f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.00f, 1.00f, 0.98f, 1.00f);  // Cyan text
            colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.50f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.30f, 0.05f, 0.20f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(1.00f, 0.16f, 0.43f, 0.80f);
            colors[ImGuiCol_TabActive] = ImVec4(0.50f, 0.10f, 0.35f, 1.00f);

            // Piano roll colors - Cyberpunk
            g_PianoRollColors.keyWhite = IM_COL32(30, 30, 50, 255);
            g_PianoRollColors.keyBlack = IM_COL32(15, 15, 30, 255);
            g_PianoRollColors.gridLine = IM_COL32(60, 20, 80, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(255, 40, 110, 200);  // Hot pink
            g_PianoRollColors.gridLinePattern = IM_COL32(252, 238, 10, 255);  // Yellow
            g_PianoRollColors.noteDefault = IM_COL32(5, 217, 232, 255);  // Cyan
            g_PianoRollColors.noteSelected = IM_COL32(252, 238, 10, 255);  // Yellow
            g_PianoRollColors.playhead = IM_COL32(255, 42, 109, 255);  // Hot pink
            g_PianoRollColors.background = IM_COL32(10, 10, 18, 255);
            break;

        case Theme::Synthwave:
            // 80s Synthwave - sunset colors, neon pink/purple/cyan
            colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.05f, 0.15f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.04f, 0.12f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.06f, 0.18f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(1.00f, 0.00f, 1.00f, 0.50f);  // Magenta
            colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.05f, 0.20f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.10f, 0.35f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.15f, 0.45f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.02f, 0.12f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.40f, 0.00f, 0.50f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.04f, 0.18f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.60f, 0.00f, 0.80f, 0.60f);  // Purple
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.80f, 0.00f, 1.00f, 0.80f);
            colors[ImGuiCol_HeaderActive] = ImVec4(1.00f, 0.00f, 1.00f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(1.00f, 0.00f, 0.60f, 0.50f);  // Hot pink
            colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 0.20f, 0.80f, 0.70f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.90f);  // Cyan
            colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);  // Cyan
            colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.00f, 1.00f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(1.00f, 0.90f, 1.00f, 1.00f);  // Soft pink-white
            colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.40f, 0.60f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.30f, 0.00f, 0.40f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.80f, 0.00f, 1.00f, 0.80f);
            colors[ImGuiCol_TabActive] = ImVec4(0.50f, 0.00f, 0.65f, 1.00f);

            // Piano roll colors - Synthwave
            g_PianoRollColors.keyWhite = IM_COL32(40, 20, 60, 255);
            g_PianoRollColors.keyBlack = IM_COL32(25, 10, 40, 255);
            g_PianoRollColors.gridLine = IM_COL32(80, 0, 120, 150);
            g_PianoRollColors.gridLineMeasure = IM_COL32(255, 0, 255, 200);  // Magenta
            g_PianoRollColors.gridLinePattern = IM_COL32(0, 255, 255, 255);  // Cyan
            g_PianoRollColors.noteDefault = IM_COL32(255, 0, 150, 255);  // Hot pink
            g_PianoRollColors.noteSelected = IM_COL32(0, 255, 255, 255);  // Cyan
            g_PianoRollColors.playhead = IM_COL32(255, 100, 0, 255);  // Orange
            g_PianoRollColors.background = IM_COL32(20, 10, 35, 255);
            break;

        case Theme::Matrix:
            // The Matrix - green on black
            colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.02f, 0.00f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.01f, 0.00f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.00f, 0.04f, 0.00f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.00f, 0.60f, 0.00f, 0.50f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.00f, 0.08f, 0.00f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.00f, 0.15f, 0.00f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.00f, 0.22f, 0.00f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.04f, 0.00f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.00f, 0.20f, 0.00f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.06f, 0.00f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.00f, 0.40f, 0.00f, 0.60f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.55f, 0.00f, 0.80f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.70f, 0.00f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.00f, 0.30f, 0.00f, 0.60f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.50f, 0.00f, 0.80f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.70f, 0.00f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.80f, 0.00f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 1.00f, 0.00f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 0.00f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.00f, 1.00f, 0.25f, 1.00f);  // Matrix green
            colors[ImGuiCol_TextDisabled] = ImVec4(0.00f, 0.40f, 0.10f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.15f, 0.00f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.00f, 0.40f, 0.00f, 0.80f);
            colors[ImGuiCol_TabActive] = ImVec4(0.00f, 0.25f, 0.00f, 1.00f);

            // Piano roll colors - Matrix
            g_PianoRollColors.keyWhite = IM_COL32(0, 30, 0, 255);
            g_PianoRollColors.keyBlack = IM_COL32(0, 15, 0, 255);
            g_PianoRollColors.gridLine = IM_COL32(0, 50, 0, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(0, 120, 0, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(0, 200, 0, 255);
            g_PianoRollColors.noteDefault = IM_COL32(0, 200, 50, 255);
            g_PianoRollColors.noteSelected = IM_COL32(0, 255, 100, 255);
            g_PianoRollColors.playhead = IM_COL32(50, 255, 50, 255);
            g_PianoRollColors.background = IM_COL32(0, 5, 0, 255);
            break;
    }
}

// Initialize Matrix rain effect
inline void InitMatrixRain(int screenWidth) {
    if (g_MatrixInitialized) return;

    const char* matrixChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%^&*()";
    int numChars = static_cast<int>(strlen(matrixChars));
    int columnWidth = 14;
    int numColumns = screenWidth / columnWidth + 1;

    g_MatrixColumns.resize(numColumns);
    for (int i = 0; i < numColumns; ++i) {
        g_MatrixColumns[i].y = static_cast<float>(rand() % 800);
        g_MatrixColumns[i].speed = 50.0f + static_cast<float>(rand() % 150);
        g_MatrixColumns[i].length = 5 + rand() % 20;
        g_MatrixColumns[i].chars.clear();
        for (int j = 0; j < g_MatrixColumns[i].length; ++j) {
            g_MatrixColumns[i].chars += matrixChars[rand() % numChars];
        }
    }
    g_MatrixInitialized = true;
}

// Draw Matrix rain background effect
inline void DrawMatrixRain(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    InitMatrixRain(static_cast<int>(screenSize.x));

    int columnWidth = 14;
    int charHeight = 16;

    for (size_t i = 0; i < g_MatrixColumns.size(); ++i) {
        MatrixColumn& col = g_MatrixColumns[i];

        // Update position
        col.y += col.speed * deltaTime;
        if (col.y > screenSize.y + col.length * charHeight) {
            col.y = -static_cast<float>(col.length * charHeight);
            col.speed = 50.0f + static_cast<float>(rand() % 150);

            // Randomize characters
            const char* matrixChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%^&*()";
            int numChars = static_cast<int>(strlen(matrixChars));
            for (int j = 0; j < col.length; ++j) {
                col.chars[j] = matrixChars[rand() % numChars];
            }
        }

        // Draw characters with fading trail
        float x = static_cast<float>(i * columnWidth);
        for (int j = 0; j < col.length; ++j) {
            float y = col.y - j * charHeight;
            if (y < 0 || y > screenSize.y) continue;

            // Brightness fades towards the tail
            float brightness = 1.0f - static_cast<float>(j) / static_cast<float>(col.length);
            int green = static_cast<int>(255 * brightness);
            int alpha = static_cast<int>(200 * brightness);

            // Head of trail is brightest (white-ish)
            ImU32 color;
            if (j == 0) {
                color = IM_COL32(200, 255, 200, 255);
            } else {
                color = IM_COL32(0, green, green / 4, alpha);
            }

            char charStr[2] = {col.chars[j], '\0'};
            drawList->AddText(ImVec2(x, y), color, charStr);
        }
    }
}

// Draw Synthwave chaser lights effect
// Helper: Get neon color based on phase (cycles through pink, cyan, green)
inline ImU32 GetNeonColor(float phase, float brightness) {
    // 3 colors: Neon Pink (255, 20, 147), Neon Cyan (0, 255, 255), Neon Green (57, 255, 20)
    float p = std::fmod(phase, 3.0f);
    int r, g, b;

    if (p < 1.0f) {
        // Pink to Cyan
        float t = p;
        r = static_cast<int>((255 * (1 - t) + 0 * t) * brightness);
        g = static_cast<int>((20 * (1 - t) + 255 * t) * brightness);
        b = static_cast<int>((147 * (1 - t) + 255 * t) * brightness);
    } else if (p < 2.0f) {
        // Cyan to Green
        float t = p - 1.0f;
        r = static_cast<int>((0 * (1 - t) + 57 * t) * brightness);
        g = static_cast<int>((255 * (1 - t) + 255 * t) * brightness);
        b = static_cast<int>((255 * (1 - t) + 20 * t) * brightness);
    } else {
        // Green to Pink
        float t = p - 2.0f;
        r = static_cast<int>((57 * (1 - t) + 255 * t) * brightness);
        g = static_cast<int>((255 * (1 - t) + 20 * t) * brightness);
        b = static_cast<int>((20 * (1 - t) + 147 * t) * brightness);
    }

    return IM_COL32(r, g, b, static_cast<int>(255 * brightness));
}

inline void DrawSynthwaveChasers(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    // Slower chaser animation
    g_ChaserOffset += deltaTime * 40.0f;  // Much slower (was 200)
    if (g_ChaserOffset > 1000.0f) g_ChaserOffset -= 1000.0f;

    // Color cycling (very slow)
    g_ChaserColorPhase += deltaTime * 0.3f;
    if (g_ChaserColorPhase > 3.0f) g_ChaserColorPhase -= 3.0f;

    // ========================================================================
    // Large Sunset Background (covers bottom half of screen)
    // ========================================================================
    float sunRadius = screenSize.x * 0.4f;  // Much larger sun
    float sunY = screenSize.y + sunRadius * 0.15f;  // Peek more above horizon
    float sunX = screenSize.x * 0.5f;
    float horizonY = screenSize.y * 0.6f;

    // Sky gradient (dark purple to orange near horizon)
    for (int i = 0; i < 40; ++i) {
        float t = static_cast<float>(i) / 40.0f;
        float y1 = horizonY * t;
        float y2 = horizonY * (t + 1.0f / 40.0f);

        // Purple at top, transitioning to orange/pink at horizon
        int r = static_cast<int>(20 + 80 * t);
        int g = static_cast<int>(10 + 20 * t);
        int b = static_cast<int>(40 + 60 * (1.0f - t * 0.5f));

        drawList->AddRectFilledMultiColor(
            ImVec2(0, y1), ImVec2(screenSize.x, y2),
            IM_COL32(r, g, b, 40), IM_COL32(r, g, b, 40),
            IM_COL32(r + 20, g + 10, b - 10, 50), IM_COL32(r + 20, g + 10, b - 10, 50));
    }

    // Sun glow (large gradient behind sun)
    for (int layer = 30; layer >= 0; --layer) {
        float t = static_cast<float>(layer) / 30.0f;
        float glowRadius = sunRadius * (1.0f + t * 0.8f);

        // Glow color: yellow/orange fading to pink/purple
        int r = 255;
        int g = static_cast<int>(200 * (1.0f - t * 0.7f));
        int b = static_cast<int>(50 + 150 * t);
        int alpha = static_cast<int>(15 * (1.0f - t));

        // Draw glow as filled segments
        for (float angle = 3.14159f; angle <= 2.0f * 3.14159f; angle += 0.02f) {
            float x1 = sunX + cosf(angle) * glowRadius;
            float y1 = sunY + sinf(angle) * glowRadius;
            float x2 = sunX + cosf(angle + 0.02f) * glowRadius;
            float y2 = sunY + sinf(angle + 0.02f) * glowRadius;

            if (y1 < screenSize.y && y2 < screenSize.y) {
                drawList->AddTriangleFilled(
                    ImVec2(sunX, sunY),
                    ImVec2(x1, y1),
                    ImVec2(x2, y2),
                    IM_COL32(r, g, b, alpha));
            }
        }
    }

    // Main sun disc (solid with horizontal stripes)
    for (float angle = 3.14159f; angle <= 2.0f * 3.14159f; angle += 0.01f) {
        float x1 = sunX + cosf(angle) * sunRadius;
        float y1 = sunY + sinf(angle) * sunRadius;
        float x2 = sunX + cosf(angle + 0.01f) * sunRadius;
        float y2 = sunY + sinf(angle + 0.01f) * sunRadius;

        if (y1 < screenSize.y && y2 < screenSize.y) {
            // Gradient from yellow at top to magenta at bottom
            float heightRatio = 1.0f - (sunY - y1) / sunRadius;
            int r = 255;
            int g = static_cast<int>(255 * (1.0f - heightRatio * 0.9f));
            int b = static_cast<int>(50 + 200 * heightRatio);

            drawList->AddTriangleFilled(
                ImVec2(sunX, sunY),
                ImVec2(x1, y1),
                ImVec2(x2, y2),
                IM_COL32(r, g, b, 200));
        }
    }

    // Horizontal stripe gaps in sun (classic synthwave look)
    float stripeSpacing = sunRadius / 8.0f;
    for (float y = sunY - sunRadius; y < screenSize.y; y += stripeSpacing) {
        if (y < horizonY) continue;

        // Calculate width at this height
        float dy = sunY - y;
        if (dy < 0 || dy > sunRadius) continue;
        float halfWidth = sqrtf(sunRadius * sunRadius - dy * dy);

        // Stripe gets thicker toward bottom
        float stripeHeight = 2.0f + (y - horizonY) / (screenSize.y - horizonY) * 8.0f;

        drawList->AddRectFilled(
            ImVec2(sunX - halfWidth, y),
            ImVec2(sunX + halfWidth, y + stripeHeight),
            IM_COL32(20, 10, 30, 255));  // Dark purple background color
    }

    // Horizon line
    drawList->AddLine(
        ImVec2(0, horizonY),
        ImVec2(screenSize.x, horizonY),
        IM_COL32(255, 100, 200, 150), 2.0f);

    // Grid floor (perspective lines going to vanishing point)
    float vanishY = horizonY;
    float vanishX = sunX;
    int numVerticalLines = 30;
    for (int i = -numVerticalLines / 2; i <= numVerticalLines / 2; ++i) {
        float x = vanishX + i * (screenSize.x / numVerticalLines) * 2.0f;
        float alpha = 1.0f - std::abs(static_cast<float>(i)) / (numVerticalLines / 2.0f);
        drawList->AddLine(
            ImVec2(vanishX, vanishY),
            ImVec2(x, screenSize.y),
            IM_COL32(255, 50, 200, static_cast<int>(80 * alpha)), 1.0f);
    }

    // Horizontal grid lines (closer together near horizon)
    for (int i = 1; i <= 15; ++i) {
        float t = static_cast<float>(i) / 15.0f;
        float y = horizonY + (screenSize.y - horizonY) * t * t;  // Quadratic spacing
        float alpha = t;
        drawList->AddLine(
            ImVec2(0, y),
            ImVec2(screenSize.x, y),
            IM_COL32(255, 50, 200, static_cast<int>(60 * alpha)), 1.0f);
    }

    // ========================================================================
    // Smooth Gradient Chasers (top and bottom)
    // ========================================================================
    float chaserHeight = 8.0f;
    float glowHeight = 20.0f;

    // Top chaser - smooth gradient wave
    for (float x = 0; x < screenSize.x; x += 2.0f) {
        float phase = x * 0.01f - g_ChaserOffset * 0.05f;
        float brightness = (sinf(phase) + 1.0f) * 0.5f;
        brightness = brightness * brightness;  // Sharper peaks

        float colorPhase = g_ChaserColorPhase + x * 0.002f;
        ImU32 color = GetNeonColor(colorPhase, brightness);

        // Main chaser bar
        drawList->AddRectFilled(
            ImVec2(x, 0),
            ImVec2(x + 3.0f, chaserHeight * brightness + 2.0f),
            color);

        // Glow below
        if (brightness > 0.3f) {
            ImU32 glowColor = GetNeonColor(colorPhase, brightness * 0.3f);
            drawList->AddRectFilledMultiColor(
                ImVec2(x, chaserHeight),
                ImVec2(x + 3.0f, chaserHeight + glowHeight * brightness),
                glowColor, glowColor,
                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
        }
    }

    // Bottom chaser - smooth gradient wave (reversed direction, different color offset)
    for (float x = 0; x < screenSize.x; x += 2.0f) {
        float phase = (screenSize.x - x) * 0.01f - g_ChaserOffset * 0.05f;
        float brightness = (sinf(phase) + 1.0f) * 0.5f;
        brightness = brightness * brightness;

        float colorPhase = g_ChaserColorPhase + 1.5f + x * 0.002f;  // Offset color
        ImU32 color = GetNeonColor(colorPhase, brightness);

        // Main chaser bar
        drawList->AddRectFilled(
            ImVec2(x, screenSize.y - chaserHeight * brightness - 2.0f),
            ImVec2(x + 3.0f, screenSize.y),
            color);

        // Glow above
        if (brightness > 0.3f) {
            ImU32 glowColor = GetNeonColor(colorPhase, brightness * 0.3f);
            drawList->AddRectFilledMultiColor(
                ImVec2(x, screenSize.y - chaserHeight - glowHeight * brightness),
                ImVec2(x + 3.0f, screenSize.y - chaserHeight),
                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                glowColor, glowColor);
        }
    }

    // Side chasers (left and right edges)
    for (float y = 0; y < screenSize.y; y += 2.0f) {
        float phase = y * 0.015f + g_ChaserOffset * 0.03f;
        float brightness = (sinf(phase) + 1.0f) * 0.5f;
        brightness = brightness * brightness;

        float colorPhase = g_ChaserColorPhase + 0.5f + y * 0.001f;
        ImU32 color = GetNeonColor(colorPhase, brightness * 0.7f);

        // Left edge
        drawList->AddRectFilled(
            ImVec2(0, y),
            ImVec2(4.0f + 4.0f * brightness, y + 3.0f),
            color);

        // Right edge
        drawList->AddRectFilled(
            ImVec2(screenSize.x - 4.0f - 4.0f * brightness, y),
            ImVec2(screenSize.x, y + 3.0f),
            color);
    }
}

// Draw Cyberpunk glitch lines effect with enhanced animations
inline void DrawCyberpunkGlitch(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    static float glitchTimer = 0.0f;
    static float nextGlitchTime = 0.5f;
    static std::vector<std::pair<float, float>> glitchLines;  // y position and width
    static float hexTimer = 0.0f;
    static float flickerTimer = 0.0f;
    static bool flickerState = true;

    glitchTimer += deltaTime;
    hexTimer += deltaTime;
    flickerTimer += deltaTime;

    // Update global animation states
    g_CyberpunkPulse += deltaTime * 2.0f;
    if (g_CyberpunkPulse > 6.28318f) g_CyberpunkPulse -= 6.28318f;

    g_DataStreamOffset += deltaTime * 150.0f;
    if (g_DataStreamOffset > 500.0f) g_DataStreamOffset -= 500.0f;

    // Flicker state (random)
    if (flickerTimer > 0.05f + static_cast<float>(rand() % 10) / 100.0f) {
        flickerTimer = 0.0f;
        flickerState = (rand() % 10) > 1;  // 90% on, 10% flicker
    }

    // ========================================================================
    // Background circuit pattern (scales with screen width)
    // ========================================================================
    // Faint circuit board lines - scale count with screen width
    int numCircuitLines = static_cast<int>(screenSize.x / 80.0f) + 5;
    float circuitSpacing = screenSize.x / static_cast<float>(numCircuitLines);
    for (int i = 0; i < numCircuitLines; ++i) {
        float x = (i * circuitSpacing + hexTimer * 5.0f);
        x = std::fmod(x, screenSize.x + 200.0f) - 100.0f;
        float alpha = 0.3f + 0.1f * sinf(hexTimer + i);

        // Vertical lines
        drawList->AddLine(
            ImVec2(x, 0),
            ImVec2(x, screenSize.y),
            IM_COL32(0, 80, 80, static_cast<int>(20 * alpha)), 1.0f);

        // Horizontal connections at random heights
        if (i % 3 == 0) {
            float y = std::fmod(i * 47.0f, screenSize.y);
            float nextX = x + circuitSpacing;
            if (nextX < screenSize.x) {
                drawList->AddLine(
                    ImVec2(x, y),
                    ImVec2(nextX, y),
                    IM_COL32(0, 100, 100, static_cast<int>(15 * alpha)), 1.0f);

                // Node dots
                drawList->AddCircleFilled(ImVec2(x, y), 3.0f, IM_COL32(0, 150, 150, static_cast<int>(40 * alpha)));
            }
        }
    }

    // ========================================================================
    // Data streams (falling binary/hex characters on sides)
    // ========================================================================
    const char* hexChars = "0123456789ABCDEF";
    // Scale stream width and column count with screen dimensions
    float streamWidth = screenSize.x * 0.06f;  // 6% of screen width
    int numStreamCols = std::max(3, static_cast<int>(streamWidth / 20.0f));
    float colSpacing = streamWidth / static_cast<float>(numStreamCols);
    int numStreamRows = static_cast<int>(screenSize.y / 15.0f) + 5;

    // Left data stream
    for (int col = 0; col < numStreamCols; ++col) {
        float x = 10.0f + col * colSpacing;
        float offset = std::fmod(g_DataStreamOffset * (0.5f + col * 0.2f), 400.0f);

        for (int row = 0; row < numStreamRows; ++row) {
            float y = std::fmod(row * 20.0f + offset, screenSize.y + 40.0f) - 20.0f;
            int charIndex = (col * 7 + row * 3 + static_cast<int>(hexTimer * 10)) % 16;
            char str[2] = { hexChars[charIndex], 0 };

            float alpha = 0.3f + 0.2f * sinf(hexTimer * 3.0f + row * 0.3f);
            ImU32 color = (row % 5 == 0)
                ? IM_COL32(255, 255, 0, static_cast<int>(200 * alpha))   // Yellow highlight
                : IM_COL32(0, 255, 255, static_cast<int>(150 * alpha));  // Cyan

            drawList->AddText(ImVec2(x, y), color, str);
        }
    }

    // Right data stream
    for (int col = 0; col < numStreamCols; ++col) {
        float x = screenSize.x - 10.0f - col * colSpacing - colSpacing;
        float offset = std::fmod(g_DataStreamOffset * (0.7f + col * 0.15f), 400.0f);

        for (int row = 0; row < numStreamRows; ++row) {
            float y = std::fmod(row * 20.0f + offset, screenSize.y + 40.0f) - 20.0f;
            int charIndex = (col * 11 + row * 5 + static_cast<int>(hexTimer * 8)) % 16;
            char str[2] = { hexChars[charIndex], 0 };

            float alpha = 0.3f + 0.2f * sinf(hexTimer * 2.5f + row * 0.4f);
            ImU32 color = (row % 4 == 0)
                ? IM_COL32(255, 0, 128, static_cast<int>(200 * alpha))   // Pink highlight
                : IM_COL32(0, 255, 255, static_cast<int>(150 * alpha));  // Cyan

            drawList->AddText(ImVec2(x, y), color, str);
        }
    }

    // ========================================================================
    // Pulsing neon border
    // ========================================================================
    float pulseVal = (sinf(g_CyberpunkPulse) + 1.0f) * 0.5f;
    float borderWidth = 3.0f + pulseVal * 2.0f;
    int borderAlpha = static_cast<int>(100 + 100 * pulseVal);

    if (flickerState) {
        // Top border - yellow
        drawList->AddRectFilled(
            ImVec2(0, 0),
            ImVec2(screenSize.x, borderWidth),
            IM_COL32(255, 255, 0, borderAlpha));

        // Bottom border - cyan
        drawList->AddRectFilled(
            ImVec2(0, screenSize.y - borderWidth),
            ImVec2(screenSize.x, screenSize.y),
            IM_COL32(0, 255, 255, borderAlpha));

        // Left border - pink
        drawList->AddRectFilled(
            ImVec2(0, 0),
            ImVec2(borderWidth, screenSize.y),
            IM_COL32(255, 0, 128, borderAlpha));

        // Right border - pink
        drawList->AddRectFilled(
            ImVec2(screenSize.x - borderWidth, 0),
            ImVec2(screenSize.x, screenSize.y),
            IM_COL32(255, 0, 128, borderAlpha));

        // Corner glow effects - scale with screen size
        float baseCornerSize = std::min(screenSize.x, screenSize.y) * 0.06f;
        float cornerSize = baseCornerSize + pulseVal * baseCornerSize * 0.4f;
        // Top-left
        drawList->AddRectFilledMultiColor(
            ImVec2(0, 0), ImVec2(cornerSize, cornerSize),
            IM_COL32(255, 255, 0, 60), IM_COL32(255, 255, 0, 0),
            IM_COL32(255, 255, 0, 0), IM_COL32(255, 255, 0, 0));
        // Top-right
        drawList->AddRectFilledMultiColor(
            ImVec2(screenSize.x - cornerSize, 0), ImVec2(screenSize.x, cornerSize),
            IM_COL32(0, 255, 255, 0), IM_COL32(0, 255, 255, 60),
            IM_COL32(0, 255, 255, 0), IM_COL32(0, 255, 255, 0));
        // Bottom-left
        drawList->AddRectFilledMultiColor(
            ImVec2(0, screenSize.y - cornerSize), ImVec2(cornerSize, screenSize.y),
            IM_COL32(255, 0, 128, 0), IM_COL32(255, 0, 128, 0),
            IM_COL32(255, 0, 128, 0), IM_COL32(255, 0, 128, 60));
        // Bottom-right
        drawList->AddRectFilledMultiColor(
            ImVec2(screenSize.x - cornerSize, screenSize.y - cornerSize),
            ImVec2(screenSize.x, screenSize.y),
            IM_COL32(0, 255, 255, 0), IM_COL32(0, 255, 255, 0),
            IM_COL32(0, 255, 255, 60), IM_COL32(0, 255, 255, 0));
    }

    // ========================================================================
    // Glitch lines (enhanced)
    // ========================================================================
    // Trigger new glitch
    if (glitchTimer > nextGlitchTime) {
        glitchTimer = 0.0f;
        nextGlitchTime = 0.2f + static_cast<float>(rand() % 100) / 100.0f * 0.8f;

        glitchLines.clear();
        int numLines = 1 + rand() % 6;
        for (int i = 0; i < numLines; ++i) {
            float y = static_cast<float>(rand() % static_cast<int>(screenSize.y));
            float width = 30.0f + static_cast<float>(rand() % 300);
            glitchLines.push_back({y, width});
        }
    }

    // Draw glitch lines with chromatic aberration effect
    float glitchAlpha = 1.0f - (glitchTimer / nextGlitchTime);
    glitchAlpha = glitchAlpha * glitchAlpha;  // Faster falloff

    for (const auto& line : glitchLines) {
        float offset = static_cast<float>(rand() % 30 - 15);
        float height = 1.0f + static_cast<float>(rand() % 4);

        // Red channel offset
        drawList->AddRectFilled(
            ImVec2(offset - 3, line.first),
            ImVec2(offset + line.second - 3, line.first + height),
            IM_COL32(255, 0, 0, static_cast<int>(80 * glitchAlpha)));

        // Cyan/green channel
        drawList->AddRectFilled(
            ImVec2(offset + 3, line.first),
            ImVec2(offset + line.second + 3, line.first + height),
            IM_COL32(0, 255, 255, static_cast<int>(80 * glitchAlpha)));

        // White core
        drawList->AddRectFilled(
            ImVec2(offset, line.first),
            ImVec2(offset + line.second, line.first + height),
            IM_COL32(255, 255, 255, static_cast<int>(120 * glitchAlpha)));

        // Mirror glitch on other side
        float mirrorY = screenSize.y - line.first - height;
        drawList->AddRectFilled(
            ImVec2(screenSize.x - line.second + offset, mirrorY),
            ImVec2(screenSize.x + offset, mirrorY + height),
            IM_COL32(255, 0, 128, static_cast<int>(70 * glitchAlpha)));
    }

    // ========================================================================
    // Moving scan line
    // ========================================================================
    float scanY = std::fmod(hexTimer * 100.0f, screenSize.y + 100.0f) - 50.0f;
    drawList->AddRectFilledMultiColor(
        ImVec2(0, scanY - 30), ImVec2(screenSize.x, scanY),
        IM_COL32(0, 255, 255, 0), IM_COL32(0, 255, 255, 0),
        IM_COL32(0, 255, 255, 30), IM_COL32(0, 255, 255, 30));
    drawList->AddLine(
        ImVec2(0, scanY),
        ImVec2(screenSize.x, scanY),
        IM_COL32(0, 255, 255, 80), 2.0f);

    // ========================================================================
    // CRT scanlines overlay
    // ========================================================================
    for (float y = 0; y < screenSize.y; y += 2.0f) {
        drawList->AddLine(
            ImVec2(0, y),
            ImVec2(screenSize.x, y),
            IM_COL32(0, 0, 0, 25), 1.0f);
    }

    // ========================================================================
    // Random pixel noise (subtle)
    // ========================================================================
    if (rand() % 3 == 0) {  // Only sometimes
        for (int i = 0; i < 20; ++i) {
            float x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
            float y = static_cast<float>(rand() % static_cast<int>(screenSize.y));
            ImU32 color = (rand() % 2 == 0)
                ? IM_COL32(0, 255, 255, 100)
                : IM_COL32(255, 255, 0, 100);
            drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + 2, y + 2), color);
        }
    }
}

// Draw theme background effects
inline void DrawThemeBackground(Theme theme, float deltaTime) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;

    switch (theme) {
        case Theme::Matrix:
            DrawMatrixRain(drawList, screenSize, deltaTime);
            break;
        case Theme::Synthwave:
            DrawSynthwaveChasers(drawList, screenSize, deltaTime);
            break;
        case Theme::Cyberpunk:
            DrawCyberpunkGlitch(drawList, screenSize, deltaTime);
            break;
        case Theme::Stock:
        default:
            // No background effect for stock theme
            break;
    }
}

// ============================================================================
// Transport Bar
// ============================================================================
inline void DrawTransportBar(Sequencer& seq, Project& project, PlaybackState& state, UIState& ui) {
    ImGui::Begin("Transport", nullptr, ImGuiWindowFlags_NoCollapse);

    // Row 1: Playback controls
    if (ImGui::Button(state.isPlaying ? "PAUSE" : "PLAY", ImVec2(60, 30))) {
        if (state.isPlaying) seq.pause();
        else seq.play();
    }
    ImGui::SameLine();

    if (ImGui::Button("STOP", ImVec2(60, 30))) {
        seq.stop();
    }
    ImGui::SameLine();

    bool loopEnabled = state.loop;
    if (ImGui::Checkbox("Loop", &loopEnabled)) {
        seq.setLoopEnabled(loopEnabled);
    }
    ImGui::SameLine();

    ImGui::SetNextItemWidth(80);
    if (ImGui::DragFloat("BPM", &project.bpm, 1.0f, 30.0f, 300.0f, "%.0f")) {
        seq.setBPM(project.bpm);

        // Auto-adjust drum durations based on new BPM
        // Formula: duration_beats = decay_seconds * (BPM / 60)
        for (Pattern& pat : project.patterns) {
            for (Note& note : pat.notes) {
                if (!isDrumType(note.oscillatorType)) continue;
                float decayTime = getDrumDecayTime(note.oscillatorType);
                note.duration = decayTime * (project.bpm / 60.0f);
            }
        }
    }

    // Row 2: Position and Master Volume
    int measure = static_cast<int>(state.currentBeat / project.beatsPerMeasure) + 1;
    int beatNum = static_cast<int>(std::fmod(state.currentBeat, static_cast<float>(project.beatsPerMeasure))) + 1;
    ImGui::Text("Position: %d.%d", measure, beatNum);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(200);
    float pos = state.currentBeat;
    if (ImGui::SliderFloat("##pos", &pos, 0.0f, project.songLength, "Beat %.1f")) {
        seq.setPosition(pos);
    }

    // Row 3: Master Volume (prominent) - display as 0-100%
    ImGui::Text("Master Volume:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    float masterPct = project.masterVolume * 100.0f;
    if (ImGui::SliderFloat("##master", &masterPct, 0.0f, 100.0f, "%.0f%%")) {
        project.masterVolume = masterPct / 100.0f;
    }

    // Update preview pattern for playback
    seq.setPreviewPattern(ui.selectedPattern, ui.selectedChannel);

    ImGui::End();
}

// ============================================================================
// File Menu Bar
// ============================================================================
inline void DrawFileMenu(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("File", nullptr, ImGuiWindowFlags_NoCollapse);

    // New project
    if (ImGui::Button("New", ImVec2(60, 25))) {
        project = Project();  // Reset to default
        ui.selectedPattern = 0;
        ui.selectedNoteIndex = -1;
        ui.selectedNoteIndices.clear();
        ui.projectFilePath = "";
        g_UndoHistory.clear();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create new project");

    ImGui::SameLine();

    // Load project
    if (ImGui::Button("Load", ImVec2(60, 25))) {
        std::string path = openFileDialog(
            "Chiptune Projects (*.ctp)\0*.ctp\0All Files (*.*)\0*.*\0",
            "ctp");
        if (!path.empty()) {
            if (loadProject(project, path)) {
                ui.projectFilePath = path;
                ui.selectedPattern = 0;
                ui.selectedNoteIndex = -1;
                ui.selectedNoteIndices.clear();
                g_UndoHistory.clear();
                seq.updateChannelConfigs();
            }
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load project file (.ctp)");

    ImGui::SameLine();

    // Save project
    if (ImGui::Button("Save", ImVec2(60, 25))) {
        if (ui.projectFilePath.empty()) {
            std::string path = saveFileDialog(
                "Chiptune Projects (*.ctp)\0*.ctp\0",
                "ctp");
            if (!path.empty()) {
                ui.projectFilePath = path;
                saveProject(project, path);
            }
        } else {
            saveProject(project, ui.projectFilePath);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save project (Ctrl+S)");

    ImGui::SameLine();

    // Save As
    if (ImGui::Button("Save As", ImVec2(70, 25))) {
        std::string path = saveFileDialog(
            "Chiptune Projects (*.ctp)\0*.ctp\0",
            "ctp");
        if (!path.empty()) {
            ui.projectFilePath = path;
            saveProject(project, path);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save project as new file");

    ImGui::SameLine(0, 30);

    // Export section
    ImGui::Text("Export:");
    ImGui::SameLine();

    static bool showExportPopup = false;
    static bool showMp3ExportPopup = false;
    static float exportDuration = 16.0f;
    static int mp3Bitrate = 192;
    static std::string exportStatus = "";

    // Calculate default duration helper
    auto calcDefaultDuration = [&]() {
        if (ui.selectedPattern >= 0 && ui.selectedPattern < static_cast<int>(project.patterns.size())) {
            const Pattern& pat = project.patterns[ui.selectedPattern];
            float maxEnd = 0.0f;
            for (const Note& n : pat.notes) {
                float end = n.startTime + n.duration;
                if (end > maxEnd) maxEnd = end;
            }
            exportDuration = std::max(4.0f, maxEnd + 1.0f);
        }
    };

    if (ImGui::Button("WAV", ImVec2(50, 25))) {
        showExportPopup = true;
        calcDefaultDuration();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Export to WAV audio file");

    ImGui::SameLine();

    if (ImGui::Button("MP3", ImVec2(50, 25))) {
        showMp3ExportPopup = true;
        calcDefaultDuration();
    }
    if (ImGui::IsItemHovered()) {
        std::string tooltip = "Export to MP3 audio file\n" + getMp3EncoderStatus();
        ImGui::SetTooltip("%s", tooltip.c_str());
    }

    // WAV Export popup
    if (showExportPopup) {
        ImGui::OpenPopup("Export WAV");
    }

    if (ImGui::BeginPopupModal("Export WAV", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Export audio to WAV file");
        ImGui::Separator();

        ImGui::SetNextItemWidth(150);
        ImGui::DragFloat("Duration (beats)", &exportDuration, 1.0f, 1.0f, 256.0f, "%.0f");

        float durationSec = exportDuration * 60.0f / project.bpm;
        ImGui::Text("Duration: %.1f seconds at %.0f BPM", durationSec, project.bpm);

        ImGui::Separator();

        if (ImGui::Button("Export", ImVec2(100, 0))) {
            std::string path = saveFileDialog(
                "WAV Audio (*.wav)\0*.wav\0",
                "wav");
            if (!path.empty()) {
                if (exportWav(project, seq, path, exportDuration)) {
                    exportStatus = "Export successful!";
                } else {
                    exportStatus = "Export failed!";
                }
            }
            showExportPopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            showExportPopup = false;
            ImGui::CloseCurrentPopup();
        }

        if (!exportStatus.empty()) {
            ImGui::TextColored(
                exportStatus.find("failed") != std::string::npos
                    ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.3f, 1, 0.3f, 1),
                "%s", exportStatus.c_str());
        }

        ImGui::EndPopup();
    }

    // MP3 Export popup
    if (showMp3ExportPopup) {
        ImGui::OpenPopup("Export MP3");
    }

    if (ImGui::BeginPopupModal("Export MP3", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Export audio to MP3 file");
        ImGui::Separator();

        // Show encoder status
        std::string encoderStatus = getMp3EncoderStatus();
        bool hasEncoder = isLameAvailable() || isFFmpegAvailable();
        ImGui::TextColored(
            hasEncoder ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.5f, 0.3f, 1),
            "%s", encoderStatus.c_str());

        ImGui::Separator();

        ImGui::SetNextItemWidth(150);
        ImGui::DragFloat("Duration (beats)", &exportDuration, 1.0f, 1.0f, 256.0f, "%.0f");

        float durationSec = exportDuration * 60.0f / project.bpm;
        ImGui::Text("Duration: %.1f seconds at %.0f BPM", durationSec, project.bpm);

        ImGui::Spacing();

        ImGui::SetNextItemWidth(150);
        ImGui::SliderInt("Bitrate (kbps)", &mp3Bitrate, 128, 320);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher = better quality, larger file");

        ImGui::Separator();

        // Disable export button if no encoder
        if (!hasEncoder) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Export", ImVec2(100, 0))) {
            std::string path = saveFileDialog(
                "MP3 Audio (*.mp3)\0*.mp3\0",
                "mp3");
            if (!path.empty()) {
                if (exportMp3(project, seq, path, exportDuration, mp3Bitrate)) {
                    exportStatus = "MP3 export successful!";
                } else {
                    exportStatus = "MP3 export failed!";
                }
            }
            showMp3ExportPopup = false;
            ImGui::CloseCurrentPopup();
        }

        if (!hasEncoder) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            showMp3ExportPopup = false;
            ImGui::CloseCurrentPopup();
        }

        if (!hasEncoder) {
            ImGui::Spacing();
            ImGui::TextWrapped("Install LAME or FFmpeg and add to PATH to enable MP3 export.");
        }

        if (!exportStatus.empty() && exportStatus.find("MP3") != std::string::npos) {
            ImGui::TextColored(
                exportStatus.find("failed") != std::string::npos
                    ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.3f, 1, 0.3f, 1),
                "%s", exportStatus.c_str());
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // Show current file path
    if (!ui.projectFilePath.empty()) {
        // Extract just filename
        size_t lastSlash = ui.projectFilePath.find_last_of("/\\");
        std::string filename = (lastSlash != std::string::npos)
            ? ui.projectFilePath.substr(lastSlash + 1)
            : ui.projectFilePath;
        ImGui::TextDisabled("| %s", filename.c_str());
    }

    ImGui::End();
}

// ============================================================================
// Piano Roll Editor - Full Featured
// ============================================================================
inline void DrawPianoRoll(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("Piano Roll", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

    if (ui.selectedPattern < 0 || ui.selectedPattern >= static_cast<int>(project.patterns.size())) {
        ImGui::Text("No pattern selected");
        ImGui::End();
        return;
    }

    Pattern& pattern = project.patterns[ui.selectedPattern];

    // ========================================================================
    // Toolbar Row 1: Pattern info and mode selection
    // ========================================================================
    ImGui::Text("Pattern: %s", pattern.name.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::DragInt("Length", &pattern.length, 1, 1, 9999);  // Essentially unlimited
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pattern length in beats (auto-extends as you add notes)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("Zoom", &ui.zoomX, 0.5f, 4.0f);

    ImGui::SameLine(0, 20);
    ImGui::Text("Mode:");
    ImGui::SameLine();

    // Mode buttons with visual indication
    bool isSelect = (ui.pianoRollMode == PianoRollMode::Select);
    bool isDraw = (ui.pianoRollMode == PianoRollMode::Draw);
    bool isErase = (ui.pianoRollMode == PianoRollMode::Erase);

    if (isSelect) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button("Select (S)")) ui.pianoRollMode = PianoRollMode::Select;
    if (isSelect) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (isDraw) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
    if (ImGui::Button("Draw (D)")) ui.pianoRollMode = PianoRollMode::Draw;
    if (isDraw) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (isErase) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Erase (E)")) ui.pianoRollMode = PianoRollMode::Erase;
    if (isErase) ImGui::PopStyleColor();

    // ========================================================================
    // Toolbar Row 2: Actions
    // ========================================================================
    // Check if we have any selected notes (single or multiple)
    bool hasSelectedNote = (ui.selectedNoteIndex >= 0 &&
                           ui.selectedNoteIndex < static_cast<int>(pattern.notes.size()));
    bool hasMultiSelection = !ui.selectedNoteIndices.empty();
    bool hasAnySelection = hasSelectedNote || hasMultiSelection;

    // Show selection count
    if (hasMultiSelection) {
        ImGui::Text("Selected: %d notes", static_cast<int>(ui.selectedNoteIndices.size()));
        ImGui::SameLine();
    }

    // Copy button - supports single and multi-selection
    if (!hasAnySelection) ImGui::BeginDisabled();
    if (ImGui::Button("Copy")) {
        g_NoteClipboard.clear();

        // Collect notes to copy
        std::vector<int> indicesToCopy;
        if (hasMultiSelection) {
            indicesToCopy = ui.selectedNoteIndices;
        } else if (hasSelectedNote) {
            indicesToCopy.push_back(ui.selectedNoteIndex);
        }

        if (!indicesToCopy.empty()) {
            // Find base time and pitch for relative positioning
            float minTime = 999999.0f;
            int minPitch = 999;
            for (int idx : indicesToCopy) {
                if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                    const Note& note = pattern.notes[idx];
                    g_NoteClipboard.push_back(note);
                    if (note.startTime < minTime) minTime = note.startTime;
                    if (note.pitch < minPitch) minPitch = note.pitch;
                }
            }
            g_ClipboardBaseTime = minTime;
            g_ClipboardBasePitch = minPitch;
        }
    }
    if (!hasAnySelection) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+C - Copy selected notes");

    ImGui::SameLine();

    // Paste button
    if (g_NoteClipboard.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Paste")) {
        if (!g_NoteClipboard.empty()) {
            // Save state for undo
            g_UndoHistory.saveState(pattern, ui.selectedPattern);

            float pasteTime = std::fmod(seq.getCurrentBeat(), static_cast<float>(pattern.length));
            ui.selectedNoteIndices.clear();

            for (const Note& clipNote : g_NoteClipboard) {
                Note newNote = clipNote;
                float offset = clipNote.startTime - g_ClipboardBaseTime;
                newNote.startTime = pasteTime + offset;
                while (newNote.startTime >= pattern.length) newNote.startTime -= pattern.length;
                while (newNote.startTime < 0) newNote.startTime += pattern.length;
                pattern.notes.push_back(newNote);
                ui.selectedNoteIndices.push_back(static_cast<int>(pattern.notes.size()) - 1);
            }
            if (!ui.selectedNoteIndices.empty()) {
                ui.selectedNoteIndex = ui.selectedNoteIndices[0];
            }
        }
    }
    if (g_NoteClipboard.empty()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+V - Paste at playhead");

    ImGui::SameLine();

    // Delete button - supports single and multi-selection
    if (!hasAnySelection) ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        // Save state for undo
        g_UndoHistory.saveState(pattern, ui.selectedPattern);

        // Collect indices to delete
        std::vector<int> indicesToDelete;
        if (hasMultiSelection) {
            indicesToDelete = ui.selectedNoteIndices;
        } else if (hasSelectedNote) {
            indicesToDelete.push_back(ui.selectedNoteIndex);
        }

        // Sort in descending order to delete from end first
        std::sort(indicesToDelete.begin(), indicesToDelete.end(), std::greater<int>());

        for (int idx : indicesToDelete) {
            if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                pattern.notes.erase(pattern.notes.begin() + idx);
            }
        }

        ui.selectedNoteIndex = -1;
        ui.selectedNoteIndices.clear();
    }
    if (!hasAnySelection) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete/Backspace");

    ImGui::SameLine();

    // Select All button
    if (pattern.notes.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Select All")) {
        ui.selectedNoteIndices.clear();
        for (size_t i = 0; i < pattern.notes.size(); ++i) {
            ui.selectedNoteIndices.push_back(static_cast<int>(i));
        }
        if (!ui.selectedNoteIndices.empty()) {
            ui.selectedNoteIndex = ui.selectedNoteIndices[0];
        }
    }
    if (pattern.notes.empty()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+A - Select all notes");

    ImGui::SameLine();

    // Clear all button with confirmation
    static bool showClearConfirm = false;
    if (ImGui::Button("Clear All")) {
        if (pattern.notes.empty()) {
            // Nothing to clear
        } else {
            showClearConfirm = true;
        }
    }

    // Clear confirmation popup
    if (showClearConfirm) {
        ImGui::OpenPopup("Confirm Clear");
    }
    if (ImGui::BeginPopupModal("Confirm Clear", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete all %zu notes in this pattern?", pattern.notes.size());
        ImGui::Separator();
        if (ImGui::Button("Yes, Clear All", ImVec2(120, 0))) {
            pattern.notes.clear();
            ui.selectedNoteIndex = -1;
            showClearConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showClearConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0, 20);
    ImGui::Text("Notes: %zu", pattern.notes.size());

    ImGui::SameLine(0, 20);
    if (hasSelectedNote) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Selected: Note %d", ui.selectedNoteIndex);
    } else {
        ImGui::TextDisabled("No note selected");
    }

    ImGui::Separator();

    // ========================================================================
    // Piano roll canvas
    // ========================================================================
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y = std::max(canvasSize.y, 400.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Dimensions
    const float keyWidth = 60.0f;
    const float noteHeight = 16.0f * ui.zoomY;
    const float beatWidth = 40.0f * ui.zoomX;
    const int visibleOctaves = 6;
    const int lowestNote = 24;  // C1 (lower bass range)
    const int highestNote = lowestNote + visibleOctaves * 12;
    const float resizeHandleWidth = 8.0f;  // Width of resize handle at note edge

    // Calculate dynamic grid width based on notes
    float maxNoteEnd = static_cast<float>(pattern.length);
    for (const Note& n : pattern.notes) {
        float noteEnd = n.startTime + n.duration;
        if (noteEnd > maxNoteEnd) maxNoteEnd = noteEnd;
    }
    // Add padding (4 beats) and round up to next measure
    float dynamicLength = std::ceil((maxNoteEnd + 4.0f) / project.beatsPerMeasure) * project.beatsPerMeasure;
    dynamicLength = std::max(dynamicLength, static_cast<float>(pattern.length));

    float gridHeight = (highestNote - lowestNote) * noteHeight;
    float gridWidth = dynamicLength * beatWidth;

    // Background
    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(30, 30, 35, 255));

    // Piano keys
    for (int note = lowestNote; note < highestNote; ++note) {
        float y = canvasPos.y + (highestNote - note - 1) * noteHeight - ui.scrollY;
        if (y < canvasPos.y - noteHeight || y > canvasPos.y + canvasSize.y) continue;

        int noteInOctave = note % 12;
        bool isBlack = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
                       noteInOctave == 8 || noteInOctave == 10);

        ImU32 keyColor = isBlack ? IM_COL32(40, 40, 45, 255) : IM_COL32(60, 60, 65, 255);
        drawList->AddRectFilled(
            ImVec2(canvasPos.x, y),
            ImVec2(canvasPos.x + keyWidth, y + noteHeight),
            keyColor);

        // Key label
        if (noteInOctave == 0) {  // C notes
            drawList->AddText(ImVec2(canvasPos.x + 5, y + 2),
                IM_COL32(200, 200, 200, 255), noteToString(note).c_str());
        }

        // Horizontal grid line
        drawList->AddLine(
            ImVec2(canvasPos.x + keyWidth, y),
            ImVec2(canvasPos.x + keyWidth + gridWidth, y),
            IM_COL32(50, 50, 55, 255));
    }

    // Beat grid lines (use dynamic length for extended grid)
    int gridBeats = static_cast<int>(dynamicLength);
    for (int beat = 0; beat <= gridBeats; ++beat) {
        float x = canvasPos.x + keyWidth + beat * beatWidth - ui.scrollX;
        if (x < canvasPos.x + keyWidth || x > canvasPos.x + canvasSize.x) continue;

        // Darker line at pattern boundary, bright at measures
        ImU32 lineColor;
        if (beat == pattern.length) {
            lineColor = IM_COL32(150, 80, 80, 255);  // Red-ish for pattern end
        } else if (beat % project.beatsPerMeasure == 0) {
            lineColor = IM_COL32(100, 100, 110, 255);
        } else {
            lineColor = IM_COL32(50, 50, 55, 255);
        }
        drawList->AddLine(
            ImVec2(x, canvasPos.y),
            ImVec2(x, canvasPos.y + gridHeight),
            lineColor);
    }

    // ========================================================================
    // Draw notes with selection highlight and fade indicators
    // ========================================================================
    for (size_t i = 0; i < pattern.notes.size(); ++i) {
        const Note& note = pattern.notes[i];
        if (note.pitch < lowestNote || note.pitch >= highestNote) continue;

        float x = canvasPos.x + keyWidth + note.startTime * beatWidth - ui.scrollX;
        float y = canvasPos.y + (highestNote - note.pitch - 1) * noteHeight - ui.scrollY;
        float w = note.duration * beatWidth;

        // Skip if off-screen
        if (x + w < canvasPos.x + keyWidth || x > canvasPos.x + canvasSize.x) continue;
        if (y + noteHeight < canvasPos.y || y > canvasPos.y + canvasSize.y) continue;

        bool isSelected = (static_cast<int>(i) == ui.selectedNoteIndex);
        // Also check multi-selection
        if (!isSelected) {
            for (int selIdx : ui.selectedNoteIndices) {
                if (selIdx == static_cast<int>(i)) {
                    isSelected = true;
                    break;
                }
            }
        }
        ImU32 noteColor = CHANNEL_COLORS[ui.selectedChannel % 8];

        // Darken/lighten based on selection
        if (isSelected) {
            noteColor = IM_COL32(255, 255, 255, 255);  // White for selected
        }

        // Note rectangle
        drawList->AddRectFilled(
            ImVec2(x, y + 1),
            ImVec2(x + w - 1, y + noteHeight - 1),
            noteColor);

        // Selection border
        if (isSelected) {
            drawList->AddRect(
                ImVec2(x - 1, y),
                ImVec2(x + w, y + noteHeight),
                IM_COL32(255, 200, 0, 255), 0.0f, 0, 2.0f);

            // Resize handle indicator (right edge)
            drawList->AddRectFilled(
                ImVec2(x + w - resizeHandleWidth, y + 1),
                ImVec2(x + w - 1, y + noteHeight - 1),
                IM_COL32(255, 150, 0, 200));
        }

        // Fade in indicator (gradient on left)
        if (note.fadeIn > 0.0f) {
            float fadeWidth = note.fadeIn * beatWidth;
            fadeWidth = std::min(fadeWidth, w * 0.5f);
            for (int fi = 0; fi < static_cast<int>(fadeWidth); fi += 2) {
                float alpha = static_cast<float>(fi) / fadeWidth;
                drawList->AddLine(
                    ImVec2(x + fi, y + 2),
                    ImVec2(x + fi, y + noteHeight - 2),
                    IM_COL32(0, 0, 0, static_cast<int>((1.0f - alpha) * 150)));
            }
        }

        // Fade out indicator (gradient on right)
        if (note.fadeOut > 0.0f) {
            float fadeWidth = note.fadeOut * beatWidth;
            fadeWidth = std::min(fadeWidth, w * 0.5f);
            for (int fi = 0; fi < static_cast<int>(fadeWidth); fi += 2) {
                float alpha = static_cast<float>(fi) / fadeWidth;
                drawList->AddLine(
                    ImVec2(x + w - fi - 1, y + 2),
                    ImVec2(x + w - fi - 1, y + noteHeight - 2),
                    IM_COL32(0, 0, 0, static_cast<int>((1.0f - alpha) * 150)));
            }
        }

        // Velocity indicator (brightness bar at bottom)
        drawList->AddRectFilled(
            ImVec2(x + 1, y + noteHeight - 4),
            ImVec2(x + 1 + (w - 2) * note.velocity, y + noteHeight - 2),
            IM_COL32(255, 255, 255, 120));
    }

    // Draw box selection rectangle
    if (ui.isBoxSelecting) {
        float minBeat = std::min(ui.boxSelectStartX, ui.boxSelectEndX);
        float maxBeat = std::max(ui.boxSelectStartX, ui.boxSelectEndX);
        int minPitch = static_cast<int>(std::min(ui.boxSelectStartY, ui.boxSelectEndY));
        int maxPitch = static_cast<int>(std::max(ui.boxSelectStartY, ui.boxSelectEndY));

        float boxX1 = canvasPos.x + keyWidth + minBeat * beatWidth - ui.scrollX;
        float boxX2 = canvasPos.x + keyWidth + maxBeat * beatWidth - ui.scrollX;
        float boxY1 = canvasPos.y + (highestNote - maxPitch - 1) * noteHeight - ui.scrollY;
        float boxY2 = canvasPos.y + (highestNote - minPitch) * noteHeight - ui.scrollY;

        // Selection box fill
        drawList->AddRectFilled(
            ImVec2(boxX1, boxY1),
            ImVec2(boxX2, boxY2),
            IM_COL32(100, 150, 255, 50));

        // Selection box border
        drawList->AddRect(
            ImVec2(boxX1, boxY1),
            ImVec2(boxX2, boxY2),
            IM_COL32(100, 150, 255, 200), 0.0f, 0, 2.0f);
    }

    // ========================================================================
    // Draw paste preview (ghost notes) if in paste preview mode
    // ========================================================================
    if (ui.isPastePreviewing && !g_NoteClipboard.empty()) {
        // Calculate mouse position for ghost notes
        ImVec2 mousePos = ImGui::GetMousePos();
        float ghostRelX = mousePos.x - canvasPos.x - keyWidth + ui.scrollX;
        float ghostRelY = mousePos.y - canvasPos.y + ui.scrollY;
        int ghostBaseNote = highestNote - 1 - static_cast<int>(ghostRelY / noteHeight);
        float ghostBaseBeat = std::floor(ghostRelX / beatWidth * 4.0f) / 4.0f;  // Snap to 1/4 beat

        // Calculate pitch offset from clipboard base
        int pitchOffset = ghostBaseNote - g_ClipboardBasePitch;

        // Draw each ghost note
        for (const Note& clipNote : g_NoteClipboard) {
            float timeOffset = clipNote.startTime - g_ClipboardBaseTime;
            float noteTime = ghostBaseBeat + timeOffset;
            int notePitch = clipNote.pitch + pitchOffset;

            // Skip if out of range
            if (notePitch < lowestNote || notePitch >= highestNote) continue;

            float x = canvasPos.x + keyWidth + noteTime * beatWidth - ui.scrollX;
            float y = canvasPos.y + (highestNote - notePitch - 1) * noteHeight - ui.scrollY;
            float w = clipNote.duration * beatWidth;

            // Ghost note style - semi-transparent with dashed border effect
            ImU32 ghostColor = IM_COL32(100, 200, 255, 100);  // Light blue, semi-transparent
            ImU32 ghostBorder = IM_COL32(100, 200, 255, 200);

            // Fill
            drawList->AddRectFilled(
                ImVec2(x, y + 1),
                ImVec2(x + w - 1, y + noteHeight - 1),
                ghostColor);

            // Border
            drawList->AddRect(
                ImVec2(x, y),
                ImVec2(x + w, y + noteHeight),
                ghostBorder, 0.0f, 0, 2.0f);
        }

        // Draw helper text
        drawList->AddText(
            ImVec2(canvasPos.x + keyWidth + 10, canvasPos.y + 10),
            IM_COL32(100, 200, 255, 255),
            "Click to place | Escape to cancel | Right-click to cancel");
    }

    // Playhead
    float playheadX = canvasPos.x + keyWidth +
        std::fmod(seq.getCurrentBeat(), static_cast<float>(pattern.length)) * beatWidth - ui.scrollX;
    if (playheadX >= canvasPos.x + keyWidth && playheadX <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(
            ImVec2(playheadX, canvasPos.y),
            ImVec2(playheadX, canvasPos.y + gridHeight),
            IM_COL32(255, 100, 100, 255), 2.0f);
    }

    // ========================================================================
    // Handle mouse input
    // ========================================================================
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##pianoroll", canvasSize);

    // Keyboard shortcuts for modes
    if (ImGui::IsWindowFocused()) {
        bool ctrl = ImGui::GetIO().KeyCtrl;

        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) ui.pianoRollMode = PianoRollMode::Select;
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) ui.pianoRollMode = PianoRollMode::Draw;
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_E)) ui.pianoRollMode = PianoRollMode::Erase;

        // Delete selected notes
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            bool hasNotesToDelete = !ui.selectedNoteIndices.empty() ||
                (ui.selectedNoteIndex >= 0 && ui.selectedNoteIndex < static_cast<int>(pattern.notes.size()));

            if (hasNotesToDelete) {
                // Save state for undo before deleting
                g_UndoHistory.saveState(pattern, ui.selectedPattern);

                if (!ui.selectedNoteIndices.empty()) {
                    // Sort indices in descending order to delete from end first
                    std::sort(ui.selectedNoteIndices.begin(), ui.selectedNoteIndices.end(), std::greater<int>());
                    for (int idx : ui.selectedNoteIndices) {
                        if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                            pattern.notes.erase(pattern.notes.begin() + idx);
                        }
                    }
                    ui.selectedNoteIndices.clear();
                    ui.selectedNoteIndex = -1;
                } else if (ui.selectedNoteIndex >= 0 && ui.selectedNoteIndex < static_cast<int>(pattern.notes.size())) {
                    pattern.notes.erase(pattern.notes.begin() + ui.selectedNoteIndex);
                    ui.selectedNoteIndex = -1;
                }
            }
        }

        // Escape to deselect or cancel paste preview
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (ui.isPastePreviewing) {
                ui.isPastePreviewing = false;
            } else {
                ui.selectedNoteIndex = -1;
                ui.selectedNoteIndices.clear();
            }
        }

        // Undo (Ctrl+Z)
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (g_UndoHistory.canUndo()) {
                PatternSnapshot snapshot = g_UndoHistory.undo(pattern, ui.selectedPattern);
                pattern.notes = snapshot.notes;
                ui.selectedNoteIndex = -1;
                ui.selectedNoteIndices.clear();
            }
        }

        // Redo (Ctrl+Y)
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            if (g_UndoHistory.canRedo()) {
                PatternSnapshot snapshot = g_UndoHistory.redo(pattern, ui.selectedPattern);
                pattern.notes = snapshot.notes;
                ui.selectedNoteIndex = -1;
                ui.selectedNoteIndices.clear();
            }
        }

        // Copy (Ctrl+C) - supports multiple selection
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            g_NoteClipboard.clear();

            // Collect notes to copy
            std::vector<int> indicesToCopy;
            if (!ui.selectedNoteIndices.empty()) {
                indicesToCopy = ui.selectedNoteIndices;
            } else if (ui.selectedNoteIndex >= 0 && ui.selectedNoteIndex < static_cast<int>(pattern.notes.size())) {
                indicesToCopy.push_back(ui.selectedNoteIndex);
            }

            if (!indicesToCopy.empty()) {
                // Find the earliest start time and lowest pitch for relative positioning
                float minTime = std::numeric_limits<float>::max();
                int minPitch = 127;

                for (int idx : indicesToCopy) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        const Note& note = pattern.notes[idx];
                        if (note.startTime < minTime) minTime = note.startTime;
                        if (note.pitch < minPitch) minPitch = note.pitch;
                    }
                }

                g_ClipboardBaseTime = minTime;
                g_ClipboardBasePitch = minPitch;

                // Copy all selected notes
                for (int idx : indicesToCopy) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        g_NoteClipboard.push_back(pattern.notes[idx]);
                    }
                }
            }
        }

        // Paste (Ctrl+V) - enter paste preview mode (ghost notes follow mouse)
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
            if (!g_NoteClipboard.empty() && !ui.isPastePreviewing) {
                ui.isPastePreviewing = true;
            }
        }

        // Cut (Ctrl+X) - copy then delete, supports multiple selection
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X)) {
            g_NoteClipboard.clear();

            // Collect notes to cut
            std::vector<int> indicesToCut;
            if (!ui.selectedNoteIndices.empty()) {
                indicesToCut = ui.selectedNoteIndices;
            } else if (ui.selectedNoteIndex >= 0 && ui.selectedNoteIndex < static_cast<int>(pattern.notes.size())) {
                indicesToCut.push_back(ui.selectedNoteIndex);
            }

            if (!indicesToCut.empty()) {
                // Save state for undo before cutting
                g_UndoHistory.saveState(pattern, ui.selectedPattern);

                // Find the earliest start time and lowest pitch for relative positioning
                float minTime = std::numeric_limits<float>::max();
                int minPitch = 127;

                for (int idx : indicesToCut) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        const Note& note = pattern.notes[idx];
                        if (note.startTime < minTime) minTime = note.startTime;
                        if (note.pitch < minPitch) minPitch = note.pitch;
                    }
                }

                g_ClipboardBaseTime = minTime;
                g_ClipboardBasePitch = minPitch;

                // Copy all selected notes to clipboard
                for (int idx : indicesToCut) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        g_NoteClipboard.push_back(pattern.notes[idx]);
                    }
                }

                // Delete notes (sort descending to delete from end first)
                std::sort(indicesToCut.begin(), indicesToCut.end(), std::greater<int>());
                for (int idx : indicesToCut) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        pattern.notes.erase(pattern.notes.begin() + idx);
                    }
                }

                ui.selectedNoteIndex = -1;
                ui.selectedNoteIndices.clear();
            }
        }

        // Select All (Ctrl+A)
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
            ui.selectedNoteIndices.clear();
            for (size_t i = 0; i < pattern.notes.size(); ++i) {
                ui.selectedNoteIndices.push_back(static_cast<int>(i));
            }
            if (!ui.selectedNoteIndices.empty()) {
                ui.selectedNoteIndex = ui.selectedNoteIndices[0];
            }
        }

    }

    // Handle drag and drop from Sound Palette
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OSC_TYPE")) {
            int oscType = *(const int*)payload->Data;

            ImVec2 mousePos = ImGui::GetMousePos();
            float relX = mousePos.x - canvasPos.x - keyWidth + ui.scrollX;
            float relY = mousePos.y - canvasPos.y + ui.scrollY;

            int droppedNote = highestNote - 1 - static_cast<int>(relY / noteHeight);
            float droppedBeat = relX / beatWidth;

            if (relX >= 0 && droppedNote >= lowestNote && droppedNote < highestNote) {
                Note newNote;
                newNote.pitch = std::clamp(droppedNote, lowestNote, highestNote - 1);
                newNote.startTime = std::floor(droppedBeat * 4.0f) / 4.0f;
                newNote.oscillatorType = static_cast<OscillatorType>(oscType);  // Per-note oscillator

                // Drums auto-adjust duration based on BPM
                if (isDrumType(newNote.oscillatorType)) {
                    float decayTime = getDrumDecayTime(newNote.oscillatorType);
                    newNote.duration = decayTime * (project.bpm / 60.0f);
                } else {
                    newNote.duration = 0.5f;
                }

                newNote.velocity = 0.8f;
                pattern.notes.push_back(newNote);
                ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;

                // Auto-extend pattern length if note goes past current end
                float noteEnd = newNote.startTime + newNote.duration;
                if (noteEnd > pattern.length) {
                    pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Mouse position calculations
    ImVec2 mousePos = ImGui::GetMousePos();
    float relX = mousePos.x - canvasPos.x - keyWidth + ui.scrollX;
    float relY = mousePos.y - canvasPos.y + ui.scrollY;
    int hoveredNote = highestNote - 1 - static_cast<int>(relY / noteHeight);
    float hoveredBeat = relX / beatWidth;

    // Find note under cursor
    int noteUnderCursor = -1;
    bool onResizeHandle = false;
    for (size_t i = 0; i < pattern.notes.size(); ++i) {
        const Note& note = pattern.notes[i];
        float noteX = note.startTime * beatWidth;
        float noteW = note.duration * beatWidth;

        if (hoveredNote == note.pitch &&
            relX >= noteX && relX < noteX + noteW) {
            noteUnderCursor = static_cast<int>(i);
            // Check if on resize handle (right edge)
            if (relX >= noteX + noteW - resizeHandleWidth) {
                onResizeHandle = true;
            }
            break;
        }
    }

    // Set cursor based on context
    if (onResizeHandle && ui.pianoRollMode == PianoRollMode::Select) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (ui.isDraggingNote) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    if (ImGui::IsItemHovered()) {
        // Handle paste preview placement (before regular mode handling)
        if (ui.isPastePreviewing && !g_NoteClipboard.empty()) {
            // Left click places the notes
            if (ImGui::IsMouseClicked(0) && relX >= 0) {
                // Save state for undo
                g_UndoHistory.saveState(pattern, ui.selectedPattern);

                // Calculate placement position
                float placeBeat = std::floor(hoveredBeat * 4.0f) / 4.0f;
                int pitchOffset = hoveredNote - g_ClipboardBasePitch;

                // Clear selection and prepare to select pasted notes
                ui.selectedNoteIndices.clear();

                for (const Note& clipNote : g_NoteClipboard) {
                    Note newNote = clipNote;
                    float timeOffset = clipNote.startTime - g_ClipboardBaseTime;
                    newNote.startTime = placeBeat + timeOffset;
                    newNote.pitch = clipNote.pitch + pitchOffset;

                    // Clamp pitch to valid range
                    newNote.pitch = std::clamp(newNote.pitch, lowestNote, highestNote - 1);

                    // Keep time positive (no wrapping needed, grid extends dynamically)
                    if (newNote.startTime < 0) newNote.startTime = 0;

                    pattern.notes.push_back(newNote);
                    ui.selectedNoteIndices.push_back(static_cast<int>(pattern.notes.size()) - 1);

                    // Auto-extend pattern length if note goes past current end
                    float noteEnd = newNote.startTime + newNote.duration;
                    if (noteEnd > pattern.length) {
                        pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
                    }
                }

                // Select the first pasted note as primary
                if (!ui.selectedNoteIndices.empty()) {
                    ui.selectedNoteIndex = ui.selectedNoteIndices[0];
                }

                // Exit paste preview mode
                ui.isPastePreviewing = false;
            }

            // Right click cancels paste preview
            if (ImGui::IsMouseClicked(1)) {
                ui.isPastePreviewing = false;
            }
        }
        // Handle mouse down (normal mode)
        else if (ImGui::IsMouseClicked(0) && relX >= 0) {
            switch (ui.pianoRollMode) {
                case PianoRollMode::Select:
                    if (noteUnderCursor >= 0) {
                        ui.selectedNoteIndex = noteUnderCursor;
                        // Check if this is a drum (drums can't be resized)
                        bool isDrumNote = isDrumType(pattern.notes[noteUnderCursor].oscillatorType);
                        if (onResizeHandle && !isDrumNote) {
                            // Save state for undo before resizing (drums can't resize)
                            g_UndoHistory.saveState(pattern, ui.selectedPattern);
                            // Start resizing
                            ui.isResizingNote = true;
                            ui.dragStartDuration = pattern.notes[noteUnderCursor].duration;
                            ui.dragStartBeat = hoveredBeat;
                        } else if (!onResizeHandle) {
                            // Save state for undo before dragging
                            g_UndoHistory.saveState(pattern, ui.selectedPattern);
                            // Start dragging
                            ui.isDraggingNote = true;
                            ui.dragStartBeat = pattern.notes[noteUnderCursor].startTime;
                            ui.dragStartPitch = pattern.notes[noteUnderCursor].pitch;
                        }
                    } else {
                        // Start box selection on empty space
                        ui.isBoxSelecting = true;
                        ui.boxSelectStartX = hoveredBeat;
                        ui.boxSelectStartY = static_cast<float>(hoveredNote);
                        ui.boxSelectEndX = hoveredBeat;
                        ui.boxSelectEndY = static_cast<float>(hoveredNote);
                        ui.selectedNoteIndex = -1;
                        ui.selectedNoteIndices.clear();
                    }
                    break;

                case PianoRollMode::Draw:
                    {
                        // Save state for undo before adding note
                        g_UndoHistory.saveState(pattern, ui.selectedPattern);

                        Note newNote;
                        newNote.pitch = std::clamp(hoveredNote, lowestNote, highestNote - 1);
                        newNote.startTime = std::floor(hoveredBeat * 4.0f) / 4.0f;

                        // Use selected palette item's oscillator type if one is selected
                        if (g_SelectedPaletteItem >= 0) {
                            newNote.oscillatorType = static_cast<OscillatorType>(g_SelectedPaletteItem);
                        }

                        // Set duration based on sound type - drums auto-adjust to BPM
                        if (isDrumType(newNote.oscillatorType)) {
                            float decayTime = getDrumDecayTime(newNote.oscillatorType);
                            newNote.duration = decayTime * (project.bpm / 60.0f);
                        } else {
                            newNote.duration = 0.25f;
                        }

                        newNote.velocity = 0.8f;
                        pattern.notes.push_back(newNote);
                        ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;

                        // Auto-extend pattern length if note goes past current end
                        float noteEnd = newNote.startTime + newNote.duration;
                        if (noteEnd > pattern.length) {
                            // Round up to next measure
                            pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
                        }
                    }
                    break;

                case PianoRollMode::Erase:
                    if (noteUnderCursor >= 0) {
                        // Save state for undo before erasing
                        g_UndoHistory.saveState(pattern, ui.selectedPattern);

                        pattern.notes.erase(pattern.notes.begin() + noteUnderCursor);
                        if (ui.selectedNoteIndex == noteUnderCursor) {
                            ui.selectedNoteIndex = -1;
                        } else if (ui.selectedNoteIndex > noteUnderCursor) {
                            ui.selectedNoteIndex--;
                        }
                    }
                    break;
            }
        }

        // Handle dragging
        if (ImGui::IsMouseDown(0)) {
            if (ui.isDraggingNote && ui.selectedNoteIndex >= 0) {
                Note& note = pattern.notes[ui.selectedNoteIndex];
                float newBeat = std::floor(hoveredBeat * 4.0f) / 4.0f;
                int newPitch = std::clamp(hoveredNote, lowestNote, highestNote - 1);
                note.startTime = std::max(0.0f, newBeat);
                note.pitch = newPitch;
            }
            if (ui.isResizingNote && ui.selectedNoteIndex >= 0) {
                Note& note = pattern.notes[ui.selectedNoteIndex];
                // Drums can't be resized - they have fixed duration
                if (!isDrumType(note.oscillatorType)) {
                    float deltaBeats = hoveredBeat - ui.dragStartBeat;
                    float newDuration = ui.dragStartDuration + deltaBeats;
                    note.duration = std::max(0.0625f, std::floor(newDuration * 4.0f) / 4.0f);
                }
            }
            // Update box selection end point
            if (ui.isBoxSelecting) {
                ui.boxSelectEndX = hoveredBeat;
                ui.boxSelectEndY = static_cast<float>(hoveredNote);
            }
        }

        // Handle mouse release
        if (ImGui::IsMouseReleased(0)) {
            ui.isDraggingNote = false;
            ui.isResizingNote = false;

            // Complete box selection - find all notes in the box
            if (ui.isBoxSelecting) {
                ui.isBoxSelecting = false;
                ui.selectedNoteIndices.clear();

                float minBeat = std::min(ui.boxSelectStartX, ui.boxSelectEndX);
                float maxBeat = std::max(ui.boxSelectStartX, ui.boxSelectEndX);
                int minPitch = static_cast<int>(std::min(ui.boxSelectStartY, ui.boxSelectEndY));
                int maxPitch = static_cast<int>(std::max(ui.boxSelectStartY, ui.boxSelectEndY));

                for (size_t i = 0; i < pattern.notes.size(); ++i) {
                    const Note& note = pattern.notes[i];
                    float noteEnd = note.startTime + note.duration;

                    // Check if note overlaps with selection box
                    if (note.pitch >= minPitch && note.pitch <= maxPitch &&
                        noteEnd > minBeat && note.startTime < maxBeat) {
                        ui.selectedNoteIndices.push_back(static_cast<int>(i));
                    }
                }

                // Set single selection to first selected note (for compatibility)
                if (!ui.selectedNoteIndices.empty()) {
                    ui.selectedNoteIndex = ui.selectedNoteIndices[0];
                }
            }
        }

        // Right click always deletes
        if (ImGui::IsMouseClicked(1) && noteUnderCursor >= 0) {
            pattern.notes.erase(pattern.notes.begin() + noteUnderCursor);
            if (ui.selectedNoteIndex == noteUnderCursor) {
                ui.selectedNoteIndex = -1;
            } else if (ui.selectedNoteIndex > noteUnderCursor) {
                ui.selectedNoteIndex--;
            }
        }

        // Show hover info
        ImGui::BeginTooltip();
        ImGui::Text("%s (Beat %.2f)", noteToString(hoveredNote).c_str(), hoveredBeat);
        if (noteUnderCursor >= 0) {
            const Note& n = pattern.notes[noteUnderCursor];
            ImGui::Text("Note: %s, Dur: %.2f, Vel: %.0f%%",
                noteToString(n.pitch).c_str(), n.duration, n.velocity * 100);
        }
        ImGui::EndTooltip();
    } else {
        // Release drag if mouse leaves area
        if (ImGui::IsMouseReleased(0)) {
            ui.isDraggingNote = false;
            ui.isResizingNote = false;
        }
    }

    // Scroll handling
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (ImGui::GetIO().KeyCtrl) {
            ui.zoomX = std::clamp(ui.zoomX + wheel * 0.1f, 0.25f, 4.0f);
        } else if (ImGui::GetIO().KeyShift) {
            ui.scrollX = std::max(0.0f, ui.scrollX - wheel * 50.0f);
        } else {
            ui.scrollY = std::clamp(ui.scrollY - wheel * 30.0f, 0.0f, gridHeight - canvasSize.y);
        }
    }

    ImGui::End();
}

// ============================================================================
// Tracker View
// ============================================================================
inline void DrawTrackerView(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("Tracker", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

    if (ui.selectedPattern < 0 || ui.selectedPattern >= static_cast<int>(project.patterns.size())) {
        ImGui::Text("No pattern selected");
        ImGui::End();
        return;
    }

    Pattern& pattern = project.patterns[ui.selectedPattern];

    // Header
    ImGui::Text("Pattern: %s  |  Length: %d steps", pattern.name.c_str(), pattern.length);
    ImGui::Separator();

    // Column headers
    ImGui::Text("Step");
    for (int ch = 0; ch < 8; ++ch) {
        ImGui::SameLine(80 + ch * 100);
        ImGui::TextColored(ImVec4(
            ((CHANNEL_COLORS[ch] >> 0) & 0xFF) / 255.0f,
            ((CHANNEL_COLORS[ch] >> 8) & 0xFF) / 255.0f,
            ((CHANNEL_COLORS[ch] >> 16) & 0xFF) / 255.0f,
            1.0f), "%s", project.channels[ch].name.c_str());
    }
    ImGui::Separator();

    // Rows (steps)
    float currentBeat = seq.getCurrentBeat();
    int currentStep = static_cast<int>(std::fmod(currentBeat, static_cast<float>(pattern.length)));

    ImGui::BeginChild("TrackerGrid", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (int step = 0; step < pattern.length; ++step) {
        bool isCurrentStep = (step == currentStep && seq.isPlaying());
        bool isHighlighted = (step % ui.trackerRowHighlight == 0);

        // Row background
        if (isCurrentStep) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
        } else if (isHighlighted) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.9f, 1.0f));
        }

        // Step number
        ImGui::Text("%02X", step);

        // Find notes at this step for each channel
        for (int ch = 0; ch < 8; ++ch) {
            ImGui::SameLine(80 + ch * 100);

            // Find note at this step (simplified - notes are stored in pattern)
            bool foundNote = false;
            for (const auto& note : pattern.notes) {
                if (static_cast<int>(note.startTime) == step) {
                    // This is a simplification - in real tracker, notes are per-channel
                    ImGui::Text("%s", noteToString(note.pitch).c_str());
                    foundNote = true;
                    break;
                }
            }
            if (!foundNote) {
                ImGui::TextDisabled("---");
            }
        }

        if (isCurrentStep || isHighlighted) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

// ============================================================================
// Arrangement Timeline
// ============================================================================
inline void DrawArrangement(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("Arrangement", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y = std::max(canvasSize.y, 300.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float trackHeight = 30.0f;
    const float headerWidth = 100.0f;
    const float beatWidth = 20.0f * ui.zoomX;

    // Background
    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(25, 25, 30, 255));

    // Channel headers and tracks
    for (int ch = 0; ch < 8; ++ch) {
        float y = canvasPos.y + ch * trackHeight;

        // Header
        ImU32 headerColor = (ch == ui.selectedChannel)
            ? IM_COL32(60, 60, 70, 255)
            : IM_COL32(40, 40, 45, 255);
        drawList->AddRectFilled(
            ImVec2(canvasPos.x, y),
            ImVec2(canvasPos.x + headerWidth, y + trackHeight - 1),
            headerColor);
        drawList->AddText(ImVec2(canvasPos.x + 5, y + 8),
            CHANNEL_COLORS[ch], project.channels[ch].name.c_str());

        // Track background
        ImU32 trackColor = (ch % 2 == 0) ? IM_COL32(35, 35, 40, 255) : IM_COL32(30, 30, 35, 255);
        drawList->AddRectFilled(
            ImVec2(canvasPos.x + headerWidth, y),
            ImVec2(canvasPos.x + canvasSize.x, y + trackHeight - 1),
            trackColor);
    }

    // Beat grid
    for (float beat = 0; beat < project.songLength; beat += 1.0f) {
        float x = canvasPos.x + headerWidth + beat * beatWidth - ui.scrollX;
        if (x < canvasPos.x + headerWidth || x > canvasPos.x + canvasSize.x) continue;

        ImU32 lineColor = (static_cast<int>(beat) % project.beatsPerMeasure == 0)
            ? IM_COL32(80, 80, 90, 255)
            : IM_COL32(45, 45, 50, 255);
        drawList->AddLine(
            ImVec2(x, canvasPos.y),
            ImVec2(x, canvasPos.y + 8 * trackHeight),
            lineColor);
    }

    // Draw clips
    for (const auto& clip : project.arrangement) {
        float x = canvasPos.x + headerWidth + clip.startBeat * beatWidth - ui.scrollX;
        float y = canvasPos.y + clip.channelIndex * trackHeight;
        float w = clip.lengthBeats * beatWidth;

        if (x + w < canvasPos.x + headerWidth || x > canvasPos.x + canvasSize.x) continue;

        ImU32 clipColor = CHANNEL_COLORS[clip.channelIndex % 8];

        drawList->AddRectFilled(
            ImVec2(x + 1, y + 2),
            ImVec2(x + w - 1, y + trackHeight - 3),
            clipColor);

        // Pattern name
        if (clip.patternIndex >= 0 && clip.patternIndex < static_cast<int>(project.patterns.size())) {
            drawList->AddText(ImVec2(x + 4, y + 8),
                IM_COL32(0, 0, 0, 255),
                project.patterns[clip.patternIndex].name.c_str());
        }
    }

    // Playhead
    float playheadX = canvasPos.x + headerWidth + seq.getCurrentBeat() * beatWidth - ui.scrollX;
    if (playheadX >= canvasPos.x + headerWidth) {
        drawList->AddLine(
            ImVec2(playheadX, canvasPos.y),
            ImVec2(playheadX, canvasPos.y + 8 * trackHeight),
            IM_COL32(255, 80, 80, 255), 2.0f);
    }

    // Handle mouse input
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##arrangement", canvasSize);

    if (ImGui::IsItemHovered()) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float relX = mousePos.x - canvasPos.x - headerWidth + ui.scrollX;
        int hoveredChannel = static_cast<int>((mousePos.y - canvasPos.y) / trackHeight);

        // Left click to select channel
        if (ImGui::IsMouseClicked(0) && mousePos.x < canvasPos.x + headerWidth) {
            if (hoveredChannel >= 0 && hoveredChannel < 8) {
                ui.selectedChannel = hoveredChannel;
            }
        }

        // Double-click to add clip
        if (ImGui::IsMouseDoubleClicked(0) && relX >= 0 && hoveredChannel >= 0 && hoveredChannel < 8) {
            Clip newClip;
            newClip.channelIndex = hoveredChannel;
            newClip.patternIndex = ui.selectedPattern;
            newClip.startBeat = std::floor(relX / beatWidth);
            newClip.lengthBeats = (ui.selectedPattern >= 0 &&
                ui.selectedPattern < static_cast<int>(project.patterns.size()))
                ? static_cast<float>(project.patterns[ui.selectedPattern].length)
                : 16.0f;
            project.arrangement.push_back(newClip);
        }
    }

    // Scroll
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (ImGui::GetIO().KeyCtrl) {
            ui.zoomX = std::clamp(ui.zoomX + wheel * 0.1f, 0.25f, 4.0f);
        } else {
            ui.scrollX = std::max(0.0f, ui.scrollX - wheel * 50.0f);
        }
    }

    ImGui::End();
}

// ============================================================================
// Mixer
// ============================================================================
inline void DrawMixer(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("Mixer", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

    for (int ch = 0; ch < 8; ++ch) {
        auto& channel = project.channels[ch];

        ImGui::BeginGroup();
        ImGui::PushID(ch);

        // Channel label
        ImVec4 labelColor(
            ((CHANNEL_COLORS[ch] >> 0) & 0xFF) / 255.0f,
            ((CHANNEL_COLORS[ch] >> 8) & 0xFF) / 255.0f,
            ((CHANNEL_COLORS[ch] >> 16) & 0xFF) / 255.0f,
            1.0f);
        ImGui::TextColored(labelColor, "%s", channel.name.c_str());

        // Volume fader (vertical)
        ImGui::VSliderFloat("##vol", ImVec2(30, 150), &channel.volume, 0.0f, 1.0f, "");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Volume: %.0f%%", channel.volume * 100.0f);
        }

        // Pan knob
        ImGui::SetNextItemWidth(50);
        ImGui::SliderFloat("##pan", &channel.pan, -1.0f, 1.0f, "");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pan: %.0f", channel.pan * 100.0f);
        }

        // Mute/Solo
        if (ImGui::Checkbox("M", &channel.muted)) {}
        ImGui::SameLine();
        if (ImGui::Checkbox("S", &channel.solo)) {}

        // Select button
        bool isSelected = (ch == ui.selectedChannel);
        if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 0.4f, 1.0f));
        if (ImGui::Button("SEL", ImVec2(50, 20))) {
            ui.selectedChannel = ch;
        }
        if (isSelected) ImGui::PopStyleColor();

        ImGui::PopID();
        ImGui::EndGroup();

        if (ch < 7) ImGui::SameLine();
    }

    ImGui::End();
}

// ============================================================================
// Channel Editor (Oscillator & Effects)
// ============================================================================
inline void DrawChannelEditor(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("Channel Editor");

    if (ui.selectedChannel < 0 || ui.selectedChannel >= 8) {
        ImGui::Text("No channel selected");
        ImGui::End();
        return;
    }

    auto& channel = project.channels[ui.selectedChannel];
    auto& osc = channel.oscillator;

    ImGui::TextColored(ImVec4(
        ((CHANNEL_COLORS[ui.selectedChannel] >> 0) & 0xFF) / 255.0f,
        ((CHANNEL_COLORS[ui.selectedChannel] >> 8) & 0xFF) / 255.0f,
        ((CHANNEL_COLORS[ui.selectedChannel] >> 16) & 0xFF) / 255.0f,
        1.0f), "Channel: %s", channel.name.c_str());

    ImGui::Separator();

    // Oscillator settings
    if (ImGui::CollapsingHeader("Oscillator", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* oscTypes[] = {"Pulse", "Triangle", "Sawtooth", "Sine", "Noise", "Custom"};
        int oscType = static_cast<int>(osc.type);
        if (ImGui::Combo("Type", &oscType, oscTypes, IM_ARRAYSIZE(oscTypes))) {
            osc.type = static_cast<OscillatorType>(oscType);
            seq.updateChannelConfigs();
        }

        if (osc.type == OscillatorType::Pulse) {
            if (ImGui::SliderFloat("Pulse Width", &osc.pulseWidth, 0.05f, 0.95f, "%.0f%%")) {
                seq.updateChannelConfigs();
            }

            // Preset buttons
            if (ImGui::Button("12.5%")) { osc.pulseWidth = 0.125f; seq.updateChannelConfigs(); }
            ImGui::SameLine();
            if (ImGui::Button("25%")) { osc.pulseWidth = 0.25f; seq.updateChannelConfigs(); }
            ImGui::SameLine();
            if (ImGui::Button("50%")) { osc.pulseWidth = 0.50f; seq.updateChannelConfigs(); }
            ImGui::SameLine();
            if (ImGui::Button("75%")) { osc.pulseWidth = 0.75f; seq.updateChannelConfigs(); }
        }

        if (osc.type == OscillatorType::Triangle || osc.type == OscillatorType::Custom) {
            if (ImGui::SliderFloat("Triangle Slope", &osc.triangleSlope, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
        }

        if (osc.type == OscillatorType::Noise) {
            if (ImGui::Checkbox("Short Mode (metallic)", &osc.noiseShortMode)) {
                seq.updateChannelConfigs();
            }
        }

        ImGui::SliderFloat("Detune (cents)", &osc.detune, -100.0f, 100.0f);
    }

    // Envelope
    if (ImGui::CollapsingHeader("Envelope (ADSR)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Attack", &channel.envelope.attack, 0.001f, 2.0f, "%.3f s");
        ImGui::SliderFloat("Decay", &channel.envelope.decay, 0.001f, 2.0f, "%.3f s");
        ImGui::SliderFloat("Sustain", &channel.envelope.sustain, 0.0f, 1.0f, "%.0f%%");
        ImGui::SliderFloat("Release", &channel.envelope.release, 0.001f, 2.0f, "%.3f s");
    }

    // Effects
    if (ImGui::CollapsingHeader("Effects")) {
        auto& fx = seq.getSynth(ui.selectedChannel).effects();

        // Bitcrusher
        ImGui::Checkbox("Bitcrusher", &fx.bitcrusherEnabled);
        if (fx.bitcrusherEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Bit Depth", &fx.bitcrusher.bitDepth, 1.0f, 16.0f);
            ImGui::SliderFloat("Sample Rate Div", &fx.bitcrusher.sampleRateReduction, 1.0f, 32.0f);
            ImGui::Unindent();
        }

        // Distortion
        ImGui::Checkbox("Distortion", &fx.distortionEnabled);
        if (fx.distortionEnabled) {
            ImGui::Indent();
            const char* distTypes[] = {"Tanh", "Hard Clip", "Foldback", "Asymmetric"};
            int distType = static_cast<int>(fx.distortion.type);
            ImGui::Combo("Type", &distType, distTypes, IM_ARRAYSIZE(distTypes));
            fx.distortion.type = static_cast<DistortionType>(distType);
            ImGui::SliderFloat("Drive", &fx.distortion.drive, 1.0f, 10.0f);
            ImGui::SliderFloat("Mix", &fx.distortion.mix, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Filter
        ImGui::Checkbox("Filter", &fx.filterEnabled);
        if (fx.filterEnabled) {
            ImGui::Indent();
            const char* filterTypes[] = {"Low Pass", "High Pass", "Band Pass"};
            int filterType = static_cast<int>(fx.filter.type);
            ImGui::Combo("Type", &filterType, filterTypes, IM_ARRAYSIZE(filterTypes));
            fx.filter.type = static_cast<FilterType>(filterType);
            ImGui::SliderFloat("Cutoff", &fx.filter.cutoff, 20.0f, 10000.0f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Resonance", &fx.filter.resonance, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Delay
        ImGui::Checkbox("Delay", &fx.delayEnabled);
        if (fx.delayEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Time", &fx.delay.delayTime, 0.01f, 1.0f, "%.3f s");
            ImGui::SliderFloat("Feedback", &fx.delay.feedback, 0.0f, 0.95f);
            ImGui::SliderFloat("Mix##delay", &fx.delay.mix, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Chorus
        ImGui::Checkbox("Chorus", &fx.chorusEnabled);
        if (fx.chorusEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Rate##chorus", &fx.chorus.rate, 0.1f, 5.0f, "%.2f Hz");
            ImGui::SliderFloat("Depth##chorus", &fx.chorus.depth, 0.0f, 0.02f);
            ImGui::SliderFloat("Mix##chorus", &fx.chorus.mix, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Phaser
        ImGui::Checkbox("Phaser", &fx.phaserEnabled);
        if (fx.phaserEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Rate##phaser", &fx.phaser.rate, 0.1f, 2.0f, "%.2f Hz");
            ImGui::SliderFloat("Depth##phaser", &fx.phaser.depth, 0.0f, 1.0f);
            ImGui::SliderFloat("Feedback##phaser", &fx.phaser.feedback, 0.0f, 0.95f);
            ImGui::Unindent();
        }

        // Tremolo
        ImGui::Checkbox("Tremolo", &fx.tremoloEnabled);
        if (fx.tremoloEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Rate##trem", &fx.tremolo.rate, 0.5f, 20.0f, "%.1f Hz");
            ImGui::SliderFloat("Depth##trem", &fx.tremolo.depth, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Ring Mod
        ImGui::Checkbox("Ring Modulator", &fx.ringModEnabled);
        if (fx.ringModEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Freq##ring", &fx.ringMod.frequency, 20.0f, 2000.0f, "%.0f Hz");
            ImGui::SliderFloat("Mix##ring", &fx.ringMod.mix, 0.0f, 1.0f);
            ImGui::Unindent();
        }
    }

    ImGui::End();
}

// ============================================================================
// Pattern List
// ============================================================================
inline void DrawPatternList(Project& project, UIState& ui) {
    ImGui::Begin("Patterns");

    if (ImGui::Button("+ New Pattern")) {
        Pattern p;
        p.name = "Pattern " + std::to_string(project.patterns.size() + 1);
        project.patterns.push_back(p);
    }

    ImGui::Separator();

    for (size_t i = 0; i < project.patterns.size(); ++i) {
        auto& pattern = project.patterns[i];
        bool isSelected = (static_cast<int>(i) == ui.selectedPattern);

        if (ImGui::Selectable(pattern.name.c_str(), isSelected)) {
            ui.selectedPattern = static_cast<int>(i);
        }
    }

    ImGui::End();
}

// ============================================================================
// Note Editor Panel - Edit selected note properties
// ============================================================================
inline void DrawNoteEditor(Project& project, UIState& ui) {
    ImGui::Begin("Note Editor");

    if (ui.selectedPattern < 0 || ui.selectedPattern >= static_cast<int>(project.patterns.size())) {
        ImGui::TextDisabled("No pattern selected");
        ImGui::End();
        return;
    }

    Pattern& pattern = project.patterns[ui.selectedPattern];

    if (ui.selectedNoteIndex < 0 || ui.selectedNoteIndex >= static_cast<int>(pattern.notes.size())) {
        ImGui::TextDisabled("No note selected");
        ImGui::Separator();
        ImGui::TextWrapped("Select a note in the Piano Roll to edit its properties.");
        ImGui::TextWrapped("Use Select mode (S) and click on a note.");
        ImGui::End();
        return;
    }

    Note& note = pattern.notes[ui.selectedNoteIndex];

    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Editing Note %d", ui.selectedNoteIndex);
    ImGui::Separator();

    // Pitch editing
    if (ImGui::CollapsingHeader("Pitch", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Current pitch display
        ImGui::Text("Current: %s (MIDI %d)", noteToString(note.pitch).c_str(), note.pitch);

        // Semitone adjustment
        ImGui::Text("Semitone:");
        ImGui::SameLine();
        if (ImGui::Button("-1")) {
            note.pitch = std::max(0, note.pitch - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("+1")) {
            note.pitch = std::min(127, note.pitch + 1);
        }

        ImGui::SameLine(0, 20);

        // Octave adjustment
        ImGui::Text("Octave:");
        ImGui::SameLine();
        if (ImGui::Button("-12")) {
            note.pitch = std::max(0, note.pitch - 12);
        }
        ImGui::SameLine();
        if (ImGui::Button("+12")) {
            note.pitch = std::min(127, note.pitch + 12);
        }

        // Direct MIDI input
        ImGui::SetNextItemWidth(100);
        int midiNote = note.pitch;
        if (ImGui::InputInt("MIDI Note", &midiNote)) {
            note.pitch = std::clamp(midiNote, 0, 127);
        }

        // Quick note buttons (one octave)
        ImGui::Text("Quick Notes:");
        const char* noteLabels[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int currentOctave = note.pitch / 12;
        for (int i = 0; i < 12; ++i) {
            if (i > 0) ImGui::SameLine();
            bool isCurrentNote = (note.pitch % 12 == i);
            if (isCurrentNote) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            }
            if (ImGui::SmallButton(noteLabels[i])) {
                note.pitch = currentOctave * 12 + i;
            }
            if (isCurrentNote) {
                ImGui::PopStyleColor();
            }
        }

        // Octave selector
        ImGui::Text("Octave:");
        ImGui::SameLine();
        for (int oct = 0; oct <= 8; ++oct) {
            if (oct > 0) ImGui::SameLine();
            char octLabel[4];
            snprintf(octLabel, sizeof(octLabel), "%d", oct);
            bool isCurrentOct = (currentOctave == oct);
            if (isCurrentOct) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            }
            if (ImGui::SmallButton(octLabel)) {
                note.pitch = oct * 12 + (note.pitch % 12);
            }
            if (isCurrentOct) {
                ImGui::PopStyleColor();
            }
        }
    }

    // Position
    if (ImGui::CollapsingHeader("Position & Duration", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(120);
        if (ImGui::DragFloat("Start (beats)", &note.startTime, 0.0625f, 0.0f, 1000.0f, "%.3f")) {
            note.startTime = std::max(0.0f, note.startTime);
        }

        ImGui::SetNextItemWidth(120);
        if (ImGui::DragFloat("Duration (beats)", &note.duration, 0.0625f, 0.0625f, 16.0f, "%.3f")) {
            note.duration = std::max(0.0625f, note.duration);
        }

        // Quick duration buttons
        ImGui::Text("Quick:");
        ImGui::SameLine();
        if (ImGui::SmallButton("1/16")) note.duration = 0.0625f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/8")) note.duration = 0.125f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/4")) note.duration = 0.25f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/2")) note.duration = 0.5f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1")) note.duration = 1.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("2")) note.duration = 2.0f;
    }

    // Velocity
    if (ImGui::CollapsingHeader("Velocity", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("##velocity", &note.velocity, 0.0f, 1.0f, "%.0f%%");

        // Quick velocity buttons
        if (ImGui::SmallButton("25%")) note.velocity = 0.25f;
        ImGui::SameLine();
        if (ImGui::SmallButton("50%")) note.velocity = 0.5f;
        ImGui::SameLine();
        if (ImGui::SmallButton("75%")) note.velocity = 0.75f;
        ImGui::SameLine();
        if (ImGui::SmallButton("100%")) note.velocity = 1.0f;
    }

    // Fade In/Out
    if (ImGui::CollapsingHeader("Fade", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Fade In:");
        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("##fadein", &note.fadeIn, 0.0f, note.duration * 0.5f, "%.3f beats");

        // Quick fade in buttons
        if (ImGui::SmallButton("0##fi")) note.fadeIn = 0.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/16##fi")) note.fadeIn = 0.0625f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/8##fi")) note.fadeIn = 0.125f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/4##fi")) note.fadeIn = 0.25f;

        ImGui::Spacing();

        ImGui::Text("Fade Out:");
        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("##fadeout", &note.fadeOut, 0.0f, note.duration * 0.5f, "%.3f beats");

        // Quick fade out buttons
        if (ImGui::SmallButton("0##fo")) note.fadeOut = 0.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/16##fo")) note.fadeOut = 0.0625f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/8##fo")) note.fadeOut = 0.125f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/4##fo")) note.fadeOut = 0.25f;

        // Ensure fades don't exceed note duration
        float maxFade = note.duration * 0.5f;
        note.fadeIn = std::min(note.fadeIn, maxFade);
        note.fadeOut = std::min(note.fadeOut, maxFade);
    }

    // Per-note effects
    if (ImGui::CollapsingHeader("Note Effects")) {
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Vibrato", &note.vibrato, 0.0f, 1.0f);

        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Slide", &note.slide, -12.0f, 12.0f, "%.1f semitones");

        // Reset effects
        if (ImGui::Button("Reset Effects")) {
            note.vibrato = 0.0f;
            note.slide = 0.0f;
            note.arpeggio = 0;
        }
    }

    ImGui::Separator();

    // Actions
    if (ImGui::Button("Duplicate Note")) {
        Note newNote = note;
        newNote.startTime += note.duration;  // Place after current note
        pattern.notes.push_back(newNote);
        ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;

        // Auto-extend pattern length if note goes past current end
        float noteEnd = newNote.startTime + newNote.duration;
        if (noteEnd > pattern.length) {
            pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
        }
    }

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Delete Note")) {
        pattern.notes.erase(pattern.notes.begin() + ui.selectedNoteIndex);
        ui.selectedNoteIndex = -1;
    }
    ImGui::PopStyleColor();

    ImGui::End();
}

// ============================================================================
// View Tabs
// ============================================================================
inline void DrawViewTabs(UIState& ui) {
    ImGui::Begin("Views", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Piano Roll", ImVec2(100, 30))) {
        ui.currentView = ViewMode::PianoRoll;
    }
    ImGui::SameLine();
    if (ImGui::Button("Tracker", ImVec2(100, 30))) {
        ui.currentView = ViewMode::Tracker;
    }
    ImGui::SameLine();
    if (ImGui::Button("Arrange", ImVec2(100, 30))) {
        ui.currentView = ViewMode::Arrangement;
    }
    ImGui::SameLine();
    if (ImGui::Button("Mixer", ImVec2(100, 30))) {
        ui.currentView = ViewMode::Mixer;
    }

    ImGui::End();
}

// ============================================================================
// Sound Palette - Visual icons for each oscillator type
// ============================================================================
inline void DrawWaveformIcon(ImDrawList* drawList, ImVec2 pos, ImVec2 size, OscillatorType type, ImU32 color) {
    float cx = pos.x + size.x * 0.5f;
    float cy = pos.y + size.y * 0.5f;
    float hw = size.x * 0.4f;
    float hh = size.y * 0.35f;

    switch (type) {
        case OscillatorType::Pulse: {
            // Square wave icon
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh), ImVec2(pos.x + 4, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(pos.x + 4, cy - hh), ImVec2(cx, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh), ImVec2(cx, cy + hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh), ImVec2(pos.x + size.x - 4, cy + hh), color, 2.0f);
            break;
        }
        case OscillatorType::Triangle: {
            // Triangle wave icon
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(cx - hw/2, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx - hw/2, cy - hh), ImVec2(cx + hw/2, cy + hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx + hw/2, cy + hh), ImVec2(pos.x + size.x - 4, cy), color, 2.0f);
            break;
        }
        case OscillatorType::Sawtooth: {
            // Sawtooth wave icon
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh), ImVec2(cx, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh), ImVec2(cx, cy + hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh), ImVec2(pos.x + size.x - 4, cy - hh), color, 2.0f);
            break;
        }
        case OscillatorType::Sine: {
            // Sine wave icon (approximated with line segments)
            ImVec2 prev(pos.x + 4, cy);
            for (int i = 1; i <= 16; ++i) {
                float t = static_cast<float>(i) / 16.0f;
                float x = pos.x + 4 + t * (size.x - 8);
                float y = cy - std::sin(t * 6.28f) * hh;
                drawList->AddLine(prev, ImVec2(x, y), color, 2.0f);
                prev = ImVec2(x, y);
            }
            break;
        }
        case OscillatorType::Noise: {
            // Noise icon (random lines)
            float x = pos.x + 4;
            float step = (size.x - 8) / 8.0f;
            for (int i = 0; i < 8; ++i) {
                float y1 = cy + ((i * 7 + 3) % 5 - 2) * hh * 0.5f;
                float y2 = cy + (((i+1) * 7 + 3) % 5 - 2) * hh * 0.5f;
                drawList->AddLine(ImVec2(x, y1), ImVec2(x + step, y2), color, 2.0f);
                x += step;
            }
            break;
        }
        case OscillatorType::Custom: {
            // Custom wave icon (wavy line)
            drawList->AddText(ImVec2(pos.x + size.x/2 - 8, pos.y + size.y/2 - 6), color, "~");
            break;
        }

        // ================================================================
        // SYNTHS
        // ================================================================
        case OscillatorType::SynthLead: {
            // Lead - double saw icon
            float sw = hw * 0.4f;
            drawList->AddLine(ImVec2(cx - sw, cy + hh*0.6f), ImVec2(cx, cy - hh*0.6f), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.6f), ImVec2(cx, cy + hh*0.6f), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh*0.6f), ImVec2(cx + sw, cy - hh*0.6f), color, 2.0f);
            // Second saw slightly offset
            drawList->AddLine(ImVec2(cx - sw + 3, cy + hh*0.4f), ImVec2(cx + 3, cy - hh*0.4f), color, 1.5f);
            break;
        }
        case OscillatorType::SynthPad: {
            // Pad - soft waves
            ImVec2 prev(pos.x + 4, cy);
            for (int i = 1; i <= 12; ++i) {
                float t = static_cast<float>(i) / 12.0f;
                float x = pos.x + 4 + t * (size.x - 8);
                float y = cy - std::sin(t * 6.28f * 0.5f) * hh * 0.6f;
                drawList->AddLine(prev, ImVec2(x, y), color, 2.5f);
                prev = ImVec2(x, y);
            }
            break;
        }
        case OscillatorType::SynthBass: {
            // Bass - thick low wave
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(cx - hw*0.3f, cy + hh*0.8f), color, 3.0f);
            drawList->AddLine(ImVec2(cx - hw*0.3f, cy + hh*0.8f), ImVec2(cx + hw*0.3f, cy - hh*0.4f), color, 3.0f);
            drawList->AddLine(ImVec2(cx + hw*0.3f, cy - hh*0.4f), ImVec2(pos.x + size.x - 4, cy), color, 3.0f);
            break;
        }
        case OscillatorType::SynthPluck: {
            // Pluck - sharp attack then decay
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(pos.x + 8, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(pos.x + 8, cy - hh), ImVec2(pos.x + size.x - 4, cy + hh*0.3f), color, 2.0f);
            break;
        }
        case OscillatorType::SynthArp: {
            // Arp - staircase pattern
            float step = (size.x - 8) / 4.0f;
            for (int i = 0; i < 4; ++i) {
                float x = pos.x + 4 + i * step;
                float y = cy - hh + (i % 2) * hh * 1.5f;
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + step - 2, y + 4), color);
            }
            break;
        }
        case OscillatorType::SynthOrgan: {
            // Organ - drawbar-like lines
            for (int i = 0; i < 5; ++i) {
                float x = pos.x + 6 + i * (size.x - 12) / 5.0f;
                float h = hh * (0.5f + (4 - i) * 0.15f);
                drawList->AddLine(ImVec2(x, cy + h), ImVec2(x, cy - h), color, 2.5f);
            }
            break;
        }
        case OscillatorType::SynthStrings: {
            // Strings - wavy lines
            for (int j = -1; j <= 1; ++j) {
                ImVec2 prev(pos.x + 4, cy + j * hh * 0.4f);
                for (int i = 1; i <= 8; ++i) {
                    float t = static_cast<float>(i) / 8.0f;
                    float x = pos.x + 4 + t * (size.x - 8);
                    float y = cy + j * hh * 0.4f - std::sin(t * 6.28f + j) * hh * 0.2f;
                    drawList->AddLine(prev, ImVec2(x, y), color, 1.5f);
                    prev = ImVec2(x, y);
                }
            }
            break;
        }
        case OscillatorType::SynthBrass: {
            // Brass - bold angular
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh*0.5f), ImVec2(cx - hw*0.2f, cy - hh*0.8f), color, 2.5f);
            drawList->AddLine(ImVec2(cx - hw*0.2f, cy - hh*0.8f), ImVec2(cx + hw*0.2f, cy + hh*0.8f), color, 2.5f);
            drawList->AddLine(ImVec2(cx + hw*0.2f, cy + hh*0.8f), ImVec2(pos.x + size.x - 4, cy - hh*0.5f), color, 2.5f);
            break;
        }
        case OscillatorType::SynthChip: {
            // Chip - narrow pulse
            float pw = hw * 0.2f;
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh), ImVec2(pos.x + 4, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(pos.x + 4, cy - hh), ImVec2(cx - pw, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx - pw, cy - hh), ImVec2(cx - pw, cy + hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx - pw, cy + hh), ImVec2(pos.x + size.x - 4, cy + hh), color, 2.0f);
            break;
        }
        case OscillatorType::SynthBell: {
            // Bell - star/sparkle shape
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.5f, color, 8, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.8f), ImVec2(cx, cy + hh*0.8f), color, 1.5f);
            drawList->AddLine(ImVec2(cx - hh*0.8f, cy), ImVec2(cx + hh*0.8f, cy), color, 1.5f);
            break;
        }

        // ================================================================
        // KICKS
        // ================================================================
        case OscillatorType::Kick: {
            // Kick drum icon - large filled circle
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.8f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.8f, IM_COL32(255, 255, 255, 100), 12, 2.0f);
            break;
        }
        case OscillatorType::Kick808: {
            // 808 Kick - double ring (deeper)
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.9f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.6f, IM_COL32(0, 0, 0, 150), 12, 2.0f);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.9f, IM_COL32(255, 255, 255, 100), 12, 2.0f);
            break;
        }
        case OscillatorType::KickHard: {
            // Hard Kick - circle with impact lines
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.7f, color);
            drawList->AddLine(ImVec2(cx - hh, cy), ImVec2(cx - hh*0.8f, cy), color, 2.0f);
            drawList->AddLine(ImVec2(cx + hh*0.8f, cy), ImVec2(cx + hh, cy), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh), ImVec2(cx, cy - hh*0.8f), color, 2.0f);
            break;
        }
        case OscillatorType::KickSoft: {
            // Soft Kick - outlined circle with gradient feel
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.8f, color, 12, 3.0f);
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.4f, color);
            break;
        }

        // ================================================================
        // SNARES
        // ================================================================
        case OscillatorType::Snare: {
            // Snare drum icon - circle with X
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.7f, color, 12, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.5f, cy - hh*0.5f), ImVec2(cx + hh*0.5f, cy + hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(cx + hh*0.5f, cy - hh*0.5f), ImVec2(cx - hh*0.5f, cy + hh*0.5f), color, 2.0f);
            break;
        }
        case OscillatorType::Snare808: {
            // 808 Snare - circle with horizontal lines (wires)
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.7f, color, 12, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.5f, cy - hh*0.2f), ImVec2(cx + hh*0.5f, cy - hh*0.2f), color, 1.5f);
            drawList->AddLine(ImVec2(cx - hh*0.5f, cy + hh*0.2f), ImVec2(cx + hh*0.5f, cy + hh*0.2f), color, 1.5f);
            break;
        }
        case OscillatorType::SnareRim: {
            // Rimshot - small circle with outer ring
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.3f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.7f, color, 12, 2.0f);
            break;
        }
        case OscillatorType::Clap: {
            // Clap - multiple small circles (hands)
            drawList->AddCircleFilled(ImVec2(cx - hh*0.4f, cy - hh*0.2f), hh * 0.25f, color);
            drawList->AddCircleFilled(ImVec2(cx + hh*0.4f, cy - hh*0.2f), hh * 0.25f, color);
            drawList->AddCircleFilled(ImVec2(cx - hh*0.2f, cy + hh*0.3f), hh * 0.25f, color);
            drawList->AddCircleFilled(ImVec2(cx + hh*0.2f, cy + hh*0.3f), hh * 0.25f, color);
            break;
        }

        // ================================================================
        // HI-HATS
        // ================================================================
        case OscillatorType::HiHat: {
            // Closed Hi-hat - two overlapping circles
            drawList->AddCircle(ImVec2(cx - 3, cy), hh * 0.5f, color, 8, 2.0f);
            drawList->AddCircle(ImVec2(cx + 3, cy), hh * 0.5f, color, 8, 2.0f);
            break;
        }
        case OscillatorType::HiHatOpen: {
            // Open Hi-hat - two circles with gap
            drawList->AddCircle(ImVec2(cx - 4, cy - 2), hh * 0.45f, color, 8, 2.0f);
            drawList->AddCircle(ImVec2(cx + 4, cy + 2), hh * 0.45f, color, 8, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.6f, cy + hh*0.5f), ImVec2(cx + hh*0.6f, cy + hh*0.5f), color, 1.5f);
            break;
        }
        case OscillatorType::HiHatPedal: {
            // Pedal Hi-hat - single circle with pedal line
            drawList->AddCircle(ImVec2(cx, cy - hh*0.2f), hh * 0.4f, color, 8, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh*0.2f), ImVec2(cx, cy + hh*0.7f), color, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.3f, cy + hh*0.7f), ImVec2(cx + hh*0.3f, cy + hh*0.7f), color, 2.0f);
            break;
        }

        // ================================================================
        // TOMS
        // ================================================================
        case OscillatorType::Tom: {
            // Mid Tom - oval with center line
            drawList->AddEllipse(ImVec2(cx, cy), ImVec2(hw * 0.5f, hh * 0.5f), color, 0.0f, 12, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.3f), ImVec2(cx, cy + hh*0.3f), color, 2.0f);
            break;
        }
        case OscillatorType::TomLow: {
            // Floor Tom - larger oval, lower
            drawList->AddEllipse(ImVec2(cx, cy + 2), ImVec2(hw * 0.6f, hh * 0.6f), color, 0.0f, 12, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.3f, cy + 2), ImVec2(cx + hh*0.3f, cy + 2), color, 2.0f);
            break;
        }
        case OscillatorType::TomHigh: {
            // High Tom - smaller oval, higher
            drawList->AddEllipse(ImVec2(cx, cy - 2), ImVec2(hw * 0.4f, hh * 0.4f), color, 0.0f, 12, 2.0f);
            drawList->AddCircleFilled(ImVec2(cx, cy - 2), hh * 0.15f, color);
            break;
        }

        // ================================================================
        // CYMBALS
        // ================================================================
        case OscillatorType::Crash: {
            // Crash cymbal - large triangle/splash
            drawList->AddTriangle(
                ImVec2(cx, cy - hh*0.8f),
                ImVec2(cx - hw*0.7f, cy + hh*0.5f),
                ImVec2(cx + hw*0.7f, cy + hh*0.5f),
                color, 2.0f);
            drawList->AddLine(ImVec2(cx - hw*0.5f, cy - hh*0.3f), ImVec2(cx + hw*0.5f, cy + hh*0.2f), color, 1.5f);
            break;
        }
        case OscillatorType::Ride: {
            // Ride cymbal - circle with center dot
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.7f, color, 12, 2.0f);
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.2f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.45f, color, 8, 1.0f);
            break;
        }

        // ================================================================
        // PERCUSSION
        // ================================================================
        case OscillatorType::Cowbell: {
            // Cowbell - trapezoid shape
            drawList->AddQuadFilled(
                ImVec2(cx - hw*0.3f, cy - hh*0.6f),
                ImVec2(cx + hw*0.3f, cy - hh*0.6f),
                ImVec2(cx + hw*0.5f, cy + hh*0.6f),
                ImVec2(cx - hw*0.5f, cy + hh*0.6f),
                color);
            break;
        }
        case OscillatorType::Clave: {
            // Clave - two crossed sticks
            drawList->AddLine(ImVec2(cx - hw*0.6f, cy - hh*0.4f), ImVec2(cx + hw*0.2f, cy + hh*0.6f), color, 3.0f);
            drawList->AddLine(ImVec2(cx - hw*0.2f, cy + hh*0.6f), ImVec2(cx + hw*0.6f, cy - hh*0.4f), color, 3.0f);
            break;
        }
        case OscillatorType::Conga: {
            // Conga - tall oval drum
            drawList->AddEllipse(ImVec2(cx, cy - hh*0.2f), ImVec2(hw * 0.35f, hh * 0.3f), color, 0.0f, 10, 2.0f);
            drawList->AddLine(ImVec2(cx - hw*0.35f, cy - hh*0.2f), ImVec2(cx - hw*0.4f, cy + hh*0.6f), color, 2.0f);
            drawList->AddLine(ImVec2(cx + hw*0.35f, cy - hh*0.2f), ImVec2(cx + hw*0.4f, cy + hh*0.6f), color, 2.0f);
            drawList->AddLine(ImVec2(cx - hw*0.4f, cy + hh*0.6f), ImVec2(cx + hw*0.4f, cy + hh*0.6f), color, 2.0f);
            break;
        }
        case OscillatorType::Maracas: {
            // Maracas - circle with handle
            drawList->AddCircleFilled(ImVec2(cx, cy - hh*0.3f), hh * 0.4f, color);
            drawList->AddLine(ImVec2(cx, cy + hh*0.1f), ImVec2(cx, cy + hh*0.7f), color, 3.0f);
            break;
        }
        case OscillatorType::Tambourine: {
            // Tambourine - circle with jingles (dots around edge)
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.6f, color, 12, 2.0f);
            for (int j = 0; j < 6; ++j) {
                float angle = j * 3.14159f * 2.0f / 6.0f;
                float jx = cx + std::cos(angle) * hh * 0.6f;
                float jy = cy + std::sin(angle) * hh * 0.6f;
                drawList->AddCircleFilled(ImVec2(jx, jy), hh * 0.12f, color);
            }
            break;
        }

        default:
            // Fallback - simple rectangle
            drawList->AddRect(ImVec2(cx - hw*0.5f, cy - hh*0.5f),
                             ImVec2(cx + hw*0.5f, cy + hh*0.5f), color, 0.0f, 0, 2.0f);
            break;
    }
}

inline void DrawSoundPalette(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("Sound Palette");

    ImGui::Text("Click to select, then click on Piano Roll to place");
    ImGui::Separator();

    const char* oscNames[] = {
        // Oscillators (0-5)
        "Pulse", "Triangle", "Sawtooth", "Sine", "Noise", "Custom",
        // Synths (6-15)
        "Lead", "Pad", "Bass", "Pluck", "Arp",
        "Organ", "Strings", "Brass", "Chip", "Bell",
        // Kicks (16-19)
        "Kick", "Kick808", "KickHard", "KickSoft",
        // Snares (20-23)
        "Snare", "Snare808", "SnareRim", "Clap",
        // Hi-Hats (24-26)
        "HiHat", "HiHatOpen", "HiHatPedal",
        // Toms (27-29)
        "Tom", "TomLow", "TomHigh",
        // Cymbals (30-31)
        "Crash", "Ride",
        // Percussion (32-36)
        "Cowbell", "Clave", "Conga", "Maracas", "Tambourine"
    };
    const char* oscDesc[] = {
        // Oscillators
        "Square wave - Classic NES sound",
        "Triangle wave - Soft, flute-like",
        "Sawtooth wave - Rich, buzzy",
        "Sine wave - Pure, clean",
        "Noise - Percussion, hi-hats",
        "Custom - Adjustable shape",
        // Synths
        "Lead - Thick detuned sawtooths",
        "Pad - Soft, atmospheric sound",
        "Bass - Deep punchy bass",
        "Pluck - Short, plucky sound",
        "Arp - Crisp for arpeggios",
        "Organ - Classic drawbar organ",
        "Strings - Lush string ensemble",
        "Brass - Rich, brassy stab",
        "Chip - Classic 12.5% chiptune",
        "Bell - FM-like bell/chime",
        // Kicks
        "Standard kick with pitch sweep",
        "Deep 808 kick, more sub-bass",
        "Punchy tight kick, fast attack",
        "Soft warm kick, rounded",
        // Snares
        "Standard snare with noise",
        "Classic 808 snare, more tonal",
        "Rimshot, clicky",
        "Hand clap, multiple bursts",
        // Hi-Hats
        "Closed hi-hat",
        "Open hi-hat, longer decay",
        "Pedal hi-hat, very short",
        // Toms
        "Mid tom",
        "Floor tom, low pitch",
        "High tom",
        // Cymbals
        "Crash cymbal, long decay",
        "Ride cymbal, sustained ping",
        // Percussion
        "808 cowbell",
        "Wood block click",
        "Conga drum",
        "Shaker",
        "Jingly metallic"
    };
    constexpr int NUM_OSCILLATORS = 6;
    constexpr int NUM_SYNTHS = 10;
    constexpr int NUM_DRUMS = 21;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 iconSize(60, 40);

    // Oscillators section
    ImGui::Text("Oscillators:");
    for (int i = 0; i < 6; ++i) {
        ImGui::PushID(i);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool isChannelOsc = (static_cast<int>(project.channels[ui.selectedChannel].oscillator.type) == i);
        bool isPaletteSelected = (g_SelectedPaletteItem == i);

        // Background - highlight if selected for placement
        ImU32 bgColor = isPaletteSelected ? IM_COL32(80, 120, 80, 255) :
                        isChannelOsc ? IM_COL32(60, 100, 60, 255) : IM_COL32(40, 40, 50, 255);
        ImU32 waveColor = isPaletteSelected ? IM_COL32(200, 255, 200, 255) :
                          isChannelOsc ? IM_COL32(150, 255, 150, 255) : IM_COL32(100, 180, 100, 255);
        ImU32 borderColor = isPaletteSelected ? IM_COL32(150, 255, 150, 255) :
                            isChannelOsc ? IM_COL32(100, 200, 100, 255) : IM_COL32(80, 80, 90, 255);

        drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), bgColor, 4.0f);
        drawList->AddRect(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 3.0f : 1.0f);

        // Draw waveform icon
        DrawWaveformIcon(drawList, pos, iconSize, static_cast<OscillatorType>(i), waveColor);

        // Button for click and drag
        ImGui::InvisibleButton("##osc", iconSize);

        // Click to select for placement (toggle)
        if (ImGui::IsItemClicked()) {
            if (g_SelectedPaletteItem == i) {
                g_SelectedPaletteItem = -1;  // Deselect
            } else {
                g_SelectedPaletteItem = i;  // Select
                ui.pianoRollMode = PianoRollMode::Draw;  // Auto-switch to Draw mode
                project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(i);
                seq.updateChannelConfigs();
            }
        }

        // Drag source (keep existing drag functionality)
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            g_DraggedOscillatorType = i;
            ImGui::SetDragDropPayload("OSC_TYPE", &i, sizeof(int));
            ImGui::Text("Drag: %s", oscNames[i]);
            DrawWaveformIcon(ImGui::GetWindowDrawList(),
                ImGui::GetCursorScreenPos(), ImVec2(40, 25),
                static_cast<OscillatorType>(i), IM_COL32(200, 255, 200, 255));
            ImGui::EndDragDropSource();
        }

        // Tooltip
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", oscNames[i]);
            ImGui::TextDisabled("%s", oscDesc[i]);
            if (isPaletteSelected) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "SELECTED - Click on Piano Roll to place");
            }
            ImGui::EndTooltip();
        }

        if (i < 5) ImGui::SameLine();
        ImGui::PopID();
    }

    // Synths section
    ImGui::Separator();
    ImGui::Text("Synths:");

    ImVec2 synthIconSize(55, 35);
    int synthsPerRow = 5;

    for (int i = NUM_OSCILLATORS; i < NUM_OSCILLATORS + NUM_SYNTHS; ++i) {
        ImGui::PushID(i);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool isChannelOsc = (static_cast<int>(project.channels[ui.selectedChannel].oscillator.type) == i);
        bool isPaletteSelected = (g_SelectedPaletteItem == i);

        // Synth colors - purple/blue theme
        ImU32 bgColor = isPaletteSelected ? IM_COL32(100, 80, 140, 255) :
                        isChannelOsc ? IM_COL32(80, 60, 120, 255) : IM_COL32(45, 40, 55, 255);
        ImU32 waveColor = isPaletteSelected ? IM_COL32(220, 180, 255, 255) :
                          isChannelOsc ? IM_COL32(180, 150, 220, 255) : IM_COL32(150, 120, 180, 255);
        ImU32 borderColor = isPaletteSelected ? IM_COL32(200, 150, 255, 255) :
                            isChannelOsc ? IM_COL32(150, 100, 200, 255) : IM_COL32(80, 70, 100, 255);

        drawList->AddRectFilled(pos, ImVec2(pos.x + synthIconSize.x, pos.y + synthIconSize.y), bgColor, 4.0f);
        drawList->AddRect(pos, ImVec2(pos.x + synthIconSize.x, pos.y + synthIconSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 3.0f : 1.0f);

        // Draw synth icon
        DrawWaveformIcon(drawList, pos, synthIconSize, static_cast<OscillatorType>(i), waveColor);

        // Button for click and drag
        ImGui::InvisibleButton("##synth", synthIconSize);

        // Click to select for placement (toggle)
        if (ImGui::IsItemClicked()) {
            if (g_SelectedPaletteItem == i) {
                g_SelectedPaletteItem = -1;  // Deselect
            } else {
                g_SelectedPaletteItem = i;  // Select
                ui.pianoRollMode = PianoRollMode::Draw;  // Auto-switch to Draw mode
                project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(i);
                seq.updateChannelConfigs();
            }
        }

        // Drag source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            g_DraggedOscillatorType = i;
            ImGui::SetDragDropPayload("OSC_TYPE", &i, sizeof(int));
            ImGui::Text("Drag: %s", oscNames[i]);
            DrawWaveformIcon(ImGui::GetWindowDrawList(),
                ImGui::GetCursorScreenPos(), ImVec2(40, 25),
                static_cast<OscillatorType>(i), IM_COL32(220, 180, 255, 255));
            ImGui::EndDragDropSource();
        }

        // Tooltip
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", oscNames[i]);
            ImGui::TextDisabled("%s", oscDesc[i]);
            if (isPaletteSelected) {
                ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "SELECTED - Click on Piano Roll to place");
            }
            ImGui::EndTooltip();
        }

        // Wrap after every synthsPerRow items
        int synthIndex = i - NUM_OSCILLATORS;
        if ((synthIndex + 1) % synthsPerRow != 0 && i < NUM_OSCILLATORS + NUM_SYNTHS - 1) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }

    // Drums section
    ImGui::Separator();
    ImGui::Text("Drums:");

    // Smaller icon size for drums to fit more
    ImVec2 drumIconSize(50, 32);
    int drumsPerRow = 5;
    int drumStartIndex = NUM_OSCILLATORS + NUM_SYNTHS;

    for (int i = drumStartIndex; i < drumStartIndex + NUM_DRUMS; ++i) {
        ImGui::PushID(i);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool isChannelOsc = (static_cast<int>(project.channels[ui.selectedChannel].oscillator.type) == i);
        bool isPaletteSelected = (g_SelectedPaletteItem == i);

        // Drum colors - more vibrant
        ImU32 bgColor = isPaletteSelected ? IM_COL32(120, 80, 80, 255) :
                        isChannelOsc ? IM_COL32(100, 60, 60, 255) : IM_COL32(50, 40, 40, 255);
        ImU32 waveColor = isPaletteSelected ? IM_COL32(255, 200, 200, 255) :
                          isChannelOsc ? IM_COL32(255, 150, 150, 255) : IM_COL32(200, 120, 120, 255);
        ImU32 borderColor = isPaletteSelected ? IM_COL32(255, 150, 150, 255) :
                            isChannelOsc ? IM_COL32(200, 100, 100, 255) : IM_COL32(90, 80, 80, 255);

        drawList->AddRectFilled(pos, ImVec2(pos.x + drumIconSize.x, pos.y + drumIconSize.y), bgColor, 4.0f);
        drawList->AddRect(pos, ImVec2(pos.x + drumIconSize.x, pos.y + drumIconSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 3.0f : 1.0f);

        // Draw drum icon
        DrawWaveformIcon(drawList, pos, drumIconSize, static_cast<OscillatorType>(i), waveColor);

        // Button for click and drag
        ImGui::InvisibleButton("##drum", drumIconSize);

        // Click to select for placement (toggle)
        if (ImGui::IsItemClicked()) {
            if (g_SelectedPaletteItem == i) {
                g_SelectedPaletteItem = -1;  // Deselect
            } else {
                g_SelectedPaletteItem = i;  // Select
                ui.pianoRollMode = PianoRollMode::Draw;  // Auto-switch to Draw mode
                project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(i);
                seq.updateChannelConfigs();
            }
        }

        // Drag source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            g_DraggedOscillatorType = i;
            ImGui::SetDragDropPayload("OSC_TYPE", &i, sizeof(int));
            ImGui::Text("Drag: %s", oscNames[i]);
            DrawWaveformIcon(ImGui::GetWindowDrawList(),
                ImGui::GetCursorScreenPos(), ImVec2(40, 25),
                static_cast<OscillatorType>(i), IM_COL32(255, 200, 200, 255));
            ImGui::EndDragDropSource();
        }

        // Tooltip
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", oscNames[i]);
            ImGui::TextDisabled("%s", oscDesc[i]);
            if (isPaletteSelected) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "SELECTED - Click on Piano Roll to place");
            }
            ImGui::EndTooltip();
        }

        // Wrap after every drumsPerRow items
        int drumIndex = i - drumStartIndex;
        if ((drumIndex + 1) % drumsPerRow != 0 && i < drumStartIndex + NUM_DRUMS - 1) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }

    ImGui::Separator();

    // Show selected item
    if (g_SelectedPaletteItem >= 0) {
        ImVec4 color;
        if (g_SelectedPaletteItem < NUM_OSCILLATORS) {
            color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);  // Green for oscillators
        } else if (g_SelectedPaletteItem < NUM_OSCILLATORS + NUM_SYNTHS) {
            color = ImVec4(0.8f, 0.6f, 1.0f, 1.0f);  // Purple for synths
        } else {
            color = ImVec4(1.0f, 0.6f, 0.6f, 1.0f);  // Red for drums
        }
        ImGui::TextColored(color, "Selected: %s", oscNames[g_SelectedPaletteItem]);
        ImGui::SameLine();
        if (ImGui::SmallButton("Deselect")) {
            g_SelectedPaletteItem = -1;
        }
    } else {
        ImGui::TextDisabled("No sound selected");
    }

    ImGui::Text("Channel: %s", project.channels[ui.selectedChannel].name.c_str());

    ImGui::End();
}

} // namespace ChiptuneTracker
