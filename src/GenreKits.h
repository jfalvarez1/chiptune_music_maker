#pragma once

/*
 * ChiptuneTracker - Genre kits
 *
 * Every style has a handful of patterns that everything else is built on: a
 * dembow, a boom bap, four on the floor, an octave-pumping bassline. Writing
 * one out by hand is not craft, it is typing - and it is the tedious part
 * standing between someone and the music they actually wanted to make.
 *
 * So each one is a button. Pick a genre, press "Dembow", and the notes are
 * there.
 *
 * The rule this must not break: it writes ordinary notes into an ordinary
 * pattern. Nothing here is a locked-in generator object, nothing is
 * regenerated behind your back, and everything it makes can be dragged,
 * retuned, deleted or rewritten exactly like notes you placed yourself. It
 * is a faster way to type, not a different kind of music. Drawing the whole
 * thing by hand remains exactly as available as it was, and every existing
 * generator is untouched.
 *
 * Rhythms are sixteen-character strings, the same notation the templates
 * use, because that is how a drum pattern is actually discussed.
 *
 * Pure and ImGui-free, so every recipe is testable.
 */

#include "Types.h"
#include "Genres.h"

#include <cmath>
#include <cstring>

namespace ChiptuneTracker {

enum class KitCategory : uint8_t {
    Drums = 0,
    Bass,
    Chords,
    Melody,
    Count
};

inline const char* kitCategoryName(KitCategory category) {
    switch (category) {
        case KitCategory::Drums:  return "Drums";
        case KitCategory::Bass:   return "Bass";
        case KitCategory::Chords: return "Chords";
        case KitCategory::Melody: return "Melody";
        default:                  return "Other";
    }
}

inline constexpr int KIT_MAX_DEGREES = 8;
inline constexpr int KIT_STEPS_PER_BAR = 16;
inline constexpr float KIT_STEP_BEATS = 0.25f;

struct KitRecipe {
    const char* name;
    const char* description;
    KitCategory category;

    // Space-separated genre keys this suits. Empty means every genre, which
    // is how the handful of universal patterns avoid being listed eight times.
    const char* genres;

    // Drums: one bar per voice.
    const char* kick;
    const char* snare;
    const char* hat;

