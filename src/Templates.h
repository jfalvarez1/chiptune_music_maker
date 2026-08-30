#pragma once

/*
 * ChiptuneTracker - Genre starter templates
 *
 * The single densest finding in the research was that people write an
 * eight-bar loop and then cannot turn it into a song, and that what they ask
 * for is a concrete structure to follow rather than a principle. The other
 * repeated finding was that a blank project is where projects die.
 *
 * So: four bars that already play. Channels named and routed, a drum pattern,
 * a bass line following a progression, chords, and a lead - in the key and at
 * the tempo the genre actually uses. It is not a song and is not meant to
 * sound finished; it is meant to be something to change.
 *
 * Two decisions worth stating:
 *
 *   - The drum pattern is one bar placed four times, rather than a four-bar
 *     pattern. That is how the arrangement is meant to be used, and a starter
 *     project is the best place to show it.
 *   - Nothing here is random. A template that generated something different
 *     every time would be impossible to learn from and impossible to test.
 *
 * Pure and ImGui-free: it returns a Project and touches nothing else.
 */

#include "Types.h"
#include "Genres.h"

#include <cmath>
#include <string>

namespace ChiptuneTracker {

// A one-bar rhythm written as sixteen steps.
//   'x' a hit, 'X' an accent, anything else a rest.
// Far easier to read and to change than a list of beat offsets.
inline void addStepLine(Pattern& pattern, const char* steps, int pitch,
                        OscillatorType oscillator, float velocity = 0.85f,
                        float stepBeats = 0.25f) {
    if (steps == nullptr) return;

    for (int i = 0; steps[i] != '\0'; ++i) {
        const char step = steps[i];
        if (step != 'x' && step != 'X') continue;
        if (static_cast<int>(pattern.notes.size()) >= Pattern::MAX_NOTES) return;

        Note note;
        note.pitch = pitch;
        note.startTime = static_cast<float>(i) * stepBeats;
        note.duration = stepBeats;
        note.oscillatorType = oscillator;
        note.velocity = (step == 'X') ? 1.0f : velocity;
        pattern.notes.push_back(note);
    }
}

struct GenreTemplate {
    // One bar of drums, sixteen steps each.
    const char* kick;
    const char* snare;
    const char* hat;

    // Four bars of harmony, as semitone offsets from the key's root. A minor
    // i - VI - III - VII is {0, 8, 3, 10}, which is most of popular music.
    int chordRoots[4];

    OscillatorType lead;
    OscillatorType chords;
    OscillatorType bass;
    OscillatorType kickVoice;
    OscillatorType snareVoice;
    OscillatorType hatVoice;

    // Whether the lead spells the chord out in eighth notes or holds it.
    bool arpeggiateLead;

