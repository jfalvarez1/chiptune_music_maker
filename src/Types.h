#pragma once

/*
 * ChiptuneTracker - Core Types
 *
 * Fundamental data structures for the DAW
 */

#include <cstdint>
#include <cmath>
#include <array>
#include <vector>
#include <string>
#include <algorithm>

#include "Macros.h"

#include "Snap.h"
#include "Sample.h"
#include "TempoMap.h"
#include "FMSynth.h"
#include "Sampler.h"
#include "GranularSynth.h"
#include "DrumMachine.h"
#include "ModMatrix.h"
#include "Reverbs.h"
#include "Convolution.h"
#include "EqualizerSuite.h"
#include "Genres.h"

namespace ChiptuneTracker {

// ============================================================================
// Mathematical Constants
// ============================================================================
constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 6.28318530718f;

// ============================================================================
// Musical Constants
// ============================================================================
constexpr int NOTES_PER_OCTAVE = 12;
constexpr int MAX_OCTAVES = 8;
constexpr int TOTAL_NOTES = NOTES_PER_OCTAVE * MAX_OCTAVES;
constexpr float BASE_A4_FREQ = 440.0f;

// MIDI note to frequency conversion
inline float noteToFrequency(int midiNote) {
    return BASE_A4_FREQ * std::pow(2.0f, (midiNote - 69) / 12.0f);
}

// ============================================================================
// Oscillator Types
// ============================================================================
enum class OscillatorType : uint8_t {
    Pulse,      // Variable duty cycle (12.5%, 25%, 50%, 75%)
    Triangle,   // Adjustable slope
    Sawtooth,   // Rising or falling
    Sine,       // Pure tone
    Noise,      // LFSR white/periodic noise
    Supersaw,   // Multiple detuned saws (7 oscillators)
    Custom,     // Wavetable
    // Synth Presets
    SynthLead,      // Bright cutting lead (detuned saws)
    SynthPad,       // Soft atmospheric pad (slow attack)
    SynthBass,      // Deep punchy bass (sub + harmonics)
    SynthPluck,     // Short plucky sound (fast decay)
    SynthArp,       // Crisp arpeggio sound (pulse + fast env)
    SynthOrgan,     // Classic organ (additive harmonics)
    SynthStrings,   // String ensemble (detuned + slow attack)
    SynthBrass,     // Brassy stab (saw + filter sweep feel)
    SynthChip,      // Classic chiptune lead (pulse 12.5%)
    SynthBell,      // Bell/chime sound (FM-like)
    // Synthwave Presets
    SynthwaveLead,  // Bright PWM lead with warmth
    SynthwaveBass,  // Deep 808-style saw bass
    SynthwavePad,   // Warm lush evolving pad
    SynthwaveArp,   // Crisp sequence/arp sound
    SynthwaveChord, // Polyphonic stab for chords
    SynthwaveFM,    // Classic DX7-style FM brass
    // Techno/Electronic Presets
    AcidBass,       // TB-303 style resonant bass
    TechnoStab,     // Short chord stab
    Hoover,         // Classic rave hoover sound
    RaveChord,      // Rave piano chord
    Reese,          // Reese bass (detuned saws)
    // Hip Hop Presets
    SubBass808,     // Deep 808 sub bass
    LoFiKeys,       // Dusty lo-fi piano
    VinylNoise,     // Vinyl crackle texture
    TrapLead,       // Trap-style lead
    // Additional Synthwave
    GatedPad,       // Rhythmic gated pad
    PolySynth,      // Rich poly synth
    SyncLead,       // Hard sync lead
    // Kicks
    Kick,       // Standard kick with pitch sweep
    Kick808,    // Deep 808 kick, more sub-bass
    KickHard,   // Punchy tight kick
    KickSoft,   // Soft warm kick
    // Snares
    Snare,      // Standard snare with noise
    Snare808,   // Classic 808 snare, more tonal
    SnareRim,   // Rimshot, clicky
    Clap,       // Hand clap (multiple bursts)
    // Hi-Hats
    HiHat,      // Closed hi-hat
    HiHatOpen,  // Open hi-hat, longer decay
    HiHatPedal, // Pedal hi-hat, very short
    // Toms
    Tom,        // Mid tom
    TomLow,     // Floor tom
    TomHigh,    // High tom
    // Cymbals
    Crash,      // Crash cymbal
    Ride,       // Ride cymbal
    // Percussion
    Cowbell,    // 808 cowbell
    Clave,      // Wood block click
    Conga,      // Conga drum
    Maracas,    // Shaker
    Tambourine, // Jingly metallic
    // Reggaeton Instruments
    ReggaetonBass,  // Deep punchy reggaeton bass (808-style with pitch sweep)
    LatinBrass,     // Brass stab for reggaeton hooks
    Guira,          // Scraped metal percussion (dembow essential)
    Bongo,          // Bongo drums
    Timbale,        // Timbale hit
    Dembow808,      // 808-style kick tuned for dembow rhythm
    DembowSnare,    // Tight clap-like snare for dembow (1-3kHz emphasis)
    
    // High-Accuracy Recreations
    Vocoder,        // Sawtooth with formant filtering (Nightcall lead)
    KavinskyBass,   // Filtered Saw with envelope (Nightcall bass)

    // Six-operator FM. At the END of the enum, and it has to be: the type is
    // serialised by name, but the pad banks, the sound palette's name table
    // and per-note overrides all index it numerically, so inserting in the
    // middle silently renumbers every type after the insertion point. I put
    // it after Custom first and the palette's static_assert caught it.
    FMSynth,

    // Multisample instrument. Same rule: appended, never inserted.
    Sampler,

    // Granular, and the editable analogue drum model. Appended, never
    // inserted - the pad banks and the palette's name table index this enum.
    Granular,
    DrumModel
};

// ============================================================================
// Oscillator Configuration
// ============================================================================
struct OscillatorConfig {
    OscillatorType type = OscillatorType::Pulse;

    // Pulse settings
    float pulseWidth = 0.5f;        // 0.0 to 1.0 (duty cycle)

    // Triangle settings
    float triangleSlope = 0.5f;     // 0.0 = saw down, 0.5 = triangle, 1.0 = saw up

    // Noise settings
    bool noiseShortMode = false;    // NES short mode (periodic, metallic)

    // Which of the 2A03's sixteen noise periods to clock the shift register
    // at. -1 tracks the played note instead, which is not what hardware does
    // but is what you want when a noise instrument should follow the
    // keyboard. 0 is the highest pitch, 15 the lowest rumble.
    int noisePeriod = -1;

    // ---- Wavetable -----------------------------------------------------
    //
    // Which of the project's banks the Custom oscillator plays, and where
    // between its tables it sits. Morph is a first-class parameter rather
    // than a fixed table choice because moving through a bank while a note
    // holds is the entire reason a bank has more than one table in it.
    int wavetableBank = 0;
    float wavetableMorph = 0.0f;    // 0 = first table, 1 = last

    // How far the morph travels over the life of a note, in morph units.
    // A static wavetable is a sampled waveform; a moving one is what makes
    // the engine worth having, and requiring an automation lane for the
    // most common case would be a poor trade.
    float wavetableMorphSweep = 0.0f;
    float wavetableSweepTime = 0.5f;   // seconds to travel the sweep

    // ---- Six-operator FM -------------------------------------------------
    //
    // Only meaningful when type == FMSynth. Held inline rather than behind a
    // pointer so a ChannelConfig stays copyable and the audio thread never
    // dereferences anything.
    FMPatch fm;
    int fmAlgorithmPreset = 0;      // index into FMAlgorithmPreset

    // ---- Multisample -----------------------------------------------------
    //
    // Only meaningful when type == Sampler. Inline for the same reason the
    // FM patch is: a ChannelConfig stays copyable and the audio thread never
    // dereferences anything to reach it.
    SamplerInstrument sampler;

    // ---- Granular and the drum model -------------------------------------
    //
    // Inline like the others, so a ChannelConfig stays copyable and the
    // audio thread never dereferences anything to reach them.
    GranularConfig granular;
    DrumModelConfig drumModel;

    // ---- Modulation ------------------------------------------------------
    //
    // Any source to any destination. This is what makes the engines above
    // worth having: a wavetable whose morph never moves is a sampled
    // waveform, and an FM patch whose index is fixed is one timbre.
    ModMatrix modMatrix;

    // General
    float detune = 0.0f;            // Cents (-100 to +100)
    float phase = 0.0f;             // Starting phase (0.0 to 1.0)
};

// ============================================================================
// ADSR Envelope
// ============================================================================
struct Envelope {
    float attack  = 0.01f;   // Seconds
    float decay   = 0.1f;    // Seconds
    float sustain = 0.7f;    // Level (0.0 to 1.0)
    float release = 0.2f;    // Seconds
};

// ============================================================================
// Duty Cycle Presets (NES-style)
// ============================================================================
enum class DutyCycle : uint8_t {
    Duty12_5 = 0,   // 12.5% - Thin, reedy sound (NES default for some channels)
    Duty25   = 1,   // 25%   - Hollow, slightly nasal
    Duty50   = 2,   // 50%   - Square wave, full and rich
    Duty75   = 3    // 75%   - Same as 25% but inverted phase
};

// Convert DutyCycle to float (0.0-1.0)
inline float dutyCycleToFloat(DutyCycle dc) {
    switch (dc) {
        case DutyCycle::Duty12_5: return 0.125f;
        case DutyCycle::Duty25:   return 0.25f;
        case DutyCycle::Duty50:   return 0.5f;
        case DutyCycle::Duty75:   return 0.75f;
        default: return 0.5f;
    }
}

// ============================================================================
// Pitch Sweep Direction (NES sweep unit style)
// ============================================================================
enum class SweepDirection : uint8_t {
    None = 0,       // No sweep
    Up   = 1,       // Pitch rises over time
    Down = 2        // Pitch falls over time (like laser sound)
};

// ============================================================================
// Note Event
// ============================================================================
struct Note {
    int      pitch     = 60;        // MIDI note number (60 = C4)
    float    velocity  = 1.0f;      // 0.0 to 1.0
    float    startTime = 0.0f;      // Position in beats
    float    duration  = 1.0f;      // Duration in beats

