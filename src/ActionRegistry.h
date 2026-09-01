#pragma once

// ============================================================================
// Actions, shortcuts, and the command palette's model
//
// Every command the program can perform, in one list, each with a stable id,
// a human label, a category, and a key it answers to.
//
// The point is discoverability. A shortcut you cannot find is a feature that
// does not exist for the person who never read the manual, and until now
// this program's shortcuts were both fixed and unlisted: the only way to
// learn that Ctrl+0 resets the layout was to be told.
//
// Two things fall out of having the list:
//
//   - a palette that finds a command by name, so it can be run without
//     knowing any key at all;
//   - rebinding, because a binding is now data rather than an `if` buried in
//     the frame loop.
//
// WHAT IS AND IS NOT IN HERE. The registry owns the *global* commands -
// transport, file, view, panels, edit. It deliberately does not own the
// context-sensitive editing keys: the piano-roll mode letters, the tracker's
// cursor movement, and the note-entry keyboard. Those are not commands, they
// are a modal editing surface where the same key means different things
// depending on what has focus, and routing them through a global dispatcher
// would break that distinction. They stay where they are.
//
// No ImGui context is needed for anything in this header except dispatch(),
// which is the part that reads the keyboard - so the matcher, the parser and
// the binding file can all be tested directly.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

namespace ChiptuneTracker {

struct Project;
struct UIState;
class Sequencer;

// ============================================================================
// A key with its modifiers
// ============================================================================
struct Shortcut {
    ImGuiKey key = ImGuiKey_None;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool valid() const { return key != ImGuiKey_None; }

