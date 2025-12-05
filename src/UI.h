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
static float g_SelectedDurationMult = 1.0f;  // Duration multiplier for drums (0.5 = short, 1.0 = normal, 2.0 = long)

// Palette category expansion state
static bool g_PaletteExpanded_Oscillators = false;
static bool g_PaletteExpanded_Synths = false;
static bool g_PaletteExpanded_Kicks = true;
static bool g_PaletteExpanded_Snares = true;
static bool g_PaletteExpanded_HiHats = true;
static bool g_PaletteExpanded_Toms = false;
static bool g_PaletteExpanded_Cymbals = false;
static bool g_PaletteExpanded_Percussion = false;

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

        case Theme::FrutigerAero:
            // Frutiger Aero - Glossy, bubbly, glass-like Web 2.0 aesthetic
            colors[ImGuiCol_WindowBg] = ImVec4(0.85f, 0.92f, 0.98f, 0.95f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.90f, 0.95f, 1.00f, 0.90f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.95f, 0.98f, 1.00f, 0.98f);
            colors[ImGuiCol_Border] = ImVec4(0.50f, 0.70f, 0.90f, 0.50f);
            colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.80f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.80f, 0.92f, 1.00f, 0.90f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.70f, 0.88f, 1.00f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.40f, 0.65f, 0.90f, 0.90f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.55f, 0.85f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.75f, 0.88f, 0.98f, 0.95f);
            colors[ImGuiCol_Header] = ImVec4(0.50f, 0.75f, 0.95f, 0.70f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.70f, 0.95f, 0.85f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.65f, 0.90f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.45f, 0.72f, 0.95f, 0.85f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.65f, 0.95f, 0.95f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.55f, 0.90f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.60f, 0.95f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.50f, 0.85f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.15f, 0.25f, 0.40f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.55f, 0.65f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.65f, 0.82f, 0.95f, 0.90f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.50f, 0.75f, 0.95f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.40f, 0.68f, 0.92f, 1.00f);

            // Piano roll colors - Frutiger Aero (glossy, bright)
            g_PianoRollColors.keyWhite = IM_COL32(240, 248, 255, 255);
            g_PianoRollColors.keyBlack = IM_COL32(70, 100, 140, 255);
            g_PianoRollColors.gridLine = IM_COL32(180, 200, 220, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(100, 150, 200, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(50, 120, 200, 255);
            g_PianoRollColors.noteDefault = IM_COL32(80, 160, 255, 255);
            g_PianoRollColors.noteSelected = IM_COL32(255, 180, 50, 255);
            g_PianoRollColors.playhead = IM_COL32(50, 200, 100, 255);
            g_PianoRollColors.background = IM_COL32(230, 240, 250, 255);
            break;

        case Theme::Minimal:
            // Minimal - Clean, flat, modern design
            colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.28f, 0.50f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.95f, 0.30f, 0.35f, 1.00f);  // Red accent
            colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 0.40f, 0.45f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.85f, 0.25f, 0.30f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.30f, 0.35f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.40f, 0.45f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.30f, 0.35f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);

            // Piano roll colors - Minimal (clean, subtle)
            g_PianoRollColors.keyWhite = IM_COL32(35, 35, 40, 255);
            g_PianoRollColors.keyBlack = IM_COL32(20, 20, 24, 255);
            g_PianoRollColors.gridLine = IM_COL32(50, 50, 55, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(70, 70, 78, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(242, 77, 89, 255);  // Red accent
            g_PianoRollColors.noteDefault = IM_COL32(242, 77, 89, 255);
            g_PianoRollColors.noteSelected = IM_COL32(255, 255, 255, 255);
            g_PianoRollColors.playhead = IM_COL32(255, 100, 110, 255);
            g_PianoRollColors.background = IM_COL32(28, 28, 32, 255);
            break;

        case Theme::Vaporwave:
            // Vaporwave - Pink/cyan aesthetic, floating shapes, retro-futurism
            colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.02f, 0.12f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.01f, 0.10f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.03f, 0.15f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.90f, 0.40f, 0.80f, 0.40f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.05f, 0.20f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.10f, 0.35f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.15f, 0.45f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.05f, 0.25f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.50f, 0.15f, 0.60f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.03f, 0.18f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.70f, 0.20f, 0.80f, 0.50f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.80f, 0.30f, 0.90f, 0.70f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.90f, 0.40f, 1.00f, 0.90f);
            colors[ImGuiCol_Button] = ImVec4(0.00f, 0.80f, 0.90f, 0.80f);  // Cyan
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.90f, 1.00f, 0.90f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.70f, 0.80f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 0.40f, 0.80f, 1.00f);  // Pink
            colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.50f, 0.90f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(1.00f, 0.85f, 0.95f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.40f, 0.55f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.30f, 0.10f, 0.40f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.60f, 0.20f, 0.70f, 0.90f);
            colors[ImGuiCol_TabActive] = ImVec4(0.50f, 0.15f, 0.60f, 1.00f);

            // Piano roll colors - Vaporwave (pink/cyan contrast)
            g_PianoRollColors.keyWhite = IM_COL32(30, 15, 40, 255);
            g_PianoRollColors.keyBlack = IM_COL32(15, 5, 25, 255);
            g_PianoRollColors.gridLine = IM_COL32(80, 30, 100, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(150, 50, 180, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(0, 220, 220, 255);
            g_PianoRollColors.noteDefault = IM_COL32(255, 100, 200, 255);
            g_PianoRollColors.noteSelected = IM_COL32(0, 255, 255, 255);
            g_PianoRollColors.playhead = IM_COL32(255, 180, 220, 255);
            g_PianoRollColors.background = IM_COL32(20, 5, 30, 255);
            break;

        case Theme::RetroTerminal:
            // Retro Terminal - Amber CRT phosphor, vintage computer aesthetic
            colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.02f, 0.00f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.01f, 0.01f, 0.00f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.03f, 0.00f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.80f, 0.55f, 0.00f, 0.40f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.06f, 0.00f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.10f, 0.00f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.15f, 0.00f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.04f, 0.00f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.14f, 0.00f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.06f, 0.04f, 0.00f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.40f, 0.28f, 0.00f, 0.60f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.38f, 0.00f, 0.80f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.70f, 0.48f, 0.00f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.35f, 0.24f, 0.00f, 0.70f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.35f, 0.00f, 0.85f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.65f, 0.45f, 0.00f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.62f, 0.00f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.72f, 0.10f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.70f, 0.00f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(1.00f, 0.75f, 0.20f, 1.00f);  // Amber phosphor
            colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.35f, 0.05f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.10f, 0.00f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.40f, 0.28f, 0.00f, 0.80f);
            colors[ImGuiCol_TabActive] = ImVec4(0.30f, 0.20f, 0.00f, 1.00f);

            // Piano roll colors - Retro Terminal (amber glow)
            g_PianoRollColors.keyWhite = IM_COL32(25, 18, 0, 255);
            g_PianoRollColors.keyBlack = IM_COL32(12, 8, 0, 255);
            g_PianoRollColors.gridLine = IM_COL32(60, 42, 0, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(120, 84, 0, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(200, 140, 0, 255);
            g_PianoRollColors.noteDefault = IM_COL32(255, 180, 50, 255);
            g_PianoRollColors.noteSelected = IM_COL32(255, 230, 150, 255);
            g_PianoRollColors.playhead = IM_COL32(255, 200, 80, 255);
            g_PianoRollColors.background = IM_COL32(8, 5, 0, 255);
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

    // Scale based on screen size (reference: 1920x1080)
    float scaleFactor = std::max(screenSize.x / 1920.0f, screenSize.y / 1080.0f);
    int columnWidth = static_cast<int>(14 * scaleFactor);
    int charHeight = static_cast<int>(16 * scaleFactor);

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
    // Faster chaser animation
    g_ChaserOffset += deltaTime * 120.0f;  // Faster (was 40)
    if (g_ChaserOffset > 1000.0f) g_ChaserOffset -= 1000.0f;

    // Color cycling (faster for vibrant effect)
    g_ChaserColorPhase += deltaTime * 1.5f;  // 3x faster color changes
    if (g_ChaserColorPhase > 3.0f) g_ChaserColorPhase -= 3.0f;

    // Grid pulse timer for center-outward effect (very slow = hypnotic)
    static float gridPulseTimer = 0.0f;
    gridPulseTimer += deltaTime * 0.125f;  // 1/16 original speed - very slow and smooth
    if (gridPulseTimer > 10.0f) gridPulseTimer -= 10.0f;

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

    // ========================================================================
    // Enhanced Grid Floor with Bold Pulsing Animation
    // Lines become bolder as the pulse wave moves outward from center
    // ========================================================================
    float vanishY = horizonY;
    float vanishX = sunX;
    int numVerticalLines = 40;  // More lines for denser grid

    // Create multiple pulse waves emanating from center
    float pulseWave1 = fmodf(gridPulseTimer * 0.8f, 1.0f);  // Main pulse
    float pulseWave2 = fmodf(gridPulseTimer * 0.8f + 0.5f, 1.0f);  // Secondary pulse (offset)

    // Vertical perspective lines
    for (int i = -numVerticalLines / 2; i <= numVerticalLines / 2; ++i) {
        float x = vanishX + i * (screenSize.x / numVerticalLines) * 2.5f;
        float distFromCenter = std::abs(static_cast<float>(i)) / (numVerticalLines / 2.0f);

        // Calculate pulse intensity - peaks when pulse wave reaches this distance
        float pulse1Dist = std::abs(distFromCenter - pulseWave1);
        float pulse2Dist = std::abs(distFromCenter - pulseWave2);
        float pulse1 = std::max(0.0f, 1.0f - pulse1Dist * 4.0f);  // Sharp falloff
        float pulse2 = std::max(0.0f, 1.0f - pulse2Dist * 4.0f) * 0.6f;  // Secondary weaker
        float pulse = std::min(1.0f, pulse1 + pulse2);
        pulse = pulse * pulse;  // Sharper peaks

        // Base visibility increases with distance from center (lines more visible at edges)
        float baseAlpha = 0.5f + distFromCenter * 0.3f;

        // Lines get BOLDER as pulse reaches them (outward from center)
        float thickness = 1.5f + pulse * 4.0f;  // Much thicker when pulsed
        float alpha = baseAlpha + pulse * 0.5f;

        // Color: magenta base, shifts to bright cyan when pulsed
        int r = static_cast<int>(255 * (1.0f - pulse * 0.7f));
        int g = static_cast<int>(50 + 205 * pulse);
        int b = 255;

        // Draw glow layer first (wider, more transparent)
        if (pulse > 0.1f) {
            drawList->AddLine(
                ImVec2(vanishX, vanishY),
                ImVec2(x, screenSize.y),
                IM_COL32(r, g, b, static_cast<int>(40 * pulse)), thickness + 6.0f);
        }

        // Draw main line
        drawList->AddLine(
            ImVec2(vanishX, vanishY),
            ImVec2(x, screenSize.y),
            IM_COL32(r, g, b, static_cast<int>(180 * alpha)), thickness);

        // Draw bright core when pulsed
        if (pulse > 0.3f) {
            drawList->AddLine(
                ImVec2(vanishX, vanishY),
                ImVec2(x, screenSize.y),
                IM_COL32(255, 255, 255, static_cast<int>(100 * pulse)), thickness * 0.3f);
        }
    }

    // Horizontal grid lines (closer together near horizon) with outward pulse
    int numHorizontalLines = 25;  // More lines
    for (int i = 1; i <= numHorizontalLines; ++i) {
        float t = static_cast<float>(i) / numHorizontalLines;
        float y = horizonY + (screenSize.y - horizonY) * t * t;  // Quadratic spacing

        // Distance from horizon (0 = at horizon, 1 = at bottom)
        float distFromHorizon = t;

        // Calculate pulse intensity for horizontal lines
        float pulse1Dist = std::abs(distFromHorizon - pulseWave1);
        float pulse2Dist = std::abs(distFromHorizon - pulseWave2);
        float pulse1 = std::max(0.0f, 1.0f - pulse1Dist * 3.5f);
        float pulse2 = std::max(0.0f, 1.0f - pulse2Dist * 3.5f) * 0.6f;
        float pulse = std::min(1.0f, pulse1 + pulse2);
        pulse = pulse * pulse;

        // Lines more visible as they get further from horizon
        float baseAlpha = 0.4f + distFromHorizon * 0.4f;

        // Thickness increases dramatically with pulse
        float thickness = 1.5f + pulse * 4.5f + distFromHorizon * 1.0f;  // Also thicker at bottom
        float alpha = baseAlpha + pulse * 0.6f;

        // Color: magenta to cyan shift
        int r = static_cast<int>(255 * (1.0f - pulse * 0.7f));
        int g = static_cast<int>(50 + 205 * pulse);
        int b = 255;

        // Glow layer
        if (pulse > 0.1f) {
            drawList->AddLine(
                ImVec2(0, y),
                ImVec2(screenSize.x, y),
                IM_COL32(r, g, b, static_cast<int>(35 * pulse)), thickness + 8.0f);
        }

        // Main line
        drawList->AddLine(
            ImVec2(0, y),
            ImVec2(screenSize.x, y),
            IM_COL32(r, g, b, static_cast<int>(160 * alpha)), thickness);

        // Bright core when pulsed
        if (pulse > 0.3f) {
            drawList->AddLine(
                ImVec2(0, y),
                ImVec2(screenSize.x, y),
                IM_COL32(255, 255, 255, static_cast<int>(90 * pulse)), thickness * 0.25f);
        }
    }

    // ========================================================================
    // Smooth Gradient Chasers (top and bottom)
    // ========================================================================
    // Scale effects based on screen size (reference: 1920x1080)
    float scaleFactor = std::max(screenSize.x / 1920.0f, screenSize.y / 1080.0f);
    float chaserHeight = 8.0f * scaleFactor;
    float glowHeight = 20.0f * scaleFactor;
    float stepSize = 2.0f * scaleFactor;  // Larger steps for higher res = better performance

    // Top chaser - smooth gradient wave
    float barWidth = stepSize + 1.0f;
    for (float x = 0; x < screenSize.x; x += stepSize) {
        float phase = x * 0.01f - g_ChaserOffset * 0.05f;
        float brightness = (sinf(phase) + 1.0f) * 0.5f;
        brightness = brightness * brightness;  // Sharper peaks

        float colorPhase = g_ChaserColorPhase + x * 0.002f;
        ImU32 color = GetNeonColor(colorPhase, brightness);

        // Main chaser bar
        drawList->AddRectFilled(
            ImVec2(x, 0),
            ImVec2(x + barWidth, chaserHeight * brightness + 2.0f * scaleFactor),
            color);

        // Glow below
        if (brightness > 0.3f) {
            ImU32 glowColor = GetNeonColor(colorPhase, brightness * 0.3f);
            drawList->AddRectFilledMultiColor(
                ImVec2(x, chaserHeight),
                ImVec2(x + barWidth, chaserHeight + glowHeight * brightness),
                glowColor, glowColor,
                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
        }
    }

    // Bottom chaser - smooth gradient wave (reversed direction, different color offset)
    for (float x = 0; x < screenSize.x; x += stepSize) {
        float phase = (screenSize.x - x) * 0.01f - g_ChaserOffset * 0.05f;
        float brightness = (sinf(phase) + 1.0f) * 0.5f;
        brightness = brightness * brightness;

        float colorPhase = g_ChaserColorPhase + 1.5f + x * 0.002f;  // Offset color
        ImU32 color = GetNeonColor(colorPhase, brightness);

        // Main chaser bar
        drawList->AddRectFilled(
            ImVec2(x, screenSize.y - chaserHeight * brightness - 2.0f * scaleFactor),
            ImVec2(x + barWidth, screenSize.y),
            color);

        // Glow above
        if (brightness > 0.3f) {
            ImU32 glowColor = GetNeonColor(colorPhase, brightness * 0.3f);
            drawList->AddRectFilledMultiColor(
                ImVec2(x, screenSize.y - chaserHeight - glowHeight * brightness),
                ImVec2(x + barWidth, screenSize.y - chaserHeight),
                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                glowColor, glowColor);
        }
    }

    // ========================================================================
    // Simple Neon Side Chasers - Clean traveling light bars on edges
    // ========================================================================
    float sideWidth = 5.0f * scaleFactor;

    // Simple chaser effect - just smooth traveling lights
    for (float y = 0; y < screenSize.y; y += stepSize) {
        // Single smooth wave traveling down
        float phase = y * 0.008f + g_ChaserOffset * 0.025f;
        float brightness = (sinf(phase) + 1.0f) * 0.5f;
        brightness = brightness * brightness * brightness;  // Cubic for sharper peaks

        if (brightness < 0.1f) continue;  // Skip dim sections

        float colorPhase = g_ChaserColorPhase + y * 0.001f;
        ImU32 color = GetNeonColor(colorPhase, brightness);

        float lightHeight = barWidth * (1.0f + brightness);

        // Left edge - simple bar
        drawList->AddRectFilled(
            ImVec2(0, y),
            ImVec2(sideWidth * (0.5f + brightness * 1.5f), y + lightHeight),
            color);

        // Right edge - simple bar
        float rightWidth = sideWidth * (0.5f + brightness * 1.5f);
        drawList->AddRectFilled(
            ImVec2(screenSize.x - rightWidth, y),
            ImVec2(screenSize.x, y + lightHeight),
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
    hexTimer += deltaTime * 2.5f;  // Faster hex character animation
    flickerTimer += deltaTime;

    // Scale factor for higher resolutions (reference: 1920x1080)
    float scaleFactor = std::max(screenSize.x / 1920.0f, screenSize.y / 1080.0f);

    // Update global animation states (slower edge flashing)
    g_CyberpunkPulse += deltaTime * 1.5f;  // 1/4 speed for smoother edge pulse
    if (g_CyberpunkPulse > 6.28318f) g_CyberpunkPulse -= 6.28318f;

    g_DataStreamOffset += deltaTime * 180.0f;  // Faster data streams
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

                // Node dots (scaled for higher resolutions)
                drawList->AddCircleFilled(ImVec2(x, y), 3.0f * scaleFactor, IM_COL32(0, 150, 150, static_cast<int>(40 * alpha)));
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
    float borderWidth = (3.0f + pulseVal * 2.0f) * scaleFactor;
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

// ============================================================================
// Frutiger Aero Theme - Floating glossy bubbles and glass reflections
// ============================================================================
struct AeroBubble {
    float x, y;
    float radius;
    float speed;
    float wobble;
    float hue;  // 0-1 for color variation
};

static std::vector<AeroBubble> g_AeroBubbles;
static bool g_AeroInitialized = false;
static float g_AeroTime = 0.0f;

inline void InitAeroBubbles(ImVec2 screenSize) {
    if (g_AeroInitialized) return;
    g_AeroBubbles.clear();

    // Create 25 bubbles
    for (int i = 0; i < 25; ++i) {
        AeroBubble bubble;
        bubble.x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
        bubble.y = static_cast<float>(rand() % static_cast<int>(screenSize.y));
        bubble.radius = 20.0f + static_cast<float>(rand() % 60);
        bubble.speed = 15.0f + static_cast<float>(rand() % 30);
        bubble.wobble = static_cast<float>(rand() % 100) / 100.0f * 6.28f;
        bubble.hue = static_cast<float>(rand() % 100) / 100.0f;
        g_AeroBubbles.push_back(bubble);
    }
    g_AeroInitialized = true;
}

inline void DrawFrutigerAero(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    InitAeroBubbles(screenSize);
    g_AeroTime += deltaTime;

    // Sky gradient background
    ImU32 topColor = IM_COL32(180, 220, 255, 255);
    ImU32 bottomColor = IM_COL32(240, 248, 255, 255);
    drawList->AddRectFilledMultiColor(
        ImVec2(0, 0), screenSize,
        topColor, topColor, bottomColor, bottomColor);

    // Soft clouds (large semi-transparent ellipses)
    for (int i = 0; i < 5; ++i) {
        float cloudX = std::fmod(g_AeroTime * 8.0f + i * screenSize.x / 4.0f, screenSize.x + 400.0f) - 200.0f;
        float cloudY = 50.0f + i * 40.0f + sinf(g_AeroTime * 0.5f + i) * 10.0f;
        float cloudW = 200.0f + i * 30.0f;
        float cloudH = 60.0f + i * 10.0f;

        drawList->AddEllipseFilled(
            ImVec2(cloudX, cloudY), ImVec2(cloudW, cloudH),
            IM_COL32(255, 255, 255, 60), 0.0f, 32);
    }

    // Floating glossy bubbles
    for (auto& bubble : g_AeroBubbles) {
        // Float upward with wobble
        bubble.y -= bubble.speed * deltaTime;
        bubble.x += sinf(g_AeroTime * 2.0f + bubble.wobble) * 0.5f;

        // Wrap around
        if (bubble.y + bubble.radius < 0) {
            bubble.y = screenSize.y + bubble.radius;
            bubble.x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
        }

        // Draw bubble with glossy effect
        float r = bubble.radius;

        // Main bubble body (gradient blue to cyan)
        int baseR = static_cast<int>(100 + bubble.hue * 50);
        int baseG = static_cast<int>(180 + bubble.hue * 40);
        int baseB = static_cast<int>(230 + bubble.hue * 25);
        drawList->AddCircleFilled(ImVec2(bubble.x, bubble.y), r,
            IM_COL32(baseR, baseG, baseB, 120), 32);

        // Glossy highlight (top-left)
        float highlightX = bubble.x - r * 0.3f;
        float highlightY = bubble.y - r * 0.3f;
        drawList->AddCircleFilled(ImVec2(highlightX, highlightY), r * 0.4f,
            IM_COL32(255, 255, 255, 180), 16);

        // Secondary smaller highlight
        drawList->AddCircleFilled(ImVec2(highlightX + r * 0.15f, highlightY + r * 0.15f), r * 0.15f,
            IM_COL32(255, 255, 255, 220), 12);

        // Bubble rim (subtle)
        drawList->AddCircle(ImVec2(bubble.x, bubble.y), r,
            IM_COL32(100, 150, 200, 80), 32, 2.0f);
    }

    // Glass reflection bars at top
    for (int i = 0; i < 3; ++i) {
        float barY = 20.0f + i * 8.0f;
        float barWidth = screenSize.x * (0.3f - i * 0.08f);
        float barX = (screenSize.x - barWidth) / 2.0f + sinf(g_AeroTime + i) * 50.0f;
        drawList->AddRectFilled(
            ImVec2(barX, barY), ImVec2(barX + barWidth, barY + 3.0f),
            IM_COL32(255, 255, 255, 60 - i * 15));
    }
}

// ============================================================================
// Minimal Theme - Subtle geometric patterns and clean lines
// ============================================================================
static float g_MinimalTime = 0.0f;

inline void DrawMinimalTheme(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    g_MinimalTime += deltaTime;

    // Dark background
    drawList->AddRectFilled(ImVec2(0, 0), screenSize, IM_COL32(28, 28, 32, 255));

    // Subtle grid pattern (very faint)
    float gridSize = 60.0f;
    ImU32 gridColor = IM_COL32(45, 45, 52, 255);

    for (float x = 0; x < screenSize.x; x += gridSize) {
        drawList->AddLine(ImVec2(x, 0), ImVec2(x, screenSize.y), gridColor, 1.0f);
    }
    for (float y = 0; y < screenSize.y; y += gridSize) {
        drawList->AddLine(ImVec2(0, y), ImVec2(screenSize.x, y), gridColor, 1.0f);
    }

    // Animated accent line traveling across bottom
    float lineProgress = std::fmod(g_MinimalTime * 0.15f, 1.0f);
    float lineX = lineProgress * (screenSize.x + 400.0f) - 200.0f;
    float lineWidth = 200.0f;

    // Gradient line (red accent)
    for (float i = 0; i < lineWidth; i += 2.0f) {
        float alpha = sinf((i / lineWidth) * 3.14159f);
        drawList->AddLine(
            ImVec2(lineX + i, screenSize.y - 3.0f),
            ImVec2(lineX + i, screenSize.y),
            IM_COL32(242, 77, 89, static_cast<int>(alpha * 200)), 3.0f);
    }

    // Corner geometric accents (subtle triangles)
    float cornerSize = 80.0f;
    float pulse = (sinf(g_MinimalTime * 2.0f) + 1.0f) * 0.5f;
    int alpha = static_cast<int>(30 + pulse * 20);

    // Top-left corner
    drawList->AddTriangleFilled(
        ImVec2(0, 0),
        ImVec2(cornerSize, 0),
        ImVec2(0, cornerSize),
        IM_COL32(242, 77, 89, alpha));

    // Bottom-right corner
    drawList->AddTriangleFilled(
        ImVec2(screenSize.x, screenSize.y),
        ImVec2(screenSize.x - cornerSize, screenSize.y),
        ImVec2(screenSize.x, screenSize.y - cornerSize),
        IM_COL32(242, 77, 89, alpha));

    // Floating geometric shapes (very subtle)
    for (int i = 0; i < 5; ++i) {
        float shapeX = std::fmod(g_MinimalTime * 20.0f + i * 400.0f, screenSize.x + 100.0f) - 50.0f;
        float shapeY = screenSize.y * 0.3f + sinf(g_MinimalTime * 0.5f + i * 1.5f) * 100.0f;
        float size = 15.0f + i * 5.0f;

        // Hollow squares
        drawList->AddRect(
            ImVec2(shapeX - size, shapeY - size),
            ImVec2(shapeX + size, shapeY + size),
            IM_COL32(242, 77, 89, 40), 0.0f, 0, 1.5f);
    }
}

// ============================================================================
// Vaporwave Theme - Pink/cyan grid, floating shapes, retro aesthetic
// ============================================================================
static float g_VaporwaveTime = 0.0f;

struct VaporShape {
    float x, y, z;  // z for depth
    int type;       // 0=triangle, 1=circle, 2=square
    float rotation;
    float rotSpeed;
};

static std::vector<VaporShape> g_VaporShapes;
static bool g_VaporInitialized = false;

inline void InitVaporShapes(ImVec2 screenSize) {
    if (g_VaporInitialized) return;
    g_VaporShapes.clear();

    for (int i = 0; i < 15; ++i) {
        VaporShape shape;
        shape.x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
        shape.y = static_cast<float>(rand() % static_cast<int>(screenSize.y));
        shape.z = 0.3f + static_cast<float>(rand() % 70) / 100.0f;
        shape.type = rand() % 3;
        shape.rotation = static_cast<float>(rand() % 360) * 0.0174533f;
        shape.rotSpeed = (static_cast<float>(rand() % 100) / 100.0f - 0.5f) * 0.5f;
        g_VaporShapes.push_back(shape);
    }
    g_VaporInitialized = true;
}

inline void DrawVaporwaveTheme(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    InitVaporShapes(screenSize);
    g_VaporwaveTime += deltaTime;

    // Gradient background (deep purple to dark)
    ImU32 topColor = IM_COL32(40, 10, 60, 255);
    ImU32 bottomColor = IM_COL32(15, 5, 25, 255);
    drawList->AddRectFilledMultiColor(
        ImVec2(0, 0), screenSize,
        topColor, topColor, bottomColor, bottomColor);

    // Sun at bottom (pink/orange gradient circle)
    float sunRadius = screenSize.x * 0.25f;
    float sunY = screenSize.y + sunRadius * 0.3f;
    float sunX = screenSize.x / 2.0f;

    // Sun glow
    for (int i = 5; i >= 0; --i) {
        float glowRadius = sunRadius + i * 30.0f;
        int alpha = 20 - i * 3;
        drawList->AddCircleFilled(ImVec2(sunX, sunY), glowRadius,
            IM_COL32(255, 100, 150, alpha), 64);
    }

    // Main sun (with horizontal stripes cut out)
    for (float y = sunY - sunRadius; y < sunY + sunRadius; y += 8.0f) {
        if (static_cast<int>((y - sunY + sunRadius) / 16.0f) % 2 == 0) {
            float dy = y - sunY;
            float halfChord = sqrtf(std::max(0.0f, sunRadius * sunRadius - dy * dy));

            // Color gradient from top (orange) to bottom (pink)
            float t = (y - (sunY - sunRadius)) / (sunRadius * 2.0f);
            int r = static_cast<int>(255);
            int g = static_cast<int>(180 - t * 130);
            int b = static_cast<int>(100 + t * 100);

            drawList->AddRectFilled(
                ImVec2(sunX - halfChord, y),
                ImVec2(sunX + halfChord, y + 6.0f),
                IM_COL32(r, g, b, 255));
        }
    }

    // Perspective grid
    float horizonY = screenSize.y * 0.55f;
    float vanishX = screenSize.x / 2.0f;

    // Horizontal lines (receding into distance)
    for (int i = 0; i < 20; ++i) {
        float t = static_cast<float>(i) / 20.0f;
        float y = horizonY + powf(t, 1.5f) * (screenSize.y - horizonY);
        float alpha = t * 255;

        // Animate grid movement
        float offset = std::fmod(g_VaporwaveTime * 50.0f, (screenSize.y - horizonY) / 20.0f);
        y += offset * powf(t, 1.5f);
        if (y > screenSize.y) continue;

        drawList->AddLine(
            ImVec2(0, y), ImVec2(screenSize.x, y),
            IM_COL32(0, 220, 220, static_cast<int>(alpha * 0.6f)), 1.5f);
    }

    // Vertical lines (converging to vanishing point)
    for (int i = -10; i <= 10; ++i) {
        float bottomX = vanishX + i * 120.0f;
        drawList->AddLine(
            ImVec2(vanishX, horizonY),
            ImVec2(bottomX, screenSize.y),
            IM_COL32(255, 100, 200, 100), 1.5f);
    }

    // Floating shapes
    for (auto& shape : g_VaporShapes) {
        shape.rotation += shape.rotSpeed * deltaTime;
        shape.x += sinf(g_VaporwaveTime + shape.z * 10.0f) * 0.3f;
        shape.y -= 10.0f * deltaTime * shape.z;

        if (shape.y < -50) {
            shape.y = screenSize.y + 50;
            shape.x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
        }

        float size = 20.0f * shape.z + 10.0f;
        int alpha = static_cast<int>(shape.z * 150);

        ImVec2 center(shape.x, shape.y);

        if (shape.type == 0) {
            // Triangle (wireframe)
            ImVec2 p1(center.x, center.y - size);
            ImVec2 p2(center.x - size * 0.866f, center.y + size * 0.5f);
            ImVec2 p3(center.x + size * 0.866f, center.y + size * 0.5f);
            drawList->AddTriangle(p1, p2, p3, IM_COL32(0, 255, 255, alpha), 2.0f);
        } else if (shape.type == 1) {
            // Circle (wireframe)
            drawList->AddCircle(center, size, IM_COL32(255, 100, 200, alpha), 24, 2.0f);
        } else {
            // Square (wireframe, rotated)
            float c = cosf(shape.rotation);
            float s = sinf(shape.rotation);
            ImVec2 corners[4];
            float offsets[4][2] = {{-1,-1}, {1,-1}, {1,1}, {-1,1}};
            for (int j = 0; j < 4; ++j) {
                float ox = offsets[j][0] * size;
                float oy = offsets[j][1] * size;
                corners[j] = ImVec2(center.x + ox*c - oy*s, center.y + ox*s + oy*c);
            }
            for (int j = 0; j < 4; ++j) {
                drawList->AddLine(corners[j], corners[(j+1)%4], IM_COL32(255, 200, 100, alpha), 2.0f);
            }
        }
    }
}

// ============================================================================
// Retro Terminal Theme - Authentic CRT monitor simulation
// Features: Scanlines, phosphor glow, screen curvature, color bleeding
// ============================================================================
static float g_TerminalTime = 0.0f;
static float g_TerminalFlicker = 1.0f;
static float g_ScanlinePhase = 0.0f;

inline void DrawRetroTerminal(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    g_TerminalTime += deltaTime;
    g_ScanlinePhase += deltaTime * 60.0f;  // 60Hz refresh simulation

    // ========================================================================
    // Screen flicker (subtle brightness variation like old CRTs)
    // ========================================================================
    if (rand() % 100 < 3) {
        g_TerminalFlicker = 0.92f + static_cast<float>(rand() % 8) / 100.0f;
    } else {
        g_TerminalFlicker = g_TerminalFlicker * 0.92f + 1.0f * 0.08f;
    }

    // Base brightness multiplier
    float brightness = g_TerminalFlicker;

    // ========================================================================
    // Dark CRT background with phosphor base color
    // ========================================================================
    int bgR = static_cast<int>(12 * brightness);
    int bgG = static_cast<int>(8 * brightness);
    int bgB = static_cast<int>(2 * brightness);
    drawList->AddRectFilled(ImVec2(0, 0), screenSize, IM_COL32(bgR, bgG, bgB, 255));

    // ========================================================================
    // CRT Curvature simulation - darker edges (barrel distortion effect)
    // ========================================================================
    ImVec2 center(screenSize.x / 2.0f, screenSize.y / 2.0f);
    float maxDist = sqrtf(center.x * center.x + center.y * center.y);

    // Draw radial vignette using concentric rectangles
    for (int ring = 0; ring < 8; ++ring) {
        float t = static_cast<float>(ring) / 8.0f;
        float edgeX = screenSize.x * (0.5f - t * 0.5f);
        float edgeY = screenSize.y * (0.5f - t * 0.5f);
        int alpha = static_cast<int>((1.0f - t) * 60);

        // Corner darkening (more pronounced)
        drawList->AddRectFilled(
            ImVec2(0, 0), ImVec2(edgeX, edgeY),
            IM_COL32(0, 0, 0, alpha));
        drawList->AddRectFilled(
            ImVec2(screenSize.x - edgeX, 0), ImVec2(screenSize.x, edgeY),
            IM_COL32(0, 0, 0, alpha));
        drawList->AddRectFilled(
            ImVec2(0, screenSize.y - edgeY), ImVec2(edgeX, screenSize.y),
            IM_COL32(0, 0, 0, alpha));
        drawList->AddRectFilled(
            ImVec2(screenSize.x - edgeX, screenSize.y - edgeY), screenSize,
            IM_COL32(0, 0, 0, alpha));
    }

    // ========================================================================
    // PROMINENT SCANLINES - The signature CRT effect
    // ========================================================================
    float scanlineSpacing = 2.0f;  // Tight scanlines
    float scanlineOffset = std::fmod(g_ScanlinePhase * 0.5f, scanlineSpacing);

    for (float y = scanlineOffset; y < screenSize.y; y += scanlineSpacing) {
        // Alternating bright/dark scanlines
        int lineIndex = static_cast<int>(y / scanlineSpacing);
        if (lineIndex % 2 == 0) {
            // Dark scanline (the gap between phosphor rows)
            drawList->AddLine(
                ImVec2(0, y), ImVec2(screenSize.x, y),
                IM_COL32(0, 0, 0, 100), 1.0f);
        }
    }

    // ========================================================================
    // RGB Sub-pixel simulation (color fringing on edges)
    // ========================================================================
    // Subtle RGB separation at screen edges
    float fringeWidth = 3.0f;

    // Left edge - red shift
    drawList->AddRectFilled(
        ImVec2(0, 0), ImVec2(fringeWidth, screenSize.y),
        IM_COL32(80, 0, 0, 30));

    // Right edge - blue shift
    drawList->AddRectFilled(
        ImVec2(screenSize.x - fringeWidth, 0), screenSize,
        IM_COL32(0, 0, 80, 30));

    // ========================================================================
    // Phosphor glow (warm amber bloom)
    // ========================================================================
    float glowPulse = (sinf(g_TerminalTime * 0.3f) + 1.0f) * 0.5f;
    int glowR = static_cast<int>((255 * 0.15f + glowPulse * 20) * brightness);
    int glowG = static_cast<int>((180 * 0.15f + glowPulse * 15) * brightness);
    int glowB = static_cast<int>((50 * 0.15f + glowPulse * 5) * brightness);
    drawList->AddRectFilled(ImVec2(0, 0), screenSize,
        IM_COL32(glowR, glowG, glowB, static_cast<int>(25 * brightness)));

    // ========================================================================
    // Horizontal sync wobble (subtle screen shake)
    // ========================================================================
    float wobble = sinf(g_TerminalTime * 120.0f) * 0.3f;
    if (rand() % 500 < 1) {
        // Occasional horizontal tear/glitch
        float tearY = static_cast<float>(rand() % static_cast<int>(screenSize.y));
        float tearHeight = 2.0f + (rand() % 4);
        float tearOffset = (rand() % 20) - 10;
        drawList->AddRectFilled(
            ImVec2(tearOffset, tearY),
            ImVec2(screenSize.x + tearOffset, tearY + tearHeight),
            IM_COL32(255, 200, 100, 120));
    }

    // ========================================================================
    // Interlace effect (every other frame shows different lines)
    // ========================================================================
    int frameNum = static_cast<int>(g_TerminalTime * 30.0f) % 2;
    for (float y = static_cast<float>(frameNum); y < screenSize.y; y += 4.0f) {
        drawList->AddLine(
            ImVec2(0, y), ImVec2(screenSize.x, y),
            IM_COL32(255, 200, 80, 8), 1.0f);
    }

    // ========================================================================
    // Screen border (CRT bezel simulation)
    // ========================================================================
    float bezelWidth = 8.0f;

    // Outer dark bezel
    drawList->AddRect(
        ImVec2(0, 0), screenSize,
        IM_COL32(20, 15, 5, 255), 0.0f, 0, bezelWidth);

    // Inner glowing edge (phosphor bleed at edges)
    float edgePulse = (sinf(g_TerminalTime * 0.8f) + 1.0f) * 0.5f;
    int edgeGlow = static_cast<int>((50 + edgePulse * 30) * brightness);
    drawList->AddRect(
        ImVec2(bezelWidth, bezelWidth),
        ImVec2(screenSize.x - bezelWidth, screenSize.y - bezelWidth),
        IM_COL32(255, 180, 50, edgeGlow), 0.0f, 0, 2.0f);

    // ========================================================================
    // Ambient glow (light bleeding from screen)
    // ========================================================================
    // Top glow
    for (int i = 0; i < 5; ++i) {
        float glowHeight = 20.0f - i * 4.0f;
        int glowAlpha = static_cast<int>((15 - i * 3) * brightness);
        drawList->AddRectFilled(
            ImVec2(bezelWidth * 2, bezelWidth + i * 2),
            ImVec2(screenSize.x - bezelWidth * 2, bezelWidth + glowHeight + i * 2),
            IM_COL32(255, 200, 100, glowAlpha));
    }

    // ========================================================================
    // Cursor blink (classic block cursor)
    // ========================================================================
    if (static_cast<int>(g_TerminalTime * 1.5f) % 2 == 0) {
        float cursorX = 30.0f;
        float cursorY = screenSize.y - 40.0f;
        int cursorAlpha = static_cast<int>(220 * brightness);
        drawList->AddRectFilled(
            ImVec2(cursorX, cursorY),
            ImVec2(cursorX + 10, cursorY + 14),
            IM_COL32(255, 200, 80, cursorAlpha));
    }

    // ========================================================================
    // Power-on effect (subtle vertical roll on startup)
    // ========================================================================
    if (g_TerminalTime < 2.0f) {
        float rollOffset = (2.0f - g_TerminalTime) * screenSize.y * 0.3f;
        float rollAlpha = (2.0f - g_TerminalTime) / 2.0f * 255;
        drawList->AddRectFilled(
            ImVec2(0, 0), ImVec2(screenSize.x, rollOffset),
            IM_COL32(0, 0, 0, static_cast<int>(rollAlpha)));
    }

    // ========================================================================
    // Screen reflection (subtle highlight on glass)
    // ========================================================================
    ImVec2 reflectStart(screenSize.x * 0.1f, screenSize.y * 0.05f);
    ImVec2 reflectEnd(screenSize.x * 0.4f, screenSize.y * 0.15f);
    drawList->AddRectFilledMultiColor(
        reflectStart, reflectEnd,
        IM_COL32(255, 255, 255, 8), IM_COL32(255, 255, 255, 3),
        IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 5));
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
        case Theme::FrutigerAero:
            DrawFrutigerAero(drawList, screenSize, deltaTime);
            break;
        case Theme::Minimal:
            DrawMinimalTheme(drawList, screenSize, deltaTime);
            break;
        case Theme::Vaporwave:
            DrawVaporwaveTheme(drawList, screenSize, deltaTime);
            break;
        case Theme::RetroTerminal:
            DrawRetroTerminal(drawList, screenSize, deltaTime);
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

    // Zoom controls
    ImGui::SameLine(0, 15);
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::SliderFloat("##ZoomX", &ui.zoomX, 0.25f, 4.0f, "X:%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Horizontal zoom (Ctrl+Wheel)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::SliderFloat("##ZoomY", &ui.zoomY, 0.5f, 3.0f, "Y:%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vertical zoom (Ctrl+Shift+Wheel)");
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        ui.zoomX = 1.0f;
        ui.zoomY = 1.0f;
        ui.scrollX = 0.0f;
        ui.scrollY = 0.0f;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset zoom and scroll to default");

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

    // Playhead (with latency compensation for audio buffer)
    // Audio buffer is 512 frames * 2 (double buffer) at 44100 Hz = ~23ms
    // Convert to beats: latency_seconds * (BPM / 60)
    float latencyCompensation = (512.0f * 2.0f / 44100.0f) * (project.bpm / 60.0f);
    float compensatedBeat = seq.getCurrentBeat() - latencyCompensation;
    if (compensatedBeat < 0) compensatedBeat += pattern.length;  // Wrap around
    float playheadX = canvasPos.x + keyWidth +
        std::fmod(compensatedBeat, static_cast<float>(pattern.length)) * beatWidth - ui.scrollX;
    if (playheadX >= canvasPos.x + keyWidth && playheadX <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(
            ImVec2(playheadX, canvasPos.y),
            ImVec2(playheadX, canvasPos.y + gridHeight),
            g_PianoRollColors.playhead, 2.0f);
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

                    // Play preview sound for first pasted note
                    const Note& firstNote = pattern.notes[ui.selectedNoteIndex];
                    seq.previewNote(firstNote.pitch, firstNote.velocity, firstNote.oscillatorType);
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
                        // Check if clicked note is part of existing multi-selection
                        bool isPartOfMultiSelection = false;
                        for (int idx : ui.selectedNoteIndices) {
                            if (idx == noteUnderCursor) {
                                isPartOfMultiSelection = true;
                                break;
                            }
                        }

                        // Check if this is a drum (drums can't be resized)
                        bool isDrumNote = isDrumType(pattern.notes[noteUnderCursor].oscillatorType);

                        if (onResizeHandle && !isDrumNote && !isPartOfMultiSelection) {
                            // Save state for undo before resizing (drums can't resize)
                            g_UndoHistory.saveState(pattern, ui.selectedPattern);
                            ui.selectedNoteIndex = noteUnderCursor;
                            // Start resizing
                            ui.isResizingNote = true;
                            ui.dragStartDuration = pattern.notes[noteUnderCursor].duration;
                            ui.dragStartBeat = hoveredBeat;
                        } else if (!onResizeHandle) {
                            // Save state for undo before dragging
                            g_UndoHistory.saveState(pattern, ui.selectedPattern);

                            if (isPartOfMultiSelection && ui.selectedNoteIndices.size() > 1) {
                                // Start multi-drag - keep all selected notes and drag them together
                                ui.isDraggingMultiple = true;
                                ui.dragAnchorBeat = hoveredBeat;
                                ui.dragAnchorPitch = hoveredNote;

                                // Store offsets for each selected note relative to anchor
                                ui.multiDragOffsets.clear();
                                for (int idx : ui.selectedNoteIndices) {
                                    const Note& note = pattern.notes[idx];
                                    float beatOffset = note.startTime - hoveredBeat;
                                    int pitchOffset = note.pitch - hoveredNote;
                                    ui.multiDragOffsets.push_back({beatOffset, pitchOffset});
                                }
                            } else {
                                // Single note drag - clear multi-selection and drag just this note
                                ui.selectedNoteIndex = noteUnderCursor;
                                ui.selectedNoteIndices.clear();
                                ui.isDraggingNote = true;
                                ui.dragStartBeat = pattern.notes[noteUnderCursor].startTime;
                                ui.dragStartPitch = pattern.notes[noteUnderCursor].pitch;
                            }
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
                            // Apply duration multiplier from palette selection
                            newNote.duration = decayTime * (project.bpm / 60.0f) * g_SelectedDurationMult;
                        } else {
                            newNote.duration = 0.25f;
                        }

                        newNote.velocity = 0.8f;
                        pattern.notes.push_back(newNote);
                        ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;

                        // Play preview sound when note is placed
                        seq.previewNote(newNote.pitch, newNote.velocity, newNote.oscillatorType);

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
                // Single note drag
                Note& note = pattern.notes[ui.selectedNoteIndex];
                float newBeat = std::floor(hoveredBeat * 4.0f) / 4.0f;
                int newPitch = std::clamp(hoveredNote, lowestNote, highestNote - 1);
                note.startTime = std::max(0.0f, newBeat);
                note.pitch = newPitch;
            }
            if (ui.isDraggingMultiple && !ui.selectedNoteIndices.empty()) {
                // Multi-note drag - move all selected notes together
                float snappedBeat = std::floor(hoveredBeat * 4.0f) / 4.0f;
                int currentPitch = hoveredNote;

                // Apply offset to each selected note
                for (size_t i = 0; i < ui.selectedNoteIndices.size() && i < ui.multiDragOffsets.size(); ++i) {
                    int idx = ui.selectedNoteIndices[i];
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        Note& note = pattern.notes[idx];
                        float beatOffset = ui.multiDragOffsets[i].first;
                        int pitchOffset = ui.multiDragOffsets[i].second;

                        float newBeat = snappedBeat + beatOffset;
                        int newPitch = currentPitch + pitchOffset;

                        note.startTime = std::max(0.0f, std::floor(newBeat * 4.0f) / 4.0f);
                        note.pitch = std::clamp(newPitch, lowestNote, highestNote - 1);
                    }
                }
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
            ui.isDraggingMultiple = false;
            ui.isResizingNote = false;
            ui.multiDragOffsets.clear();

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
            ui.isDraggingMultiple = false;
            ui.isResizingNote = false;
            ui.multiDragOffsets.clear();
        }
    }

    // Scroll and zoom handling
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift) {
            // Ctrl+Shift+Wheel = Vertical zoom
            ui.zoomY = std::clamp(ui.zoomY + wheel * 0.1f, 0.5f, 3.0f);
        } else if (ImGui::GetIO().KeyCtrl) {
            // Ctrl+Wheel = Horizontal zoom
            ui.zoomX = std::clamp(ui.zoomX + wheel * 0.1f, 0.25f, 4.0f);
        } else if (ImGui::GetIO().KeyShift) {
            // Shift+Wheel = Horizontal scroll
            ui.scrollX = std::max(0.0f, ui.scrollX - wheel * 50.0f);
        } else {
            // Wheel = Vertical scroll
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
        // Display sustain as 0-100% (value is 0.0-1.0)
        float sustainPercent = channel.envelope.sustain * 100.0f;
        if (ImGui::SliderFloat("Sustain", &sustainPercent, 0.0f, 100.0f, "%.0f%%")) {
            channel.envelope.sustain = sustainPercent / 100.0f;
        }
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
        // Display velocity as 0-100% (value is 0.0-1.0)
        float velocityPercent = note.velocity * 100.0f;
        if (ImGui::SliderFloat("##velocity", &velocityPercent, 0.0f, 100.0f, "%.0f%%")) {
            note.velocity = velocityPercent / 100.0f;
        }

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
    ImGui::SameLine();
    if (ImGui::Button("Pad", ImVec2(100, 30))) {
        ui.currentView = ViewMode::PadController;
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
        case OscillatorType::Supersaw: {
            // Supersaw icon - multiple stacked saws
            float sw = (size.x - 8) / 3.0f;
            for (int i = 0; i < 3; ++i) {
                float xOffset = pos.x + 4 + i * sw;
                float yOffset = (i - 1) * hh * 0.15f;  // Slight vertical offset for depth
                float thickness = (i == 1) ? 2.5f : 1.5f;  // Center saw is thicker
                drawList->AddLine(ImVec2(xOffset, cy + hh * 0.7f + yOffset),
                                  ImVec2(xOffset + sw - 2, cy - hh * 0.7f + yOffset), color, thickness);
                drawList->AddLine(ImVec2(xOffset + sw - 2, cy - hh * 0.7f + yOffset),
                                  ImVec2(xOffset + sw - 2, cy + hh * 0.7f + yOffset), color, thickness);
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
        // SYNTHWAVE PRESETS
        // ================================================================
        case OscillatorType::SynthwaveLead: {
            // SW Lead - PWM pulse with modulation wave
            float pw = hw * 0.3f;
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh*0.5f), ImVec2(pos.x + 4, cy - hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(pos.x + 4, cy - hh*0.5f), ImVec2(cx - pw, cy - hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(cx - pw, cy - hh*0.5f), ImVec2(cx - pw, cy + hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(cx - pw, cy + hh*0.5f), ImVec2(cx + pw, cy + hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(cx + pw, cy + hh*0.5f), ImVec2(cx + pw, cy - hh*0.3f), color, 2.0f);
            // Modulation wave on top
            for (int i = 0; i < 3; ++i) {
                float x = pos.x + 8 + i * 10;
                drawList->AddCircle(ImVec2(x, cy - hh*0.8f), 2.0f, color, 6, 1.5f);
            }
            break;
        }
        case OscillatorType::SynthwaveBass: {
            // SW Bass - thick sub wave with harmonics
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(cx - hw*0.4f, cy + hh*0.9f), color, 4.0f);
            drawList->AddLine(ImVec2(cx - hw*0.4f, cy + hh*0.9f), ImVec2(cx, cy - hh*0.3f), color, 4.0f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.3f), ImVec2(cx + hw*0.4f, cy + hh*0.5f), color, 3.0f);
            drawList->AddLine(ImVec2(cx + hw*0.4f, cy + hh*0.5f), ImVec2(pos.x + size.x - 4, cy), color, 2.0f);
            break;
        }
        case OscillatorType::SynthwavePad: {
            // SW Pad - multiple soft overlapping waves (supersaw)
            for (int j = -2; j <= 2; ++j) {
                float offset = j * 3.0f;
                float alpha = 1.0f - std::abs(j) * 0.2f;
                ImU32 layerColor = IM_COL32(
                    (color >> 0) & 0xFF,
                    (color >> 8) & 0xFF,
                    (color >> 16) & 0xFF,
                    static_cast<int>(((color >> 24) & 0xFF) * alpha));
                ImVec2 prev(pos.x + 4, cy + offset);
                for (int i = 1; i <= 8; ++i) {
                    float t = static_cast<float>(i) / 8.0f;
                    float x = pos.x + 4 + t * (size.x - 8);
                    float y = cy + offset + std::sin(t * 6.28f * 1.5f + j * 0.5f) * hh * 0.4f;
                    drawList->AddLine(prev, ImVec2(x, y), layerColor, 1.5f);
                    prev = ImVec2(x, y);
                }
            }
            break;
        }
        case OscillatorType::SynthwaveArp: {
            // SW Arp - fast staircase with glow dots
            float step = (size.x - 8) / 5.0f;
            for (int i = 0; i < 5; ++i) {
                float x = pos.x + 4 + i * step;
                float y = cy - hh + (i % 3) * hh * 0.8f;
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + step - 3, y + 3), color);
                drawList->AddCircleFilled(ImVec2(x + 2, y + 1.5f), 2.5f, color);
            }
            break;
        }
        case OscillatorType::SynthwaveChord: {
            // SW Chord - stacked rectangles (polyphonic)
            float rh = hh * 0.3f;
            drawList->AddRectFilled(ImVec2(cx - hw*0.6f, cy - rh*2.5f), ImVec2(cx + hw*0.6f, cy - rh*1.2f), color);
            drawList->AddRectFilled(ImVec2(cx - hw*0.5f, cy - rh*0.5f), ImVec2(cx + hw*0.5f, cy + rh*0.5f), color);
            drawList->AddRectFilled(ImVec2(cx - hw*0.6f, cy + rh*1.2f), ImVec2(cx + hw*0.6f, cy + rh*2.5f), color);
            break;
        }
        case OscillatorType::SynthwaveFM: {
            // SW FM - modulator carrier visualization
            drawList->AddCircle(ImVec2(cx - hw*0.3f, cy), hh * 0.4f, color, 8, 2.0f);  // Modulator
            drawList->AddCircleFilled(ImVec2(cx + hw*0.3f, cy), hh * 0.5f, color);     // Carrier
            drawList->AddLine(ImVec2(cx - hw*0.3f + hh*0.4f, cy), ImVec2(cx + hw*0.3f - hh*0.5f, cy), color, 2.0f);  // Connection
            // FM sidebands
            drawList->AddLine(ImVec2(cx + hw*0.3f, cy - hh*0.7f), ImVec2(cx + hw*0.3f, cy - hh*0.5f), color, 1.5f);
            drawList->AddLine(ImVec2(cx + hw*0.3f, cy + hh*0.5f), ImVec2(cx + hw*0.3f, cy + hh*0.7f), color, 1.5f);
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

        // ================================================================
        // TECHNO / ELECTRONIC PRESETS
        // ================================================================
        case OscillatorType::AcidBass: {
            // TB-303 style - resonant filter peak shape
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh*0.6f), ImVec2(cx - hw*0.2f, cy - hh*0.9f), color, 2.5f);
            drawList->AddLine(ImVec2(cx - hw*0.2f, cy - hh*0.9f), ImVec2(cx + hw*0.1f, cy + hh*0.3f), color, 2.5f);
            drawList->AddLine(ImVec2(cx + hw*0.1f, cy + hh*0.3f), ImVec2(pos.x + size.x - 4, cy + hh*0.5f), color, 2.0f);
            // Resonance spike
            drawList->AddCircleFilled(ImVec2(cx - hw*0.2f, cy - hh*0.9f), 3.0f, color);
            break;
        }
        case OscillatorType::TechnoStab: {
            // Short stab - sharp attack, quick decay
            drawList->AddRectFilled(ImVec2(cx - hw*0.4f, cy - hh*0.6f), ImVec2(cx + hw*0.4f, cy + hh*0.2f), color);
            drawList->AddLine(ImVec2(cx + hw*0.4f, cy - hh*0.4f), ImVec2(cx + hw*0.8f, cy + hh*0.6f), color, 2.0f);
            break;
        }
        case OscillatorType::Hoover: {
            // Hoover - detuned saws sweeping
            for (int i = -2; i <= 2; ++i) {
                float yOff = i * hh * 0.25f;
                float thickness = (i == 0) ? 2.5f : 1.5f;
                drawList->AddLine(ImVec2(pos.x + 4, cy + yOff + hh*0.3f), ImVec2(cx, cy + yOff - hh*0.3f), color, thickness);
                drawList->AddLine(ImVec2(cx, cy + yOff - hh*0.3f), ImVec2(cx, cy + yOff + hh*0.3f), color, thickness);
            }
            break;
        }
        case OscillatorType::RaveChord: {
            // Rave chord - stacked piano keys
            float kw = hw * 0.25f;
            for (int i = 0; i < 4; ++i) {
                float x = cx - hw*0.5f + i * kw;
                drawList->AddRectFilled(ImVec2(x, cy - hh*0.5f), ImVec2(x + kw - 2, cy + hh*0.5f), color);
            }
            break;
        }
        case OscillatorType::Reese: {
            // Reese bass - two detuned saws creating movement
            ImVec2 prev1(pos.x + 4, cy);
            ImVec2 prev2(pos.x + 4, cy);
            for (int i = 1; i <= 12; ++i) {
                float t = static_cast<float>(i) / 12.0f;
                float x = pos.x + 4 + t * (size.x - 8);
                float y1 = cy + std::sin(t * 12.0f) * hh * 0.4f;
                float y2 = cy + std::sin(t * 12.0f + 0.5f) * hh * 0.4f;
                drawList->AddLine(prev1, ImVec2(x, y1), color, 2.0f);
                drawList->AddLine(prev2, ImVec2(x, y2), IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, 150), 1.5f);
                prev1 = ImVec2(x, y1);
                prev2 = ImVec2(x, y2);
            }
            break;
        }

        // ================================================================
        // HIP HOP PRESETS
        // ================================================================
        case OscillatorType::SubBass808: {
            // Deep 808 sub - very thick low sine
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.9f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.6f, IM_COL32(0, 0, 0, 100), 12, 2.0f);
            // Sub indicator
            drawList->AddText(ImVec2(cx - 6, cy - 6), IM_COL32(0, 0, 0, 200), "S");
            break;
        }
        case OscillatorType::LoFiKeys: {
            // Lo-fi keys - dusty piano keys with vinyl crackle dots
            float kw = hw * 0.3f;
            for (int i = 0; i < 3; ++i) {
                float x = cx - hw*0.4f + i * kw;
                drawList->AddRectFilled(ImVec2(x, cy - hh*0.6f), ImVec2(x + kw - 3, cy + hh*0.6f), color);
            }
            // Dust particles
            drawList->AddCircleFilled(ImVec2(cx - hw*0.5f, cy - hh*0.3f), 2.0f, color);
            drawList->AddCircleFilled(ImVec2(cx + hw*0.6f, cy + hh*0.2f), 1.5f, color);
            break;
        }
        case OscillatorType::VinylNoise: {
            // Vinyl crackle - scattered dots
            for (int i = 0; i < 10; ++i) {
                float x = pos.x + 6 + (i * 13 + 7) % static_cast<int>(size.x - 12);
                float y = cy + ((i * 7 + 3) % 5 - 2) * hh * 0.4f;
                float r = 1.0f + (i % 3) * 0.5f;
                drawList->AddCircleFilled(ImVec2(x, y), r, color);
            }
            break;
        }
        case OscillatorType::TrapLead: {
            // Trap lead - plucky square wave
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(pos.x + 6, cy - hh*0.8f), color, 2.5f);
            drawList->AddLine(ImVec2(pos.x + 6, cy - hh*0.8f), ImVec2(cx, cy - hh*0.8f), color, 2.5f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.8f), ImVec2(cx, cy + hh*0.4f), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh*0.4f), ImVec2(pos.x + size.x - 4, cy + hh*0.1f), color, 1.5f);
            break;
        }

        // ================================================================
        // ADDITIONAL SYNTHWAVE
        // ================================================================
        case OscillatorType::GatedPad: {
            // Gated pad - rhythmic blocks
            float bw = (size.x - 8) / 6.0f;
            for (int i = 0; i < 6; ++i) {
                float alpha = (i % 2 == 0) ? 1.0f : 0.3f;
                ImU32 blockColor = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, static_cast<int>(((color >> 24) & 0xFF) * alpha));
                drawList->AddRectFilled(ImVec2(pos.x + 4 + i * bw, cy - hh*0.5f), ImVec2(pos.x + 4 + (i + 1) * bw - 2, cy + hh*0.5f), blockColor);
            }
            break;
        }
        case OscillatorType::PolySynth: {
            // Poly synth - multiple layered waves
            for (int j = -1; j <= 1; ++j) {
                ImVec2 prev(pos.x + 4, cy + j * hh * 0.3f);
                for (int i = 1; i <= 10; ++i) {
                    float t = static_cast<float>(i) / 10.0f;
                    float x = pos.x + 4 + t * (size.x - 8);
                    float y = cy + j * hh * 0.3f + std::sin(t * 6.28f + j * 0.8f) * hh * 0.35f;
                    float thickness = (j == 0) ? 2.5f : 1.5f;
                    drawList->AddLine(prev, ImVec2(x, y), color, thickness);
                    prev = ImVec2(x, y);
                }
            }
            break;
        }
        case OscillatorType::SyncLead: {
            // Hard sync - zigzag with discontinuities
            float lastY = cy;
            for (int i = 0; i < 4; ++i) {
                float x1 = pos.x + 4 + i * (size.x - 8) / 4.0f;
                float x2 = x1 + (size.x - 8) / 4.0f;
                drawList->AddLine(ImVec2(x1, lastY), ImVec2(x1, cy - hh*0.7f), color, 2.0f);
                drawList->AddLine(ImVec2(x1, cy - hh*0.7f), ImVec2(x2 - 2, cy + hh*0.5f), color, 2.0f);
                lastY = cy - hh*0.3f;  // Jump point
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

// Helper to draw a drum item with duration variant
inline void DrawDrumVariant(ImDrawList* drawList, int oscIndex, const char* name, const char* desc,
                            float durationMult, const char* durationLabel,
                            Project& project, UIState& ui, Sequencer& seq) {
    ImVec2 itemSize(120, 28);
    ImVec2 pos = ImGui::GetCursorScreenPos();

    bool isPaletteSelected = (g_SelectedPaletteItem == oscIndex && std::abs(g_SelectedDurationMult - durationMult) < 0.01f);

    // Colors based on selection state
    ImU32 bgColor = isPaletteSelected ? IM_COL32(140, 80, 80, 255) : IM_COL32(50, 40, 40, 200);
    ImU32 textColor = isPaletteSelected ? IM_COL32(255, 220, 220, 255) : IM_COL32(200, 150, 150, 255);
    ImU32 borderColor = isPaletteSelected ? IM_COL32(255, 150, 150, 255) : IM_COL32(80, 60, 60, 255);

    drawList->AddRectFilled(pos, ImVec2(pos.x + itemSize.x, pos.y + itemSize.y), bgColor, 3.0f);
    drawList->AddRect(pos, ImVec2(pos.x + itemSize.x, pos.y + itemSize.y), borderColor, 3.0f, 0, isPaletteSelected ? 2.0f : 1.0f);

    // Draw small icon
    ImVec2 iconPos(pos.x + 4, pos.y + 4);
    ImVec2 iconSize(20, 20);
    DrawWaveformIcon(drawList, iconPos, iconSize, static_cast<OscillatorType>(oscIndex), textColor);

    // Draw text
    char label[64];
    snprintf(label, sizeof(label), "%s %s", name, durationLabel);
    drawList->AddText(ImVec2(pos.x + 28, pos.y + 6), textColor, label);

    // Invisible button for interaction
    ImGui::InvisibleButton(label, itemSize);

    if (ImGui::IsItemClicked()) {
        if (isPaletteSelected) {
            g_SelectedPaletteItem = -1;
            g_SelectedDurationMult = 1.0f;
        } else {
            g_SelectedPaletteItem = oscIndex;
            g_SelectedDurationMult = durationMult;
            ui.pianoRollMode = PianoRollMode::Draw;
            project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(oscIndex);
            seq.updateChannelConfigs();
        }
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%s (%s)", name, durationLabel);
        ImGui::TextDisabled("%s", desc);
        ImGui::TextDisabled("Duration: %.1fx", durationMult);
        if (isPaletteSelected) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "SELECTED");
        }
        ImGui::EndTooltip();
    }
}