    // Per-note oscillator type (each note can have its own sound)
    OscillatorType oscillatorType = OscillatorType::Pulse;

    // Sample playback (if >= 0, use sample instead of oscillatorType)
    int      sampleID = -1;         // -1 = use oscillatorType, >= 0 = use sample from pool

    // Fade in/out (in beats, 0 = instant)
    float    fadeIn    = 0.0f;      // Fade in duration (beats)
    float    fadeOut   = 0.0f;      // Fade out duration (beats)

    // Per-note effects (classic tracker effects)
    int      arpeggio  = 0;         // Chord offset pattern (0 = none)
    float    vibrato   = 0.0f;      // Vibrato depth (semitones)
    float    vibratoSpeed = 6.0f;   // Vibrato speed (Hz)
    float    slide     = 0.0f;      // Pitch slide amount (semitones per beat)

    // NES-style Duty Cycle (for pulse waves)
    DutyCycle dutyCycle = DutyCycle::Duty50;    // 12.5%, 25%, 50%, or 75%
    bool useDutyCycle = false;                  // Override channel duty cycle

    // Pitch Sweep (NES sweep unit - automatic pitch bend)
    SweepDirection sweepDirection = SweepDirection::None;
    float    sweepSpeed = 1.0f;     // How fast pitch changes (semitones per beat)
    float    sweepAmount = 12.0f;   // Total sweep range (semitones)

    // Echo/Delay (MIDI delay technique)
    int      echoRepeats = 0;       // Number of echo repeats (0 = off, 1-4)
    float    echoDelay = 0.25f;     // Delay between echoes (beats)
    float    echoDecay = 0.5f;      // Volume decay per echo (0.0-1.0)

    // Note Retrigger (rapid retriggering for stutter effects)
    int      retriggerCount = 0;    // Number of retriggers (0 = off)
    float    retriggerSpeed = 0.125f; // Time between retriggers (beats)

    // Note Cut/Delay (tracker-style)
    float    noteCut = 0.0f;        // Cut note after this many beats (0 = no cut)
    float    noteDelay = 0.0f;      // Delay note start by this many beats

    // Tremolo (volume modulation)
    float    tremolo = 0.0f;        // Tremolo depth (0.0-1.0)
    float    tremoloSpeed = 4.0f;   // Tremolo speed (Hz)

    // Chance this note sounds on any given pass, 0..1. A sixteen-step loop
    // repeats a great deal; this is the cheapest way to stop it sounding
    // like a loop, and it is one float. 1.0 means always, as it always did.
    float    probability = 1.0f;

    bool isValid() const { return pitch >= 0 && pitch < 128; }
};

// ============================================================================
// Pattern (Sequence of Notes for one channel)
// ============================================================================
struct Pattern {
    static constexpr int MAX_NOTES = 256;
    static constexpr int DEFAULT_LENGTH = 16;  // Steps (beats)

    std::string name = "Pattern";
    int length = DEFAULT_LENGTH;               // Pattern length in beats
    std::vector<Note> notes;

    Pattern() { notes.reserve(MAX_NOTES); }
};

// ============================================================================
// Channel Configuration
// ============================================================================
// How many sends a channel can carry. Two covers the common case - a
// reverb and a delay - without making the mixer strip unreadable.
inline constexpr int MAX_SENDS_PER_CHANNEL = 2;

/*
 * A copy of a channel's signal, delivered to an aux bus at its own level.
 *
 * Pre-fader means the send is taken before the channel's volume, so pulling
 * the channel down leaves the send at full level - the usual way to fade a
 * dry signal into its own reverb tail.
 */
struct SendConfig {
    int destination = -1;    // aux bus index; -1 = no send
    float level = 0.0f;      // 0..1
    bool preFader = false;
};

// Capacity of a channel's insert rack. Lives here because ChannelConfig
// carries the order and Types.h is below Effects.h; EffectsChain static_asserts
// that its own MAX_SLOTS agrees with this.
inline constexpr int MAX_FX_SLOTS = 24;

struct ChannelConfig {
    std::string name = "Channel";
    OscillatorConfig oscillator;
    Envelope envelope;

    float volume = 0.8f;
    float pan = 0.0f;           // -1.0 (left) to +1.0 (right)
    bool muted = false;
    bool solo = false;

    // Effect enables
    bool arpeggiatorEnabled = false;
    bool vibratoEnabled = false;
    bool bitcrusherEnabled = false;
    bool distortionEnabled = false;
    bool delayEnabled = false;
    bool filterEnabled = false;

    // Reverb settings (genre effects)
    bool reverbEnabled = false;
    float reverbMix = 0.35f;
    float reverbRoomSize = 0.7f;
    float reverbDamping = 0.4f;

    // Chorus settings (genre effects)
    bool chorusEnabled = false;
    float chorusMix = 0.3f;
    float chorusRate = 0.5f;
    float chorusDepth = 0.005f;

    // Extended delay settings (genre effects)
    float delayMix = 0.2f;
    float delayTime = 0.25f;
    float delayFeedback = 0.3f;

    // Stereo Widener (essential for synthwave pads)
    bool stereoWidenerEnabled = false;
    float stereoWidenerWidth = 0.5f;
    float stereoWidenerHaas = 0.015f;
    float stereoWidenerMix = 0.5f;

    // Tape Saturation (analog warmth)
    bool tapeSaturationEnabled = false;
    float tapeDrive = 1.5f;
    float tapeWarmth = 0.5f;
    float tapeCompression = 0.3f;
    float tapeMix = 0.5f;

    // Filter settings (for genre effects)
    int filterType = 0;  // 0=LP, 1=HP, 2=BP
    float filterCutoff = 2000.0f;
    float filterResonance = 0.3f;

    // Distortion settings
    int distortionType = 0;  // 0=Tanh, 1=HardClip, 2=Foldback, 3=Asymmetric
    float distortionDrive = 2.0f;
    float distortionMix = 0.5f;

    // Bitcrusher settings (lo-fi / chiptune)
    float bitDepth = 8.0f;
    float sampleRateDiv = 4.0f;

    // Phaser settings
    bool phaserEnabled = false;
    float phaserRate = 0.5f;
    float phaserDepth = 0.5f;
    float phaserFeedback = 0.5f;

    // Flanger settings (NEW - jet-plane/swoosh effect for synthwave)
    bool flangerEnabled = false;
    float flangerRate = 0.5f;      // LFO Hz (0.1-10)
    float flangerDepth = 0.005f;   // Delay depth in seconds (0.001-0.01)
    float flangerFeedback = 0.5f;  // Feedback amount (-0.95 to +0.95)
    float flangerMix = 0.5f;       // Dry/wet mix (0.0-1.0)

    // Tremolo settings
    bool tremoloEnabled = false;
    float tremoloRate = 5.0f;
    float tremoloDepth = 0.3f;

    // Sidechain settings
    bool sidechainEnabled = false;
    int sidechainSource = -1; // Source channel index (-1 = none)
    float sidechainAmount = 0.6f;
    float sidechainRelease = 0.15f;

    // Attack and threshold had controls in the Channel Editor and nowhere to
    // be stored, so they were written to the live chain and lost on save.
    // Defaults match Sidechain's own, so a project that never touched them
    // is unchanged.
    float sidechainAttack = 0.005f;
    float sidechainThreshold = 0.3f;

    // The ring modulator had no config at all - not even an enable flag -
    // despite having three controls in the editor.
    bool ringModEnabled = false;
    float ringModFrequency = 200.0f;
    float ringModMix = 0.5f;

    // Channel-level Echo (applies to all notes on this channel)
    bool echoEnabled = false;
    float echoTime = 0.25f;         // Echo delay time (seconds)
    float echoFeedback = 0.4f;      // Feedback amount (0.0-0.9)
    float echoMix = 0.3f;           // Wet/dry mix (0.0-1.0)

    // Channel Detune (for stereo widening/richness)
    float detuneCents = 0.0f;       // Fine detune (-100 to +100 cents)

    // Filter Envelope (Per-voice modulation)
    bool filterEnvEnabled = false;
    float filterEnvAmount = 0.0f;   // -1.0 to 1.0 (modulates cutoff relative to base)
    float filterEnvAttack = 0.0f;   // Seconds
    float filterEnvDecay = 0.1f;    // Seconds

    // 3-Band EQ settings
    bool eqEnabled = false;
    float eqLow = 1.0f;     // Gain (0.0 to 2.0)
    float eqMid = 1.0f;
    float eqHigh = 1.0f;
    float eqLowFreq = 200.0f;
    float eqMidFreq = 1000.0f;
    float eqHighFreq = 5000.0f;

    // Sends to the aux buses. Silent by default: a project that has never
    // heard of sends behaves exactly as it did.
    std::array<SendConfig, MAX_SENDS_PER_CHANNEL> sends{};

    // Sidechain from an aux bus rather than by tapping a channel directly.
    // -1 keeps the legacy channel tap in sidechainSource, which is how every
    // pre-v4 project works and must keep working.
    int sidechainBus = -1;

