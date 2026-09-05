#pragma once

/*
 * ChiptuneTracker - Pattern generators
 *
 * Algorithmic starting points, not autopilot. Each of these produces notes
 * you then edit; none of them tries to write the song.
 *
 * Deliberately free of ImGui and of Project mutation - each function takes
 * parameters and returns notes. That keeps them testable headlessly, which
 * the Tools panel's nine existing generators are not, since their logic
 * lives inside UI.h behind ImGui calls.
 */

#include "Types.h"

#include "Scales.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>

namespace ChiptuneTracker {
namespace generators {

// ============================================================================
// Euclidean rhythms
//
// Bjorklund's algorithm distributes k onsets as evenly as possible over n
// steps. That single idea produces a startling number of the world's actual
// drum patterns - E(3,8) is the tresillo, E(5,8) the cinquillo, E(2,5) a
// khafif-e-ramal - which is why it is the fastest way from nothing to a
// rhythm worth keeping.
//
// Returns a bool per step: true where a hit falls.
// ============================================================================
inline std::vector<bool> euclideanPattern(int steps, int pulses, int rotation = 0) {
    std::vector<bool> pattern;
    if (steps <= 0) return pattern;

    pattern.assign(static_cast<size_t>(steps), false);
    pulses = std::clamp(pulses, 0, steps);
    if (pulses == 0) return pattern;
    if (pulses == steps) {
        pattern.assign(static_cast<size_t>(steps), true);
        return pattern;
    }

    // Bresenham formulation: equivalent to Bjorklund's recursion and far
    // easier to read. Each step asks whether the accumulated error has
    // crossed another whole pulse.
    int bucket = 0;
    for (int i = 0; i < steps; ++i) {
        bucket += pulses;
        if (bucket >= steps) {
            bucket -= steps;
            pattern[static_cast<size_t>(i)] = true;
        }
    }

    if (rotation != 0) {
        std::vector<bool> rotated(static_cast<size_t>(steps), false);
        for (int i = 0; i < steps; ++i) {
            // Positive rotation moves the pattern later in the bar
            int from = ((i - rotation) % steps + steps) % steps;
            rotated[static_cast<size_t>(i)] = pattern[static_cast<size_t>(from)];
        }
        pattern.swap(rotated);
    }

    return pattern;
}

// One Euclidean line turned into notes.
struct EuclideanVoice {
    OscillatorType instrument = OscillatorType::Kick;
    int pitch = 36;
    int steps = 16;
    int pulses = 4;
    int rotation = 0;
    float velocity = 1.0f;
    float noteLength = 0.25f;   // beats
};

// Lays the pattern across `lengthBeats`, so a 16-step figure over 4 beats
// puts a step on every sixteenth.
inline std::vector<Note> generateEuclidean(const EuclideanVoice& voice,
                                           float lengthBeats = 4.0f) {
    std::vector<Note> notes;
    if (voice.steps <= 0 || lengthBeats <= 0.0f) return notes;

    const std::vector<bool> hits = euclideanPattern(voice.steps, voice.pulses, voice.rotation);
    const float stepBeats = lengthBeats / static_cast<float>(voice.steps);

    for (int i = 0; i < voice.steps; ++i) {
        if (!hits[static_cast<size_t>(i)]) continue;

        Note note;
        note.pitch = std::clamp(voice.pitch, 0, 127);
        note.startTime = static_cast<float>(i) * stepBeats;
        note.duration = std::max(0.03125f, std::min(voice.noteLength, stepBeats));
        note.velocity = std::clamp(voice.velocity, 0.0f, 1.0f);
        note.oscillatorType = voice.instrument;
        notes.push_back(note);
    }

    return notes;
}

// A whole kit at once. The defaults are a serviceable four-on-the-floor with
// an offbeat hat and a backbeat snare - somewhere to start rather than
// something to keep.
inline std::vector<Note> generateEuclideanKit(int steps = 16,
                                              int kickPulses = 4,
                                              int snarePulses = 2,
                                              int hatPulses = 11,
                                              float lengthBeats = 4.0f) {
    std::vector<Note> notes;

    EuclideanVoice kick;
    kick.instrument = OscillatorType::Kick808;
    kick.pitch = 36;
    kick.steps = steps;
    kick.pulses = kickPulses;
    kick.velocity = 1.0f;
    kick.noteLength = 0.25f;

    EuclideanVoice snare;
    snare.instrument = OscillatorType::Snare;
    snare.pitch = 38;
    snare.steps = steps;
    snare.pulses = snarePulses;
    snare.rotation = steps / 4;    // land off the kick, on the backbeat
    snare.velocity = 0.9f;
    snare.noteLength = 0.25f;

    EuclideanVoice hat;
    hat.instrument = OscillatorType::HiHat;
    hat.pitch = 42;
    hat.steps = steps;
    hat.pulses = hatPulses;
    hat.velocity = 0.55f;
    hat.noteLength = 0.125f;

    for (const EuclideanVoice& voice : {kick, snare, hat}) {
        const std::vector<Note> line = generateEuclidean(voice, lengthBeats);
        notes.insert(notes.end(), line.begin(), line.end());
    }

    // Sorted by time so the result reads sensibly in the tracker view and
    // the sequencer sees events in order.
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return a.startTime < b.startTime;
    });

