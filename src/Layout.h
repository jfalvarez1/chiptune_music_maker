#pragma once

/*
 * ChiptuneTracker - Docking workspaces
 *
 * The app has nineteen panels. Left to per-window default positions they
 * overlap, spill off the bottom of a small display and leave gaps on a
 * large one, which is the single biggest reason the interface reads as
 * unfinished - ahead of any question of colour or widget styling.
 *
 * The answer the established tools converged on:
 *
 *   Reaper   a Docker: panels dock into shared regions and become TABS,
 *            plus Screensets - saved layouts you switch between for
 *            recording, mixing and arranging.
 *   Bitwig   docked, resizable panels rather than a rigid layout.
 *   Furnace  "the most flexible and customizable tracker interface ever",
 *            built on Dear ImGui's docking.
 *
 * So: a full-viewport DockSpace, panels docked into named regions, panels
 * sharing a region rendered as tabs, and three workspaces that rebuild the
 * arrangement for the task at hand. Users can then drag any panel anywhere
 * and their arrangement persists in imgui.ini - the workspaces are a good
 * starting point, not a cage.
 *
 * Panels never overlap and never fall off screen, because the dock nodes
 * partition the viewport exactly.
 */

#include "Types.h"

#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder* lives here

#include <algorithm>

namespace ChiptuneTracker {

enum class Workspace : uint8_t {
    Compose,      // writing parts: editor large, palette and patterns to hand
    SoundDesign,  // shaping one sound: instrument editors large, score small
    Mix           // balancing: mixer wide, analysers visible
};

inline const char* WorkspaceName(Workspace workspace) {
    switch (workspace) {
        case Workspace::Compose:     return "Compose";
        case Workspace::SoundDesign: return "Sound Design";
        case Workspace::Mix:         return "Mix";
    }
    return "Compose";
}

inline constexpr int WORKSPACE_COUNT = 3;

// The editor views share one dock slot: only one is shown at a time, and
// which one is chosen by the View menu rather than by a dock tab.
inline constexpr const char* EDITOR_VIEWS[] = {
    "Piano Roll", "Tracker", "Arrangement", "Mixer", "Pad Controller"
};

// ============================================================================
// The dock space
//
// Hosted in a full-viewport window with no decoration, so the dock nodes
// cover everything below the menu bar.
// ============================================================================
inline ImGuiID BeginDockSpace() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("##DockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspaceId = ImGui::GetID("ChiptuneDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
    return dockspaceId;
}

// ============================================================================
// Workspace layouts
//
// Each rebuilds the dock tree from scratch. Splitting proportions are
// fractions of the *remaining* space at each step, which is how
// DockBuilderSplitNode works - so they read in the order the regions are
// carved off.
// ============================================================================
inline void BuildWorkspaceLayout(ImGuiID dockspaceId, Workspace workspace, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);

    // The toolbars are short and wide; they get a strip across the top in
    // every workspace, because transport and file actions are always
    // relevant no matter what you are doing.
    // 0.16 rather than 0.13: Transport carries three rows (transport
    // buttons, position, master volume) and was being clipped.
    ImGuiID remaining = dockspaceId;
    const ImGuiID topStrip = ImGui::DockBuilderSplitNode(
        remaining, ImGuiDir_Up, 0.16f, nullptr, &remaining);

    // Split the strip three ways. File carries seven buttons and Views five,
    // so they get more room than Transport.
    ImGuiID stripRest = topStrip;
    const ImGuiID transportNode = ImGui::DockBuilderSplitNode(
        stripRest, ImGuiDir_Left, 0.27f, nullptr, &stripRest);
    const ImGuiID fileNode = ImGui::DockBuilderSplitNode(
        stripRest, ImGuiDir_Left, 0.52f, nullptr, &stripRest);
    const ImGuiID viewsNode = stripRest;

    ImGui::DockBuilderDockWindow("Transport", transportNode);
    ImGui::DockBuilderDockWindow("File", fileNode);
    ImGui::DockBuilderDockWindow("Views", viewsNode);