    // Insert-rack order, as EffectType indices.
    //
    // A count of 0 means "the classic order" - which is precisely what every
    // project file written before v3 implies, so those migrate by doing
    // nothing at all rather than by a translation that could be wrong.
    // ---- Pitch and time --------------------------------------------------
    //
    // All three are phase-vocoder effects with a window of latency, and none
    // of them is remotely chip-authentic - so all three are off by default.
    // Which reverb algorithm the channel's reverb slot runs. Room is the
    // original and the default, so an existing project is untouched.
    int reverbAlgorithm = 0;

    // ---- Convolution ------------------------------------------------------
    //
    // Off by default, and the engine holds no buffers until it is on - the
    // delay line is around 700 KB per second of impulse response, and this
    // struct is instantiated 36 times.
    bool convolutionEnabled = false;
    int convolutionIR = 0;          // indexes ImpulseResponse
    float convolutionMix = 0.35f;

    // ---- Equalisers -------------------------------------------------------
    bool tiltEqEnabled = false;
    float tiltEqAmount = 0.0f;      // dB, negative is darker
    float tiltEqCentre = 700.0f;

    bool graphicEqEnabled = false;
    std::array<float, GraphicEQ::BANDS> graphicEqGains{};

    bool dynamicEqEnabled = false;
    float dynamicEqFrequency = 300.0f;
    float dynamicEqQ = 1.2f;
    float dynamicEqThreshold = -24.0f;
    float dynamicEqRange = -6.0f;
    float dynamicEqAttack = 0.010f;
    float dynamicEqRelease = 0.120f;

    bool pitchShiftEnabled = false;
    float pitchShiftSemitones = 0.0f;
    float pitchShiftMix = 1.0f;

    bool formantShiftEnabled = false;
    float formantShiftSemitones = 0.0f;
    float formantShiftMix = 1.0f;

    bool autoTuneEnabled = false;
    float autoTuneStrength = 0.5f;
    int autoTuneRoot = 0;              // 0 = C
    int autoTuneScaleMask = 0x0FFF;    // chromatic
    float autoTuneMix = 1.0f;

    std::array<uint8_t, MAX_FX_SLOTS> fxOrder{};
    int fxSlotCount = 0;

    // Compressor settings
    bool compressorEnabled = false;
    float compThreshold = 0.5f;
    float compRatio = 4.0f;
    float compAttack = 0.01f;
    float compRelease = 0.1f;
    float compGain = 1.0f;
    
    // Formant Filter
    bool formantEnabled = false;
    int formantVowel = 3; // 0=A, 1=E, 2=I, 3=O, 4=U (Default O for Nightcall)
    float formantResonance = 5.0f;

    // Instrument macros - the step sequences that make a chip instrument.
    // See Macros.h. Inactive by default, so channels behave exactly as
    // before until a macro is switched on.
    InstrumentMacros macros;

    // Chip-authentic playback options
    //
    // Real hardware had a 4-bit volume DAC. Quantising to 16 levels is a
    // surprisingly large part of why chiptune sounds like chiptune, and it
    // is the kind of thing that has to be opt-in per channel rather than
    // imposed on a project that is only chiptune-adjacent.
    bool quantizeVolume4Bit = false;
};

// ============================================================================
// Arrangement Clip (Pattern placement on timeline)
// ============================================================================
// What a clip on the timeline actually is.
//
// Audio clips sit on a channel beside pattern clips rather than on a
// separate track type, so they inherit that channel's volume, pan, insert
// rack and sends. A recorded vocal gets the channel's reverb for free.
enum class ClipType : uint8_t {
    Pattern = 0,
    Audio
};

struct Clip {
    int patternIndex = 0;       // Which pattern to play
    int channelIndex = 0;       // Which channel
    float startBeat = 0.0f;     // Position on timeline
    float lengthBeats = 16.0f;  // Length (can be different from pattern)

    // Visual
    uint32_t color = 0xFF4488FF;

    // Semitones added to every note when this placement plays. LSDJ's chain
    // transpose: one bassline pattern, placed four times at 0, +8, +3, +10,
    // is a whole progression - which also relieves the 64-pattern cap.
    int transpose = 0;

    // ---- Audio clips -----------------------------------------------------
    //
    // Only meaningful when type == Audio. Kept on Clip rather than in a
    // separate struct so the arrangement stays one list: every existing
    // operation - move, delete, select, the loop range, the tracker's
    // channel resolution - keeps working on both kinds without a branch.
    int sampleId = -1;          // index into the project's SamplePool
    float gain = 1.0f;
    float trimStartSeconds = 0.0f;   // where playback starts inside the sample
    float trimEndSeconds = 0.0f;     // 0 = to the end of the sample
    float fadeInBeats = 0.0f;
    float fadeOutBeats = 0.0f;
    bool loopClip = false;      // repeat the trimmed region to fill the clip

    // Last, not first. Putting a new field at the front of an
    // aggregate-initialised struct silently re-maps every Clip{...} in the
    // codebase - the exact mistake Clip::transpose taught, and which I made
    // again here by leading with it.
    ClipType type = ClipType::Pattern;

    // Which comp group emitted this clip, or -1 for one placed by hand.
    // Flattening a comp replaces its own previous output rather than adding
    // to it, and this is how it recognises that output. After `type` for the
    // same reason `type` is last: Clip is aggregate-initialised in a dozen
    // places and inserting in the middle silently re-maps every one.
    int compGroup = -1;
};

// ============================================================================
// Wavetable System
// ============================================================================

// Single wavetable (one waveform cycle)
struct Wavetable {
    static constexpr int TABLE_SIZE = 256;
    std::array<float, TABLE_SIZE> samples;  // -1.0 to +1.0
    std::string name = "Wavetable";

    // Initialize with a basic waveform
    void initSine() {
        for (int i = 0; i < TABLE_SIZE; i++) {
            samples[i] = std::sin(2.0f * PI * i / TABLE_SIZE);
        }
        name = "Sine";
    }

    void initSaw() {
        for (int i = 0; i < TABLE_SIZE; i++) {
            samples[i] = 2.0f * (float)i / TABLE_SIZE - 1.0f;
        }
        name = "Saw";
    }

    void initSquare() {
        for (int i = 0; i < TABLE_SIZE; i++) {
            samples[i] = (i < TABLE_SIZE / 2) ? 1.0f : -1.0f;
        }
        name = "Square";
    }

    void initTriangle() {
        for (int i = 0; i < TABLE_SIZE; i++) {
            if (i < TABLE_SIZE / 2) {
                samples[i] = 4.0f * i / TABLE_SIZE - 1.0f;
            } else {
                samples[i] = 3.0f - 4.0f * i / TABLE_SIZE;
            }
        }
        name = "Triangle";
    }

    // Clear to silence
    void clear() {
        samples.fill(0.0f);
    }

    // Normalize waveform to -1.0 to +1.0 range
    void normalize() {
        float maxVal = 0.0f;
        for (float sample : samples) {
            maxVal = std::max(maxVal, std::abs(sample));
        }
        if (maxVal > 0.0001f) {
            for (float& sample : samples) {
                sample /= maxVal;
            }
        }
    }

    // Interpolated lookup (for high-quality playback)
    float lookup(float phase) const {
        // phase is 0.0 to 1.0
        float pos = phase * TABLE_SIZE;
        int index0 = (int)pos % TABLE_SIZE;
        int index1 = (index0 + 1) % TABLE_SIZE;
        float frac = pos - (int)pos;

        return samples[index0] * (1.0f - frac) + samples[index1] * frac;
    }

    Wavetable() {
        initSine();
    }
};

// Wavetable bank (collection of wavetables with morphing)
struct WavetableBank {
    static constexpr int MAX_TABLES = 16;
    std::vector<Wavetable> tables;
    std::string name = "Bank";

    // Add a wavetable
    void addTable(const Wavetable& table) {
        if (tables.size() < MAX_TABLES) {
            tables.push_back(table);
        }
    }

    // Get interpolated sample between two wavetables
    float lookupMorph(float phase, float morphPosition) const {
        if (tables.empty()) return 0.0f;
        if (tables.size() == 1) return tables[0].lookup(phase);

        // morphPosition: 0.0 = first table, 1.0 = last table
        float tableIndex = morphPosition * (tables.size() - 1);
        int index0 = std::clamp((int)tableIndex, 0, (int)tables.size() - 1);
        int index1 = std::clamp(index0 + 1, 0, (int)tables.size() - 1);
        float frac = tableIndex - index0;

        float sample0 = tables[index0].lookup(phase);
        float sample1 = tables[index1].lookup(phase);

        return sample0 * (1.0f - frac) + sample1 * frac;
    }

    void clear() {
        tables.clear();
    }

    WavetableBank() {
        // Add some default wavetables
        Wavetable sine, saw, square;
        sine.initSine();
        saw.initSaw();
        square.initSquare();
        tables = { sine, saw, square };
        name = "Default";
    }
};


// ============================================================================
// Take lanes
//
// The structures live here because Project holds them; the operations that
// work on them - swiping, flattening, validation - are in TakeLanes.h, which
// needs a complete Project and therefore cannot be included from this file.
// ============================================================================
struct Take {
    int sampleId = -1;
    float startBeat = 0.0f;       // where the pass began on the timeline
    float lengthBeats = 4.0f;
    std::string name;
    bool muted = false;           // kept, but never chosen

