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
    Custom      // Wavetable
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
// Note Event
// ============================================================================
struct Note {
    int      pitch     = 60;        // MIDI note number (60 = C4)
    float    velocity  = 1.0f;      // 0.0 to 1.0
    float    startTime = 0.0f;      // Position in beats
    float    duration  = 1.0f;      // Duration in beats

    // Fade in/out (in beats, 0 = instant)
    float    fadeIn    = 0.0f;      // Fade in duration (beats)
    float    fadeOut   = 0.0f;      // Fade out duration (beats)

    // Per-note effects
    int      arpeggio  = 0;         // Chord offset pattern (0 = none)
    float    vibrato   = 0.0f;      // Vibrato depth
    float    slide     = 0.0f;      // Pitch slide amount

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
    Mixer
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
    PianoRollMode pianoRollMode = PianoRollMode::Select;  // Current edit mode
    int selectedNoteIndex = -1;     // Currently selected note (-1 = none)
    bool isDraggingNote = false;    // Dragging a note to move it
    bool isResizingNote = false;    // Resizing note duration
    float dragStartBeat = 0.0f;     // Where drag started
    int dragStartPitch = 0;         // Original pitch when drag started
    float dragStartDuration = 0.0f; // Original duration when resize started

    // Tracker
    int trackerRowHighlight = 4;    // Highlight every N rows

    // Selection
    bool hasSelection = false;
    float selectionStart = 0.0f;
    float selectionEnd = 0.0f;
};

} // namespace ChiptuneTracker