    bool operator==(const Shortcut& other) const {
        return key == other.key && ctrl == other.ctrl &&
               shift == other.shift && alt == other.alt;
    }
    bool operator!=(const Shortcut& other) const { return !(*this == other); }
};

/*
 * The keys that may be bound, with the names they are saved under.
 *
 * Written out rather than taken from ImGui::GetKeyName because that needs a
 * live context, and because the set of keys it is sane to bind is smaller
 * than the set ImGui knows about - binding a command to a mouse button or to
 * ImGuiKey_ModCtrl would produce something unusable.
 */
struct KeyName { ImGuiKey key; const char* name; };

inline const std::vector<KeyName>& bindableKeys() {
    static const std::vector<KeyName> KEYS = {
        {ImGuiKey_A, "A"}, {ImGuiKey_B, "B"}, {ImGuiKey_C, "C"},
        {ImGuiKey_D, "D"}, {ImGuiKey_E, "E"}, {ImGuiKey_F, "F"},
        {ImGuiKey_G, "G"}, {ImGuiKey_H, "H"}, {ImGuiKey_I, "I"},
        {ImGuiKey_J, "J"}, {ImGuiKey_K, "K"}, {ImGuiKey_L, "L"},
        {ImGuiKey_M, "M"}, {ImGuiKey_N, "N"}, {ImGuiKey_O, "O"},
        {ImGuiKey_P, "P"}, {ImGuiKey_Q, "Q"}, {ImGuiKey_R, "R"},
        {ImGuiKey_S, "S"}, {ImGuiKey_T, "T"}, {ImGuiKey_U, "U"},
        {ImGuiKey_V, "V"}, {ImGuiKey_W, "W"}, {ImGuiKey_X, "X"},
        {ImGuiKey_Y, "Y"}, {ImGuiKey_Z, "Z"},

        {ImGuiKey_0, "0"}, {ImGuiKey_1, "1"}, {ImGuiKey_2, "2"},
        {ImGuiKey_3, "3"}, {ImGuiKey_4, "4"}, {ImGuiKey_5, "5"},
        {ImGuiKey_6, "6"}, {ImGuiKey_7, "7"}, {ImGuiKey_8, "8"},
        {ImGuiKey_9, "9"},

        {ImGuiKey_F1, "F1"}, {ImGuiKey_F2, "F2"}, {ImGuiKey_F3, "F3"},
        {ImGuiKey_F4, "F4"}, {ImGuiKey_F5, "F5"}, {ImGuiKey_F6, "F6"},
        {ImGuiKey_F7, "F7"}, {ImGuiKey_F8, "F8"}, {ImGuiKey_F9, "F9"},
        {ImGuiKey_F10, "F10"}, {ImGuiKey_F11, "F11"}, {ImGuiKey_F12, "F12"},

        {ImGuiKey_Space, "Space"}, {ImGuiKey_Enter, "Enter"},
        {ImGuiKey_Tab, "Tab"}, {ImGuiKey_Escape, "Escape"},
        {ImGuiKey_Backspace, "Backspace"}, {ImGuiKey_Delete, "Delete"},
        {ImGuiKey_Insert, "Insert"},
        {ImGuiKey_Home, "Home"}, {ImGuiKey_End, "End"},
        {ImGuiKey_PageUp, "PageUp"}, {ImGuiKey_PageDown, "PageDown"},

        {ImGuiKey_LeftArrow, "Left"}, {ImGuiKey_RightArrow, "Right"},
        {ImGuiKey_UpArrow, "Up"}, {ImGuiKey_DownArrow, "Down"},

        {ImGuiKey_LeftBracket, "["}, {ImGuiKey_RightBracket, "]"},
        {ImGuiKey_Comma, ","}, {ImGuiKey_Period, "."},
        {ImGuiKey_Slash, "/"}, {ImGuiKey_Backslash, "\\"},
        {ImGuiKey_Semicolon, ";"}, {ImGuiKey_Apostrophe, "'"},
        {ImGuiKey_Minus, "-"}, {ImGuiKey_Equal, "="},
        {ImGuiKey_GraveAccent, "`"},
    };
    return KEYS;
}

inline const char* keyName(ImGuiKey key) {
    for (const KeyName& entry : bindableKeys()) {
        if (entry.key == key) return entry.name;
    }
    return "";
}

inline ImGuiKey keyFromName(const std::string& name) {
    for (const KeyName& entry : bindableKeys()) {
        if (name == entry.name) return entry.key;
    }
    return ImGuiKey_None;
}

// "Ctrl+Shift+S". Empty for an unbound action, which the UI shows as blank
// rather than as the word "None" - a column of "None" reads like an error.
inline std::string shortcutText(const Shortcut& shortcut) {
    if (!shortcut.valid()) return std::string();
    std::string text;
    if (shortcut.ctrl) text += "Ctrl+";
    if (shortcut.shift) text += "Shift+";
    if (shortcut.alt) text += "Alt+";
    text += keyName(shortcut.key);
    return text;
}

/*
 * Parse "Ctrl+Shift+S" back into a Shortcut.
 *
 * Returns an invalid Shortcut for anything it does not understand, including
 * a key name from a newer version - a binding file that mentions a key this
 * build has never heard of must leave the action on its default, not crash
 * and not silently bind something else.
 */
inline Shortcut shortcutFromText(const std::string& text) {
    Shortcut shortcut;
    size_t start = 0;

    while (true) {
        const size_t plus = text.find('+', start);
        if (plus == std::string::npos) break;

        // A trailing '+' is the key itself, not a separator.
        const std::string token = text.substr(start, plus - start);
        if (token == "Ctrl") shortcut.ctrl = true;
        else if (token == "Shift") shortcut.shift = true;
        else if (token == "Alt") shortcut.alt = true;
        else return Shortcut();     // an unknown modifier: refuse the whole thing

        start = plus + 1;
    }

    shortcut.key = keyFromName(text.substr(start));
    if (!shortcut.valid()) return Shortcut();
    return shortcut;
}

// ============================================================================
// Fuzzy matching
//
// The palette has to find "Reset Layout" from "rsl", because that is how
// people actually type into one. A plain substring search does not, and
// makes the palette feel broken rather than strict.
// ============================================================================

/*
 * Score `needle` against `haystack`, or -1 if the characters are not all
 * there in order.
 *
 * Higher is better. The bonuses are what make the ranking useful rather
 * than merely correct: a run of consecutive characters and a match at the
 * start of a word are both far stronger evidence of intent than the same
 * letters scattered through the string.
 */
inline int fuzzyScore(const std::string& needle, const std::string& haystack) {
    if (needle.empty()) return 0;
    if (needle.size() > haystack.size()) return -1;

    auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    auto isBoundary = [](char c) {
        return c == ' ' || c == '-' || c == '_' || c == '.' || c == ':' ||
               c == '/';
    };

    int score = 0;
    int run = 0;
    size_t h = 0;

    for (size_t n = 0; n < needle.size(); ++n) {
        const char want = lower(needle[n]);
        if (want == ' ') continue;   // spaces in the query match nothing in particular

        bool found = false;
        while (h < haystack.size()) {
            const char have = lower(haystack[h]);
            if (have == want) {
                score += 1;

                // Consecutive characters: the single strongest signal that
                // this is the word being typed.
                if (run > 0) score += 15;

                // The start of a word, which is what an acronym query hits.
                const bool wordStart =
                    (h == 0) ||
                    isBoundary(haystack[h - 1]) ||
                    (std::isupper(static_cast<unsigned char>(haystack[h])) &&
                     !std::isupper(static_cast<unsigned char>(haystack[h - 1])));
                if (wordStart) score += 10;

                if (h == 0) score += 20;    // matches from the very beginning

                ++run;
                ++h;
                found = true;
                break;
            }
            // A skipped character costs a little, so a tight match beats a
            // loose one over the same string.
            score -= 1;
            run = 0;
            ++h;
        }
        if (!found) return -1;
    }

    // Prefer the shorter of two otherwise equal matches: "Save" should beat
    // "Save As" for the query "save".
    score -= static_cast<int>(haystack.size()) / 8;
    return score;
}

// ============================================================================
// An action
// ============================================================================

/*
 * What an action is handed when it runs.
 *
 * A struct of pointers rather than captured references, so the registry can
 * be built once at startup and still act on whatever project is loaded now -
 * capturing a Project& would bind to the one that existed at registration,
 * which is exactly the bug that makes "New Project" stop working after the
 * first time.
 */
struct ActionContext {
    Project* project = nullptr;
    UIState* ui = nullptr;
    Sequencer* seq = nullptr;