    float endBeat() const { return startBeat + lengthBeats; }
    bool covers(float beat) const {
        return beat >= startBeat && beat < endBeat();
    }
};

/*
 * A span of the timeline, and which take wins over it.
 *
 * Segments are kept sorted, non-overlapping and gapless. That invariant is
 * what makes the whole thing tractable: "which take is playing here" is a
 * single lookup, and a swipe only has to maintain it rather than reason
 * about arbitrary overlaps.
 */
struct CompSegment {
    float startBeat = 0.0f;
    float endBeat = 4.0f;
    int takeIndex = -1;           // -1 is a deliberate hole

    float length() const { return endBeat - startBeat; }
};

struct CompGroup {
    static constexpr int MAX_TAKES = 32;

    int channelIndex = 0;
    std::string name = "Comp";
    std::vector<Take> takes;
    std::vector<CompSegment> segments;

    // Where the comp lives on the timeline. Takes outside it are kept but
    // never sound, which is what makes punch recording safe.
    float startBeat = 0.0f;
    float lengthBeats = 16.0f;

    float endBeat() const { return startBeat + lengthBeats; }
};

// ============================================================================
// Automation System
// ============================================================================

// What parameter to automate
enum class AutomationTarget : uint8_t {
    Volume,
    Pan,
    FilterCutoff,
    FilterResonance,
    ReverbMix,
    DelayMix,
    ChorusMix,
    DistortionDrive,
    BitcrusherDepth,
    PhaserRate,
    FlangerRate,
    TremoloRate,
    CompressorThreshold,
    EQLow,
    EQMid,
    EQHigh,
    StereoWidth,
    TapeDrive,
    // Master channel parameters
    MasterVolume,
    MasterEQLow,
    MasterEQMid,
    MasterEQHigh,
    MasterCompThreshold,
    MasterLimiterCeiling
};

// Interpolation type for automation curves
enum class InterpolationType : uint8_t {
    Linear,     // Straight lines between points
    Bezier,     // Smooth curves with curvature control
    Step        // Hold value until next point (stairs)
};

// Single point on an automation curve
struct AutomationPoint {
    float time = 0.0f;          // Beat position
    float value = 0.0f;         // Normalized value (0.0 to 1.0)
    float curvature = 0.0f;     // Bezier curve tension (-1.0 to 1.0, 0 = linear)

    bool operator<(const AutomationPoint& other) const {
        return time < other.time;
    }
};

// Automation curve (collection of points with interpolation)
struct AutomationCurve {
    std::vector<AutomationPoint> points;
    InterpolationType interpolation = InterpolationType::Linear;

    // Evaluate curve at a given time (returns normalized 0.0-1.0 value)
    float evaluate(float time) const {
        if (points.empty()) return 0.5f; // Default to midpoint

        // Before first point - return first value
        if (time <= points.front().time) {
            return points.front().value;
        }

        // After last point - return last value
        if (time >= points.back().time) {
            return points.back().value;
        }

        // Find the two points we're between
        for (size_t i = 0; i < points.size() - 1; i++) {
            const auto& p1 = points[i];
            const auto& p2 = points[i + 1];

            if (time >= p1.time && time <= p2.time) {
                // Calculate normalized position between points (0.0 to 1.0)
                float t = (time - p1.time) / (p2.time - p1.time);

                switch (interpolation) {
                    case InterpolationType::Step:
                        return p1.value;

                    case InterpolationType::Linear:
                        return p1.value + t * (p2.value - p1.value);

                    case InterpolationType::Bezier: {
                        // Hermite interpolation with curvature control
                        // curvature affects the "tightness" of the curve
                        float tension = (p1.curvature + p2.curvature) * 0.5f;

                        // Ease-in-out cubic interpolation modified by tension
                        float eased = t * t * (3.0f - 2.0f * t); // Smoothstep
                        float linear = t;
                        float blend = (1.0f + tension) * 0.5f; // 0 to 1
                        float finalT = linear * (1.0f - blend) + eased * blend;

                        return p1.value + finalT * (p2.value - p1.value);
                    }
                }
            }
        }

        return 0.5f; // Shouldn't reach here
    }

    // Add a point (automatically sorts by time)
    void addPoint(float time, float value, float curvature = 0.0f) {
        AutomationPoint point;
        point.time = time;
        point.value = std::clamp(value, 0.0f, 1.0f);
        point.curvature = std::clamp(curvature, -1.0f, 1.0f);

        points.push_back(point);
        std::sort(points.begin(), points.end());
    }

    // Remove a point by index
    void removePoint(int index) {
        if (index >= 0 && index < (int)points.size()) {
            points.erase(points.begin() + index);
        }
    }

    // Move a point (maintains sort order)
    void movePoint(int index, float time, float value) {
        if (index >= 0 && index < (int)points.size()) {
            points[index].time = time;
            points[index].value = std::clamp(value, 0.0f, 1.0f);
            std::sort(points.begin(), points.end());
        }
    }

    // Set curvature for a point
    void setCurvature(int index, float curvature) {
        if (index >= 0 && index < (int)points.size()) {
            points[index].curvature = std::clamp(curvature, -1.0f, 1.0f);
        }
    }

    // Clear all points
    void clear() {
        points.clear();
    }

    // Find point at or near a time (within tolerance)
    int findPointAt(float time, float tolerance = 0.1f) const {
        for (int i = 0; i < (int)points.size(); i++) {
            if (std::abs(points[i].time - time) <= tolerance) {
                return i;
            }
        }
        return -1;
    }
};

// Automation lane (binds a curve to a specific parameter on a channel)
struct AutomationLane {
    std::string name = "Automation";
    int channelIndex = 0;               // Which channel (-1 for master)
    AutomationTarget target = AutomationTarget::Volume;
    AutomationCurve curve;
    bool enabled = true;
    bool visible = true;                // Show in UI
    uint32_t color = 0xFF88AAFF;        // Visual color

    // Get display name for this automation
    std::string getDisplayName() const {
        std::string targetName;
        switch (target) {
            case AutomationTarget::Volume: targetName = "Volume"; break;
            case AutomationTarget::Pan: targetName = "Pan"; break;
            case AutomationTarget::FilterCutoff: targetName = "Filter Cutoff"; break;
            case AutomationTarget::FilterResonance: targetName = "Filter Resonance"; break;
            case AutomationTarget::ReverbMix: targetName = "Reverb Mix"; break;
            case AutomationTarget::DelayMix: targetName = "Delay Mix"; break;
            case AutomationTarget::ChorusMix: targetName = "Chorus Mix"; break;
            case AutomationTarget::DistortionDrive: targetName = "Distortion Drive"; break;
            case AutomationTarget::BitcrusherDepth: targetName = "Bitcrusher"; break;
            case AutomationTarget::PhaserRate: targetName = "Phaser Rate"; break;
            case AutomationTarget::FlangerRate: targetName = "Flanger Rate"; break;
            case AutomationTarget::TremoloRate: targetName = "Tremolo Rate"; break;
            case AutomationTarget::CompressorThreshold: targetName = "Compressor"; break;
            case AutomationTarget::EQLow: targetName = "EQ Low"; break;
            case AutomationTarget::EQMid: targetName = "EQ Mid"; break;
            case AutomationTarget::EQHigh: targetName = "EQ High"; break;
            case AutomationTarget::StereoWidth: targetName = "Stereo Width"; break;
            case AutomationTarget::TapeDrive: targetName = "Tape Drive"; break;
            case AutomationTarget::MasterVolume: targetName = "Master Volume"; break;
            case AutomationTarget::MasterEQLow: targetName = "Master EQ Low"; break;
            case AutomationTarget::MasterEQMid: targetName = "Master EQ Mid"; break;
            case AutomationTarget::MasterEQHigh: targetName = "Master EQ High"; break;
            case AutomationTarget::MasterCompThreshold: targetName = "Master Comp"; break;
            case AutomationTarget::MasterLimiterCeiling: targetName = "Master Limiter"; break;
            default: targetName = "Unknown"; break;
        }

        if (channelIndex < 0) {
            return "Master - " + targetName;
        } else {
            return "Ch" + std::to_string(channelIndex + 1) + " - " + targetName;
        }
    }
};

// ============================================================================
// Project State
// ============================================================================
/*
 * An aux bus: sums the sends pointed at it, runs its own insert rack, and
 * feeds the master or another bus.
 *
 * `strip` is a full ChannelConfig, which is more than a bus needs - the
 * oscillator, envelope and macro fields are meaningless here. It is
 * deliberate: it buys the entire Task A insert rack, the serialization
 * machinery and the config-to-chain sync with no second code path that
 * could drift from the channel one.
 */
struct AuxBusConfig {
    std::string name = "Aux";
    float volume = 0.8f;
    float pan = 0.0f;
    bool muted = false;

    // -1 = master. Anything else is another aux index, which is why
    // Routing.h exists.
    int output = -1;

    ChannelConfig strip;
};

struct Project {
    // 32, not 8. Eight is the 2A03's constraint and it was being imposed
    // on sample and supersaw channels no chip ever had. The array is still
    // fixed-capacity, so the audio thread allocates nothing either way -
    // what changed is how many of it are used.
    static constexpr int MAX_CHANNELS = 32;

    // The number a 2A03-shaped project is allowed. Kept as a named constant
    // because it appears in the chip-authentic rule and in the migrator.
    static constexpr int CHIP_CHANNELS = 8;
    static constexpr int MAX_PATTERNS = 64;

    std::string name = "Untitled";
    float bpm = 120.0f;
    int beatsPerMeasure = 4;
    float masterVolume = 0.7f;      // Master volume (0.0 to 1.0)

