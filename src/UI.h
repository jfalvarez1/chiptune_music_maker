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
    ImGui::DragInt("Length", &pattern.length, 1, 1, 64);
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
    // Delete selected note button
    bool hasSelectedNote = (ui.selectedNoteIndex >= 0 &&
                           ui.selectedNoteIndex < static_cast<int>(pattern.notes.size()));

    // Copy button
    if (!hasSelectedNote) ImGui::BeginDisabled();
    if (ImGui::Button("Copy")) {
        if (hasSelectedNote) {
            g_NoteClipboard.clear();
            g_NoteClipboard.push_back(pattern.notes[ui.selectedNoteIndex]);
            g_ClipboardBaseTime = pattern.notes[ui.selectedNoteIndex].startTime;
        }
    }
    if (!hasSelectedNote) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+C");

    ImGui::SameLine();

    // Paste button
    if (g_NoteClipboard.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Paste")) {
        if (!g_NoteClipboard.empty()) {
            float pasteTime = std::fmod(seq.getCurrentBeat(), static_cast<float>(pattern.length));
            for (const Note& clipNote : g_NoteClipboard) {
                Note newNote = clipNote;
                float offset = clipNote.startTime - g_ClipboardBaseTime;
                newNote.startTime = pasteTime + offset;
                while (newNote.startTime >= pattern.length) newNote.startTime -= pattern.length;
                while (newNote.startTime < 0) newNote.startTime += pattern.length;
                pattern.notes.push_back(newNote);
            }
            ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;
        }
    }
    if (g_NoteClipboard.empty()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+V - Paste at playhead");

    ImGui::SameLine();

    // Delete button
    if (!hasSelectedNote) ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        if (hasSelectedNote) {
            pattern.notes.erase(pattern.notes.begin() + ui.selectedNoteIndex);
            ui.selectedNoteIndex = -1;
        }
    }
    if (!hasSelectedNote) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete/Backspace");

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

    float gridHeight = (highestNote - lowestNote) * noteHeight;
    float gridWidth = pattern.length * beatWidth;

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

    // Beat grid lines
    for (int beat = 0; beat <= pattern.length; ++beat) {
        float x = canvasPos.x + keyWidth + beat * beatWidth - ui.scrollX;
        if (x < canvasPos.x + keyWidth || x > canvasPos.x + canvasSize.x) continue;

        ImU32 lineColor = (beat % project.beatsPerMeasure == 0)
            ? IM_COL32(100, 100, 110, 255)
            : IM_COL32(50, 50, 55, 255);
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

        // Escape to deselect
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ui.selectedNoteIndex = -1;
            ui.selectedNoteIndices.clear();
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

        // Paste (Ctrl+V) - paste at playhead position, preserving relative arrangement
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
            if (!g_NoteClipboard.empty()) {
                // Save state for undo before pasting
                g_UndoHistory.saveState(pattern, ui.selectedPattern);

                // Get current playhead position in pattern
                float pasteTime = std::fmod(seq.getCurrentBeat(), static_cast<float>(pattern.length));

                // Clear selection and prepare to select pasted notes
                ui.selectedNoteIndices.clear();
                int firstPastedIndex = static_cast<int>(pattern.notes.size());

                for (const Note& clipNote : g_NoteClipboard) {
                    Note newNote = clipNote;
                    // Offset from clipboard base time to paste position
                    float timeOffset = clipNote.startTime - g_ClipboardBaseTime;
                    newNote.startTime = pasteTime + timeOffset;

                    // Wrap to pattern length
                    while (newNote.startTime >= pattern.length) {
                        newNote.startTime -= pattern.length;
                    }
                    while (newNote.startTime < 0) {
                        newNote.startTime += pattern.length;
                    }

                    pattern.notes.push_back(newNote);
                    ui.selectedNoteIndices.push_back(static_cast<int>(pattern.notes.size()) - 1);
                }

                // Select the first pasted note as primary
                if (!ui.selectedNoteIndices.empty()) {
                    ui.selectedNoteIndex = ui.selectedNoteIndices[0];
                }
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
                newNote.duration = 0.5f;
                newNote.velocity = 0.8f;
                newNote.oscillatorType = static_cast<OscillatorType>(oscType);  // Per-note oscillator
                pattern.notes.push_back(newNote);
                ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;
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
        // Handle mouse down
        if (ImGui::IsMouseClicked(0) && relX >= 0) {
            switch (ui.pianoRollMode) {
                case PianoRollMode::Select:
                    if (noteUnderCursor >= 0) {
                        ui.selectedNoteIndex = noteUnderCursor;
                        if (onResizeHandle) {
                            // Save state for undo before resizing
                            g_UndoHistory.saveState(pattern, ui.selectedPattern);
                            // Start resizing
                            ui.isResizingNote = true;
                            ui.dragStartDuration = pattern.notes[noteUnderCursor].duration;
                            ui.dragStartBeat = hoveredBeat;
                        } else {
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
                        newNote.duration = 0.25f;
                        newNote.velocity = 0.8f;
                        // Use selected palette item's oscillator type if one is selected
                        if (g_SelectedPaletteItem >= 0) {
                            newNote.oscillatorType = static_cast<OscillatorType>(g_SelectedPaletteItem);
                        }
                        pattern.notes.push_back(newNote);
                        ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;
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
                float deltaBeats = hoveredBeat - ui.dragStartBeat;
                float newDuration = ui.dragStartDuration + deltaBeats;
                note.duration = std::max(0.0625f, std::floor(newDuration * 4.0f) / 4.0f);
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
        case OscillatorType::Kick: {
            // Kick drum icon - large circle
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.8f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.8f, IM_COL32(255, 255, 255, 100), 12, 2.0f);
            break;
        }
        case OscillatorType::Snare: {
            // Snare drum icon - circle with lines
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.7f, color, 12, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.5f, cy - hh*0.5f), ImVec2(cx + hh*0.5f, cy + hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(cx + hh*0.5f, cy - hh*0.5f), ImVec2(cx - hh*0.5f, cy + hh*0.5f), color, 2.0f);
            break;
        }
        case OscillatorType::HiHat: {
            // Hi-hat icon - two overlapping circles
            drawList->AddCircle(ImVec2(cx - 4, cy), hh * 0.5f, color, 8, 2.0f);
            drawList->AddCircle(ImVec2(cx + 4, cy), hh * 0.5f, color, 8, 2.0f);
            break;
        }
        case OscillatorType::Tom: {
            // Tom drum icon - oval
            drawList->AddEllipse(ImVec2(cx, cy), ImVec2(hw * 0.6f, hh * 0.5f), color, 0.0f, 12, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.3f), ImVec2(cx, cy + hh*0.3f), color, 2.0f);
            break;
        }
    }
}

inline void DrawSoundPalette(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("Sound Palette");

    ImGui::Text("Click to select, then click on Piano Roll to place");
    ImGui::Separator();

    const char* oscNames[] = {"Pulse", "Triangle", "Sawtooth", "Sine", "Noise", "Custom",
                              "Kick", "Snare", "HiHat", "Tom"};
    const char* oscDesc[] = {
        "Square wave - Classic NES sound",
        "Triangle wave - Soft, flute-like",
        "Sawtooth wave - Rich, buzzy",
        "Sine wave - Pure, clean",
        "Noise - Percussion, hi-hats",
        "Custom - Adjustable shape",
        "Kick drum - Deep bass hit",
        "Snare drum - Snappy crack",
        "Hi-hat - Metallic cymbal",
        "Tom drum - Pitched drum"
    };

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

    // Drums section
    ImGui::Separator();
    ImGui::Text("Drums:");
    for (int i = 6; i < 10; ++i) {
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

        drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), bgColor, 4.0f);
        drawList->AddRect(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 3.0f : 1.0f);

        // Draw drum icon
        DrawWaveformIcon(drawList, pos, iconSize, static_cast<OscillatorType>(i), waveColor);

        // Button for click and drag
        ImGui::InvisibleButton("##drum", iconSize);

        // Click to select for placement (toggle)
        if (ImGui::IsItemClicked()) {
            if (g_SelectedPaletteItem == i) {
                g_SelectedPaletteItem = -1;  // Deselect
            } else {
                g_SelectedPaletteItem = i;  // Select
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

        if (i < 9) ImGui::SameLine();
        ImGui::PopID();
    }

    ImGui::Separator();

    // Show selected item
    if (g_SelectedPaletteItem >= 0) {
        ImVec4 color = g_SelectedPaletteItem < 6 ?
            ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.6f, 0.6f, 1.0f);
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