    // Eighth-note bass rather than one note a bar.
    bool drivingBass;
};

inline const GenreTemplate& genreTemplate(Genre genre) {
    static const GenreTemplate TEMPLATES[] = {
        {   // Everything - a neutral starting point, not a genre
            "x...x...x...x...",
            "....x.......x...",
            "x.x.x.x.x.x.x.x.",
            {0, 8, 3, 10},
            OscillatorType::Pulse, OscillatorType::SynthPad, OscillatorType::Triangle,
            OscillatorType::Kick808, OscillatorType::Snare, OscillatorType::HiHat,
            false, false
        },
        {   // Chiptune - arpeggios do the work chords cannot on three voices
            "x...x...x...x...",
            "....x.......x...",
            "x.x.x.x.x.x.x.x.",
            {0, 8, 3, 10},
            OscillatorType::Pulse, OscillatorType::Pulse, OscillatorType::Triangle,
            OscillatorType::Kick808, OscillatorType::Snare, OscillatorType::HiHat,
            true, true
        },
        {   // Synthwave - offbeat hats, held pad, driving bass
            "x...x...x...x...",
            "....x.......x...",
            "..x...x...x...x.",
            {0, 8, 3, 10},
            OscillatorType::SynthwaveLead, OscillatorType::SynthwavePad,
            OscillatorType::SynthwaveBass,
            OscillatorType::Kick808, OscillatorType::Snare, OscillatorType::HiHat,
            false, true
        },
        {   // Hip Hop - boom bap, sparse and swung
            "x..x..x.....x...",
            "....x.......x...",
            "x.x.x.x.x.x.x.x.",
            {0, 8, 3, 10},
            OscillatorType::TrapLead, OscillatorType::LoFiKeys,
            OscillatorType::SubBass808,
            OscillatorType::Kick808, OscillatorType::Snare, OscillatorType::HiHat,
            false, false
        },
        {   // Reggaeton - the dembow. The percussion is the genre.
            //
            // The widely used approximation: kick on every beat, and the
            // snare in the 3-3-2 grouping that gives the pattern its lilt.
            "x...x...x...x...",
            "..x.x..x..x.x..x",
            "x.x.x.x.x.x.x.x.",
            {0, 8, 3, 10},
            OscillatorType::PolySynth, OscillatorType::PolySynth,
            OscillatorType::SubBass808,
            OscillatorType::Kick808, OscillatorType::Clap, OscillatorType::HiHat,
            false, false
        },
        {   // EDM - four on the floor, offbeat hats, supersaw
            "x...x...x...x...",
            "....x.......x...",
            "..x...x...x...x.",
            {0, 8, 3, 10},
            OscillatorType::Supersaw, OscillatorType::RaveChord,
            OscillatorType::Reese,
            OscillatorType::Kick808, OscillatorType::Clap, OscillatorType::HiHat,
            true, true
        },
        {   // Rock - backbeat and a full kit
            "x.....x.x.......",
            "....x.......x...",
            "x.x.x.x.x.x.x.x.",
            {0, 8, 3, 10},
            OscillatorType::Sawtooth, OscillatorType::SynthBrass,
            OscillatorType::SynthBass,
            OscillatorType::Kick, OscillatorType::Snare, OscillatorType::HiHat,
            false, false
        },
        {   // Lofi - slow, swung, and deliberately behind the beat
            "x.......x..x....",
            "....x.......x...",
            "x..x.x..x..x.x..",
            {0, 8, 3, 10},
            OscillatorType::LoFiKeys, OscillatorType::LoFiKeys,
            OscillatorType::SubBass808,
            OscillatorType::KickSoft, OscillatorType::SnareRim, OscillatorType::HiHat,
            false, false
        },
    };

    static_assert(sizeof(TEMPLATES) / sizeof(TEMPLATES[0]) ==
                  static_cast<size_t>(Genre::Count),
                  "every genre needs a template");

    const int index = static_cast<int>(genre);
    if (index < 0 || index >= static_cast<int>(Genre::Count)) return TEMPLATES[0];
    return TEMPLATES[index];
}

inline constexpr int TEMPLATE_BARS = 4;

/*
 * Build a four-bar starting point for a genre.
 *
 * Channels: 0 Lead, 1 Chords, 2 Bass, 3 Drums. Patterns are named for what
 * they are, because the pattern list is the first thing anyone reads.
 */
inline Project makeGenreTemplate(Genre genre) {
    const GenreProfile& profile = genreProfile(genre);
    const GenreTemplate& recipe = genreTemplate(genre);

    Project project;
    project.name = std::string(profile.name) + " Starter";
    project.bpm = profile.bpm;
    project.swing = profile.swing;
    project.beatsPerMeasure = 4;

    const float barBeats = 4.0f;
    const float songBeats = barBeats * TEMPLATE_BARS;
    project.songLength = songBeats;

    // The root an octave below middle C, so the bass has somewhere to go.
    const int keyRoot = 48 + profile.scaleRoot;

    project.patterns.clear();
    project.arrangement.clear();

    // ---- Channels ---------------------------------------------------------
    project.channels[0].name = "Lead";
    project.channels[0].oscillator.type = recipe.lead;
    project.channels[0].volume = 0.75f;
    project.channels[0].pan = -0.15f;

    project.channels[1].name = "Chords";
    project.channels[1].oscillator.type = recipe.chords;
    project.channels[1].volume = 0.55f;
    project.channels[1].pan = 0.20f;

    project.channels[2].name = "Bass";
    project.channels[2].oscillator.type = recipe.bass;
    project.channels[2].volume = 0.80f;
    project.channels[2].pan = 0.0f;

    project.channels[3].name = "Drums";
    project.channels[3].oscillator.type = recipe.kickVoice;
    project.channels[3].volume = 0.85f;
    project.channels[3].pan = 0.0f;

    // ---- Drums: one bar, placed four times --------------------------------
    //
    // Deliberately not a four-bar pattern. Reusing one pattern across the
    // arrangement is the thing a starter project should demonstrate.
    Pattern drums;
    drums.name = "Drums";
    drums.length = static_cast<int>(barBeats);
    addStepLine(drums, recipe.kick, 36, recipe.kickVoice, 1.0f);
    addStepLine(drums, recipe.snare, 38, recipe.snareVoice, 0.9f);
    addStepLine(drums, recipe.hat, 42, recipe.hatVoice, 0.45f);
    project.patterns.push_back(drums);
    const int drumsIndex = 0;

    // ---- Bass -------------------------------------------------------------
    Pattern bass;
    bass.name = "Bass";
    bass.length = static_cast<int>(songBeats);
    for (int bar = 0; bar < TEMPLATE_BARS; ++bar) {
        const int root = keyRoot + recipe.chordRoots[bar];
        const float barStart = static_cast<float>(bar) * barBeats;

        if (recipe.drivingBass) {
            for (int eighth = 0; eighth < 8; ++eighth) {
                Note note;
                note.pitch = root;
                note.startTime = barStart + static_cast<float>(eighth) * 0.5f;
                note.duration = 0.45f;
                note.oscillatorType = recipe.bass;
                note.velocity = (eighth % 2 == 0) ? 0.9f : 0.7f;
                bass.notes.push_back(note);
            }
        } else {
            for (int hit = 0; hit < 2; ++hit) {
                Note note;
                note.pitch = root;
                note.startTime = barStart + static_cast<float>(hit) * 2.0f;
                note.duration = 1.75f;
                note.oscillatorType = recipe.bass;
                note.velocity = 0.9f;
                bass.notes.push_back(note);
            }
        }
    }
    project.patterns.push_back(bass);
    const int bassIndex = 1;

    // ---- Chords -----------------------------------------------------------
    //
    // Root, minor third and fifth held for the bar. Minor because every
    // genre profile here is in a minor key, which is not an accident.
    Pattern chords;
    chords.name = "Chords";
    chords.length = static_cast<int>(songBeats);
    for (int bar = 0; bar < TEMPLATE_BARS; ++bar) {
        const int root = keyRoot + 12 + recipe.chordRoots[bar];
        const int voicing[] = {0, 3, 7};
        for (int interval : voicing) {
            Note note;
            note.pitch = root + interval;
            note.startTime = static_cast<float>(bar) * barBeats;
            note.duration = barBeats - 0.1f;
            note.oscillatorType = recipe.chords;
            note.velocity = 0.55f;
            chords.notes.push_back(note);
        }
    }
    project.patterns.push_back(chords);
    const int chordsIndex = 2;

    // ---- Lead -------------------------------------------------------------
    Pattern lead;
    lead.name = "Lead";
    lead.length = static_cast<int>(songBeats);
    for (int bar = 0; bar < TEMPLATE_BARS; ++bar) {
        const int root = keyRoot + 24 + recipe.chordRoots[bar];
        const float barStart = static_cast<float>(bar) * barBeats;

        if (recipe.arpeggiateLead) {
            // Up and back down the triad across the bar.
            const int shape[] = {0, 3, 7, 12, 7, 3, 0, 3};
            for (int i = 0; i < 8; ++i) {
                Note note;
                note.pitch = root + shape[i];
                note.startTime = barStart + static_cast<float>(i) * 0.5f;
                note.duration = 0.45f;
                note.oscillatorType = recipe.lead;
                note.velocity = (i == 0) ? 0.9f : 0.7f;
                lead.notes.push_back(note);
            }
        } else {
            // A held answer to the chord, so there is a melody to argue with.
            const int shape[] = {7, 3};
            for (int i = 0; i < 2; ++i) {
                Note note;
                note.pitch = root + shape[i];
                note.startTime = barStart + static_cast<float>(i) * 2.0f;
                note.duration = 1.75f;
                note.oscillatorType = recipe.lead;
                note.velocity = 0.8f;
                lead.notes.push_back(note);
            }
        }
    }
    project.patterns.push_back(lead);
    const int leadIndex = 3;

    // ---- Arrangement ------------------------------------------------------
    for (int bar = 0; bar < TEMPLATE_BARS; ++bar) {
        Clip clip;
        clip.patternIndex = drumsIndex;
        clip.channelIndex = 3;
        clip.startBeat = static_cast<float>(bar) * barBeats;
        clip.lengthBeats = barBeats;
        clip.color = 0xFFCC6644u;
        project.arrangement.push_back(clip);
    }

    auto placeFullLength = [&](int patternIndex, int channel, uint32_t colour) {
        Clip clip;
        clip.patternIndex = patternIndex;
        clip.channelIndex = channel;
        clip.startBeat = 0.0f;
        clip.lengthBeats = songBeats;
        clip.color = colour;
        project.arrangement.push_back(clip);
    };

    placeFullLength(leadIndex, 0, 0xFF4488FFu);
    placeFullLength(chordsIndex, 1, 0xFFAA66FFu);
    placeFullLength(bassIndex, 2, 0xFF44CC88u);

    return project;
}

} // namespace ChiptuneTracker
