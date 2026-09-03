#pragma once

// ============================================================================
// Showcase songs - finished pieces that demonstrate what this can do
//
// The genre templates in Templates.h are STARTERS: four bars, deliberately
// sparse, meant to be taken over. These are the opposite - complete short
// pieces, mixed and mastered, each built around the parts of the program
// that are hardest to discover by poking at panels.
//
// WHY THEY EXIST. Five instrument engines, four equalisers, six reverb
// algorithms and a convolution unit are all reachable from a dropdown and
// invisible until somebody happens to choose one. A person evaluating this
// should be able to hear an FM patch, a wavetable sweep and a granular pad
// inside a minute, in a piece of music rather than as a held note.
//
// MIXED AND MASTERED, which is the part that makes them worth loading. A
// demonstration where the bass swamps the lead teaches the wrong lesson
// about the engine. Every one of these sets its own channel levels and
// pans, its own send levels, and its own master chain - and the mastering
// is deliberately conservative: gentle compression, a small amount of
// tone, and a limiter that is catching peaks rather than doing the work.
//
// Each is short on purpose. Four to eight bars that loop is enough to hear
// what an engine does, and long enough to be music rather than a test tone.
// ============================================================================

#include <string>
#include <vector>

#include "Types.h"
#include "FMSynth.h"
#include "GranularSynth.h"
#include "DrumMachine.h"
#include "InstrumentPresets.h"