    bool complete() const {
        return project != nullptr && ui != nullptr && seq != nullptr;
    }
};

struct Action {
    std::string id;          // stable; what a binding file refers to
    std::string label;       // what a person reads
    std::string category;    // for grouping, and searched along with the label
    std::string hint;        // one line, shown under the label in the palette

    Shortcut defaultShortcut;
    Shortcut shortcut;

    /*
     * Whether running this opens a blocking OS file dialog.
     *
     * Marked so the test that runs every action can skip these: a modal
     * dialog in a headless run does not fail, it hangs, and a suite that
     * hangs is worse than one that fails.
     */
    bool opensDialog = false;

    std::function<void(ActionContext&)> run;

    // Optional. An action that cannot run right now is shown greyed rather
    // than hidden: hiding it makes the palette's contents change as you
    // type for reasons that are invisible.
    std::function<bool(const ActionContext&)> enabled;

    bool isEnabled(const ActionContext& context) const {
        if (!enabled) return true;
        return enabled(context);
    }
};

struct ActionMatch {
    int index = -1;
    int score = 0;
};

// ============================================================================
// The registry
// ============================================================================
class ActionRegistry {
public:
    void add(Action action) {
        // An id must be unique or a binding file cannot refer to it; the
        // later registration wins, which makes overriding one deliberate.
        for (Action& existing : m_actions) {
            if (existing.id == action.id) {
                action.shortcut = action.defaultShortcut;
                existing = std::move(action);
                return;
            }
        }
        action.shortcut = action.defaultShortcut;
        m_actions.push_back(std::move(action));
    }

    size_t size() const { return m_actions.size(); }
    const std::vector<Action>& all() const { return m_actions; }
    std::vector<Action>& all() { return m_actions; }

