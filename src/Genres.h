#pragma once

/*
 * ChiptuneTracker - Genre focus
 *
 * The Sound Palette carries eight chord sets and seven drum categories, and
 * the workspace carries a panel for every feature we have ever shipped. If
 * you are writing chiptune, the jazz voicings and the reggaeton percussion
 * are not options - they are things to scroll past. If you are writing
 * reggaeton, the wavetable editor is.
 *
 * A genre says what to put in front of you. It never removes anything: the
 * View menu still lists every panel, and the palette keeps a "show
 * everything" switch that says how much is being held back. This is a filter
 * on attention, not on capability.
 *
 * Two deliberate limits:
 *
 *   - The default is Everything, which behaves exactly as the program did
 *     before. Nobody has a genre imposed on them.
 *   - Choosing a genre never touches the music. Tempo, swing and key are
 *     offered as a separate, explicit action, because silently retuning
 *     someone's project is not a layout change.
 *
 * Pure data and queries, no ImGui, so the profiles are testable.
 */

#include <cstdint>
#include <cstring>

namespace ChiptuneTracker {

enum class Genre : uint8_t {
    Everything = 0,
    Chiptune,
    Synthwave,
    HipHop,
    Reggaeton,
    EDM,
    Rock,
    Lofi,
    Count
};

// Fixed-size lists with a null terminator, so a profile is a plain literal
// and needs no allocation or ordering pass at startup.
inline constexpr int GENRE_MAX_CHORDS = 8;
inline constexpr int GENRE_MAX_DRUMS = 8;
inline constexpr int GENRE_MAX_TOOLS = 12;

struct GenreProfile {
    const char* name;
    const char* blurb;

    // Suggestions, applied only when the user explicitly asks for them.
    float bpm;
    float swing;
    int scaleRoot;      // 0 = C
    int scaleType;      // indexes Scales.h

    // Palette sections worth showing. An empty first entry means "all of
    // them", which is how Everything is expressed without a special case.
    const char* chords[GENRE_MAX_CHORDS];
    const char* drums[GENRE_MAX_DRUMS];
    const char* tools[GENRE_MAX_TOOLS];