// Helper to draw a drum category with expandable variations
inline void DrawDrumCategory(const char* categoryName, bool& expanded,
                             const int* oscIndices, const char** names, const char** descs, int count,
                             Project& project, UIState& ui, Sequencer& seq) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.2f, 0.2f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.25f, 0.25f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.5f, 0.3f, 0.3f, 1.0f));

    if (ImGui::CollapsingHeader(categoryName, expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        expanded = true;
        ImGui::Indent(10.0f);

        for (int i = 0; i < count; ++i) {
            int oscIdx = oscIndices[i];
            ImGui::PushID(oscIdx * 100);

            // Show drum name as sub-header
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.7f, 1.0f), "%s", names[i]);

            // Duration variations in a row
            DrawDrumVariant(drawList, oscIdx, names[i], descs[i], 0.5f, "(Short)", project, ui, seq);
            ImGui::SameLine();
            DrawDrumVariant(drawList, oscIdx, names[i], descs[i], 1.0f, "(Normal)", project, ui, seq);
            ImGui::SameLine();
            DrawDrumVariant(drawList, oscIdx, names[i], descs[i], 2.0f, "(Long)", project, ui, seq);

            ImGui::PopID();
        }

        ImGui::Unindent(10.0f);
    } else {
        expanded = false;
    }

    ImGui::PopStyleColor(3);
}