    int indexOf(const std::string& id) const {
        for (size_t i = 0; i < m_actions.size(); ++i) {
            if (m_actions[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    const Action* find(const std::string& id) const {
        const int index = indexOf(id);
        return index < 0 ? nullptr : &m_actions[static_cast<size_t>(index)];
    }
    Action* find(const std::string& id) {
        const int index = indexOf(id);
        return index < 0 ? nullptr : &m_actions[static_cast<size_t>(index)];
    }

    /*
     * Everything matching `query`, best first.
     *
     * An empty query returns everything in registration order, which is
     * grouped by category - so opening the palette and typing nothing is a
     * readable list of what the program can do, not a random pile.
     */
    std::vector<ActionMatch> search(const std::string& query) const {
        std::vector<ActionMatch> matches;

        if (query.empty()) {
            for (size_t i = 0; i < m_actions.size(); ++i) {
                matches.push_back(ActionMatch{static_cast<int>(i), 0});
            }
            return matches;
        }

        for (size_t i = 0; i < m_actions.size(); ++i) {
            const Action& action = m_actions[i];

            // The label is what people type, so it scores on its own; the
            // category is searched too, so "view mixer" finds it, but at a
            // discount so a label match always outranks a category one.
            const int labelScore = fuzzyScore(query, action.label);
            const int bothScore =
                fuzzyScore(query, action.category + " " + action.label);

            int score = labelScore;
            if (score < 0 && bothScore >= 0) score = bothScore - 30;
            else if (score >= 0 && bothScore > score) score = bothScore;

            if (score < 0) continue;
            matches.push_back(ActionMatch{static_cast<int>(i), score});
        }

        std::stable_sort(matches.begin(), matches.end(),
                         [](const ActionMatch& a, const ActionMatch& b) {
                             return a.score > b.score;
                         });
        return matches;
    }

    // ---- Bindings ----------------------------------------------------------

    /*
     * Which other action already answers to this key, or -1.
     *
     * Rebinding does not refuse a conflict, it reports one: two commands on
     * one key is occasionally what somebody wants (they are in different
     * contexts), and a modal refusal in a settings panel is worse than a
     * warning next to the row.
     */
    int conflict(const Shortcut& shortcut, const std::string& exceptId) const {
        if (!shortcut.valid()) return -1;
        for (size_t i = 0; i < m_actions.size(); ++i) {
            if (m_actions[i].id == exceptId) continue;
            if (m_actions[i].shortcut == shortcut) return static_cast<int>(i);
        }
        return -1;
    }

    bool bind(const std::string& id, const Shortcut& shortcut) {
        Action* action = find(id);
        if (action == nullptr) return false;
        action->shortcut = shortcut;
        return true;
    }

    bool resetToDefault(const std::string& id) {
        Action* action = find(id);
        if (action == nullptr) return false;
        action->shortcut = action->defaultShortcut;
        return true;
    }

    void resetAllToDefaults() {
        for (Action& action : m_actions) action.shortcut = action.defaultShortcut;
    }

    bool anyRebound() const {
        for (const Action& action : m_actions) {
            if (action.shortcut != action.defaultShortcut) return true;
        }
        return false;
    }

    /*
     * Save the bindings that differ from their defaults.
     *
     * Only the differences, so that changing a default in a later version
     * reaches everybody who never rebound that command - writing the whole
     * table would freeze today's defaults into every user's config file
     * forever.
     */
    bool saveBindings(const std::string& path) const {
        std::ofstream file(path);
        if (!file.is_open()) return false;

        file << "# ChiptuneTracker key bindings\n";
        file << "# Only commands you have rebound are listed here.\n";
        file << "# Safe to delete - the defaults come back.\n";
        for (const Action& action : m_actions) {
            if (action.shortcut == action.defaultShortcut) continue;
            file << action.id << "=" << shortcutText(action.shortcut) << "\n";
        }
        return file.good();
    }

    /*
     * Load bindings, tolerantly.
     *
     * An id this build does not have is skipped rather than treated as an
     * error: a config file shared between versions, or left behind by one,
     * must not cost somebody the rest of their bindings.
     */
    bool loadBindings(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            if (line.empty() || line[0] == '#') continue;

            const size_t equals = line.find('=');
            if (equals == std::string::npos) continue;

            const std::string id = line.substr(0, equals);
            const std::string value = line.substr(equals + 1);

            Action* action = find(id);
            if (action == nullptr) continue;

            // An empty value is a deliberately unbound command, which is
            // different from an unparseable one.
            if (value.empty()) { action->shortcut = Shortcut(); continue; }

            const Shortcut shortcut = shortcutFromText(value);
            if (shortcut.valid()) action->shortcut = shortcut;
        }
        return true;
    }

    // ---- Dispatch -----------------------------------------------------------

    /*
     * Run whichever action's key was just pressed. Returns its index, or -1.
     *
     * The only function here that touches ImGui, and the only one a test
     * cannot call without a context.
     *
     * Modifiers are matched exactly: an action bound to S must not fire on
     * Ctrl+S, or every unmodified letter binding would swallow its Ctrl
     * counterpart. And nothing fires while a text field has the keyboard,
     * or typing a channel name would trigger commands with every letter.
     */
    int dispatch(ActionContext& context) {
        if (!context.complete()) return -1;

        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) return -1;

        for (size_t i = 0; i < m_actions.size(); ++i) {
            const Action& action = m_actions[i];
            if (!action.shortcut.valid() || !action.run) continue;
            if (action.shortcut.ctrl != io.KeyCtrl) continue;
            if (action.shortcut.shift != io.KeyShift) continue;
            if (action.shortcut.alt != io.KeyAlt) continue;
            if (!ImGui::IsKeyPressed(action.shortcut.key, false)) continue;
            if (!action.isEnabled(context)) continue;

            // Copy the callback before running it: an action that
            // re-registers commands would otherwise reallocate the vector
            // out from under this loop.
            auto run = action.run;
            run(context);
            return static_cast<int>(i);
        }
        return -1;
    }

    /*
     * The keys currently pressed, as a Shortcut, for the rebinding UI.
     *
     * Returns invalid until an actual key - not a bare modifier - goes
     * down, so holding Ctrl while reaching for a letter does not bind Ctrl
     * on its own.
     */
    static Shortcut capturePressedShortcut() {
        ImGuiIO& io = ImGui::GetIO();
        for (const KeyName& entry : bindableKeys()) {
            if (!ImGui::IsKeyPressed(entry.key, false)) continue;
            Shortcut shortcut;
            shortcut.key = entry.key;
            shortcut.ctrl = io.KeyCtrl;
            shortcut.shift = io.KeyShift;
            shortcut.alt = io.KeyAlt;
            return shortcut;
        }
        return Shortcut();
    }

private:
    std::vector<Action> m_actions;
};

inline const char* KEYBINDINGS_FILENAME = "chiptune-keys.ini";

inline std::string keybindingsPath(const std::string& directory = std::string()) {
    if (directory.empty()) return KEYBINDINGS_FILENAME;
    return directory + "/" + KEYBINDINGS_FILENAME;
}

} // namespace ChiptuneTracker