    // Master Effects Settings (for final mastering)
    bool masterEQEnabled = false;
    float masterEQLowGain = 0.0f;       // dB (-12 to +12)
    float masterEQMidGain = 0.0f;
    float masterEQHighGain = 0.0f;

    bool masterCompressorEnabled = false;
    float masterCompThreshold = -12.0f;  // dB
    float masterCompRatio = 2.5f;
    float masterCompAttack = 0.01f;      // seconds
    float masterCompRelease = 0.1f;
    float masterCompMakeup = 2.0f;       // dB

    bool masterLimiterEnabled = true;    // Usually always on
    float masterLimiterCeiling = -0.3f;  // dB
    float masterLimiterRelease = 0.05f;  // seconds

    // Chip-accurate output. Both off by default: most channels here host a
    // supersaw or a sample, which a 2A03 never had, so neither setting is
    // something to impose on an existing project.
    // Hold the project to the eight channels a 2A03 had.
    //
    // A per-project switch rather than a preference, because whether a piece
    // is chip-legal is a property of the piece. It is enforced in the audio
    // thread, not just the UI - so it is a real constraint and it also costs
    // nothing to leave on.
    bool chipAuthentic = false;

    bool chipMixEnabled = false;      // non-linear pulse / triangle-noise DACs
    bool chipFilterEnabled = false;   // the console's own output filters
    bool chipFilterFamicom = false;   // Famicom voicing rather than NES

    // Swing/groove settings
    float swing = 0.0f;             // Swing amount: 0.0 = no swing, 1.0 = max swing (triplet feel)
    float swingGrid = 0.5f;         // Grid division for swing (0.5 = 8th notes, 0.25 = 16th notes)
    bool humanize = false;          // Add random timing variation
    float humanizeAmount = 0.02f;   // Humanize timing variation (beats)
    float humanizeVelocity = 0.1f;  // Humanize velocity variation (0.0 to 1.0)

    std::array<ChannelConfig, MAX_CHANNELS> channels;

    // Audio the project references. Not serialized as data - the file
    // carries paths and the pool reloads them, which is how every DAW does
    // it and keeps a .ctp a text file you can read.
    SamplePool samplePool;

    // Paths the loader could not find. Surfaced rather than swallowed: a
    // clip playing silence with no explanation is worse than one that says
    // which file moved.
    std::vector<std::string> missingSamples;

    /*
     * Tempo and meter changes through the song.
     *
     * bpm and beatsPerMeasure above stay exactly what they were - the value
     * in force from beat 0 until the first change - so every existing
     * control, every existing file and every path that has not been taught
     * about the map keeps working. An empty map is a project at one tempo,
     * which is every project written before this, and it writes nothing at
     * all to the file.
     */
    TempoMap tempoMap;

    // Named points and named spans. Not read by the audio thread, which is
    // why they can afford to carry strings.
    std::vector<Marker> markers;
    std::vector<Region> regions;

    /*
     * Take lanes and their comps.
     *
     * Editing state only: the mixer never reads these. A comp is flattened
     * into ordinary audio clips on the arrangement, and those are what
     * plays - so comping reuses the whole tested audio-clip path rather
     * than duplicating trimming, fades and sample-rate conversion inside a
     * second one.
     */
    std::vector<CompGroup> compGroups;

    // Convenience wrappers, so call sites do not have to remember to pass
    // the base tempo and meter every time.
    float bpmAt(float beat) const { return tempoMap.bpmAtBeat(beat, bpm); }
    float beatToSeconds(float beat) const { return tempoMap.beatToSeconds(beat, bpm); }
    float secondsToBeat(float seconds) const { return tempoMap.secondsToBeat(seconds, bpm); }
    int barAt(float beat) const { return tempoMap.barAtBeat(beat, beatsPerMeasure); }
    float beatOfBar(int bar) const { return tempoMap.beatOfBar(bar, beatsPerMeasure); }
    MeterChange meterAt(float beat) const {
        return tempoMap.meterAtBeat(beat, beatsPerMeasure);
    }

    // How many channels this project actually uses. Everything that walks
    // channels - the mixer, the tracker, the arrangement, and the audio mix
    // itself - asks this rather than MAX_CHANNELS.
    int activeChannelCount() const {
        return chipAuthentic ? CHIP_CHANNELS : MAX_CHANNELS;
    }

    // Four aux buses. Empty and routed to the master by default, so a
    // project that never touches them mixes exactly as it did before.
    static constexpr int MAX_AUX_BUSES = 4;
    std::array<AuxBusConfig, MAX_AUX_BUSES> auxBuses;
    std::vector<Pattern> patterns;
    std::vector<Clip> arrangement;
    std::vector<AutomationLane> automationLanes;  // Parameter automation
    std::vector<WavetableBank> wavetableBanks;    // Custom wavetables

    float songLength = 64.0f;   // Total length in beats

    Project() {
        // Initialize default channels
        channels[0] = {"Pulse 1", {OscillatorType::Pulse}, {}, 0.8f, -0.3f};
        channels[0].oscillator.pulseWidth = 0.5f;

        channels[1] = {"Pulse 2", {OscillatorType::Pulse}, {}, 0.8f, 0.3f};
        channels[1].oscillator.pulseWidth = 0.25f;

        channels[2] = {"Triangle", {OscillatorType::Triangle}, {}, 0.8f, 0.0f};

        channels[3] = {"Sawtooth", {OscillatorType::Sawtooth}, {}, 0.6f, -0.5f};

        channels[4] = {"Sine", {OscillatorType::Sine}, {}, 0.7f, 0.5f};

        channels[5] = {"Noise", {OscillatorType::Noise}, {}, 0.5f, 0.0f};

        channels[6] = {"Pulse 3", {OscillatorType::Pulse}, {}, 0.7f, -0.2f};
        channels[6].oscillator.pulseWidth = 0.125f;

        channels[7] = {"Custom", {OscillatorType::Triangle}, {}, 0.7f, 0.2f};
        channels[7].oscillator.triangleSlope = 0.3f;

        // The channels past the original eight. Plain defaults and a plain
        // name - these are the ones a 2A03 never had, so giving them chip
        // voicings would be a lie about what they are for.
        for (int i = CHIP_CHANNELS; i < MAX_CHANNELS; ++i) {
            channels[static_cast<size_t>(i)].name =
                "Channel " + std::to_string(i + 1);
        }

        // Create one default pattern
        patterns.push_back(Pattern());
        patterns[0].name = "Pattern 1";

        // Initialize default wavetable bank
        wavetableBanks.push_back(WavetableBank());
    }
};

// ============================================================================
// Playback State
// ============================================================================
struct PlaybackState {
    bool isPlaying = false;
    bool isRecording = false;
    bool loop = true;

    // loopStart/loopEnd existed from the beginning but nothing ever set them
    // and the playback loop never read loopEnd - it wrapped at the end of the
    // content instead. loopRangeActive is what distinguishes "the user drew a
    // range on the ruler" from "just repeat whatever is there", which is the
    // difference between iterating on two bars and replaying the whole song.
    bool loopRangeActive = false;
    float loopStart = 0.0f;
    float loopEnd = 16.0f;
    float currentBeat = 0.0f;
    float currentTime = 0.0f;   // In seconds
};

// ============================================================================
// UI State
// ============================================================================
enum class ViewMode : uint8_t {
    PianoRoll,
    Tracker,
    Arrangement,
    Mixer,
    PadController   // Live performance pad controller (MPC-style)
};

// ============================================================================
// Arpeggiator Rate (for Pad Controller)
// ============================================================================
enum class ArpRate : uint8_t {
    Quarter,        // 1/4 notes
    Eighth,         // 1/8 notes
    Sixteenth,      // 1/16 notes
    ThirtySecond    // 1/32 notes
};

// Forward declaration for ArpMode (defined in Effects.h)
// We use int here to avoid circular dependency
// ============================================================================
// Recorded Note Event (for live recording)
// ============================================================================
struct RecordedNoteEvent {
    int pitch = 60;                     // MIDI note
    float velocity = 1.0f;              // 0.0 to 1.0
    float timestamp = 0.0f;             // Beat position when recorded
    float duration = 0.25f;             // Duration in beats
    OscillatorType oscillatorType = OscillatorType::Pulse;
    bool isNoteOn = true;               // true = note on, false = note off
};

// ============================================================================
// Pad Assignment (what sound each pad plays)
// ============================================================================
struct PadAssignment {
    OscillatorType oscillatorType = OscillatorType::Kick;
    int midiNote = 60;                  // Base MIDI note for this pad
    std::string label = "Kick";         // Display name
    uint32_t color = 0xFF4488FF;        // Pad color
};

// ============================================================================
// Pad Controller Genre Presets
// ============================================================================
enum class PadGenre : uint8_t {
    General,    // Default - mixed drums/synths
    Synthwave,  // 80s retro synthwave
    Techno,     // Electronic/techno
    HipHop,     // Hip hop/trap
    DrumMachine // Pure drum kit
};

// ============================================================================
// Pad Controller State
// ============================================================================
struct PadControllerState {
    static constexpr int NUM_PADS = 16;         // 4x4 grid
    static constexpr int NUM_KNOBS = 8;         // 8 parameter knobs
    static constexpr int NUM_KEYS = 25;         // 2 octave keyboard + 1

    // Current genre preset
    PadGenre currentGenre = PadGenre::General;

    // Pad banks (A = drums, B = synths)
    int currentBank = 0;                         // 0 = Bank A, 1 = Bank B
    std::array<PadAssignment, NUM_PADS> bankA;   // Drum pads
    std::array<PadAssignment, NUM_PADS> bankB;   // Synth pads