inline void DrawSoundPalette(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("Sound Palette");

    ImGui::Text("Click to select, then draw on Piano Roll");
    ImGui::Separator();

    const char* oscNames[] = {
        "Pulse", "Triangle", "Sawtooth", "Sine", "Noise", "Supersaw", "Custom",
        "Lead", "Pad", "Bass", "Pluck", "Arp", "Organ", "Strings", "Brass", "Chip", "Bell",
        "SW Lead", "SW Bass", "SW Pad", "SW Arp", "SW Chord", "SW FM",
        "Kick", "Kick808", "KickHard", "KickSoft",
        "Snare", "Snare808", "SnareRim", "Clap",
        "HiHat", "HiHatOpen", "HiHatPedal",
        "Tom", "TomLow", "TomHigh",
        "Crash", "Ride",
        "Cowbell", "Clave", "Conga", "Maracas", "Tambourine"
    };
    const char* oscDesc[] = {
        "Square wave - Classic NES", "Triangle - Soft, flute-like", "Sawtooth - Rich, buzzy",
        "Sine - Pure, clean", "Noise - Percussion", "7 detuned saws - Massive", "Custom - Adjustable",
        "Thick detuned saws", "Soft, atmospheric", "Deep punchy bass", "Short, plucky",
        "Crisp arpeggios", "Classic drawbar", "Lush ensemble", "Rich, brassy", "12.5% chiptune", "FM bell/chime",
        "80s PWM lead - Bright, cutting", "808 saw bass - Deep sub", "Supersaw pad - Lush, wide",
        "Crisp arp - Tight sequences", "Poly stab - Chord hits", "DX7 FM - Brass/keys",
        "Standard pitch sweep", "Deep 808 sub-bass", "Punchy tight", "Soft warm",
        "Standard with noise", "808 more tonal", "Rimshot clicky", "Hand clap bursts",
        "Closed", "Open longer", "Pedal very short",
        "Mid tom", "Floor tom", "High tom",
        "Crash long", "Ride sustained",
        "808 cowbell", "Wood block", "Conga", "Shaker", "Jingly"
    };

    constexpr int NUM_OSCILLATORS = 7;  // Pulse, Triangle, Sawtooth, Sine, Noise, Supersaw, Custom
    constexpr int NUM_SYNTHS = 16;  // 10 original + 6 synthwave
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // ========== OSCILLATORS (Collapsible) ==========
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.3f, 0.2f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.4f, 0.25f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.3f, 0.5f, 0.3f, 1.0f));

    if (ImGui::CollapsingHeader("Oscillators", g_PaletteExpanded_Oscillators ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        g_PaletteExpanded_Oscillators = true;
        ImVec2 iconSize(55, 35);
        for (int i = 0; i < NUM_OSCILLATORS; ++i) {
            ImGui::PushID(i);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool isPaletteSelected = (g_SelectedPaletteItem == i);

            ImU32 bgColor = isPaletteSelected ? IM_COL32(80, 120, 80, 255) : IM_COL32(40, 50, 40, 255);
            ImU32 waveColor = isPaletteSelected ? IM_COL32(200, 255, 200, 255) : IM_COL32(100, 180, 100, 255);
            ImU32 borderColor = isPaletteSelected ? IM_COL32(150, 255, 150, 255) : IM_COL32(70, 90, 70, 255);

            drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), bgColor, 4.0f);
            drawList->AddRect(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 2.0f : 1.0f);
            DrawWaveformIcon(drawList, pos, iconSize, static_cast<OscillatorType>(i), waveColor);

            ImGui::InvisibleButton("##osc", iconSize);
            if (ImGui::IsItemClicked()) {
                if (g_SelectedPaletteItem == i) {
                    g_SelectedPaletteItem = -1;
                } else {
                    g_SelectedPaletteItem = i;
                    g_SelectedDurationMult = 1.0f;
                    ui.pianoRollMode = PianoRollMode::Draw;
                    project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(i);
                    seq.updateChannelConfigs();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", oscNames[i]);
                ImGui::TextDisabled("%s", oscDesc[i]);
                ImGui::EndTooltip();
            }
            if (i < NUM_OSCILLATORS - 1) ImGui::SameLine();
            ImGui::PopID();
        }
    } else {
        g_PaletteExpanded_Oscillators = false;
    }
    ImGui::PopStyleColor(3);

    // ========== SYNTHS (Collapsible) ==========
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.2f, 0.35f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.35f, 0.25f, 0.45f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.4f, 0.3f, 0.55f, 1.0f));

    if (ImGui::CollapsingHeader("Synths", g_PaletteExpanded_Synths ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        g_PaletteExpanded_Synths = true;
        ImVec2 iconSize(50, 32);
        for (int i = NUM_OSCILLATORS; i < NUM_OSCILLATORS + NUM_SYNTHS; ++i) {
            ImGui::PushID(i);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool isPaletteSelected = (g_SelectedPaletteItem == i);

            ImU32 bgColor = isPaletteSelected ? IM_COL32(100, 80, 140, 255) : IM_COL32(45, 40, 55, 255);
            ImU32 waveColor = isPaletteSelected ? IM_COL32(220, 180, 255, 255) : IM_COL32(150, 120, 180, 255);
            ImU32 borderColor = isPaletteSelected ? IM_COL32(200, 150, 255, 255) : IM_COL32(80, 70, 100, 255);

            drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), bgColor, 4.0f);
            drawList->AddRect(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 2.0f : 1.0f);
            DrawWaveformIcon(drawList, pos, iconSize, static_cast<OscillatorType>(i), waveColor);

            ImGui::InvisibleButton("##synth", iconSize);
            if (ImGui::IsItemClicked()) {
                if (g_SelectedPaletteItem == i) {
                    g_SelectedPaletteItem = -1;
                } else {
                    g_SelectedPaletteItem = i;
                    g_SelectedDurationMult = 1.0f;
                    ui.pianoRollMode = PianoRollMode::Draw;
                    project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(i);
                    seq.updateChannelConfigs();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", oscNames[i]);
                ImGui::TextDisabled("%s", oscDesc[i]);
                ImGui::EndTooltip();
            }
            int idx = i - NUM_OSCILLATORS;
            if ((idx + 1) % 5 != 0 && i < NUM_OSCILLATORS + NUM_SYNTHS - 1) ImGui::SameLine();
            ImGui::PopID();
        }
    } else {
        g_PaletteExpanded_Synths = false;
    }
    ImGui::PopStyleColor(3);

    // ========== DRUMS (Expandable Categories) ==========
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "DRUMS (click category to expand)");

    // Kicks (indices shifted by 6 for synthwave synths)
    {
        const int indices[] = { 22, 23, 24, 25 };
        const char* names[] = { "Kick", "Kick808", "KickHard", "KickSoft" };
        const char* descs[] = { "Standard pitch sweep", "Deep 808 sub-bass", "Punchy tight", "Soft warm" };
        DrawDrumCategory("Kicks", g_PaletteExpanded_Kicks, indices, names, descs, 4, project, ui, seq);
    }

    // Snares
    {
        const int indices[] = { 26, 27, 28, 29 };
        const char* names[] = { "Snare", "Snare808", "SnareRim", "Clap" };
        const char* descs[] = { "Standard with noise", "808 more tonal", "Rimshot clicky", "Hand clap bursts" };
        DrawDrumCategory("Snares & Claps", g_PaletteExpanded_Snares, indices, names, descs, 4, project, ui, seq);
    }

    // Hi-Hats
    {
        const int indices[] = { 30, 31, 32 };
        const char* names[] = { "HiHat", "HiHatOpen", "HiHatPedal" };
        const char* descs[] = { "Closed hi-hat", "Open longer decay", "Pedal very short" };
        DrawDrumCategory("Hi-Hats", g_PaletteExpanded_HiHats, indices, names, descs, 3, project, ui, seq);
    }

    // Toms
    {
        const int indices[] = { 33, 34, 35 };
        const char* names[] = { "Tom", "TomLow", "TomHigh" };
        const char* descs[] = { "Mid tom", "Floor tom low pitch", "High tom" };
        DrawDrumCategory("Toms", g_PaletteExpanded_Toms, indices, names, descs, 3, project, ui, seq);
    }

    // Cymbals
    {
        const int indices[] = { 36, 37 };
        const char* names[] = { "Crash", "Ride" };
        const char* descs[] = { "Crash cymbal long decay", "Ride cymbal sustained" };
        DrawDrumCategory("Cymbals", g_PaletteExpanded_Cymbals, indices, names, descs, 2, project, ui, seq);
    }

    // Percussion
    {
        const int indices[] = { 38, 39, 40, 41, 42 };
        const char* names[] = { "Cowbell", "Clave", "Conga", "Maracas", "Tambourine" };
        const char* descs[] = { "808 cowbell", "Wood block click", "Conga drum", "Shaker", "Jingly metallic" };
        DrawDrumCategory("Percussion", g_PaletteExpanded_Percussion, indices, names, descs, 5, project, ui, seq);
    }

    ImGui::Separator();

    // Show selected item
    if (g_SelectedPaletteItem >= 0) {
        ImVec4 color;
        const char* typeStr;
        if (g_SelectedPaletteItem < NUM_OSCILLATORS) {
            color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
            typeStr = "Oscillator";
        } else if (g_SelectedPaletteItem < NUM_OSCILLATORS + NUM_SYNTHS) {
            color = ImVec4(0.8f, 0.6f, 1.0f, 1.0f);
            typeStr = "Synth";
        } else {
            color = ImVec4(1.0f, 0.6f, 0.6f, 1.0f);
            typeStr = "Drum";
        }
        ImGui::TextColored(color, "Selected: %s", oscNames[g_SelectedPaletteItem]);
        if (g_SelectedPaletteItem >= NUM_OSCILLATORS + NUM_SYNTHS) {
            // Show duration for drums
            const char* durStr = (g_SelectedDurationMult < 0.75f) ? "Short" :
                                 (g_SelectedDurationMult > 1.5f) ? "Long" : "Normal";
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", durStr);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            g_SelectedPaletteItem = -1;
            g_SelectedDurationMult = 1.0f;
        }
    } else {
        ImGui::TextDisabled("No sound selected");
    }

    ImGui::End();
}