    // Panels to open when this genre is chosen. The user can still toggle
    // any of them afterwards; this only decides the starting point.
    bool macroEditor;
    bool wavetableEditor;
    bool spectrumAnalyzer;
    bool automation;
};

// Everything a focus can hold back, listed once. A profile naming
// something not in these lists is a typo that would silently hide a section
// forever, so the tests check every profile against them.
inline constexpr const char* ALL_CHORD_SETS[] = {
    "Pop", "Jazz", "Rock", "EDM", "HipHop", "Reggaeton", "Synthwave", "Chiptune"
};
inline constexpr int ALL_CHORD_SET_COUNT = 8;

inline constexpr const char* ALL_DRUM_SETS[] = {
    "Kicks", "Snares & Claps", "Hi-Hats", "Toms", "Cymbals", "Percussion",
    "Reggaeton Drums"
};
inline constexpr int ALL_DRUM_SET_COUNT = 7;

inline constexpr const char* ALL_TOOL_SECTIONS[] = {
    "Euclidean Rhythms", "Drum Pattern Generator", "Arpeggiator",
    "Bass Generator", "Scale Lock", "Velocity Curve", "Fill Generator",
    "Pattern Variation", "Quick Layer", "Humanize"
};
inline constexpr int ALL_TOOL_SECTION_COUNT = 10;

inline const GenreProfile& genreProfile(Genre genre) {
    static const GenreProfile PROFILES[] = {
        {   // Everything
            "Everything",
            "Every tool on screen. This is how the program has always behaved.",
            120.0f, 0.0f, 0, 0,
            {nullptr}, {nullptr}, {nullptr},
            false, false, false, false
        },
        {   // Chiptune
            "Chiptune",
            "Pulse, triangle and noise. Macros and wavetables do the work that "
            "a sampler would do elsewhere.",
            150.0f, 0.0f, 9, 1,          // A minor
            {"Chiptune", "Pop", "Rock", nullptr},
            {"Kicks", "Snares & Claps", "Hi-Hats", nullptr},
            {"Arpeggiator", "Euclidean Rhythms", "Drum Pattern Generator",
             "Bass Generator", "Scale Lock", "Pattern Variation", nullptr},
            true, true, false, false
        },
        {   // Synthwave
            "Synthwave",
            "Wide pads, gated drums and a lot of automation.",
            110.0f, 0.0f, 9, 1,          // A minor
            {"Synthwave", "Pop", "EDM", nullptr},
            {"Kicks", "Snares & Claps", "Hi-Hats", "Toms", "Cymbals", nullptr},
            {"Arpeggiator", "Bass Generator", "Scale Lock", "Quick Layer",
             "Velocity Curve", "Drum Pattern Generator", nullptr},
            false, true, true, true
        },
        {   // Hip Hop
            "Hip Hop",
            "Swung drums, jazz voicings, and room for the low end.",
            90.0f, 0.15f, 0, 1,          // C minor
            {"HipHop", "Jazz", "Pop", nullptr},
            {"Kicks", "Snares & Claps", "Hi-Hats", "Percussion", nullptr},
            {"Humanize", "Drum Pattern Generator", "Velocity Curve",
             "Fill Generator", "Scale Lock", nullptr},
            false, false, true, true
        },
        {   // Reggaeton
            "Reggaeton",
            "Dembow. The percussion set matters more than the chord set.",
            95.0f, 0.0f, 9, 1,           // A minor
            {"Reggaeton", "Pop", "HipHop", nullptr},
            {"Kicks", "Snares & Claps", "Hi-Hats", "Reggaeton Drums",
             "Percussion", nullptr},
            {"Drum Pattern Generator", "Euclidean Rhythms", "Fill Generator",
             "Velocity Curve", "Scale Lock", nullptr},
            false, false, true, true
        },
        {   // EDM
            "EDM",
            "Four on the floor, big supersaws, automation on everything.",
            128.0f, 0.0f, 9, 1,          // A minor
            {"EDM", "Pop", "Synthwave", nullptr},
            {"Kicks", "Snares & Claps", "Hi-Hats", "Cymbals", "Percussion", nullptr},
            {"Arpeggiator", "Euclidean Rhythms", "Drum Pattern Generator",
             "Fill Generator", "Quick Layer", "Velocity Curve", nullptr},
            false, true, true, true
        },
        {   // Rock
            "Rock",
            "Power chords and a full kit.",
            120.0f, 0.0f, 4, 1,          // E minor
            {"Rock", "Pop", nullptr},
            {"Kicks", "Snares & Claps", "Hi-Hats", "Toms", "Cymbals", nullptr},
            {"Drum Pattern Generator", "Fill Generator", "Humanize",
             "Bass Generator", "Scale Lock", nullptr},
            false, false, false, false
        },
        {   // Lofi
            "Lofi",
            "Slow, swung, and deliberately imperfect.",
            75.0f, 0.20f, 0, 1,          // C minor
            {"Jazz", "HipHop", "Pop", nullptr},
            {"Kicks", "Snares & Claps", "Hi-Hats", "Percussion", nullptr},
            {"Humanize", "Velocity Curve", "Drum Pattern Generator",
             "Pattern Variation", "Scale Lock", nullptr},
            false, false, false, true
        },
    };

    static_assert(sizeof(PROFILES) / sizeof(PROFILES[0]) ==
                  static_cast<size_t>(Genre::Count),
                  "every genre needs a profile");

    const int index = static_cast<int>(genre);
    if (index < 0 || index >= static_cast<int>(Genre::Count)) {
        return PROFILES[0];
    }
    return PROFILES[index];
}

inline const char* genreName(Genre genre) {
    return genreProfile(genre).name;
}

// An empty list means "everything", which is how Everything avoids being a
// special case at every call site.
inline bool listAllowsAll(const char* const* list) {
    return list[0] == nullptr;
}

inline bool listContains(const char* const* list, int capacity, const char* key) {
    if (key == nullptr) return false;
    if (listAllowsAll(list)) return true;

    for (int i = 0; i < capacity && list[i] != nullptr; ++i) {
        if (std::strcmp(list[i], key) == 0) return true;
    }
    return false;
}

inline bool genreShowsChordSet(Genre genre, const char* key) {
    return listContains(genreProfile(genre).chords, GENRE_MAX_CHORDS, key);
}

inline bool genreShowsDrumCategory(Genre genre, const char* key) {
    return listContains(genreProfile(genre).drums, GENRE_MAX_DRUMS, key);
}

// Generators in the Tools panel. Chiptune wants the arpeggiator and no
// humanising; hip hop wants the reverse.
inline bool genreShowsTool(Genre genre, const char* key) {
    return listContains(genreProfile(genre).tools, GENRE_MAX_TOOLS, key);
}

// How many palette sections a genre is holding back, so the UI can say so
// rather than leaving the user to wonder where everything went.
inline int genreHiddenToolCount(Genre genre, const char* const* allTools,
                                int toolCount) {
    int hidden = 0;
    for (int i = 0; i < toolCount; ++i) {
        if (!genreShowsTool(genre, allTools[i])) ++hidden;
    }
    return hidden;
}

inline int genreHiddenSectionCount(Genre genre,
                                   const char* const* allChords, int chordCount,
                                   const char* const* allDrums, int drumCount) {
    int hidden = 0;
    for (int i = 0; i < chordCount; ++i) {
        if (!genreShowsChordSet(genre, allChords[i])) ++hidden;
    }
    for (int i = 0; i < drumCount; ++i) {
        if (!genreShowsDrumCategory(genre, allDrums[i])) ++hidden;
    }
    return hidden;
}

} // namespace ChiptuneTracker