    return notes;
}

// Named rhythms worth having one click away, all of them Euclidean.
struct EuclideanPreset {
    const char* name;
    const char* description;
    int steps;
    int pulses;
    int rotation;
};

inline const std::vector<EuclideanPreset>& euclideanPresets() {
    static const std::vector<EuclideanPreset> presets = {
        {"Tresillo",    "E(3,8) - the Cuban 3+3+2, and half of pop music", 8,  3, 0},
        {"Cinquillo",   "E(5,8) - its busier relative",                    8,  5, 0},
        {"Four on floor", "E(4,16) - a hit on every beat",                16,  4, 0},
        {"Offbeat",     "E(4,16) rotated - lands between the beats",      16,  4, 2},
        {"Khafif",      "E(2,5) - a five-step Persian figure",             5,  2, 0},
        {"Ruchenitza",  "E(4,7) - Bulgarian seven",                        7,  4, 0},
        {"Agbadza",     "E(5,12) - West African bell pattern",            12,  5, 0},
        {"Bossa",       "E(5,16) - the bossa nova clave",                 16,  5, 0},
        {"Sixteenths",  "E(11,16) - dense, good for hats",                16, 11, 0},
    };
    return presets;
}

// ============================================================================
// Chord progressions
//
// Progressions existed here already, in two places, and both were the same
// mistake: a hardcoded list of degrees inside a genre kit or a starter
// template, with every chord built as a fixed {0, 3, 7} minor triad. So the
// "i - VI - III - VII" a recipe described came out with a minor chord on
// every degree, including the ones that are major in the key it names.
//
// The fix is not more tables. It is deriving the chord from the scale: the
// triad on a degree is that degree plus the two scale steps above it, and
// whether that comes out major, minor or diminished falls out of where in
// the scale it sits. Which is also the only way "I - V - vi - IV" can mean
// what it says.
// ============================================================================

/*
 * A pitch some number of scale steps above another.
 *
 * The same walk NoteFX.h does for its diatonic chord, and for the same
 * reason: the third above C in C major is four semitones and the third above
 * D is three, so a fixed interval cannot express a diatonic chord.
 */
inline int scaleStepsAbove(int pitch, int steps, int root, int scaleType) {
    int listed = 7;
    const int* intervals = scaleIntervals(scaleType, listed);

    int size = 0;
    while (size < listed && intervals[size] >= 0) ++size;
    if (size <= 0) return pitch;

    const int snapped = snapToScale(pitch, root, scaleType);
    const int pitchClass = ((snapped - root) % 12 + 12) % 12;

    int index = 0;
    for (int i = 0; i < size; ++i) {
        if (intervals[i] == pitchClass) { index = i; break; }
    }

    const int target = index + steps;
    int octave = target / size;
    int within = target % size;
    if (within < 0) { within += size; --octave; }

    return snapped - intervals[index] + intervals[within] + 12 * octave;
}

struct ProgressionVoice {
    // Degrees of the scale, one-based the way musicians write them: 1 is the
    // tonic, 5 is the dominant. Not zero-based, because every source anybody
    // would copy a progression from writes it this way and translating in
    // your head is where mistakes come from.
    std::vector<int> degrees{1, 5, 6, 4};

    int root = 0;            // 0 = C
    int scaleType = 0;       // an index into Scales.h
    int octave = 4;          // which octave the tonic sits in
    int voices = 3;          // 3 = triad, 4 = seventh
    bool spread = false;     // drop the root an octave, for a wider voicing
    float velocity = 0.7f;
};

/*
 * Write a progression out as notes, one chord per bar.
 *
 * One chord per bar because that is what a progression is - the thing the
 * song sits on rather than the rhythm it has. Anything more specific is an
 * arrangement, and arranging is what the pattern editor is for.
 */
inline std::vector<Note> generateProgression(const ProgressionVoice& voice,
                                             float barBeats = 4.0f) {
    std::vector<Note> notes;
    if (voice.degrees.empty() || barBeats <= 0.0f) return notes;

    const int root = std::clamp(voice.root, 0, 11);
    const int scaleType = std::clamp(voice.scaleType, 0, SCALE_COUNT - 1);
    const int voiceCount = std::clamp(voice.voices, 2, 4);
    const int tonic = std::clamp(12 * (voice.octave + 1) + root, 0, 127);

    for (size_t bar = 0; bar < voice.degrees.size(); ++bar) {
        // One-based in, zero-based here. A degree of zero or a negative one
        // is a typo rather than an instruction, so it becomes the tonic
        // rather than walking backwards out of the scale.
        const int degree = std::max(1, voice.degrees[bar]) - 1;
        const int chordRoot = scaleStepsAbove(tonic, degree, root, scaleType);

        for (int v = 0; v < voiceCount; ++v) {
            // Thirds stacked: the chord tone, two steps up, four steps up,
            // six for a seventh. Which of those are major and which minor
            // falls out of the scale rather than being chosen.
            int pitch = scaleStepsAbove(chordRoot, v * 2, root, scaleType);

            // A spread voicing drops the root an octave, which is what stops
            // a stack of close triads sounding like a church organ.
            if (voice.spread && v == 0) pitch -= 12;

            if (pitch < 0 || pitch > 127) continue;

            Note note;
            note.pitch = pitch;
            note.startTime = static_cast<float>(bar) * barBeats;
            note.duration = barBeats;
            note.velocity = std::clamp(voice.velocity, 0.05f, 1.0f);
            notes.push_back(note);
        }
    }

    return notes;
}

