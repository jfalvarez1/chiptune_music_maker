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

namespace ChiptuneTracker {

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
    DembowSnare     // Tight clap-like snare for dembow (1-3kHz emphasis)
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
    bool noiseShortMode = false;    // NES short mode (more metallic)

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

    // Extended delay settings (genre effects)
    float delayMix = 0.2f;
    float delayTime = 0.25f;
    float delayFeedback = 0.3f;

    // Channel-level Echo (applies to all notes on this channel)
    bool echoEnabled = false;
    float echoTime = 0.25f;         // Echo delay time (seconds)
    float echoFeedback = 0.4f;      // Feedback amount (0.0-0.9)
    float echoMix = 0.3f;           // Wet/dry mix (0.0-1.0)

    // Channel Detune (for stereo widening/richness)
    float detuneCents = 0.0f;       // Fine detune (-100 to +100 cents)
};

// ============================================================================
// Arrangement Clip (Pattern placement on timeline)
// ============================================================================
struct Clip {
    int patternIndex = 0;       // Which pattern to play
    int channelIndex = 0;       // Which channel
    float startBeat = 0.0f;     // Position on timeline
    float lengthBeats = 16.0f;  // Length (can be different from pattern)

    // Visual
    uint32_t color = 0xFF4488FF;
};

// ============================================================================
// Project State
// ============================================================================
struct Project {
    static constexpr int MAX_CHANNELS = 8;
    static constexpr int MAX_PATTERNS = 64;

    std::string name = "Untitled";
    float bpm = 120.0f;
    int beatsPerMeasure = 4;
    float masterVolume = 0.7f;      // Master volume (0.0 to 1.0)

    // Swing/groove settings
    float swing = 0.0f;             // Swing amount: 0.0 = no swing, 1.0 = max swing (triplet feel)
    float swingGrid = 0.5f;         // Grid division for swing (0.5 = 8th notes, 0.25 = 16th notes)
    bool humanize = false;          // Add random timing variation
    float humanizeAmount = 0.02f;   // Humanize timing variation (beats)
    float humanizeVelocity = 0.1f;  // Humanize velocity variation (0.0 to 1.0)

    std::array<ChannelConfig, MAX_CHANNELS> channels;
    std::vector<Pattern> patterns;
    std::vector<Clip> arrangement;

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

        // Create one default pattern
        patterns.push_back(Pattern());
        patterns[0].name = "Pattern 1";
    }
};

// ============================================================================
// Playback State
// ============================================================================
struct PlaybackState {
    bool isPlaying = false;
    bool isRecording = false;
    bool loop = true;
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
    RetroTerminal   // Amber CRT phosphor, scanlines, vintage computer
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
    float zoomX = 1.0f;
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

    // Selection
    bool hasSelection = false;
    float selectionStart = 0.0f;
    float selectionEnd = 0.0f;

    // File dialog
    std::string projectFilePath = "";

    // Visual theme
    Theme currentTheme = Theme::Stock;

    // Pad Controller state
    PadControllerState padController;

    // Window auto-layout (for maximize/resize handling)
    float lastWindowWidth = 0.0f;
    float lastWindowHeight = 0.0f;
    bool needsLayoutUpdate = true;  // True on first frame and after significant resize
};

// ============================================================================
// Undo/Redo History
// ============================================================================
struct PatternSnapshot {
    std::vector<Note> notes;
    int patternIndex = 0;
};

struct UndoHistory {
    static constexpr int MAX_HISTORY = 50;
    std::vector<PatternSnapshot> undoStack;
    std::vector<PatternSnapshot> redoStack;

    void saveState(const Pattern& pattern, int patternIndex) {
        PatternSnapshot snapshot;
        snapshot.notes = pattern.notes;
        snapshot.patternIndex = patternIndex;
        undoStack.push_back(snapshot);
        if (undoStack.size() > MAX_HISTORY) {
            undoStack.erase(undoStack.begin());
        }
        // Clear redo stack when new action is performed
        redoStack.clear();
    }

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    PatternSnapshot undo(const Pattern& currentPattern, int currentPatternIndex) {
        if (undoStack.empty()) return { currentPattern.notes, currentPatternIndex };

        // Save current state to redo stack
        PatternSnapshot currentSnapshot;
        currentSnapshot.notes = currentPattern.notes;
        currentSnapshot.patternIndex = currentPatternIndex;
        redoStack.push_back(currentSnapshot);

        // Pop and return from undo stack
        PatternSnapshot snapshot = undoStack.back();
        undoStack.pop_back();
        return snapshot;
    }

    PatternSnapshot redo(const Pattern& currentPattern, int currentPatternIndex) {
        if (redoStack.empty()) return { currentPattern.notes, currentPatternIndex };

        // Save current state to undo stack
        PatternSnapshot currentSnapshot;
        currentSnapshot.notes = currentPattern.notes;
        currentSnapshot.patternIndex = currentPatternIndex;
        undoStack.push_back(currentSnapshot);

        // Pop and return from redo stack
        PatternSnapshot snapshot = redoStack.back();
        redoStack.pop_back();
        return snapshot;
    }

    void clear() {
        undoStack.clear();
        redoStack.clear();
    }
};

} // namespace ChiptuneTracker
