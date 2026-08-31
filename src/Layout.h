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

    // Inset from the viewport edges. Docking tiles its area exactly, so a
    // full-bleed dock space left the animated theme backgrounds - the Matrix
    // rain, the Synthwave sun and grid - with nowhere to appear at all.
    //
    // The margin turns them into a frame around the workspace, and the
    // separator gutters let them run between panels. They stay where they
    // belong: behind and around the work, never underneath the text.
    constexpr float MARGIN = 26.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + MARGIN,
                                   viewport->WorkPos.y + MARGIN));
    ImGui::SetNextWindowSize(ImVec2(std::max(64.0f, viewport->WorkSize.x - MARGIN * 2.0f),
                                    std::max(64.0f, viewport->WorkSize.y - MARGIN * 2.0f)));
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

// Is the dock space empty - no windows docked into it anywhere?
//
// This is how an upgrade from a pre-docking version presents. The saved
// imgui.ini has positions for every window but no DockId assignments, so
// ImGui restores them as floating windows scattered at their old
// coordinates and the dock space sits empty behind them. The app used to
// skip its default layout whenever *any* ini existed, so there was nothing
// to correct it and no sign that Ctrl+0 would.
//
// Checking the tree rather than sniffing the file means this also catches
// an ini truncated mid-write, or one hand-edited into uselessness.
inline bool IsDockSpaceEmpty(ImGuiID dockspaceId) {
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
    if (node == nullptr) return true;

    // A node holding windows, or split into children that might, is fine.
    if (node->IsSplitNode()) return false;
    return node->Windows.Size == 0;
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
    // Must match the inset in BeginDockSpace, or the tree is built at the
    // wrong size and every panel is laid out slightly off.
    constexpr float MARGIN = 26.0f;
    const ImVec2 inner(std::max(64.0f, size.x - MARGIN * 2.0f),
                       std::max(64.0f, size.y - MARGIN * 2.0f));

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, inner);

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
            ImGui::DockBuilderDockWindow("Master Bus", rightLower);

            for (const char* view : EDITOR_VIEWS) {
                ImGui::DockBuilderDockWindow(view, centre);
            }
            // Analysis panels tab into the centre, behind the editor
            ImGui::DockBuilderDockWindow("Spectrum Analyzer", centre);
            ImGui::DockBuilderDockWindow("Automation", centre);
            ImGui::DockBuilderDockWindow("Wavetable Editor", centre);
            ImGui::DockBuilderDockWindow("MIDI Input", rightLower);
            ImGui::DockBuilderDockWindow("Voice to Notes", leftLower);
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
            ImGui::DockBuilderDockWindow("Master Bus", lowerMid);
            ImGui::DockBuilderDockWindow("MIDI Input", upperLeft);
            ImGui::DockBuilderDockWindow("Voice to Notes", upperLeft);
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
            ImGui::DockBuilderDockWindow("Master Bus", upperRight);
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
            ImGui::DockBuilderDockWindow("Voice to Notes", upperRight);
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

/*
 * Dock any core panel the saved layout has never heard of.
 *
 * An imgui.ini written by an older build restores fine - but a panel added
 * since then has no entry, so it floats at its fallback position on top of
 * whatever is underneath. The empty-dockspace health check does not catch
 * this: the layout is fine except for the orphan.
 *
 * Each panel names a sibling it belongs beside. An orphan joins its
 * sibling's node as a tab; everything the user arranged is left exactly as
 * it was, which is why this is not a rebuild. Returns how many windows were
 * adopted, so the caller can log it.
 *
 * Add a row here whenever a new dockable window is born - the smoke test
 * cannot catch a missing row, because a fresh capture has no stale ini.
 */
inline int AdoptOrphanedWindows() {
    struct Adoption { const char* window; const char* sibling; };
    static const Adoption ADOPTIONS[] = {
        {"Master Bus",        "Channel Editor"},
        {"Instrument Macros", "Channel Editor"},
        {"Wavetable Editor",  "Piano Roll"},
        {"Spectrum Analyzer", "Piano Roll"},
        {"Automation",        "Piano Roll"},
        {"MIDI Input",        "Channel Editor"},
        {"Voice to Notes",    "Tools"},
    };

    int adopted = 0;
    for (const Adoption& adoption : ADOPTIONS) {
        ImGuiWindow* window = ImGui::FindWindowByName(adoption.window);
        if (window == nullptr) continue;          // never opened; nothing to fix
        if (window->DockId != 0) continue;        // already lives somewhere

        ImGuiWindow* sibling = ImGui::FindWindowByName(adoption.sibling);
        if (sibling == nullptr || sibling->DockId == 0) continue;

        ImGui::SetWindowDock(window, sibling->DockId, ImGuiCond_Always);
        ++adopted;
    }
    return adopted;
}

} // namespace ChiptuneTracker
