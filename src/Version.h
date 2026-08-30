#pragma once

/*
 * ChiptuneTracker - Version
 *
 * One place. The window title, the About dialog, the project file header
 * and the release notes all read from here, so a release cannot ship with
 * three different version numbers in three different corners of the app.
 */

#include <string>

namespace ChiptuneTracker {

inline constexpr int VERSION_MAJOR = 3;
inline constexpr int VERSION_MINOR = 1;
inline constexpr int VERSION_PATCH = 0;

inline constexpr const char* VERSION_STRING = "3.1.0";
inline constexpr const char* VERSION_NAME = "Dockyard";
inline constexpr const char* APP_NAME = "ChiptuneTracker";

inline std::string windowTitle() {
    return std::string(APP_NAME) + " " + VERSION_STRING;
}

inline std::string aboutText() {
    return std::string(APP_NAME) + " " + VERSION_STRING + " \"" + VERSION_NAME + "\"\n\n"
           "A chiptune tracker and DAW.\n\n"
           "Piano roll, tracker and arrangement views, 65 instruments,\n"
           "instrument macros, a full per-channel effects chain, a\n"
           "mastering bus, and export to WAV, MP3 and MIDI.\n\n"
           "Keyboard:\n"
           "  Space       play / pause\n"
           "  F4          instrument macros\n"
           "  F12         screenshot\n"
           "  Ctrl+0      reset window layout\n"
           "  [ and ]     octave down / up\n"
           "  Z S X D C.. play notes";
}

} // namespace ChiptuneTracker
