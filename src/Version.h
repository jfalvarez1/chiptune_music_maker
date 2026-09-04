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
inline constexpr int VERSION_MINOR = 11;
inline constexpr int VERSION_PATCH = 0;

// Composed, never written by hand. This was a second hand-written literal
// once, and it sat at 3.4.1 while two releases shipped around it - the
// window title was the only place anyone could see the lie.
inline const std::string VERSION_STRING =
    std::to_string(VERSION_MAJOR) + "." +
    std::to_string(VERSION_MINOR) + "." +
    std::to_string(VERSION_PATCH);
inline constexpr const char* VERSION_NAME = "Rack";
inline constexpr const char* APP_NAME = "ChiptuneTracker";

inline std::string windowTitle() {
    return std::string(APP_NAME) + " " + VERSION_STRING;
}

inline std::string aboutText() {
    return std::string(APP_NAME) + " " + VERSION_STRING + " \"" + VERSION_NAME + "\"\n\n"
           "A chiptune tracker and DAW.\n\n"
           "Piano roll, tracker and arrangement views.\n\n"
           "Wavetable, FM, sampler, granular and modelled-drum engines,\n"
           "a modulation matrix, instrument macros, audio clips on the\n"
           "timeline, tempo and meter changes, voice-to-notes, a full\n"
           "per-channel effects rack, a mastering bus, and export to\n"
           "WAV, MP3 and MIDI.\n\n"
           "Keyboard:\n"
           "  Space       play / pause\n"
           "  F4          instrument macros\n"
           "  F12         screenshot\n"
           "  Ctrl+0      reset window layout\n"
           "  [ and ]     octave down / up\n"
           "  Z S X D C.. play notes";
}

} // namespace ChiptuneTracker