    // Active pad states (for visual feedback)
    std::array<bool, NUM_PADS> padActive = {};
    std::array<float, NUM_PADS> padVelocity = {};

    // Parameter knobs (0.0 to 1.0)
    // Knob 0: Attack, 1: Decay, 2: Sustain, 3: Release
    // Knob 4: Pulse Width, 5: Detune, 6: Filter Cutoff, 7: Volume
    std::array<float, NUM_KNOBS> knobValues = {
        0.01f,  // Attack (fast default)
        0.1f,   // Decay
        0.7f,   // Sustain
        0.2f,   // Release
        0.5f,   // Pulse Width
        0.0f,   // Detune (center)
        1.0f,   // Filter Cutoff (fully open)
        0.8f    // Volume
    };

    // Virtual keyboard state
    int keyboardOctave = 4;                      // Current octave (C4 default)
    std::array<bool, NUM_KEYS> keyActive = {};   // Which keys are pressed
    OscillatorType keyboardSound = OscillatorType::SynthLead;  // Sound for keyboard

    // Arpeggiator (arpMode uses int to match ArpMode from Effects.h: 0=Up,1=Down,2=UpDown,3=Random)
    bool arpEnabled = false;
    int arpMode = 0;  // 0=Up, 1=Down, 2=UpDown, 3=Random (matches ArpMode in Effects.h)
    ArpRate arpRate = ArpRate::Sixteenth;
    int arpOctaves = 1;                          // 1-4 octaves
    std::vector<int> arpHeldNotes;               // Notes currently held for arp
    int arpCurrentIndex = 0;                     // Current position in arp sequence
    float arpLastTriggerBeat = 0.0f;             // When last arp note triggered

    // Recording
    bool recordArmed = false;                    // Ready to record
    bool isRecording = false;                    // Currently recording
    bool quantizeEnabled = true;                 // Snap to grid
    float quantizeResolution = 0.25f;            // 1/16th note default
    float recordStartBeat = 0.0f;                // When recording started
    std::vector<RecordedNoteEvent> recordedEvents;  // Captured events

    // Real-time waveform display buffer
    static constexpr int WAVEFORM_SAMPLES = 256;
    std::array<float, WAVEFORM_SAMPLES> waveformBuffer = {};
    int waveformWritePos = 0;

    // Initialize default pad assignments
    void initDefaults() {
        // Bank A: Drums
        bankA[0]  = { OscillatorType::Kick,     36, "Kick",     0xFF3366FF };
        bankA[1]  = { OscillatorType::Kick808,  36, "808 Kick", 0xFF3388FF };
        bankA[2]  = { OscillatorType::KickHard, 36, "Hard",     0xFF33AAFF };
        bankA[3]  = { OscillatorType::KickSoft, 36, "Soft",     0xFF33CCFF };
        bankA[4]  = { OscillatorType::Snare,    38, "Snare",    0xFFFF6633 };
        bankA[5]  = { OscillatorType::Snare808, 38, "808 Snr",  0xFFFF8833 };
        bankA[6]  = { OscillatorType::SnareRim, 37, "Rim",      0xFFFFAA33 };
        bankA[7]  = { OscillatorType::Clap,     39, "Clap",     0xFFFFCC33 };
        bankA[8]  = { OscillatorType::HiHat,    42, "HH Cls",   0xFF33FF66 };
        bankA[9]  = { OscillatorType::HiHatOpen,46, "HH Opn",   0xFF33FF88 };
        bankA[10] = { OscillatorType::HiHatPedal,44,"HH Pdl",   0xFF33FFAA };
        bankA[11] = { OscillatorType::Cowbell,  56, "Cowbell",  0xFF33FFCC };
        bankA[12] = { OscillatorType::Tom,      45, "Tom",      0xFFFF33FF };
        bankA[13] = { OscillatorType::TomLow,   41, "Low Tom",  0xFFCC33FF };
        bankA[14] = { OscillatorType::Crash,    49, "Crash",    0xFF9933FF };
        bankA[15] = { OscillatorType::Ride,     51, "Ride",     0xFF6633FF };

        // Bank B: Synths (same note = C4 for each, user plays keyboard)
        bankB[0]  = { OscillatorType::SynthLead,   60, "Lead",    0xFF66FFFF };
        bankB[1]  = { OscillatorType::SynthPad,    60, "Pad",     0xFF88FFFF };
        bankB[2]  = { OscillatorType::SynthBass,   48, "Bass",    0xFFAAFFFF };
        bankB[3]  = { OscillatorType::SynthPluck,  60, "Pluck",   0xFFCCFFFF };
        bankB[4]  = { OscillatorType::SynthArp,    60, "Arp",     0xFFFFFF66 };
        bankB[5]  = { OscillatorType::SynthOrgan,  60, "Organ",   0xFFFFFF88 };
        bankB[6]  = { OscillatorType::SynthStrings,60, "Strings", 0xFFFFFFAA };
        bankB[7]  = { OscillatorType::SynthBrass,  60, "Brass",   0xFFFFFFCC };
        bankB[8]  = { OscillatorType::SynthwaveLead,60,"SW Lead", 0xFFFF66FF };
        bankB[9]  = { OscillatorType::SynthwaveBass,48,"SW Bass", 0xFFFF88CC };
        bankB[10] = { OscillatorType::SynthwavePad, 60,"SW Pad",  0xFFFFAACC };
        bankB[11] = { OscillatorType::SynthwaveArp, 60,"SW Arp",  0xFFFFCCCC };
        bankB[12] = { OscillatorType::Pulse,       60, "Pulse",   0xFF66FF66 };
        bankB[13] = { OscillatorType::Sawtooth,    60, "Saw",     0xFF88FF66 };
        bankB[14] = { OscillatorType::Triangle,    60, "Tri",     0xFFAAFF66 };
        bankB[15] = { OscillatorType::Supersaw,    60, "Super",   0xFFCCFF66 };
    }

    PadControllerState() {
        initDefaults();
    }

    // Get current bank's pad assignments
    const std::array<PadAssignment, NUM_PADS>& getCurrentBank() const {
        return currentBank == 0 ? bankA : bankB;
    }

    // Quantize a beat position to grid
    float quantizeBeat(float beat) const {
        if (!quantizeEnabled || quantizeResolution <= 0.0f) return beat;
        return std::round(beat / quantizeResolution) * quantizeResolution;
    }

    // Load genre-specific preset
    void loadGenrePreset(PadGenre genre) {
        currentGenre = genre;

        switch (genre) {
            case PadGenre::Synthwave:
                loadSynthwavePreset();
                break;
            case PadGenre::Techno:
                loadTechnoPreset();
                break;
            case PadGenre::HipHop:
                loadHipHopPreset();
                break;
            case PadGenre::DrumMachine:
                loadDrumMachinePreset();
                break;
            default:
                initDefaults();
                break;
        }
    }

    void loadSynthwavePreset() {
        // Bank A: Synthwave-appropriate drums
        bankA[0]  = { OscillatorType::Kick808,    36, "808 Kick", 0xFFFF3366 };
        bankA[1]  = { OscillatorType::KickSoft,   36, "Soft Kck", 0xFFFF5588 };
        bankA[2]  = { OscillatorType::Snare808,   38, "808 Snr",  0xFFFF77AA };
        bankA[3]  = { OscillatorType::Clap,       39, "Clap",     0xFFFF99CC };
        bankA[4]  = { OscillatorType::HiHat,      42, "HH Cls",   0xFF33FFFF };
        bankA[5]  = { OscillatorType::HiHatOpen,  46, "HH Opn",   0xFF55FFFF };
        bankA[6]  = { OscillatorType::Tom,        45, "Tom",      0xFF77FFFF };
        bankA[7]  = { OscillatorType::Cowbell,    56, "Cowbell",  0xFF99FFFF };
        bankA[8]  = { OscillatorType::Ride,       51, "Ride",     0xFFFFFF33 };
        bankA[9]  = { OscillatorType::Crash,      49, "Crash",    0xFFFFFF55 };
        bankA[10] = { OscillatorType::TomLow,     41, "Low Tom",  0xFFFFFF77 };
        bankA[11] = { OscillatorType::TomHigh,    48, "Hi Tom",   0xFFFFFF99 };
        bankA[12] = { OscillatorType::SnareRim,   37, "Rim",      0xFFBBFFFF };
        bankA[13] = { OscillatorType::Clave,      75, "Clave",    0xFFDDFFFF };
        bankA[14] = { OscillatorType::Tambourine, 54, "Tamb",     0xFFFFBBFF };
        bankA[15] = { OscillatorType::Maracas,    70, "Shaker",   0xFFFFDDFF };

        // Bank B: Synthwave synths
        bankB[0]  = { OscillatorType::SynthwaveLead,  60, "SW Lead",  0xFFFF00FF };
        bankB[1]  = { OscillatorType::SynthwaveBass,  48, "SW Bass",  0xFFFF33CC };
        bankB[2]  = { OscillatorType::SynthwavePad,   60, "SW Pad",   0xFFFF6699 };
        bankB[3]  = { OscillatorType::SynthwaveArp,   60, "SW Arp",   0xFFFF9966 };
        bankB[4]  = { OscillatorType::SynthwaveChord, 60, "SW Chord", 0xFFFFCC33 };
        bankB[5]  = { OscillatorType::SynthwaveFM,    60, "SW FM",    0xFFFFFF00 };
        bankB[6]  = { OscillatorType::GatedPad,       60, "Gated",    0xFF00FFFF };
        bankB[7]  = { OscillatorType::PolySynth,      60, "Poly",     0xFF33FFCC };
        bankB[8]  = { OscillatorType::SyncLead,       60, "Sync",     0xFF66FF99 };
        bankB[9]  = { OscillatorType::SynthStrings,   60, "Strings",  0xFF99FF66 };
        bankB[10] = { OscillatorType::SynthBrass,     60, "Brass",    0xFFCCFF33 };
        bankB[11] = { OscillatorType::SynthBell,      60, "Bell",     0xFFFFFF00 };
        bankB[12] = { OscillatorType::Supersaw,       60, "Super",    0xFF00FF00 };
        bankB[13] = { OscillatorType::SynthLead,      60, "Lead",     0xFF33FF33 };
        bankB[14] = { OscillatorType::SynthPad,       60, "Pad",      0xFF66FF66 };
        bankB[15] = { OscillatorType::SynthPluck,     60, "Pluck",    0xFF99FF99 };

        keyboardSound = OscillatorType::SynthwaveLead;
    }