namespace ChiptuneTracker {

enum class Showcase : uint8_t {
    FMBells = 0,     // the six-operator FM engine, in a piece
    WavetableMotion, // a morphing wavetable, swept and modulated
    GranularClouds,  // grains, and the reverbs behind them
    ModelledDrums,   // the analogue drum model, with the EQs on it
    Count
};

inline const char* showcaseName(Showcase which) {
    switch (which) {
        case Showcase::FMBells:         return "FM Bells";
        case Showcase::WavetableMotion: return "Wavetable Motion";
        case Showcase::GranularClouds:  return "Granular Clouds";
        case Showcase::ModelledDrums:   return "Modelled Drums";
        default:                        return "?";
    }
}

inline const char* showcaseDescription(Showcase which) {
    switch (which) {
        case Showcase::FMBells:
            return "Six-operator FM: inharmonic bells over an FM bass, "
                   "through a convolution reverb.";
        case Showcase::WavetableMotion:
            return "A wavetable lead whose morph is swept by an LFO, with "
                   "the filter and delay moving under it.";
        case Showcase::GranularClouds:
            return "Granular pads through the shimmer and hall reverbs, "
                   "under a simple melody.";
        case Showcase::ModelledDrums:
            return "The modelled drum kit - pitch and decay per drum - with "
                   "a dynamic EQ and bus compression on it.";
        default:
            return "";
    }
}

namespace showcase {

// A note, without repeating six fields at every call site.
inline void put(Pattern& pattern, float beat, int pitch, float duration,
                float velocity = 0.8f) {
    Note note;
    note.startTime = beat;
    note.pitch = pitch;
    note.duration = duration;
    note.velocity = velocity;
    pattern.notes.push_back(note);
}

// A drum hit, which carries its own instrument.
inline void hit(Pattern& pattern, float beat, int pitch, OscillatorType voice,
                float velocity = 0.9f) {
    Note note;
    note.startTime = beat;
    note.pitch = pitch;
    note.duration = 0.125f;
    note.velocity = velocity;
    note.oscillatorType = voice;
    pattern.notes.push_back(note);
}

inline int addPattern(Project& project, Pattern pattern, int lengthBeats) {
    pattern.length = lengthBeats;
    project.patterns.push_back(std::move(pattern));
    return static_cast<int>(project.patterns.size()) - 1;
}

// Place a pattern on a channel, repeated to fill the song.
inline void arrange(Project& project, int patternIndex, int channel,
                    int lengthBeats, int repeats) {
    for (int i = 0; i < repeats; ++i) {
        Clip clip;
        clip.patternIndex = patternIndex;
        clip.channelIndex = channel;
        clip.startBeat = float(i * lengthBeats);
        clip.lengthBeats = float(lengthBeats);
        project.arrangement.push_back(clip);
    }
}

/*
 * A conservative master chain.
 *
 * Gentle on purpose. A demonstration that arrives squashed teaches the
 * wrong thing about the engine it is demonstrating, and loud is not the
 * same as finished - the limiter here is catching peaks rather than doing
 * the work, and the compressor is holding the mix together rather than
 * flattening it.
 */
inline void master(Project& project, float lowGain, float midGain,
                   float highGain, float compThreshold = -14.0f) {
    project.masterVolume = 0.72f;

    project.masterEQEnabled = true;
    project.masterEQLowGain = lowGain;
    project.masterEQMidGain = midGain;
    project.masterEQHighGain = highGain;

    project.masterCompressorEnabled = true;
    project.masterCompThreshold = compThreshold;
    project.masterCompRatio = 2.2f;      // gentle
    project.masterCompAttack = 0.012f;   // slow enough to let transients through
    project.masterCompRelease = 0.18f;
    project.masterCompMakeup = 1.5f;

    project.masterLimiterEnabled = true;
    project.masterLimiterCeiling = -0.3f;
    project.masterLimiterRelease = 0.05f;
}

} // namespace showcase

// ============================================================================
// FM Bells
// ============================================================================
inline Project makeShowcaseFMBells() {
    using namespace showcase;

    Project project;
    project.name = "FM Bells";
    project.bpm = 96.0f;
    project.beatsPerMeasure = 4;

    const int bar = 4;
    const int loop = bar * 4;          // four bars
    project.songLength = float(loop * 2);

    project.patterns.clear();
    project.arrangement.clear();

    // ---- Channels -----------------------------------------------------------
    ChannelConfig& bells = project.channels[0];
    bells.name = "FM Bells";
    bells.oscillator.type = OscillatorType::FMSynth;
    FM_PRESETS[0].apply(bells.oscillator.fm);      // the inharmonic bell
    bells.volume = 0.62f;
    bells.pan = -0.18f;
    // Convolution rather than the algorithmic reverb, because a bell's decay
    // is where a real impulse response is most obviously different.
    bells.convolutionEnabled = true;
    bells.convolutionMix = 0.42f;

    ChannelConfig& bass = project.channels[1];
    bass.name = "FM Bass";
    bass.oscillator.type = OscillatorType::FMSynth;
    FM_PRESETS[3].apply(bass.oscillator.fm);       // three operators, heavy feedback
    bass.volume = 0.78f;
    bass.pan = 0.0f;
    bass.filterEnabled = true;
    bass.filterCutoff = 2200.0f;
    bass.filterResonance = 0.18f;

    ChannelConfig& keys = project.channels[2];
    keys.name = "FM Keys";
    keys.oscillator.type = OscillatorType::FMSynth;
    FM_PRESETS[1].apply(keys.oscillator.fm);       // the tine electric piano
    keys.volume = 0.45f;
    keys.pan = 0.28f;
    keys.delayEnabled = true;
    keys.delayTime = 0.375f;                        // dotted, against the 96 bpm
    keys.delayFeedback = 0.28f;
    keys.delayMix = 0.22f;

    ChannelConfig& drums = project.channels[3];
    drums.name = "Drums";
    drums.volume = 0.70f;

    // ---- Music --------------------------------------------------------------
    //
    // A minor, i - VI - III - VII, which is where bells sound like bells.
    Pattern bellPattern;
    bellPattern.name = "Bells";
    const int BELL_ROOTS[] = {69, 65, 72, 67};      // A, F, C, G
    for (int b = 0; b < 4; ++b) {
        const float at = float(b * bar);
        put(bellPattern, at + 0.0f, BELL_ROOTS[b], 1.5f, 0.72f);
        put(bellPattern, at + 1.5f, BELL_ROOTS[b] + 7, 1.0f, 0.55f);
        put(bellPattern, at + 2.5f, BELL_ROOTS[b] + 12, 1.5f, 0.62f);
    }
    arrange(project, addPattern(project, bellPattern, loop), 0, loop, 2);

    Pattern bassPattern;
    bassPattern.name = "FM Bass";
    const int BASS_ROOTS[] = {45, 41, 48, 43};
    for (int b = 0; b < 4; ++b) {
        const float at = float(b * bar);
        put(bassPattern, at + 0.0f, BASS_ROOTS[b], 0.9f, 0.9f);
        put(bassPattern, at + 1.5f, BASS_ROOTS[b], 0.4f, 0.7f);
        put(bassPattern, at + 2.0f, BASS_ROOTS[b] + 12, 0.4f, 0.65f);
        put(bassPattern, at + 3.0f, BASS_ROOTS[b] + 7, 0.9f, 0.75f);
    }
    arrange(project, addPattern(project, bassPattern, loop), 1, loop, 2);

    Pattern keysPattern;
    keysPattern.name = "FM Keys";
    for (int b = 0; b < 4; ++b) {
        const float at = float(b * bar);
        for (int i = 0; i < 4; ++i) {
            put(keysPattern, at + float(i) * 0.5f + 0.25f,
                BELL_ROOTS[b] - 12 + (i % 2 ? 4 : 0), 0.35f, 0.38f);
        }
    }
    arrange(project, addPattern(project, keysPattern, loop), 2, loop, 2);

    Pattern drumPattern;
    drumPattern.name = "Drums";
    for (int b = 0; b < 4; ++b) {
        const float at = float(b * bar);
        hit(drumPattern, at + 0.0f, 36, OscillatorType::KickSoft, 0.85f);
        hit(drumPattern, at + 2.0f, 38, OscillatorType::SnareRim, 0.55f);
        for (int i = 0; i < 8; ++i) {
            hit(drumPattern, at + float(i) * 0.5f, 42, OscillatorType::HiHat,
                (i % 2) ? 0.28f : 0.42f);
        }
    }
    arrange(project, addPattern(project, drumPattern, loop), 3, loop, 2);

    // Warm, with the top left alone so the bells keep their air.
    showcase::master(project, 1.0f, -0.5f, 1.5f, -14.0f);
    return project;
}

// ============================================================================
// Wavetable Motion
// ============================================================================
inline Project makeShowcaseWavetable() {
    using namespace showcase;

    Project project;
    project.name = "Wavetable Motion";
    project.bpm = 124.0f;
    project.beatsPerMeasure = 4;

    const int bar = 4;
    const int loop = bar * 4;
    project.songLength = float(loop * 2);

    project.patterns.clear();
    project.arrangement.clear();

    ChannelConfig& lead = project.channels[0];
    lead.name = "Wavetable";
    lead.oscillator.type = OscillatorType::Custom;
    lead.volume = 0.68f;
    lead.pan = -0.10f;
    /*
     * The morph is swept rather than parked.
     *
     * A wavetable sitting at a fixed morph position is a sampled waveform -
     * the movement is the entire point of the engine, and a demonstration
     * that does not move it demonstrates nothing.
     */
    lead.oscillator.wavetableMorph = 0.15f;
    lead.oscillator.wavetableMorphSweep = 0.75f;
    lead.oscillator.wavetableSweepTime = 1.6f;
    lead.filterEnabled = true;
    lead.filterCutoff = 4200.0f;
    lead.filterResonance = 0.32f;
    lead.delayEnabled = true;
    lead.delayTime = 0.25f;
    lead.delayFeedback = 0.34f;
    lead.delayMix = 0.26f;

    ChannelConfig& pad = project.channels[1];
    pad.name = "Wavetable Pad";
    pad.oscillator.type = OscillatorType::Custom;
    pad.oscillator.wavetableMorph = 0.6f;
    pad.oscillator.wavetableMorphSweep = -0.4f;
    pad.oscillator.wavetableSweepTime = 3.2f;
    pad.volume = 0.40f;
    pad.pan = 0.32f;
    pad.reverbEnabled = true;
    pad.reverbMix = 0.38f;
    pad.reverbRoomSize = 0.82f;

    ChannelConfig& bass = project.channels[2];
    bass.name = "Bass";
    bass.oscillator.type = OscillatorType::AcidBass;
    bass.volume = 0.76f;
    bass.filterEnabled = true;
    bass.filterCutoff = 1500.0f;
    bass.filterResonance = 0.55f;
    bass.filterEnvEnabled = true;
    bass.filterEnvAmount = 0.6f;

    ChannelConfig& drums = project.channels[3];
    drums.name = "Drums";
    drums.volume = 0.82f;

    // F minor, driving.
    Pattern leadPattern;
    leadPattern.name = "Wavetable Lead";
    const int NOTES[] = {65, 68, 72, 68, 70, 68, 65, 63};
    for (int b = 0; b < 4; ++b) {
        const float at = float(b * bar);
        for (int i = 0; i < 8; ++i) {
            put(leadPattern, at + float(i) * 0.5f,
                NOTES[(i + b) % 8], 0.45f, (i % 2) ? 0.55f : 0.75f);
        }
    }
    arrange(project, addPattern(project, leadPattern, loop), 0, loop, 2);

    Pattern padPattern;
    padPattern.name = "Pad";
    const int PAD_ROOTS[] = {53, 56, 60, 58};
    for (int b = 0; b < 4; ++b) {
        put(padPattern, float(b * bar), PAD_ROOTS[b], 3.8f, 0.5f);
        put(padPattern, float(b * bar), PAD_ROOTS[b] + 7, 3.8f, 0.35f);
    }
    arrange(project, addPattern(project, padPattern, loop), 1, loop, 2);

    Pattern bassPattern;
    bassPattern.name = "Bass";
    for (int b = 0; b < 4; ++b) {
        const float at = float(b * bar);
        for (int i = 0; i < 4; ++i) {
            put(bassPattern, at + float(i), PAD_ROOTS[b] - 12, 0.85f,
                (i == 0) ? 0.95f : 0.7f);
        }
    }
    arrange(project, addPattern(project, bassPattern, loop), 2, loop, 2);

    Pattern drumPattern;
    drumPattern.name = "Drums";
    for (int b = 0; b < 4; ++b) {
        const float at = float(b * bar);
        for (int i = 0; i < 4; ++i) {
            hit(drumPattern, at + float(i), 36, OscillatorType::Kick, 0.95f);
        }
        hit(drumPattern, at + 1.0f, 38, OscillatorType::Clap, 0.8f);
        hit(drumPattern, at + 3.0f, 38, OscillatorType::Clap, 0.8f);
        for (int i = 0; i < 8; ++i) {
            if (i % 2 == 1) {
                hit(drumPattern, at + float(i) * 0.5f, 46,
                    OscillatorType::HiHatOpen, 0.45f);
            }
        }
    }
    arrange(project, addPattern(project, drumPattern, loop), 3, loop, 2);

    showcase::master(project, 1.5f, -1.0f, 2.0f, -13.0f);
    return project;
}

// ============================================================================
// Granular Clouds
// ============================================================================
inline Project makeShowcaseGranular() {
    using namespace showcase;

    Project project;
    project.name = "Granular Clouds";
    project.bpm = 76.0f;
    project.beatsPerMeasure = 4;

    const int bar = 4;
    const int loop = bar * 4;
    project.songLength = float(loop * 2);

    project.patterns.clear();
    project.arrangement.clear();

    ChannelConfig& cloud = project.channels[0];
    cloud.name = "Granular";
    cloud.oscillator.type = OscillatorType::Granular;
    GRANULAR_PRESETS[1].apply(cloud.oscillator.granular);   // the cloud
    cloud.volume = 0.52f;
    cloud.pan = -0.22f;
    // Shimmer, which is the algorithm that most obviously is not a room.
    cloud.reverbEnabled = true;
    cloud.reverbAlgorithm = 4;
    cloud.reverbMix = 0.55f;
    cloud.reverbRoomSize = 0.88f;

    ChannelConfig& texture = project.channels[1];
    texture.name = "Grain Texture";
    texture.oscillator.type = OscillatorType::Granular;
    GRANULAR_PRESETS[0].apply(texture.oscillator.granular);  // freeze
    texture.volume = 0.34f;
    texture.pan = 0.30f;
    texture.reverbEnabled = true;
    texture.reverbAlgorithm = 1;      // hall, behind the shimmer
    texture.reverbMix = 0.45f;

    ChannelConfig& melody = project.channels[2];
    melody.name = "Melody";
    melody.oscillator.type = OscillatorType::SynthPad;
    melody.volume = 0.50f;
    melody.pan = 0.05f;
    melody.reverbEnabled = true;
    melody.reverbMix = 0.30f;

    ChannelConfig& bass = project.channels[3];
    bass.name = "Sub";
    bass.oscillator.type = OscillatorType::SubBass808;
    bass.volume = 0.66f;

    // D minor, slow and open.
    const int ROOTS[] = {62, 60, 57, 58};

    Pattern cloudPattern;
    cloudPattern.name = "Granular";
    for (int b = 0; b < 4; ++b) {
        put(cloudPattern, float(b * bar), ROOTS[b], 3.9f, 0.55f);
    }
    arrange(project, addPattern(project, cloudPattern, loop), 0, loop, 2);

    Pattern texturePattern;
    texturePattern.name = "Texture";
    for (int b = 0; b < 4; ++b) {
        put(texturePattern, float(b * bar) + 1.0f, ROOTS[b] + 12, 2.6f, 0.4f);
    }
    arrange(project, addPattern(project, texturePattern, loop), 1, loop, 2);

    Pattern melodyPattern;
    melodyPattern.name = "Melody";
    for (int b = 0; b < 4; ++b) {
        const float at = float(b * bar);
        put(melodyPattern, at + 0.5f, ROOTS[b] + 12, 1.2f, 0.6f);
        put(melodyPattern, at + 2.0f, ROOTS[b] + 15, 0.8f, 0.5f);
        put(melodyPattern, at + 3.0f, ROOTS[b] + 19, 0.9f, 0.45f);
    }
    arrange(project, addPattern(project, melodyPattern, loop), 2, loop, 2);

    Pattern bassPattern;
    bassPattern.name = "Sub";
    for (int b = 0; b < 4; ++b) {
        put(bassPattern, float(b * bar), ROOTS[b] - 24, 3.5f, 0.8f);
    }
    arrange(project, addPattern(project, bassPattern, loop), 3, loop, 2);

    // Low end kept in check, since four sustained parts stack up.
    showcase::master(project, -1.0f, 0.5f, 1.0f, -16.0f);
    return project;
}

// ============================================================================
// Modelled Drums
// ============================================================================
inline Project makeShowcaseDrums() {
    using namespace showcase;

    Project project;
    project.name = "Modelled Drums";
    project.bpm = 92.0f;
    project.beatsPerMeasure = 4;

    const int bar = 4;
    const int loop = bar * 2;          // two bars, so the fill lands
    project.songLength = float(loop * 4);

    project.patterns.clear();
    project.arrangement.clear();

    /*
     * Three channels of the SAME engine, tuned differently.
     *
     * That is the point of a modelled drum rather than a sampled one: the
     * kick, snare and hat here are one algorithm with different pitch and
     * decay, and a demonstration on one channel would hide that.
     */
    ChannelConfig& kick = project.channels[0];
    kick.name = "Model Kick";
    kick.oscillator.type = OscillatorType::DrumModel;
    DRUM_PRESETS[0].apply(kick.oscillator.drumModel);
    kick.volume = 0.90f;
    // A dynamic EQ, so the low end ducks only when the kick is loud.
    kick.dynamicEqEnabled = true;
    kick.dynamicEqFrequency = 120.0f;
    kick.dynamicEqThreshold = -20.0f;
    kick.dynamicEqRange = -4.0f;

    ChannelConfig& snare = project.channels[1];
    snare.name = "Model Snare";
    snare.oscillator.type = OscillatorType::DrumModel;
    DRUM_PRESETS[2].apply(snare.oscillator.drumModel);
    snare.volume = 0.78f;
    snare.pan = -0.08f;
    snare.reverbEnabled = true;
    snare.reverbAlgorithm = 2;         // plate, which is what a snare wants
    snare.reverbMix = 0.26f;

    ChannelConfig& hats = project.channels[2];
    hats.name = "Model Hats";
    hats.oscillator.type = OscillatorType::DrumModel;
    DRUM_PRESETS[4].apply(hats.oscillator.drumModel);
    hats.volume = 0.55f;
    hats.pan = 0.24f;
    hats.tiltEqEnabled = true;
    hats.tiltEqAmount = 2.5f;            // tilted bright

    ChannelConfig& bass = project.channels[3];
    bass.name = "Bass";
    bass.oscillator.type = OscillatorType::SynthBass;
    bass.volume = 0.72f;
    // Ducked by the kick, which is what makes a groove breathe.
    bass.sidechainEnabled = true;
    bass.sidechainAmount = 0.45f;
    bass.sidechainRelease = 0.14f;

    Pattern kickPattern;
    kickPattern.name = "Kick";
    for (int b = 0; b < 2; ++b) {
        const float at = float(b * bar);
        put(kickPattern, at + 0.0f, 36, 0.2f, 0.98f);
        put(kickPattern, at + 1.75f, 36, 0.2f, 0.72f);
        put(kickPattern, at + 2.5f, 36, 0.2f, 0.85f);
        if (b == 1) put(kickPattern, at + 3.5f, 36, 0.2f, 0.6f);
    }
    arrange(project, addPattern(project, kickPattern, loop), 0, loop, 4);

    Pattern snarePattern;
    snarePattern.name = "Snare";
    for (int b = 0; b < 2; ++b) {
        const float at = float(b * bar);
        put(snarePattern, at + 1.0f, 38, 0.25f, 0.9f);
        put(snarePattern, at + 3.0f, 38, 0.25f, 0.9f);
        if (b == 1) {
            put(snarePattern, at + 3.5f, 38, 0.15f, 0.55f);
            put(snarePattern, at + 3.75f, 38, 0.15f, 0.7f);
        }
    }
    arrange(project, addPattern(project, snarePattern, loop), 1, loop, 4);

    Pattern hatPattern;
    hatPattern.name = "Hats";
    for (int b = 0; b < 2; ++b) {
        const float at = float(b * bar);
        for (int i = 0; i < 8; ++i) {
            put(hatPattern, at + float(i) * 0.5f, 42, 0.1f,
                (i % 2) ? 0.32f : 0.5f);
        }
    }
    arrange(project, addPattern(project, hatPattern, loop), 2, loop, 4);

    Pattern bassPattern;
    bassPattern.name = "Bass";
    const int ROOTS[] = {40, 40, 43, 41};
    for (int b = 0; b < 2; ++b) {
        const float at = float(b * bar);
        put(bassPattern, at + 0.0f, ROOTS[b * 2], 0.7f, 0.85f);
        put(bassPattern, at + 1.5f, ROOTS[b * 2], 0.4f, 0.6f);
        put(bassPattern, at + 2.5f, ROOTS[b * 2 + 1], 1.2f, 0.8f);
    }
    arrange(project, addPattern(project, bassPattern, loop), 3, loop, 4);

    // Punchy, with a little lift on top for the hats.
    showcase::master(project, 1.5f, 0.0f, 1.0f, -12.0f);
    return project;
}

inline Project makeShowcase(Showcase which) {
    switch (which) {
        case Showcase::WavetableMotion: return makeShowcaseWavetable();
        case Showcase::GranularClouds:  return makeShowcaseGranular();
        case Showcase::ModelledDrums:   return makeShowcaseDrums();
        default:                        return makeShowcaseFMBells();
    }
}

} // namespace ChiptuneTracker
