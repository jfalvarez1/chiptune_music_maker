#pragma once

/*
 * ChiptuneTracker - Undo history
 *
 * The old UndoHistory snapshotted `std::vector<Note>` and a pattern index,
 * so it covered notes in one pattern and nothing else. Channel settings,
 * macros, effects, the arrangement, wavetables and automation were all
 * outside it - tweak a macro or delete a clip and there was no way back.
 * And File > Edit > Undo was literally `if (ImGui::MenuItem("Undo")) {}`.
 *
 * A snapshot here is the whole project, serialised through the same
 * writeProject/readProject pair the .ctp format uses. That is the point: a
 * field that survives a save/load round trip survives undo automatically,
 * and one that does not shows up as a bug in both places at once rather
 * than silently only in undo.
 *
 * Text snapshots are also far smaller than they look. Omit-if-default means
 * a typical project serialises to a few KB, so the whole history costs less
 * than one uncompressed copy of the Project struct.
 */

#include "Types.h"
#include "ProjectSerializer.h"

#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <utility>

namespace ChiptuneTracker {

struct UndoSnapshot {
    std::string data;    // serialised project
    std::string label;   // what the user did, for the menu item
};

class UndoHistory {
public:
    // Deep enough to cover a working session, bounded so a long session in a
    // large project cannot grow without limit.
    static constexpr size_t MAX_ENTRIES = 64;
    static constexpr size_t MAX_BYTES = 24u * 1024u * 1024u;

    // Call BEFORE mutating the project - the snapshot is the state to come
    // back to. This is how the existing thirteen call sites in UI.h already
    // use it, so the ordering carries over unchanged.
    void saveState(const Project& project, const char* label = "Edit") {
        std::string data;
        if (!serialize(project, data)) return;

        // Several gestures call this on the click that begins a drag and
        // again as it resolves. Recording an identical state would spend an
        // undo step that appears to do nothing when the user presses Ctrl+Z.
        if (!m_undo.empty() && m_undo.back().data == data) return;

        m_undo.push_back(UndoSnapshot{std::move(data), label ? label : "Edit"});
        trim(m_undo);

        // Any new edit invalidates the redo branch.
        m_redo.clear();

        ++m_edits;
    }

    /*
     * How many edits have been recorded, ever.
     *
     * For the autosave, which needs to know whether anything changed and
     * cannot afford to serialise the project every frame to find out. Its
     * own check was a fingerprint of note COUNTS - so changing a note's
     * pitch, its velocity, its length, any effect on it, or any mixer or
     * instrument setting left the fingerprint identical and never triggered
     * a save. Only adding or deleting something did.
     *
     * This is exact for anything undoable, which is every edit that goes
     * through the eighty-odd call sites in the UI, and it costs one integer.
     * Note that saveState deliberately drops a state identical to the last
     * one, so a drag that resolves to where it started does not count -
     * which is correct: nothing changed.
     */
    uint64_t editCount() const { return m_edits; }

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    // Labels for the menu, so it reads "Undo Draw Note" rather than "Undo".
    const char* undoLabel() const {
        return m_undo.empty() ? "" : m_undo.back().label.c_str();
    }
    const char* redoLabel() const {
        return m_redo.empty() ? "" : m_redo.back().label.c_str();
    }

    bool undo(Project& project) {
        return step(m_undo, m_redo, project);
    }

    bool redo(Project& project) {
        return step(m_redo, m_undo, project);
    }

    void clear() {
        m_undo.clear();
        m_redo.clear();
    }

    size_t undoDepth() const { return m_undo.size(); }
    size_t redoDepth() const { return m_redo.size(); }

    // Total bytes held, so the UI can show the cost if it ever wants to and
    // the tests can assert the bound is actually enforced.
    size_t byteSize() const { return bytesIn(m_undo) + bytesIn(m_redo); }

private:
    static bool serialize(const Project& project, std::string& out) {
        std::ostringstream stream;
        if (!writeProject(stream, project)) return false;
        out = stream.str();
        return !out.empty();
    }

    static bool deserialize(const std::string& data, Project& project) {
        std::istringstream stream(data);
        return readProject(stream, project);
    }

    // Move one state across, putting the current one on the opposite stack so
    // the operation is its own inverse.
    static bool step(std::vector<UndoSnapshot>& from,
                     std::vector<UndoSnapshot>& to,
                     Project& project) {
        if (from.empty()) return false;

        std::string current;
        if (!serialize(project, current)) return false;

        // Copy before popping - the restore reads from this after the pop.
        const UndoSnapshot target = from.back();
        from.pop_back();

        if (!deserialize(target.data, project)) {
            // Put it back rather than losing a step to a malformed snapshot.
            from.push_back(target);
            return false;
        }

        to.push_back(UndoSnapshot{std::move(current), target.label});
        trim(to);
        return true;
    }

    static size_t bytesIn(const std::vector<UndoSnapshot>& stack) {
        size_t total = 0;
        for (const UndoSnapshot& entry : stack) total += entry.data.size();
        return total;
    }

    // Drop from the far end, which is the oldest state - the one the user is
    // least likely to want back.
    static void trim(std::vector<UndoSnapshot>& stack) {
        while (stack.size() > MAX_ENTRIES) {
            stack.erase(stack.begin());
        }
        while (stack.size() > 1 && bytesIn(stack) > MAX_BYTES) {
            stack.erase(stack.begin());
        }
    }

    // Every recorded edit, counted. See editCount().
    uint64_t m_edits = 0;

    std::vector<UndoSnapshot> m_undo;
    std::vector<UndoSnapshot> m_redo;
};

} // namespace ChiptuneTracker