// ============================================================================
// Pad Controller - MPC-style live performance interface
// ============================================================================

// Helper: Draw a rotary knob
inline bool DrawKnob(const char* label, float* value, float minVal = 0.0f, float maxVal = 1.0f,
                     float radius = 30.0f, ImU32 color = IM_COL32(100, 180, 255, 255)) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center(pos.x + radius, pos.y + radius);

    bool changed = false;

    // Invisible button for interaction
    ImGui::InvisibleButton(label, ImVec2(radius * 2, radius * 2 + 20));
    bool isActive = ImGui::IsItemActive();
    bool isHovered = ImGui::IsItemHovered();

    if (isActive && ImGui::IsMouseDragging(0)) {
        float delta = -ImGui::GetIO().MouseDelta.y * 0.005f;
        *value = std::clamp(*value + delta * (maxVal - minVal), minVal, maxVal);
        changed = true;
    }

    // Knob colors
    ImU32 bgColor = isHovered ? IM_COL32(60, 60, 70, 255) : IM_COL32(40, 40, 50, 255);
    ImU32 borderColor = isActive ? IM_COL32(255, 200, 100, 255) : color;

    // Draw knob body
    drawList->AddCircleFilled(center, radius, bgColor);
    drawList->AddCircle(center, radius, borderColor, 32, 2.5f);

    // Draw value arc (from 7 o'clock to 5 o'clock = 240 degrees)
    float normalizedValue = (*value - minVal) / (maxVal - minVal);
    float startAngle = 2.356f;  // 135 degrees in radians (7 o'clock)
    float endAngle = startAngle + normalizedValue * 4.712f;  // 270 degrees range

    // Draw arc segments
    ImVec2 prevPoint(center.x + std::cos(startAngle) * (radius - 5),
                     center.y + std::sin(startAngle) * (radius - 5));
    for (int i = 1; i <= 32; ++i) {
        float t = static_cast<float>(i) / 32.0f;
        float angle = startAngle + t * normalizedValue * 4.712f;
        if (angle > endAngle) break;
        ImVec2 point(center.x + std::cos(angle) * (radius - 5),
                     center.y + std::sin(angle) * (radius - 5));
        drawList->AddLine(prevPoint, point, color, 4.0f);
        prevPoint = point;
    }

    // Draw indicator line
    float indicatorAngle = startAngle + normalizedValue * 4.712f;
    ImVec2 indicatorEnd(center.x + std::cos(indicatorAngle) * (radius - 10),
                        center.y + std::sin(indicatorAngle) * (radius - 10));
    drawList->AddLine(center, indicatorEnd, IM_COL32(255, 255, 255, 200), 3.0f);

    // Label below
    ImVec2 textPos(pos.x, pos.y + radius * 2 + 2);
    drawList->AddText(textPos, IM_COL32(200, 200, 200, 255), label);

    return changed;
}