    switch (workspace) {
        case Workspace::Compose: {
            ImGuiID centre = remaining;
            const ImGuiID leftNode = ImGui::DockBuilderSplitNode(
                centre, ImGuiDir_Left, 0.18f, nullptr, &centre);
            const ImGuiID rightNode = ImGui::DockBuilderSplitNode(
                centre, ImGuiDir_Right, 0.22f, nullptr, &centre);

            ImGuiID leftLower = leftNode;
            const ImGuiID leftUpper = ImGui::DockBuilderSplitNode(
                leftLower, ImGuiDir_Up, 0.62f, nullptr, &leftLower);

            ImGuiID rightLower = rightNode;
            const ImGuiID rightUpper = ImGui::DockBuilderSplitNode(
                rightLower, ImGuiDir_Up, 0.34f, nullptr, &rightLower);

            ImGui::DockBuilderDockWindow("Sound Palette", leftUpper);
            ImGui::DockBuilderDockWindow("Patterns", leftLower);
            ImGui::DockBuilderDockWindow("Tools", leftLower);        // tabbed with Patterns

            ImGui::DockBuilderDockWindow("Note Editor", rightUpper);
            ImGui::DockBuilderDockWindow("Channel Editor", rightLower);
            ImGui::DockBuilderDockWindow("Instrument Macros", rightLower);

            for (const char* view : EDITOR_VIEWS) {
                ImGui::DockBuilderDockWindow(view, centre);
            }
            // Analysis panels tab into the centre, behind the editor
            ImGui::DockBuilderDockWindow("Spectrum Analyzer", centre);
            ImGui::DockBuilderDockWindow("Automation", centre);
            ImGui::DockBuilderDockWindow("Wavetable Editor", centre);
            ImGui::DockBuilderDockWindow("MIDI Input", rightLower);
            break;
        }

        case Workspace::SoundDesign: {
            // Sound design wants the instrument editors large and the score
            // small - you are listening to one note, not reading a part.
            ImGuiID lower = remaining;
            const ImGuiID upper = ImGui::DockBuilderSplitNode(
                lower, ImGuiDir_Up, 0.58f, nullptr, &lower);

            ImGuiID upperRight = upper;
            const ImGuiID upperLeft = ImGui::DockBuilderSplitNode(
                upperRight, ImGuiDir_Left, 0.34f, nullptr, &upperRight);
            const ImGuiID upperMid = ImGui::DockBuilderSplitNode(
                upperRight, ImGuiDir_Left, 0.5f, nullptr, &upperRight);

            ImGuiID lowerRight = lower;
            const ImGuiID lowerLeft = ImGui::DockBuilderSplitNode(
                lowerRight, ImGuiDir_Left, 0.34f, nullptr, &lowerRight);
            const ImGuiID lowerMid = ImGui::DockBuilderSplitNode(
                lowerRight, ImGuiDir_Left, 0.5f, nullptr, &lowerRight);

            ImGui::DockBuilderDockWindow("Channel Editor", upperLeft);
            ImGui::DockBuilderDockWindow("Instrument Macros", upperMid);
            ImGui::DockBuilderDockWindow("Wavetable Editor", upperRight);

            ImGui::DockBuilderDockWindow("Sound Palette", lowerLeft);
            ImGui::DockBuilderDockWindow("Spectrum Analyzer", lowerMid);
            ImGui::DockBuilderDockWindow("Tools", lowerMid);
            for (const char* view : EDITOR_VIEWS) {
                ImGui::DockBuilderDockWindow(view, lowerRight);
            }
            ImGui::DockBuilderDockWindow("Note Editor", lowerRight);
            ImGui::DockBuilderDockWindow("Patterns", lowerLeft);
            ImGui::DockBuilderDockWindow("Automation", lowerMid);
            ImGui::DockBuilderDockWindow("MIDI Input", upperLeft);
            break;
        }

        case Workspace::Mix: {
            ImGuiID lower = remaining;
            const ImGuiID upper = ImGui::DockBuilderSplitNode(
                lower, ImGuiDir_Up, 0.52f, nullptr, &lower);

            ImGuiID upperRight = upper;
            const ImGuiID upperMain = ImGui::DockBuilderSplitNode(
                upperRight, ImGuiDir_Left, 0.76f, nullptr, &upperRight);

            ImGuiID lowerRight = lower;
            const ImGuiID lowerLeft = ImGui::DockBuilderSplitNode(
                lowerRight, ImGuiDir_Left, 0.5f, nullptr, &lowerRight);

            for (const char* view : EDITOR_VIEWS) {
                ImGui::DockBuilderDockWindow(view, upperMain);
            }
            ImGui::DockBuilderDockWindow("Channel Editor", upperRight);
            ImGui::DockBuilderDockWindow("Instrument Macros", upperRight);

            ImGui::DockBuilderDockWindow("Spectrum Analyzer", lowerLeft);
            ImGui::DockBuilderDockWindow("Automation", lowerRight);
            ImGui::DockBuilderDockWindow("Sound Palette", lowerLeft);
            ImGui::DockBuilderDockWindow("Patterns", lowerRight);
            ImGui::DockBuilderDockWindow("Note Editor", lowerRight);
            ImGui::DockBuilderDockWindow("Tools", lowerLeft);
            ImGui::DockBuilderDockWindow("Wavetable Editor", lowerLeft);
            ImGui::DockBuilderDockWindow("MIDI Input", upperRight);
            break;
        }
    }

    ImGui::DockBuilderFinish(dockspaceId);
}

// Standalone tools are never docked - they are separate utilities, not
// panels of the workspace.
inline bool IsFloatingTool(const char* name) {
    return std::string(name) == "Voice to Note Converter";
}

} // namespace ChiptuneTracker
