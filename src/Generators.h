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

#include <vector>
#include <algorithm>
#include <cmath>

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

} // namespace generators
} // namespace ChiptuneTracker