// Helper: Draw a pad button
inline bool DrawPad(int index, const PadAssignment& pad, bool isActive, float velocity,
                    ImVec2 size, Sequencer& sequencer, PadControllerState& state) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Create unique ID
    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "##pad_%d", index);

    bool triggered = false;

    ImGui::InvisibleButton(idBuf, size);
    bool isHovered = ImGui::IsItemHovered();
    bool isPressed = ImGui::IsItemActive();

    // Trigger on press
    if (ImGui::IsItemActivated()) {
        triggered = true;
        state.padActive[index] = true;
        state.padVelocity[index] = 0.8f;

        // Play the sound
        sequencer.previewNote(pad.midiNote, 0.8f, pad.oscillatorType, 0.5f);
    }

    // Release
    if (ImGui::IsItemDeactivated()) {
        state.padActive[index] = false;
    }

    // Calculate colors with glow effect
    ImU32 padColor = pad.color;
    float r = ((padColor >> 24) & 0xFF) / 255.0f;
    float g = ((padColor >> 16) & 0xFF) / 255.0f;
    float b = ((padColor >> 8) & 0xFF) / 255.0f;

    float brightness = isActive ? 1.5f : (isPressed ? 1.3f : (isHovered ? 1.1f : 0.7f));
    r = std::min(r * brightness, 1.0f);
    g = std::min(g * brightness, 1.0f);
    b = std::min(b * brightness, 1.0f);

    ImU32 fillColor = IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255),
                                static_cast<int>(b * 255), 255);
    ImU32 borderColor = isActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 80, 90, 255);

    // Draw pad with rounded corners
    float rounding = 8.0f;
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), fillColor, rounding);
    drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, rounding, 0, 2.0f);

    // Glow effect when active
    if (isActive) {
        drawList->AddRect(ImVec2(pos.x - 2, pos.y - 2),
                          ImVec2(pos.x + size.x + 2, pos.y + size.y + 2),
                          IM_COL32(255, 255, 255, 100), rounding + 2, 0, 3.0f);
    }

    // Draw label centered
    ImVec2 textSize = ImGui::CalcTextSize(pad.label.c_str());
    ImVec2 textPos(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f);
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), pad.label.c_str());

    return triggered;
}

