#pragma once

/*
 * ChiptuneTracker - User settings
 *
 * Preferences that outlive a session and are not part of any project: which
 * genre you work in, and whether we have already asked. A project belongs to
 * a song; these belong to the person.
 *
 * Deliberately its own tiny file rather than a corner of imgui.ini. Deleting
 * imgui.ini is the standard fix for a broken layout, and it should not also
 * make the program forget who it is talking to and start interrogating them
 * again.
 *
 * Every read is tolerant: a missing file, an unknown key, a truncated line
 * and a value from a newer version all leave the defaults in place. Settings
 * are a convenience, and no arrangement of this file may stop the program
 * starting.
 */

#include "Genres.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace ChiptuneTracker {

struct UserSettings {
    // Whether the welcome has been shown. Separate from the genre, because
    // choosing "Everything" is a real answer and must not read as "never
    // asked" - otherwise the one person who wants every tool gets the
    // dialog on every launch forever.
    bool welcomed = false;

    Genre genre = Genre::Everything;

    // Whether the welcome offered to set tempo and key, and was taken up on
    // it. Remembered so the answer can be defaulted next time.
    bool applyGenreDefaults = false;
};

inline const char* SETTINGS_FILENAME = "chiptune-settings.ini";

inline std::string settingsPath(const std::string& directory = std::string()) {
    if (directory.empty()) return SETTINGS_FILENAME;
    return directory + "/" + SETTINGS_FILENAME;
}

inline bool loadSettings(UserSettings& settings, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        // Tolerate CRLF written by another tool.
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;

        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        if (key == "welcomed") {
            settings.welcomed = (value == "1" || value == "true");
        } else if (key == "genre") {
            settings.genre = genreFromKey(value.c_str());
        } else if (key == "applyGenreDefaults") {
            settings.applyGenreDefaults = (value == "1" || value == "true");
        }
        // Anything else is from a newer version; leaving it alone is the
        // point of a tolerant reader.
    }
    return true;
}

inline bool saveSettings(const UserSettings& settings, const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "# ChiptuneTracker user settings\n";
    file << "# Safe to delete - the program will simply ask again.\n";
    file << "welcomed=" << (settings.welcomed ? 1 : 0) << "\n";
    file << "genre=" << genreKey(settings.genre) << "\n";
    file << "applyGenreDefaults=" << (settings.applyGenreDefaults ? 1 : 0) << "\n";
    return file.good();
}

} // namespace ChiptuneTracker