    void loadTechnoPreset() {
        // Bank A: Techno drums
        bankA[0]  = { OscillatorType::KickHard,   36, "Hard Kck", 0xFF00FF00 };
        bankA[1]  = { OscillatorType::Kick808,    36, "808 Kck",  0xFF33FF33 };
        bankA[2]  = { OscillatorType::Snare,      38, "Snare",    0xFF66FF66 };
        bankA[3]  = { OscillatorType::Clap,       39, "Clap",     0xFF99FF99 };
        bankA[4]  = { OscillatorType::HiHat,      42, "HH Cls",   0xFFFFFF00 };
        bankA[5]  = { OscillatorType::HiHatOpen,  46, "HH Opn",   0xFFFFFF33 };
        bankA[6]  = { OscillatorType::HiHatPedal, 44, "HH Pdl",   0xFFFFFF66 };
        bankA[7]  = { OscillatorType::SnareRim,   37, "Rim",      0xFFFFFF99 };
        bankA[8]  = { OscillatorType::Tom,        45, "Tom",      0xFF00FFFF };
        bankA[9]  = { OscillatorType::TomLow,     41, "Lo Tom",   0xFF33FFFF };
        bankA[10] = { OscillatorType::Ride,       51, "Ride",     0xFF66FFFF };
        bankA[11] = { OscillatorType::Crash,      49, "Crash",    0xFF99FFFF };
        bankA[12] = { OscillatorType::Cowbell,    56, "Cowbell",  0xFFFF00FF };
        bankA[13] = { OscillatorType::Clave,      75, "Clave",    0xFFFF33FF };
        bankA[14] = { OscillatorType::Conga,      63, "Conga",    0xFFFF66FF };
        bankA[15] = { OscillatorType::Tambourine, 54, "Tamb",     0xFFFF99FF };

        // Bank B: Techno synths
        bankB[0]  = { OscillatorType::AcidBass,    36, "Acid",     0xFF00FF00 };
        bankB[1]  = { OscillatorType::TechnoStab,  60, "Stab",     0xFF33FF00 };
        bankB[2]  = { OscillatorType::Hoover,      48, "Hoover",   0xFF66FF00 };
        bankB[3]  = { OscillatorType::RaveChord,   60, "Rave",     0xFF99FF00 };
        bankB[4]  = { OscillatorType::Reese,       36, "Reese",    0xFFCCFF00 };
        bankB[5]  = { OscillatorType::SynthLead,   60, "Lead",     0xFFFFFF00 };
        bankB[6]  = { OscillatorType::SynthArp,    60, "Arp",      0xFFFF0000 };
        bankB[7]  = { OscillatorType::GatedPad,    60, "Gated",    0xFFFF3300 };
        bankB[8]  = { OscillatorType::SynthBass,   36, "Bass",     0xFFFF6600 };
        bankB[9]  = { OscillatorType::PolySynth,   60, "Poly",     0xFFFF9900 };
        bankB[10] = { OscillatorType::SyncLead,    60, "Sync",     0xFFFFCC00 };
        bankB[11] = { OscillatorType::Supersaw,    60, "Super",    0xFFFFFF00 };
        bankB[12] = { OscillatorType::Sawtooth,    60, "Saw",      0xFF00FFFF };
        bankB[13] = { OscillatorType::Pulse,       60, "Pulse",    0xFF33FFFF };
        bankB[14] = { OscillatorType::Triangle,    60, "Tri",      0xFF66FFFF };
        bankB[15] = { OscillatorType::Noise,       60, "Noise",    0xFF99FFFF };

        keyboardSound = OscillatorType::AcidBass;
    }

    void loadHipHopPreset() {
        // Bank A: Hip hop drums
        bankA[0]  = { OscillatorType::Kick808,    36, "808 Kick", 0xFFFF0000 };
        bankA[1]  = { OscillatorType::KickSoft,   36, "Boom",     0xFFFF3333 };
        bankA[2]  = { OscillatorType::Snare808,   38, "808 Snr",  0xFFFF6666 };
        bankA[3]  = { OscillatorType::SnareRim,   37, "Rim",      0xFFFF9999 };
        bankA[4]  = { OscillatorType::Clap,       39, "Clap",     0xFFFFCCCC };
        bankA[5]  = { OscillatorType::HiHat,      42, "HH Cls",   0xFFFFFF00 };
        bankA[6]  = { OscillatorType::HiHatOpen,  46, "HH Opn",   0xFFFFFF33 };
        bankA[7]  = { OscillatorType::HiHatPedal, 44, "HH Pdl",   0xFFFFFF66 };
        bankA[8]  = { OscillatorType::Tom,        45, "Tom",      0xFF00FF00 };
        bankA[9]  = { OscillatorType::TomLow,     41, "Lo Tom",   0xFF33FF33 };
        bankA[10] = { OscillatorType::Cowbell,    56, "Cowbell",  0xFF66FF66 };
        bankA[11] = { OscillatorType::Conga,      63, "Conga",    0xFF99FF99 };
        bankA[12] = { OscillatorType::Crash,      49, "Crash",    0xFF00FFFF };
        bankA[13] = { OscillatorType::Ride,       51, "Ride",     0xFF33FFFF };
        bankA[14] = { OscillatorType::Tambourine, 54, "Tamb",     0xFF66FFFF };
        bankA[15] = { OscillatorType::Maracas,    70, "Shaker",   0xFF99FFFF };

        // Bank B: Hip hop synths/sounds
        bankB[0]  = { OscillatorType::SubBass808,  36, "Sub 808",  0xFFFF0000 };
        bankB[1]  = { OscillatorType::LoFiKeys,    60, "LoFi Key", 0xFFFF5500 };
        bankB[2]  = { OscillatorType::TrapLead,    60, "Trap Ld",  0xFFFFAA00 };
        bankB[3]  = { OscillatorType::SynthPluck,  60, "Pluck",    0xFFFFFF00 };
        bankB[4]  = { OscillatorType::VinylNoise,  60, "Vinyl",    0xFFAAFF00 };
        bankB[5]  = { OscillatorType::SynthBell,   60, "Bell",     0xFF55FF00 };
        bankB[6]  = { OscillatorType::SynthPad,    60, "Pad",      0xFF00FF00 };
        bankB[7]  = { OscillatorType::SynthStrings,60, "Strings",  0xFF00FF55 };
        bankB[8]  = { OscillatorType::SynthBrass,  60, "Brass",    0xFF00FFAA };
        bankB[9]  = { OscillatorType::SynthOrgan,  60, "Organ",    0xFF00FFFF };
        bankB[10] = { OscillatorType::SynthLead,   60, "Lead",     0xFF00AAFF };
        bankB[11] = { OscillatorType::SynthArp,    60, "Arp",      0xFF0055FF };
        bankB[12] = { OscillatorType::Sine,        60, "Sine",     0xFF0000FF };
        bankB[13] = { OscillatorType::Triangle,    60, "Tri",      0xFF5500FF };
        bankB[14] = { OscillatorType::Sawtooth,    60, "Saw",      0xFFAA00FF };
        bankB[15] = { OscillatorType::Pulse,       60, "Pulse",    0xFFFF00FF };

        keyboardSound = OscillatorType::SubBass808;
    }