// Helper: Draw a piano key
inline bool DrawPianoKey(int keyIndex, int octaveOffset, bool isBlack, bool isActive,
                         ImVec2 pos, ImVec2 size, Sequencer& sequencer,
                         OscillatorType sound, PadControllerState& state) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Calculate MIDI note
    // keyIndex 0 = C, 1 = C#, 2 = D, etc.
    static const int keyToNote[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    int midiNote = (octaveOffset * 12) + keyToNote[keyIndex % 12];

    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "##key_%d_%d", octaveOffset, keyIndex);

    bool triggered = false;

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(idBuf, size);
    bool isHovered = ImGui::IsItemHovered();
    bool isPressed = ImGui::IsItemActive();

    if (ImGui::IsItemActivated()) {
        triggered = true;
        sequencer.previewNote(midiNote, 0.8f, sound, 0.5f);

        // Record if recording
        if (state.isRecording) {
            // Note: Recording logic handled in Sequencer
        }
    }

    // Colors
    ImU32 fillColor, borderColor;
    if (isBlack) {
        fillColor = isActive ? IM_COL32(100, 100, 255, 255) :
                   (isPressed ? IM_COL32(80, 80, 100, 255) :
                   (isHovered ? IM_COL32(50, 50, 60, 255) : IM_COL32(30, 30, 40, 255)));
        borderColor = IM_COL32(20, 20, 30, 255);
    } else {
        fillColor = isActive ? IM_COL32(150, 150, 255, 255) :
                   (isPressed ? IM_COL32(200, 200, 220, 255) :
                   (isHovered ? IM_COL32(240, 240, 245, 255) : IM_COL32(255, 255, 255, 255)));
        borderColor = IM_COL32(100, 100, 110, 255);
    }

    // Draw key
    float rounding = isBlack ? 2.0f : 4.0f;
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), fillColor, rounding);
    drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, rounding);

    return triggered;
}

