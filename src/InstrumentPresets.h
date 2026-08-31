#pragma once

/*
 * ChiptuneTracker - Starting points for the configurable engines
 *
 * The five engines added in Task F each have a lot of parameters, and a user
 * who picks "FM" from the palette gets exactly one sound: whatever the
 * default patch happens to be. Six operators with ratios, levels, detune and
 * four envelope stages each is 60-odd numbers, and nobody discovers a bell
 * by dragging them one at a time.
 *
 * So each engine gets a short list of starting points. Not a preset browser
 * and not an attempt at completeness - a handful of recognisable sounds that
 * demonstrate what the engine is FOR, chosen so that the differences between
 * them teach the parameters. The FM list is bell, electric piano, brass and
 * bass because those four are what FM is famous for and each one is a
 * different algorithm; a list of four variations on the same routing would
 * be prettier and would teach nothing.
 *
 * These write into the config and nothing else. They are not a preset
 * SYSTEM: there is no saving, no user list, no file format. That is
 * deliberate for a first version - the .ctp already stores every one of
 * these values, so "save your own" is copying a channel, and a preset format
 * would be a second way to store the same thing.
 */

#include <array>
#include <cstdint>

#include "Types.h"

namespace ChiptuneTracker {

// ============================================================================
// FM
// ============================================================================
struct FMPreset {
    const char* name;
    const char* description;
    void (*apply)(FMPatch&);
};

namespace presets {

inline void fmBell(FMPatch& patch) {
    patch = FMPatch{};
    patch.algorithm = FMAlgorithm::threePairs();
    patch.index = 7.5f;
    patch.algorithm.feedback = 0.0f;

    // Carriers ring; modulators die fast. That difference IS the bell: the
    // strike is metallic because the modulators are still loud, and the tail
    // is pure because they have gone.
    for (int i : {0, 2, 4}) {
        FMOperator& op = patch.operators[static_cast<size_t>(i)];
        op.attack = 0.001f;
        op.decay = 4.0f;
        op.sustain = 0.0f;
        op.release = 2.5f;
    }
    for (int i : {1, 3, 5}) {
        FMOperator& op = patch.operators[static_cast<size_t>(i)];
        op.attack = 0.001f;
        op.decay = 0.25f;
        op.sustain = 0.0f;
        op.release = 0.2f;
        op.velocitySensitivity = 0.7f;
    }

    // Inharmonic ratios, which is what makes it a bell rather than an organ.
    patch.operators[0].ratio = 1.0f;
    patch.operators[1].ratio = 3.51f;
    patch.operators[2].ratio = 2.0f;
    patch.operators[3].ratio = 5.13f;
    patch.operators[4].ratio = 4.0f;
    patch.operators[5].ratio = 7.02f;
}

inline void fmElectricPiano(FMPatch& patch) {
    patch = FMPatch{};
    patch.algorithm = FMAlgorithm::threePairs();
    patch.index = 3.2f;

    // Whole-number ratios: harmonic, so it reads as a pitched instrument
    // rather than a bell. The bark on the attack is the modulator's fast
    // decay, and the velocity sensitivity is what makes it playable.
    patch.operators[0].ratio = 1.0f;
    patch.operators[1].ratio = 14.0f;   // the classic DX7 tine
    patch.operators[2].ratio = 1.0f;
    patch.operators[3].ratio = 1.0f;
    patch.operators[4].ratio = 2.0f;
    patch.operators[5].ratio = 1.0f;

    for (int i : {0, 2, 4}) {
        FMOperator& op = patch.operators[static_cast<size_t>(i)];
        op.attack = 0.002f;
        op.decay = 2.2f;
        op.sustain = 0.15f;
        op.release = 0.5f;
    }
    for (int i : {1, 3, 5}) {
        FMOperator& op = patch.operators[static_cast<size_t>(i)];
        op.attack = 0.001f;
        op.decay = 0.12f;
        op.sustain = 0.0f;
        op.release = 0.1f;
        op.velocitySensitivity = 0.85f;
    }
    patch.operators[1].level = 0.55f;
}

inline void fmBrass(FMPatch& patch) {
    patch = FMPatch{};
    patch.algorithm = FMAlgorithm::brass();
    patch.index = 4.0f;
    patch.algorithm.feedback = 0.35f;

    for (FMOperator& op : patch.operators) {
        op.ratio = 1.0f;
        op.attack = 0.06f;      // brass does not start instantly
        op.decay = 0.4f;
        op.sustain = 0.8f;
        op.release = 0.25f;
    }
    patch.operators[1].ratio = 2.0f;
    patch.operators[2].ratio = 3.0f;

    // The modulator opens more slowly than the carriers, so the tone gets
    // brighter as the note is held - which is what a brass player does.
    patch.operators[5].ratio = 1.0f;
    patch.operators[5].attack = 0.15f;
    patch.operators[5].decay = 0.6f;
    patch.operators[5].sustain = 0.7f;
    patch.operators[5].velocitySensitivity = 0.6f;
}

inline void fmBass(FMPatch& patch) {
    patch = FMPatch{};
    patch.algorithm = FMAlgorithm::stack();
    patch.index = 2.4f;
    patch.algorithm.feedback = 0.55f;   // past ~0.5 the top operator saws

    for (FMOperator& op : patch.operators) {
        op.ratio = 1.0f;
        op.attack = 0.0f;
        op.decay = 0.5f;
        op.sustain = 0.6f;
        op.release = 0.12f;
        op.enabled = false;
    }
    // Only three operators: a deep stack on a bass is mud, and switching the
    // rest off is cheaper than turning their levels down.
    patch.operators[0].enabled = true;
    patch.operators[1].enabled = true;
    patch.operators[5].enabled = true;

    patch.operators[1].ratio = 1.0f;
    patch.operators[5].ratio = 2.0f;
    patch.operators[5].decay = 0.18f;
    patch.operators[5].sustain = 0.2f;
    patch.operators[5].velocitySensitivity = 0.8f;
}

} // namespace presets

inline constexpr FMPreset FM_PRESETS[] = {
    {"Bell",          "Inharmonic ratios, ringing carriers, fast modulators",
     &presets::fmBell},
    {"Electric Piano","The DX7 tine: harmonic, with a bark on the attack",
     &presets::fmElectricPiano},
    {"Brass",         "One modulator into three carriers, opening as it holds",
     &presets::fmBrass},
    {"Bass",          "Three operators and heavy feedback - a saw with teeth",
     &presets::fmBass},
};

inline constexpr int FM_PRESET_COUNT =
    static_cast<int>(sizeof(FM_PRESETS) / sizeof(FM_PRESETS[0]));

// ============================================================================
// The modelled drums
// ============================================================================
struct DrumPreset {
    const char* name;
    const char* description;
    void (*apply)(DrumModelConfig&);
};

namespace presets {

inline void drum808Kick(DrumModelConfig& config) {
    config = DrumModelConfig{};
    config.voice = DrumVoiceType::Kick;
    config.tuneHz = 48.0f;
    // The long tail is the whole 808 kick, and it only works because the
    // pitch sweep is short: a note, not a thud.
    config.decaySeconds = 1.4f;
    config.pitchSweepSemitones = 22.0f;
    config.pitchSweepSeconds = 0.03f;
    config.snap = 0.15f;
}

inline void drumHardKick(DrumModelConfig& config) {
    config = DrumModelConfig{};
    config.voice = DrumVoiceType::Kick;
    config.tuneHz = 62.0f;
    config.decaySeconds = 0.22f;
    config.pitchSweepSemitones = 44.0f;   // a long way down, very fast
    config.pitchSweepSeconds = 0.02f;
    config.snap = 0.75f;                  // and a hard click on top
}

inline void drumSnare(DrumModelConfig& config) {
    config = DrumModelConfig{};
    config.voice = DrumVoiceType::Snare;
    config.tuneHz = 190.0f;
    config.decaySeconds = 0.2f;
    config.noiseMix = 0.62f;
    config.noiseTone = 0.55f;
    config.snap = 0.5f;
}

inline void drumRimshot(DrumModelConfig& config) {
    config = DrumModelConfig{};
    config.voice = DrumVoiceType::Snare;
    config.tuneHz = 380.0f;
    config.decaySeconds = 0.06f;
    // Almost all shell and almost no rattle, which is what a rimshot is.
    config.noiseMix = 0.15f;
    config.snap = 0.9f;
}

inline void drumClosedHat(DrumModelConfig& config) {
    config = DrumModelConfig{};
    config.voice = DrumVoiceType::HiHat;
    config.tuneHz = 300.0f;
    config.decaySeconds = 0.045f;
    config.hatHighpass = 0.8f;
    config.snap = 0.4f;
}

inline void drumOpenHat(DrumModelConfig& config) {
    drumClosedHat(config);
    config.decaySeconds = 0.55f;
}

} // namespace presets

inline constexpr DrumPreset DRUM_PRESETS[] = {
    {"808 Kick",   "Long tuned tail - a bass note as much as a drum",
     &presets::drum808Kick},
    {"Hard Kick",  "A very fast, very deep sweep with a click on top",
     &presets::drumHardKick},
    {"Snare",      "Tuned shell plus rattle, roughly even",
     &presets::drumSnare},
    {"Rimshot",    "Nearly all shell, almost no rattle, very short",
     &presets::drumRimshot},
    {"Closed Hat", "Six inharmonic squares, high-passed hard",
     &presets::drumClosedHat},
    {"Open Hat",   "The same, allowed to ring",
     &presets::drumOpenHat},
};

inline constexpr int DRUM_PRESET_COUNT =
    static_cast<int>(sizeof(DRUM_PRESETS) / sizeof(DRUM_PRESETS[0]));

// ============================================================================
// Granular
// ============================================================================
struct GranularPreset {
    const char* name;
    const char* description;
    void (*apply)(GranularConfig&);
};

namespace presets {

inline void granFreeze(GranularConfig& config) {
    const int keep = config.sampleId;   // never clobber the loaded audio
    config = GranularConfig{};
    config.sampleId = keep;
    config.positionRate = 0.0f;         // the headline trick
    config.spray = 0.03f;
    config.grainSeconds = 0.09f;
    config.grainsPerSecond = 45.0f;
    config.followNote = true;
}

inline void granCloud(GranularConfig& config) {
    const int keep = config.sampleId;
    config = GranularConfig{};
    config.sampleId = keep;
    config.positionRate = 0.12f;        // drifting, not frozen
    config.spray = 0.25f;
    config.grainSeconds = 0.12f;
    config.grainsPerSecond = 80.0f;
    config.pitchJitter = 5.0f;
    config.reverseChance = 0.35f;
    config.windowShape = 0.0f;          // smoothest
}

inline void granStutter(GranularConfig& config) {
    const int keep = config.sampleId;
    config = GranularConfig{};
    config.sampleId = keep;
    config.positionRate = 0.0f;
    config.spray = 0.0f;                // no scatter, so it is rhythmic
    config.grainSeconds = 0.03f;
    config.grainsPerSecond = 22.0f;
    config.windowShape = 1.0f;          // flat-topped, keeps transients
}

inline void granSlowScan(GranularConfig& config) {
    const int keep = config.sampleId;
    config = GranularConfig{};
    config.sampleId = keep;
    config.positionRate = 0.25f;        // a 4x time stretch, in effect
    config.spray = 0.02f;
    config.grainSeconds = 0.1f;
    config.grainsPerSecond = 60.0f;
}

} // namespace presets

inline constexpr GranularPreset GRANULAR_PRESETS[] = {
    {"Freeze",    "Hold one moment forever, at its own pitch",
     &presets::granFreeze},
    {"Cloud",     "Scattered, detuned, some grains backwards",
     &presets::granCloud},
    {"Stutter",   "No scatter and a flat window - rhythmic, not smeared",
     &presets::granStutter},
    {"Slow Scan", "A quarter speed with the pitch untouched",
     &presets::granSlowScan},
};

inline constexpr int GRANULAR_PRESET_COUNT =
    static_cast<int>(sizeof(GRANULAR_PRESETS) / sizeof(GRANULAR_PRESETS[0]));

// ============================================================================
// Modulation
// ============================================================================
/*
 * The matrix is the hardest of these to get started with, because an empty
 * one does nothing and the first useful route is not obvious. These are the
 * four that answer "what is this for".
 */
struct ModPreset {
    const char* name;
    const char* description;
    void (*apply)(ModMatrix&);
};

namespace presets {

inline void modVibrato(ModMatrix& matrix) {
    matrix = ModMatrix{};
    matrix.lfos[0].shape = LFOShape::Sine;
    matrix.lfos[0].rateHz = 5.5f;
    // Delayed and faded, because vibrato arriving with the note sounds
    // mechanical and no player does it.
    matrix.lfos[0].delaySeconds = 0.25f;
    matrix.lfos[0].fadeSeconds = 0.35f;
    matrix.addRoute(ModRoute{ModSource::LFO1, ModDestination::Pitch, 0.02f, true});
}

inline void modWobble(ModMatrix& matrix) {
    matrix = ModMatrix{};
    matrix.lfos[0].shape = LFOShape::Sine;
    matrix.lfos[0].rateHz = 3.0f;
    matrix.addRoute(ModRoute{ModSource::LFO1, ModDestination::FilterCutoff,
                             0.45f, true});
    matrix.addRoute(ModRoute{ModSource::Velocity, ModDestination::FilterCutoff,
                             0.25f, true});
}

inline void modEvolvingPad(ModMatrix& matrix) {
    matrix = ModMatrix{};
    matrix.lfos[0].shape = LFOShape::Triangle;
    matrix.lfos[0].rateHz = 0.18f;      // very slow
    matrix.lfos[0].retrigger = false;   // free-running, so notes differ
    matrix.env2Attack = 1.6f;
    matrix.env2Decay = 3.0f;
    matrix.env2Sustain = 0.7f;
    matrix.env2Release = 2.0f;
    matrix.addRoute(ModRoute{ModSource::LFO1, ModDestination::WavetableMorph,
                             0.55f, true});
    matrix.addRoute(ModRoute{ModSource::Envelope2, ModDestination::FMBrightness,
                             0.4f, true});
}

inline void modExpressive(ModMatrix& matrix) {
    matrix = ModMatrix{};
    matrix.addRoute(ModRoute{ModSource::Velocity, ModDestination::FMBrightness,
                             0.7f, true});
    matrix.addRoute(ModRoute{ModSource::KeyTrack, ModDestination::FilterCutoff,
                             0.5f, true});
    matrix.addRoute(ModRoute{ModSource::ModWheel, ModDestination::WavetableMorph,
                             0.8f, true});
    matrix.addRoute(ModRoute{ModSource::RandomPerNote, ModDestination::Pitch,
                             0.004f, true});   // a few cents of human drift
}

} // namespace presets

inline constexpr ModPreset MOD_PRESETS[] = {
    {"Vibrato",       "A delayed sine on pitch, the way a player adds it",
     &presets::modVibrato},
    {"Filter Wobble", "Slow LFO and velocity, both opening the filter",
     &presets::modWobble},
    {"Evolving Pad",  "A very slow free-running LFO on morph and brightness",
     &presets::modEvolvingPad},
    {"Expressive",    "Velocity, key position, the wheel, and a little drift",
     &presets::modExpressive},
};

inline constexpr int MOD_PRESET_COUNT =
    static_cast<int>(sizeof(MOD_PRESETS) / sizeof(MOD_PRESETS[0]));

} // namespace ChiptuneTracker