/*
 * Parse a progression written the way people write them.
 *
 * "1 5 6 4" and "I V vi IV" both work, and so does "1-5-6-4". Roman numerals
 * are accepted because that is how progressions are published, and rejecting
 * them would mean everybody retypes every progression they find - but the
 * CASE is deliberately ignored. Upper and lower case in roman numerals mean
 * major and minor, and here the scale decides that; honouring the case would
 * let somebody write a major chord on a degree where the key has a minor one
 * and get something that is not in the key at all.
 */
inline bool progressionFromText(const std::string& text,
                                std::vector<int>& degreesOut) {
    degreesOut.clear();

    /*
     * A degree ends at the first separator, and everything between is the
     * chord's quality.
     *
     * That rule is the whole parser, and it is there because of one case:
     * "ii7" and "V7". Reading left to right and taking any digit as a degree
     * turns those into two chords each - the ii and then a vii - which is a
     * progression nobody typed. A digit immediately after a degree is a
     * seventh, not a new chord; a digit after a space is a new chord.
     *
     * The quality itself is dropped rather than honoured. "V7" becomes the
     * chord on the fifth degree, and whether it comes out as a seventh is
     * decided by the voice count, which is a control the user can see.
     * Silently making one chord a seventh because of a character in a text
     * box would be a setting with no widget.
     */
    auto isSeparator = [](char c) {
        return !(std::isalnum(static_cast<unsigned char>(c)));
    };

    size_t at = 0;
    while (at < text.size() && degreesOut.size() < 16) {
        if (isSeparator(text[at])) { ++at; continue; }

        // One token: everything up to the next separator.
        size_t end = at;
        while (end < text.size() && !isSeparator(text[end])) ++end;

        std::string token = text.substr(at, end - at);
        at = end;

        // A leading run of I and V is a roman numeral; a leading digit is a
        // degree. Either way the rest of the token is the quality.
        std::string head;
        for (char c : token) {
            const char upper =
                static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (upper == 'I' || upper == 'V') {
                head += upper;
            } else {
                break;
            }
        }

        int degree = 0;
        if (!head.empty()) {
            // Case is deliberately ignored. Upper and lower case in roman
            // numerals mean major and minor, and here the SCALE decides
            // that. Honouring the case would let somebody write a major
            // chord on a degree the key has a minor one on, and get a chord
            // that is not in the key at all - which is the bug this whole
            // generator exists to stop.
            if (head == "I") degree = 1;
            else if (head == "II") degree = 2;
            else if (head == "III") degree = 3;
            else if (head == "IV") degree = 4;
            else if (head == "V") degree = 5;
            else if (head == "VI") degree = 6;
            else if (head == "VII") degree = 7;
        } else if (!token.empty() && token[0] >= '1' && token[0] <= '7') {
            degree = token[0] - '0';
        }

        if (degree > 0) degreesOut.push_back(degree);
    }

    return !degreesOut.empty();
}

inline std::string progressionToText(const std::vector<int>& degrees) {
    std::string out;
    for (int degree : degrees) {
        if (!out.empty()) out += ' ';
        out += std::to_string(std::clamp(degree, 1, 7));
    }
    return out;
}

struct ProgressionPreset {
    const char* name;
    const char* degrees;
    const char* description;
};

inline const std::vector<ProgressionPreset>& progressionPresets() {
    static const std::vector<ProgressionPreset> presets = {
        {"Pop",         "1 5 6 4",
         "The one in more songs than any other. Works in major and, played "
         "against a minor scale, becomes something else entirely."},
        {"Sad pop",     "6 4 1 5",
         "The same four chords starting a different place, which is most of "
         "what makes a progression feel like it does."},
        {"Doo-wop",     "1 6 4 5",
         "Fifties, and still everywhere. Turns around cleanly, so it loops."},
        {"Andalusian",  "1 7 6 5",
         "Descending. In a minor scale this is the flamenco cadence, and it "
         "is where a great deal of synthwave lives."},
        {"Jazz turn",   "2 5 1",
         "The one every jazz standard is built from. Sevenths rather than "
         "triads."},
        {"Epic",        "6 4 1 5 6 4 1 5",
         "Eight bars rather than four, so it resolves half as often - which "
         "is what makes it feel larger."},
        {"Static",      "1 1 4 1",
         "Barely moves. What a lot of chiptune actually does, because three "
         "channels do not leave room for much harmony."},
    };
    return presets;
}

} // namespace generators
} // namespace ChiptuneTracker