    void loadDrumMachinePreset() {
        // Bank A: All drums (main kit)
        bankA[0]  = { OscillatorType::Kick,       36, "Kick",     0xFF0066FF };
        bankA[1]  = { OscillatorType::Kick808,    36, "808 Kck",  0xFF0088FF };
        bankA[2]  = { OscillatorType::KickHard,   36, "Hard",     0xFF00AAFF };
        bankA[3]  = { OscillatorType::KickSoft,   36, "Soft",     0xFF00CCFF };
        bankA[4]  = { OscillatorType::Snare,      38, "Snare",    0xFFFF6600 };
        bankA[5]  = { OscillatorType::Snare808,   38, "808 Snr",  0xFFFF8800 };
        bankA[6]  = { OscillatorType::SnareRim,   37, "Rim",      0xFFFFAA00 };
        bankA[7]  = { OscillatorType::Clap,       39, "Clap",     0xFFFFCC00 };
        bankA[8]  = { OscillatorType::HiHat,      42, "HH Cls",   0xFF00FF66 };
        bankA[9]  = { OscillatorType::HiHatOpen,  46, "HH Opn",   0xFF00FF88 };
        bankA[10] = { OscillatorType::HiHatPedal, 44, "HH Pdl",   0xFF00FFAA };
        bankA[11] = { OscillatorType::Ride,       51, "Ride",     0xFF00FFCC };
        bankA[12] = { OscillatorType::Tom,        45, "Tom",      0xFFFF00FF };
        bankA[13] = { OscillatorType::TomLow,     41, "Lo Tom",   0xFFCC00FF };
        bankA[14] = { OscillatorType::TomHigh,    48, "Hi Tom",   0xFF9900FF };
        bankA[15] = { OscillatorType::Crash,      49, "Crash",    0xFF6600FF };

        // Bank B: Percussion
        bankB[0]  = { OscillatorType::Cowbell,    56, "Cowbell",  0xFFFFFF00 };
        bankB[1]  = { OscillatorType::Clave,      75, "Clave",    0xFFFFCC00 };
        bankB[2]  = { OscillatorType::Conga,      63, "Conga",    0xFFFF9900 };
        bankB[3]  = { OscillatorType::Maracas,    70, "Maracas",  0xFFFF6600 };
        bankB[4]  = { OscillatorType::Tambourine, 54, "Tamb",     0xFFFF3300 };
        bankB[5]  = { OscillatorType::Ride,       59, "Ride 2",   0xFFFF0000 };
        bankB[6]  = { OscillatorType::Crash,      57, "Crash 2",  0xFFCC0000 };
        bankB[7]  = { OscillatorType::Tom,        50, "Tom 2",    0xFF990000 };
        bankB[8]  = { OscillatorType::Kick,       35, "Kick 2",   0xFF660000 };
        bankB[9]  = { OscillatorType::Snare,      40, "Snare 2",  0xFF00FFFF };
        bankB[10] = { OscillatorType::HiHat,      44, "HH 2",     0xFF00CCFF };
        bankB[11] = { OscillatorType::Clap,       75, "Clap 2",   0xFF0099FF };
        bankB[12] = { OscillatorType::Conga,      62, "Conga 2",  0xFF0066FF };
        bankB[13] = { OscillatorType::Cowbell,    76, "Bell 2",   0xFF0033FF };
        bankB[14] = { OscillatorType::TomLow,     43, "Floor",    0xFF0000FF };
        bankB[15] = { OscillatorType::TomHigh,    47, "Rack",     0xFF3300FF };

        keyboardSound = OscillatorType::SynthLead;
    }
};

// Visual themes
enum class Theme : uint8_t {
    Stock,          // Default dark theme
    Cyberpunk,      // Neon yellow, hot pink, electric blue
    Synthwave,      // 80s retro with neon pinks and purples
    Matrix,         // Green on black with falling code
    FrutigerAero,   // Glossy bubbles, glass effects, sky blue gradients
    Minimal,        // Clean flat design, subtle geometric patterns
    Vaporwave,      // Pink/cyan aesthetic, floating shapes
    RetroTerminal,  // Amber CRT phosphor, scanlines, vintage computer
    GameBoy,        // DMG four-shade green, dot-matrix grid
    Daylight,       // Light theme for working in a bright room

    // A sentinel, like every other enum here has. Without it the tests
    // carried a hand-written list of all ten, which is a list that goes
    // stale the first time someone adds a theme and forgets it.
    Count
};

// Piano roll edit modes
enum class PianoRollMode : uint8_t {
    Draw,       // Click to add notes
    Select,     // Click to select notes
    Erase       // Click to delete notes
};

struct UIState {
    ViewMode currentView = ViewMode::PianoRoll;
    int selectedChannel = 0;
    int selectedPattern = 0;

    // Grid snap. Was hardcoded to a 1/16 note in fourteen places, so triplets
    // - and therefore shuffle and 6/8 - were unwritable. Sixteenth reproduces
    // the old behaviour exactly, so nothing changes until the user asks.
    SnapDivision snapDivision = DEFAULT_SNAP;

    // Genre focus. Decides which palette sections and panels are put in
    // front of you; never removes anything. Everything is the default and
    // behaves exactly as the program did before.
    Genre genre = Genre::Everything;
    bool paletteShowEverything = false;

    // The one-line "what next" hint. On by default and dismissible; the
    // people it is for are the least likely to go looking for it in a menu.
    bool showNextStep = true;

    // Frames left to pull focus back to the main editor. Opening panels
    // (the genre's macro or wavetable editors) steals the centre tab, and
    // the score is where anyone starts - so the welcome and a returning
    // user's first frames push focus back. Counted in frames because the
    // dock layout needs a frame or two to settle first.
    int focusEditorFrames = 0;

    // Show the other channels' notes faintly behind the edited pattern.
    // Off by default: this audience self-selects for a quiet screen.
    bool showGhostNotes = false;

    // Loop range being dragged on the timeline ruler.
    bool isDraggingLoopRange = false;
    float loopDragAnchorBeat = 0.0f;
    // 1.75, not 1.0: at 1.0 a sixteenth note is ten pixels wide, too
    // narrow to grab or resize - the first real user hit this in minutes.
    float zoomX = 1.75f;
    float zoomY = 1.0f;
    float scrollX = 0.0f;
    float scrollY = 0.0f;

    // Piano roll
    int pianoRollOctaveOffset = 4;  // Middle C octave
    PianoRollMode pianoRollMode = PianoRollMode::Draw;  // Current edit mode (Draw is default)
    int selectedNoteIndex = -1;     // Currently selected note (-1 = none)
    std::vector<int> selectedNoteIndices;  // Multiple selected notes
    bool isDraggingNote = false;    // Dragging a note to move it
    bool isDraggingMultiple = false; // Dragging multiple selected notes
    bool isResizingNote = false;    // Resizing note duration
    bool isResizingMultiple = false; // Resizing multiple selected notes
    float dragStartBeat = 0.0f;     // Where drag started
    int dragStartPitch = 0;         // Original pitch when drag started
    float dragStartDuration = 0.0f; // Original duration when resize started
    std::vector<float> multiResizeStartDurations; // Original durations of selected notes for multi-resize
    float dragAnchorBeat = 0.0f;    // Anchor point for multi-drag (mouse position at start)
    int dragAnchorPitch = 0;        // Anchor pitch for multi-drag
    std::vector<std::pair<float, int>> multiDragOffsets; // Beat and pitch offsets from anchor for each selected note

    // Pending drag state (click-select without immediate drag)
    bool isPendingDrag = false;     // Clicked on note, waiting to see if user drags
    bool isPendingMultiDrag = false; // Clicked on multi-selection, waiting for drag
    float pendingDragStartX = 0.0f; // Screen X where click started
    float pendingDragStartY = 0.0f; // Screen Y where click started
    int pendingDragNoteIndex = -1;  // Note index for pending single drag
    static constexpr float DRAG_THRESHOLD = 5.0f; // Pixels before drag starts

    // Box selection
    bool isBoxSelecting = false;    // Currently drawing selection box
    float boxSelectStartX = 0.0f;   // Box start position (beat)
    float boxSelectStartY = 0.0f;   // Box start position (pitch as float)
    float boxSelectEndX = 0.0f;     // Box end position (beat)
    float boxSelectEndY = 0.0f;     // Box end position (pitch as float)

    // Paste preview (ghost notes following mouse)
    bool isPastePreviewing = false; // Currently showing paste preview
    float pastePreviewBeat = 0.0f;  // Where ghost notes are positioned (time)
    int pastePreviewPitch = 60;     // Base pitch for ghost notes

    // Tracker
    int trackerRowHighlight = 4;    // Highlight every N rows

    // Tracker editing. The view used to be read-only - and wrong, printing
    // the same note into all eight columns - so none of this existed.
    int trackerCursorRow = 0;
    int trackerCursorChannel = 0;
    bool trackerEditMode = false;   // Space toggles; off means navigate only
    int trackerEditStep = 1;        // rows the cursor advances after entry
    int trackerOctave = 4;          // base octave for keyboard note entry
    int trackerRowsPerBeat = 4;     // 4 = one row per 1/16 note
    bool trackerFollowPlayhead = true;

    // Selection
    bool hasSelection = false;
    float selectionStart = 0.0f;
    float selectionEnd = 0.0f;

    // File dialog
    std::string projectFilePath = "";

    // Visual theme
    Theme currentTheme = Theme::Stock;

    // Instrument macro editor (see MacroEditorUI.h)
    bool showMacroEditor = false;
    int macroEditorTab = 0;         // 0=volume, 1=arpeggio, 2=duty, 3=pitch

    // Optional panels. These used to be drawn unconditionally, which meant
    // four extra windows piled on top of the editor from the moment the app
    // opened. They are useful, but only when you have asked for them.
    bool showSpectrumAnalyzer = false;
    bool showMIDIInput = false;
    bool showAutomation = false;
    bool showWavetableEditor = false;

    // The browser and the shortcut list. Both start closed: they are places
    // you go looking for something, not things that should be in the way of
    // somebody who already knows what they are doing.
    bool showBrowser = false;
    bool showShortcuts = false;

    /*
     * Where per-user settings live - key bindings among them.
     *
     * Held here rather than found again at each call site because the
     * screenshot and test runs point it at a scratch directory, and a
     * rebind during a test must not write into the real one.
     */
    std::string settingsDirectory;

    // Workspace layout (see Layout.h). pendingLayoutFrames counts down; the
    // layout has to be applied after the windows exist, and for two frames
    // so size changes settle before anything is drawn at the new size.
    int currentWorkspace = 0;       // index into Workspace
    int pendingLayoutFrames = 0;

    // Pad Controller state
    PadControllerState padController;

    // Window auto-layout (for maximize/resize handling)
    float lastWindowWidth = 0.0f;
    float lastWindowHeight = 0.0f;
    bool needsLayoutUpdate = true;  // True on first frame and after significant resize
};

} // namespace ChiptuneTracker