inline void DrawPadController(Project& project, UIState& ui, Sequencer& sequencer) {
    auto& state = ui.padController;

    // ==========================================================================
    // Computer Keyboard Input (ASDF row = white keys, QWERTY row = black keys)
    // Standard piano layout on QWERTY keyboard
    // ==========================================================================
    if (!ImGui::GetIO().WantTextInput) {
        int baseNote = state.keyboardOctave * 12;  // Base MIDI note for current octave

        // White keys: A S D F G H J K L ; (C D E F G A B C D E)
        // MIDI offsets: 0 2 4 5 7 9 11 12 14 16
        struct KeyMapping { ImGuiKey key; int noteOffset; int keyIndex; };
        static const KeyMapping whiteKeys[] = {
            {ImGuiKey_A, 0, 0},   // C
            {ImGuiKey_S, 2, 1},   // D
            {ImGuiKey_D, 4, 2},   // E
            {ImGuiKey_F, 5, 3},   // F
            {ImGuiKey_G, 7, 4},   // G
            {ImGuiKey_H, 9, 5},   // A
            {ImGuiKey_J, 11, 6},  // B
            {ImGuiKey_K, 12, 7},  // C (next octave)
            {ImGuiKey_L, 14, 8},  // D (next octave)
            {ImGuiKey_Semicolon, 16, 9}  // E (next octave)
        };

        // Black keys: W E   T Y U   O P (C# D#   F# G# A#   C# D#)
        // MIDI offsets: 1 3   6 8 10  13 15
        static const KeyMapping blackKeys[] = {
            {ImGuiKey_W, 1, 10},  // C#
            {ImGuiKey_E, 3, 11},  // D#
            {ImGuiKey_T, 6, 12},  // F#
            {ImGuiKey_Y, 8, 13},  // G#
            {ImGuiKey_U, 10, 14}, // A#
            {ImGuiKey_O, 13, 15}, // C# (next octave)
            {ImGuiKey_P, 15, 16}  // D# (next octave)
        };

        // Process white keys
        for (const auto& km : whiteKeys) {
            int midiNote = baseNote + km.noteOffset;
            if (ImGui::IsKeyPressed(km.key)) {
                sequencer.previewNote(midiNote, 0.8f, state.keyboardSound, 0.5f);
                if (km.keyIndex < PadControllerState::NUM_KEYS) {
                    state.keyActive[km.keyIndex] = true;
                }
                // Record if recording
                if (state.isRecording) {
                    RecordedNoteEvent evt;
                    evt.pitch = midiNote;
                    evt.velocity = 0.8f;
                    evt.timestamp = sequencer.getCurrentBeat();
                    evt.oscillatorType = state.keyboardSound;
                    evt.duration = 0.5f;
                    evt.isNoteOn = true;
                    state.recordedEvents.push_back(evt);
                }
            }
            if (ImGui::IsKeyReleased(km.key)) {
                if (km.keyIndex < PadControllerState::NUM_KEYS) {
                    state.keyActive[km.keyIndex] = false;
                }
            }
        }

        // Process black keys
        for (const auto& km : blackKeys) {
            int midiNote = baseNote + km.noteOffset;
            if (ImGui::IsKeyPressed(km.key)) {
                sequencer.previewNote(midiNote, 0.8f, state.keyboardSound, 0.5f);
                if (km.keyIndex < PadControllerState::NUM_KEYS) {
                    state.keyActive[km.keyIndex] = true;
                }
                // Record if recording
                if (state.isRecording) {
                    RecordedNoteEvent evt;
                    evt.pitch = midiNote;
                    evt.velocity = 0.8f;
                    evt.timestamp = sequencer.getCurrentBeat();
                    evt.oscillatorType = state.keyboardSound;
                    evt.duration = 0.5f;
                    evt.isNoteOn = true;
                    state.recordedEvents.push_back(evt);
                }
            }
            if (ImGui::IsKeyReleased(km.key)) {
                if (km.keyIndex < PadControllerState::NUM_KEYS) {
                    state.keyActive[km.keyIndex] = false;
                }
            }
        }

        // Octave up/down with Z and X keys
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
            state.keyboardOctave = std::max(0, state.keyboardOctave - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_X)) {
            state.keyboardOctave = std::min(7, state.keyboardOctave + 1);
        }

        // Number keys 1-8 trigger pads (top row = pads 0-7, with shift = pads 8-15)
        static const ImGuiKey padKeys[] = {
            ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
            ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8
        };
        bool shift = ImGui::GetIO().KeyShift;
        const auto& currentBank = state.getCurrentBank();
        for (int i = 0; i < 8; ++i) {
            int padIdx = shift ? (i + 8) : i;
            if (ImGui::IsKeyPressed(padKeys[i])) {
                const auto& pad = currentBank[padIdx];
                sequencer.previewNote(pad.midiNote, 0.8f, pad.oscillatorType, 0.5f);
                state.padActive[padIdx] = true;
                state.padVelocity[padIdx] = 0.8f;
                // Record if recording
                if (state.isRecording) {
                    RecordedNoteEvent evt;
                    evt.pitch = pad.midiNote;
                    evt.velocity = 0.8f;
                    evt.timestamp = sequencer.getCurrentBeat();
                    evt.oscillatorType = pad.oscillatorType;
                    evt.duration = 0.25f;
                    evt.isNoteOn = true;
                    state.recordedEvents.push_back(evt);
                }
            }
            if (ImGui::IsKeyReleased(padKeys[i])) {
                state.padActive[padIdx] = false;
            }
        }

        // Bank switch with Tab
        if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
            state.currentBank = 1 - state.currentBank;
        }
    }

    ImGui::Begin("Pad Controller", nullptr, ImGuiWindowFlags_NoCollapse);

    ImVec2 windowSize = ImGui::GetContentRegionAvail();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Layout dimensions
    float padSize = std::min(80.0f, (windowSize.x - 400) / 4.5f);
    float knobRadius = 25.0f;
    float keyboardHeight = 100.0f;

    // ==========================================================================
    // Top Section: Bank Select + Record Controls + Arpeggiator
    // ==========================================================================
    ImGui::BeginChild("TopControls", ImVec2(0, 60), true);

    // Genre preset selector
    ImGui::Text("Genre:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    const char* genreNames[] = {"General", "Synthwave", "Techno", "Hip Hop", "Drum Kit"};
    int currentGenreInt = static_cast<int>(state.currentGenre);
    if (ImGui::Combo("##genre", &currentGenreInt, genreNames, 5)) {
        state.loadGenrePreset(static_cast<PadGenre>(currentGenreInt));
    }
    ImGui::SameLine();

    // Bank selection
    ImGui::Text("Bank:");
    ImGui::SameLine();
    if (ImGui::RadioButton("A (Drums)", state.currentBank == 0)) {
        state.currentBank = 0;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("B (Synths)", state.currentBank == 1)) {
        state.currentBank = 1;
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // =============================================
    // PLAY/STOP Button - For playback without recording
    // =============================================
    bool isPlaying = sequencer.isPlaying() && !state.isRecording;

    if (isPlaying) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 150, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 200, 80, 255));
    }

    if (ImGui::Button(isPlaying ? "STOP" : "PLAY", ImVec2(60, 40))) {
        if (isPlaying) {
            // Stop playback AND clear preview pattern
            // This ensures REC starts fresh without old notes playing
            sequencer.stop();
            sequencer.clearPreviewPattern();
        } else {
            // Start playback of current pattern
            sequencer.setPreviewPattern(ui.selectedPattern, ui.selectedChannel);
            sequencer.stop();  // Reset position
            sequencer.play();  // Start playing
        }
    }

    if (isPlaying) {
        ImGui::PopStyleColor(2);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isPlaying ?
            "Stop playback" :
            "Play current pattern");
    }

    ImGui::SameLine();

    // =============================================
    // RECORD Button - Records notes while playing
    // =============================================
    ImVec2 recPos = ImGui::GetCursorScreenPos();

    // Color the button based on state
    if (state.isRecording) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 50, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 80, 80, 255));
    }

    if (ImGui::Button(state.isRecording ? "STOP REC" : "REC", ImVec2(80, 40))) {
        if (state.isRecording) {
            // =============================================
            // STOP RECORDING - Save notes to pattern
            // =============================================
            state.isRecording = false;
            state.recordArmed = false;

            // Stop playback
            sequencer.stop();

            // Transfer recorded events to pattern
            if (!state.recordedEvents.empty() && ui.selectedPattern < static_cast<int>(project.patterns.size())) {
                Pattern& pattern = project.patterns[ui.selectedPattern];
                for (const auto& evt : state.recordedEvents) {
                    if (evt.isNoteOn) {
                        Note note;
                        note.pitch = evt.pitch;
                        note.velocity = evt.velocity;
                        note.startTime = state.quantizeEnabled ?
                                         state.quantizeBeat(evt.timestamp - state.recordStartBeat) :
                                         evt.timestamp - state.recordStartBeat;
                        note.duration = evt.duration;
                        note.oscillatorType = evt.oscillatorType;
                        pattern.notes.push_back(note);
                    }
                }
                // Note: Don't auto-play recording - user can click PLAY to hear it
                // This prevents confusion when clicking REC again
            }
            state.recordedEvents.clear();
        } else {
            // =============================================
            // START RECORDING - Immediate start, no arm step
            // =============================================

            // CRITICAL: Stop everything first to silence any playing notes
            sequencer.stop();  // This calls allNotesOff() internally

            // Clear the preview pattern so old notes don't play
            sequencer.clearPreviewPattern();

            // Now set recording state
            state.isRecording = true;
            state.recordArmed = true;
            state.recordStartBeat = 0.0f;
            state.recordedEvents.clear();

            // Start the sequencer for timing purposes only
            // No notes will play since preview pattern is cleared
            sequencer.play();
            state.recordStartBeat = sequencer.getCurrentBeat();
        }
    }

    if (state.isRecording) {
        ImGui::PopStyleColor(2);
    }

    // Tooltip with instructions
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(state.isRecording ?
            "Click to STOP recording and save notes" :
            "Click to START recording\n"
            "Then play pads/keyboard\n"
            "Notes are saved when you stop");
    }

    // Draw recording indicator (blinking red dot)
    if (state.isRecording) {
        float time = static_cast<float>(ImGui::GetTime());
        float alpha = (std::sin(time * 6.0f) + 1.0f) * 0.5f;
        ImVec2 indicatorPos(recPos.x + 85, recPos.y + 20);
        drawList->AddCircleFilled(indicatorPos, 8, IM_COL32(255, 0, 0, static_cast<int>(alpha * 255)));
        drawList->AddCircle(indicatorPos, 10, IM_COL32(255, 100, 100, 150), 12, 2.0f);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Quantize", &state.quantizeEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    const char* quantItems[] = {"1/4", "1/8", "1/16", "1/32"};
    static int quantIndex = 2;  // Default to 1/16
    if (ImGui::Combo("##quant", &quantIndex, quantItems, 4)) {
        const float quantVals[] = {1.0f, 0.5f, 0.25f, 0.125f};
        state.quantizeResolution = quantVals[quantIndex];
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Arpeggiator controls
    ImGui::Checkbox("ARP", &state.arpEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    const char* arpModeNames[] = {"Up", "Down", "Up-Down", "Random"};
    if (ImGui::Combo("##arpmode", &state.arpMode, arpModeNames, 4)) {
        // arpMode is already an int (0-3)
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    const char* arpRateNames[] = {"1/4", "1/8", "1/16", "1/32"};
    int arpRateInt = static_cast<int>(state.arpRate);
    if (ImGui::Combo("##arprate", &arpRateInt, arpRateNames, 4)) {
        state.arpRate = static_cast<ArpRate>(arpRateInt);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::SliderInt("Oct", &state.arpOctaves, 1, 4);

    ImGui::EndChild();

    // ==========================================================================
    // Main Section: Pads + Knobs + Waveform
    // ==========================================================================
    ImGui::BeginChild("MainSection", ImVec2(0, windowSize.y - keyboardHeight - 100), false);

    // Left side: 4x4 Pad Grid
    ImGui::BeginChild("PadGrid", ImVec2(padSize * 4 + 30, 0), true);

    const auto& currentBank = state.getCurrentBank();
    float padding = 5.0f;

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            int idx = row * 4 + col;

            if (col > 0) ImGui::SameLine();

            ImVec2 padPos = ImGui::GetCursorScreenPos();
            DrawPad(idx, currentBank[idx], state.padActive[idx], state.padVelocity[idx],
                   ImVec2(padSize, padSize), sequencer, state);

            // Record the event if recording
            if (state.isRecording && ImGui::IsItemActivated()) {
                RecordedNoteEvent evt;
                evt.pitch = currentBank[idx].midiNote;
                evt.velocity = 0.8f;
                evt.timestamp = sequencer.getCurrentBeat();
                evt.oscillatorType = currentBank[idx].oscillatorType;
                evt.duration = 0.25f;  // Default duration, could be adjusted
                evt.isNoteOn = true;
                state.recordedEvents.push_back(evt);
            }
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // Right side: Knobs + Waveform
    ImGui::BeginChild("KnobsAndWaveform", ImVec2(0, 0), true);

    // Parameter Knobs (2 rows of 4)
    const char* knobLabels[] = {"Attack", "Decay", "Sustain", "Release",
                                 "Width", "Detune", "Filter", "Volume"};
    ImU32 knobColors[] = {
        IM_COL32(255, 100, 100, 255),  // Attack - red
        IM_COL32(255, 180, 100, 255),  // Decay - orange
        IM_COL32(100, 255, 100, 255),  // Sustain - green
        IM_COL32(100, 180, 255, 255),  // Release - blue
        IM_COL32(255, 100, 255, 255),  // Width - magenta
        IM_COL32(100, 255, 255, 255),  // Detune - cyan
        IM_COL32(255, 255, 100, 255),  // Filter - yellow
        IM_COL32(200, 200, 200, 255)   // Volume - white
    };

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 4; ++col) {
            int idx = row * 4 + col;
            if (col > 0) ImGui::SameLine();

            float minVal = (idx == 5) ? -1.0f : 0.0f;  // Detune can be negative
            float maxVal = 1.0f;
            if (idx < 4) {
                // ADSR values in seconds
                maxVal = (idx == 0 || idx == 3) ? 2.0f : 1.0f;
            }

            if (DrawKnob(knobLabels[idx], &state.knobValues[idx], minVal, maxVal,
                        knobRadius, knobColors[idx])) {
                // Apply knob values to the preview channel synth (channel 7)
                const int previewChannel = 7;  // MAX_CHANNELS - 1
                auto& synth = sequencer.getSynth(previewChannel);

                // Build envelope and oscillator config from knob values
                Envelope env;
                env.attack = state.knobValues[0];   // Knob 0: Attack
                env.decay = state.knobValues[1];    // Knob 1: Decay
                env.sustain = state.knobValues[2];  // Knob 2: Sustain
                env.release = state.knobValues[3];  // Knob 3: Release

                OscillatorConfig osc;
                osc.pulseWidth = state.knobValues[4];  // Knob 4: Width
                osc.detune = state.knobValues[5] * 100.0f;  // Knob 5: Detune (-100 to +100 cents)

                synth.setConfig(osc, env);

                // Update project channel volume for preview channel
                project.channels[previewChannel].volume = state.knobValues[7];  // Knob 7: Volume
            }
        }
        ImGui::Spacing();
    }

    // Apply knob values every frame (not just on change) for live control
    {
        const int previewChannel = 7;
        Envelope env;
        env.attack = state.knobValues[0];
        env.decay = state.knobValues[1];
        env.sustain = state.knobValues[2];
        env.release = state.knobValues[3];

        OscillatorConfig osc;
        osc.pulseWidth = state.knobValues[4];
        osc.detune = state.knobValues[5] * 100.0f;

        sequencer.getSynth(previewChannel).setConfig(osc, env);
        project.channels[previewChannel].volume = state.knobValues[7];
    }

    ImGui::Separator();

    // Waveform Display
    ImGui::Text("Waveform");
    ImVec2 wavePos = ImGui::GetCursorScreenPos();
    ImVec2 waveSize(ImGui::GetContentRegionAvail().x - 10, 80);

    // Background
    drawList->AddRectFilled(wavePos, ImVec2(wavePos.x + waveSize.x, wavePos.y + waveSize.y),
                           IM_COL32(20, 20, 30, 255), 4.0f);
    drawList->AddRect(wavePos, ImVec2(wavePos.x + waveSize.x, wavePos.y + waveSize.y),
                     IM_COL32(60, 60, 80, 255), 4.0f);

    // Draw waveform from buffer
    float centerY = wavePos.y + waveSize.y * 0.5f;
    drawList->AddLine(ImVec2(wavePos.x, centerY),
                     ImVec2(wavePos.x + waveSize.x, centerY),
                     IM_COL32(40, 40, 60, 255));

    if (state.waveformBuffer[0] != 0.0f || sequencer.isPlaying()) {
        ImVec2 prevPoint(wavePos.x, centerY);
        for (int i = 0; i < PadControllerState::WAVEFORM_SAMPLES - 1; ++i) {
            int readIdx = (state.waveformWritePos + i) % PadControllerState::WAVEFORM_SAMPLES;
            float sample = state.waveformBuffer[readIdx];
            float x = wavePos.x + (static_cast<float>(i) / PadControllerState::WAVEFORM_SAMPLES) * waveSize.x;
            float y = centerY - sample * waveSize.y * 0.4f;
            y = std::clamp(y, wavePos.y + 2, wavePos.y + waveSize.y - 2);

            ImVec2 point(x, y);
            drawList->AddLine(prevPoint, point, IM_COL32(0, 255, 150, 200), 1.5f);
            prevPoint = point;
        }
    }

    ImGui::Dummy(waveSize);

    // Keyboard sound selector
    ImGui::Spacing();
    ImGui::Text("Keyboard Sound:");
    ImGui::SameLine();

    const char* soundNames[] = {"Lead", "Pad", "Bass", "Pluck", "Saw", "Pulse", "Tri", "Super"};
    OscillatorType soundTypes[] = {
        OscillatorType::SynthLead, OscillatorType::SynthPad, OscillatorType::SynthBass,
        OscillatorType::SynthPluck, OscillatorType::Sawtooth, OscillatorType::Pulse,
        OscillatorType::Triangle, OscillatorType::Supersaw
    };

    for (int i = 0; i < 8; ++i) {
        if (i > 0) ImGui::SameLine();
        bool selected = state.keyboardSound == soundTypes[i];
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(100, 150, 255, 255));
        }
        if (ImGui::SmallButton(soundNames[i])) {
            state.keyboardSound = soundTypes[i];
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();

    ImGui::EndChild();

    // ==========================================================================
    // Bottom Section: 2-Octave Keyboard
    // ==========================================================================
    ImGui::BeginChild("Keyboard", ImVec2(0, keyboardHeight), true);

    ImVec2 keyboardPos = ImGui::GetCursorScreenPos();
    float whiteKeyWidth = (windowSize.x - 30) / 15.0f;  // 15 white keys for 2 octaves + 1
    float whiteKeyHeight = keyboardHeight - 20;
    float blackKeyWidth = whiteKeyWidth * 0.65f;
    float blackKeyHeight = whiteKeyHeight * 0.6f;

    // Octave controls
    ImGui::Text("Octave: %d", state.keyboardOctave);
    ImGui::SameLine();
    if (ImGui::SmallButton("-")) {
        state.keyboardOctave = std::max(0, state.keyboardOctave - 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        state.keyboardOctave = std::min(7, state.keyboardOctave + 1);
    }

    // Draw white keys first
    static const int whiteKeyNotes[] = {0, 2, 4, 5, 7, 9, 11};  // C D E F G A B
    float x = keyboardPos.x;

    for (int oct = 0; oct < 2; ++oct) {
        for (int i = 0; i < 7; ++i) {
            int keyIndex = whiteKeyNotes[i];
            int midiNote = (state.keyboardOctave + oct) * 12 + keyIndex;

            ImVec2 keyPos(x, keyboardPos.y + 20);
            ImVec2 keySize(whiteKeyWidth - 2, whiteKeyHeight);

            DrawPianoKey(keyIndex + oct * 12, state.keyboardOctave, false,
                        state.keyActive[oct * 12 + i], keyPos, keySize,
                        sequencer, state.keyboardSound, state);

            // Record if recording and key was triggered
            if (state.isRecording && ImGui::IsItemActivated()) {
                RecordedNoteEvent evt;
                evt.pitch = midiNote;
                evt.velocity = 0.8f;
                evt.timestamp = sequencer.getCurrentBeat();
                evt.oscillatorType = state.keyboardSound;
                evt.duration = 0.5f;
                evt.isNoteOn = true;
                state.recordedEvents.push_back(evt);
            }

            x += whiteKeyWidth;
        }
    }

    // Add final C
    {
        int midiNote = (state.keyboardOctave + 2) * 12;
        ImVec2 keyPos(x, keyboardPos.y + 20);
        ImVec2 keySize(whiteKeyWidth - 2, whiteKeyHeight);
        DrawPianoKey(0, state.keyboardOctave + 2, false, false, keyPos, keySize,
                    sequencer, state.keyboardSound, state);
    }

    // Draw black keys on top
    static const int blackKeyOffsets[] = {1, 3, -1, 6, 8, 10, -1};  // C# D# skip F# G# A# skip
    x = keyboardPos.x + whiteKeyWidth - blackKeyWidth * 0.5f;

    for (int oct = 0; oct < 2; ++oct) {
        for (int i = 0; i < 7; ++i) {
            if (blackKeyOffsets[i] >= 0) {
                int keyIndex = blackKeyOffsets[i];

                ImVec2 keyPos(x, keyboardPos.y + 20);
                ImVec2 keySize(blackKeyWidth, blackKeyHeight);

                DrawPianoKey(keyIndex, state.keyboardOctave + oct, true, false,
                            keyPos, keySize, sequencer, state.keyboardSound, state);
            }
            x += whiteKeyWidth;
        }
    }

    ImGui::EndChild();

    ImGui::End();
}

} // namespace ChiptuneTracker
