#pragma once

/*
 * ChiptuneTracker - Scales
 *
 * These tables and helpers used to live in the middle of UI.h, several
 * thousand lines below the piano roll that needed them - which is exactly
 * why the "Snap to Scale" checkbox did nothing. The flag was set, the
 * helpers existed, and nothing in the note-placement path could reach them.
 *
 * Out here they are ImGui-free and testable, and the piano roll can call
 * them.
 */

#include <cstdint>

namespace ChiptuneTracker {

// Semitone offsets from the root. -1 marks an unused slot in the shorter
// scales, so every row is the same width.
inline constexpr int SCALE_MAJOR[]            = {0, 2, 4, 5, 7, 9, 11};
inline constexpr int SCALE_MINOR[]            = {0, 2, 3, 5, 7, 8, 10};
inline constexpr int SCALE_DORIAN[]           = {0, 2, 3, 5, 7, 9, 10};
inline constexpr int SCALE_PHRYGIAN[]         = {0, 1, 3, 5, 7, 8, 10};
inline constexpr int SCALE_LYDIAN[]           = {0, 2, 4, 6, 7, 9, 11};
inline constexpr int SCALE_MIXOLYDIAN[]       = {0, 2, 4, 5, 7, 9, 10};
inline constexpr int SCALE_LOCRIAN[]          = {0, 1, 3, 5, 6, 8, 10};
inline constexpr int SCALE_HARMONIC_MINOR[]   = {0, 2, 3, 5, 7, 8, 11};
inline constexpr int SCALE_PENTATONIC_MAJOR[] = {0, 2, 4, 7, 9, -1, -1};
inline constexpr int SCALE_PENTATONIC_MINOR[] = {0, 3, 5, 7, 10, -1, -1};
inline constexpr int SCALE_BLUES[]            = {0, 3, 5, 6, 7, 10, -1};

inline constexpr int SCALE_COUNT = 11;

inline const char* scaleName(int scaleType) {
    switch (scaleType) {
        case 0:  return "Major";
        case 1:  return "Minor";
        case 2:  return "Dorian";
        case 3:  return "Phrygian";
        case 4:  return "Lydian";
        case 5:  return "Mixolydian";
        case 6:  return "Locrian";
        case 7:  return "Harmonic Minor";
        case 8:  return "Pentatonic Major";
        case 9:  return "Pentatonic Minor";
        case 10: return "Blues";
        default: return "Major";
    }
}

inline const char* noteName(int pitchClass) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F",
                                    "F#", "G", "G#", "A", "A#", "B"};
    const int index = ((pitchClass % 12) + 12) % 12;
    return names[index];
}

inline const int* scaleIntervals(int scaleType, int& sizeOut) {
    sizeOut = 7;
    switch (scaleType) {
        case 0:  return SCALE_MAJOR;
        case 1:  return SCALE_MINOR;
        case 2:  return SCALE_DORIAN;
        case 3:  return SCALE_PHRYGIAN;
        case 4:  return SCALE_LYDIAN;
        case 5:  return SCALE_MIXOLYDIAN;
        case 6:  return SCALE_LOCRIAN;
        case 7:  return SCALE_HARMONIC_MINOR;
        case 8:  sizeOut = 5; return SCALE_PENTATONIC_MAJOR;
        case 9:  sizeOut = 5; return SCALE_PENTATONIC_MINOR;
        case 10: sizeOut = 6; return SCALE_BLUES;
        default: return SCALE_MAJOR;
    }
}

inline bool isNoteInScale(int pitch, int scaleRoot, int scaleType) {
    int scaleSize = 7;
    const int* scale = scaleIntervals(scaleType, scaleSize);

    // +120 so a pitch below the root does not produce a negative modulus.
    const int noteInOctave = (pitch - scaleRoot + 120) % 12;
    for (int i = 0; i < scaleSize; ++i) {
        if (scale[i] == noteInOctave) return true;
    }
    return false;
}

// Move a pitch to the nearest note that belongs to the scale.
//
// Ties go upward, which matters for the blues scale where the flat fifth
// sits a semitone from two scale tones at once.
inline int snapToScale(int pitch, int scaleRoot, int scaleType) {
    if (isNoteInScale(pitch, scaleRoot, scaleType)) return pitch;

    for (int offset = 1; offset <= 6; ++offset) {
        if (isNoteInScale(pitch + offset, scaleRoot, scaleType)) return pitch + offset;
        if (isNoteInScale(pitch - offset, scaleRoot, scaleType)) return pitch - offset;
    }
    return pitch;   // no scale is this sparse, but never return garbage
}

} // namespace ChiptuneTracker