    // Everything else: one bar of rhythm, with degrees cycled across the
    // hits - or, for chords, one chord per bar taken from degrees.
    const char* rhythm;
    int8_t degrees[KIT_MAX_DEGREES];
    int degreeCount;
    int barsPerCycle;        // chords spread over several bars
    int octaveOffset;        // semitones from the key root
    bool triads;             // place a full minor triad on each hit
    float noteLengthSteps;
};

inline const KitRecipe* kitRecipes(int& countOut) {
    static const KitRecipe RECIPES[] = {
        // ---- Drums --------------------------------------------------------
        {"Four on the Floor",
         "A kick on every beat with hats between. The backbone of house, EDM "
         "and most synthwave.",
         KitCategory::Drums, "edm synthwave",
         "x...x...x...x...", "....x.......x...", "..x...x...x...x.",
         nullptr, {0}, 0, 1, 0, false, 1.0f},

        {"Dembow",
         "The reggaeton pattern. The snare's 3-3-2 grouping is what makes it "
         "sound like reggaeton and not a straight beat.",
         KitCategory::Drums, "reggaeton",
         "x...x...x...x...", "...x..x....x..x.", "x.x.x.x.x.x.x.x.",
         nullptr, {0}, 0, 1, 0, false, 1.0f},

        {"Boom Bap",
         "Kick on one and the offbeat, snare on two and four. Leave it slightly "
         "swung and it is most of hip hop.",
         KitCategory::Drums, "hiphop lofi",
         "x..x..x.....x...", "....x.......x...", "x.x.x.x.x.x.x.x.",
         nullptr, {0}, 0, 1, 0, false, 1.0f},

        {"Chip Beat",
         "Simple and square. Chip drums are usually noise bursts, so they work "
         "better plain than busy.",
         KitCategory::Drums, "chiptune",
         "x...x...x...x...", "....x.......x...", "x.x.x.x.x.x.x.x.",
         nullptr, {0}, 0, 1, 0, false, 1.0f},

        {"Rock Backbeat",
         "The snare on two and four is the whole idea; everything else can move.",
         KitCategory::Drums, "rock",
         "x.....x.x.......", "....x.......x...", "x.x.x.x.x.x.x.x.",
         nullptr, {0}, 0, 1, 0, false, 1.0f},

        {"Half-Time",
         "The snare moves to beat three. Same tempo, but everything feels half "
         "the speed - the usual trick for a chorus or a drop.",
         KitCategory::Drums, "hiphop lofi edm",
         "x.......x.......", "........x.......", "x.x.x.x.x.x.x.x.",
         nullptr, {0}, 0, 1, 0, false, 1.0f},

        {"Busy Hats",
         "Sixteenths with accents. Adds urgency without touching the kick or "
         "the snare.",
         KitCategory::Drums, "",
         "................", "................", "X.x.X.x.X.x.X.x.",
         nullptr, {0}, 0, 1, 0, false, 1.0f},

        {"Breakbeat",
         "Syncopated kick and a snare that lands early. Where drum and bass and "
         "big beat come from.",
         KitCategory::Drums, "edm",
         "x.....x...x.....", "....x.......x..x", "x.x.x.x.x.x.x.x.",
         nullptr, {0}, 0, 1, 0, false, 1.0f},

        // ---- Bass ---------------------------------------------------------
        {"Root Notes",
         "The root of the bar, twice. Never wrong, and the right place to "
         "start before trying anything cleverer.",
         KitCategory::Bass, "",
         nullptr, nullptr, nullptr,
         "x.......x.......", {0, 0}, 2, 1, -12, false, 3.5f},

        {"Octave Pump",
         "Root and octave alternating in eighths. Drives everything forward and "
         "sits under a four-on-the-floor kick perfectly.",
         KitCategory::Bass, "edm synthwave chiptune",
         nullptr, nullptr, nullptr,
         "x.x.x.x.x.x.x.x.", {0, 12}, 2, 1, -12, false, 1.5f},

        {"Offbeat Bass",
         "Bass only between the kicks. Leaves the low end clear where the kick "
         "lands, which is why it sounds bigger, not smaller.",
         KitCategory::Bass, "reggaeton edm",
         nullptr, nullptr, nullptr,
         "..x...x...x...x.", {0}, 1, 1, -12, false, 1.5f},

        {"Walking Eighths",
         "Root, fifth, octave and back. Movement without leaving the chord.",
         KitCategory::Bass, "rock lofi hiphop",
         nullptr, nullptr, nullptr,
         "x.x.x.x.x.x.x.x.", {0, 7, 12, 7}, 4, 1, -12, false, 1.5f},

        {"Long 808",
         "One note, held. The pitch does the work, so the space around it "
         "matters more than the rhythm.",
         KitCategory::Bass, "hiphop reggaeton",
         nullptr, nullptr, nullptr,
         "x...............", {0}, 1, 1, -12, false, 15.0f},

        // ---- Chords -------------------------------------------------------
        {"Minor Pop",
         "i - VI - III - VII. Probably the most used progression in popular "
         "music, and it works in every one of these genres.",
         KitCategory::Chords, "",
         nullptr, nullptr, nullptr,
         "x...............", {0, 8, 3, 10}, 4, 4, 0, true, 15.5f},

        {"Sad Loop",
         "i - VII - VI - VII. Circles rather than resolving, which is why it "
         "loops for hours without tiring.",
         KitCategory::Chords, "lofi synthwave hiphop",
         nullptr, nullptr, nullptr,
         "x...............", {0, 10, 8, 10}, 4, 4, 0, true, 15.5f},

        {"Andalusian",
         "i - VII - VI - V. The descending line under it is the oldest hook "
         "there is.",
         KitCategory::Chords, "rock chiptune synthwave",
         nullptr, nullptr, nullptr,
         "x...............", {0, 10, 8, 7}, 4, 4, 0, true, 15.5f},

        {"Stabs",
         "The same chords, played short and off the beat. Turns a pad into a "
         "rhythm part.",
         KitCategory::Chords, "reggaeton edm hiphop",
         nullptr, nullptr, nullptr,
         "..x...x...x...x.", {0, 0, 8, 8}, 4, 4, 0, true, 1.0f},

        // ---- Melody -------------------------------------------------------
        {"Triad Arp",
         "Up and back down the chord in eighths. On three channels this is how "
         "chiptune implies harmony it cannot play.",
         KitCategory::Melody, "chiptune edm synthwave",
         nullptr, nullptr, nullptr,
         "x.x.x.x.x.x.x.x.", {0, 3, 7, 12, 7, 3, 0, 3}, 8, 1, 12, false, 1.5f},

        {"Octave Ping",
         "Root and octave on the offbeats. Sparse enough to sit over anything.",
         KitCategory::Melody, "chiptune",
         nullptr, nullptr, nullptr,
         "..x...x...x...x.", {0, 12}, 2, 1, 12, false, 1.5f},

        {"Call and Answer",
         "A phrase in the first half of the bar and a reply in the second. The "
         "shape most melodies actually have.",
         KitCategory::Melody, "",
         nullptr, nullptr, nullptr,
         "x.x.x.......x.x.", {0, 3, 7, 3, 0}, 5, 1, 12, false, 1.5f},
    };

    countOut = static_cast<int>(sizeof(RECIPES) / sizeof(RECIPES[0]));
    return RECIPES;
}

// Whether a recipe belongs to a genre. An empty genre list means every one.
inline bool recipeSuitsGenre(const KitRecipe& recipe, Genre genre) {
    if (recipe.genres == nullptr || recipe.genres[0] == '\0') return true;
    if (genre == Genre::Everything) return true;

    const char* key = genreKey(genre);
    const size_t keyLength = std::strlen(key);

    // Whole-word match, so "edm" never matches inside a longer key.
    const char* cursor = recipe.genres;
    while (*cursor != '\0') {
        while (*cursor == ' ') ++cursor;
        const char* wordStart = cursor;
        while (*cursor != '\0' && *cursor != ' ') ++cursor;

        const size_t wordLength = static_cast<size_t>(cursor - wordStart);
        if (wordLength == keyLength &&
            std::strncmp(wordStart, key, keyLength) == 0) {
            return true;
        }
    }
    return false;
}

inline int countRecipesForGenre(Genre genre, KitCategory category) {
    int count = 0;
    int total = 0;
    const KitRecipe* recipes = kitRecipes(total);
    for (int i = 0; i < total; ++i) {
        if (recipes[i].category == category && recipeSuitsGenre(recipes[i], genre)) {
            ++count;
        }
    }
    return count;
}

// Which voices a drum recipe should use. Passed in rather than baked into the
// recipe so the same rhythm can be played by any kit.
struct KitVoices {
    OscillatorType kick = OscillatorType::Kick808;
    OscillatorType snare = OscillatorType::Snare;
    OscillatorType hat = OscillatorType::HiHat;
    OscillatorType pitched = OscillatorType::Pulse;
};

namespace detail {

inline void emitStepLine(Pattern& pattern, const char* steps, int pitch,
                         OscillatorType voice, float barStart, float velocity,
                         float lengthSteps, int& added) {
    if (steps == nullptr) return;

    for (int i = 0; i < KIT_STEPS_PER_BAR && steps[i] != '\0'; ++i) {
        const char step = steps[i];
        if (step != 'x' && step != 'X') continue;
        if (static_cast<int>(pattern.notes.size()) >= Pattern::MAX_NOTES) return;
        if (pitch < 0 || pitch > 127) continue;

        Note note;
        note.pitch = pitch;
        note.startTime = barStart + static_cast<float>(i) * KIT_STEP_BEATS;
        note.duration = lengthSteps * KIT_STEP_BEATS;
        note.oscillatorType = voice;
        note.velocity = (step == 'X') ? 1.0f : velocity;
        pattern.notes.push_back(note);
        ++added;
    }
}

} // namespace detail

/*
 * Write a recipe into a pattern, starting at a beat.
 *
 * `bars` is how many bars to fill; a recipe shorter than that repeats, and a
 * chord progression longer than that is truncated. Returns the number of
 * notes added, so the caller can tell the user nothing happened rather than
 * leaving them wondering.
 *
 * Existing notes are left alone. Layering a bassline under a melody is
 * ordinary; silently deleting someone's work to make room is not.
 */
inline int applyKitRecipe(Pattern& pattern, const KitRecipe& recipe,
                          float startBeat, int bars, int keyRoot,
                          const KitVoices& voices) {
    if (bars <= 0) return 0;
    if (keyRoot < 0 || keyRoot > 127) return 0;
    if (!std::isfinite(startBeat) || startBeat < 0.0f) return 0;

    int added = 0;
    const float barBeats = KIT_STEPS_PER_BAR * KIT_STEP_BEATS;

    if (recipe.category == KitCategory::Drums) {
        for (int bar = 0; bar < bars; ++bar) {
            const float barStart = startBeat + static_cast<float>(bar) * barBeats;
            detail::emitStepLine(pattern, recipe.kick, 36, voices.kick,
                                 barStart, 1.0f, 1.0f, added);
            detail::emitStepLine(pattern, recipe.snare, 38, voices.snare,
                                 barStart, 0.9f, 1.0f, added);
            detail::emitStepLine(pattern, recipe.hat, 42, voices.hat,
                                 barStart, 0.45f, 1.0f, added);
        }
        return added;
    }

    if (recipe.rhythm == nullptr || recipe.degreeCount <= 0) return 0;

    for (int bar = 0; bar < bars; ++bar) {
        const float barStart = startBeat + static_cast<float>(bar) * barBeats;

        if (recipe.category == KitCategory::Chords) {
            // One chord per bar, cycling through the progression.
            const int cycle = (recipe.barsPerCycle > 0) ? recipe.barsPerCycle : 1;
            const int degree = recipe.degrees[bar % cycle % recipe.degreeCount];
            const int root = keyRoot + recipe.octaveOffset + degree;

            const int voicing[] = {0, 3, 7};
            for (int interval : voicing) {
                detail::emitStepLine(pattern, recipe.rhythm, root + interval,
                                     voices.pitched, barStart, 0.55f,
                                     recipe.noteLengthSteps, added);
                if (!recipe.triads) break;
            }
            continue;
        }

        // Bass and melody: cycle the degrees across the hits of the bar.
        int hit = 0;
        for (int i = 0; i < KIT_STEPS_PER_BAR && recipe.rhythm[i] != '\0'; ++i) {
            const char step = recipe.rhythm[i];
            if (step != 'x' && step != 'X') continue;
            if (static_cast<int>(pattern.notes.size()) >= Pattern::MAX_NOTES) break;

            const int degree = recipe.degrees[hit % recipe.degreeCount];
            const int pitch = keyRoot + recipe.octaveOffset + degree;
            ++hit;
            if (pitch < 0 || pitch > 127) continue;

            Note note;
            note.pitch = pitch;
            note.startTime = barStart + static_cast<float>(i) * KIT_STEP_BEATS;
            note.duration = recipe.noteLengthSteps * KIT_STEP_BEATS;
            note.oscillatorType = voices.pitched;
            note.velocity = (step == 'X') ? 1.0f : 0.8f;
            pattern.notes.push_back(note);
            ++added;
        }
    }

    return added;
}

} // namespace ChiptuneTracker
