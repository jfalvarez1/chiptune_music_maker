#pragma once

/*
 * ChiptuneTracker - UI Components
 *
 * ImGui-based views for the DAW:
 *   - Piano Roll Editor
 *   - Tracker View
 *   - Arrangement Timeline
 *   - Mixer
 *   - Effects Rack
 */

#include "imgui.h"
#include "Types.h"
#include "UndoHistory.h"
#include "VoicePanel.h"
#include "InstrumentPresets.h"
#include "SettingsAudit.h"
#include "TakeLanes.h"
#include "Browser.h"
#include "PluginHost.h"
#include "VoiceMode.h"
#include "Showcase.h"
#include "ChipInstruments.h"
#include "MIDIImport.h"
#include "Mastering.h"
#include "LoopRange.h"
#include "Snap.h"
#include "Scales.h"
#include "ChordID.h"
#include "ChipLegality.h"
#include "NoteTransforms.h"
#include "WaveTools.h"
#include "GhostNotes.h"
#include "TrackerGrid.h"
#include "Genres.h"
#include "Templates.h"
#include "NextStep.h"
#include "GenreKits.h"
#include "GroovePresets.h"
#include "Tutorial.h"
#include "Routing.h"
#include "Sequencer.h"
#include "Widgets.h"
#include "FileIO.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace ChiptuneTracker {

// Note names for display
const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

inline std::string noteToString(int midiNote) {
    int octave = midiNote / 12 - 1;
    int note = midiNote % 12;
    char buf[8];
    snprintf(buf, sizeof(buf), "%s%d", NOTE_NAMES[note], octave);
    return buf;
}

// Colors for channels
const ImU32 CHANNEL_COLORS[] = {
    IM_COL32(255, 100, 100, 255),  // Pulse 1 - Red
    IM_COL32(100, 255, 100, 255),  // Pulse 2 - Green
    IM_COL32(100, 100, 255, 255),  // Triangle - Blue
    IM_COL32(255, 255, 100, 255),  // Sawtooth - Yellow
    IM_COL32(255, 100, 255, 255),  // Sine - Magenta
    IM_COL32(100, 255, 255, 255),  // Noise - Cyan
    IM_COL32(255, 180, 100, 255),  // Pulse 3 - Orange
    IM_COL32(180, 100, 255, 255),  // Custom - Purple
};

inline constexpr int CHANNEL_COLOR_COUNT =
    static_cast<int>(sizeof(CHANNEL_COLORS) / sizeof(CHANNEL_COLORS[0]));

/*
 * The colour for a channel.
 *
 * There are eight colours and up to 32 channels, so they wrap. Indexing the
 * array directly was fine when the two numbers matched and became an
 * out-of-bounds read the moment the cap went up - undefined behaviour that
 * happens to look like a colour.
 *
 * Wrapping rather than inventing 24 more is deliberate: a 32-colour palette
 * that still satisfies the ten themes' contrast assertions is its own piece
 * of work, and a bad one would regress the themes.
 */
inline ImU32 channelColor(int channel) {
    if (channel < 0) channel = 0;
    return CHANNEL_COLORS[channel % CHANNEL_COLOR_COUNT];
}

// Linear blend between two packed colours. `t` = 0 keeps `a`, 1 gives `b`.
inline ImU32 blendColors(ImU32 a, ImU32 b, float t) {
    t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
    auto lerp8 = [&](int shift) {
        const int ca = (a >> shift) & 0xFF;
        const int cb = (b >> shift) & 0xFF;
        return static_cast<ImU32>(ca + static_cast<int>((cb - ca) * t)) & 0xFFu;
    };
    return (lerp8(IM_COL32_R_SHIFT) << IM_COL32_R_SHIFT) |
           (lerp8(IM_COL32_G_SHIFT) << IM_COL32_G_SHIFT) |
           (lerp8(IM_COL32_B_SHIFT) << IM_COL32_B_SHIFT) |
           (lerp8(IM_COL32_A_SHIFT) << IM_COL32_A_SHIFT);
}


// Global clipboard for copy/paste notes (stores relative positions)
static std::vector<Note> g_NoteClipboard;
static float g_ClipboardBaseTime = 0.0f;
static int g_ClipboardBasePitch = 60;

// Global drag state for oscillator type
static int g_DraggedOscillatorType = -1;

// Global state for selected palette item (for click-to-place)
static int g_SelectedPaletteItem = -1;  // -1 = none selected
static float g_SelectedDurationMult = 1.0f;  // Duration multiplier for drums (0.5 = short, 1.0 = normal, 2.0 = long)

// Palette category expansion state
static bool g_PaletteExpanded_Oscillators = false;
static bool g_PaletteExpanded_Synths = false;
static bool g_PaletteExpanded_Kicks = false;
static bool g_PaletteExpanded_Snares = false;
static bool g_PaletteExpanded_HiHats = false;
static bool g_PaletteExpanded_Toms = false;
static bool g_PaletteExpanded_Cymbals = false;
static bool g_PaletteExpanded_Percussion = false;
static bool g_PaletteExpanded_Reggaeton = false;
static bool g_PaletteExpanded_Recreations = false;
static bool g_PaletteExpanded_Engines = false;
static bool g_PaletteExpanded_Patterns = false;

// ============================================================================
// Pattern Templates - Pre-made drum patterns for different genres
// ============================================================================
struct PatternNote {
    float beat;         // Start beat (0-based)
    int pitch;          // MIDI note (for positioning in piano roll)
    OscillatorType osc; // Drum/sound type
    float duration;     // Note duration in beats
};

struct DrumPattern {
    const char* name;
    const char* description;
    const PatternNote* notes;
    int noteCount;
    int lengthBeats;    // Total pattern length
};

// Synthwave 4/4 beat - driving kick with open hats
static const PatternNote g_SynthwavePattern[] = {
    {0.0f, 36, OscillatorType::Kick808, 0.25f},
    {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 38, OscillatorType::Snare808, 0.25f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 36, OscillatorType::Kick808, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 38, OscillatorType::Snare808, 0.25f},
    {3.5f, 46, OscillatorType::HiHatOpen, 0.25f},
};

// Techno 4/4 - four on the floor with offbeat hats
static const PatternNote g_TechnoPattern[] = {
    {0.0f, 36, OscillatorType::Kick, 0.25f},
    {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 36, OscillatorType::Kick, 0.25f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 36, OscillatorType::Kick, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 36, OscillatorType::Kick, 0.25f},
    {3.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 39, OscillatorType::Clap, 0.25f},
    {3.0f, 39, OscillatorType::Clap, 0.25f},
};

// Hip Hop - boom bap style
static const PatternNote g_HipHopPattern[] = {
    {0.0f, 36, OscillatorType::Kick808, 0.5f},
    {0.75f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 38, OscillatorType::Snare808, 0.25f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.25f, 36, OscillatorType::Kick808, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 38, OscillatorType::Snare808, 0.25f},
    {3.5f, 42, OscillatorType::HiHat, 0.125f},
};

// House - classic house beat
static const PatternNote g_HousePattern[] = {
    {0.0f, 36, OscillatorType::Kick, 0.25f},
    {0.5f, 42, OscillatorType::HiHatOpen, 0.25f},
    {1.0f, 36, OscillatorType::Kick, 0.25f},
    {1.0f, 38, OscillatorType::Clap, 0.25f},
    {1.5f, 42, OscillatorType::HiHatOpen, 0.25f},
    {2.0f, 36, OscillatorType::Kick, 0.25f},
    {2.5f, 42, OscillatorType::HiHatOpen, 0.25f},
    {3.0f, 36, OscillatorType::Kick, 0.25f},
    {3.0f, 38, OscillatorType::Clap, 0.25f},
    {3.5f, 42, OscillatorType::HiHatOpen, 0.25f},
};

// Drum & Bass - breakbeat style
static const PatternNote g_DnBPattern[] = {
    {0.0f, 36, OscillatorType::KickHard, 0.25f},
    {0.25f, 42, OscillatorType::HiHat, 0.125f},
    {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {0.75f, 38, OscillatorType::SnareRim, 0.125f},
    {1.0f, 42, OscillatorType::HiHat, 0.125f},
    {1.25f, 36, OscillatorType::KickHard, 0.25f},
    {1.5f, 38, OscillatorType::Snare, 0.25f},
    {1.75f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 36, OscillatorType::KickHard, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.75f, 38, OscillatorType::SnareRim, 0.125f},
    {3.0f, 42, OscillatorType::HiHat, 0.125f},
    {3.25f, 36, OscillatorType::KickHard, 0.125f},
    {3.5f, 38, OscillatorType::Snare, 0.25f},
    {3.75f, 42, OscillatorType::HiHat, 0.125f},
};

// 8-bit / Chiptune style
static const PatternNote g_ChiptunePattern[] = {
    {0.0f, 36, OscillatorType::Kick, 0.25f},
    {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 38, OscillatorType::Snare, 0.25f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 36, OscillatorType::Kick, 0.25f},
    {2.25f, 36, OscillatorType::Kick, 0.125f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 38, OscillatorType::Snare, 0.25f},
    {3.5f, 42, OscillatorType::HiHat, 0.125f},
};

// Trap - 808 heavy with hi-hat rolls
static const PatternNote g_TrapPattern[] = {
    {0.0f, 36, OscillatorType::Kick808, 0.5f},
    {0.25f, 42, OscillatorType::HiHat, 0.0625f},
    {0.375f, 42, OscillatorType::HiHat, 0.0625f},
    {0.5f, 42, OscillatorType::HiHat, 0.0625f},
    {0.625f, 42, OscillatorType::HiHat, 0.0625f},
    {0.75f, 42, OscillatorType::HiHat, 0.0625f},
    {0.875f, 42, OscillatorType::HiHat, 0.0625f},
    {1.0f, 38, OscillatorType::Snare808, 0.25f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 36, OscillatorType::Kick808, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.0625f},
    {2.625f, 42, OscillatorType::HiHat, 0.0625f},
    {2.75f, 42, OscillatorType::HiHat, 0.0625f},
    {2.875f, 42, OscillatorType::HiHat, 0.0625f},
    {3.0f, 38, OscillatorType::Snare808, 0.25f},
    {3.25f, 36, OscillatorType::Kick808, 0.25f},
    {3.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.75f, 42, OscillatorType::HiHat, 0.125f},
};

// Simple Rock beat
static const PatternNote g_RockPattern[] = {
    {0.0f, 36, OscillatorType::Kick, 0.25f},
    {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 38, OscillatorType::Snare, 0.25f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 36, OscillatorType::Kick, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 38, OscillatorType::Snare, 0.25f},
    {3.5f, 42, OscillatorType::HiHat, 0.125f},
};

static const DrumPattern g_DrumPatterns[] = {
    {"Synthwave", "Driving 80s beat with 808 kick", g_SynthwavePattern, 8, 4},
    {"Techno", "Four on the floor with claps", g_TechnoPattern, 10, 4},
    {"Hip Hop", "Boom bap groove", g_HipHopPattern, 8, 4},
    {"House", "Classic house with open hats", g_HousePattern, 10, 4},
    {"Drum & Bass", "Fast breakbeat style", g_DnBPattern, 15, 4},
    {"Chiptune", "8-bit game style", g_ChiptunePattern, 9, 4},
    {"Trap", "808 heavy with hi-hat rolls", g_TrapPattern, 18, 4},
    {"Rock", "Simple rock beat", g_RockPattern, 8, 4},
};
static constexpr int g_NumDrumPatterns = sizeof(g_DrumPatterns) / sizeof(g_DrumPatterns[0]);

// Pattern preview state (ghost notes before placement)
static bool g_IsPatternPreviewing = false;
static int g_PreviewPatternIndex = -1;  // Which pattern is being previewed
static int g_PatternPreviewBasePitch = 36;  // Base pitch for the pattern (kick note)

// ============================================================================
// Chord Presets - Organized by Genre
// ============================================================================
// Chord intervals are semitones relative to root note (0 = root)
struct ChordPreset {
    const char* name;
    const char* description;
    const char* genre;
    int intervals[6];   // Up to 6 notes per chord (0 = unused)
    int noteCount;      // How many notes in this chord
    OscillatorType defaultOsc;  // Recommended oscillator for this genre
};

// Chord presets organized by genre
static const ChordPreset g_ChordPresets[] = {
    // ===== POP (5 chords) =====
    {"C Major", "Bright, happy (I)", "Pop", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthPad},
    {"G Major", "Uplifting (V)", "Pop", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthPad},
    {"Am", "Emotional (vi)", "Pop", {0, 3, 7, 0, 0, 0}, 3, OscillatorType::SynthPad},
    {"F Major", "Warm resolution (IV)", "Pop", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthPad},
    {"Dm7", "Smooth tension", "Pop", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::SynthPad},
    {"Cadd9", "Modern pop sound", "Pop", {0, 4, 7, 14, 0, 0}, 4, OscillatorType::SynthPad},

    // ===== JAZZ (6 chords) =====
    {"Cmaj7", "Smooth jazz (I)", "Jazz", {0, 4, 7, 11, 0, 0}, 4, OscillatorType::SynthOrgan},
    {"Dm9", "Extended tension (ii)", "Jazz", {0, 3, 7, 10, 14, 0}, 5, OscillatorType::SynthOrgan},
    {"G13", "Dominant funk", "Jazz", {0, 4, 7, 10, 14, 21}, 6, OscillatorType::SynthOrgan},
    {"Fm7", "Modal jazz", "Jazz", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::SynthOrgan},
    {"Bbmaj7", "Warm substitution", "Jazz", {0, 4, 7, 11, 0, 0}, 4, OscillatorType::SynthOrgan},
    {"Am7b5", "Half-diminished", "Jazz", {0, 3, 6, 10, 0, 0}, 4, OscillatorType::SynthOrgan},

    // ===== ROCK (5 chords) =====
    {"E5", "Power chord (root)", "Rock", {0, 7, 12, 0, 0, 0}, 3, OscillatorType::Sawtooth},
    {"A5", "Power chord (IV)", "Rock", {0, 7, 12, 0, 0, 0}, 3, OscillatorType::Sawtooth},
    {"D5", "Power chord", "Rock", {0, 7, 12, 0, 0, 0}, 3, OscillatorType::Sawtooth},
    {"G5", "Power chord", "Rock", {0, 7, 12, 0, 0, 0}, 3, OscillatorType::Sawtooth},
    {"B5", "Power chord (V)", "Rock", {0, 7, 12, 0, 0, 0}, 3, OscillatorType::Sawtooth},

    // ===== EDM / SYNTHWAVE (6 chords) =====
    {"Am", "Dark minor", "EDM", {0, 3, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveChord},
    {"F", "Anthemic major", "EDM", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveChord},
    {"C", "Bright drop", "EDM", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveChord},
    {"G", "Build up", "EDM", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveChord},
    {"Em7", "Chill vibes", "EDM", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::SynthwavePad},
    {"Dm", "Dark tension", "EDM", {0, 3, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveChord},

    // ===== HIP HOP (5 chords) =====
    {"Cm7", "Lo-fi chill", "HipHop", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::LoFiKeys},
    {"Fm9", "Soulful sample", "HipHop", {0, 3, 7, 10, 14, 0}, 5, OscillatorType::LoFiKeys},
    {"Bbmaj7", "Smooth keys", "HipHop", {0, 4, 7, 11, 0, 0}, 4, OscillatorType::LoFiKeys},
    {"Gm7", "Dark trap", "HipHop", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::SubBass808},
    {"Ebmaj9", "Neo-soul", "HipHop", {0, 4, 7, 11, 14, 0}, 5, OscillatorType::LoFiKeys},

    // ===== REGGAETON (5 chords) =====
    {"Am", "Dembow minor", "Reggaeton", {0, 3, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveChord},
    {"F", "Perreo major", "Reggaeton", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveChord},
    {"Dm", "Latin tension", "Reggaeton", {0, 3, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveChord},
    {"E", "Flamenco flavor", "Reggaeton", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthBrass},
    {"G", "Hook resolution", "Reggaeton", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveChord},

    // ===== SYNTHWAVE (20 chords - classic 80s synth sounds) =====
    // Basic triads with lush pads
    {"Fm", "Outrun minor", "Synthwave", {0, 3, 7, 0, 0, 0}, 3, OscillatorType::SynthwavePad},
    {"Cm", "Dark retro", "Synthwave", {0, 3, 7, 0, 0, 0}, 3, OscillatorType::SynthwavePad},
    {"Ab", "Dreamy 80s", "Synthwave", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthwavePad},
    {"Eb", "Sunset drive", "Synthwave", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthwavePad},
    {"Db", "Blade runner", "Synthwave", {0, 4, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveLead},
    // Minor 7ths - melancholic synthwave staple
    {"Am7", "Melancholic", "Synthwave", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::SynthwavePad},
    {"Dm7", "Dark wave", "Synthwave", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::SynthwavePad},
    {"Em7", "Ethereal", "Synthwave", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::SynthwaveChord},
    {"Bbm7", "Midnight city", "Synthwave", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::SynthwaveChord},
    {"Gm7", "Noir streets", "Synthwave", {0, 3, 7, 10, 0, 0}, 4, OscillatorType::SynthwavePad},
    // Major 7ths - lush and dreamy (Vangelis style)
    {"Fmaj7", "Vangelis lush", "Synthwave", {0, 4, 7, 11, 0, 0}, 4, OscillatorType::SynthwavePad},
    {"Cmaj7", "Bright pad", "Synthwave", {0, 4, 7, 11, 0, 0}, 4, OscillatorType::SynthwavePad},
    {"Dbmaj7", "Cinematic", "Synthwave", {0, 4, 7, 11, 0, 0}, 4, OscillatorType::SynthwaveChord},
    {"Ebmaj7", "Noir feel", "Synthwave", {0, 4, 7, 11, 0, 0}, 4, OscillatorType::SynthwaveChord},
    {"Abmaj7", "Endless summer", "Synthwave", {0, 4, 7, 11, 0, 0}, 4, OscillatorType::SynthwavePad},
    // Extended chords - 9ths for modern synthwave
    {"Fm9", "Neon nights", "Synthwave", {0, 3, 7, 10, 14, 0}, 5, OscillatorType::SynthwaveChord},
    {"Am9", "Tearful", "Synthwave", {0, 3, 7, 10, 14, 0}, 5, OscillatorType::SynthwavePad},
    {"Cadd9", "Modern drive", "Synthwave", {0, 4, 7, 14, 0, 0}, 4, OscillatorType::SynthwaveChord},
    // Suspended chords - tension and atmosphere
    {"Asus4", "Tension", "Synthwave", {0, 5, 7, 0, 0, 0}, 3, OscillatorType::SynthwaveLead},
    {"Dsus2", "Resolve", "Synthwave", {0, 2, 7, 0, 0, 0}, 3, OscillatorType::SynthwavePad},

    // ===== CHIPTUNE (8 chords) =====
    {"C Arp", "NES major arp", "Chiptune", {0, 4, 7, 12, 0, 0}, 4, OscillatorType::SynthChip},
    {"Am Arp", "NES minor arp", "Chiptune", {0, 3, 7, 12, 0, 0}, 4, OscillatorType::SynthChip},
    {"G Arp", "Dominant 8-bit", "Chiptune", {0, 4, 7, 12, 0, 0}, 4, OscillatorType::SynthChip},
    {"Em Arp", "Dark 8-bit", "Chiptune", {0, 3, 7, 12, 0, 0}, 4, OscillatorType::SynthChip},
    {"F Arp", "Warm 8-bit", "Chiptune", {0, 4, 7, 12, 0, 0}, 4, OscillatorType::SynthChip},
    {"C5", "NES power chord", "Chiptune", {0, 7, 12, 0, 0, 0}, 3, OscillatorType::Pulse},
    {"E5", "Rock 8-bit", "Chiptune", {0, 7, 12, 0, 0, 0}, 3, OscillatorType::Pulse},
    {"Dm", "Castlevania minor", "Chiptune", {0, 3, 7, 0, 0, 0}, 3, OscillatorType::Triangle},
};
static constexpr int g_NumChordPresets = sizeof(g_ChordPresets) / sizeof(g_ChordPresets[0]);

// Chord selection state
static int g_SelectedChordIndex = -1;  // -1 = no chord selected

// Chord palette expansion state by genre
static bool g_PaletteExpanded_Chords_Pop = false;
static bool g_PaletteExpanded_Chords_Jazz = false;
static bool g_PaletteExpanded_Chords_Rock = false;
static bool g_PaletteExpanded_Chords_EDM = false;
static bool g_PaletteExpanded_Chords_HipHop = false;
static bool g_PaletteExpanded_Chords_Reggaeton = false;
static bool g_PaletteExpanded_Chords_Synthwave = false;
static bool g_PaletteExpanded_Chords_Chiptune = false;

// ============================================================================
// Sample Tracks - Complete songs with melody, bass, and drums
// ============================================================================
struct TrackNote {
    float beat;         // Start beat (0-based)
    int pitch;          // MIDI note
    OscillatorType osc; // Instrument type
    float duration;     // Note duration in beats
    float velocity = 0.8f;    // Note velocity (0.0-1.0) for dynamics
    float vibrato = 0.0f;     // Vibrato depth in semitones (0 = none)
    float vibratoSpeed = 5.5f; // Vibrato speed in Hz
};

// Genre-specific effect presets for professional sound
struct GenreEffects {
    // Reverb
    bool reverbEnabled = false;
    float reverbMix = 0.3f;
    float reverbRoomSize = 0.7f;
    float reverbDamping = 0.4f;
    // Chorus
    bool chorusEnabled = false;
    float chorusMix = 0.3f;
    float chorusRate = 0.5f;
    float chorusDepth = 0.01f;
    // Delay
    bool delayEnabled = false;
    float delayMix = 0.2f;
    float delayTime = 0.25f;
    float delayFeedback = 0.3f;
    // Sidechain
    bool sidechainEnabled = false;
    float sidechainAmount = 0.6f;
    float sidechainRelease = 0.15f;
    // Stereo Widener (essential for synthwave)
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
    // Filter
    bool filterEnabled = false;
    int filterType = 0;  // 0=LP, 1=HP, 2=BP
    float filterCutoff = 2000.0f;
    float filterResonance = 0.3f;
    // Distortion
    bool distortionEnabled = false;
    int distortionType = 0;  // 0=Tanh, 1=HardClip, 2=Foldback, 3=Asymmetric
    float distortionDrive = 2.0f;
    float distortionMix = 0.5f;
    // Bitcrusher (lo-fi / chiptune)
    bool bitcrusherEnabled = false;
    float bitDepth = 8.0f;
    float sampleRateDiv = 4.0f;
    // Phaser
    bool phaserEnabled = false;
    float phaserRate = 0.5f;
    float phaserDepth = 0.5f;
    float phaserFeedback = 0.5f;
    // Tremolo
    bool tremoloEnabled = false;
    float tremoloRate = 5.0f;
    float tremoloDepth = 0.3f;
    // EQ
    bool eqEnabled = false;
    float eqLow = 1.0f;
    float eqMid = 1.0f;
    float eqHigh = 1.0f;
    // Compressor
    bool compressorEnabled = false;
    float compThreshold = 0.5f;
    float compRatio = 4.0f;
    float compAttack = 0.01f;
    float compRelease = 0.1f;
    float compGain = 1.0f;
    // Filter Envelope
    bool filterEnvEnabled = false;
    float filterEnvAmount = 0.0f;
    float filterEnvAttack = 0.0f;
    float filterEnvDecay = 0.1f;
};

// Apply effects to channel config
inline void applyGenreEffects(ChannelConfig& config, const GenreEffects& fx) {
    config.reverbEnabled = fx.reverbEnabled;
    config.reverbMix = fx.reverbMix;
    config.reverbRoomSize = fx.reverbRoomSize;
    config.reverbDamping = fx.reverbDamping;
    
    config.chorusEnabled = fx.chorusEnabled;
    config.chorusMix = fx.chorusMix;
    config.chorusRate = fx.chorusRate;
    
    config.delayEnabled = fx.delayEnabled;
    config.delayMix = fx.delayMix;
    config.delayTime = fx.delayTime;
    config.delayFeedback = fx.delayFeedback;
    
    config.sidechainEnabled = fx.sidechainEnabled;
    config.sidechainAmount = fx.sidechainAmount;
    config.sidechainRelease = fx.sidechainRelease;
    
    config.stereoWidenerEnabled = fx.stereoWidenerEnabled;
    config.stereoWidenerWidth = fx.stereoWidenerWidth;
    config.stereoWidenerHaas = fx.stereoWidenerHaas;
    config.stereoWidenerMix = fx.stereoWidenerMix;
    
    config.tapeSaturationEnabled = fx.tapeSaturationEnabled;
    config.tapeDrive = fx.tapeDrive;
    config.tapeWarmth = fx.tapeWarmth;
    config.tapeCompression = fx.tapeCompression;
    config.tapeMix = fx.tapeMix;
    
    config.filterEnabled = fx.filterEnabled;
    config.filterType = fx.filterType;
    config.filterCutoff = fx.filterCutoff;
    config.filterResonance = fx.filterResonance;
    
    config.distortionEnabled = fx.distortionEnabled;
    config.distortionType = fx.distortionType;
    config.distortionDrive = fx.distortionDrive;
    config.distortionMix = fx.distortionMix;
    
    config.bitcrusherEnabled = fx.bitcrusherEnabled;
    config.bitDepth = fx.bitDepth;
    config.sampleRateDiv = fx.sampleRateDiv;
    
    config.phaserEnabled = fx.phaserEnabled;
    config.phaserRate = fx.phaserRate;
    config.phaserDepth = fx.phaserDepth;
    config.phaserFeedback = fx.phaserFeedback;
    
    config.tremoloEnabled = fx.tremoloEnabled;
    config.tremoloRate = fx.tremoloRate;
    config.tremoloDepth = fx.tremoloDepth;
    
    // NEW ADVANCED EFFECTS
    config.eqEnabled = fx.eqEnabled;
    config.eqLow = fx.eqLow;
    config.eqMid = fx.eqMid;
    config.eqHigh = fx.eqHigh;
    
    config.compressorEnabled = fx.compressorEnabled;
    config.compThreshold = fx.compThreshold;
    config.compRatio = fx.compRatio;
    config.compAttack = fx.compAttack;
    config.compRelease = fx.compRelease;
    config.compGain = fx.compGain;
    
    config.filterEnvEnabled = fx.filterEnvEnabled;
    config.filterEnvAmount = fx.filterEnvAmount;
    config.filterEnvAttack = fx.filterEnvAttack;
    config.filterEnvDecay = fx.filterEnvDecay;
}

// Get effect preset for a genre - comprehensive settings for authentic sound
inline GenreEffects getGenreEffects(const char* genre) {
    GenreEffects fx;

    if (strcmp(genre, "Synthwave") == 0) {
        // === SYNTHWAVE: The signature 80s sound ===
        // Lush reverb with long tail (like Vangelis/Blade Runner)
        fx.reverbEnabled = true;
        fx.reverbMix = 0.45f;
        fx.reverbRoomSize = 0.85f;
        fx.reverbDamping = 0.25f;  // Bright reverb tail
        // Thick chorus for detuned poly-synth feel (like Juno-106)
        fx.chorusEnabled = true;
        fx.chorusMix = 0.4f;
        fx.chorusRate = 0.35f;
        fx.chorusDepth = 0.015f;
        // Dotted 8th delay (THE synthwave delay - "Take On Me", "The Midnight")
        fx.delayEnabled = true;
        fx.delayMix = 0.3f;
        fx.delayTime = 0.375f;  // Dotted 8th at 120bpm = 375ms
        fx.delayFeedback = 0.4f;
        // Stereo widener for huge pad sounds (essential for synthwave)
        fx.stereoWidenerEnabled = true;
        fx.stereoWidenerWidth = 0.7f;
        fx.stereoWidenerHaas = 0.020f;  // 20ms Haas for wide stereo
        fx.stereoWidenerMix = 0.6f;
        // Tape saturation for analog warmth (cassette/VHS aesthetic)
        fx.tapeSaturationEnabled = true;
        fx.tapeDrive = 1.8f;
        fx.tapeWarmth = 0.6f;
        fx.tapeCompression = 0.35f;
        fx.tapeMix = 0.5f;
        // Subtle low-pass for warmth
        fx.filterEnabled = true;
        fx.filterType = 0;  // Low-pass
        fx.filterCutoff = 8000.0f;  // Roll off harsh highs
        fx.filterResonance = 0.2f;
        // Sidechain for pumping (The Weeknd, Kavinsky style)
        fx.sidechainEnabled = true;
        fx.sidechainAmount = 0.5f;
        fx.sidechainRelease = 0.2f;
        
        // === NEW ADVANCED EFFECTS ===
        // EQ: V-shape (boost lows and highs)
        fx.eqEnabled = true;
        fx.eqLow = 1.2f;   // Boost bass
        fx.eqMid = 0.9f;   // Cut mids slightly
        fx.eqHigh = 1.2f;  // Boost air
        
        // Compressor: Glue everything together
        fx.compressorEnabled = true;
        fx.compThreshold = 0.4f;
        fx.compRatio = 3.0f;
        fx.compAttack = 0.01f;
        fx.compRelease = 0.1f;
        fx.compGain = 1.2f;
        
        // Filter Envelope: For that analog "wow" on bass/leads
        fx.filterEnvEnabled = true;
        fx.filterEnvAmount = 0.3f;
        fx.filterEnvAttack = 0.05f;
        fx.filterEnvDecay = 0.3f;
    }
    else if (strcmp(genre, "Chiptune") == 0) {
        // === CHIPTUNE: Authentic 8-bit NES/C64 sound ===
        // No reverb - consoles didn't have it!
        fx.reverbEnabled = false;
        fx.chorusEnabled = false;
        // Short delay for pseudo-echo effect (like cave levels)
        fx.delayEnabled = true;
        fx.delayMix = 0.2f;
        fx.delayTime = 0.125f;  // Tempo-synced short delay
        fx.delayFeedback = 0.25f;
        // Bitcrusher for authentic lo-fi (NES was 7-bit, C64 was 4-bit DAC)
        fx.bitcrusherEnabled = true;
        fx.bitDepth = 8.0f;
        fx.sampleRateDiv = 2.0f;  // Subtle crunch
        // No stereo widening - NES was mono!
        fx.stereoWidenerEnabled = false;
        // No saturation - keep it digital
        fx.tapeSaturationEnabled = false;
    }
    else if (strcmp(genre, "Techno") == 0) {
        // === TECHNO: Dark Berlin warehouse sound ===
        // Industrial reverb (short, metallic)
        fx.reverbEnabled = true;
        fx.reverbMix = 0.25f;
        fx.reverbRoomSize = 0.4f;  // Small dark room
        fx.reverbDamping = 0.65f;  // Damped, dark
        // No chorus - techno is precise
        fx.chorusEnabled = false;
        // Tempo-synced delay (16th notes)
        fx.delayEnabled = true;
        fx.delayMix = 0.25f;
        fx.delayTime = 0.125f;  // 16th note at 130bpm
        fx.delayFeedback = 0.45f;
        // Aggressive sidechain (the pumping sound)
        fx.sidechainEnabled = true;
        fx.sidechainAmount = 0.75f;
        fx.sidechainRelease = 0.12f;  // Quick release
        // Resonant filter sweep (essential for techno)
        fx.filterEnabled = true;
        fx.filterType = 0;  // Low-pass
        fx.filterCutoff = 3500.0f;
        fx.filterResonance = 0.6f;  // Resonant acid sound
        // Subtle distortion for edge
        fx.distortionEnabled = true;
        fx.distortionType = 0;  // Tanh (soft)
        fx.distortionDrive = 2.5f;
        fx.distortionMix = 0.3f;
        // Phaser for movement
        fx.phaserEnabled = true;
        fx.phaserRate = 0.3f;
        fx.phaserDepth = 0.4f;
        fx.phaserFeedback = 0.5f;
    }
    else if (strcmp(genre, "Hip Hop") == 0) {
        // === HIP HOP: Boom bap / modern trap hybrid ===
        // Lo-fi room reverb
        fx.reverbEnabled = true;
        fx.reverbMix = 0.2f;
        fx.reverbRoomSize = 0.35f;
        fx.reverbDamping = 0.7f;
        fx.chorusEnabled = false;
        // Subtle delay for vibe
        fx.delayEnabled = true;
        fx.delayMix = 0.15f;
        fx.delayTime = 0.3f;
        fx.delayFeedback = 0.2f;
        // Lo-fi tape for vinyl warmth
        fx.tapeSaturationEnabled = true;
        fx.tapeDrive = 1.4f;
        fx.tapeWarmth = 0.65f;
        fx.tapeCompression = 0.4f;
        fx.tapeMix = 0.45f;
        // Lo-fi filter (dusty vinyl sound)
        fx.filterEnabled = true;
        fx.filterType = 0;  // Low-pass
        fx.filterCutoff = 6000.0f;
        fx.filterResonance = 0.15f;
        // Subtle bitcrush for lo-fi
        fx.bitcrusherEnabled = true;
        fx.bitDepth = 12.0f;  // Subtle SP-1200 style
        fx.sampleRateDiv = 1.5f;
    }
    else if (strcmp(genre, "Trap") == 0) {
        // === TRAP: Dark 808s and hi-hats ===
        // Big hall reverb on snares
        fx.reverbEnabled = true;
        fx.reverbMix = 0.3f;
        fx.reverbRoomSize = 0.6f;
        fx.reverbDamping = 0.5f;
        fx.chorusEnabled = false;
        // Triplet delay (trap signature)
        fx.delayEnabled = true;
        fx.delayMix = 0.2f;
        fx.delayTime = 0.222f;  // Triplet feel
        fx.delayFeedback = 0.25f;
        // Stereo width for pads
        fx.stereoWidenerEnabled = true;
        fx.stereoWidenerWidth = 0.5f;
        fx.stereoWidenerHaas = 0.015f;
        fx.stereoWidenerMix = 0.4f;
        // Distortion for hard 808s
        fx.distortionEnabled = true;
        fx.distortionType = 3;  // Asymmetric for 808 growl
        fx.distortionDrive = 3.0f;
        fx.distortionMix = 0.35f;
    }
    else if (strcmp(genre, "House") == 0) {
        // === HOUSE: Classic Chicago / Ibiza sound ===
        // Big room reverb (club sound)
        fx.reverbEnabled = true;
        fx.reverbMix = 0.4f;
        fx.reverbRoomSize = 0.75f;
        fx.reverbDamping = 0.35f;
        // Subtle chorus for warmth
        fx.chorusEnabled = true;
        fx.chorusMix = 0.25f;
        fx.chorusRate = 0.4f;
        fx.chorusDepth = 0.01f;
        // Tempo-synced delay
        fx.delayEnabled = true;
        fx.delayMix = 0.2f;
        fx.delayTime = 0.25f;  // Quarter note
        fx.delayFeedback = 0.35f;
        // Classic sidechain pump
        fx.sidechainEnabled = true;
        fx.sidechainAmount = 0.65f;
        fx.sidechainRelease = 0.18f;
        // Phaser for movement (disco heritage)
        fx.phaserEnabled = true;
        fx.phaserRate = 0.25f;
        fx.phaserDepth = 0.35f;
        fx.phaserFeedback = 0.4f;
        // Gentle high-pass to keep it clean
        fx.filterEnabled = true;
        fx.filterType = 1;  // High-pass
        fx.filterCutoff = 80.0f;  // Remove sub rumble
        fx.filterResonance = 0.1f;
    }
    else if (strcmp(genre, "Reggaeton") == 0) {
        // === REGGAETON: Dembow and perreo ===
        // Tight room reverb (club/street sound)
        fx.reverbEnabled = true;
        fx.reverbMix = 0.2f;
        fx.reverbRoomSize = 0.25f;  // Small, punchy
        fx.reverbDamping = 0.55f;
        fx.chorusEnabled = false;
        // Short slapback delay (dembow bounce)
        fx.delayEnabled = true;
        fx.delayMix = 0.18f;
        fx.delayTime = 0.1875f;  // Synced to dembow
        fx.delayFeedback = 0.15f;
        // Distortion for punchy 808s
        fx.distortionEnabled = true;
        fx.distortionType = 0;  // Tanh soft clip
        fx.distortionDrive = 2.0f;
        fx.distortionMix = 0.25f;
        // High-pass to keep kicks tight
        fx.filterEnabled = true;
        fx.filterType = 1;  // High-pass
        fx.filterCutoff = 60.0f;
        fx.filterResonance = 0.2f;
        // Slight stereo width on synths
        fx.stereoWidenerEnabled = true;
        fx.stereoWidenerWidth = 0.35f;
        fx.stereoWidenerHaas = 0.010f;
        fx.stereoWidenerMix = 0.3f;
    }
    else if (strcmp(genre, "EDM") == 0) {
        // === EDM: Festival main stage sound ===
        // Huge reverb for big room
        fx.reverbEnabled = true;
        fx.reverbMix = 0.5f;
        fx.reverbRoomSize = 0.9f;
        fx.reverbDamping = 0.3f;
        // Supersaw chorus
        fx.chorusEnabled = true;
        fx.chorusMix = 0.45f;
        fx.chorusRate = 0.3f;
        fx.chorusDepth = 0.02f;
        // Build-up delay
        fx.delayEnabled = true;
        fx.delayMix = 0.25f;
        fx.delayTime = 0.1875f;
        fx.delayFeedback = 0.5f;
        // Massive sidechain
        fx.sidechainEnabled = true;
        fx.sidechainAmount = 0.8f;
        fx.sidechainRelease = 0.15f;
        // Wide stereo
        fx.stereoWidenerEnabled = true;
        fx.stereoWidenerWidth = 0.8f;
        fx.stereoWidenerHaas = 0.025f;
        fx.stereoWidenerMix = 0.7f;
        // Bright filter
        fx.filterEnabled = true;
        fx.filterType = 0;
        fx.filterCutoff = 12000.0f;
        fx.filterResonance = 0.4f;
    }
    else if (strcmp(genre, "Ambient") == 0) {
        // === AMBIENT: Atmospheric soundscapes ===
        // Massive cathedral reverb
        fx.reverbEnabled = true;
        fx.reverbMix = 0.7f;
        fx.reverbRoomSize = 0.95f;
        fx.reverbDamping = 0.2f;
        // Lush modulation
        fx.chorusEnabled = true;
        fx.chorusMix = 0.5f;
        fx.chorusRate = 0.15f;  // Very slow
        fx.chorusDepth = 0.02f;
        // Long, evolving delay
        fx.delayEnabled = true;
        fx.delayMix = 0.35f;
        fx.delayTime = 0.5f;
        fx.delayFeedback = 0.6f;
        // Ultra-wide stereo
        fx.stereoWidenerEnabled = true;
        fx.stereoWidenerWidth = 0.9f;
        fx.stereoWidenerHaas = 0.030f;
        fx.stereoWidenerMix = 0.7f;
        // Slow tremolo for movement
        fx.tremoloEnabled = true;
        fx.tremoloRate = 2.0f;
        fx.tremoloDepth = 0.2f;
        // Dark filter
        fx.filterEnabled = true;
        fx.filterType = 0;
        fx.filterCutoff = 4000.0f;
        fx.filterResonance = 0.1f;
    }
    else if (strcmp(genre, "Phonk") == 0) {
        // === PHONK: Gritty, distorted, cowbell-heavy ===
        // Heavy distortion for that blown-out sound
        fx.distortionEnabled = true;
        fx.distortionType = 1; // Hard clip
        fx.distortionDrive = 4.5f;
        fx.distortionMix = 0.4f;
        // Lo-fi grit
        fx.bitcrusherEnabled = true;
        fx.bitDepth = 12.0f;
        fx.sampleRateDiv = 1.5f;
        // Dark room reverb
        fx.reverbEnabled = true;
        fx.reverbMix = 0.25f;
        fx.reverbRoomSize = 0.4f;
        // Heavy sidechain
        fx.sidechainEnabled = true;
        fx.sidechainAmount = 0.85f;
    }
    else if (strcmp(genre, "Future Bass") == 0) {
        // === FUTURE BASS: Huge saws and heavy ducking ===
        // Massive sidechain is the defining feature
        fx.sidechainEnabled = true;
        fx.sidechainAmount = 0.95f; // Extreme ducking
        fx.sidechainRelease = 0.3f; // Longer release for "sucking" feel
        // Wide stereo for supersaws
        fx.stereoWidenerEnabled = true;
        fx.stereoWidenerWidth = 0.8f;
        fx.stereoWidenerMix = 0.7f;
        // Bright reverb
        fx.reverbEnabled = true;
        fx.reverbMix = 0.35f;
        fx.reverbRoomSize = 0.8f;
        // Chorus for thickness
        fx.chorusEnabled = true;
        fx.chorusMix = 0.3f;
    }

    return fx;
}

struct SampleTrack {
    const char* name;
    const char* genre;
    const char* description;
    const TrackNote* notes;
    int noteCount;
    int lengthBeats;    // Total track length
    int bpm;            // Suggested BPM
    bool fixedPosition; // If true, notes are placed at their exact beat positions (starting from beat 0)
};

// ===========================================
// SYNTHWAVE TRACKS
// ===========================================

// Synthwave Track 1: "Midnight Drive" - Am-F-C-G progression (16 bars)
// Full {beat, pitch, osc, duration, velocity, vibrato, vibratoSpeed}
static const TrackNote g_SynthwaveMidnightDrive[] = {
    // === DRUMS (punchy, high velocity) ===
    {0.0f, 36, OscillatorType::Kick808, 0.25f, 0.95f, 0.0f, 5.5f},
    {1.0f, 38, OscillatorType::Snare808, 0.25f, 0.9f, 0.0f, 5.5f},
    {2.0f, 36, OscillatorType::Kick808, 0.25f, 0.95f, 0.0f, 5.5f},
    {3.0f, 38, OscillatorType::Snare808, 0.25f, 0.9f, 0.0f, 5.5f},
    {4.0f, 36, OscillatorType::Kick808, 0.25f, 0.95f, 0.0f, 5.5f},
    {5.0f, 38, OscillatorType::Snare808, 0.25f, 0.9f, 0.0f, 5.5f},
    {6.0f, 36, OscillatorType::Kick808, 0.25f, 0.95f, 0.0f, 5.5f},
    {7.0f, 38, OscillatorType::Snare808, 0.25f, 0.9f, 0.0f, 5.5f},
    {8.0f, 36, OscillatorType::Kick808, 0.25f, 0.95f, 0.0f, 5.5f},
    {9.0f, 38, OscillatorType::Snare808, 0.25f, 0.9f, 0.0f, 5.5f},
    {10.0f, 36, OscillatorType::Kick808, 0.25f, 0.95f, 0.0f, 5.5f},
    {11.0f, 38, OscillatorType::Snare808, 0.25f, 0.9f, 0.0f, 5.5f},
    {12.0f, 36, OscillatorType::Kick808, 0.25f, 0.95f, 0.0f, 5.5f},
    {13.0f, 38, OscillatorType::Snare808, 0.25f, 0.9f, 0.0f, 5.5f},
    {14.0f, 36, OscillatorType::Kick808, 0.25f, 0.95f, 0.0f, 5.5f},
    {15.0f, 38, OscillatorType::Snare808, 0.25f, 0.9f, 0.0f, 5.5f},
    // Hihats (softer velocity for groove)
    {0.5f, 42, OscillatorType::HiHat, 0.125f, 0.6f, 0.0f, 5.5f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f, 0.55f, 0.0f, 5.5f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f, 0.6f, 0.0f, 5.5f},
    {3.5f, 42, OscillatorType::HiHatOpen, 0.25f, 0.7f, 0.0f, 5.5f},
    {4.5f, 42, OscillatorType::HiHat, 0.125f, 0.6f, 0.0f, 5.5f},
    {5.5f, 42, OscillatorType::HiHat, 0.125f, 0.55f, 0.0f, 5.5f},
    {6.5f, 42, OscillatorType::HiHat, 0.125f, 0.6f, 0.0f, 5.5f},
    {7.5f, 42, OscillatorType::HiHatOpen, 0.25f, 0.7f, 0.0f, 5.5f},
    {8.5f, 42, OscillatorType::HiHat, 0.125f, 0.6f, 0.0f, 5.5f},
    {9.5f, 42, OscillatorType::HiHat, 0.125f, 0.55f, 0.0f, 5.5f},
    {10.5f, 42, OscillatorType::HiHat, 0.125f, 0.6f, 0.0f, 5.5f},
    {11.5f, 42, OscillatorType::HiHatOpen, 0.25f, 0.7f, 0.0f, 5.5f},
    {12.5f, 42, OscillatorType::HiHat, 0.125f, 0.6f, 0.0f, 5.5f},
    {13.5f, 42, OscillatorType::HiHat, 0.125f, 0.55f, 0.0f, 5.5f},
    {14.5f, 42, OscillatorType::HiHat, 0.125f, 0.6f, 0.0f, 5.5f},
    {15.5f, 42, OscillatorType::HiHatOpen, 0.25f, 0.7f, 0.0f, 5.5f},
    // === BASS (warm and full) ===
    {0.0f, 33, OscillatorType::SynthwaveBass, 3.5f, 0.85f, 0.0f, 5.5f},   // A1
    {4.0f, 29, OscillatorType::SynthwaveBass, 3.5f, 0.85f, 0.0f, 5.5f},   // F1
    {8.0f, 36, OscillatorType::SynthwaveBass, 3.5f, 0.85f, 0.0f, 5.5f},   // C2
    {12.0f, 31, OscillatorType::SynthwaveBass, 3.5f, 0.85f, 0.0f, 5.5f},  // G1
    // === MELODY (expressive with vibrato on sustained notes) ===
    {0.0f, 69, OscillatorType::SynthwaveLead, 1.0f, 0.8f, 0.15f, 5.0f},   // A4 - subtle vibrato
    {1.0f, 72, OscillatorType::SynthwaveLead, 0.5f, 0.75f, 0.0f, 5.5f},   // C5
    {1.5f, 74, OscillatorType::SynthwaveLead, 0.5f, 0.7f, 0.0f, 5.5f},    // D5
    {2.0f, 76, OscillatorType::SynthwaveLead, 2.0f, 0.9f, 0.25f, 4.5f},   // E5 - stronger vibrato
    {4.0f, 77, OscillatorType::SynthwaveLead, 1.0f, 0.85f, 0.15f, 5.0f},  // F5
    {5.0f, 76, OscillatorType::SynthwaveLead, 0.5f, 0.75f, 0.0f, 5.5f},   // E5
    {5.5f, 74, OscillatorType::SynthwaveLead, 0.5f, 0.7f, 0.0f, 5.5f},    // D5
    {6.0f, 72, OscillatorType::SynthwaveLead, 2.0f, 0.9f, 0.25f, 4.5f},   // C5 - strong vibrato
    {8.0f, 72, OscillatorType::SynthwaveLead, 1.0f, 0.8f, 0.15f, 5.0f},   // C5
    {9.0f, 74, OscillatorType::SynthwaveLead, 0.5f, 0.75f, 0.0f, 5.5f},   // D5
    {9.5f, 76, OscillatorType::SynthwaveLead, 0.5f, 0.7f, 0.0f, 5.5f},    // E5
    {10.0f, 79, OscillatorType::SynthwaveLead, 2.0f, 0.95f, 0.3f, 4.5f},  // G5 - emotional peak
    {12.0f, 79, OscillatorType::SynthwaveLead, 1.0f, 0.85f, 0.2f, 5.0f},  // G5
    {13.0f, 77, OscillatorType::SynthwaveLead, 0.5f, 0.75f, 0.0f, 5.5f},  // F5
    {13.5f, 76, OscillatorType::SynthwaveLead, 0.5f, 0.7f, 0.0f, 5.5f},   // E5
    {14.0f, 69, OscillatorType::SynthwaveLead, 2.0f, 0.9f, 0.25f, 4.5f},  // A4 - resolve with vibrato
    // === PADS (soft and lush with slow vibrato) ===
    {0.0f, 57, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},     // A3 (Am chord root)
    {0.0f, 60, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},     // C4
    {0.0f, 64, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},     // E4
    {4.0f, 53, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},     // F3
    {4.0f, 57, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},     // A3
    {4.0f, 60, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},     // C4
    {8.0f, 48, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},     // C3
    {8.0f, 52, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},     // E3
    {8.0f, 55, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},     // G3
    {12.0f, 55, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},    // G3
    {12.0f, 59, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},    // B3
    {12.0f, 62, OscillatorType::SynthwavePad, 4.0f, 0.5f, 0.1f, 3.0f},    // D4
};

// Synthwave Track 2: "Neon Dreams" - Fm-Db-Ab-Eb (slower, dreamy)
static const TrackNote g_SynthwaveNeonDreams[] = {
    // === DRUMS (slower, more sparse) ===
    {0.0f, 36, OscillatorType::Kick808, 0.5f}, {2.0f, 38, OscillatorType::Snare808, 0.25f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f}, {6.0f, 38, OscillatorType::Snare808, 0.25f},
    {8.0f, 36, OscillatorType::Kick808, 0.5f}, {10.0f, 38, OscillatorType::Snare808, 0.25f},
    {12.0f, 36, OscillatorType::Kick808, 0.5f}, {14.0f, 38, OscillatorType::Snare808, 0.25f},
    // Hihats (quarter notes)
    {1.0f, 42, OscillatorType::HiHat, 0.125f}, {3.0f, 42, OscillatorType::HiHatOpen, 0.25f},
    {5.0f, 42, OscillatorType::HiHat, 0.125f}, {7.0f, 42, OscillatorType::HiHatOpen, 0.25f},
    {9.0f, 42, OscillatorType::HiHat, 0.125f}, {11.0f, 42, OscillatorType::HiHatOpen, 0.25f},
    {13.0f, 42, OscillatorType::HiHat, 0.125f}, {15.0f, 42, OscillatorType::HiHatOpen, 0.25f},
    // === BASS ===
    {0.0f, 29, OscillatorType::SynthwaveBass, 3.5f},   // F1
    {4.0f, 25, OscillatorType::SynthwaveBass, 3.5f},   // Db1
    {8.0f, 32, OscillatorType::SynthwaveBass, 3.5f},   // Ab1
    {12.0f, 27, OscillatorType::SynthwaveBass, 3.5f},  // Eb1
    // === ARPEGGIO (dreamy sequence) ===
    {0.0f, 65, OscillatorType::SynthwaveArp, 0.25f},   // F4
    {0.5f, 68, OscillatorType::SynthwaveArp, 0.25f},   // Ab4
    {1.0f, 72, OscillatorType::SynthwaveArp, 0.25f},   // C5
    {1.5f, 68, OscillatorType::SynthwaveArp, 0.25f},   // Ab4
    {2.0f, 65, OscillatorType::SynthwaveArp, 0.25f},   // F4
    {2.5f, 68, OscillatorType::SynthwaveArp, 0.25f},   // Ab4
    {3.0f, 72, OscillatorType::SynthwaveArp, 0.25f},   // C5
    {3.5f, 77, OscillatorType::SynthwaveArp, 0.25f},   // F5
    {4.0f, 61, OscillatorType::SynthwaveArp, 0.25f},   // Db4
    {4.5f, 65, OscillatorType::SynthwaveArp, 0.25f},   // F4
    {5.0f, 68, OscillatorType::SynthwaveArp, 0.25f},   // Ab4
    {5.5f, 65, OscillatorType::SynthwaveArp, 0.25f},   // F4
    {6.0f, 61, OscillatorType::SynthwaveArp, 0.25f},   // Db4
    {6.5f, 65, OscillatorType::SynthwaveArp, 0.25f},   // F4
    {7.0f, 68, OscillatorType::SynthwaveArp, 0.25f},   // Ab4
    {7.5f, 73, OscillatorType::SynthwaveArp, 0.25f},   // Db5
    {8.0f, 68, OscillatorType::SynthwaveArp, 0.25f},   // Ab4
    {8.5f, 72, OscillatorType::SynthwaveArp, 0.25f},   // C5
    {9.0f, 75, OscillatorType::SynthwaveArp, 0.25f},   // Eb5
    {9.5f, 72, OscillatorType::SynthwaveArp, 0.25f},   // C5
    {10.0f, 68, OscillatorType::SynthwaveArp, 0.25f},  // Ab4
    {10.5f, 72, OscillatorType::SynthwaveArp, 0.25f},  // C5
    {11.0f, 75, OscillatorType::SynthwaveArp, 0.25f},  // Eb5
    {11.5f, 80, OscillatorType::SynthwaveArp, 0.25f},  // Ab5
    {12.0f, 63, OscillatorType::SynthwaveArp, 0.25f},  // Eb4
    {12.5f, 67, OscillatorType::SynthwaveArp, 0.25f},  // G4
    {13.0f, 70, OscillatorType::SynthwaveArp, 0.25f},  // Bb4
    {13.5f, 67, OscillatorType::SynthwaveArp, 0.25f},  // G4
    {14.0f, 63, OscillatorType::SynthwaveArp, 0.25f},  // Eb4
    {14.5f, 67, OscillatorType::SynthwaveArp, 0.25f},  // G4
    {15.0f, 70, OscillatorType::SynthwaveArp, 0.25f},  // Bb4
    {15.5f, 75, OscillatorType::SynthwaveArp, 0.25f},  // Eb5
    // === PADS ===
    {0.0f, 53, OscillatorType::SynthwavePad, 4.0f},    // Fm
    {0.0f, 56, OscillatorType::SynthwavePad, 4.0f},
    {0.0f, 60, OscillatorType::SynthwavePad, 4.0f},
    {4.0f, 49, OscillatorType::SynthwavePad, 4.0f},    // Db
    {4.0f, 53, OscillatorType::SynthwavePad, 4.0f},
    {4.0f, 56, OscillatorType::SynthwavePad, 4.0f},
    {8.0f, 56, OscillatorType::SynthwavePad, 4.0f},    // Ab
    {8.0f, 60, OscillatorType::SynthwavePad, 4.0f},
    {8.0f, 63, OscillatorType::SynthwavePad, 4.0f},
    {12.0f, 51, OscillatorType::SynthwavePad, 4.0f},   // Eb
    {12.0f, 55, OscillatorType::SynthwavePad, 4.0f},
    {12.0f, 58, OscillatorType::SynthwavePad, 4.0f},
};

// Synthwave Track 3: "Retro Racer" - Em-C-G-D (energetic)
static const TrackNote g_SynthwaveRetroRacer[] = {
    // === DRUMS (driving beat) ===
    {0.0f, 36, OscillatorType::Kick808, 0.25f}, {1.0f, 38, OscillatorType::Snare808, 0.25f},
    {2.0f, 36, OscillatorType::Kick808, 0.25f}, {2.5f, 36, OscillatorType::Kick808, 0.125f},
    {3.0f, 38, OscillatorType::Snare808, 0.25f},
    {4.0f, 36, OscillatorType::Kick808, 0.25f}, {5.0f, 38, OscillatorType::Snare808, 0.25f},
    {6.0f, 36, OscillatorType::Kick808, 0.25f}, {6.5f, 36, OscillatorType::Kick808, 0.125f},
    {7.0f, 38, OscillatorType::Snare808, 0.25f},
    {8.0f, 36, OscillatorType::Kick808, 0.25f}, {9.0f, 38, OscillatorType::Snare808, 0.25f},
    {10.0f, 36, OscillatorType::Kick808, 0.25f}, {10.5f, 36, OscillatorType::Kick808, 0.125f},
    {11.0f, 38, OscillatorType::Snare808, 0.25f},
    {12.0f, 36, OscillatorType::Kick808, 0.25f}, {13.0f, 38, OscillatorType::Snare808, 0.25f},
    {14.0f, 36, OscillatorType::Kick808, 0.25f}, {14.5f, 36, OscillatorType::Kick808, 0.125f},
    {15.0f, 38, OscillatorType::Snare808, 0.25f},
    // Hihats (16ths on last beat of each bar for energy)
    {0.5f, 42, OscillatorType::HiHat, 0.125f}, {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f}, {3.25f, 42, OscillatorType::HiHat, 0.0625f},
    {3.5f, 42, OscillatorType::HiHat, 0.0625f}, {3.75f, 42, OscillatorType::HiHatOpen, 0.125f},
    {4.5f, 42, OscillatorType::HiHat, 0.125f}, {5.5f, 42, OscillatorType::HiHat, 0.125f},
    {6.5f, 42, OscillatorType::HiHat, 0.125f}, {7.25f, 42, OscillatorType::HiHat, 0.0625f},
    {7.5f, 42, OscillatorType::HiHat, 0.0625f}, {7.75f, 42, OscillatorType::HiHatOpen, 0.125f},
    {8.5f, 42, OscillatorType::HiHat, 0.125f}, {9.5f, 42, OscillatorType::HiHat, 0.125f},
    {10.5f, 42, OscillatorType::HiHat, 0.125f}, {11.25f, 42, OscillatorType::HiHat, 0.0625f},
    {11.5f, 42, OscillatorType::HiHat, 0.0625f}, {11.75f, 42, OscillatorType::HiHatOpen, 0.125f},
    {12.5f, 42, OscillatorType::HiHat, 0.125f}, {13.5f, 42, OscillatorType::HiHat, 0.125f},
    {14.5f, 42, OscillatorType::HiHat, 0.125f}, {15.25f, 42, OscillatorType::HiHat, 0.0625f},
    {15.5f, 42, OscillatorType::HiHat, 0.0625f}, {15.75f, 42, OscillatorType::HiHatOpen, 0.125f},
    // === BASS (driving 8th note pattern) ===
    {0.0f, 40, OscillatorType::SynthwaveBass, 0.5f}, {0.5f, 40, OscillatorType::SynthwaveBass, 0.5f},
    {1.0f, 40, OscillatorType::SynthwaveBass, 0.5f}, {1.5f, 40, OscillatorType::SynthwaveBass, 0.5f},
    {2.0f, 40, OscillatorType::SynthwaveBass, 0.5f}, {2.5f, 40, OscillatorType::SynthwaveBass, 0.5f},
    {3.0f, 40, OscillatorType::SynthwaveBass, 0.5f}, {3.5f, 40, OscillatorType::SynthwaveBass, 0.5f},
    {4.0f, 36, OscillatorType::SynthwaveBass, 0.5f}, {4.5f, 36, OscillatorType::SynthwaveBass, 0.5f},
    {5.0f, 36, OscillatorType::SynthwaveBass, 0.5f}, {5.5f, 36, OscillatorType::SynthwaveBass, 0.5f},
    {6.0f, 36, OscillatorType::SynthwaveBass, 0.5f}, {6.5f, 36, OscillatorType::SynthwaveBass, 0.5f},
    {7.0f, 36, OscillatorType::SynthwaveBass, 0.5f}, {7.5f, 36, OscillatorType::SynthwaveBass, 0.5f},
    {8.0f, 43, OscillatorType::SynthwaveBass, 0.5f}, {8.5f, 43, OscillatorType::SynthwaveBass, 0.5f},
    {9.0f, 43, OscillatorType::SynthwaveBass, 0.5f}, {9.5f, 43, OscillatorType::SynthwaveBass, 0.5f},
    {10.0f, 43, OscillatorType::SynthwaveBass, 0.5f}, {10.5f, 43, OscillatorType::SynthwaveBass, 0.5f},
    {11.0f, 43, OscillatorType::SynthwaveBass, 0.5f}, {11.5f, 43, OscillatorType::SynthwaveBass, 0.5f},
    {12.0f, 38, OscillatorType::SynthwaveBass, 0.5f}, {12.5f, 38, OscillatorType::SynthwaveBass, 0.5f},
    {13.0f, 38, OscillatorType::SynthwaveBass, 0.5f}, {13.5f, 38, OscillatorType::SynthwaveBass, 0.5f},
    {14.0f, 38, OscillatorType::SynthwaveBass, 0.5f}, {14.5f, 38, OscillatorType::SynthwaveBass, 0.5f},
    {15.0f, 38, OscillatorType::SynthwaveBass, 0.5f}, {15.5f, 38, OscillatorType::SynthwaveBass, 0.5f},
    // === LEAD MELODY ===
    {0.0f, 76, OscillatorType::SynthwaveLead, 0.5f},  // E5
    {0.5f, 79, OscillatorType::SynthwaveLead, 0.5f},  // G5
    {1.0f, 83, OscillatorType::SynthwaveLead, 1.0f},  // B5
    {2.0f, 79, OscillatorType::SynthwaveLead, 0.5f},  // G5
    {2.5f, 76, OscillatorType::SynthwaveLead, 0.5f},  // E5
    {3.0f, 74, OscillatorType::SynthwaveLead, 1.0f},  // D5
    {4.0f, 72, OscillatorType::SynthwaveLead, 0.5f},  // C5
    {4.5f, 76, OscillatorType::SynthwaveLead, 0.5f},  // E5
    {5.0f, 79, OscillatorType::SynthwaveLead, 1.0f},  // G5
    {6.0f, 76, OscillatorType::SynthwaveLead, 0.5f},  // E5
    {6.5f, 72, OscillatorType::SynthwaveLead, 0.5f},  // C5
    {7.0f, 71, OscillatorType::SynthwaveLead, 1.0f},  // B4
    {8.0f, 79, OscillatorType::SynthwaveLead, 0.5f},  // G5
    {8.5f, 83, OscillatorType::SynthwaveLead, 0.5f},  // B5
    {9.0f, 86, OscillatorType::SynthwaveLead, 1.0f},  // D6
    {10.0f, 83, OscillatorType::SynthwaveLead, 0.5f}, // B5
    {10.5f, 79, OscillatorType::SynthwaveLead, 0.5f}, // G5
    {11.0f, 76, OscillatorType::SynthwaveLead, 1.0f}, // E5
    {12.0f, 74, OscillatorType::SynthwaveLead, 0.5f}, // D5
    {12.5f, 78, OscillatorType::SynthwaveLead, 0.5f}, // F#5
    {13.0f, 81, OscillatorType::SynthwaveLead, 1.0f}, // A5
    {14.0f, 78, OscillatorType::SynthwaveLead, 0.5f}, // F#5
    {14.5f, 74, OscillatorType::SynthwaveLead, 0.5f}, // D5
    {15.0f, 76, OscillatorType::SynthwaveLead, 1.0f}, // E5
};

// Synthwave Track 4: "Nightcall" - Kavinsky style, corrected (Cm-Eb-Bb-Gm)
// Driving 16th note bass, heavy snare, vocoded lead approximation
static const TrackNote g_SynthwaveNightcall[] = {
    // === DRUMS (Heavy, gated reverb feel) ===
    // Kick: Driving beat (Boom... Boom-Boom...)
    {0.0f, 36, OscillatorType::Kick808, 0.6f},   // 1
    {1.5f, 36, OscillatorType::Kick808, 0.4f},   // 2& (ghost)
    {2.0f, 36, OscillatorType::Kick808, 0.6f},   // 3
    {2.5f, 36, OscillatorType::Kick808, 0.5f},   // 3&
    {4.0f, 36, OscillatorType::Kick808, 0.6f},   // 1
    {6.0f, 36, OscillatorType::Kick808, 0.6f},   // 3
    {6.5f, 36, OscillatorType::Kick808, 0.5f},   // 3&
    {8.0f, 36, OscillatorType::Kick808, 0.6f},   // 1
    {9.5f, 36, OscillatorType::Kick808, 0.4f},   // 2&
    {10.0f, 36, OscillatorType::Kick808, 0.6f},  // 3
    {10.5f, 36, OscillatorType::Kick808, 0.5f},  // 3&
    {12.0f, 36, OscillatorType::Kick808, 0.6f},  // 1
    {14.0f, 36, OscillatorType::Kick808, 0.6f},  // 3
    {14.5f, 36, OscillatorType::Kick808, 0.5f},  // 3&

    // Snare: Huge, gated sound on 2 and 4
    {1.0f, 38, OscillatorType::Snare808, 0.7f},
    {3.0f, 38, OscillatorType::Snare808, 0.7f},
    {5.0f, 38, OscillatorType::Snare808, 0.7f},
    {7.0f, 38, OscillatorType::Snare808, 0.7f},
    {9.0f, 38, OscillatorType::Snare808, 0.7f},
    {11.0f, 38, OscillatorType::Snare808, 0.7f},
    {13.0f, 38, OscillatorType::Snare808, 0.7f},
    {15.0f, 38, OscillatorType::Snare808, 0.7f},

    // Hi-Hats: Steady 16th notes (closed)
    {0.0f, 42, OscillatorType::HiHat, 0.3f}, {0.25f, 42, OscillatorType::HiHat, 0.2f}, {0.5f, 42, OscillatorType::HiHat, 0.3f}, {0.75f, 42, OscillatorType::HiHat, 0.2f},
    {1.0f, 42, OscillatorType::HiHat, 0.3f}, {1.25f, 42, OscillatorType::HiHat, 0.2f}, {1.5f, 42, OscillatorType::HiHat, 0.3f}, {1.75f, 42, OscillatorType::HiHat, 0.2f},
    {2.0f, 42, OscillatorType::HiHat, 0.3f}, {2.25f, 42, OscillatorType::HiHat, 0.2f}, {2.5f, 42, OscillatorType::HiHat, 0.3f}, {2.75f, 42, OscillatorType::HiHat, 0.2f},
    {3.0f, 42, OscillatorType::HiHat, 0.3f}, {3.25f, 42, OscillatorType::HiHat, 0.2f}, {3.5f, 42, OscillatorType::HiHat, 0.3f}, {3.75f, 42, OscillatorType::HiHat, 0.2f},
    // ... repeat for 4 bars ...
    {4.0f, 42, OscillatorType::HiHat, 0.3f}, {4.5f, 42, OscillatorType::HiHat, 0.3f}, {5.0f, 42, OscillatorType::HiHat, 0.3f}, {5.5f, 42, OscillatorType::HiHat, 0.3f},
    {6.0f, 42, OscillatorType::HiHat, 0.3f}, {6.5f, 42, OscillatorType::HiHat, 0.3f}, {7.0f, 42, OscillatorType::HiHat, 0.3f}, {7.5f, 42, OscillatorType::HiHat, 0.3f},
    {8.0f, 42, OscillatorType::HiHat, 0.3f}, {8.5f, 42, OscillatorType::HiHat, 0.3f}, {9.0f, 42, OscillatorType::HiHat, 0.3f}, {9.5f, 42, OscillatorType::HiHat, 0.3f},
    {10.0f, 42, OscillatorType::HiHat, 0.3f}, {10.5f, 42, OscillatorType::HiHat, 0.3f}, {11.0f, 42, OscillatorType::HiHat, 0.3f}, {11.5f, 42, OscillatorType::HiHat, 0.3f},
    {12.0f, 42, OscillatorType::HiHat, 0.3f}, {12.5f, 42, OscillatorType::HiHat, 0.3f}, {13.0f, 42, OscillatorType::HiHat, 0.3f}, {13.5f, 42, OscillatorType::HiHat, 0.3f},
    {14.0f, 42, OscillatorType::HiHat, 0.3f}, {14.5f, 42, OscillatorType::HiHat, 0.3f}, {15.0f, 42, OscillatorType::HiHat, 0.3f}, {15.5f, 42, OscillatorType::HiHat, 0.3f},

    // === BASS (Cm - Eb - Bb - Gm) ===
    // Driving 8th note pulse with octave jumps
    // Bar 1: Cm (C2)
    {0.0f, 36, OscillatorType::Sawtooth, 0.5f}, {0.5f, 48, OscillatorType::Sawtooth, 0.5f},
    {1.0f, 36, OscillatorType::Sawtooth, 0.5f}, {1.5f, 48, OscillatorType::Sawtooth, 0.5f},
    {2.0f, 36, OscillatorType::Sawtooth, 0.5f}, {2.5f, 48, OscillatorType::Sawtooth, 0.5f},
    {3.0f, 36, OscillatorType::Sawtooth, 0.5f}, {3.5f, 48, OscillatorType::Sawtooth, 0.5f},
    // Bar 2: Eb (Eb2)
    {4.0f, 39, OscillatorType::Sawtooth, 0.5f}, {4.5f, 51, OscillatorType::Sawtooth, 0.5f},
    {5.0f, 39, OscillatorType::Sawtooth, 0.5f}, {5.5f, 51, OscillatorType::Sawtooth, 0.5f},
    {6.0f, 39, OscillatorType::Sawtooth, 0.5f}, {6.5f, 51, OscillatorType::Sawtooth, 0.5f},
    {7.0f, 39, OscillatorType::Sawtooth, 0.5f}, {7.5f, 51, OscillatorType::Sawtooth, 0.5f},
    // Bar 3: Bb (Bb1)
    {8.0f, 34, OscillatorType::Sawtooth, 0.5f}, {8.5f, 46, OscillatorType::Sawtooth, 0.5f},
    {9.0f, 34, OscillatorType::Sawtooth, 0.5f}, {9.5f, 46, OscillatorType::Sawtooth, 0.5f},
    {10.0f, 34, OscillatorType::Sawtooth, 0.5f}, {10.5f, 46, OscillatorType::Sawtooth, 0.5f},
    {11.0f, 34, OscillatorType::Sawtooth, 0.5f}, {11.5f, 46, OscillatorType::Sawtooth, 0.5f},
    // Bar 4: Gm (G1)
    {12.0f, 31, OscillatorType::Sawtooth, 0.5f}, {12.5f, 43, OscillatorType::Sawtooth, 0.5f},
    {13.0f, 31, OscillatorType::Sawtooth, 0.5f}, {13.5f, 43, OscillatorType::Sawtooth, 0.5f},
    {14.0f, 31, OscillatorType::Sawtooth, 0.5f}, {14.5f, 43, OscillatorType::Sawtooth, 0.5f},
    {15.0f, 31, OscillatorType::Sawtooth, 0.5f}, {15.5f, 43, OscillatorType::Sawtooth, 0.5f},

    // === PADS (Dark, sustained) ===
    // Cm
    {0.0f, 48, OscillatorType::SynthwavePad, 4.0f}, {0.0f, 51, OscillatorType::SynthwavePad, 4.0f}, {0.0f, 55, OscillatorType::SynthwavePad, 4.0f},
    // Eb
    {4.0f, 51, OscillatorType::SynthwavePad, 4.0f}, {4.0f, 55, OscillatorType::SynthwavePad, 4.0f}, {4.0f, 58, OscillatorType::SynthwavePad, 4.0f},
    // Bb
    {8.0f, 46, OscillatorType::SynthwavePad, 4.0f}, {8.0f, 50, OscillatorType::SynthwavePad, 4.0f}, {8.0f, 53, OscillatorType::SynthwavePad, 4.0f},
    // Gm
    {12.0f, 43, OscillatorType::SynthwavePad, 4.0f}, {12.0f, 46, OscillatorType::SynthwavePad, 4.0f}, {12.0f, 50, OscillatorType::SynthwavePad, 4.0f},

    // === LEAD (Vocoder-style melody approximation) ===
    // "I'm giving you a night call..."
    {0.0f, 67, OscillatorType::Sawtooth, 0.5f},  // G4
    {0.5f, 67, OscillatorType::Sawtooth, 0.25f}, // G4
    {0.75f, 67, OscillatorType::Sawtooth, 0.5f}, // G4
    {1.25f, 65, OscillatorType::Sawtooth, 0.25f}, // F4
    {1.5f, 63, OscillatorType::Sawtooth, 0.5f},  // Eb4
    {2.0f, 63, OscillatorType::Sawtooth, 1.0f},  // Eb4
    {3.0f, 60, OscillatorType::Sawtooth, 1.0f},  // C4

    // "To tell you how I feel"
    {4.0f, 67, OscillatorType::Sawtooth, 0.5f},  // G4
    {4.5f, 67, OscillatorType::Sawtooth, 0.25f}, // G4
    {4.75f, 67, OscillatorType::Sawtooth, 0.5f}, // G4
    {5.25f, 68, OscillatorType::Sawtooth, 0.25f}, // Ab4
    {5.5f, 67, OscillatorType::Sawtooth, 0.5f},  // G4
    {6.0f, 65, OscillatorType::Sawtooth, 1.0f},  // F4
    {7.0f, 63, OscillatorType::Sawtooth, 1.0f},  // Eb4

    // "I want to drive you through the night"
    {8.0f, 65, OscillatorType::Sawtooth, 0.5f},  // F4
    {8.5f, 65, OscillatorType::Sawtooth, 0.25f}, // F4
    {8.75f, 65, OscillatorType::Sawtooth, 0.5f}, // F4
    {9.25f, 63, OscillatorType::Sawtooth, 0.25f}, // Eb4
    {9.5f, 62, OscillatorType::Sawtooth, 0.5f},  // D4
    {10.0f, 62, OscillatorType::Sawtooth, 1.0f}, // D4
    {11.0f, 58, OscillatorType::Sawtooth, 1.0f}, // Bb3

    // "Down the hills"
    {12.0f, 62, OscillatorType::Sawtooth, 0.5f}, // D4
    {12.5f, 62, OscillatorType::Sawtooth, 0.25f}, // D4
    {12.75f, 62, OscillatorType::Sawtooth, 0.5f}, // D4
    {13.25f, 63, OscillatorType::Sawtooth, 0.25f}, // Eb4
    {13.5f, 62, OscillatorType::Sawtooth, 0.5f}, // D4
    {14.0f, 60, OscillatorType::Sawtooth, 2.0f}, // C4
};

// Synthwave Track 5: "Turbo Killer" - Carpenter Brut style, aggressive (Em-C-G-D)
static const TrackNote g_SynthwaveTurboKiller[] = {
    // === DRUMS (hard hitting, driving) ===
    {0.0f, 36, OscillatorType::KickHard, 0.25f}, {1.0f, 38, OscillatorType::Snare808, 0.25f},
    {2.0f, 36, OscillatorType::KickHard, 0.25f}, {3.0f, 38, OscillatorType::Snare808, 0.25f},
    {4.0f, 36, OscillatorType::KickHard, 0.25f}, {5.0f, 38, OscillatorType::Snare808, 0.25f},
    {6.0f, 36, OscillatorType::KickHard, 0.25f}, {7.0f, 38, OscillatorType::Snare808, 0.25f},
    {8.0f, 36, OscillatorType::KickHard, 0.25f}, {9.0f, 38, OscillatorType::Snare808, 0.25f},
    {10.0f, 36, OscillatorType::KickHard, 0.25f}, {11.0f, 38, OscillatorType::Snare808, 0.25f},
    {12.0f, 36, OscillatorType::KickHard, 0.25f}, {13.0f, 38, OscillatorType::Snare808, 0.25f},
    {14.0f, 36, OscillatorType::KickHard, 0.25f}, {15.0f, 38, OscillatorType::Snare808, 0.25f},
    // Fast 16th note hihats
    {0.0f, 42, OscillatorType::HiHat, 0.125f}, {0.25f, 42, OscillatorType::HiHat, 0.125f},
    {0.5f, 42, OscillatorType::HiHat, 0.125f}, {0.75f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 42, OscillatorType::HiHat, 0.125f}, {1.25f, 42, OscillatorType::HiHat, 0.125f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f}, {1.75f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 42, OscillatorType::HiHat, 0.125f}, {2.25f, 42, OscillatorType::HiHat, 0.125f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f}, {2.75f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 42, OscillatorType::HiHat, 0.125f}, {3.25f, 42, OscillatorType::HiHat, 0.125f},
    {3.5f, 46, OscillatorType::HiHatOpen, 0.25f}, {3.75f, 42, OscillatorType::HiHat, 0.125f},
    // === BASS (aggressive saw bass) ===
    {0.0f, 28, OscillatorType::SynthwaveBass, 1.0f},   // E1
    {1.0f, 28, OscillatorType::SynthwaveBass, 0.5f},
    {1.5f, 40, OscillatorType::SynthwaveBass, 0.5f},   // octave
    {2.0f, 28, OscillatorType::SynthwaveBass, 1.0f},
    {3.0f, 28, OscillatorType::SynthwaveBass, 0.5f},
    {3.5f, 40, OscillatorType::SynthwaveBass, 0.5f},
    {4.0f, 36, OscillatorType::SynthwaveBass, 1.0f},   // C2
    {5.0f, 36, OscillatorType::SynthwaveBass, 0.5f},
    {5.5f, 48, OscillatorType::SynthwaveBass, 0.5f},
    {6.0f, 36, OscillatorType::SynthwaveBass, 1.0f},
    {7.0f, 36, OscillatorType::SynthwaveBass, 0.5f},
    {7.5f, 48, OscillatorType::SynthwaveBass, 0.5f},
    {8.0f, 31, OscillatorType::SynthwaveBass, 1.0f},   // G1
    {9.0f, 31, OscillatorType::SynthwaveBass, 0.5f},
    {9.5f, 43, OscillatorType::SynthwaveBass, 0.5f},
    {10.0f, 31, OscillatorType::SynthwaveBass, 1.0f},
    {11.0f, 31, OscillatorType::SynthwaveBass, 0.5f},
    {11.5f, 43, OscillatorType::SynthwaveBass, 0.5f},
    {12.0f, 26, OscillatorType::SynthwaveBass, 1.0f},  // D1
    {13.0f, 26, OscillatorType::SynthwaveBass, 0.5f},
    {13.5f, 38, OscillatorType::SynthwaveBass, 0.5f},
    {14.0f, 26, OscillatorType::SynthwaveBass, 1.0f},
    {15.0f, 26, OscillatorType::SynthwaveBass, 0.5f},
    {15.5f, 38, OscillatorType::SynthwaveBass, 0.5f},
    // === LEAD (aggressive, fast arpeggios) ===
    {0.0f, 64, OscillatorType::SynthwaveLead, 0.25f},  // E4
    {0.25f, 67, OscillatorType::SynthwaveLead, 0.25f}, // G4
    {0.5f, 71, OscillatorType::SynthwaveLead, 0.25f},  // B4
    {0.75f, 76, OscillatorType::SynthwaveLead, 0.25f}, // E5
    {1.0f, 71, OscillatorType::SynthwaveLead, 0.25f},
    {1.25f, 67, OscillatorType::SynthwaveLead, 0.25f},
    {1.5f, 64, OscillatorType::SynthwaveLead, 0.25f},
    {1.75f, 67, OscillatorType::SynthwaveLead, 0.25f},
    {2.0f, 64, OscillatorType::SynthwaveLead, 0.25f},
    {2.25f, 67, OscillatorType::SynthwaveLead, 0.25f},
    {2.5f, 71, OscillatorType::SynthwaveLead, 0.25f},
    {2.75f, 76, OscillatorType::SynthwaveLead, 0.25f},
    {3.0f, 71, OscillatorType::SynthwaveLead, 0.25f},
    {3.25f, 67, OscillatorType::SynthwaveLead, 0.25f},
    {3.5f, 64, OscillatorType::SynthwaveLead, 0.25f},
    {3.75f, 67, OscillatorType::SynthwaveLead, 0.25f},
    // C chord arp
    {4.0f, 60, OscillatorType::SynthwaveLead, 0.25f},  // C4
    {4.25f, 64, OscillatorType::SynthwaveLead, 0.25f}, // E4
    {4.5f, 67, OscillatorType::SynthwaveLead, 0.25f},  // G4
    {4.75f, 72, OscillatorType::SynthwaveLead, 0.25f}, // C5
    {5.0f, 67, OscillatorType::SynthwaveLead, 0.25f},
    {5.25f, 64, OscillatorType::SynthwaveLead, 0.25f},
    {5.5f, 60, OscillatorType::SynthwaveLead, 0.25f},
    {5.75f, 64, OscillatorType::SynthwaveLead, 0.25f},
};

// Synthwave Track 6: "Endless Summer" - The Midnight style, emotional (Am7-Fmaj7-Cmaj7-G)
static const TrackNote g_SynthwaveEndlessSummer[] = {
    // === DRUMS (smooth, groovy) ===
    {0.0f, 36, OscillatorType::Kick808, 0.25f}, {2.0f, 38, OscillatorType::Snare808, 0.25f},
    {4.0f, 36, OscillatorType::Kick808, 0.25f}, {6.0f, 38, OscillatorType::Snare808, 0.25f},
    {8.0f, 36, OscillatorType::Kick808, 0.25f}, {10.0f, 38, OscillatorType::Snare808, 0.25f},
    {12.0f, 36, OscillatorType::Kick808, 0.25f}, {14.0f, 38, OscillatorType::Snare808, 0.25f},
    // Shaker rhythm
    {0.5f, 42, OscillatorType::HiHat, 0.125f}, {1.0f, 42, OscillatorType::HiHat, 0.125f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f}, {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 42, OscillatorType::HiHat, 0.125f}, {3.5f, 42, OscillatorType::HiHat, 0.125f},
    {4.5f, 42, OscillatorType::HiHat, 0.125f}, {5.0f, 42, OscillatorType::HiHat, 0.125f},
    {5.5f, 42, OscillatorType::HiHat, 0.125f}, {6.5f, 42, OscillatorType::HiHat, 0.125f},
    {7.0f, 42, OscillatorType::HiHat, 0.125f}, {7.5f, 42, OscillatorType::HiHat, 0.125f},
    // === BASS (smooth, melodic bass) ===
    {0.0f, 33, OscillatorType::SynthwaveBass, 3.5f},   // A1
    {4.0f, 29, OscillatorType::SynthwaveBass, 3.5f},   // F1
    {8.0f, 36, OscillatorType::SynthwaveBass, 3.5f},   // C2
    {12.0f, 31, OscillatorType::SynthwaveBass, 3.5f},  // G1
    // === MELODY (soaring, emotional) ===
    {0.0f, 72, OscillatorType::SynthwaveLead, 1.5f},   // C5
    {2.0f, 71, OscillatorType::SynthwaveLead, 0.5f},   // B4
    {2.5f, 69, OscillatorType::SynthwaveLead, 1.5f},   // A4
    {4.0f, 72, OscillatorType::SynthwaveLead, 1.0f},   // C5
    {5.0f, 74, OscillatorType::SynthwaveLead, 0.5f},   // D5
    {5.5f, 76, OscillatorType::SynthwaveLead, 1.5f},   // E5
    {7.0f, 77, OscillatorType::SynthwaveLead, 1.0f},   // F5
    {8.0f, 79, OscillatorType::SynthwaveLead, 1.5f},   // G5
    {10.0f, 77, OscillatorType::SynthwaveLead, 0.5f},  // F5
    {10.5f, 76, OscillatorType::SynthwaveLead, 1.5f},  // E5
    {12.0f, 74, OscillatorType::SynthwaveLead, 1.0f},  // D5
    {13.0f, 72, OscillatorType::SynthwaveLead, 0.5f},  // C5
    {13.5f, 71, OscillatorType::SynthwaveLead, 0.5f},  // B4
    {14.0f, 69, OscillatorType::SynthwaveLead, 2.0f},  // A4
    // === PADS (Am7-Fmaj7-Cmaj7-G) ===
    {0.0f, 57, OscillatorType::SynthwavePad, 4.0f},    // A3
    {0.0f, 60, OscillatorType::SynthwavePad, 4.0f},    // C4
    {0.0f, 64, OscillatorType::SynthwavePad, 4.0f},    // E4
    {0.0f, 67, OscillatorType::SynthwavePad, 4.0f},    // G4
    {4.0f, 53, OscillatorType::SynthwavePad, 4.0f},    // F3
    {4.0f, 57, OscillatorType::SynthwavePad, 4.0f},    // A3
    {4.0f, 60, OscillatorType::SynthwavePad, 4.0f},    // C4
    {4.0f, 64, OscillatorType::SynthwavePad, 4.0f},    // E4
    {8.0f, 48, OscillatorType::SynthwavePad, 4.0f},    // C3
    {8.0f, 52, OscillatorType::SynthwavePad, 4.0f},    // E3
    {8.0f, 55, OscillatorType::SynthwavePad, 4.0f},    // G3
    {8.0f, 59, OscillatorType::SynthwavePad, 4.0f},    // B3
    {12.0f, 55, OscillatorType::SynthwavePad, 4.0f},   // G3
    {12.0f, 59, OscillatorType::SynthwavePad, 4.0f},   // B3
    {12.0f, 62, OscillatorType::SynthwavePad, 4.0f},   // D4
};

// Synthwave Track 7: "Tech Noir" - Gunship style, darker (Dm-Bb-F-C)
static const TrackNote g_SynthwaveTechNoir[] = {
    // === DRUMS (heavier, darker) ===
    {0.0f, 36, OscillatorType::Kick808, 0.5f}, {1.5f, 36, OscillatorType::Kick808, 0.25f},
    {2.0f, 38, OscillatorType::Snare808, 0.25f}, {3.5f, 36, OscillatorType::Kick808, 0.25f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f}, {5.5f, 36, OscillatorType::Kick808, 0.25f},
    {6.0f, 38, OscillatorType::Snare808, 0.25f}, {7.5f, 36, OscillatorType::Kick808, 0.25f},
    {8.0f, 36, OscillatorType::Kick808, 0.5f}, {9.5f, 36, OscillatorType::Kick808, 0.25f},
    {10.0f, 38, OscillatorType::Snare808, 0.25f}, {11.5f, 36, OscillatorType::Kick808, 0.25f},
    {12.0f, 36, OscillatorType::Kick808, 0.5f}, {13.5f, 36, OscillatorType::Kick808, 0.25f},
    {14.0f, 38, OscillatorType::Snare808, 0.25f}, {15.5f, 36, OscillatorType::Kick808, 0.25f},
    // Hihats
    {0.0f, 42, OscillatorType::HiHat, 0.125f}, {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 46, OscillatorType::HiHatOpen, 0.25f}, {2.0f, 42, OscillatorType::HiHat, 0.125f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f}, {3.0f, 46, OscillatorType::HiHatOpen, 0.25f},
    {4.0f, 42, OscillatorType::HiHat, 0.125f}, {4.5f, 42, OscillatorType::HiHat, 0.125f},
    {5.0f, 46, OscillatorType::HiHatOpen, 0.25f}, {6.0f, 42, OscillatorType::HiHat, 0.125f},
    {6.5f, 42, OscillatorType::HiHat, 0.125f}, {7.0f, 46, OscillatorType::HiHatOpen, 0.25f},
    // === BASS (dark, syncopated) ===
    {0.0f, 26, OscillatorType::SynthwaveBass, 0.5f},   // D1
    {0.5f, 26, OscillatorType::SynthwaveBass, 0.25f},
    {1.0f, 38, OscillatorType::SynthwaveBass, 0.5f},   // D2
    {2.0f, 26, OscillatorType::SynthwaveBass, 0.5f},
    {3.0f, 26, OscillatorType::SynthwaveBass, 0.25f},
    {3.5f, 38, OscillatorType::SynthwaveBass, 0.5f},
    {4.0f, 22, OscillatorType::SynthwaveBass, 0.5f},   // Bb0
    {4.5f, 22, OscillatorType::SynthwaveBass, 0.25f},
    {5.0f, 34, OscillatorType::SynthwaveBass, 0.5f},   // Bb1
    {6.0f, 22, OscillatorType::SynthwaveBass, 0.5f},
    {7.0f, 22, OscillatorType::SynthwaveBass, 0.25f},
    {7.5f, 34, OscillatorType::SynthwaveBass, 0.5f},
    {8.0f, 29, OscillatorType::SynthwaveBass, 0.5f},   // F1
    {8.5f, 29, OscillatorType::SynthwaveBass, 0.25f},
    {9.0f, 41, OscillatorType::SynthwaveBass, 0.5f},   // F2
    {10.0f, 29, OscillatorType::SynthwaveBass, 0.5f},
    {11.0f, 29, OscillatorType::SynthwaveBass, 0.25f},
    {11.5f, 41, OscillatorType::SynthwaveBass, 0.5f},
    {12.0f, 24, OscillatorType::SynthwaveBass, 0.5f},  // C1
    {12.5f, 24, OscillatorType::SynthwaveBass, 0.25f},
    {13.0f, 36, OscillatorType::SynthwaveBass, 0.5f},  // C2
    {14.0f, 24, OscillatorType::SynthwaveBass, 0.5f},
    {15.0f, 24, OscillatorType::SynthwaveBass, 0.25f},
    {15.5f, 36, OscillatorType::SynthwaveBass, 0.5f},
    // === MELODY (darker, mysterious) ===
    {0.0f, 62, OscillatorType::SynthwaveLead, 1.0f},   // D4
    {1.0f, 65, OscillatorType::SynthwaveLead, 0.5f},   // F4
    {1.5f, 69, OscillatorType::SynthwaveLead, 0.5f},   // A4
    {2.0f, 70, OscillatorType::SynthwaveLead, 2.0f},   // Bb4
    {4.0f, 69, OscillatorType::SynthwaveLead, 0.5f},   // A4
    {4.5f, 65, OscillatorType::SynthwaveLead, 0.5f},   // F4
    {5.0f, 62, OscillatorType::SynthwaveLead, 1.0f},   // D4
    {6.0f, 60, OscillatorType::SynthwaveLead, 2.0f},   // C4
    {8.0f, 65, OscillatorType::SynthwaveLead, 1.0f},   // F4
    {9.0f, 69, OscillatorType::SynthwaveLead, 0.5f},   // A4
    {9.5f, 72, OscillatorType::SynthwaveLead, 0.5f},   // C5
    {10.0f, 74, OscillatorType::SynthwaveLead, 2.0f},  // D5
    {12.0f, 72, OscillatorType::SynthwaveLead, 0.5f},  // C5
    {12.5f, 69, OscillatorType::SynthwaveLead, 0.5f},  // A4
    {13.0f, 67, OscillatorType::SynthwaveLead, 1.0f},  // G4
    {14.0f, 65, OscillatorType::SynthwaveLead, 2.0f},  // F4
    // === PADS (Dm-Bb-F-C) ===
    {0.0f, 50, OscillatorType::SynthwavePad, 4.0f},    // D3
    {0.0f, 53, OscillatorType::SynthwavePad, 4.0f},    // F3
    {0.0f, 57, OscillatorType::SynthwavePad, 4.0f},    // A3
    {4.0f, 46, OscillatorType::SynthwavePad, 4.0f},    // Bb2
    {4.0f, 50, OscillatorType::SynthwavePad, 4.0f},    // D3
    {4.0f, 53, OscillatorType::SynthwavePad, 4.0f},    // F3
    {8.0f, 53, OscillatorType::SynthwavePad, 4.0f},    // F3
    {8.0f, 57, OscillatorType::SynthwavePad, 4.0f},    // A3
    {8.0f, 60, OscillatorType::SynthwavePad, 4.0f},    // C4
    {12.0f, 48, OscillatorType::SynthwavePad, 4.0f},   // C3
    {12.0f, 52, OscillatorType::SynthwavePad, 4.0f},   // E3
    {12.0f, 55, OscillatorType::SynthwavePad, 4.0f},   // G3
};

// Synthwave Track 8: "A Real Hero" - Drive soundtrack style, slow emotional (Dm-F-C-Am)
static const TrackNote g_SynthwaveRealHero[] = {
    // === DRUMS (minimal, slow) ===
    {0.0f, 36, OscillatorType::Kick808, 0.5f}, {4.0f, 38, OscillatorType::Snare808, 0.25f},
    {8.0f, 36, OscillatorType::Kick808, 0.5f}, {12.0f, 38, OscillatorType::Snare808, 0.25f},
    // Soft hihats
    {2.0f, 42, OscillatorType::HiHat, 0.125f}, {6.0f, 42, OscillatorType::HiHat, 0.125f},
    {10.0f, 42, OscillatorType::HiHat, 0.125f}, {14.0f, 42, OscillatorType::HiHat, 0.125f},
    // === BASS (slow, sustaining) ===
    {0.0f, 26, OscillatorType::SynthwaveBass, 7.5f},   // D1
    {8.0f, 29, OscillatorType::SynthwaveBass, 7.5f},   // F1
    // === MELODY (very slow, emotional) ===
    {0.0f, 69, OscillatorType::SynthwaveLead, 4.0f},   // A4
    {4.0f, 72, OscillatorType::SynthwaveLead, 2.0f},   // C5
    {6.0f, 74, OscillatorType::SynthwaveLead, 2.0f},   // D5
    {8.0f, 77, OscillatorType::SynthwaveLead, 4.0f},   // F5
    {12.0f, 76, OscillatorType::SynthwaveLead, 2.0f},  // E5
    {14.0f, 74, OscillatorType::SynthwaveLead, 2.0f},  // D5
    // === PADS (Dm-F) ===
    {0.0f, 50, OscillatorType::SynthwavePad, 8.0f},    // D3
    {0.0f, 53, OscillatorType::SynthwavePad, 8.0f},    // F3
    {0.0f, 57, OscillatorType::SynthwavePad, 8.0f},    // A3
    {8.0f, 53, OscillatorType::SynthwavePad, 8.0f},    // F3
    {8.0f, 57, OscillatorType::SynthwavePad, 8.0f},    // A3
    {8.0f, 60, OscillatorType::SynthwavePad, 8.0f},    // C4
};

// ===========================================
// TECHNO TRACKS
// ===========================================

// Techno Track 1: "Machine" - Minimal driving techno
static const TrackNote g_TechnoMachine[] = {
    // === DRUMS (4 on the floor) ===
    {0.0f, 36, OscillatorType::Kick, 0.25f}, {1.0f, 36, OscillatorType::Kick, 0.25f},
    {2.0f, 36, OscillatorType::Kick, 0.25f}, {3.0f, 36, OscillatorType::Kick, 0.25f},
    {4.0f, 36, OscillatorType::Kick, 0.25f}, {5.0f, 36, OscillatorType::Kick, 0.25f},
    {6.0f, 36, OscillatorType::Kick, 0.25f}, {7.0f, 36, OscillatorType::Kick, 0.25f},
    {8.0f, 36, OscillatorType::Kick, 0.25f}, {9.0f, 36, OscillatorType::Kick, 0.25f},
    {10.0f, 36, OscillatorType::Kick, 0.25f}, {11.0f, 36, OscillatorType::Kick, 0.25f},
    {12.0f, 36, OscillatorType::Kick, 0.25f}, {13.0f, 36, OscillatorType::Kick, 0.25f},
    {14.0f, 36, OscillatorType::Kick, 0.25f}, {15.0f, 36, OscillatorType::Kick, 0.25f},
    // Claps on 2 and 4
    {1.0f, 39, OscillatorType::Clap, 0.25f}, {3.0f, 39, OscillatorType::Clap, 0.25f},
    {5.0f, 39, OscillatorType::Clap, 0.25f}, {7.0f, 39, OscillatorType::Clap, 0.25f},
    {9.0f, 39, OscillatorType::Clap, 0.25f}, {11.0f, 39, OscillatorType::Clap, 0.25f},
    {13.0f, 39, OscillatorType::Clap, 0.25f}, {15.0f, 39, OscillatorType::Clap, 0.25f},
    // Offbeat hihats
    {0.5f, 42, OscillatorType::HiHat, 0.125f}, {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f}, {3.5f, 42, OscillatorType::HiHat, 0.125f},
    {4.5f, 42, OscillatorType::HiHat, 0.125f}, {5.5f, 42, OscillatorType::HiHat, 0.125f},
    {6.5f, 42, OscillatorType::HiHat, 0.125f}, {7.5f, 42, OscillatorType::HiHat, 0.125f},
    {8.5f, 42, OscillatorType::HiHat, 0.125f}, {9.5f, 42, OscillatorType::HiHat, 0.125f},
    {10.5f, 42, OscillatorType::HiHat, 0.125f}, {11.5f, 42, OscillatorType::HiHat, 0.125f},
    {12.5f, 42, OscillatorType::HiHat, 0.125f}, {13.5f, 42, OscillatorType::HiHat, 0.125f},
    {14.5f, 42, OscillatorType::HiHat, 0.125f}, {15.5f, 42, OscillatorType::HiHat, 0.125f},
    // === ACID BASS (TB-303 style) ===
    {0.0f, 36, OscillatorType::AcidBass, 0.25f}, {0.5f, 36, OscillatorType::AcidBass, 0.125f},
    {0.75f, 39, OscillatorType::AcidBass, 0.125f}, {1.0f, 36, OscillatorType::AcidBass, 0.25f},
    {1.5f, 48, OscillatorType::AcidBass, 0.125f}, {1.75f, 36, OscillatorType::AcidBass, 0.125f},
    {2.0f, 36, OscillatorType::AcidBass, 0.25f}, {2.5f, 36, OscillatorType::AcidBass, 0.125f},
    {2.75f, 41, OscillatorType::AcidBass, 0.125f}, {3.0f, 36, OscillatorType::AcidBass, 0.25f},
    {3.5f, 48, OscillatorType::AcidBass, 0.125f}, {3.75f, 36, OscillatorType::AcidBass, 0.125f},
    {4.0f, 36, OscillatorType::AcidBass, 0.25f}, {4.5f, 36, OscillatorType::AcidBass, 0.125f},
    {4.75f, 39, OscillatorType::AcidBass, 0.125f}, {5.0f, 36, OscillatorType::AcidBass, 0.25f},
    {5.5f, 48, OscillatorType::AcidBass, 0.125f}, {5.75f, 36, OscillatorType::AcidBass, 0.125f},
    {6.0f, 36, OscillatorType::AcidBass, 0.25f}, {6.5f, 36, OscillatorType::AcidBass, 0.125f},
    {6.75f, 43, OscillatorType::AcidBass, 0.125f}, {7.0f, 36, OscillatorType::AcidBass, 0.25f},
    {7.5f, 48, OscillatorType::AcidBass, 0.125f}, {7.75f, 36, OscillatorType::AcidBass, 0.125f},
    // Repeat pattern bars 9-16
    {8.0f, 36, OscillatorType::AcidBass, 0.25f}, {8.5f, 36, OscillatorType::AcidBass, 0.125f},
    {8.75f, 39, OscillatorType::AcidBass, 0.125f}, {9.0f, 36, OscillatorType::AcidBass, 0.25f},
    {9.5f, 48, OscillatorType::AcidBass, 0.125f}, {9.75f, 36, OscillatorType::AcidBass, 0.125f},
    {10.0f, 36, OscillatorType::AcidBass, 0.25f}, {10.5f, 36, OscillatorType::AcidBass, 0.125f},
    {10.75f, 41, OscillatorType::AcidBass, 0.125f}, {11.0f, 36, OscillatorType::AcidBass, 0.25f},
    {11.5f, 48, OscillatorType::AcidBass, 0.125f}, {11.75f, 36, OscillatorType::AcidBass, 0.125f},
    {12.0f, 36, OscillatorType::AcidBass, 0.25f}, {12.5f, 36, OscillatorType::AcidBass, 0.125f},
    {12.75f, 39, OscillatorType::AcidBass, 0.125f}, {13.0f, 36, OscillatorType::AcidBass, 0.25f},
    {13.5f, 48, OscillatorType::AcidBass, 0.125f}, {13.75f, 36, OscillatorType::AcidBass, 0.125f},
    {14.0f, 36, OscillatorType::AcidBass, 0.25f}, {14.5f, 36, OscillatorType::AcidBass, 0.125f},
    {14.75f, 43, OscillatorType::AcidBass, 0.125f}, {15.0f, 36, OscillatorType::AcidBass, 0.25f},
    {15.5f, 48, OscillatorType::AcidBass, 0.125f}, {15.75f, 36, OscillatorType::AcidBass, 0.125f},
};

// Techno Track 2: "Dark Factory" - Darker, harder
static const TrackNote g_TechnoDarkFactory[] = {
    // === DRUMS (harder kick) ===
    {0.0f, 36, OscillatorType::KickHard, 0.25f}, {1.0f, 36, OscillatorType::KickHard, 0.25f},
    {2.0f, 36, OscillatorType::KickHard, 0.25f}, {3.0f, 36, OscillatorType::KickHard, 0.25f},
    {4.0f, 36, OscillatorType::KickHard, 0.25f}, {5.0f, 36, OscillatorType::KickHard, 0.25f},
    {6.0f, 36, OscillatorType::KickHard, 0.25f}, {7.0f, 36, OscillatorType::KickHard, 0.25f},
    {8.0f, 36, OscillatorType::KickHard, 0.25f}, {9.0f, 36, OscillatorType::KickHard, 0.25f},
    {10.0f, 36, OscillatorType::KickHard, 0.25f}, {11.0f, 36, OscillatorType::KickHard, 0.25f},
    {12.0f, 36, OscillatorType::KickHard, 0.25f}, {13.0f, 36, OscillatorType::KickHard, 0.25f},
    {14.0f, 36, OscillatorType::KickHard, 0.25f}, {15.0f, 36, OscillatorType::KickHard, 0.25f},
    // Snare on 2 and 4
    {1.0f, 38, OscillatorType::Snare, 0.25f}, {3.0f, 38, OscillatorType::Snare, 0.25f},
    {5.0f, 38, OscillatorType::Snare, 0.25f}, {7.0f, 38, OscillatorType::Snare, 0.25f},
    {9.0f, 38, OscillatorType::Snare, 0.25f}, {11.0f, 38, OscillatorType::Snare, 0.25f},
    {13.0f, 38, OscillatorType::Snare, 0.25f}, {15.0f, 38, OscillatorType::Snare, 0.25f},
    // Fast hihats
    {0.25f, 42, OscillatorType::HiHat, 0.0625f}, {0.5f, 42, OscillatorType::HiHat, 0.0625f},
    {0.75f, 42, OscillatorType::HiHat, 0.0625f}, {1.25f, 42, OscillatorType::HiHat, 0.0625f},
    {1.5f, 42, OscillatorType::HiHat, 0.0625f}, {1.75f, 42, OscillatorType::HiHat, 0.0625f},
    {2.25f, 42, OscillatorType::HiHat, 0.0625f}, {2.5f, 42, OscillatorType::HiHat, 0.0625f},
    {2.75f, 42, OscillatorType::HiHat, 0.0625f}, {3.25f, 42, OscillatorType::HiHat, 0.0625f},
    {3.5f, 42, OscillatorType::HiHat, 0.0625f}, {3.75f, 42, OscillatorType::HiHat, 0.0625f},
    {4.25f, 42, OscillatorType::HiHat, 0.0625f}, {4.5f, 42, OscillatorType::HiHat, 0.0625f},
    {4.75f, 42, OscillatorType::HiHat, 0.0625f}, {5.25f, 42, OscillatorType::HiHat, 0.0625f},
    {5.5f, 42, OscillatorType::HiHat, 0.0625f}, {5.75f, 42, OscillatorType::HiHat, 0.0625f},
    {6.25f, 42, OscillatorType::HiHat, 0.0625f}, {6.5f, 42, OscillatorType::HiHat, 0.0625f},
    {6.75f, 42, OscillatorType::HiHat, 0.0625f}, {7.25f, 42, OscillatorType::HiHat, 0.0625f},
    {7.5f, 42, OscillatorType::HiHat, 0.0625f}, {7.75f, 42, OscillatorType::HiHat, 0.0625f},
    // === REESE BASS (detuned) ===
    {0.0f, 29, OscillatorType::Reese, 4.0f},   // F1
    {4.0f, 27, OscillatorType::Reese, 4.0f},   // Eb1
    {8.0f, 29, OscillatorType::Reese, 4.0f},   // F1
    {12.0f, 32, OscillatorType::Reese, 4.0f},  // Ab1
    // === STAB ===
    {0.0f, 53, OscillatorType::TechnoStab, 0.125f},
    {0.75f, 53, OscillatorType::TechnoStab, 0.125f},
    {4.0f, 51, OscillatorType::TechnoStab, 0.125f},
    {4.75f, 51, OscillatorType::TechnoStab, 0.125f},
    {8.0f, 53, OscillatorType::TechnoStab, 0.125f},
    {8.75f, 53, OscillatorType::TechnoStab, 0.125f},
    {12.0f, 56, OscillatorType::TechnoStab, 0.125f},
    {12.75f, 56, OscillatorType::TechnoStab, 0.125f},
};

// Techno Track 3: "Underground" - Rolling bass, hypnotic
static const TrackNote g_TechnoUnderground[] = {
    // === DRUMS ===
    {0.0f, 36, OscillatorType::Kick, 0.25f}, {1.0f, 36, OscillatorType::Kick, 0.25f},
    {2.0f, 36, OscillatorType::Kick, 0.25f}, {3.0f, 36, OscillatorType::Kick, 0.25f},
    {4.0f, 36, OscillatorType::Kick, 0.25f}, {5.0f, 36, OscillatorType::Kick, 0.25f},
    {6.0f, 36, OscillatorType::Kick, 0.25f}, {7.0f, 36, OscillatorType::Kick, 0.25f},
    // Rim on 2, clap on 4
    {1.0f, 37, OscillatorType::SnareRim, 0.125f}, {3.0f, 39, OscillatorType::Clap, 0.25f},
    {5.0f, 37, OscillatorType::SnareRim, 0.125f}, {7.0f, 39, OscillatorType::Clap, 0.25f},
    // Shaker
    {0.5f, 70, OscillatorType::Maracas, 0.125f}, {1.5f, 70, OscillatorType::Maracas, 0.125f},
    {2.5f, 70, OscillatorType::Maracas, 0.125f}, {3.5f, 70, OscillatorType::Maracas, 0.125f},
    {4.5f, 70, OscillatorType::Maracas, 0.125f}, {5.5f, 70, OscillatorType::Maracas, 0.125f},
    {6.5f, 70, OscillatorType::Maracas, 0.125f}, {7.5f, 70, OscillatorType::Maracas, 0.125f},
    // === ROLLING BASS (16th notes) ===
    {0.0f, 33, OscillatorType::SynthBass, 0.25f}, {0.25f, 33, OscillatorType::SynthBass, 0.125f},
    {0.5f, 33, OscillatorType::SynthBass, 0.25f}, {0.75f, 33, OscillatorType::SynthBass, 0.125f},
    {1.0f, 33, OscillatorType::SynthBass, 0.25f}, {1.25f, 33, OscillatorType::SynthBass, 0.125f},
    {1.5f, 33, OscillatorType::SynthBass, 0.25f}, {1.75f, 33, OscillatorType::SynthBass, 0.125f},
    {2.0f, 31, OscillatorType::SynthBass, 0.25f}, {2.25f, 31, OscillatorType::SynthBass, 0.125f},
    {2.5f, 31, OscillatorType::SynthBass, 0.25f}, {2.75f, 31, OscillatorType::SynthBass, 0.125f},
    {3.0f, 31, OscillatorType::SynthBass, 0.25f}, {3.25f, 31, OscillatorType::SynthBass, 0.125f},
    {3.5f, 31, OscillatorType::SynthBass, 0.25f}, {3.75f, 31, OscillatorType::SynthBass, 0.125f},
    {4.0f, 36, OscillatorType::SynthBass, 0.25f}, {4.25f, 36, OscillatorType::SynthBass, 0.125f},
    {4.5f, 36, OscillatorType::SynthBass, 0.25f}, {4.75f, 36, OscillatorType::SynthBass, 0.125f},
    {5.0f, 36, OscillatorType::SynthBass, 0.25f}, {5.25f, 36, OscillatorType::SynthBass, 0.125f},
    {5.5f, 36, OscillatorType::SynthBass, 0.25f}, {5.75f, 36, OscillatorType::SynthBass, 0.125f},
    {6.0f, 38, OscillatorType::SynthBass, 0.25f}, {6.25f, 38, OscillatorType::SynthBass, 0.125f},
    {6.5f, 38, OscillatorType::SynthBass, 0.25f}, {6.75f, 38, OscillatorType::SynthBass, 0.125f},
    {7.0f, 38, OscillatorType::SynthBass, 0.25f}, {7.25f, 38, OscillatorType::SynthBass, 0.125f},
    {7.5f, 38, OscillatorType::SynthBass, 0.25f}, {7.75f, 38, OscillatorType::SynthBass, 0.125f},
};

// ===========================================
// CHIPTUNE TRACKS
// ===========================================

// Chiptune Track 1: "Level 1" - Bouncy Mario-style
static const TrackNote g_ChiptuneLevel1[] = {
    // === DRUMS ===
    {0.0f, 36, OscillatorType::Kick, 0.25f}, {1.0f, 38, OscillatorType::Snare, 0.25f},
    {2.0f, 36, OscillatorType::Kick, 0.25f}, {2.5f, 36, OscillatorType::Kick, 0.125f},
    {3.0f, 38, OscillatorType::Snare, 0.25f},
    {4.0f, 36, OscillatorType::Kick, 0.25f}, {5.0f, 38, OscillatorType::Snare, 0.25f},
    {6.0f, 36, OscillatorType::Kick, 0.25f}, {6.5f, 36, OscillatorType::Kick, 0.125f},
    {7.0f, 38, OscillatorType::Snare, 0.25f},
    // Hihats
    {0.5f, 42, OscillatorType::HiHat, 0.125f}, {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f}, {3.5f, 42, OscillatorType::HiHat, 0.125f},
    {4.5f, 42, OscillatorType::HiHat, 0.125f}, {5.5f, 42, OscillatorType::HiHat, 0.125f},
    {6.5f, 42, OscillatorType::HiHat, 0.125f}, {7.5f, 42, OscillatorType::HiHat, 0.125f},
    // === BASS (Triangle - classic NES style) ===
    {0.0f, 48, OscillatorType::Triangle, 0.5f}, {0.5f, 48, OscillatorType::Triangle, 0.5f},
    {1.0f, 48, OscillatorType::Triangle, 0.5f}, {1.5f, 48, OscillatorType::Triangle, 0.5f},
    {2.0f, 53, OscillatorType::Triangle, 0.5f}, {2.5f, 53, OscillatorType::Triangle, 0.5f},
    {3.0f, 55, OscillatorType::Triangle, 0.5f}, {3.5f, 55, OscillatorType::Triangle, 0.5f},
    {4.0f, 48, OscillatorType::Triangle, 0.5f}, {4.5f, 48, OscillatorType::Triangle, 0.5f},
    {5.0f, 48, OscillatorType::Triangle, 0.5f}, {5.5f, 48, OscillatorType::Triangle, 0.5f},
    {6.0f, 55, OscillatorType::Triangle, 0.5f}, {6.5f, 55, OscillatorType::Triangle, 0.5f},
    {7.0f, 53, OscillatorType::Triangle, 0.5f}, {7.5f, 53, OscillatorType::Triangle, 0.5f},
    // === MELODY (Pulse - 12.5% duty for that NES sound) ===
    {0.0f, 72, OscillatorType::SynthChip, 0.25f},  // C5
    {0.25f, 76, OscillatorType::SynthChip, 0.25f}, // E5
    {0.5f, 79, OscillatorType::SynthChip, 0.5f},   // G5
    {1.0f, 84, OscillatorType::SynthChip, 0.5f},   // C6
    {1.5f, 79, OscillatorType::SynthChip, 0.25f},  // G5
    {1.75f, 76, OscillatorType::SynthChip, 0.25f}, // E5
    {2.0f, 77, OscillatorType::SynthChip, 0.5f},   // F5
    {2.5f, 81, OscillatorType::SynthChip, 0.5f},   // A5
    {3.0f, 79, OscillatorType::SynthChip, 0.5f},   // G5
    {3.5f, 76, OscillatorType::SynthChip, 0.5f},   // E5
    {4.0f, 72, OscillatorType::SynthChip, 0.25f},  // C5
    {4.25f, 76, OscillatorType::SynthChip, 0.25f}, // E5
    {4.5f, 79, OscillatorType::SynthChip, 0.5f},   // G5
    {5.0f, 84, OscillatorType::SynthChip, 0.5f},   // C6
    {5.5f, 86, OscillatorType::SynthChip, 0.25f},  // D6
    {5.75f, 84, OscillatorType::SynthChip, 0.25f}, // C6
    {6.0f, 79, OscillatorType::SynthChip, 1.0f},   // G5
    {7.0f, 77, OscillatorType::SynthChip, 0.5f},   // F5
    {7.5f, 76, OscillatorType::SynthChip, 0.5f},   // E5
    // === HARMONY (Pulse 2 - lower) ===
    {0.0f, 60, OscillatorType::Pulse, 0.5f},   // C4
    {0.5f, 64, OscillatorType::Pulse, 0.5f},   // E4
    {1.0f, 67, OscillatorType::Pulse, 0.5f},   // G4
    {1.5f, 64, OscillatorType::Pulse, 0.5f},   // E4
    {2.0f, 65, OscillatorType::Pulse, 0.5f},   // F4
    {2.5f, 69, OscillatorType::Pulse, 0.5f},   // A4
    {3.0f, 67, OscillatorType::Pulse, 0.5f},   // G4
    {3.5f, 64, OscillatorType::Pulse, 0.5f},   // E4
    {4.0f, 60, OscillatorType::Pulse, 0.5f},   // C4
    {4.5f, 64, OscillatorType::Pulse, 0.5f},   // E4
    {5.0f, 67, OscillatorType::Pulse, 0.5f},   // G4
    {5.5f, 72, OscillatorType::Pulse, 0.5f},   // C5
    {6.0f, 67, OscillatorType::Pulse, 1.0f},   // G4
    {7.0f, 65, OscillatorType::Pulse, 0.5f},   // F4
    {7.5f, 64, OscillatorType::Pulse, 0.5f},   // E4
};

// Chiptune Track 2: "Boss Fight" - Intense, faster
static const TrackNote g_ChiptuneBossFight[] = {
    // === DRUMS (fast and intense) ===
    {0.0f, 36, OscillatorType::KickHard, 0.25f}, {0.5f, 38, OscillatorType::Snare, 0.125f},
    {1.0f, 36, OscillatorType::KickHard, 0.25f}, {1.5f, 38, OscillatorType::Snare, 0.125f},
    {2.0f, 36, OscillatorType::KickHard, 0.25f}, {2.5f, 38, OscillatorType::Snare, 0.125f},
    {3.0f, 36, OscillatorType::KickHard, 0.25f}, {3.5f, 38, OscillatorType::Snare, 0.125f},
    {4.0f, 36, OscillatorType::KickHard, 0.25f}, {4.5f, 38, OscillatorType::Snare, 0.125f},
    {5.0f, 36, OscillatorType::KickHard, 0.25f}, {5.5f, 38, OscillatorType::Snare, 0.125f},
    {6.0f, 36, OscillatorType::KickHard, 0.25f}, {6.5f, 38, OscillatorType::Snare, 0.125f},
    {7.0f, 36, OscillatorType::KickHard, 0.25f}, {7.5f, 38, OscillatorType::Snare, 0.125f},
    // Fast hihats
    {0.25f, 42, OscillatorType::HiHat, 0.0625f}, {0.75f, 42, OscillatorType::HiHat, 0.0625f},
    {1.25f, 42, OscillatorType::HiHat, 0.0625f}, {1.75f, 42, OscillatorType::HiHat, 0.0625f},
    {2.25f, 42, OscillatorType::HiHat, 0.0625f}, {2.75f, 42, OscillatorType::HiHat, 0.0625f},
    {3.25f, 42, OscillatorType::HiHat, 0.0625f}, {3.75f, 42, OscillatorType::HiHat, 0.0625f},
    {4.25f, 42, OscillatorType::HiHat, 0.0625f}, {4.75f, 42, OscillatorType::HiHat, 0.0625f},
    {5.25f, 42, OscillatorType::HiHat, 0.0625f}, {5.75f, 42, OscillatorType::HiHat, 0.0625f},
    {6.25f, 42, OscillatorType::HiHat, 0.0625f}, {6.75f, 42, OscillatorType::HiHat, 0.0625f},
    {7.25f, 42, OscillatorType::HiHat, 0.0625f}, {7.75f, 42, OscillatorType::HiHat, 0.0625f},
    // === BASS (aggressive) ===
    {0.0f, 40, OscillatorType::Triangle, 0.25f}, {0.25f, 40, OscillatorType::Triangle, 0.25f},
    {0.5f, 40, OscillatorType::Triangle, 0.25f}, {0.75f, 40, OscillatorType::Triangle, 0.25f},
    {1.0f, 40, OscillatorType::Triangle, 0.25f}, {1.25f, 40, OscillatorType::Triangle, 0.25f},
    {1.5f, 40, OscillatorType::Triangle, 0.25f}, {1.75f, 40, OscillatorType::Triangle, 0.25f},
    {2.0f, 43, OscillatorType::Triangle, 0.25f}, {2.25f, 43, OscillatorType::Triangle, 0.25f},
    {2.5f, 43, OscillatorType::Triangle, 0.25f}, {2.75f, 43, OscillatorType::Triangle, 0.25f},
    {3.0f, 45, OscillatorType::Triangle, 0.25f}, {3.25f, 45, OscillatorType::Triangle, 0.25f},
    {3.5f, 45, OscillatorType::Triangle, 0.25f}, {3.75f, 45, OscillatorType::Triangle, 0.25f},
    {4.0f, 40, OscillatorType::Triangle, 0.25f}, {4.25f, 40, OscillatorType::Triangle, 0.25f},
    {4.5f, 40, OscillatorType::Triangle, 0.25f}, {4.75f, 40, OscillatorType::Triangle, 0.25f},
    {5.0f, 40, OscillatorType::Triangle, 0.25f}, {5.25f, 40, OscillatorType::Triangle, 0.25f},
    {5.5f, 40, OscillatorType::Triangle, 0.25f}, {5.75f, 40, OscillatorType::Triangle, 0.25f},
    {6.0f, 47, OscillatorType::Triangle, 0.25f}, {6.25f, 47, OscillatorType::Triangle, 0.25f},
    {6.5f, 47, OscillatorType::Triangle, 0.25f}, {6.75f, 47, OscillatorType::Triangle, 0.25f},
    {7.0f, 45, OscillatorType::Triangle, 0.25f}, {7.25f, 45, OscillatorType::Triangle, 0.25f},
    {7.5f, 43, OscillatorType::Triangle, 0.25f}, {7.75f, 43, OscillatorType::Triangle, 0.25f},
    // === MELODY (intense arpeggios) ===
    {0.0f, 64, OscillatorType::SynthChip, 0.125f},  // E4
    {0.125f, 67, OscillatorType::SynthChip, 0.125f}, // G4
    {0.25f, 71, OscillatorType::SynthChip, 0.125f},  // B4
    {0.375f, 76, OscillatorType::SynthChip, 0.125f}, // E5
    {0.5f, 71, OscillatorType::SynthChip, 0.125f},   // B4
    {0.625f, 67, OscillatorType::SynthChip, 0.125f}, // G4
    {0.75f, 64, OscillatorType::SynthChip, 0.125f},  // E4
    {0.875f, 67, OscillatorType::SynthChip, 0.125f}, // G4
    {1.0f, 64, OscillatorType::SynthChip, 0.125f},
    {1.125f, 67, OscillatorType::SynthChip, 0.125f},
    {1.25f, 71, OscillatorType::SynthChip, 0.125f},
    {1.375f, 76, OscillatorType::SynthChip, 0.125f},
    {1.5f, 71, OscillatorType::SynthChip, 0.125f},
    {1.625f, 67, OscillatorType::SynthChip, 0.125f},
    {1.75f, 64, OscillatorType::SynthChip, 0.125f},
    {1.875f, 67, OscillatorType::SynthChip, 0.125f},
    {2.0f, 67, OscillatorType::SynthChip, 0.125f},   // G4
    {2.125f, 70, OscillatorType::SynthChip, 0.125f}, // Bb4
    {2.25f, 74, OscillatorType::SynthChip, 0.125f},  // D5
    {2.375f, 79, OscillatorType::SynthChip, 0.125f}, // G5
    {2.5f, 74, OscillatorType::SynthChip, 0.125f},
    {2.625f, 70, OscillatorType::SynthChip, 0.125f},
    {2.75f, 67, OscillatorType::SynthChip, 0.125f},
    {2.875f, 70, OscillatorType::SynthChip, 0.125f},
    {3.0f, 69, OscillatorType::SynthChip, 0.125f},   // A4
    {3.125f, 72, OscillatorType::SynthChip, 0.125f}, // C5
    {3.25f, 76, OscillatorType::SynthChip, 0.125f},  // E5
    {3.375f, 81, OscillatorType::SynthChip, 0.125f}, // A5
    {3.5f, 76, OscillatorType::SynthChip, 0.125f},
    {3.625f, 72, OscillatorType::SynthChip, 0.125f},
    {3.75f, 69, OscillatorType::SynthChip, 0.125f},
    {3.875f, 72, OscillatorType::SynthChip, 0.125f},
    // Repeat with variation
    {4.0f, 64, OscillatorType::SynthChip, 0.125f},
    {4.125f, 67, OscillatorType::SynthChip, 0.125f},
    {4.25f, 71, OscillatorType::SynthChip, 0.125f},
    {4.375f, 76, OscillatorType::SynthChip, 0.125f},
    {4.5f, 79, OscillatorType::SynthChip, 0.125f},
    {4.625f, 76, OscillatorType::SynthChip, 0.125f},
    {4.75f, 71, OscillatorType::SynthChip, 0.125f},
    {4.875f, 67, OscillatorType::SynthChip, 0.125f},
    {5.0f, 64, OscillatorType::SynthChip, 0.125f},
    {5.125f, 67, OscillatorType::SynthChip, 0.125f},
    {5.25f, 71, OscillatorType::SynthChip, 0.125f},
    {5.375f, 76, OscillatorType::SynthChip, 0.125f},
    {5.5f, 79, OscillatorType::SynthChip, 0.125f},
    {5.625f, 83, OscillatorType::SynthChip, 0.125f},
    {5.75f, 79, OscillatorType::SynthChip, 0.125f},
    {5.875f, 76, OscillatorType::SynthChip, 0.125f},
    {6.0f, 71, OscillatorType::SynthChip, 0.5f},
    {6.5f, 74, OscillatorType::SynthChip, 0.5f},
    {7.0f, 76, OscillatorType::SynthChip, 0.5f},
    {7.5f, 74, OscillatorType::SynthChip, 0.5f},
};

// Chiptune Track 3: "Victory Theme" - Triumphant fanfare
static const TrackNote g_ChiptuneVictory[] = {
    // === DRUMS ===
    {0.0f, 36, OscillatorType::Kick, 0.25f},
    {1.0f, 38, OscillatorType::Snare, 0.25f}, {1.5f, 38, OscillatorType::Snare, 0.125f},
    {2.0f, 36, OscillatorType::Kick, 0.25f}, {2.5f, 36, OscillatorType::Kick, 0.125f},
    {3.0f, 38, OscillatorType::Snare, 0.25f},
    {4.0f, 36, OscillatorType::Kick, 0.25f},
    {5.0f, 38, OscillatorType::Snare, 0.25f}, {5.5f, 38, OscillatorType::Snare, 0.125f},
    {6.0f, 38, OscillatorType::Snare, 0.125f}, {6.25f, 38, OscillatorType::Snare, 0.125f},
    {6.5f, 38, OscillatorType::Snare, 0.125f}, {6.75f, 38, OscillatorType::Snare, 0.125f},
    {7.0f, 49, OscillatorType::Crash, 1.0f},
    // === BASS (triumphant) ===
    {0.0f, 48, OscillatorType::Triangle, 1.0f},  // C3
    {1.0f, 48, OscillatorType::Triangle, 1.0f},
    {2.0f, 53, OscillatorType::Triangle, 1.0f},  // F3
    {3.0f, 55, OscillatorType::Triangle, 1.0f},  // G3
    {4.0f, 48, OscillatorType::Triangle, 1.0f},  // C3
    {5.0f, 55, OscillatorType::Triangle, 1.0f},  // G3
    {6.0f, 53, OscillatorType::Triangle, 1.0f},  // F3
    {7.0f, 48, OscillatorType::Triangle, 1.0f},  // C3
    // === MELODY (fanfare) ===
    {0.0f, 72, OscillatorType::SynthChip, 0.5f},   // C5
    {0.5f, 72, OscillatorType::SynthChip, 0.25f},
    {0.75f, 74, OscillatorType::SynthChip, 0.25f}, // D5
    {1.0f, 76, OscillatorType::SynthChip, 1.0f},   // E5
    {2.0f, 77, OscillatorType::SynthChip, 0.5f},   // F5
    {2.5f, 79, OscillatorType::SynthChip, 0.5f},   // G5
    {3.0f, 84, OscillatorType::SynthChip, 1.0f},   // C6
    {4.0f, 84, OscillatorType::SynthChip, 0.25f},  // C6
    {4.25f, 83, OscillatorType::SynthChip, 0.25f}, // B5
    {4.5f, 84, OscillatorType::SynthChip, 0.25f},  // C6
    {4.75f, 86, OscillatorType::SynthChip, 0.25f}, // D6
    {5.0f, 88, OscillatorType::SynthChip, 1.0f},   // E6
    {6.0f, 91, OscillatorType::SynthChip, 0.5f},   // G6
    {6.5f, 88, OscillatorType::SynthChip, 0.5f},   // E6
    {7.0f, 84, OscillatorType::SynthChip, 1.0f},   // C6
    // === HARMONY ===
    {0.0f, 60, OscillatorType::Pulse, 0.5f},   // C4
    {0.5f, 60, OscillatorType::Pulse, 0.5f},
    {1.0f, 64, OscillatorType::Pulse, 1.0f},   // E4
    {2.0f, 65, OscillatorType::Pulse, 0.5f},   // F4
    {2.5f, 67, OscillatorType::Pulse, 0.5f},   // G4
    {3.0f, 72, OscillatorType::Pulse, 1.0f},   // C5
    {4.0f, 72, OscillatorType::Pulse, 0.5f},
    {4.5f, 72, OscillatorType::Pulse, 0.5f},
    {5.0f, 76, OscillatorType::Pulse, 1.0f},   // E5
    {6.0f, 79, OscillatorType::Pulse, 0.5f},   // G5
    {6.5f, 76, OscillatorType::Pulse, 0.5f},   // E5
    {7.0f, 72, OscillatorType::Pulse, 1.0f},   // C5
};

// ===========================================
// HIP HOP TRACKS
// ===========================================

// Hip Hop Track 1: "Boom Bap" - Classic 90s style
static const TrackNote g_HipHopBoomBap[] = {
    // === DRUMS (boom bap pattern) ===
    {0.0f, 36, OscillatorType::Kick808, 0.5f},
    {0.75f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 38, OscillatorType::Snare808, 0.25f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.25f, 36, OscillatorType::Kick808, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 38, OscillatorType::Snare808, 0.25f},
    {3.5f, 42, OscillatorType::HiHat, 0.125f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f},
    {4.75f, 42, OscillatorType::HiHat, 0.125f},
    {5.0f, 38, OscillatorType::Snare808, 0.25f},
    {5.5f, 42, OscillatorType::HiHat, 0.125f},
    {6.25f, 36, OscillatorType::Kick808, 0.25f},
    {6.5f, 42, OscillatorType::HiHat, 0.125f},
    {7.0f, 38, OscillatorType::Snare808, 0.25f},
    {7.5f, 42, OscillatorType::HiHatOpen, 0.25f},
    // === BASS (deep 808 sub) ===
    {0.0f, 33, OscillatorType::SubBass808, 2.0f},   // A1
    {2.25f, 36, OscillatorType::SubBass808, 0.5f},  // C2
    {3.0f, 33, OscillatorType::SubBass808, 1.0f},   // A1
    {4.0f, 33, OscillatorType::SubBass808, 2.0f},   // A1
    {6.25f, 38, OscillatorType::SubBass808, 0.5f},  // D2
    {7.0f, 36, OscillatorType::SubBass808, 1.0f},   // C2
    // === KEYS (lo-fi piano) ===
    {0.0f, 57, OscillatorType::LoFiKeys, 0.5f},    // A3
    {0.0f, 60, OscillatorType::LoFiKeys, 0.5f},    // C4
    {0.0f, 64, OscillatorType::LoFiKeys, 0.5f},    // E4
    {1.0f, 55, OscillatorType::LoFiKeys, 0.5f},    // G3
    {1.0f, 59, OscillatorType::LoFiKeys, 0.5f},    // B3
    {1.0f, 62, OscillatorType::LoFiKeys, 0.5f},    // D4
    {2.0f, 53, OscillatorType::LoFiKeys, 0.5f},    // F3
    {2.0f, 57, OscillatorType::LoFiKeys, 0.5f},    // A3
    {2.0f, 60, OscillatorType::LoFiKeys, 0.5f},    // C4
    {3.0f, 52, OscillatorType::LoFiKeys, 0.5f},    // E3
    {3.0f, 55, OscillatorType::LoFiKeys, 0.5f},    // G3
    {3.0f, 59, OscillatorType::LoFiKeys, 0.5f},    // B3
    {4.0f, 57, OscillatorType::LoFiKeys, 0.5f},
    {4.0f, 60, OscillatorType::LoFiKeys, 0.5f},
    {4.0f, 64, OscillatorType::LoFiKeys, 0.5f},
    {5.0f, 55, OscillatorType::LoFiKeys, 0.5f},
    {5.0f, 59, OscillatorType::LoFiKeys, 0.5f},
    {5.0f, 62, OscillatorType::LoFiKeys, 0.5f},
    {6.0f, 53, OscillatorType::LoFiKeys, 0.5f},
    {6.0f, 57, OscillatorType::LoFiKeys, 0.5f},
    {6.0f, 60, OscillatorType::LoFiKeys, 0.5f},
    {7.0f, 52, OscillatorType::LoFiKeys, 0.5f},
    {7.0f, 55, OscillatorType::LoFiKeys, 0.5f},
    {7.0f, 59, OscillatorType::LoFiKeys, 0.5f},
};

// Hip Hop Track 2: "Lo-Fi Chill" - Relaxed beats
static const TrackNote g_HipHopLoFi[] = {
    // === DRUMS (laid back) ===
    {0.0f, 36, OscillatorType::KickSoft, 0.5f},
    {1.0f, 42, OscillatorType::HiHat, 0.125f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 38, OscillatorType::Snare808, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 42, OscillatorType::HiHat, 0.125f},
    {3.5f, 42, OscillatorType::HiHat, 0.125f},
    {4.0f, 36, OscillatorType::KickSoft, 0.5f},
    {4.5f, 36, OscillatorType::KickSoft, 0.25f},
    {5.0f, 42, OscillatorType::HiHat, 0.125f},
    {5.5f, 42, OscillatorType::HiHat, 0.125f},
    {6.0f, 38, OscillatorType::Snare808, 0.25f},
    {6.5f, 42, OscillatorType::HiHat, 0.125f},
    {7.0f, 42, OscillatorType::HiHat, 0.125f},
    {7.5f, 42, OscillatorType::HiHatOpen, 0.25f},
    // === BASS (mellow) ===
    {0.0f, 41, OscillatorType::SynthBass, 1.5f},   // F2
    {2.0f, 43, OscillatorType::SynthBass, 1.5f},   // G2
    {4.0f, 45, OscillatorType::SynthBass, 1.5f},   // A2
    {6.0f, 43, OscillatorType::SynthBass, 1.5f},   // G2
    // === MELODY (jazzy) ===
    {0.0f, 65, OscillatorType::LoFiKeys, 0.75f},   // F4
    {1.0f, 68, OscillatorType::LoFiKeys, 0.5f},    // Ab4
    {1.5f, 65, OscillatorType::LoFiKeys, 0.5f},    // F4
    {2.0f, 67, OscillatorType::LoFiKeys, 1.0f},    // G4
    {3.0f, 65, OscillatorType::LoFiKeys, 0.5f},    // F4
    {3.5f, 63, OscillatorType::LoFiKeys, 0.5f},    // Eb4
    {4.0f, 65, OscillatorType::LoFiKeys, 0.75f},   // F4
    {5.0f, 70, OscillatorType::LoFiKeys, 0.5f},    // Bb4
    {5.5f, 68, OscillatorType::LoFiKeys, 0.5f},    // Ab4
    {6.0f, 67, OscillatorType::LoFiKeys, 1.0f},    // G4
    {7.0f, 65, OscillatorType::LoFiKeys, 0.5f},    // F4
    {7.5f, 63, OscillatorType::LoFiKeys, 0.5f},    // Eb4
};

// ===========================================
// TRAP TRACKS
// ===========================================

// Trap Track 1: "808 Bounce" - Hard hitting
static const TrackNote g_Trap808Bounce[] = {
    // === DRUMS (trap pattern with rolls) ===
    {0.0f, 36, OscillatorType::Kick808, 0.5f},
    {0.25f, 42, OscillatorType::HiHat, 0.0625f},
    {0.375f, 42, OscillatorType::HiHat, 0.0625f},
    {0.5f, 42, OscillatorType::HiHat, 0.0625f},
    {0.625f, 42, OscillatorType::HiHat, 0.0625f},
    {0.75f, 42, OscillatorType::HiHat, 0.0625f},
    {0.875f, 42, OscillatorType::HiHat, 0.0625f},
    {1.0f, 38, OscillatorType::Snare808, 0.25f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 36, OscillatorType::Kick808, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.0625f},
    {2.625f, 42, OscillatorType::HiHat, 0.0625f},
    {2.75f, 42, OscillatorType::HiHat, 0.0625f},
    {2.875f, 42, OscillatorType::HiHat, 0.0625f},
    {3.0f, 38, OscillatorType::Snare808, 0.25f},
    {3.25f, 36, OscillatorType::Kick808, 0.25f},
    {3.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.75f, 42, OscillatorType::HiHat, 0.125f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f},
    {4.25f, 42, OscillatorType::HiHat, 0.0625f},
    {4.375f, 42, OscillatorType::HiHat, 0.0625f},
    {4.5f, 42, OscillatorType::HiHat, 0.0625f},
    {4.625f, 42, OscillatorType::HiHat, 0.0625f},
    {4.75f, 42, OscillatorType::HiHat, 0.0625f},
    {4.875f, 42, OscillatorType::HiHat, 0.0625f},
    {5.0f, 38, OscillatorType::Snare808, 0.25f},
    {5.5f, 42, OscillatorType::HiHat, 0.125f},
    {6.0f, 36, OscillatorType::Kick808, 0.25f},
    {6.5f, 42, OscillatorType::HiHat, 0.0625f},
    {6.625f, 42, OscillatorType::HiHat, 0.0625f},
    {6.75f, 42, OscillatorType::HiHat, 0.0625f},
    {6.875f, 42, OscillatorType::HiHat, 0.0625f},
    {7.0f, 38, OscillatorType::Snare808, 0.25f},
    {7.25f, 36, OscillatorType::Kick808, 0.25f},
    {7.5f, 42, OscillatorType::HiHat, 0.125f},
    {7.75f, 42, OscillatorType::HiHatOpen, 0.125f},
    // === 808 BASS (sliding) ===
    {0.0f, 29, OscillatorType::SubBass808, 2.0f},   // F1
    {3.25f, 34, OscillatorType::SubBass808, 0.5f},  // Bb1
    {4.0f, 29, OscillatorType::SubBass808, 2.0f},   // F1
    {6.0f, 24, OscillatorType::SubBass808, 1.0f},   // C1
    {7.25f, 31, OscillatorType::SubBass808, 0.5f},  // G1
    // === LEAD (trap melody) ===
    {0.0f, 77, OscillatorType::TrapLead, 0.5f},    // F5
    {0.5f, 75, OscillatorType::TrapLead, 0.25f},   // Eb5
    {0.75f, 72, OscillatorType::TrapLead, 0.25f},  // C5
    {1.0f, 70, OscillatorType::TrapLead, 0.5f},    // Bb4
    {2.0f, 72, OscillatorType::TrapLead, 0.5f},    // C5
    {2.5f, 70, OscillatorType::TrapLead, 0.5f},    // Bb4
    {3.0f, 65, OscillatorType::TrapLead, 1.0f},    // F4
    {4.0f, 77, OscillatorType::TrapLead, 0.5f},    // F5
    {4.5f, 79, OscillatorType::TrapLead, 0.25f},   // G5
    {4.75f, 77, OscillatorType::TrapLead, 0.25f},  // F5
    {5.0f, 75, OscillatorType::TrapLead, 0.5f},    // Eb5
    {6.0f, 72, OscillatorType::TrapLead, 0.5f},    // C5
    {6.5f, 70, OscillatorType::TrapLead, 0.5f},    // Bb4
    {7.0f, 67, OscillatorType::TrapLead, 1.0f},    // G4
};

// Trap Track 2: "Dark Trap" - Moody atmosphere
static const TrackNote g_TrapDark[] = {
    // === DRUMS ===
    {0.0f, 36, OscillatorType::Kick808, 0.5f},
    {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 38, OscillatorType::Clap, 0.25f},
    {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 36, OscillatorType::Kick808, 0.25f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.75f, 42, OscillatorType::HiHat, 0.0625f},
    {3.0f, 38, OscillatorType::Clap, 0.25f},
    {3.5f, 42, OscillatorType::HiHat, 0.0625f},
    {3.625f, 42, OscillatorType::HiHat, 0.0625f},
    {3.75f, 42, OscillatorType::HiHat, 0.0625f},
    {3.875f, 42, OscillatorType::HiHat, 0.0625f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f},
    {4.5f, 42, OscillatorType::HiHat, 0.125f},
    {5.0f, 38, OscillatorType::Clap, 0.25f},
    {5.5f, 42, OscillatorType::HiHat, 0.125f},
    {6.0f, 36, OscillatorType::Kick808, 0.25f},
    {6.25f, 36, OscillatorType::Kick808, 0.25f},
    {6.5f, 42, OscillatorType::HiHat, 0.125f},
    {7.0f, 38, OscillatorType::Clap, 0.25f},
    {7.5f, 42, OscillatorType::HiHatOpen, 0.25f},
    // === BASS ===
    {0.0f, 28, OscillatorType::SubBass808, 2.0f},   // E1
    {2.0f, 33, OscillatorType::SubBass808, 1.0f},   // A1
    {3.0f, 31, OscillatorType::SubBass808, 1.0f},   // G1
    {4.0f, 28, OscillatorType::SubBass808, 2.0f},   // E1
    {6.0f, 26, OscillatorType::SubBass808, 1.0f},   // D1
    {7.0f, 28, OscillatorType::SubBass808, 1.0f},   // E1
    // === PAD (dark atmosphere) ===
    {0.0f, 52, OscillatorType::SynthwavePad, 4.0f},  // E3
    {0.0f, 55, OscillatorType::SynthwavePad, 4.0f},  // G3
    {0.0f, 59, OscillatorType::SynthwavePad, 4.0f},  // B3
    {4.0f, 50, OscillatorType::SynthwavePad, 4.0f},  // D3
    {4.0f, 54, OscillatorType::SynthwavePad, 4.0f},  // F#3
    {4.0f, 57, OscillatorType::SynthwavePad, 4.0f},  // A3
    // === MELODY ===
    {0.0f, 76, OscillatorType::TrapLead, 0.5f},    // E5
    {0.5f, 74, OscillatorType::TrapLead, 0.5f},    // D5
    {1.0f, 71, OscillatorType::TrapLead, 1.0f},    // B4
    {2.0f, 69, OscillatorType::TrapLead, 0.5f},    // A4
    {2.5f, 67, OscillatorType::TrapLead, 0.5f},    // G4
    {3.0f, 64, OscillatorType::TrapLead, 1.0f},    // E4
    {4.0f, 66, OscillatorType::TrapLead, 0.5f},    // F#4
    {4.5f, 69, OscillatorType::TrapLead, 0.5f},    // A4
    {5.0f, 71, OscillatorType::TrapLead, 1.0f},    // B4
    {6.0f, 74, OscillatorType::TrapLead, 0.5f},    // D5
    {6.5f, 71, OscillatorType::TrapLead, 0.5f},    // B4
    {7.0f, 69, OscillatorType::TrapLead, 1.0f},    // A4
};

// ===========================================
// HOUSE TRACKS
// ===========================================

// House Track 1: "Disco House" - Funky groovy
static const TrackNote g_HouseDiscoHouse[] = {
    // === DRUMS (4 on the floor with open hats) ===
    {0.0f, 36, OscillatorType::Kick, 0.25f}, {1.0f, 36, OscillatorType::Kick, 0.25f},
    {2.0f, 36, OscillatorType::Kick, 0.25f}, {3.0f, 36, OscillatorType::Kick, 0.25f},
    {4.0f, 36, OscillatorType::Kick, 0.25f}, {5.0f, 36, OscillatorType::Kick, 0.25f},
    {6.0f, 36, OscillatorType::Kick, 0.25f}, {7.0f, 36, OscillatorType::Kick, 0.25f},
    // Claps on 2 and 4
    {1.0f, 39, OscillatorType::Clap, 0.25f}, {3.0f, 39, OscillatorType::Clap, 0.25f},
    {5.0f, 39, OscillatorType::Clap, 0.25f}, {7.0f, 39, OscillatorType::Clap, 0.25f},
    // Open hats on offbeats
    {0.5f, 46, OscillatorType::HiHatOpen, 0.25f}, {1.5f, 46, OscillatorType::HiHatOpen, 0.25f},
    {2.5f, 46, OscillatorType::HiHatOpen, 0.25f}, {3.5f, 46, OscillatorType::HiHatOpen, 0.25f},
    {4.5f, 46, OscillatorType::HiHatOpen, 0.25f}, {5.5f, 46, OscillatorType::HiHatOpen, 0.25f},
    {6.5f, 46, OscillatorType::HiHatOpen, 0.25f}, {7.5f, 46, OscillatorType::HiHatOpen, 0.25f},
    // === BASS (funky octave jumps) ===
    {0.0f, 36, OscillatorType::SynthBass, 0.25f},  // C2
    {0.25f, 48, OscillatorType::SynthBass, 0.125f}, // C3
    {0.5f, 36, OscillatorType::SynthBass, 0.25f},
    {0.75f, 48, OscillatorType::SynthBass, 0.125f},
    {1.0f, 36, OscillatorType::SynthBass, 0.25f},
    {1.25f, 48, OscillatorType::SynthBass, 0.125f},
    {1.5f, 38, OscillatorType::SynthBass, 0.25f},  // D2
    {1.75f, 50, OscillatorType::SynthBass, 0.125f}, // D3
    {2.0f, 41, OscillatorType::SynthBass, 0.25f},  // F2
    {2.25f, 53, OscillatorType::SynthBass, 0.125f}, // F3
    {2.5f, 41, OscillatorType::SynthBass, 0.25f},
    {2.75f, 53, OscillatorType::SynthBass, 0.125f},
    {3.0f, 41, OscillatorType::SynthBass, 0.25f},
    {3.25f, 53, OscillatorType::SynthBass, 0.125f},
    {3.5f, 43, OscillatorType::SynthBass, 0.25f},  // G2
    {3.75f, 55, OscillatorType::SynthBass, 0.125f}, // G3
    {4.0f, 36, OscillatorType::SynthBass, 0.25f},
    {4.25f, 48, OscillatorType::SynthBass, 0.125f},
    {4.5f, 36, OscillatorType::SynthBass, 0.25f},
    {4.75f, 48, OscillatorType::SynthBass, 0.125f},
    {5.0f, 36, OscillatorType::SynthBass, 0.25f},
    {5.25f, 48, OscillatorType::SynthBass, 0.125f},
    {5.5f, 38, OscillatorType::SynthBass, 0.25f},
    {5.75f, 50, OscillatorType::SynthBass, 0.125f},
    {6.0f, 41, OscillatorType::SynthBass, 0.25f},
    {6.25f, 53, OscillatorType::SynthBass, 0.125f},
    {6.5f, 41, OscillatorType::SynthBass, 0.25f},
    {6.75f, 53, OscillatorType::SynthBass, 0.125f},
    {7.0f, 43, OscillatorType::SynthBass, 0.25f},
    {7.25f, 55, OscillatorType::SynthBass, 0.125f},
    {7.5f, 41, OscillatorType::SynthBass, 0.25f},
    {7.75f, 53, OscillatorType::SynthBass, 0.125f},
    // === CHORDS (stabby) ===
    {0.0f, 60, OscillatorType::SynthwaveChord, 0.25f},  // Cm
    {0.0f, 63, OscillatorType::SynthwaveChord, 0.25f},
    {0.0f, 67, OscillatorType::SynthwaveChord, 0.25f},
    {0.5f, 60, OscillatorType::SynthwaveChord, 0.25f},
    {0.5f, 63, OscillatorType::SynthwaveChord, 0.25f},
    {0.5f, 67, OscillatorType::SynthwaveChord, 0.25f},
    {2.0f, 65, OscillatorType::SynthwaveChord, 0.25f},  // Fm
    {2.0f, 68, OscillatorType::SynthwaveChord, 0.25f},
    {2.0f, 72, OscillatorType::SynthwaveChord, 0.25f},
    {2.5f, 65, OscillatorType::SynthwaveChord, 0.25f},
    {2.5f, 68, OscillatorType::SynthwaveChord, 0.25f},
    {2.5f, 72, OscillatorType::SynthwaveChord, 0.25f},
    {4.0f, 60, OscillatorType::SynthwaveChord, 0.25f},  // Cm
    {4.0f, 63, OscillatorType::SynthwaveChord, 0.25f},
    {4.0f, 67, OscillatorType::SynthwaveChord, 0.25f},
    {4.5f, 60, OscillatorType::SynthwaveChord, 0.25f},
    {4.5f, 63, OscillatorType::SynthwaveChord, 0.25f},
    {4.5f, 67, OscillatorType::SynthwaveChord, 0.25f},
    {6.0f, 67, OscillatorType::SynthwaveChord, 0.25f},  // G
    {6.0f, 71, OscillatorType::SynthwaveChord, 0.25f},
    {6.0f, 74, OscillatorType::SynthwaveChord, 0.25f},
    {6.5f, 67, OscillatorType::SynthwaveChord, 0.25f},
    {6.5f, 71, OscillatorType::SynthwaveChord, 0.25f},
    {6.5f, 74, OscillatorType::SynthwaveChord, 0.25f},
};

// House Track 2: "Deep House" - Moody and deep
static const TrackNote g_HouseDeepHouse[] = {
    // === DRUMS ===
    {0.0f, 36, OscillatorType::KickSoft, 0.25f}, {1.0f, 36, OscillatorType::KickSoft, 0.25f},
    {2.0f, 36, OscillatorType::KickSoft, 0.25f}, {3.0f, 36, OscillatorType::KickSoft, 0.25f},
    {4.0f, 36, OscillatorType::KickSoft, 0.25f}, {5.0f, 36, OscillatorType::KickSoft, 0.25f},
    {6.0f, 36, OscillatorType::KickSoft, 0.25f}, {7.0f, 36, OscillatorType::KickSoft, 0.25f},
    // Rim on 2 and 4
    {1.0f, 37, OscillatorType::SnareRim, 0.125f}, {3.0f, 37, OscillatorType::SnareRim, 0.125f},
    {5.0f, 37, OscillatorType::SnareRim, 0.125f}, {7.0f, 37, OscillatorType::SnareRim, 0.125f},
    // Shaker
    {0.5f, 70, OscillatorType::Maracas, 0.125f}, {1.5f, 70, OscillatorType::Maracas, 0.125f},
    {2.5f, 70, OscillatorType::Maracas, 0.125f}, {3.5f, 70, OscillatorType::Maracas, 0.125f},
    {4.5f, 70, OscillatorType::Maracas, 0.125f}, {5.5f, 70, OscillatorType::Maracas, 0.125f},
    {6.5f, 70, OscillatorType::Maracas, 0.125f}, {7.5f, 70, OscillatorType::Maracas, 0.125f},
    // === BASS (deep and minimal) ===
    {0.0f, 33, OscillatorType::SynthBass, 2.0f},   // A1
    {2.0f, 36, OscillatorType::SynthBass, 2.0f},   // C2
    {4.0f, 33, OscillatorType::SynthBass, 2.0f},   // A1
    {6.0f, 31, OscillatorType::SynthBass, 2.0f},   // G1
    // === PAD (atmospheric) ===
    {0.0f, 57, OscillatorType::SynthPad, 4.0f},    // Am
    {0.0f, 60, OscillatorType::SynthPad, 4.0f},
    {0.0f, 64, OscillatorType::SynthPad, 4.0f},
    {4.0f, 55, OscillatorType::SynthPad, 4.0f},    // Gmaj7
    {4.0f, 59, OscillatorType::SynthPad, 4.0f},
    {4.0f, 62, OscillatorType::SynthPad, 4.0f},
    {4.0f, 66, OscillatorType::SynthPad, 4.0f},
    // === MELODY (sparse) ===
    {0.0f, 69, OscillatorType::SynthLead, 0.5f},   // A4
    {2.0f, 72, OscillatorType::SynthLead, 0.5f},   // C5
    {4.0f, 71, OscillatorType::SynthLead, 0.5f},   // B4
    {4.5f, 69, OscillatorType::SynthLead, 0.5f},   // A4
    {6.0f, 67, OscillatorType::SynthLead, 1.0f},   // G4
};

// =============================================================================
// REGGAETON SAMPLE TRACKS - Dembow rhythm at ~95 BPM
// =============================================================================

// Reggaeton Track 1: "Perreo" - Classic dembow beat
static const TrackNote g_ReggaetonPerreo[] = {
    // === DEMBOW DRUMS (8 beats, classic kick-snare pattern) ===
    // Kick on 1, 2.5, 5, 6.5 (boom-ka pattern)
    {0.0f, 36, OscillatorType::Dembow808, 0.25f}, {2.5f, 36, OscillatorType::Dembow808, 0.25f},
    {4.0f, 36, OscillatorType::Dembow808, 0.25f}, {6.5f, 36, OscillatorType::Dembow808, 0.25f},
    // Snare on 2, 4, 6, 8 (the backbeat)
    {1.0f, 38, OscillatorType::Snare808, 0.125f}, {3.0f, 38, OscillatorType::Snare808, 0.125f},
    {5.0f, 38, OscillatorType::Snare808, 0.125f}, {7.0f, 38, OscillatorType::Snare808, 0.125f},
    // Guira (scraped metal) on every 8th note
    {0.0f, 60, OscillatorType::Guira, 0.125f}, {0.5f, 60, OscillatorType::Guira, 0.125f},
    {1.0f, 60, OscillatorType::Guira, 0.125f}, {1.5f, 60, OscillatorType::Guira, 0.125f},
    {2.0f, 60, OscillatorType::Guira, 0.125f}, {2.5f, 60, OscillatorType::Guira, 0.125f},
    {3.0f, 60, OscillatorType::Guira, 0.125f}, {3.5f, 60, OscillatorType::Guira, 0.125f},
    {4.0f, 60, OscillatorType::Guira, 0.125f}, {4.5f, 60, OscillatorType::Guira, 0.125f},
    {5.0f, 60, OscillatorType::Guira, 0.125f}, {5.5f, 60, OscillatorType::Guira, 0.125f},
    {6.0f, 60, OscillatorType::Guira, 0.125f}, {6.5f, 60, OscillatorType::Guira, 0.125f},
    {7.0f, 60, OscillatorType::Guira, 0.125f}, {7.5f, 60, OscillatorType::Guira, 0.125f},
    // === BASS (punchy reggaeton bass following the dembow) ===
    {0.0f, 33, OscillatorType::ReggaetonBass, 0.5f},   // A1
    {2.5f, 33, OscillatorType::ReggaetonBass, 0.5f},   // A1
    {4.0f, 36, OscillatorType::ReggaetonBass, 0.5f},   // C2
    {6.5f, 36, OscillatorType::ReggaetonBass, 0.5f},   // C2
    // === BRASS STABS (on offbeats for hooks) ===
    {1.5f, 69, OscillatorType::LatinBrass, 0.25f},     // A4
    {5.5f, 69, OscillatorType::LatinBrass, 0.25f},     // A4
    // === MELODY (simple Latin hook) ===
    {0.0f, 72, OscillatorType::SynthLead, 0.5f},       // C5
    {0.75f, 71, OscillatorType::SynthLead, 0.25f},     // B4
    {1.25f, 69, OscillatorType::SynthLead, 0.5f},      // A4
    {4.0f, 72, OscillatorType::SynthLead, 0.5f},       // C5
    {4.75f, 74, OscillatorType::SynthLead, 0.25f},     // D5
    {5.25f, 72, OscillatorType::SynthLead, 0.75f},     // C5
};

// Reggaeton Track 2: "Gasolina" - Energetic party dembow
static const TrackNote g_ReggaetonGasolina[] = {
    // === DRUMS (high energy dembow with extra hi-hats) ===
    // Kick pattern (more aggressive)
    {0.0f, 36, OscillatorType::Dembow808, 0.25f}, {0.75f, 36, OscillatorType::Dembow808, 0.125f},
    {2.5f, 36, OscillatorType::Dembow808, 0.25f},
    {4.0f, 36, OscillatorType::Dembow808, 0.25f}, {4.75f, 36, OscillatorType::Dembow808, 0.125f},
    {6.5f, 36, OscillatorType::Dembow808, 0.25f},
    // Snare/clap combo
    {1.0f, 39, OscillatorType::Clap, 0.125f}, {3.0f, 39, OscillatorType::Clap, 0.125f},
    {5.0f, 39, OscillatorType::Clap, 0.125f}, {7.0f, 39, OscillatorType::Clap, 0.125f},
    // Open hi-hat on &s
    {0.5f, 46, OscillatorType::HiHatOpen, 0.125f}, {2.5f, 46, OscillatorType::HiHatOpen, 0.125f},
    {4.5f, 46, OscillatorType::HiHatOpen, 0.125f}, {6.5f, 46, OscillatorType::HiHatOpen, 0.125f},
    // Bongo fills
    {1.75f, 60, OscillatorType::Bongo, 0.125f}, {3.75f, 60, OscillatorType::Bongo, 0.125f},
    {5.75f, 60, OscillatorType::Bongo, 0.125f}, {7.75f, 60, OscillatorType::Bongo, 0.125f},
    // Timbale accent
    {3.5f, 60, OscillatorType::Timbale, 0.125f}, {7.5f, 60, OscillatorType::Timbale, 0.125f},
    // === BASS (syncopated reggaeton bass) ===
    {0.0f, 36, OscillatorType::ReggaetonBass, 0.375f},   // C2
    {2.5f, 38, OscillatorType::ReggaetonBass, 0.375f},   // D2
    {4.0f, 33, OscillatorType::ReggaetonBass, 0.375f},   // A1
    {6.5f, 35, OscillatorType::ReggaetonBass, 0.375f},   // B1
    // === BRASS (energetic stabs) ===
    {0.0f, 60, OscillatorType::LatinBrass, 0.25f},       // C4
    {0.0f, 64, OscillatorType::LatinBrass, 0.25f},       // E4
    {0.0f, 67, OscillatorType::LatinBrass, 0.25f},       // G4
    {3.0f, 62, OscillatorType::LatinBrass, 0.25f},       // D4
    {3.0f, 65, OscillatorType::LatinBrass, 0.25f},       // F4
    {3.0f, 69, OscillatorType::LatinBrass, 0.25f},       // A4
    // === MELODY (catchy hook) ===
    {0.5f, 72, OscillatorType::SynthwaveLead, 0.5f},     // C5
    {1.25f, 74, OscillatorType::SynthwaveLead, 0.25f},   // D5
    {1.75f, 76, OscillatorType::SynthwaveLead, 0.75f},   // E5
    {4.5f, 79, OscillatorType::SynthwaveLead, 0.5f},     // G5
    {5.25f, 77, OscillatorType::SynthwaveLead, 0.25f},   // F5
    {5.75f, 76, OscillatorType::SynthwaveLead, 0.75f},   // E5
};



// =============================================================================
// NEW RECREATION TRACKS
// =============================================================================

// Reggaeton Track 3: "Noche" - Dark/moody reggaeton
static const TrackNote g_ReggaetonNoche[] = {
    // === DRUMS (slower, moodier dembow) ===
    // Deep kick
    {0.0f, 36, OscillatorType::Kick808, 0.5f}, {2.5f, 36, OscillatorType::Kick808, 0.5f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f}, {6.5f, 36, OscillatorType::Kick808, 0.5f},
    // Snare (softer)
    {1.0f, 38, OscillatorType::SnareRim, 0.125f}, {3.0f, 38, OscillatorType::SnareRim, 0.125f},
    {5.0f, 38, OscillatorType::SnareRim, 0.125f}, {7.0f, 38, OscillatorType::SnareRim, 0.125f},
    // Closed hi-hat pattern
    {0.0f, 42, OscillatorType::HiHat, 0.125f}, {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 42, OscillatorType::HiHat, 0.125f}, {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.0f, 42, OscillatorType::HiHat, 0.125f}, {2.5f, 42, OscillatorType::HiHat, 0.125f},
    {3.0f, 42, OscillatorType::HiHat, 0.125f}, {3.5f, 42, OscillatorType::HiHat, 0.125f},
    {4.0f, 42, OscillatorType::HiHat, 0.125f}, {4.5f, 42, OscillatorType::HiHat, 0.125f},
    {5.0f, 42, OscillatorType::HiHat, 0.125f}, {5.5f, 42, OscillatorType::HiHat, 0.125f},
    {6.0f, 42, OscillatorType::HiHat, 0.125f}, {6.5f, 42, OscillatorType::HiHat, 0.125f},
    {7.0f, 42, OscillatorType::HiHat, 0.125f}, {7.5f, 42, OscillatorType::HiHat, 0.125f},
    // Conga accent
    {1.5f, 63, OscillatorType::Conga, 0.25f}, {5.5f, 63, OscillatorType::Conga, 0.25f},
    // === BASS (dark, minimal) ===
    {0.0f, 33, OscillatorType::ReggaetonBass, 1.0f},     // A1
    {2.5f, 33, OscillatorType::ReggaetonBass, 0.5f},     // A1
    {4.0f, 31, OscillatorType::ReggaetonBass, 1.0f},     // G1
    {6.5f, 31, OscillatorType::ReggaetonBass, 0.5f},     // G1
    // === PAD (dark atmosphere) ===
    {0.0f, 57, OscillatorType::SynthwavePad, 4.0f},      // Am chord
    {0.0f, 60, OscillatorType::SynthwavePad, 4.0f},
    {0.0f, 64, OscillatorType::SynthwavePad, 4.0f},
    {4.0f, 55, OscillatorType::SynthwavePad, 4.0f},      // Gm chord
    {4.0f, 58, OscillatorType::SynthwavePad, 4.0f},
    {4.0f, 62, OscillatorType::SynthwavePad, 4.0f},
    // === MELODY (haunting, sparse) ===
    {0.0f, 69, OscillatorType::SynthBell, 1.0f},         // A4
    {2.0f, 67, OscillatorType::SynthBell, 0.5f},         // G4
    {3.0f, 65, OscillatorType::SynthBell, 1.0f},         // F4
    {6.0f, 64, OscillatorType::SynthBell, 2.0f},         // E4
};

// 1. HOME - "Resonance" (Synthwave)
// Warm detuned saw pad, pluck lead, subby sine/saw bass
static const TrackNote g_SynthwaveResonance[] = {
    // Drums
    {0.0f, 36, OscillatorType::Kick808, 0.5f}, {2.0f, 38, OscillatorType::Snare808, 0.25f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f}, {6.0f, 38, OscillatorType::Snare808, 0.25f},
    {8.0f, 36, OscillatorType::Kick808, 0.5f}, {10.0f, 38, OscillatorType::Snare808, 0.25f},
    {12.0f, 36, OscillatorType::Kick808, 0.5f}, {14.0f, 38, OscillatorType::Snare808, 0.25f},
    // Bass
    {0.0f, 24, OscillatorType::SynthwaveBass, 3.5f}, {4.0f, 29, OscillatorType::SynthwaveBass, 3.5f},
    {8.0f, 26, OscillatorType::SynthwaveBass, 3.5f}, {12.0f, 31, OscillatorType::SynthwaveBass, 3.5f},
    // Pad
    {0.0f, 48, OscillatorType::SynthwavePad, 4.0f}, {0.0f, 52, OscillatorType::SynthwavePad, 4.0f}, {0.0f, 55, OscillatorType::SynthwavePad, 4.0f},
    {4.0f, 53, OscillatorType::SynthwavePad, 4.0f}, {4.0f, 57, OscillatorType::SynthwavePad, 4.0f}, {4.0f, 60, OscillatorType::SynthwavePad, 4.0f},
    {8.0f, 50, OscillatorType::SynthwavePad, 4.0f}, {8.0f, 53, OscillatorType::SynthwavePad, 4.0f}, {8.0f, 57, OscillatorType::SynthwavePad, 4.0f},
    {12.0f, 55, OscillatorType::SynthwavePad, 4.0f}, {12.0f, 59, OscillatorType::SynthwavePad, 4.0f}, {12.0f, 62, OscillatorType::SynthwavePad, 4.0f},
    // Lead
    {0.0f, 72, OscillatorType::SynthPluck, 0.5f}, {0.75f, 74, OscillatorType::SynthPluck, 0.5f},
    {1.5f, 76, OscillatorType::SynthPluck, 0.5f}, {2.5f, 79, OscillatorType::SynthPluck, 0.5f},
};

// 2. Instupendo - "Comfort Chain" (Lo-Fi)
// Bell/mallety lead, airy pad, gentle sub, lo-fi hats
static const TrackNote g_LofiComfortChain[] = {
    // Drums
    {0.0f, 36, OscillatorType::KickSoft, 0.5f}, {2.0f, 37, OscillatorType::SnareRim, 0.25f},
    {4.0f, 36, OscillatorType::KickSoft, 0.5f}, {6.0f, 37, OscillatorType::SnareRim, 0.25f},
    {8.0f, 36, OscillatorType::KickSoft, 0.5f}, {10.0f, 37, OscillatorType::SnareRim, 0.25f},
    {12.0f, 36, OscillatorType::KickSoft, 0.5f}, {14.0f, 37, OscillatorType::SnareRim, 0.25f},
    {0.5f, 42, OscillatorType::HiHat, 0.125f}, {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f}, {3.5f, 42, OscillatorType::HiHat, 0.125f},
    // Bass
    {0.0f, 33, OscillatorType::SubBass808, 4.0f}, {4.0f, 36, OscillatorType::SubBass808, 4.0f},
    // Lead (Bell)
    {0.0f, 69, OscillatorType::SynthBell, 1.0f}, {1.0f, 72, OscillatorType::SynthBell, 1.0f},
    {2.0f, 76, OscillatorType::SynthBell, 1.0f}, {3.0f, 74, OscillatorType::SynthBell, 1.0f},
    {4.0f, 72, OscillatorType::SynthBell, 1.0f}, {5.0f, 69, OscillatorType::SynthBell, 1.0f},
};

// 3. Juice WRLD - "All Girls Are the Same" (Trap)
// Plucked guitar lead, 808 bass, trap hats
static const TrackNote g_TrapAllGirls[] = {
    // Drums
    {0.0f, 36, OscillatorType::Kick808, 0.5f}, {2.5f, 36, OscillatorType::Kick808, 0.25f},
    {3.0f, 38, OscillatorType::Snare808, 0.25f}, {7.0f, 38, OscillatorType::Snare808, 0.25f},
    {0.0f, 42, OscillatorType::HiHat, 0.125f}, {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 42, OscillatorType::HiHat, 0.125f}, {1.25f, 42, OscillatorType::HiHat, 0.0625f},
    {1.5f, 42, OscillatorType::HiHat, 0.0625f}, {1.75f, 42, OscillatorType::HiHat, 0.125f},
    // Bass
    {0.0f, 29, OscillatorType::SubBass808, 2.5f}, {3.0f, 34, OscillatorType::SubBass808, 1.0f},
    {4.0f, 29, OscillatorType::SubBass808, 2.5f}, {7.0f, 36, OscillatorType::SubBass808, 1.0f},
    // Lead (Pluck)
    {0.0f, 65, OscillatorType::SynthPluck, 0.5f}, {0.5f, 67, OscillatorType::SynthPluck, 0.5f},
    {1.0f, 69, OscillatorType::SynthPluck, 0.5f}, {1.5f, 65, OscillatorType::SynthPluck, 0.5f},
    {2.0f, 67, OscillatorType::SynthPluck, 0.5f}, {2.5f, 62, OscillatorType::SynthPluck, 0.5f},
};

// 4. Playboi Carti - "Broke Boi" (Hip Hop/Trap)
// Simple square/saw lead, sub/808 bass, thin hats
static const TrackNote g_HipHopBrokeBoi[] = {
    // Drums
    {0.0f, 36, OscillatorType::Kick808, 0.5f}, {2.0f, 39, OscillatorType::Clap, 0.25f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f}, {6.0f, 39, OscillatorType::Clap, 0.25f},
    {0.0f, 42, OscillatorType::HiHat, 0.125f}, {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 42, OscillatorType::HiHat, 0.125f}, {1.5f, 42, OscillatorType::HiHat, 0.125f},
    // Bass
    {0.0f, 31, OscillatorType::SubBass808, 2.0f}, {2.0f, 26, OscillatorType::SubBass808, 2.0f},
    {4.0f, 31, OscillatorType::SubBass808, 2.0f}, {6.0f, 33, OscillatorType::SubBass808, 2.0f},
    // Lead
    {0.0f, 74, OscillatorType::Pulse, 0.5f}, {0.5f, 71, OscillatorType::Pulse, 0.5f},
    {1.0f, 69, OscillatorType::Pulse, 0.5f}, {1.5f, 67, OscillatorType::Pulse, 0.5f},
};

// 5. Crystal Waters - "Gypsy Woman" (House)
// Organ/house piano stab, M1-style organ bass
static const TrackNote g_HouseGypsyWoman[] = {
    // Drums
    {0.0f, 36, OscillatorType::Kick, 0.25f}, {1.0f, 36, OscillatorType::Kick, 0.25f},
    {2.0f, 36, OscillatorType::Kick, 0.25f}, {3.0f, 36, OscillatorType::Kick, 0.25f},
    {1.0f, 39, OscillatorType::Clap, 0.25f}, {3.0f, 39, OscillatorType::Clap, 0.25f},
    {0.5f, 46, OscillatorType::HiHatOpen, 0.25f}, {1.5f, 46, OscillatorType::HiHatOpen, 0.25f},
    {2.5f, 46, OscillatorType::HiHatOpen, 0.25f}, {3.5f, 46, OscillatorType::HiHatOpen, 0.25f},
    // Bass
    {0.0f, 36, OscillatorType::SynthOrgan, 0.5f}, {0.5f, 48, OscillatorType::SynthOrgan, 0.25f},
    {1.0f, 36, OscillatorType::SynthOrgan, 0.5f}, {1.5f, 43, OscillatorType::SynthOrgan, 0.25f},
    {2.0f, 36, OscillatorType::SynthOrgan, 0.5f}, {2.5f, 48, OscillatorType::SynthOrgan, 0.25f},
    // Chords
    {0.0f, 60, OscillatorType::SynthOrgan, 0.25f}, {0.0f, 63, OscillatorType::SynthOrgan, 0.25f}, {0.0f, 67, OscillatorType::SynthOrgan, 0.25f},
    {1.5f, 60, OscillatorType::SynthOrgan, 0.25f}, {1.5f, 63, OscillatorType::SynthOrgan, 0.25f}, {1.5f, 67, OscillatorType::SynthOrgan, 0.25f},
    {3.0f, 60, OscillatorType::SynthOrgan, 0.25f}, {3.0f, 63, OscillatorType::SynthOrgan, 0.25f}, {3.0f, 67, OscillatorType::SynthOrgan, 0.25f},
};

// 6. Juice WRLD - "Hide" (Trap)
// Clean bell/mallet lead, lush pad, sub/808
static const TrackNote g_TrapHide[] = {
    // Drums
    {0.0f, 36, OscillatorType::Kick808, 0.5f}, {3.0f, 38, OscillatorType::Snare808, 0.25f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f}, {7.0f, 38, OscillatorType::Snare808, 0.25f},
    {0.0f, 42, OscillatorType::HiHat, 0.125f}, {0.5f, 42, OscillatorType::HiHat, 0.125f},
    {1.0f, 42, OscillatorType::HiHat, 0.125f}, {1.5f, 42, OscillatorType::HiHat, 0.125f},
    // Bass
    {0.0f, 31, OscillatorType::SubBass808, 3.0f}, {4.0f, 26, OscillatorType::SubBass808, 3.0f},
    // Lead
    {0.0f, 72, OscillatorType::SynthBell, 0.5f}, {0.5f, 74, OscillatorType::SynthBell, 0.5f},
    {1.0f, 76, OscillatorType::SynthBell, 0.5f}, {1.5f, 72, OscillatorType::SynthBell, 0.5f},
    {2.0f, 79, OscillatorType::SynthBell, 0.5f}, {3.0f, 76, OscillatorType::SynthBell, 0.5f},
};

// 7. Sam Gellaitry - "Assumptions" (Future Bass)
// Detuned saw leads, filtered plucks, heavy sidechain
static const TrackNote g_FutureBassAssumptions[] = {
    // Drums
    {0.0f, 36, OscillatorType::KickHard, 0.5f}, {2.0f, 38, OscillatorType::Snare808, 0.25f},
    {4.0f, 36, OscillatorType::KickHard, 0.5f}, {6.0f, 38, OscillatorType::Snare808, 0.25f},
    // Bass
    {0.0f, 33, OscillatorType::SynthBass, 2.0f}, {2.0f, 33, OscillatorType::SynthBass, 2.0f},
    {4.0f, 33, OscillatorType::SynthBass, 2.0f}, {6.0f, 33, OscillatorType::SynthBass, 2.0f},
    // Chords (Supersaw)
    {0.0f, 57, OscillatorType::Supersaw, 2.0f}, {0.0f, 60, OscillatorType::Supersaw, 2.0f}, {0.0f, 64, OscillatorType::Supersaw, 2.0f},
    {2.0f, 57, OscillatorType::Supersaw, 2.0f}, {2.0f, 60, OscillatorType::Supersaw, 2.0f}, {2.0f, 64, OscillatorType::Supersaw, 2.0f},
    {4.0f, 53, OscillatorType::Supersaw, 2.0f}, {4.0f, 57, OscillatorType::Supersaw, 2.0f}, {4.0f, 60, OscillatorType::Supersaw, 2.0f},
    {6.0f, 53, OscillatorType::Supersaw, 2.0f}, {6.0f, 57, OscillatorType::Supersaw, 2.0f}, {6.0f, 60, OscillatorType::Supersaw, 2.0f},
    // Lead
    {0.0f, 76, OscillatorType::SynthLead, 0.5f}, {0.5f, 74, OscillatorType::SynthLead, 0.5f},
    {1.0f, 72, OscillatorType::SynthLead, 0.5f}, {1.5f, 69, OscillatorType::SynthLead, 0.5f},
};

// 8. Ark Patrol - "Let Go" (Electronic/Ambient)
// Airy vocal-like lead, wide pad, sub bass
static const TrackNote g_ElectronicLetGo[] = {
    // Drums
    {0.0f, 36, OscillatorType::KickSoft, 0.5f}, {2.0f, 38, OscillatorType::SnareRim, 0.25f},
    {4.0f, 36, OscillatorType::KickSoft, 0.5f}, {6.0f, 38, OscillatorType::SnareRim, 0.25f},
    // Bass
    {0.0f, 33, OscillatorType::SubBass808, 4.0f}, {4.0f, 36, OscillatorType::SubBass808, 4.0f},
    // Pad
    {0.0f, 57, OscillatorType::SynthPad, 4.0f}, {0.0f, 60, OscillatorType::SynthPad, 4.0f}, {0.0f, 64, OscillatorType::SynthPad, 4.0f},
    {4.0f, 53, OscillatorType::SynthPad, 4.0f}, {4.0f, 57, OscillatorType::SynthPad, 4.0f}, {4.0f, 60, OscillatorType::SynthPad, 4.0f},
    // Lead
    {0.0f, 76, OscillatorType::Sine, 1.0f}, {1.0f, 74, OscillatorType::Sine, 1.0f},
    {2.0f, 72, OscillatorType::Sine, 1.0f}, {3.0f, 69, OscillatorType::Sine, 1.0f},
};

// 9. INTERWORLD - "METAMORPHOSIS" (Phonk)
// Aggressive reese bass, sharp pluck lead, hard trap drums
static const TrackNote g_PhonkMetamorphosis[] = {
    // Drums
    {0.0f, 36, OscillatorType::KickHard, 0.5f}, {2.0f, 38, OscillatorType::Snare808, 0.25f},
    {4.0f, 36, OscillatorType::KickHard, 0.5f}, {6.0f, 38, OscillatorType::Snare808, 0.25f},
    {0.0f, 56, OscillatorType::Cowbell, 0.25f}, {0.5f, 56, OscillatorType::Cowbell, 0.25f},
    {1.0f, 56, OscillatorType::Cowbell, 0.25f}, {1.5f, 56, OscillatorType::Cowbell, 0.25f},
    // Bass (Reese)
    {0.0f, 29, OscillatorType::Reese, 4.0f}, {4.0f, 31, OscillatorType::Reese, 4.0f},
    {8.0f, 33, OscillatorType::Reese, 4.0f}, {12.0f, 31, OscillatorType::Reese, 4.0f},
    // Lead (Cowbell Melody)
    {0.0f, 72, OscillatorType::Cowbell, 0.25f}, {0.25f, 72, OscillatorType::Cowbell, 0.25f},
    {0.5f, 75, OscillatorType::Cowbell, 0.25f}, {0.75f, 72, OscillatorType::Cowbell, 0.25f},
    {1.0f, 70, OscillatorType::Cowbell, 0.25f}, {1.25f, 67, OscillatorType::Cowbell, 0.25f},
    {1.5f, 70, OscillatorType::Cowbell, 0.25f}, {1.75f, 67, OscillatorType::Cowbell, 0.25f},
};

// 10. Narvent/VOJ - "Memory Reboot" (Ambient/Synthwave)
// Lush pad, deep sub sine, soft keys
static const TrackNote g_AmbientMemoryReboot[] = {
    // Bass
    {0.0f, 33, OscillatorType::SubBass808, 8.0f}, {8.0f, 29, OscillatorType::SubBass808, 8.0f},
    // Pad
    {0.0f, 57, OscillatorType::SynthPad, 8.0f}, {0.0f, 60, OscillatorType::SynthPad, 8.0f}, {0.0f, 64, OscillatorType::SynthPad, 8.0f},
    {8.0f, 53, OscillatorType::SynthPad, 8.0f}, {8.0f, 57, OscillatorType::SynthPad, 8.0f}, {8.0f, 60, OscillatorType::SynthPad, 8.0f},
    // Keys
    {0.0f, 72, OscillatorType::SynthBell, 2.0f}, {2.0f, 76, OscillatorType::SynthBell, 2.0f},
    {4.0f, 79, OscillatorType::SynthBell, 2.0f}, {6.0f, 76, OscillatorType::SynthBell, 2.0f},
};

// 11. "7 Weeks & 3 Days" (Lo-Fi)
// Chill lo-fi keys, pad, sub
static const TrackNote g_Lofi7Weeks[] = {
    // Drums
    {0.0f, 36, OscillatorType::KickSoft, 0.5f}, {2.0f, 37, OscillatorType::SnareRim, 0.25f},
    {4.0f, 36, OscillatorType::KickSoft, 0.5f}, {6.0f, 37, OscillatorType::SnareRim, 0.25f},
    // Bass
    {0.0f, 33, OscillatorType::SubBass808, 4.0f}, {4.0f, 36, OscillatorType::SubBass808, 4.0f},
    // Keys
    {0.0f, 60, OscillatorType::LoFiKeys, 0.5f}, {0.0f, 64, OscillatorType::LoFiKeys, 0.5f}, {0.0f, 67, OscillatorType::LoFiKeys, 0.5f},
    {1.0f, 60, OscillatorType::LoFiKeys, 0.5f}, {1.0f, 64, OscillatorType::LoFiKeys, 0.5f}, {1.0f, 67, OscillatorType::LoFiKeys, 0.5f},
    {2.0f, 62, OscillatorType::LoFiKeys, 0.5f}, {2.0f, 65, OscillatorType::LoFiKeys, 0.5f}, {2.0f, 69, OscillatorType::LoFiKeys, 0.5f},
};

// 12. The Midnight - "Vampires" (Synthwave)
// 80s synth brass/pad, sax/lead, analog bass
static const TrackNote g_SynthwaveVampires[] = {
    // Drums
    {0.0f, 36, OscillatorType::Kick808, 0.5f}, {2.0f, 38, OscillatorType::Snare808, 0.25f},
    {4.0f, 36, OscillatorType::Kick808, 0.5f}, {6.0f, 38, OscillatorType::Snare808, 0.25f},
    {0.5f, 42, OscillatorType::HiHat, 0.125f}, {1.5f, 42, OscillatorType::HiHat, 0.125f},
    {2.5f, 42, OscillatorType::HiHat, 0.125f}, {3.5f, 42, OscillatorType::HiHat, 0.125f},
    // Bass
    {0.0f, 29, OscillatorType::SynthwaveBass, 0.5f}, {0.5f, 29, OscillatorType::SynthwaveBass, 0.5f},
    {1.0f, 29, OscillatorType::SynthwaveBass, 0.5f}, {1.5f, 29, OscillatorType::SynthwaveBass, 0.5f},
    {2.0f, 29, OscillatorType::SynthwaveBass, 0.5f}, {2.5f, 29, OscillatorType::SynthwaveBass, 0.5f},
    // Brass
    {0.0f, 60, OscillatorType::SynthBrass, 0.5f}, {0.0f, 64, OscillatorType::SynthBrass, 0.5f}, {0.0f, 67, OscillatorType::SynthBrass, 0.5f},
    {2.0f, 60, OscillatorType::SynthBrass, 0.5f}, {2.0f, 64, OscillatorType::SynthBrass, 0.5f}, {2.0f, 67, OscillatorType::SynthBrass, 0.5f},
    // Sax Lead
    {0.0f, 72, OscillatorType::SynthLead, 1.5f}, {1.5f, 74, OscillatorType::SynthLead, 0.5f},
    {2.0f, 76, OscillatorType::SynthLead, 2.0f}, {4.0f, 72, OscillatorType::SynthLead, 4.0f},
};

// Array of all sample tracks
// fixedPosition=true means notes are placed at their exact beat positions (starting from beat 0)
static const SampleTrack g_SampleTracks[] = {
    // Synthwave (8 tracks)
    {"Midnight Drive", "Synthwave", "Driving 80s retrowave", g_SynthwaveMidnightDrive, sizeof(g_SynthwaveMidnightDrive)/sizeof(TrackNote), 16, 110, true},
    {"Neon Dreams", "Synthwave", "Dreamy arpeggiated", g_SynthwaveNeonDreams, sizeof(g_SynthwaveNeonDreams)/sizeof(TrackNote), 16, 100, true},
    {"Retro Racer", "Synthwave", "Energetic driving", g_SynthwaveRetroRacer, sizeof(g_SynthwaveRetroRacer)/sizeof(TrackNote), 16, 118, true},
    {"Nightcall", "Synthwave", "Kavinsky style pulsing", g_SynthwaveNightcall, sizeof(g_SynthwaveNightcall)/sizeof(TrackNote), 16, 92, true},
    {"Turbo Killer", "Synthwave", "Aggressive Carpenter Brut", g_SynthwaveTurboKiller, sizeof(g_SynthwaveTurboKiller)/sizeof(TrackNote), 8, 128, true},
    {"Endless Summer", "Synthwave", "The Midnight emotional", g_SynthwaveEndlessSummer, sizeof(g_SynthwaveEndlessSummer)/sizeof(TrackNote), 16, 105, true},
    {"Tech Noir", "Synthwave", "Gunship dark cyberpunk", g_SynthwaveTechNoir, sizeof(g_SynthwaveTechNoir)/sizeof(TrackNote), 16, 108, true},
    {"A Real Hero", "Synthwave", "Drive soundtrack emotional", g_SynthwaveRealHero, sizeof(g_SynthwaveRealHero)/sizeof(TrackNote), 16, 85, true},
    // Techno
    {"Machine", "Techno", "Acid techno groove", g_TechnoMachine, sizeof(g_TechnoMachine)/sizeof(TrackNote), 16, 130, true},
    {"Dark Factory", "Techno", "Hard dark techno", g_TechnoDarkFactory, sizeof(g_TechnoDarkFactory)/sizeof(TrackNote), 16, 135, true},
    {"Underground", "Techno", "Rolling hypnotic", g_TechnoUnderground, sizeof(g_TechnoUnderground)/sizeof(TrackNote), 8, 126, true},
    // Chiptune
    {"Level 1", "Chiptune", "Bouncy game theme", g_ChiptuneLevel1, sizeof(g_ChiptuneLevel1)/sizeof(TrackNote), 8, 140, true},
    {"Boss Fight", "Chiptune", "Intense battle music", g_ChiptuneBossFight, sizeof(g_ChiptuneBossFight)/sizeof(TrackNote), 8, 160, true},
    {"Victory Theme", "Chiptune", "Triumphant fanfare", g_ChiptuneVictory, sizeof(g_ChiptuneVictory)/sizeof(TrackNote), 8, 120, true},
    // Hip Hop
    {"Boom Bap", "Hip Hop", "Classic 90s beat", g_HipHopBoomBap, sizeof(g_HipHopBoomBap)/sizeof(TrackNote), 8, 90, true},
    {"Lo-Fi Chill", "Hip Hop", "Relaxed lo-fi", g_HipHopLoFi, sizeof(g_HipHopLoFi)/sizeof(TrackNote), 8, 85, true},
    // Trap
    {"808 Bounce", "Trap", "Hard hitting 808", g_Trap808Bounce, sizeof(g_Trap808Bounce)/sizeof(TrackNote), 8, 140, true},
    {"Dark Trap", "Trap", "Moody atmosphere", g_TrapDark, sizeof(g_TrapDark)/sizeof(TrackNote), 8, 135, true},
    // House
    {"Disco House", "House", "Funky groovy", g_HouseDiscoHouse, sizeof(g_HouseDiscoHouse)/sizeof(TrackNote), 8, 124, true},
    {"Deep House", "House", "Moody and deep", g_HouseDeepHouse, sizeof(g_HouseDeepHouse)/sizeof(TrackNote), 8, 122, true},
    // Reggaeton
    {"Perreo", "Reggaeton", "Classic dembow beat", g_ReggaetonPerreo, sizeof(g_ReggaetonPerreo)/sizeof(TrackNote), 8, 95, true},
    {"Gasolina", "Reggaeton", "Energetic party dembow", g_ReggaetonGasolina, sizeof(g_ReggaetonGasolina)/sizeof(TrackNote), 8, 100, true},
    {"Noche", "Reggaeton", "Dark moody reggaeton", g_ReggaetonNoche, sizeof(g_ReggaetonNoche)/sizeof(TrackNote), 8, 90, true},
    // Recreations
    {"Resonance", "Synthwave", "HOME style pad & lead", g_SynthwaveResonance, sizeof(g_SynthwaveResonance)/sizeof(TrackNote), 16, 90, true},
    {"Comfort Chain", "Hip Hop", "Instupendo lo-fi vibe", g_LofiComfortChain, sizeof(g_LofiComfortChain)/sizeof(TrackNote), 16, 85, true},
    {"All Girls Same", "Trap", "Juice WRLD guitar trap", g_TrapAllGirls, sizeof(g_TrapAllGirls)/sizeof(TrackNote), 8, 140, true},
    {"Broke Boi", "Trap", "Playboi Carti minimalism", g_HipHopBrokeBoi, sizeof(g_HipHopBrokeBoi)/sizeof(TrackNote), 8, 135, true},
    {"Gypsy Woman", "House", "Crystal Waters organ", g_HouseGypsyWoman, sizeof(g_HouseGypsyWoman)/sizeof(TrackNote), 8, 120, true},
    {"Hide", "Trap", "Juice WRLD melodic", g_TrapHide, sizeof(g_TrapHide)/sizeof(TrackNote), 8, 140, true},
    {"Assumptions", "Future Bass", "Sam Gellaitry style", g_FutureBassAssumptions, sizeof(g_FutureBassAssumptions)/sizeof(TrackNote), 8, 130, true},
    {"Let Go", "Ambient", "Ark Patrol atmospheric", g_ElectronicLetGo, sizeof(g_ElectronicLetGo)/sizeof(TrackNote), 8, 100, true},
    {"METAMORPHOSIS", "Phonk", "INTERWORLD drift phonk", g_PhonkMetamorphosis, sizeof(g_PhonkMetamorphosis)/sizeof(TrackNote), 8, 125, true},
    {"Memory Reboot", "Ambient", "Narvent lush synthwave", g_AmbientMemoryReboot, sizeof(g_AmbientMemoryReboot)/sizeof(TrackNote), 16, 95, true},
    {"7 Weeks", "Hip Hop", "Lo-fi chill keys", g_Lofi7Weeks, sizeof(g_Lofi7Weeks)/sizeof(TrackNote), 8, 80, true},
    {"Vampires", "Synthwave", "The Midnight sax/brass", g_SynthwaveVampires, sizeof(g_SynthwaveVampires)/sizeof(TrackNote), 8, 105, true},
};
static constexpr int g_NumSampleTracks = sizeof(g_SampleTracks) / sizeof(g_SampleTracks[0]);

// Sample track preview state
static bool g_IsSampleTrackPreviewing = false;
static int g_PreviewSampleTrackIndex = -1;

// Palette expansion state for sample tracks
static bool g_PaletteExpanded_SampleTracks = false;
static bool g_PaletteExpanded_SynthwaveTracks = false;
static bool g_PaletteExpanded_TechnoTracks = false;
static bool g_PaletteExpanded_ChiptuneTracks = false;
static bool g_PaletteExpanded_HipHopTracks = false;
static bool g_PaletteExpanded_TrapTracks = false;
static bool g_PaletteExpanded_HouseTracks = false;
static bool g_PaletteExpanded_ReggaetonTracks = false;

// Global undo/redo history
// Holding Alt drops to no snap for one gesture, which is how a note gets
// placed deliberately off the grid without changing the setting.
inline SnapDivision effectiveSnap(const UIState& ui) {
    return ImGui::GetIO().KeyAlt ? SnapDivision::Off : ui.snapDivision;
}

/*
 * Project-aware snapping.
 *
 * Every call site used to pass project.beatsPerMeasure - one number, which
 * is the right bar length only while the meter never changes. These
 * overloads route Bar through the meter map so the grid snaps to the bar
 * line the ruler actually drew, and leave every other division exactly as it
 * was, since a 1/16 is a 1/16 in any time signature.
 *
 * They are overloads rather than renamed functions so that the fourteen
 * existing call sites keep reading the way they did.
 */
inline float snapBeat(float beat, SnapDivision division, const Project& project) {
    return snapBeatMapped(beat, division, project.tempoMap, project.beatsPerMeasure);
}

inline float snapBeatNearest(float beat, SnapDivision division, const Project& project) {
    return snapBeatNearestMapped(beat, division, project.tempoMap,
                                 project.beatsPerMeasure);
}

// A duration is a length, not a position, so it takes the bar length in
// force where it sits rather than walking the map.
inline float snapDuration(float beats, SnapDivision division, const Project& project) {
    const MeterChange meter = project.meterAt(0.0f);
    const int barBeats = std::max(1, static_cast<int>(
        std::lround(TempoMap::barLengthBeats(meter))));
    return snapDuration(beats, division, barBeats);
}

// Scale state. These used to live beside the other Tools globals, several
// thousand lines below the piano roll - which is precisely why the "Snap to
// Scale" checkbox set a flag that nothing in the note-placement path could
// read. Declared here, the placement code can actually honour it.
static int g_ToolsScaleType = 0;       // 0=Major, 1=Minor, 2=Dorian, ...
static int g_ToolsScaleRoot = 0;       // Root note, 0=C
static bool g_ToolsScaleLock = false;  // Snap placed notes to the scale
static bool g_ToolsScaleHighlight = true;

// Set by the capture harness so a screenshot can show a section that is
// collapsed by default.
static bool g_ExpandChipAccuracy = false;
static bool g_ExpandEffectRack = false;

/*
 * A header and its force-open flag, as ONE call.
 *
 * Written after finding that `g_ExpandChipAccuracy` was opening the Aux
 * Buses header. Nothing was wrong with either line; the Chip Accuracy
 * section had been moved further down at some point and the SetNextItemOpen
 * stayed behind, silently attaching itself to whichever header came next.
 *
 * Nothing could have caught it. The unit tests never draw, and the smoke
 * test compares distinct colour counts rather than content - so a shot named
 * master-bus-chip-accuracy photographed an open Aux Buses panel for
 * however long, and passed.
 *
 * Binding the two together is the fix, because it removes the failure rather
 * than correcting this instance of it: a later edit that moves the header
 * takes its opener with it, and there is no loose statement left to strand.
 */
inline bool CollapsingHeaderExpandable(const char* label, bool forceOpen) {
    if (forceOpen) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    return ImGui::CollapsingHeader(label);
}

// The wavetable editor's two new sections, for the same reason: a shot of a
// closed header shows nothing about what is inside it, and a headless test
// that never opens one never draws the widgets that can break.
static bool g_ExpandWaveTools = false;

// What the last chip-patch import did, shown until dismissed. An import that
// was refused and looks like nothing happening is worse than an error.
static std::string g_ChipImportMessage;

// Same, for the modulation matrix's tree.
static bool g_ExpandModulation = false;

// The guided first-track lesson. Static here rather than on UIState
// because it is UI-session state, not project state: it must survive a
// project load mid-lesson and never be saved into anyone's file.
static TutorialProgress g_Tutorial;

inline void StartTutorial() {
    g_Tutorial = TutorialProgress{};
    g_Tutorial.active = true;
}

static UndoHistory g_UndoHistory;

// Undo restores the whole project, which rebuilds project.patterns - and
// DrawPianoRoll holds a Pattern& into that vector for the length of the
// frame. So the keypress only records the intent; main.cpp calls
// ApplyPendingHistory() at the top of the next frame, before any reference
// is taken. Applying it inline would dangle that reference.
static bool g_UndoRequested = false;
static bool g_RedoRequested = false;

inline void RequestUndo() { g_UndoRequested = true; }
inline void RequestRedo() { g_RedoRequested = true; }
inline bool HasPendingHistoryRequest() { return g_UndoRequested || g_RedoRequested; }

// ============================================================================
// Theme System
// ============================================================================

// Matrix rain effect state (with morphing characters like the movie)
struct MatrixColumn {
    float y = 0.0f;
    float speed = 0.0f;
    int length = 0;
    std::string chars;
    float morphTimer = 0.0f;        // Timer for character morphing
    float morphInterval = 0.02f;    // How often to morph (faster = more dynamic)
};
static std::vector<MatrixColumn> g_MatrixColumns;
static bool g_MatrixInitialized = false;
static const char* g_MatrixChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%^&*()+-=[]{}|;':\",./<>?~`";

// Synthwave chaser state
static float g_ChaserOffset = 0.0f;
static float g_ChaserColorPhase = 0.0f;  // For color cycling
static float g_CyberpunkPulse = 0.0f;    // For Cyberpunk pulsing effects
static float g_DataStreamOffset = 0.0f;  // For Cyberpunk data streams

// Theme-specific colors for piano roll
struct ThemePianoRollColors {
    ImU32 keyWhite;
    ImU32 keyBlack;
    ImU32 gridLine;
    ImU32 gridLineMeasure;
    ImU32 gridLinePattern;
    ImU32 noteDefault;
    ImU32 noteSelected;
    ImU32 playhead;
    ImU32 background;
};

static ThemePianoRollColors g_PianoRollColors;

// ============================================================================
// Derived theme colours
//
// ImGui has 63 distinct colour slots. Every theme here sets the same 24, so
// the remaining 39 - scrollbars, separators, resize grips, table headers and
// row striping, plot lines, text selection, the modal dim layer, dimmed tabs
// and the nav cursor - kept whatever ImGui's dark default left behind, in all
// ten themes.
//
// On a dark theme that reads as slightly unfinished. On a light one it is
// simply broken: dark grey scrollbars and dark table headers on a pale
// window. Writing 39 more lines into each of ten cases would be 390 lines of
// near-duplicate colour, and the eleventh theme would forget half of them.
//
// So derive them instead, once, from the colours the theme already chose.
// Any theme added later gets complete styling for free.
// ============================================================================
namespace themederive {

// Sentinel written into the derived slots before the switch runs. No real
// colour has a negative component, so anything still holding it afterwards
// is a slot the theme did not set - and a theme that *does* set one keeps
// its own value. Explicit always beats derived.
inline constexpr float UNSET_MARKER = -1.0f;

inline bool isUnset(const ImVec4& c) { return c.x < 0.0f; }

inline ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

inline ImVec4 withAlpha(const ImVec4& c, float alpha) {
    return ImVec4(c.x, c.y, c.z, alpha);
}

// Shift toward white for a positive amount, toward black for a negative one.
inline ImVec4 shift(const ImVec4& c, float amount) {
    const float target = amount >= 0.0f ? 1.0f : 0.0f;
    const float t = std::fabs(amount);
    return ImVec4(c.x + (target - c.x) * t,
                  c.y + (target - c.y) * t,
                  c.z + (target - c.z) * t,
                  c.w);
}

inline float luminance(const ImVec4& c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

} // namespace themederive

// WCAG relative luminance, and the contrast ratio between two colours.
// Used to guarantee that a label can be read on the surface under it.
inline float relativeLuminance(const ImVec4& c) {
    auto channel = [](float v) {
        return (v <= 0.03928f) ? v / 12.92f
                               : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * channel(c.x) + 0.7152f * channel(c.y) + 0.0722f * channel(c.z);
}

inline float contrastRatio(const ImVec4& a, const ImVec4& b) {
    float la = relativeLuminance(a);
    float lb = relativeLuminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05f) / (lb + 0.05f);
}

// Moves `surface` away from `text` until the pair clears `minRatio`, by
// scaling toward black or toward white. Hue survives; only value moves, and
// only as far as it has to.
inline ImVec4 ensureReadable(const ImVec4& surface, const ImVec4& text, float minRatio) {
    if (contrastRatio(surface, text) >= minRatio) return surface;

    // Light text wants a darker surface, dark text a lighter one.
    const bool darken = relativeLuminance(text) > relativeLuminance(surface) ||
                        relativeLuminance(text) > 0.4f;

    ImVec4 best = surface;
    for (int step = 1; step <= 24; ++step) {
        const float t = step / 24.0f;
        ImVec4 candidate = surface;
        if (darken) {
            candidate.x = surface.x * (1.0f - t);
            candidate.y = surface.y * (1.0f - t);
            candidate.z = surface.z * (1.0f - t);
        } else {
            candidate.x = surface.x + (1.0f - surface.x) * t;
            candidate.y = surface.y + (1.0f - surface.y) * t;
            candidate.z = surface.z + (1.0f - surface.z) * t;
        }
        best = candidate;
        if (contrastRatio(candidate, text) >= minRatio) break;
    }
    return best;
}

// Marks the 39 derived slots as untouched. Called before the theme switch so
// a theme can override any of them simply by assigning it.
inline void MarkDerivedThemeColorsUnset(ImVec4* colors) {
    static const ImGuiCol derived[] = {
        ImGuiCol_BorderShadow, ImGuiCol_CheckboxSelectedBg,
        ImGuiCol_DragDropTarget, ImGuiCol_DragDropTargetBg,
        ImGuiCol_InputTextCursor, ImGuiCol_ModalWindowDimBg,
        ImGuiCol_NavCursor, ImGuiCol_NavWindowingDimBg,
        ImGuiCol_NavWindowingHighlight, ImGuiCol_PlotHistogram,
        ImGuiCol_PlotHistogramHovered, ImGuiCol_PlotLines,
        ImGuiCol_PlotLinesHovered, ImGuiCol_ResizeGrip,
        ImGuiCol_ResizeGripActive, ImGuiCol_ResizeGripHovered,
        ImGuiCol_ScrollbarBg, ImGuiCol_ScrollbarGrab,
        ImGuiCol_ScrollbarGrabActive, ImGuiCol_ScrollbarGrabHovered,
        ImGuiCol_Separator, ImGuiCol_SeparatorActive,
        ImGuiCol_SeparatorHovered, ImGuiCol_TabDimmed,
        ImGuiCol_TabDimmedSelected, ImGuiCol_TabDimmedSelectedOverline,
        ImGuiCol_TabSelectedOverline, ImGuiCol_TableBorderLight,
        ImGuiCol_TableBorderStrong, ImGuiCol_TableHeaderBg,
        ImGuiCol_TableRowBg, ImGuiCol_TableRowBgAlt, ImGuiCol_TextLink,
        ImGuiCol_TextSelectedBg, ImGuiCol_TitleBgCollapsed,
        ImGuiCol_TreeLines, ImGuiCol_UnsavedMarker,
#ifdef IMGUI_HAS_DOCK
        ImGuiCol_DockingPreview, ImGuiCol_DockingEmptyBg,
#endif
    };

    for (ImGuiCol slot : derived) {
        colors[slot] = ImVec4(themederive::UNSET_MARKER, 0.0f, 0.0f, 0.0f);
    }
}

// Fills in every derived slot the theme left alone.
inline void DeriveRemainingThemeColors(ImGuiStyle& style) {
    using namespace themederive;
    ImVec4* colors = style.Colors;

    const ImVec4 windowBg   = colors[ImGuiCol_WindowBg];
    const ImVec4 childBg    = colors[ImGuiCol_ChildBg];
    const ImVec4 border     = colors[ImGuiCol_Border];
    const ImVec4 button     = colors[ImGuiCol_Button];
    const ImVec4 buttonHov  = colors[ImGuiCol_ButtonHovered];
    const ImVec4 buttonAct  = colors[ImGuiCol_ButtonActive];
    const ImVec4 header     = colors[ImGuiCol_Header];
    const ImVec4 headerHov  = colors[ImGuiCol_HeaderHovered];
    const ImVec4 headerAct  = colors[ImGuiCol_HeaderActive];
    const ImVec4 checkMark  = colors[ImGuiCol_CheckMark];
    const ImVec4 tab        = colors[ImGuiCol_Tab];
    const ImVec4 tabActive  = colors[ImGuiCol_TabSelected];
    const ImVec4 titleBg    = colors[ImGuiCol_TitleBg];
    const ImVec4 text       = colors[ImGuiCol_Text];

    // Which way "away from the background" points. Derived furniture has to
    // move darker on a light theme and lighter on a dark one, or it vanishes.
    const bool isLight = luminance(windowBg) > 0.5f;
    const float away = isLight ? -0.14f : 0.14f;

    auto set = [&](ImGuiCol slot, const ImVec4& value) {
        if (isUnset(colors[slot])) colors[slot] = value;
    };

    // Scrollbars follow the button family: they are the other thing you grab.
    set(ImGuiCol_ScrollbarBg, withAlpha(shift(childBg, away * 0.3f), 0.55f));
    set(ImGuiCol_ScrollbarGrab, withAlpha(button, 0.80f));
    set(ImGuiCol_ScrollbarGrabHovered, withAlpha(buttonHov, 0.95f));
    set(ImGuiCol_ScrollbarGrabActive, withAlpha(buttonAct, 1.00f));

    // Separators are borders that got a hover state.
    set(ImGuiCol_Separator, withAlpha(border, border.w * 0.70f));
    set(ImGuiCol_SeparatorHovered, withAlpha(headerHov, 0.78f));
    set(ImGuiCol_SeparatorActive, withAlpha(headerAct, 1.00f));

    // Resize grips are near-invisible until you reach for them.
    set(ImGuiCol_ResizeGrip, withAlpha(button, 0.20f));
    set(ImGuiCol_ResizeGripHovered, withAlpha(button, 0.55f));
    set(ImGuiCol_ResizeGripActive, withAlpha(button, 0.85f));

    // Tables. Row striping is a small luminance step, not a colour change -
    // a tinted stripe fights every palette it lands in.
    set(ImGuiCol_TableHeaderBg, withAlpha(header, 1.00f));
    set(ImGuiCol_TableBorderStrong, withAlpha(border, 1.00f));
    set(ImGuiCol_TableBorderLight, withAlpha(border, border.w * 0.45f));
    set(ImGuiCol_TableRowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    set(ImGuiCol_TableRowBgAlt, withAlpha(shift(windowBg, away * 0.3f), 0.55f));

    // Plots carry the spectrum analyser and the scope, so they take the
    // accent rather than a neutral - they have to be legible, not tasteful.
    set(ImGuiCol_PlotLines, checkMark);
    set(ImGuiCol_PlotLinesHovered, shift(checkMark, 0.20f));
    set(ImGuiCol_PlotHistogram, checkMark);
    set(ImGuiCol_PlotHistogramHovered, shift(checkMark, 0.20f));

    set(ImGuiCol_TextSelectedBg, withAlpha(header, 0.45f));

    // A dimmed tab is the same tab, pulled toward the window behind it.
    set(ImGuiCol_TabDimmed, mix(tab, windowBg, 0.35f));
    set(ImGuiCol_TabDimmedSelected, mix(tabActive, windowBg, 0.25f));
    set(ImGuiCol_TabSelectedOverline, checkMark);
    set(ImGuiCol_TabDimmedSelectedOverline, withAlpha(checkMark, 0.45f));

    set(ImGuiCol_TitleBgCollapsed, withAlpha(titleBg, 0.75f));

    // Everything that means "this one, right here" uses the accent the theme
    // already defined, so focus reads the same way everywhere.
    set(ImGuiCol_NavCursor, checkMark);
    set(ImGuiCol_DragDropTarget, checkMark);
    set(ImGuiCol_DragDropTargetBg, withAlpha(checkMark, 0.20f));
    set(ImGuiCol_TextLink, checkMark);
    set(ImGuiCol_UnsavedMarker, checkMark);
    set(ImGuiCol_InputTextCursor, checkMark);
    set(ImGuiCol_CheckboxSelectedBg, withAlpha(checkMark, 0.18f));
    set(ImGuiCol_NavWindowingHighlight, withAlpha(text, 0.70f));

    // A light theme needs a gentler dim, or the modal layer turns the page
    // into mud.
    const float dim = isLight ? 0.25f : 0.45f;
    set(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, dim));
    set(ImGuiCol_NavWindowingDimBg, ImVec4(0.0f, 0.0f, 0.0f, dim * 0.55f));

    // Shadows fight the flat look the shared metrics block establishes.
    set(ImGuiCol_BorderShadow, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    set(ImGuiCol_TreeLines, withAlpha(border, border.w * 0.60f));

#ifdef IMGUI_HAS_DOCK
    set(ImGuiCol_DockingPreview, withAlpha(headerAct, 0.70f));
    set(ImGuiCol_DockingEmptyBg, windowBg);
#endif

    // Anything still holding the sentinel would render as a negative colour.
    // Nothing should reach here; fall back to the window background if it does.
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        if (isUnset(colors[i])) colors[i] = windowBg;
    }

    // ------------------------------------------------------------------
    // Label legibility, enforced rather than hoped for.
    //
    // ImGui draws every label in ImGuiCol_Text - one colour for the entire
    // UI - so a surface cannot choose a label colour that suits it. The
    // surface has to come to the text instead.
    //
    // Nine of the ten themes had button labels below 4.5:1 and seven were
    // under 3:1, the worst at 1.22:1. The cause was the same everywhere:
    // hover and active states brighten the fill, which under light text
    // makes the label harder to read the more you interact with it.
    //
    // Each surface moves away from the text only as far as it must, and
    // only in value, so every theme keeps its hue and its identity.
    // ------------------------------------------------------------------
    static const ImGuiCol labelSurfaces[] = {
        ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive,
        ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive,
        ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
        ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabSelected,
        ImGuiCol_TitleBgActive, ImGuiCol_MenuBarBg, ImGuiCol_PopupBg,
    };

    for (ImGuiCol slot : labelSurfaces) {
        const float alpha = colors[slot].w;
        ImVec4 fixed = ensureReadable(colors[slot], text, 4.5f);
        fixed.w = alpha;                 // never trade away opacity for contrast
        colors[slot] = fixed;
    }

    // The derived slots that mirror those surfaces have to follow, or a
    // table header stops matching the header it was derived from.
    colors[ImGuiCol_TableHeaderBg] = withAlpha(colors[ImGuiCol_Header], 1.0f);
    colors[ImGuiCol_TextSelectedBg] = withAlpha(colors[ImGuiCol_Header], 0.45f);
}

// ============================================================================
// Per-theme geometry
//
// Colour alone leaves every theme looking like the same widgets repainted,
// because shape is what the eye reads first. A Game Boy has square pixel
// blocks; Frutiger Aero is all glossy bubbles; a terminal has hard corners
// and heavy rules. Those are different *shapes*, not different palettes.
//
// This layers a per-theme delta on top of the shared metrics block rather
// than replacing it - spacing and padding stay uniform, because those are
// about usability and should not change when you switch theme. Only the
// character does: corner radius, border weight, grab size.
// ============================================================================
inline void ApplyThemeGeometry(ImGuiStyle& style, Theme theme) {
    // rounding, borderSize, grabMinSize
    struct Geometry { float rounding; float border; float grab; };

    Geometry g{5.0f, 1.0f, 11.0f};   // the shared default

    switch (theme) {
        // Hard edges. Cyberpunk and the two terminals are angular by
        // definition - a rounded corner reads as friendly, which is wrong
        // for all three.
        case Theme::Cyberpunk:     g = {0.0f, 1.6f, 12.0f}; break;
        case Theme::Matrix:        g = {0.0f, 1.0f, 10.0f}; break;
        case Theme::RetroTerminal: g = {0.0f, 1.4f, 10.0f}; break;

        // A DMG has no curves and no thin lines: it has pixels. Square
        // corners and a heavy border read as an LCD cell.
        case Theme::GameBoy:       g = {0.0f, 2.0f, 14.0f}; break;

        // Neon signs are tubes - slightly rounded, never soft.
        case Theme::Synthwave:     g = {2.0f, 1.2f, 12.0f}; break;

        // Frutiger Aero is bubbles and lozenges. This is the one theme
        // where a large radius is the entire point.
        case Theme::FrutigerAero:  g = {12.0f, 1.0f, 13.0f}; break;

        // Vaporwave is hazy and soft-edged, but less glossy than Aero.
        case Theme::Vaporwave:     g = {9.0f, 1.0f, 12.0f}; break;

        // Minimal: almost square, hairline borders. Restraint in shape as
        // well as in colour.
        case Theme::Minimal:       g = {2.0f, 1.0f, 10.0f}; break;

        case Theme::Daylight:      g = {6.0f, 1.0f, 11.0f}; break;
        case Theme::Stock:
        default:                   break;
    }

    style.WindowRounding    = g.rounding;
    style.ChildRounding     = g.rounding * 0.75f;
    style.PopupRounding     = g.rounding * 0.75f;
    style.FrameRounding     = g.rounding * 0.62f;
    style.GrabRounding      = g.rounding * 0.62f;
    style.TabRounding       = g.rounding * 0.75f;
    style.ScrollbarRounding = g.rounding;

    style.WindowBorderSize  = g.border;
    style.ChildBorderSize   = g.border;
    style.PopupBorderSize   = g.border;
    style.GrabMinSize       = g.grab;

    // A visible frame border suits the hard-edged themes and muddies the
    // soft ones, where the fill already defines the shape.
    style.FrameBorderSize = (g.rounding <= 0.5f) ? 1.0f : 0.0f;
}

// Apply a theme to ImGui style
inline void ApplyTheme(Theme theme) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Mark the derived slots before the switch, so a theme that wants to set
    // one of them explicitly simply wins - see DeriveRemainingThemeColors.
    MarkDerivedThemeColorsUnset(colors);

    // ------------------------------------------------------------------
    // Shared geometry, applied under every theme.
    //
    // Stock ImGui reads as "a debug overlay" mostly because of its metrics,
    // not its colours: tight padding, square corners, hairline borders on
    // everything. Generous padding and consistent rounding do more for the
    // feel of the app than any palette, and doing it here means every
    // theme - including ones added later - inherits it.
    // ------------------------------------------------------------------
    style.WindowRounding      = 8.0f;
    style.ChildRounding       = 6.0f;
    style.PopupRounding       = 6.0f;
    style.FrameRounding       = 5.0f;
    style.ScrollbarRounding   = 8.0f;
    style.GrabRounding        = 5.0f;
    style.TabRounding         = 6.0f;

    style.WindowPadding       = ImVec2(12.0f, 10.0f);
    style.FramePadding        = ImVec2(9.0f, 5.0f);
    style.CellPadding         = ImVec2(7.0f, 4.0f);
    style.ItemSpacing         = ImVec2(9.0f, 7.0f);
    style.ItemInnerSpacing    = ImVec2(7.0f, 5.0f);
    style.IndentSpacing       = 21.0f;

    style.ScrollbarSize       = 13.0f;
    style.GrabMinSize         = 11.0f;

    // Borders only where they separate things, not around every widget
    style.WindowBorderSize    = 1.0f;
    style.ChildBorderSize     = 1.0f;
    style.PopupBorderSize     = 1.0f;
    style.FrameBorderSize     = 0.0f;
    style.TabBorderSize       = 0.0f;

    // Titles and headings centred left with breathing room
    style.WindowTitleAlign    = ImVec2(0.02f, 0.5f);
    style.ButtonTextAlign     = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    style.AntiAliasedLines    = true;
    style.AntiAliasedFill     = true;

    // Gutters between docked panels.
    //
    // Docking tiles the viewport exactly, so with flush panels the animated
    // theme backgrounds - the Matrix rain, the Synthwave sun and grid - had
    // nowhere to show and were invisible. A visible separator lets them run
    // between the panels, which is where they belong: behind and around the
    // work, never underneath the text.
    style.DockingSeparatorSize = 7.0f;

    switch (theme) {
        case Theme::Stock:
            // The neutral default. Restraint is the point - it has to still
            // look sober sitting next to Cyberpunk.
            //
            // Header and Button used to be the same colour, so nothing told
            // you what was pressable. Headers are now a quiet raised neutral
            // that groups content; the saturated accent belongs to buttons
            // alone. Greys are warmed very slightly, because an app people
            // sit in front of for hours should not feel clinical.
            colors[ImGuiCol_ChildBg] = ImVec4(0.092f, 0.088f, 0.085f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.132f, 0.127f, 0.122f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.180f, 0.174f, 0.167f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.222f, 0.215f, 0.206f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.232f, 0.226f, 0.218f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.280f, 0.272f, 0.262f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.335f, 0.325f, 0.315f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.104f, 0.100f, 0.096f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.165f, 0.172f, 0.196f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.150f, 0.145f, 0.139f, 1.00f);

            // Headers: a raised neutral-blue surface, not the accent.
            colors[ImGuiCol_Header] = ImVec4(0.235f, 0.245f, 0.278f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.292f, 0.306f, 0.348f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.342f, 0.360f, 0.408f, 1.00f);

            // Buttons: the one saturated thing on screen.
            colors[ImGuiCol_Button] = ImVec4(0.235f, 0.450f, 0.720f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.295f, 0.530f, 0.815f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.180f, 0.375f, 0.640f, 1.00f);

            colors[ImGuiCol_SliderGrab] = ImVec4(0.300f, 0.520f, 0.800f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.400f, 0.620f, 0.900f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.420f, 0.690f, 1.000f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.905f, 0.900f, 0.912f, 1.00f);
            // Nudged up from 0.50: disabled should read as quiet, not absent.
            colors[ImGuiCol_TextDisabled] = ImVec4(0.560f, 0.552f, 0.568f, 1.00f);
            // The selected tab leans on TabSelectedOverline (derived from the
            // accent) rather than on fill brightness alone.
            colors[ImGuiCol_Tab] = ImVec4(0.150f, 0.155f, 0.175f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.270f, 0.330f, 0.430f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.225f, 0.290f, 0.395f, 1.00f);

            // Piano roll - blue note identity kept
            g_PianoRollColors.keyWhite = IM_COL32(62, 60, 66, 255);
            g_PianoRollColors.keyBlack = IM_COL32(38, 37, 41, 255);
            g_PianoRollColors.gridLine = IM_COL32(52, 51, 56, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(104, 102, 112, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(158, 86, 86, 255);
            g_PianoRollColors.noteDefault = IM_COL32(66, 126, 196, 255);
            g_PianoRollColors.noteSelected = IM_COL32(126, 212, 255, 255);
            g_PianoRollColors.playhead = IM_COL32(255, 96, 96, 255);
            g_PianoRollColors.background = IM_COL32(30, 29, 33, 255);
            break;

        case Theme::Cyberpunk:
            // Near-black ground, cyan / hot-pink / neon-yellow triad.
            //
            // The rule, so it survives the next edit: cyan is STRUCTURAL
            // (borders, checkmarks, active states), hot pink is the
            // INTERACTIVE accent (buttons), neon yellow is RARE emphasis
            // (playhead, slider active). Previously all three ran at full
            // saturation with no dominant, which is why it read as noise.
            //
            // Body text was pure saturated cyan. That shimmers on subpixel
            // layouts and, worse, left no way to emphasise anything - if all
            // text is neon, nothing stands out. Near-white with a cyan cast
            // instead, and pure cyan reserved for accents.
            colors[ImGuiCol_ChildBg] = ImVec4(0.030f, 0.032f, 0.048f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.058f, 0.060f, 0.086f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.098f, 0.102f, 0.140f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.130f, 0.134f, 0.180f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.150f, 0.110f, 0.190f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.210f, 0.130f, 0.260f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.100f, 0.850f, 0.900f, 0.55f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.075f, 0.030f, 0.095f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.420f, 0.060f, 0.260f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.085f, 0.048f, 0.115f, 1.00f);

            // Headers: a cyan-tinted surface, so they are not mistaken for
            // the pink buttons. Opaque, because the glitch animates behind.
            colors[ImGuiCol_Header] = ImVec4(0.100f, 0.260f, 0.320f, 0.92f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.140f, 0.350f, 0.420f, 0.96f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.180f, 0.450f, 0.540f, 1.00f);

            colors[ImGuiCol_Button] = ImVec4(0.920f, 0.160f, 0.450f, 0.92f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(1.000f, 0.280f, 0.550f, 0.96f);
            colors[ImGuiCol_ButtonActive] = ImVec4(1.000f, 0.420f, 0.650f, 1.00f);

            colors[ImGuiCol_SliderGrab] = ImVec4(0.100f, 0.850f, 0.920f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.990f, 0.930f, 0.150f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.100f, 0.900f, 0.950f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.880f, 0.970f, 0.975f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.480f, 0.520f, 0.600f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.140f, 0.120f, 0.220f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.620f, 0.140f, 0.340f, 0.95f);
            colors[ImGuiCol_TabActive] = ImVec4(0.400f, 0.100f, 0.260f, 1.00f);

            // Piano roll - emissive notes on a very dark grid
            g_PianoRollColors.keyWhite = IM_COL32(34, 34, 54, 255);
            g_PianoRollColors.keyBlack = IM_COL32(16, 16, 30, 255);
            g_PianoRollColors.gridLine = IM_COL32(44, 26, 62, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(255, 40, 110, 210);
            g_PianoRollColors.gridLinePattern = IM_COL32(252, 238, 10, 255);
            g_PianoRollColors.noteDefault = IM_COL32(5, 217, 232, 255);
            g_PianoRollColors.noteSelected = IM_COL32(255, 90, 160, 255);
            g_PianoRollColors.playhead = IM_COL32(252, 238, 10, 255);
            g_PianoRollColors.background = IM_COL32(10, 10, 20, 255);
            break;

        case Theme::Synthwave:
            // Outrun: deep indigo sky, magenta and cyan horizon, hard edges.
            //
            // This theme and Vaporwave were two shades of the same purple.
            // They are split deliberately now: Synthwave takes the
            // saturated, high-contrast, neon-on-near-black lane; Vaporwave
            // takes pastel and hazy. Deepening the ground is what lets the
            // neon actually glow against it.
            //
            // Axis: magenta is interactive (buttons, active states), cyan is
            // informational (checkmarks, selection, plots). Purple is the
            // SURFACE colour, not an accent - that was the missing bit.
            colors[ImGuiCol_ChildBg] = ImVec4(0.038f, 0.020f, 0.070f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.030f, 0.095f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.105f, 0.055f, 0.165f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.125f, 0.070f, 0.190f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.175f, 0.090f, 0.250f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.245f, 0.125f, 0.330f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.950f, 0.200f, 0.850f, 0.55f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.020f, 0.085f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.380f, 0.050f, 0.500f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.075f, 0.035f, 0.115f, 1.00f);

            // Purple surfaces, opaque so the chasers animate between panels
            // rather than through the button labels.
            colors[ImGuiCol_Header] = ImVec4(0.320f, 0.100f, 0.460f, 0.92f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.420f, 0.140f, 0.580f, 0.96f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.520f, 0.180f, 0.700f, 1.00f);

            // No channel pinned at 0.00 or 1.00 on the base state, so hover
            // and press have somewhere to go.
            colors[ImGuiCol_Button] = ImVec4(0.880f, 0.120f, 0.550f, 0.92f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.960f, 0.280f, 0.680f, 0.96f);
            colors[ImGuiCol_ButtonActive] = ImVec4(1.000f, 0.420f, 0.780f, 1.00f);

            colors[ImGuiCol_SliderGrab] = ImVec4(0.100f, 0.900f, 0.950f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.350f, 1.000f, 1.000f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.100f, 0.920f, 0.960f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(1.000f, 0.900f, 1.000f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.620f, 0.450f, 0.660f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.200f, 0.050f, 0.300f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.620f, 0.140f, 0.800f, 0.95f);
            colors[ImGuiCol_TabActive] = ImVec4(0.420f, 0.100f, 0.560f, 1.00f);

            // Piano roll - the measure line is the horizon
            g_PianoRollColors.keyWhite = IM_COL32(44, 22, 68, 255);
            g_PianoRollColors.keyBlack = IM_COL32(26, 11, 42, 255);
            g_PianoRollColors.gridLine = IM_COL32(62, 20, 96, 170);
            g_PianoRollColors.gridLineMeasure = IM_COL32(255, 45, 235, 220);
            g_PianoRollColors.gridLinePattern = IM_COL32(35, 245, 245, 255);
            g_PianoRollColors.noteDefault = IM_COL32(255, 40, 150, 255);
            g_PianoRollColors.noteSelected = IM_COL32(60, 250, 250, 255);
            g_PianoRollColors.playhead = IM_COL32(255, 165, 40, 255);
            g_PianoRollColors.background = IM_COL32(14, 7, 26, 255);
            break;

        case Theme::Matrix:
            // Terminal phosphor, but built so it can be looked at for hours.
            //
            // Every colour here used to have R and B pinned at zero, so the
            // whole theme lived on one axis of the colour cube at maximum
            // chroma and depth was carried by the green channel alone. Fully
            // saturated green sits near the eye's peak sensitivity, which
            // makes it the most tiring hue to max out, and it leaves no
            // low-chroma surface anywhere to rest on.
            //
            // So: VALUE carries the hierarchy, CHROMA is spent only where
            // something needs noticing. Surfaces are dark green-cast
            // neutrals with real luminance steps; the accent stays vivid.
            colors[ImGuiCol_ChildBg] = ImVec4(0.020f, 0.042f, 0.026f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.032f, 0.062f, 0.038f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.055f, 0.098f, 0.062f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.072f, 0.125f, 0.080f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.078f, 0.138f, 0.088f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.105f, 0.178f, 0.115f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.180f, 0.420f, 0.235f, 0.55f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.028f, 0.055f, 0.034f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.075f, 0.145f, 0.088f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.040f, 0.075f, 0.046f, 1.00f);

            // Headers are surfaces: enough chroma to separate, not enough to
            // compete with the buttons.
            colors[ImGuiCol_Header] = ImVec4(0.085f, 0.230f, 0.120f, 0.92f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.115f, 0.310f, 0.160f, 0.96f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.150f, 0.400f, 0.205f, 1.00f);

            colors[ImGuiCol_Button] = ImVec4(0.130f, 0.480f, 0.235f, 0.92f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.175f, 0.590f, 0.300f, 0.96f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.225f, 0.700f, 0.370f, 1.00f);

            // The one place full phosphor still belongs.
            colors[ImGuiCol_SliderGrab] = ImVec4(0.250f, 0.780f, 0.400f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.340f, 0.900f, 0.500f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.300f, 0.880f, 0.470f, 1.00f);

            // Text keeps the hue but drops to a moderate chroma at high
            // value. It reads as phosphor without glowing at you.
            colors[ImGuiCol_Text] = ImVec4(0.640f, 0.940f, 0.700f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.360f, 0.560f, 0.410f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.045f, 0.100f, 0.058f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.130f, 0.330f, 0.180f, 0.95f);
            colors[ImGuiCol_TabActive] = ImVec4(0.090f, 0.230f, 0.125f, 1.00f);

            // Piano roll - the grid recedes, the notes carry the chroma
            g_PianoRollColors.keyWhite = IM_COL32(34, 58, 40, 255);
            g_PianoRollColors.keyBlack = IM_COL32(18, 32, 22, 255);
            g_PianoRollColors.gridLine = IM_COL32(28, 52, 34, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(58, 104, 68, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(96, 176, 112, 255);
            g_PianoRollColors.noteDefault = IM_COL32(70, 200, 110, 255);
            g_PianoRollColors.noteSelected = IM_COL32(180, 250, 200, 255);
            g_PianoRollColors.playhead = IM_COL32(120, 255, 150, 255);
            g_PianoRollColors.background = IM_COL32(10, 20, 13, 255);
            break;

        case Theme::FrutigerAero:
            // Glossy Web 2.0: sky blue, aqua secondary, light from above.
            //
            // WindowBg is opaque now. It was 0.95, which put dark navy body
            // text over a moving background - translucency belongs on chrome
            // that does not hold text, so it moved to title bars, popups and
            // inactive tabs.
            //
            // Elevation is inverted for a light theme: inset surfaces are
            // DARKER than the window, raised ones lighter. Brighter on top is
            // what implies a light source, and that inversion is most of what
            // sells a light theme.
            colors[ImGuiCol_WindowBg] = ImVec4(0.880f, 0.930f, 0.972f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.845f, 0.905f, 0.958f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.800f, 0.872f, 0.940f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.752f, 0.878f, 0.888f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.705f, 0.815f, 0.930f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.970f, 0.985f, 1.000f, 0.97f);
            // Glass, but only where it costs no legibility
            colors[ImGuiCol_TitleBg] = ImVec4(0.560f, 0.822f, 0.808f, 0.92f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.372f, 0.742f, 0.712f, 0.96f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.760f, 0.902f, 0.905f, 1.00f);
            // On a pale window a widget without a defined edge dissolves.
            colors[ImGuiCol_Border] = ImVec4(0.420f, 0.600f, 0.800f, 0.85f);

            // Buttons stay light: labels are drawn in ImGuiCol_Text, which is
            // dark navy here, so a dark button would bury its own label.
            // The green half of white/green/blue. Without it the theme reads
            // as "a light blue theme" no matter what the background does.
            colors[ImGuiCol_Header] = ImVec4(0.618f, 0.855f, 0.792f, 0.92f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.522f, 0.812f, 0.736f, 0.95f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.412f, 0.752f, 0.668f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.635f, 0.822f, 0.960f, 0.95f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.530f, 0.770f, 0.960f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.425f, 0.690f, 0.935f, 1.00f);

            // Aqua secondary against the sky blue
            colors[ImGuiCol_SliderGrab] = ImVec4(0.110f, 0.620f, 0.600f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.060f, 0.520f, 0.500f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.055f, 0.512f, 0.462f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.100f, 0.180f, 0.300f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.430f, 0.510f, 0.600f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.720f, 0.860f, 0.960f, 0.92f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.580f, 0.800f, 0.960f, 1.00f);
            // The selected tab is the LIGHTEST, matching light-from-above
            colors[ImGuiCol_TabActive] = ImVec4(0.945f, 0.975f, 1.000f, 1.00f);

            // Piano roll - the only light-ground roll, so every value here is
            // chosen against white rather than inherited from a dark theme.
            g_PianoRollColors.keyWhite = IM_COL32(250, 253, 255, 255);
            g_PianoRollColors.keyBlack = IM_COL32(148, 176, 204, 255);
            g_PianoRollColors.gridLine = IM_COL32(206, 220, 233, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(138, 178, 176, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(52, 124, 196, 255);
            g_PianoRollColors.noteDefault = IM_COL32(38, 116, 202, 255);
            g_PianoRollColors.noteSelected = IM_COL32(232, 138, 16, 255);
            g_PianoRollColors.playhead = IM_COL32(206, 36, 62, 255);
            g_PianoRollColors.background = IM_COL32(236, 243, 250, 255);
            break;

        case Theme::Minimal:
            // Restraint in the NUMBER of colours, not weakness in them.
            //
            // The accent was red. In a DAW red means record, and also
            // destructive and error - so record, delete, error and an
            // ordinary OK button were all one colour. The accent is teal
            // now: unambiguous here, and it leaves red free to mean red.
            // After this nothing in the theme is red except the playhead.
            //
            // Contrast goes UP, not down. Minimal earns calm through the
            // spacing the shared metrics block already provides; washed-out
            // greys just read as murky.
            colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.075f, 0.085f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.105f, 0.116f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.165f, 0.165f, 0.180f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.205f, 0.205f, 0.222f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.225f, 0.225f, 0.244f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.285f, 0.285f, 0.308f, 1.00f);
            // Borders do the delimiting, since FrameBorderSize is 0.
            colors[ImGuiCol_Border] = ImVec4(0.300f, 0.300f, 0.330f, 0.75f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.075f, 0.075f, 0.085f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.135f, 0.135f, 0.148f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.090f, 0.090f, 0.100f, 1.00f);

            // Header sits ~11% above the window now instead of 8%, so a
            // collapsing section actually registers as a surface.
            colors[ImGuiCol_Header] = ImVec4(0.215f, 0.215f, 0.235f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.285f, 0.285f, 0.310f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.145f, 0.560f, 0.530f, 1.00f);

            colors[ImGuiCol_Button] = ImVec4(0.135f, 0.620f, 0.580f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.180f, 0.720f, 0.680f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.100f, 0.500f, 0.470f, 1.00f);

            colors[ImGuiCol_SliderGrab] = ImVec4(0.160f, 0.680f, 0.640f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.220f, 0.800f, 0.750f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.200f, 0.780f, 0.720f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.950f, 0.950f, 0.960f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.550f, 0.550f, 0.578f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.135f, 0.135f, 0.148f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.240f, 0.240f, 0.262f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.190f, 0.190f, 0.210f, 1.00f);

            // Piano roll - near-monochrome, accent for notes, and red is now
            // free for the one thing that should be red.
            g_PianoRollColors.keyWhite = IM_COL32(42, 42, 48, 255);
            g_PianoRollColors.keyBlack = IM_COL32(24, 24, 28, 255);
            g_PianoRollColors.gridLine = IM_COL32(54, 54, 60, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(82, 82, 90, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(122, 122, 132, 255);
            g_PianoRollColors.noteDefault = IM_COL32(40, 180, 168, 255);
            g_PianoRollColors.noteSelected = IM_COL32(236, 240, 244, 255);
            g_PianoRollColors.playhead = IM_COL32(236, 70, 70, 255);
            g_PianoRollColors.background = IM_COL32(26, 26, 30, 255);
            break;

        case Theme::Vaporwave:
            // Sun-bleached poster: dusty rose, pale lilac, powder cyan.
            //
            // The opposite lane from Synthwave, deliberately. Synthwave is
            // saturated neon on near-black; this is pastel, hazy, mid-toned.
            // Lifting the ground from near-black to a dusty purple is the
            // single change that separates them at a glance.
            //
            // Pastel does not mean pale here - the accents are muted
            // mid-tones, because near-white text still has to read on them.
            colors[ImGuiCol_ChildBg] = ImVec4(0.165f, 0.120f, 0.225f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.200f, 0.150f, 0.270f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.255f, 0.195f, 0.335f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.285f, 0.220f, 0.375f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.310f, 0.240f, 0.400f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.365f, 0.285f, 0.465f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.780f, 0.580f, 0.740f, 0.50f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.240f, 0.165f, 0.305f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.440f, 0.285f, 0.520f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.220f, 0.158f, 0.288f, 1.00f);

            // Hue does as much level-signalling as luminance here, because a
            // soft palette cannot afford large brightness steps.
            colors[ImGuiCol_Header] = ImVec4(0.480f, 0.330f, 0.560f, 0.92f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.560f, 0.400f, 0.640f, 0.96f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.640f, 0.470f, 0.720f, 1.00f);

            colors[ImGuiCol_Button] = ImVec4(0.620f, 0.340f, 0.500f, 0.92f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.720f, 0.430f, 0.590f, 0.96f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.800f, 0.520f, 0.670f, 1.00f);

            colors[ImGuiCol_SliderGrab] = ImVec4(0.520f, 0.800f, 0.820f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.640f, 0.880f, 0.900f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.560f, 0.840f, 0.860f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.960f, 0.900f, 0.940f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.680f, 0.600f, 0.700f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.300f, 0.210f, 0.380f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.500f, 0.350f, 0.580f, 0.95f);
            colors[ImGuiCol_TabActive] = ImVec4(0.400f, 0.280f, 0.480f, 1.00f);

            // Piano roll - selection separates from default by HUE, since
            // both are soft; the playhead is the one saturated thing.
            g_PianoRollColors.keyWhite = IM_COL32(62, 48, 78, 255);
            g_PianoRollColors.keyBlack = IM_COL32(40, 30, 54, 255);
            g_PianoRollColors.gridLine = IM_COL32(84, 66, 104, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(140, 110, 166, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(190, 150, 210, 255);
            g_PianoRollColors.noteDefault = IM_COL32(226, 150, 180, 255);
            g_PianoRollColors.noteSelected = IM_COL32(150, 220, 222, 255);
            g_PianoRollColors.playhead = IM_COL32(255, 186, 88, 255);
            g_PianoRollColors.background = IM_COL32(40, 30, 54, 255);
            break;

        case Theme::RetroTerminal:
            // Retro Terminal - Amber CRT phosphor, vintage computer aesthetic
            colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.02f, 0.00f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.01f, 0.01f, 0.00f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.03f, 0.00f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.80f, 0.55f, 0.00f, 0.40f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.06f, 0.00f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.10f, 0.00f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.15f, 0.00f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.04f, 0.00f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.14f, 0.00f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.06f, 0.04f, 0.00f, 1.00f);
            // Opaque enough to read a label against the scanlines
            colors[ImGuiCol_Header] = ImVec4(0.34f, 0.23f, 0.02f, 0.92f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.46f, 0.31f, 0.02f, 0.96f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.70f, 0.48f, 0.00f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.50f, 0.34f, 0.00f, 0.92f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.64f, 0.44f, 0.02f, 0.96f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.65f, 0.45f, 0.00f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.62f, 0.00f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.72f, 0.10f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.70f, 0.00f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(1.00f, 0.75f, 0.20f, 1.00f);  // Amber phosphor
            colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.35f, 0.05f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.10f, 0.00f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.40f, 0.28f, 0.00f, 0.80f);
            colors[ImGuiCol_TabActive] = ImVec4(0.30f, 0.20f, 0.00f, 1.00f);

            // Piano roll colors - Retro Terminal (amber glow)
            g_PianoRollColors.keyWhite = IM_COL32(25, 18, 0, 255);
            g_PianoRollColors.keyBlack = IM_COL32(12, 8, 0, 255);
            g_PianoRollColors.gridLine = IM_COL32(60, 42, 0, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(120, 84, 0, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(200, 140, 0, 255);
            g_PianoRollColors.noteDefault = IM_COL32(255, 180, 50, 255);
            g_PianoRollColors.noteSelected = IM_COL32(255, 230, 150, 255);
            g_PianoRollColors.playhead = IM_COL32(255, 200, 80, 255);
            g_PianoRollColors.background = IM_COL32(8, 5, 0, 255);
            break;

        case Theme::GameBoy:
            // DMG-01, 1989. Not the Pocket, not the Color, not the Advance.
            //
            // Built on values sampled from photographs of a running DMG -
            // roughly 1b2a09 / 0e450b / 496b22 / 9a9e3f - rather than the
            // 0f380f / 306230 / 8bac0f / 9bbc0f palette everyone copies.
            // That one is community convention, not measurement: the screen
            // is a reflective STN panel with four transmission levels behind
            // a green polariser, so there is no RGB in the hardware to
            // sample. The real thing is markedly more olive and far less
            // saturated than the meme palette suggests.
            //
            // The four shades are used literally in the piano roll, where
            // there is no text. The chrome needs more steps than four to
            // separate a surface from a label, so it uses the same hue
            // family with proper value spacing - and the derive pass darkens
            // anything a label cannot be read on.
            colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.085f, 0.020f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.082f, 0.122f, 0.032f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.120f, 0.170f, 0.050f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.150f, 0.205f, 0.065f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.145f, 0.200f, 0.060f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.185f, 0.250f, 0.080f, 1.00f);
            // 496b22 - the third shade, used as the panel edge
            colors[ImGuiCol_Border] = ImVec4(0.286f, 0.420f, 0.133f, 0.85f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.070f, 0.105f, 0.028f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.150f, 0.210f, 0.068f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.095f, 0.140f, 0.038f, 1.00f);

            colors[ImGuiCol_Header] = ImVec4(0.135f, 0.190f, 0.058f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.175f, 0.240f, 0.075f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.220f, 0.300f, 0.095f, 1.00f);

            colors[ImGuiCol_Button] = ImVec4(0.200f, 0.285f, 0.090f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.245f, 0.340f, 0.110f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.290f, 0.395f, 0.130f, 1.00f);

            colors[ImGuiCol_SliderGrab] = ImVec4(0.430f, 0.500f, 0.190f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.545f, 0.585f, 0.230f, 1.00f);
            // 9a9e3f - a lit pixel
            colors[ImGuiCol_CheckMark] = ImVec4(0.604f, 0.620f, 0.247f, 1.00f);

            // Slightly brighter than the lightest shade, because a
            // photograph of a reflective LCD is always darker than the panel
            // looks in the hand - and because a label has to be readable.
            colors[ImGuiCol_Text] = ImVec4(0.690f, 0.706f, 0.310f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.400f, 0.430f, 0.200f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.095f, 0.140f, 0.040f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.185f, 0.255f, 0.080f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.140f, 0.195f, 0.062f, 1.00f);

            // Piano roll - the four sampled shades, used as shades
            g_PianoRollColors.background = IM_COL32(27, 42, 9, 255);       // 1b2a09
            g_PianoRollColors.keyBlack = IM_COL32(22, 34, 8, 255);
            g_PianoRollColors.keyWhite = IM_COL32(44, 64, 20, 255);
            g_PianoRollColors.gridLine = IM_COL32(34, 50, 14, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(73, 107, 34, 255); // 496b22
            g_PianoRollColors.gridLinePattern = IM_COL32(110, 135, 50, 255);
            g_PianoRollColors.noteDefault = IM_COL32(154, 158, 63, 255);    // 9a9e3f
            g_PianoRollColors.noteSelected = IM_COL32(240, 242, 205, 255);
            g_PianoRollColors.playhead = IM_COL32(200, 205, 120, 255);
            break;

        case Theme::Daylight:
            // A genuine light theme. Every other theme here is dark, which
            // is unusable next to a window on a bright afternoon. Warm
            // greys rather than pure white, and a blue accent that stays
            // legible against them.
            colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.95f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.97f, 0.97f, 0.98f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.99f, 0.99f, 1.00f, 1.00f);
            colors[ImGuiCol_Border] = ImVec4(0.76f, 0.76f, 0.80f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.88f, 0.88f, 0.91f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.83f, 0.85f, 0.92f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.78f, 0.82f, 0.93f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.80f, 0.84f, 0.92f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.91f, 0.91f, 0.93f, 1.00f);
            // Header was the same blue as Button. It becomes a light
            // neutral surface so the accent belongs to buttons alone.
            colors[ImGuiCol_Header] = ImVec4(0.82f, 0.85f, 0.91f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.75f, 0.80f, 0.89f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.67f, 0.75f, 0.87f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.36f, 0.55f, 0.86f, 0.90f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.50f, 0.84f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.43f, 0.78f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.48f, 0.82f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.20f, 0.40f, 0.76f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.42f, 0.78f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.53f, 0.57f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.84f, 0.85f, 0.88f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.72f, 0.79f, 0.92f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.80f, 0.85f, 0.94f, 1.00f);

            g_PianoRollColors.keyWhite = IM_COL32(250, 250, 252, 255);
            g_PianoRollColors.keyBlack = IM_COL32(196, 198, 206, 255);
            g_PianoRollColors.gridLine = IM_COL32(214, 216, 222, 255);
            g_PianoRollColors.gridLineMeasure = IM_COL32(168, 172, 182, 255);
            g_PianoRollColors.gridLinePattern = IM_COL32(214, 132, 132, 255);
            g_PianoRollColors.noteDefault = IM_COL32(72, 122, 208, 255);
            g_PianoRollColors.noteSelected = IM_COL32(40, 92, 190, 255);
            g_PianoRollColors.playhead = IM_COL32(214, 78, 78, 255);
            g_PianoRollColors.background = IM_COL32(238, 239, 243, 255);
            break;
    }

    // Derived last, because it reads the palette the theme just chose.
    DeriveRemainingThemeColors(style);

    // Shape after colour. Layers a per-theme delta over the shared metrics
    // block above; spacing and padding are deliberately left alone.
    ApplyThemeGeometry(style, theme);
}

// Initialize Matrix rain effect
inline void InitMatrixRain(int screenWidth) {
    if (g_MatrixInitialized) return;

    int numChars = static_cast<int>(strlen(g_MatrixChars));
    int columnWidth = 14;
    int numColumns = screenWidth / columnWidth + 1;

    g_MatrixColumns.resize(numColumns);
    for (int i = 0; i < numColumns; ++i) {
        g_MatrixColumns[i].y = static_cast<float>(rand() % 800);
        g_MatrixColumns[i].speed = 50.0f + static_cast<float>(rand() % 150);
        g_MatrixColumns[i].length = 5 + rand() % 20;
        g_MatrixColumns[i].morphTimer = 0.0f;
        g_MatrixColumns[i].morphInterval = 0.015f + static_cast<float>(rand() % 25) / 1000.0f; // 15-40ms (faster!)
        g_MatrixColumns[i].chars.clear();
        for (int j = 0; j < g_MatrixColumns[i].length; ++j) {
            g_MatrixColumns[i].chars += g_MatrixChars[rand() % numChars];
        }
    }
    g_MatrixInitialized = true;
}

// Draw Matrix rain background effect (with morphing characters like the movie!)
inline void DrawMatrixRain(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    InitMatrixRain(static_cast<int>(screenSize.x));

    // Scale based on screen size (reference: 1920x1080)
    float scaleFactor = std::max(screenSize.x / 1920.0f, screenSize.y / 1080.0f);
    int columnWidth = static_cast<int>(14 * scaleFactor);
    int charHeight = static_cast<int>(16 * scaleFactor);
    int numChars = static_cast<int>(strlen(g_MatrixChars));

    for (size_t i = 0; i < g_MatrixColumns.size(); ++i) {
        MatrixColumn& col = g_MatrixColumns[i];

        // Update position
        col.y += col.speed * deltaTime;
        if (col.y > screenSize.y + col.length * charHeight) {
            col.y = -static_cast<float>(col.length * charHeight);
            col.speed = 50.0f + static_cast<float>(rand() % 150);
            col.morphInterval = 0.015f + static_cast<float>(rand() % 25) / 1000.0f; // 15-40ms

            // Randomize all characters on reset
            for (int j = 0; j < col.length; ++j) {
                col.chars[j] = g_MatrixChars[rand() % numChars];
            }
        }

        // === MATRIX CHARACTER MORPHING (like the movie!) ===
        // Characters randomly change as they fall, creating the "live data" effect
        col.morphTimer += deltaTime;
        if (col.morphTimer >= col.morphInterval) {
            col.morphTimer = 0.0f;
            // Randomly morph 2-5 characters in the trail for faster, more dynamic effect
            int numToMorph = 2 + rand() % 4;
            for (int m = 0; m < numToMorph; ++m) {
                int morphIdx = rand() % col.length;
                // Characters closer to tail morph more often (more unstable)
                if (morphIdx > 0 || rand() % 3 == 0) {  // Head morphs occasionally too
                    col.chars[morphIdx] = g_MatrixChars[rand() % numChars];
                }
            }
        }

        // Draw characters with fading trail
        float x = static_cast<float>(i * columnWidth);
        for (int j = 0; j < col.length; ++j) {
            float y = col.y - j * charHeight;
            if (y < 0 || y > screenSize.y) continue;

            // Brightness fades towards the tail
            float brightness = 1.0f - static_cast<float>(j) / static_cast<float>(col.length);
            int green = static_cast<int>(255 * brightness);
            int alpha = static_cast<int>(200 * brightness);

            // Head of trail is brightest (white-ish green glow)
            ImU32 color;
            if (j == 0) {
                color = IM_COL32(200, 255, 200, 255);  // Bright white-green
            } else if (j == 1) {
                color = IM_COL32(100, 255, 150, 240);  // Slightly dimmer
            } else {
                color = IM_COL32(0, green, green / 4, alpha);
            }

            char charStr[2] = {col.chars[j], '\0'};
            drawList->AddText(ImVec2(x, y), color, charStr);
        }
    }
}

// Draw Synthwave chaser lights effect
// Helper: Get neon color based on phase (cycles through pink, cyan, green)
inline ImU32 GetNeonColor(float phase, float brightness) {
    // 3 colors: Neon Pink (255, 20, 147), Neon Cyan (0, 255, 255), Neon Green (57, 255, 20)
    float p = std::fmod(phase, 3.0f);
    int r, g, b;

    if (p < 1.0f) {
        // Pink to Cyan
        float t = p;
        r = static_cast<int>((255 * (1 - t) + 0 * t) * brightness);
        g = static_cast<int>((20 * (1 - t) + 255 * t) * brightness);
        b = static_cast<int>((147 * (1 - t) + 255 * t) * brightness);
    } else if (p < 2.0f) {
        // Cyan to Green
        float t = p - 1.0f;
        r = static_cast<int>((0 * (1 - t) + 57 * t) * brightness);
        g = static_cast<int>((255 * (1 - t) + 255 * t) * brightness);
        b = static_cast<int>((255 * (1 - t) + 20 * t) * brightness);
    } else {
        // Green to Pink
        float t = p - 2.0f;
        r = static_cast<int>((57 * (1 - t) + 255 * t) * brightness);
        g = static_cast<int>((255 * (1 - t) + 20 * t) * brightness);
        b = static_cast<int>((20 * (1 - t) + 147 * t) * brightness);
    }

    return IM_COL32(r, g, b, static_cast<int>(255 * brightness));
}

inline void DrawSynthwaveChasers(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    // Faster chaser animation
    g_ChaserOffset += deltaTime * 120.0f;  // Faster (was 40)
    if (g_ChaserOffset > 1000.0f) g_ChaserOffset -= 1000.0f;

    // Color cycling (faster for vibrant effect)
    g_ChaserColorPhase += deltaTime * 1.5f;  // 3x faster color changes
    if (g_ChaserColorPhase > 3.0f) g_ChaserColorPhase -= 3.0f;

    // Grid pulse timer for center-outward effect (very slow = hypnotic)
    static float gridPulseTimer = 0.0f;
    gridPulseTimer += deltaTime * 0.125f;  // 1/16 original speed - very slow and smooth
    if (gridPulseTimer > 10.0f) gridPulseTimer -= 10.0f;

    // ========================================================================
    // Large Sunset Background (covers bottom half of screen)
    // ========================================================================
    float sunRadius = screenSize.x * 0.4f;  // Much larger sun
    float sunY = screenSize.y + sunRadius * 0.15f;  // Peek more above horizon
    float sunX = screenSize.x * 0.5f;
    float horizonY = screenSize.y * 0.6f;

    // Sky gradient (dark purple to orange near horizon)
    for (int i = 0; i < 40; ++i) {
        float t = static_cast<float>(i) / 40.0f;
        float y1 = horizonY * t;
        float y2 = horizonY * (t + 1.0f / 40.0f);

        // Purple at top, transitioning to orange/pink at horizon
        int r = static_cast<int>(20 + 80 * t);
        int g = static_cast<int>(10 + 20 * t);
        int b = static_cast<int>(40 + 60 * (1.0f - t * 0.5f));

        drawList->AddRectFilledMultiColor(
            ImVec2(0, y1), ImVec2(screenSize.x, y2),
            IM_COL32(r, g, b, 40), IM_COL32(r, g, b, 40),
            IM_COL32(r + 20, g + 10, b - 10, 50), IM_COL32(r + 20, g + 10, b - 10, 50));
    }

    // Sun glow (large gradient behind sun)
    for (int layer = 30; layer >= 0; --layer) {
        float t = static_cast<float>(layer) / 30.0f;
        float glowRadius = sunRadius * (1.0f + t * 0.8f);

        // Glow color: yellow/orange fading to pink/purple
        int r = 255;
        int g = static_cast<int>(200 * (1.0f - t * 0.7f));
        int b = static_cast<int>(50 + 150 * t);
        int alpha = static_cast<int>(15 * (1.0f - t));

        // Draw glow as filled segments
        for (float angle = 3.14159f; angle <= 2.0f * 3.14159f; angle += 0.02f) {
            float x1 = sunX + cosf(angle) * glowRadius;
            float y1 = sunY + sinf(angle) * glowRadius;
            float x2 = sunX + cosf(angle + 0.02f) * glowRadius;
            float y2 = sunY + sinf(angle + 0.02f) * glowRadius;

            if (y1 < screenSize.y && y2 < screenSize.y) {
                drawList->AddTriangleFilled(
                    ImVec2(sunX, sunY),
                    ImVec2(x1, y1),
                    ImVec2(x2, y2),
                    IM_COL32(r, g, b, alpha));
            }
        }
    }

    // Main sun disc (solid with horizontal stripes)
    for (float angle = 3.14159f; angle <= 2.0f * 3.14159f; angle += 0.01f) {
        float x1 = sunX + cosf(angle) * sunRadius;
        float y1 = sunY + sinf(angle) * sunRadius;
        float x2 = sunX + cosf(angle + 0.01f) * sunRadius;
        float y2 = sunY + sinf(angle + 0.01f) * sunRadius;

        if (y1 < screenSize.y && y2 < screenSize.y) {
            // Gradient from yellow at top to magenta at bottom
            float heightRatio = 1.0f - (sunY - y1) / sunRadius;
            int r = 255;
            int g = static_cast<int>(255 * (1.0f - heightRatio * 0.9f));
            int b = static_cast<int>(50 + 200 * heightRatio);

            drawList->AddTriangleFilled(
                ImVec2(sunX, sunY),
                ImVec2(x1, y1),
                ImVec2(x2, y2),
                IM_COL32(r, g, b, 200));
        }
    }

    // Horizontal stripe gaps in sun (classic synthwave look)
    float stripeSpacing = sunRadius / 8.0f;
    for (float y = sunY - sunRadius; y < screenSize.y; y += stripeSpacing) {
        if (y < horizonY) continue;

        // Calculate width at this height
        float dy = sunY - y;
        if (dy < 0 || dy > sunRadius) continue;
        float halfWidth = sqrtf(sunRadius * sunRadius - dy * dy);

        // Stripe gets thicker toward bottom
        float stripeHeight = 2.0f + (y - horizonY) / (screenSize.y - horizonY) * 8.0f;

        drawList->AddRectFilled(
            ImVec2(sunX - halfWidth, y),
            ImVec2(sunX + halfWidth, y + stripeHeight),
            IM_COL32(20, 10, 30, 255));  // Dark purple background color
    }

    // Horizon line
    drawList->AddLine(
        ImVec2(0, horizonY),
        ImVec2(screenSize.x, horizonY),
        IM_COL32(255, 100, 200, 150), 2.0f);

    // ========================================================================
    // Enhanced Grid Floor with Bold Pulsing Animation
    // Lines become bolder as the pulse wave moves outward from center
    // ========================================================================
    float vanishY = horizonY;
    float vanishX = sunX;
    int numVerticalLines = 40;  // More lines for denser grid

    // Create multiple pulse waves emanating from center
    float pulseWave1 = fmodf(gridPulseTimer * 0.8f, 1.0f);  // Main pulse
    float pulseWave2 = fmodf(gridPulseTimer * 0.8f + 0.5f, 1.0f);  // Secondary pulse (offset)

    // Vertical perspective lines
    for (int i = -numVerticalLines / 2; i <= numVerticalLines / 2; ++i) {
        float x = vanishX + i * (screenSize.x / numVerticalLines) * 2.5f;
        float distFromCenter = std::abs(static_cast<float>(i)) / (numVerticalLines / 2.0f);

        // Calculate pulse intensity - peaks when pulse wave reaches this distance
        float pulse1Dist = std::abs(distFromCenter - pulseWave1);
        float pulse2Dist = std::abs(distFromCenter - pulseWave2);
        float pulse1 = std::max(0.0f, 1.0f - pulse1Dist * 4.0f);  // Sharp falloff
        float pulse2 = std::max(0.0f, 1.0f - pulse2Dist * 4.0f) * 0.6f;  // Secondary weaker
        float pulse = std::min(1.0f, pulse1 + pulse2);
        pulse = pulse * pulse;  // Sharper peaks

        // Base visibility increases with distance from center (lines more visible at edges)
        float baseAlpha = 0.5f + distFromCenter * 0.3f;

        // Lines get BOLDER as pulse reaches them (outward from center)
        float thickness = 1.5f + pulse * 4.0f;  // Much thicker when pulsed
        float alpha = baseAlpha + pulse * 0.5f;

        // Color: magenta base, shifts to bright cyan when pulsed
        int r = static_cast<int>(255 * (1.0f - pulse * 0.7f));
        int g = static_cast<int>(50 + 205 * pulse);
        int b = 255;

        // Draw glow layer first (wider, more transparent)
        if (pulse > 0.1f) {
            drawList->AddLine(
                ImVec2(vanishX, vanishY),
                ImVec2(x, screenSize.y),
                IM_COL32(r, g, b, static_cast<int>(40 * pulse)), thickness + 6.0f);
        }

        // Draw main line
        drawList->AddLine(
            ImVec2(vanishX, vanishY),
            ImVec2(x, screenSize.y),
            IM_COL32(r, g, b, static_cast<int>(180 * alpha)), thickness);

        // Draw bright core when pulsed
        if (pulse > 0.3f) {
            drawList->AddLine(
                ImVec2(vanishX, vanishY),
                ImVec2(x, screenSize.y),
                IM_COL32(255, 255, 255, static_cast<int>(100 * pulse)), thickness * 0.3f);
        }
    }

    // Horizontal grid lines (closer together near horizon) with outward pulse
    int numHorizontalLines = 25;  // More lines
    for (int i = 1; i <= numHorizontalLines; ++i) {
        float t = static_cast<float>(i) / numHorizontalLines;
        float y = horizonY + (screenSize.y - horizonY) * t * t;  // Quadratic spacing

        // Distance from horizon (0 = at horizon, 1 = at bottom)
        float distFromHorizon = t;

        // Calculate pulse intensity for horizontal lines
        float pulse1Dist = std::abs(distFromHorizon - pulseWave1);
        float pulse2Dist = std::abs(distFromHorizon - pulseWave2);
        float pulse1 = std::max(0.0f, 1.0f - pulse1Dist * 3.5f);
        float pulse2 = std::max(0.0f, 1.0f - pulse2Dist * 3.5f) * 0.6f;
        float pulse = std::min(1.0f, pulse1 + pulse2);
        pulse = pulse * pulse;

        // Lines more visible as they get further from horizon
        float baseAlpha = 0.4f + distFromHorizon * 0.4f;

        // Thickness increases dramatically with pulse
        float thickness = 1.5f + pulse * 4.5f + distFromHorizon * 1.0f;  // Also thicker at bottom
        float alpha = baseAlpha + pulse * 0.6f;

        // Color: magenta to cyan shift
        int r = static_cast<int>(255 * (1.0f - pulse * 0.7f));
        int g = static_cast<int>(50 + 205 * pulse);
        int b = 255;

        // Glow layer
        if (pulse > 0.1f) {
            drawList->AddLine(
                ImVec2(0, y),
                ImVec2(screenSize.x, y),
                IM_COL32(r, g, b, static_cast<int>(35 * pulse)), thickness + 8.0f);
        }

        // Main line
        drawList->AddLine(
            ImVec2(0, y),
            ImVec2(screenSize.x, y),
            IM_COL32(r, g, b, static_cast<int>(160 * alpha)), thickness);

        // Bright core when pulsed
        if (pulse > 0.3f) {
            drawList->AddLine(
                ImVec2(0, y),
                ImVec2(screenSize.x, y),
                IM_COL32(255, 255, 255, static_cast<int>(90 * pulse)), thickness * 0.25f);
        }
    }

    // ========================================================================
    // Smooth Gradient Chasers (top and bottom)
    // ========================================================================
    // Scale effects based on screen size (reference: 1920x1080)
    float scaleFactor = std::max(screenSize.x / 1920.0f, screenSize.y / 1080.0f);
    float chaserHeight = 8.0f * scaleFactor;
    float glowHeight = 20.0f * scaleFactor;
    float stepSize = 2.0f * scaleFactor;  // Larger steps for higher res = better performance

    // Top chaser - smooth gradient wave
    float barWidth = stepSize + 1.0f;
    for (float x = 0; x < screenSize.x; x += stepSize) {
        float phase = x * 0.01f - g_ChaserOffset * 0.05f;
        float brightness = (sinf(phase) + 1.0f) * 0.5f;
        brightness = brightness * brightness;  // Sharper peaks

        float colorPhase = g_ChaserColorPhase + x * 0.002f;
        ImU32 color = GetNeonColor(colorPhase, brightness);

        // Main chaser bar
        drawList->AddRectFilled(
            ImVec2(x, 0),
            ImVec2(x + barWidth, chaserHeight * brightness + 2.0f * scaleFactor),
            color);

        // Glow below
        if (brightness > 0.3f) {
            ImU32 glowColor = GetNeonColor(colorPhase, brightness * 0.3f);
            drawList->AddRectFilledMultiColor(
                ImVec2(x, chaserHeight),
                ImVec2(x + barWidth, chaserHeight + glowHeight * brightness),
                glowColor, glowColor,
                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
        }
    }

    // Bottom chaser - smooth gradient wave (reversed direction, different color offset)
    for (float x = 0; x < screenSize.x; x += stepSize) {
        float phase = (screenSize.x - x) * 0.01f - g_ChaserOffset * 0.05f;
        float brightness = (sinf(phase) + 1.0f) * 0.5f;
        brightness = brightness * brightness;

        float colorPhase = g_ChaserColorPhase + 1.5f + x * 0.002f;  // Offset color
        ImU32 color = GetNeonColor(colorPhase, brightness);

        // Main chaser bar
        drawList->AddRectFilled(
            ImVec2(x, screenSize.y - chaserHeight * brightness - 2.0f * scaleFactor),
            ImVec2(x + barWidth, screenSize.y),
            color);

        // Glow above
        if (brightness > 0.3f) {
            ImU32 glowColor = GetNeonColor(colorPhase, brightness * 0.3f);
            drawList->AddRectFilledMultiColor(
                ImVec2(x, screenSize.y - chaserHeight - glowHeight * brightness),
                ImVec2(x + barWidth, screenSize.y - chaserHeight),
                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                glowColor, glowColor);
        }
    }

    // ========================================================================
    // Simple Neon Side Chasers - Clean traveling light bars on edges
    // ========================================================================
    float sideWidth = 5.0f * scaleFactor;

    // Simple chaser effect - just smooth traveling lights
    for (float y = 0; y < screenSize.y; y += stepSize) {
        // Single smooth wave traveling down
        float phase = y * 0.008f + g_ChaserOffset * 0.025f;
        float brightness = (sinf(phase) + 1.0f) * 0.5f;
        brightness = brightness * brightness * brightness;  // Cubic for sharper peaks

        if (brightness < 0.1f) continue;  // Skip dim sections

        float colorPhase = g_ChaserColorPhase + y * 0.001f;
        ImU32 color = GetNeonColor(colorPhase, brightness);

        float lightHeight = barWidth * (1.0f + brightness);

        // Left edge - simple bar
        drawList->AddRectFilled(
            ImVec2(0, y),
            ImVec2(sideWidth * (0.5f + brightness * 1.5f), y + lightHeight),
            color);

        // Right edge - simple bar
        float rightWidth = sideWidth * (0.5f + brightness * 1.5f);
        drawList->AddRectFilled(
            ImVec2(screenSize.x - rightWidth, y),
            ImVec2(screenSize.x, y + lightHeight),
            color);
    }
}

// Draw Cyberpunk glitch lines effect with enhanced animations
inline void DrawCyberpunkGlitch(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    static float glitchTimer = 0.0f;
    static float nextGlitchTime = 0.5f;
    static std::vector<std::pair<float, float>> glitchLines;  // y position and width
    static float hexTimer = 0.0f;
    static float flickerTimer = 0.0f;
    static bool flickerState = true;

    glitchTimer += deltaTime;
    hexTimer += deltaTime * 2.5f;  // Faster hex character animation
    flickerTimer += deltaTime;

    // Scale factor for higher resolutions (reference: 1920x1080)
    float scaleFactor = std::max(screenSize.x / 1920.0f, screenSize.y / 1080.0f);

    // Update global animation states (slower edge flashing)
    g_CyberpunkPulse += deltaTime * 1.5f;  // 1/4 speed for smoother edge pulse
    if (g_CyberpunkPulse > 6.28318f) g_CyberpunkPulse -= 6.28318f;

    g_DataStreamOffset += deltaTime * 180.0f;  // Faster data streams
    if (g_DataStreamOffset > 500.0f) g_DataStreamOffset -= 500.0f;

    // Flicker state (random)
    if (flickerTimer > 0.05f + static_cast<float>(rand() % 10) / 100.0f) {
        flickerTimer = 0.0f;
        flickerState = (rand() % 10) > 1;  // 90% on, 10% flicker
    }

    // ========================================================================
    // Background circuit pattern (scales with screen width)
    // ========================================================================
    // Faint circuit board lines - scale count with screen width
    int numCircuitLines = static_cast<int>(screenSize.x / 80.0f) + 5;
    float circuitSpacing = screenSize.x / static_cast<float>(numCircuitLines);
    for (int i = 0; i < numCircuitLines; ++i) {
        float x = (i * circuitSpacing + hexTimer * 5.0f);
        x = std::fmod(x, screenSize.x + 200.0f) - 100.0f;
        float alpha = 0.3f + 0.1f * sinf(hexTimer + i);

        // Vertical lines
        drawList->AddLine(
            ImVec2(x, 0),
            ImVec2(x, screenSize.y),
            IM_COL32(0, 80, 80, static_cast<int>(20 * alpha)), 1.0f);

        // Horizontal connections at random heights
        if (i % 3 == 0) {
            float y = std::fmod(i * 47.0f, screenSize.y);
            float nextX = x + circuitSpacing;
            if (nextX < screenSize.x) {
                drawList->AddLine(
                    ImVec2(x, y),
                    ImVec2(nextX, y),
                    IM_COL32(0, 100, 100, static_cast<int>(15 * alpha)), 1.0f);

                // Node dots (scaled for higher resolutions)
                drawList->AddCircleFilled(ImVec2(x, y), 3.0f * scaleFactor, IM_COL32(0, 150, 150, static_cast<int>(40 * alpha)));
            }
        }
    }

    // ========================================================================
    // Data streams (falling binary/hex characters on sides)
    // ========================================================================
    const char* hexChars = "0123456789ABCDEF";
    // Scale stream width and column count with screen dimensions
    float streamWidth = screenSize.x * 0.06f;  // 6% of screen width
    int numStreamCols = std::max(3, static_cast<int>(streamWidth / 20.0f));
    float colSpacing = streamWidth / static_cast<float>(numStreamCols);
    int numStreamRows = static_cast<int>(screenSize.y / 15.0f) + 5;

    // Left data stream
    for (int col = 0; col < numStreamCols; ++col) {
        float x = 10.0f + col * colSpacing;
        float offset = std::fmod(g_DataStreamOffset * (0.5f + col * 0.2f), 400.0f);

        for (int row = 0; row < numStreamRows; ++row) {
            float y = std::fmod(row * 20.0f + offset, screenSize.y + 40.0f) - 20.0f;
            int charIndex = (col * 7 + row * 3 + static_cast<int>(hexTimer * 10)) % 16;
            char str[2] = { hexChars[charIndex], 0 };

            float alpha = 0.3f + 0.2f * sinf(hexTimer * 3.0f + row * 0.3f);
            ImU32 color = (row % 5 == 0)
                ? IM_COL32(255, 255, 0, static_cast<int>(200 * alpha))   // Yellow highlight
                : IM_COL32(0, 255, 255, static_cast<int>(150 * alpha));  // Cyan

            drawList->AddText(ImVec2(x, y), color, str);
        }
    }

    // Right data stream
    for (int col = 0; col < numStreamCols; ++col) {
        float x = screenSize.x - 10.0f - col * colSpacing - colSpacing;
        float offset = std::fmod(g_DataStreamOffset * (0.7f + col * 0.15f), 400.0f);

        for (int row = 0; row < numStreamRows; ++row) {
            float y = std::fmod(row * 20.0f + offset, screenSize.y + 40.0f) - 20.0f;
            int charIndex = (col * 11 + row * 5 + static_cast<int>(hexTimer * 8)) % 16;
            char str[2] = { hexChars[charIndex], 0 };

            float alpha = 0.3f + 0.2f * sinf(hexTimer * 2.5f + row * 0.4f);
            ImU32 color = (row % 4 == 0)
                ? IM_COL32(255, 0, 128, static_cast<int>(200 * alpha))   // Pink highlight
                : IM_COL32(0, 255, 255, static_cast<int>(150 * alpha));  // Cyan

            drawList->AddText(ImVec2(x, y), color, str);
        }
    }

    // ========================================================================
    // Pulsing neon border
    // ========================================================================
    float pulseVal = (sinf(g_CyberpunkPulse) + 1.0f) * 0.5f;
    float borderWidth = (3.0f + pulseVal * 2.0f) * scaleFactor;
    int borderAlpha = static_cast<int>(100 + 100 * pulseVal);

    if (flickerState) {
        // Top border - yellow
        drawList->AddRectFilled(
            ImVec2(0, 0),
            ImVec2(screenSize.x, borderWidth),
            IM_COL32(255, 255, 0, borderAlpha));

        // Bottom border - cyan
        drawList->AddRectFilled(
            ImVec2(0, screenSize.y - borderWidth),
            ImVec2(screenSize.x, screenSize.y),
            IM_COL32(0, 255, 255, borderAlpha));

        // Left border - pink
        drawList->AddRectFilled(
            ImVec2(0, 0),
            ImVec2(borderWidth, screenSize.y),
            IM_COL32(255, 0, 128, borderAlpha));

        // Right border - pink
        drawList->AddRectFilled(
            ImVec2(screenSize.x - borderWidth, 0),
            ImVec2(screenSize.x, screenSize.y),
            IM_COL32(255, 0, 128, borderAlpha));

        // Corner glow effects - scale with screen size
        float baseCornerSize = std::min(screenSize.x, screenSize.y) * 0.06f;
        float cornerSize = baseCornerSize + pulseVal * baseCornerSize * 0.4f;
        // Top-left
        drawList->AddRectFilledMultiColor(
            ImVec2(0, 0), ImVec2(cornerSize, cornerSize),
            IM_COL32(255, 255, 0, 60), IM_COL32(255, 255, 0, 0),
            IM_COL32(255, 255, 0, 0), IM_COL32(255, 255, 0, 0));
        // Top-right
        drawList->AddRectFilledMultiColor(
            ImVec2(screenSize.x - cornerSize, 0), ImVec2(screenSize.x, cornerSize),
            IM_COL32(0, 255, 255, 0), IM_COL32(0, 255, 255, 60),
            IM_COL32(0, 255, 255, 0), IM_COL32(0, 255, 255, 0));
        // Bottom-left
        drawList->AddRectFilledMultiColor(
            ImVec2(0, screenSize.y - cornerSize), ImVec2(cornerSize, screenSize.y),
            IM_COL32(255, 0, 128, 0), IM_COL32(255, 0, 128, 0),
            IM_COL32(255, 0, 128, 0), IM_COL32(255, 0, 128, 60));
        // Bottom-right
        drawList->AddRectFilledMultiColor(
            ImVec2(screenSize.x - cornerSize, screenSize.y - cornerSize),
            ImVec2(screenSize.x, screenSize.y),
            IM_COL32(0, 255, 255, 0), IM_COL32(0, 255, 255, 0),
            IM_COL32(0, 255, 255, 60), IM_COL32(0, 255, 255, 0));
    }

    // ========================================================================
    // Glitch lines (enhanced)
    // ========================================================================
    // Trigger new glitch
    if (glitchTimer > nextGlitchTime) {
        glitchTimer = 0.0f;
        nextGlitchTime = 0.2f + static_cast<float>(rand() % 100) / 100.0f * 0.8f;

        glitchLines.clear();
        int numLines = 1 + rand() % 6;
        for (int i = 0; i < numLines; ++i) {
            float y = static_cast<float>(rand() % static_cast<int>(screenSize.y));
            float width = 30.0f + static_cast<float>(rand() % 300);
            glitchLines.push_back({y, width});
        }
    }

    // Draw glitch lines with chromatic aberration effect
    float glitchAlpha = 1.0f - (glitchTimer / nextGlitchTime);
    glitchAlpha = glitchAlpha * glitchAlpha;  // Faster falloff

    for (const auto& line : glitchLines) {
        float offset = static_cast<float>(rand() % 30 - 15);
        float height = 1.0f + static_cast<float>(rand() % 4);

        // Red channel offset
        drawList->AddRectFilled(
            ImVec2(offset - 3, line.first),
            ImVec2(offset + line.second - 3, line.first + height),
            IM_COL32(255, 0, 0, static_cast<int>(80 * glitchAlpha)));

        // Cyan/green channel
        drawList->AddRectFilled(
            ImVec2(offset + 3, line.first),
            ImVec2(offset + line.second + 3, line.first + height),
            IM_COL32(0, 255, 255, static_cast<int>(80 * glitchAlpha)));

        // White core
        drawList->AddRectFilled(
            ImVec2(offset, line.first),
            ImVec2(offset + line.second, line.first + height),
            IM_COL32(255, 255, 255, static_cast<int>(120 * glitchAlpha)));

        // Mirror glitch on other side
        float mirrorY = screenSize.y - line.first - height;
        drawList->AddRectFilled(
            ImVec2(screenSize.x - line.second + offset, mirrorY),
            ImVec2(screenSize.x + offset, mirrorY + height),
            IM_COL32(255, 0, 128, static_cast<int>(70 * glitchAlpha)));
    }

    // ========================================================================
    // Moving scan line
    // ========================================================================
    float scanY = std::fmod(hexTimer * 100.0f, screenSize.y + 100.0f) - 50.0f;
    drawList->AddRectFilledMultiColor(
        ImVec2(0, scanY - 30), ImVec2(screenSize.x, scanY),
        IM_COL32(0, 255, 255, 0), IM_COL32(0, 255, 255, 0),
        IM_COL32(0, 255, 255, 30), IM_COL32(0, 255, 255, 30));
    drawList->AddLine(
        ImVec2(0, scanY),
        ImVec2(screenSize.x, scanY),
        IM_COL32(0, 255, 255, 80), 2.0f);

    // ========================================================================
    // CRT scanlines overlay
    // ========================================================================
    for (float y = 0; y < screenSize.y; y += 2.0f) {
        drawList->AddLine(
            ImVec2(0, y),
            ImVec2(screenSize.x, y),
            IM_COL32(0, 0, 0, 25), 1.0f);
    }

    // ========================================================================
    // Random pixel noise (subtle)
    // ========================================================================
    if (rand() % 3 == 0) {  // Only sometimes
        for (int i = 0; i < 20; ++i) {
            float x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
            float y = static_cast<float>(rand() % static_cast<int>(screenSize.y));
            ImU32 color = (rand() % 2 == 0)
                ? IM_COL32(0, 255, 255, 100)
                : IM_COL32(255, 255, 0, 100);
            drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + 2, y + 2), color);
        }
    }
}

// ============================================================================
// Frutiger Aero Theme - Floating glossy bubbles and glass reflections
// ============================================================================
struct AeroBubble {
    float x, y;
    float radius;
    float speed;
    float wobble;
    float hue;  // 0-1 for color variation
};

static std::vector<AeroBubble> g_AeroBubbles;
static bool g_AeroInitialized = false;
static float g_AeroTime = 0.0f;

inline void InitAeroBubbles(ImVec2 screenSize) {
    if (g_AeroInitialized) return;
    g_AeroBubbles.clear();

    // Create 25 bubbles
    for (int i = 0; i < 25; ++i) {
        AeroBubble bubble;
        bubble.x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
        bubble.y = static_cast<float>(rand() % static_cast<int>(screenSize.y));
        bubble.radius = 20.0f + static_cast<float>(rand() % 60);
        bubble.speed = 15.0f + static_cast<float>(rand() % 30);
        bubble.wobble = static_cast<float>(rand() % 100) / 100.0f * 6.28f;
        bubble.hue = static_cast<float>(rand() % 100) / 100.0f;
        g_AeroBubbles.push_back(bubble);
    }
    g_AeroInitialized = true;
}

// Tropical fish, drifting across the scene. Frutiger Aero's single most
// recognisable motif after the bubbles - the Windows 7 beta fish, the
// aquarium wallpapers, the "technology in harmony with nature" idea made
// literal.
struct AeroFish {
    float x, y;
    float speed;
    float scale;
    float phase;
    int   direction;   // +1 swims right, -1 swims left
    ImU32 body;
    ImU32 fin;
};

static std::vector<AeroFish> g_AeroFish;
static bool g_AeroFishInit = false;

inline void InitAeroFish(ImVec2 screenSize) {
    if (g_AeroFishInit) return;
    g_AeroFishInit = true;
    g_AeroFish.clear();

    // Warm tropical colours against the blue-green ground.
    const ImU32 bodies[] = {
        IM_COL32(255, 158,  60, 190),   // clownfish orange
        IM_COL32(255, 206,  80, 180),   // yellow tang
        IM_COL32( 92, 190, 235, 175),   // powder blue
        IM_COL32(240, 130, 165, 175),   // coral pink
        IM_COL32(130, 225, 180, 170),   // mint
    };

    for (int i = 0; i < 6; ++i) {
        AeroFish f;
        f.direction = (i % 2 == 0) ? 1 : -1;
        f.scale = 0.75f + (i % 3) * 0.28f;
        f.speed = (18.0f + (i % 4) * 9.0f) * f.scale;
        f.x = (screenSize.x / 6.0f) * i;
        f.y = screenSize.y * (0.28f + 0.11f * (i % 5));
        f.phase = i * 1.37f;
        f.body = bodies[i % 5];
        f.fin = IM_COL32(255, 255, 255, 110);
        g_AeroFish.push_back(f);
    }
}

// A fish is a body ellipse, a triangular tail, a dorsal fin and an eye.
// Drawn small and semi-transparent, that reads unmistakably as a fish
// without needing a sprite.
inline void DrawAeroFish(ImDrawList* drawList, const AeroFish& fish, float bob) {
    const float s = fish.scale;
    const float dir = static_cast<float>(fish.direction);
    const float x = fish.x;
    const float y = fish.y + bob;

    const float bodyW = 22.0f * s;
    const float bodyH = 11.0f * s;

    // Tail, behind the body
    const float tailX = x - dir * bodyW * 0.95f;
    drawList->AddTriangleFilled(
        ImVec2(tailX, y),
        ImVec2(tailX - dir * 13.0f * s, y - 9.0f * s),
        ImVec2(tailX - dir * 13.0f * s, y + 9.0f * s),
        fish.body);

    // Dorsal fin
    drawList->AddTriangleFilled(
        ImVec2(x - dir * 2.0f * s, y - bodyH * 0.85f),
        ImVec2(x + dir * 7.0f * s, y - bodyH * 1.75f),
        ImVec2(x + dir * 9.0f * s, y - bodyH * 0.6f),
        fish.fin);

    drawList->AddEllipseFilled(ImVec2(x, y), ImVec2(bodyW, bodyH), fish.body, 0.0f, 24);

    // Glossy top-light, the skeuomorphic tell
    drawList->AddEllipseFilled(
        ImVec2(x - dir * 3.0f * s, y - bodyH * 0.42f),
        ImVec2(bodyW * 0.52f, bodyH * 0.30f),
        IM_COL32(255, 255, 255, 90), 0.0f, 16);

    // Eye
    drawList->AddCircleFilled(
        ImVec2(x + dir * bodyW * 0.55f, y - bodyH * 0.18f), 2.4f * s,
        IM_COL32(30, 45, 60, 210), 10);
}

inline void DrawFrutigerAero(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    InitAeroBubbles(screenSize);
    InitAeroFish(screenSize);
    g_AeroTime += deltaTime;

    // ------------------------------------------------------------------
    // Sky. In this aesthetic a gradient is lighting, not decoration - it
    // should read as light falling through air and water rather than as a
    // colour ramp. Deep sky at the top, brightening toward the horizon.
    // ------------------------------------------------------------------
    const float horizon = screenSize.y * 0.72f;

    drawList->AddRectFilledMultiColor(
        ImVec2(0, 0), ImVec2(screenSize.x, horizon),
        IM_COL32( 96, 178, 240, 255), IM_COL32( 96, 178, 240, 255),
        IM_COL32(214, 242, 252, 255), IM_COL32(214, 242, 252, 255));

    // ------------------------------------------------------------------
    // Sun and lens flare. The flare is the period signature - a chain of
    // translucent discs along the axis from the sun through the centre.
    // ------------------------------------------------------------------
    const ImVec2 sun(screenSize.x * 0.78f, horizon * 0.26f);
    for (int i = 7; i >= 1; --i) {
        drawList->AddCircleFilled(sun, 26.0f * i,
                                  IM_COL32(255, 252, 226, 12), 40);
    }
    drawList->AddCircleFilled(sun, 26.0f, IM_COL32(255, 255, 244, 210), 40);

    const ImVec2 centre(screenSize.x * 0.5f, screenSize.y * 0.5f);
    for (int i = 1; i <= 5; ++i) {
        const float t = i * 0.34f;
        const ImVec2 pos(sun.x + (centre.x - sun.x) * t * 1.6f,
                         sun.y + (centre.y - sun.y) * t * 1.6f);
        const float radius = 9.0f + (i % 3) * 13.0f;
        const ImU32 tint = (i % 2 == 0) ? IM_COL32(180, 255, 210, 26)
                                        : IM_COL32(255, 226, 170, 26);
        drawList->AddCircleFilled(pos, radius, tint, 28);
        drawList->AddCircle(pos, radius, IM_COL32(255, 255, 255, 20), 28, 1.5f);
    }

    // ------------------------------------------------------------------
    // Clouds - soft, high, unhurried.
    // ------------------------------------------------------------------
    for (int i = 0; i < 6; ++i) {
        const float cloudX = std::fmod(g_AeroTime * 7.0f + i * screenSize.x / 5.0f,
                                       screenSize.x + 460.0f) - 230.0f;
        const float cloudY = 40.0f + i * 34.0f + std::sin(g_AeroTime * 0.4f + i) * 9.0f;
        const float cloudW = 180.0f + i * 26.0f;
        const float cloudH = 44.0f + i * 7.0f;

        drawList->AddEllipseFilled(ImVec2(cloudX, cloudY), ImVec2(cloudW, cloudH),
                                   IM_COL32(255, 255, 255, 62), 0.0f, 32);
        drawList->AddEllipseFilled(ImVec2(cloudX + cloudW * 0.42f, cloudY + cloudH * 0.18f),
                                   ImVec2(cloudW * 0.62f, cloudH * 0.78f),
                                   IM_COL32(255, 255, 255, 48), 0.0f, 32);
    }

    // ------------------------------------------------------------------
    // Bokeh. Out-of-focus highlights, the other half of the period look.
    // ------------------------------------------------------------------
    for (int i = 0; i < 14; ++i) {
        const float bx = std::fmod(i * 137.5f + g_AeroTime * 4.0f, screenSize.x);
        const float by = std::fmod(i * 91.3f + std::sin(g_AeroTime * 0.3f + i) * 24.0f,
                                   horizon);
        const float br = 8.0f + (i % 5) * 7.0f;
        drawList->AddCircleFilled(ImVec2(bx, by), br, IM_COL32(255, 255, 255, 20), 20);
        drawList->AddCircle(ImVec2(bx, by), br, IM_COL32(255, 255, 255, 26), 20, 1.5f);
    }

    // ------------------------------------------------------------------
    // Grass. The green half of the palette, and the half that was missing -
    // the theme read as "light blue" without it. Two layers, the back one
    // darker and slower, so the band has depth rather than being a fringe.
    // ------------------------------------------------------------------
    drawList->AddRectFilledMultiColor(
        ImVec2(0, horizon), ImVec2(screenSize.x, screenSize.y),
        IM_COL32(150, 216, 118, 190), IM_COL32(150, 216, 118, 190),
        IM_COL32( 58, 148,  62, 235), IM_COL32( 58, 148,  62, 235));

    struct GrassLayer { float step, height, sway, speed; ImU32 color; };
    const GrassLayer layers[] = {
        {13.0f, 74.0f, 5.0f, 0.7f, IM_COL32( 74, 158,  74, 205)},   // back
        { 9.0f, 52.0f, 8.0f, 1.2f, IM_COL32(112, 196,  92, 225)},   // front
    };

    for (const GrassLayer& layer : layers) {
        for (float x = -10.0f; x < screenSize.x + 10.0f; x += layer.step) {
            const float seed = x * 0.09f;
            const float height = layer.height * (0.62f + 0.38f * std::sin(seed * 3.1f));
            const float sway = std::sin(g_AeroTime * layer.speed + seed) * layer.sway;
            const float baseY = screenSize.y + 4.0f;
            const float tipY = baseY - height;

            drawList->AddTriangleFilled(
                ImVec2(x - 3.0f, baseY),
                ImVec2(x + 3.0f, baseY),
                ImVec2(x + sway, tipY),
                layer.color);
        }
    }

    // Caustic light on the grass, as if seen through water
    for (int i = 0; i < 9; ++i) {
        const float cx = std::fmod(i * 173.0f + g_AeroTime * 16.0f, screenSize.x + 120.0f) - 60.0f;
        const float cy = horizon + 12.0f + (i % 3) * 22.0f;
        const float cw = 46.0f + (i % 4) * 22.0f;
        drawList->AddEllipseFilled(ImVec2(cx, cy), ImVec2(cw, 7.0f),
                                   IM_COL32(255, 255, 255, 26), 0.0f, 20);
    }

    // ------------------------------------------------------------------
    // Fish, between the grass and the bubbles.
    // ------------------------------------------------------------------
    for (AeroFish& fish : g_AeroFish) {
        fish.x += fish.speed * static_cast<float>(fish.direction) * deltaTime;

        const float margin = 90.0f * fish.scale;
        if (fish.direction > 0 && fish.x > screenSize.x + margin) fish.x = -margin;
        if (fish.direction < 0 && fish.x < -margin) fish.x = screenSize.x + margin;

        const float bob = std::sin(g_AeroTime * 1.6f + fish.phase) * 7.0f;
        DrawAeroFish(drawList, fish, bob);
    }

    // ------------------------------------------------------------------
    // Bubbles last, so they float in front of everything.
    // ------------------------------------------------------------------
    for (auto& bubble : g_AeroBubbles) {
        bubble.y -= bubble.speed * deltaTime;
        bubble.x += std::sin(g_AeroTime * 2.0f + bubble.wobble) * 0.5f;

        if (bubble.y + bubble.radius < 0) {
            bubble.y = screenSize.y + bubble.radius;
            bubble.x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
        }

        const float r = bubble.radius;

        const int baseR = static_cast<int>(120 + bubble.hue * 60);
        const int baseG = static_cast<int>(198 + bubble.hue * 40);
        const int baseB = static_cast<int>(236 + bubble.hue * 19);
        drawList->AddCircleFilled(ImVec2(bubble.x, bubble.y), r,
                                  IM_COL32(baseR, baseG, baseB, 96), 32);

        // Rim light along the lower edge - a real bubble catches light
        // there too, and it is what stops it reading as a flat disc.
        drawList->AddCircle(ImVec2(bubble.x, bubble.y + r * 0.10f), r * 0.92f,
                            IM_COL32(255, 255, 255, 70), 32, 2.0f);

        const float highlightX = bubble.x - r * 0.32f;
        const float highlightY = bubble.y - r * 0.32f;
        drawList->AddCircleFilled(ImVec2(highlightX, highlightY), r * 0.36f,
                                  IM_COL32(255, 255, 255, 170), 16);
        drawList->AddCircleFilled(ImVec2(highlightX + r * 0.14f, highlightY + r * 0.14f),
                                  r * 0.14f, IM_COL32(255, 255, 255, 225), 12);

        drawList->AddCircle(ImVec2(bubble.x, bubble.y), r,
                            IM_COL32(120, 176, 214, 88), 32, 1.6f);
    }

    // Glass reflection bars at the very top, the Aero window-chrome nod
    for (int i = 0; i < 3; ++i) {
        const float barY = 18.0f + i * 8.0f;
        const float barWidth = screenSize.x * (0.30f - i * 0.08f);
        const float barX = (screenSize.x - barWidth) * 0.5f + std::sin(g_AeroTime + i) * 46.0f;
        drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + 3.0f),
                                IM_COL32(255, 255, 255, 58 - i * 15));
    }
}

// ============================================================================
// Minimal Theme - Subtle geometric patterns and clean lines
// ============================================================================
static float g_MinimalTime = 0.0f;

inline void DrawMinimalTheme(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    g_MinimalTime += deltaTime;

    // Dark background
    drawList->AddRectFilled(ImVec2(0, 0), screenSize, IM_COL32(28, 28, 32, 255));

    // Subtle grid pattern (very faint)
    float gridSize = 60.0f;
    ImU32 gridColor = IM_COL32(45, 45, 52, 255);

    for (float x = 0; x < screenSize.x; x += gridSize) {
        drawList->AddLine(ImVec2(x, 0), ImVec2(x, screenSize.y), gridColor, 1.0f);
    }
    for (float y = 0; y < screenSize.y; y += gridSize) {
        drawList->AddLine(ImVec2(0, y), ImVec2(screenSize.x, y), gridColor, 1.0f);
    }

    // Animated accent line traveling across bottom
    float lineProgress = std::fmod(g_MinimalTime * 0.15f, 1.0f);
    float lineX = lineProgress * (screenSize.x + 400.0f) - 200.0f;
    float lineWidth = 200.0f;

    // Gradient line (red accent)
    for (float i = 0; i < lineWidth; i += 2.0f) {
        float alpha = sinf((i / lineWidth) * 3.14159f);
        drawList->AddLine(
            ImVec2(lineX + i, screenSize.y - 3.0f),
            ImVec2(lineX + i, screenSize.y),
            IM_COL32(242, 77, 89, static_cast<int>(alpha * 200)), 3.0f);
    }

    // Corner geometric accents (subtle triangles)
    float cornerSize = 80.0f;
    float pulse = (sinf(g_MinimalTime * 2.0f) + 1.0f) * 0.5f;
    int alpha = static_cast<int>(30 + pulse * 20);

    // Top-left corner
    drawList->AddTriangleFilled(
        ImVec2(0, 0),
        ImVec2(cornerSize, 0),
        ImVec2(0, cornerSize),
        IM_COL32(242, 77, 89, alpha));

    // Bottom-right corner
    drawList->AddTriangleFilled(
        ImVec2(screenSize.x, screenSize.y),
        ImVec2(screenSize.x - cornerSize, screenSize.y),
        ImVec2(screenSize.x, screenSize.y - cornerSize),
        IM_COL32(242, 77, 89, alpha));

    // Floating geometric shapes (very subtle)
    for (int i = 0; i < 5; ++i) {
        float shapeX = std::fmod(g_MinimalTime * 20.0f + i * 400.0f, screenSize.x + 100.0f) - 50.0f;
        float shapeY = screenSize.y * 0.3f + sinf(g_MinimalTime * 0.5f + i * 1.5f) * 100.0f;
        float size = 15.0f + i * 5.0f;

        // Hollow squares
        drawList->AddRect(
            ImVec2(shapeX - size, shapeY - size),
            ImVec2(shapeX + size, shapeY + size),
            IM_COL32(242, 77, 89, 40), 0.0f, 0, 1.5f);
    }
}

// ============================================================================
// Vaporwave Theme - Pink/cyan grid, floating shapes, retro aesthetic
// ============================================================================
static float g_VaporwaveTime = 0.0f;

struct VaporShape {
    float x, y, z;  // z for depth
    int type;       // 0=triangle, 1=circle, 2=square
    float rotation;
    float rotSpeed;
};

static std::vector<VaporShape> g_VaporShapes;
static bool g_VaporInitialized = false;

inline void InitVaporShapes(ImVec2 screenSize) {
    if (g_VaporInitialized) return;
    g_VaporShapes.clear();

    for (int i = 0; i < 15; ++i) {
        VaporShape shape;
        shape.x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
        shape.y = static_cast<float>(rand() % static_cast<int>(screenSize.y));
        shape.z = 0.3f + static_cast<float>(rand() % 70) / 100.0f;
        shape.type = rand() % 3;
        shape.rotation = static_cast<float>(rand() % 360) * 0.0174533f;
        shape.rotSpeed = (static_cast<float>(rand() % 100) / 100.0f - 0.5f) * 0.5f;
        g_VaporShapes.push_back(shape);
    }
    g_VaporInitialized = true;
}

inline void DrawVaporwaveTheme(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    InitVaporShapes(screenSize);
    g_VaporwaveTime += deltaTime;

    // Gradient background (deep purple to dark)
    ImU32 topColor = IM_COL32(40, 10, 60, 255);
    ImU32 bottomColor = IM_COL32(15, 5, 25, 255);
    drawList->AddRectFilledMultiColor(
        ImVec2(0, 0), screenSize,
        topColor, topColor, bottomColor, bottomColor);

    // Sun at bottom (pink/orange gradient circle)
    float sunRadius = screenSize.x * 0.25f;
    float sunY = screenSize.y + sunRadius * 0.3f;
    float sunX = screenSize.x / 2.0f;

    // Sun glow
    for (int i = 5; i >= 0; --i) {
        float glowRadius = sunRadius + i * 30.0f;
        int alpha = 20 - i * 3;
        drawList->AddCircleFilled(ImVec2(sunX, sunY), glowRadius,
            IM_COL32(255, 100, 150, alpha), 64);
    }

    // Main sun (with horizontal stripes cut out)
    for (float y = sunY - sunRadius; y < sunY + sunRadius; y += 8.0f) {
        if (static_cast<int>((y - sunY + sunRadius) / 16.0f) % 2 == 0) {
            float dy = y - sunY;
            float halfChord = sqrtf(std::max(0.0f, sunRadius * sunRadius - dy * dy));

            // Color gradient from top (orange) to bottom (pink)
            float t = (y - (sunY - sunRadius)) / (sunRadius * 2.0f);
            int r = static_cast<int>(255);
            int g = static_cast<int>(180 - t * 130);
            int b = static_cast<int>(100 + t * 100);

            drawList->AddRectFilled(
                ImVec2(sunX - halfChord, y),
                ImVec2(sunX + halfChord, y + 6.0f),
                IM_COL32(r, g, b, 255));
        }
    }

    // Perspective grid
    float horizonY = screenSize.y * 0.55f;
    float vanishX = screenSize.x / 2.0f;

    // Horizontal lines (receding into distance)
    for (int i = 0; i < 20; ++i) {
        float t = static_cast<float>(i) / 20.0f;
        float y = horizonY + powf(t, 1.5f) * (screenSize.y - horizonY);
        float alpha = t * 255;

        // Animate grid movement
        float offset = std::fmod(g_VaporwaveTime * 50.0f, (screenSize.y - horizonY) / 20.0f);
        y += offset * powf(t, 1.5f);
        if (y > screenSize.y) continue;

        drawList->AddLine(
            ImVec2(0, y), ImVec2(screenSize.x, y),
            IM_COL32(0, 220, 220, static_cast<int>(alpha * 0.6f)), 1.5f);
    }

    // Vertical lines (converging to vanishing point)
    for (int i = -10; i <= 10; ++i) {
        float bottomX = vanishX + i * 120.0f;
        drawList->AddLine(
            ImVec2(vanishX, horizonY),
            ImVec2(bottomX, screenSize.y),
            IM_COL32(255, 100, 200, 100), 1.5f);
    }

    // Floating shapes
    for (auto& shape : g_VaporShapes) {
        shape.rotation += shape.rotSpeed * deltaTime;
        shape.x += sinf(g_VaporwaveTime + shape.z * 10.0f) * 0.3f;
        shape.y -= 10.0f * deltaTime * shape.z;

        if (shape.y < -50) {
            shape.y = screenSize.y + 50;
            shape.x = static_cast<float>(rand() % static_cast<int>(screenSize.x));
        }

        float size = 20.0f * shape.z + 10.0f;
        int alpha = static_cast<int>(shape.z * 150);

        ImVec2 center(shape.x, shape.y);

        if (shape.type == 0) {
            // Triangle (wireframe)
            ImVec2 p1(center.x, center.y - size);
            ImVec2 p2(center.x - size * 0.866f, center.y + size * 0.5f);
            ImVec2 p3(center.x + size * 0.866f, center.y + size * 0.5f);
            drawList->AddTriangle(p1, p2, p3, IM_COL32(0, 255, 255, alpha), 2.0f);
        } else if (shape.type == 1) {
            // Circle (wireframe)
            drawList->AddCircle(center, size, IM_COL32(255, 100, 200, alpha), 24, 2.0f);
        } else {
            // Square (wireframe, rotated)
            float c = cosf(shape.rotation);
            float s = sinf(shape.rotation);
            ImVec2 corners[4];
            float offsets[4][2] = {{-1,-1}, {1,-1}, {1,1}, {-1,1}};
            for (int j = 0; j < 4; ++j) {
                float ox = offsets[j][0] * size;
                float oy = offsets[j][1] * size;
                corners[j] = ImVec2(center.x + ox*c - oy*s, center.y + ox*s + oy*c);
            }
            for (int j = 0; j < 4; ++j) {
                drawList->AddLine(corners[j], corners[(j+1)%4], IM_COL32(255, 200, 100, alpha), 2.0f);
            }
        }
    }
}

// ============================================================================
// Retro Terminal Theme - Authentic CRT monitor simulation
// Features: Scanlines, phosphor glow, screen curvature, color bleeding
// ============================================================================
static float g_TerminalTime = 0.0f;
static float g_TerminalFlicker = 1.0f;
static float g_ScanlinePhase = 0.0f;

inline void DrawRetroTerminal(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    g_TerminalTime += deltaTime;
    g_ScanlinePhase += deltaTime * 60.0f;  // 60Hz refresh simulation

    // ========================================================================
    // Screen flicker (subtle brightness variation like old CRTs)
    // ========================================================================
    if (rand() % 100 < 3) {
        g_TerminalFlicker = 0.92f + static_cast<float>(rand() % 8) / 100.0f;
    } else {
        g_TerminalFlicker = g_TerminalFlicker * 0.92f + 1.0f * 0.08f;
    }

    // Base brightness multiplier
    float brightness = g_TerminalFlicker;

    // ========================================================================
    // Dark CRT background with phosphor base color
    // ========================================================================
    int bgR = static_cast<int>(12 * brightness);
    int bgG = static_cast<int>(8 * brightness);
    int bgB = static_cast<int>(2 * brightness);
    drawList->AddRectFilled(ImVec2(0, 0), screenSize, IM_COL32(bgR, bgG, bgB, 255));

    // ========================================================================
    // CRT Curvature simulation - darker edges (barrel distortion effect)
    // ========================================================================
    ImVec2 center(screenSize.x / 2.0f, screenSize.y / 2.0f);
    float maxDist = sqrtf(center.x * center.x + center.y * center.y);

    // Draw radial vignette using concentric rectangles
    for (int ring = 0; ring < 8; ++ring) {
        float t = static_cast<float>(ring) / 8.0f;
        float edgeX = screenSize.x * (0.5f - t * 0.5f);
        float edgeY = screenSize.y * (0.5f - t * 0.5f);
        int alpha = static_cast<int>((1.0f - t) * 60);

        // Corner darkening (more pronounced)
        drawList->AddRectFilled(
            ImVec2(0, 0), ImVec2(edgeX, edgeY),
            IM_COL32(0, 0, 0, alpha));
        drawList->AddRectFilled(
            ImVec2(screenSize.x - edgeX, 0), ImVec2(screenSize.x, edgeY),
            IM_COL32(0, 0, 0, alpha));
        drawList->AddRectFilled(
            ImVec2(0, screenSize.y - edgeY), ImVec2(edgeX, screenSize.y),
            IM_COL32(0, 0, 0, alpha));
        drawList->AddRectFilled(
            ImVec2(screenSize.x - edgeX, screenSize.y - edgeY), screenSize,
            IM_COL32(0, 0, 0, alpha));
    }

    // ========================================================================
    // PROMINENT SCANLINES - The signature CRT effect
    // ========================================================================
    float scanlineSpacing = 2.0f;  // Tight scanlines
    float scanlineOffset = std::fmod(g_ScanlinePhase * 0.5f, scanlineSpacing);

    for (float y = scanlineOffset; y < screenSize.y; y += scanlineSpacing) {
        // Alternating bright/dark scanlines
        int lineIndex = static_cast<int>(y / scanlineSpacing);
        if (lineIndex % 2 == 0) {
            // Dark scanline (the gap between phosphor rows)
            drawList->AddLine(
                ImVec2(0, y), ImVec2(screenSize.x, y),
                IM_COL32(0, 0, 0, 100), 1.0f);
        }
    }

    // ========================================================================
    // RGB Sub-pixel simulation (color fringing on edges)
    // ========================================================================
    // Subtle RGB separation at screen edges
    float fringeWidth = 3.0f;

    // Left edge - red shift
    drawList->AddRectFilled(
        ImVec2(0, 0), ImVec2(fringeWidth, screenSize.y),
        IM_COL32(80, 0, 0, 30));

    // Right edge - blue shift
    drawList->AddRectFilled(
        ImVec2(screenSize.x - fringeWidth, 0), screenSize,
        IM_COL32(0, 0, 80, 30));

    // ========================================================================
    // Phosphor glow (warm amber bloom)
    // ========================================================================
    float glowPulse = (sinf(g_TerminalTime * 0.3f) + 1.0f) * 0.5f;
    int glowR = static_cast<int>((255 * 0.15f + glowPulse * 20) * brightness);
    int glowG = static_cast<int>((180 * 0.15f + glowPulse * 15) * brightness);
    int glowB = static_cast<int>((50 * 0.15f + glowPulse * 5) * brightness);
    drawList->AddRectFilled(ImVec2(0, 0), screenSize,
        IM_COL32(glowR, glowG, glowB, static_cast<int>(25 * brightness)));

    // ========================================================================
    // Horizontal sync wobble (subtle screen shake)
    // ========================================================================
    float wobble = sinf(g_TerminalTime * 120.0f) * 0.3f;
    if (rand() % 500 < 1) {
        // Occasional horizontal tear/glitch
        float tearY = static_cast<float>(rand() % static_cast<int>(screenSize.y));
        float tearHeight = 2.0f + (rand() % 4);
        float tearOffset = (rand() % 20) - 10;
        drawList->AddRectFilled(
            ImVec2(tearOffset, tearY),
            ImVec2(screenSize.x + tearOffset, tearY + tearHeight),
            IM_COL32(255, 200, 100, 120));
    }

    // ========================================================================
    // Interlace effect (every other frame shows different lines)
    // ========================================================================
    int frameNum = static_cast<int>(g_TerminalTime * 30.0f) % 2;
    for (float y = static_cast<float>(frameNum); y < screenSize.y; y += 4.0f) {
        drawList->AddLine(
            ImVec2(0, y), ImVec2(screenSize.x, y),
            IM_COL32(255, 200, 80, 8), 1.0f);
    }

    // ========================================================================
    // Screen border (CRT bezel simulation)
    // ========================================================================
    float bezelWidth = 8.0f;

    // Outer dark bezel
    drawList->AddRect(
        ImVec2(0, 0), screenSize,
        IM_COL32(20, 15, 5, 255), 0.0f, 0, bezelWidth);

    // Inner glowing edge (phosphor bleed at edges)
    float edgePulse = (sinf(g_TerminalTime * 0.8f) + 1.0f) * 0.5f;
    int edgeGlow = static_cast<int>((50 + edgePulse * 30) * brightness);
    drawList->AddRect(
        ImVec2(bezelWidth, bezelWidth),
        ImVec2(screenSize.x - bezelWidth, screenSize.y - bezelWidth),
        IM_COL32(255, 180, 50, edgeGlow), 0.0f, 0, 2.0f);

    // ========================================================================
    // Ambient glow (light bleeding from screen)
    // ========================================================================
    // Top glow
    for (int i = 0; i < 5; ++i) {
        float glowHeight = 20.0f - i * 4.0f;
        int glowAlpha = static_cast<int>((15 - i * 3) * brightness);
        drawList->AddRectFilled(
            ImVec2(bezelWidth * 2, bezelWidth + i * 2),
            ImVec2(screenSize.x - bezelWidth * 2, bezelWidth + glowHeight + i * 2),
            IM_COL32(255, 200, 100, glowAlpha));
    }

    // ========================================================================
    // Cursor blink (classic block cursor)
    // ========================================================================
    if (static_cast<int>(g_TerminalTime * 1.5f) % 2 == 0) {
        float cursorX = 30.0f;
        float cursorY = screenSize.y - 40.0f;
        int cursorAlpha = static_cast<int>(220 * brightness);
        drawList->AddRectFilled(
            ImVec2(cursorX, cursorY),
            ImVec2(cursorX + 10, cursorY + 14),
            IM_COL32(255, 200, 80, cursorAlpha));
    }

    // ========================================================================
    // Power-on effect (subtle vertical roll on startup)
    // ========================================================================
    if (g_TerminalTime < 2.0f) {
        float rollOffset = (2.0f - g_TerminalTime) * screenSize.y * 0.3f;
        float rollAlpha = (2.0f - g_TerminalTime) / 2.0f * 255;
        drawList->AddRectFilled(
            ImVec2(0, 0), ImVec2(screenSize.x, rollOffset),
            IM_COL32(0, 0, 0, static_cast<int>(rollAlpha)));
    }

    // ========================================================================
    // Screen reflection (subtle highlight on glass)
    // ========================================================================
    ImVec2 reflectStart(screenSize.x * 0.1f, screenSize.y * 0.05f);
    ImVec2 reflectEnd(screenSize.x * 0.4f, screenSize.y * 0.15f);
    drawList->AddRectFilledMultiColor(
        reflectStart, reflectEnd,
        IM_COL32(255, 255, 255, 8), IM_COL32(255, 255, 255, 3),
        IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 5));
}

// Draw theme background effects
// Game Boy DMG dot matrix. The original LCD had a visible pixel grid and a
// faint vertical banding; both are what makes a screenshot read instantly
// as a Game Boy rather than as "something green".
inline void DrawGameBoyDotMatrix(ImDrawList* drawList, ImVec2 screenSize, float deltaTime) {
    static float scanPhase = 0.0f;
    scanPhase += deltaTime * 0.35f;
    if (scanPhase > 1.0f) scanPhase -= 1.0f;

    const ImU32 background = IM_COL32(15, 56, 15, 255);
    drawList->AddRectFilled(ImVec2(0, 0), screenSize, background);

    // Dot grid
    const float spacing = 4.0f;
    const ImU32 dot = IM_COL32(30, 74, 30, 90);
    for (float y = 0.0f; y < screenSize.y; y += spacing) {
        for (float x = 0.0f; x < screenSize.x; x += spacing) {
            drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + 1.0f, y + 1.0f), dot);
        }
    }

    // A slow band drifting down the panel, like an LCD refresh
    const float bandY = scanPhase * screenSize.y;
    drawList->AddRectFilledMultiColor(
        ImVec2(0.0f, bandY - 60.0f), ImVec2(screenSize.x, bandY + 60.0f),
        IM_COL32(139, 172, 15, 0), IM_COL32(139, 172, 15, 0),
        IM_COL32(139, 172, 15, 14), IM_COL32(139, 172, 15, 14));
}

// Daylight keeps the background almost plain - a light theme earns its
// keep by being calm, and an animated effect would undo that. Just a very
// soft vertical wash so the window does not read as flat paper.
inline void DrawDaylightBackground(ImDrawList* drawList, ImVec2 screenSize, float /*deltaTime*/) {
    drawList->AddRectFilledMultiColor(
        ImVec2(0, 0), screenSize,
        IM_COL32(242, 244, 249, 255), IM_COL32(242, 244, 249, 255),
        IM_COL32(228, 231, 240, 255), IM_COL32(228, 231, 240, 255));
}

inline void DrawThemeBackground(Theme theme, float deltaTime) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;

    switch (theme) {
        case Theme::Matrix:
            DrawMatrixRain(drawList, screenSize, deltaTime);
            break;
        case Theme::Synthwave:
            DrawSynthwaveChasers(drawList, screenSize, deltaTime);
            break;
        case Theme::Cyberpunk:
            DrawCyberpunkGlitch(drawList, screenSize, deltaTime);
            break;
        case Theme::FrutigerAero:
            DrawFrutigerAero(drawList, screenSize, deltaTime);
            break;
        case Theme::Minimal:
            DrawMinimalTheme(drawList, screenSize, deltaTime);
            break;
        case Theme::Vaporwave:
            DrawVaporwaveTheme(drawList, screenSize, deltaTime);
            break;
        case Theme::RetroTerminal:
            DrawRetroTerminal(drawList, screenSize, deltaTime);
            break;
        case Theme::GameBoy:
            DrawGameBoyDotMatrix(drawList, screenSize, deltaTime);
            break;
        case Theme::Daylight:
            DrawDaylightBackground(drawList, screenSize, deltaTime);
            break;
        case Theme::Stock:
        default:
            // No background effect for stock theme
            break;
    }
}

// ============================================================================
// Transport Bar
// ============================================================================
inline void DrawTransportBar(Sequencer& seq, Project& project, PlaybackState& state, UIState& ui) {
    // Set initial window position on first use (top-left)
    ImGui::SetNextWindowPos(ImVec2(10, 35), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 90), ImGuiCond_FirstUseEver);
    ImGui::Begin("Transport", nullptr, ImGuiWindowFlags_NoCollapse);

    // Row 1: Playback controls
    if (ImGui::Button(state.isPlaying ? "PAUSE" : "PLAY", ImVec2(60, 30))) {
        if (state.isPlaying) seq.pause();
        else seq.play();
    }
    ImGui::SameLine();

    if (ImGui::Button("STOP", ImVec2(60, 30))) {
        seq.stop();
    }
    ImGui::SameLine();

    bool loopEnabled = state.loop;
    if (ImGui::Checkbox("Loop", &loopEnabled)) {
        seq.setLoopEnabled(loopEnabled);
    }
    ImGui::SameLine();

    ImGui::SetNextItemWidth(80);
    if (ImGui::DragFloat("BPM", &project.bpm, 1.0f, 30.0f, 300.0f, "%.0f")) {
        seq.setBPM(project.bpm);

        // Auto-adjust drum durations based on new BPM
        // Formula: duration_beats = decay_seconds * (BPM / 60)
        for (Pattern& pat : project.patterns) {
            for (Note& note : pat.notes) {
                if (!isDrumType(note.oscillatorType)) continue;
                float decayTime = getDrumDecayTime(note.oscillatorType);
                note.duration = decayTime * (project.bpm / 60.0f);
            }
        }
    }

    // Row 2: Position and Master Volume
    int measure = static_cast<int>(state.currentBeat / project.beatsPerMeasure) + 1;
    int beatNum = static_cast<int>(std::fmod(state.currentBeat, static_cast<float>(project.beatsPerMeasure))) + 1;
    ImGui::Text("Position: %d.%d", measure, beatNum);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(200);
    float pos = state.currentBeat;
    if (ImGui::SliderFloat("##pos", &pos, 0.0f, project.songLength, "Beat %.1f")) {
        seq.setPosition(pos);
    }

    // Row 3: Master Volume (prominent) - display as 0-100%
    ImGui::Text("Master Volume:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    float masterPct = project.masterVolume * 100.0f;
    if (ImGui::SliderFloat("##master", &masterPct, 0.0f, 100.0f, "%.0f%%")) {
        project.masterVolume = masterPct / 100.0f;
    }


    // Update preview pattern for playback
    seq.setPreviewPattern(ui.selectedPattern, ui.selectedChannel);

    ImGui::End();
}

// ============================================================================
// Master Bus
//
// All of this used to sit inside the Transport window. Transport is docked
// into a 16% strip across the top of the workspace, so the limiter, the
// compressor, the EQ, the loudness presets and the groove controls were all
// clipped below the fold - reachable only if you thought to drag the window
// bigger. Transport now keeps what genuinely fits in a strip, and everything
// that needs room to be used has a panel of its own.
// ============================================================================
inline void DrawMasterBus(Sequencer& seq, Project& project, UIState& ui) {
    (void)ui;
    ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 540), ImGuiCond_FirstUseEver);
    ImGui::Begin("Master Bus", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::CollapsingHeader("Master Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Loudness Presets
        ImGui::Text("Target Platform:");
        ImGui::SameLine();
        const char* presets[] = { "Off", "Spotify", "Apple Music", "YouTube", "SoundCloud", "CD Master" };
        static int currentPreset = 0;
        if (ImGui::Combo("##masterPreset", &currentPreset, presets, 6)) {
            MasterPresets::applyLoudnessPreset(seq.getMasterEffects(), presets[currentPreset]);
            project.masterEQEnabled = seq.getMasterEffects().eqEnabled;
            project.masterCompressorEnabled = seq.getMasterEffects().compressorEnabled;
            project.masterLimiterEnabled = seq.getMasterEffects().limiterEnabled;
            project.masterCompThreshold = seq.getMasterEffects().compressor.threshold;
            project.masterCompRatio = seq.getMasterEffects().compressor.ratio;
            project.masterCompMakeup = seq.getMasterEffects().compressor.makeupGain;
            project.masterLimiterCeiling = seq.getMasterEffects().limiter.ceiling;
            seq.updateMasterEffects();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-configure mastering for streaming platforms");

        ImGui::Spacing();

    // ------------------------------------------------------------------
    // Aux buses
    //
    // Here rather than in a new dockable window on purpose: a new panel
    // needs a Layout.h slot AND a row in AdoptOrphanedWindows, and
    // forgetting the second is what left this very panel floating over the
    // left column. Routing is mixing.
    // ------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Aux Buses")) {
        ImGui::TextDisabled("Channels send here; buses feed the master.");

        for (int bus = 0; bus < Project::MAX_AUX_BUSES; ++bus) {
            AuxBusConfig& busConfig = project.auxBuses[static_cast<size_t>(bus)];
            ImGui::PushID(2000 + bus);

            char nameBuffer[64];
            snprintf(nameBuffer, sizeof(nameBuffer), "%s", busConfig.name.c_str());
            ImGui::SetNextItemWidth(100);
            if (ImGui::InputText("##busname", nameBuffer, sizeof(nameBuffer))) {
                busConfig.name = nameBuffer;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::SliderFloat("##busvol", &busConfig.volume, 0.0f, 1.5f, "%.2f");

            ImGui::SameLine();
            bool busMuted = busConfig.muted;
            if (ImGui::Checkbox("M", &busMuted)) busConfig.muted = busMuted;

            ImGui::SameLine();
            ImGui::SetNextItemWidth(95);
            const char* outLabel = (busConfig.output < 0)
                ? "master"
                : project.auxBuses[static_cast<size_t>(busConfig.output)].name.c_str();
            if (ImGui::BeginCombo("##busout", outLabel)) {
                if (ImGui::Selectable("master", busConfig.output < 0)) {
                    busConfig.output = ROUTE_TO_MASTER;
                }
                for (int target = 0; target < Project::MAX_AUX_BUSES; ++target) {
                    if (target == bus) continue;

                    // Refused here, so an impossible routing is never
                    // written down at all.
                    const bool loops = wouldCreateCycle(project.auxBuses, bus, target);
                    if (loops) ImGui::BeginDisabled();
                    if (ImGui::Selectable(
                            project.auxBuses[static_cast<size_t>(target)].name.c_str(),
                            busConfig.output == target)) {
                        busConfig.output = target;
                    }
                    if (loops) ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Where this bus goes. Destinations that would\n"
                                  "feed it back into itself are greyed out.");
            }

            ImGui::PopID();
        }
    }

    /*
     * Mid-side EQ.
     *
     * On the master rather than a channel because it needs stereo - the
     * channel chain is mono until the pan - and because it is a mastering
     * tool. Cutting bass from the side alone tightens a mix without thinning
     * it, since bass is nearly always mono in practice and side bass is
     * mostly phase trouble.
     */
    if (ImGui::CollapsingHeader("Mid-Side EQ")) {
        MasterEffects& master = seq.getMasterEffects();

        ImGui::Checkbox("Enable##ms", &master.midSideEnabled);
        if (master.midSideEnabled) {
            ImGui::TextDisabled(
                "Mid is what the channels share; side is what differs.");

            bool changed = false;
            static const char* BAND_NAMES[3] = {"Low", "Mid", "High"};

            ImGui::Text("Mid");
            for (int i = 0; i < 3; ++i) {
                ImGui::PushID(1400 + i);
                ImGui::SetNextItemWidth(150);
                changed |= ImGui::SliderFloat(BAND_NAMES[i],
                                              &master.midSide.mid[static_cast<size_t>(i)].gainDb,
                                              -18.0f, 18.0f, "%.1f dB");
                ImGui::PopID();
            }

            ImGui::Text("Side");
            for (int i = 0; i < 3; ++i) {
                ImGui::PushID(1410 + i);
                ImGui::SetNextItemWidth(150);
                changed |= ImGui::SliderFloat(BAND_NAMES[i],
                                              &master.midSide.side[static_cast<size_t>(i)].gainDb,
                                              -18.0f, 18.0f, "%.1f dB");
                ImGui::PopID();
            }

            // The biquads are only rebuilt when a gain actually moved -
            // recomputing six of them per frame would cost more than the
            // whole master bus.
            if (changed) master.midSide.update();

            if (ImGui::SmallButton("Flat##ms")) {
                for (int i = 0; i < 3; ++i) {
                    master.midSide.mid[static_cast<size_t>(i)].gainDb = 0.0f;
                    master.midSide.side[static_cast<size_t>(i)].gainDb = 0.0f;
                }
                master.midSide.update();
            }
        }
    }

    if (CollapsingHeaderExpandable("Chip Accuracy", g_ExpandChipAccuracy)) {
            /*
              * Region.
              *
              * Above the channel cap because it changes more: every pitch by
              * 7.65%, the envelope rate, and the noise period table - which
              * is a different table rather than a scaled one, and crosses
              * over at index 2 where PAL is brighter.
              */
            {
                int region = static_cast<int>(project.chipRegion);
                ImGui::SetNextItemWidth(140);
                if (ImGui::Combo("Region", &region, "NTSC\0PAL\0")) {
                    project.chipRegion = (region == 1) ? ChipRegion::PAL
                                                       : ChipRegion::NTSC;
                    seq.updateChannelConfigs();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Which machine the chip behaves like.\n\n"
                        "The CPU clock differs by 7.65%%, so every pitch does.\n"
                        "The envelope rate differs (240 Hz against 200), and\n"
                        "neither is the 60 Hz people assume.\n\n"
                        "And the noise period table is a DIFFERENT table, not\n"
                        "a scaled one: at setting 2 the PAL period is shorter,\n"
                        "so PAL noise is brighter there and darker everywhere\n"
                        "else.");
                }
            }

            ImGui::Checkbox("8 channels only", &project.chipAuthentic);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Hold the project to the eight channels a 2A03 had.\n\n"
                    "Enforced in the audio engine, not just the interface -\n"
                    "channels beyond the eighth are not mixed at all, so this\n"
                    "is a real constraint rather than a reminder.");
            }

            ImGui::Checkbox("Non-linear mixing", &project.chipMixEnabled);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "A 2A03 shares one DAC between its two pulse channels and\n"
                    "another between triangle and noise, so channels duck each\n"
                    "other and the triangle sits louder against the pulses.\n\n"
                    "Two pulses at full volume become 1.73x one pulse, not 2x.\n"
                    "Only Pulse, Triangle and Noise channels are affected;\n"
                    "anything else mixes linearly as before.");
            }

            ImGui::Checkbox("Output filters", &project.chipFilterEnabled);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The console's own output filtering.\n\n"
                    "NES: 90 Hz and 440 Hz high-pass, 14 kHz low-pass. The\n"
                    "440 Hz stage is the one emulations usually leave out,\n"
                    "which is why they sound bass-heavy.\n"
                    "Famicom: a single 37 Hz high-pass and no low-pass -\n"
                    "brighter and fuller, which is much of why the two\n"
                    "machines sound different playing the same music.");
            }

            if (project.chipFilterEnabled) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110);
                int voicing = project.chipFilterFamicom ? 1 : 0;
                if (ImGui::Combo("##chipvoicing", &voicing, "NES\0Famicom\0")) {
                    project.chipFilterFamicom = (voicing == 1);
                }
            }

            /*
             * ---- The verdict ------------------------------------------------
             *
             * What everything in this section adds up to, in one line. The
             * research finding it answers is precise: the scene does not
             * object to modern production, it objects to not being told. So
             * it says which rung the project is on and exactly why, and
             * never suggests changing anything - there is usually nothing
             * wrong.
             *
             * ABOVE the per-channel list rather than below it, which is not
             * only the usual summary-then-detail order: eight channel rows
             * push anything under them past the bottom of the docked panel,
             * and a verdict you have to scroll to find is a verdict for a
             * feature whose entire purpose is being seen.
             */
            ImGui::Spacing();
            ImGui::SeparatorText("Would this run on the hardware?");
            {
                const ChipLegalityReport legality = auditChipLegality(project);

                // Not a red-to-green scale. Four distinguishable colours that
                // do not read as a score, because the rungs are claims about
                // hardware rather than marks out of four.
                ImVec4 tierColour;
                switch (legality.tier) {
                    case ChipTier::HardwareLegal: tierColour = ImVec4(0.45f, 0.95f, 0.65f, 1.0f); break;
                    case ChipTier::Expansion:     tierColour = ImVec4(0.55f, 0.80f, 1.00f, 1.0f); break;
                    case ChipTier::ChipFlavoured: tierColour = ImVec4(0.85f, 0.75f, 1.00f, 1.0f); break;
                    default:                      tierColour = ImVec4(1.00f, 0.80f, 0.55f, 1.0f); break;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, tierColour);
                ImGui::TextUnformatted(chipTierName(legality.tier));
                ImGui::PopStyleColor();
                ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
                ImGui::TextDisabled("%s", chipTierDescription(legality.tier));
                ImGui::PopTextWrapPos();

                if (!legality.notes.empty() &&
                    ImGui::TreeNode("chiplegalwhy", "Why (%d)",
                                    static_cast<int>(legality.notes.size()))) {
                    for (const ChipLegalityNote& note : legality.notes) {
                        ImGui::Bullet();
                        ImGui::TextWrapped("%s", note.what.c_str());
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\n\nCaps at: %s", note.why.c_str(),
                                              chipTierName(note.caps));
                        }
                    }
                    ImGui::TreePop();
                }
            }

            /*
             * ---- Holding a channel to a chip -------------------------------
             *
             * Everything above this point is tone: how the finished mix is
             * coloured. This is different in kind - it is a constraint on
             * what you can write, applied while you write it, so a note the
             * hardware could not play comes out the way the hardware would
             * have played it.
             *
             * Per channel, and deliberately separate from the eight-channel
             * cap, because that flag is already on in saved projects and
             * quantising their pitch would change how they sound.
             */
            ImGui::Spacing();
            ImGui::SeparatorText("Hold channels to a chip");

            if (ImGui::SmallButton("NES layout")) {
                // Two pulses, a triangle and a noise channel, which is
                // literally what the machine had.
                const ChipVoice layout[4] = {
                    ChipVoice::NESPulse, ChipVoice::NESPulse,
                    ChipVoice::NESTriangle, ChipVoice::NESNoise
                };
                for (int i = 0; i < 4 && i < Project::MAX_CHANNELS; ++i) {
                    project.channels[static_cast<size_t>(i)].chipVoice = layout[i];
                }
                seq.updateChannelConfigs();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Game Boy layout")) {
                const ChipVoice layout[4] = {
                    ChipVoice::GameBoyPulse, ChipVoice::GameBoyPulse,
                    ChipVoice::GameBoyWave, ChipVoice::NESNoise
                };
                for (int i = 0; i < 4 && i < Project::MAX_CHANNELS; ++i) {
                    project.channels[static_cast<size_t>(i)].chipVoice = layout[i];
                }
                seq.updateChannelConfigs();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##chipvoices")) {
                for (auto& channel : project.channels) {
                    channel.chipVoice = ChipVoice::None;
                }
                seq.updateChannelConfigs();
            }

            const char* VOICE_ITEMS =
                "Unconstrained\0NES pulse\0NES triangle\0NES noise\0"
                "Game Boy pulse\0Game Boy wave\0";

            const int shown = std::min(project.activeChannelCount(),
                                       Project::MAX_CHANNELS);
            for (int ch = 0; ch < shown; ++ch) {
                ChannelConfig& channel = project.channels[static_cast<size_t>(ch)];

                // Only the constrained ones and the first eight, so a
                // 32-channel project is not thirty-two combo boxes of
                // "Unconstrained".
                if (ch >= 8 && channel.chipVoice == ChipVoice::None) continue;

                ImGui::PushID(ch);
                ImGui::SetNextItemWidth(150);
                int voice = static_cast<int>(channel.chipVoice);
                const std::string label =
                    std::to_string(ch + 1) + ": " + channel.name;
                if (ImGui::Combo(label.c_str(), &voice, VOICE_ITEMS)) {
                    if (voice >= 0 && voice < CHIP_VOICE_COUNT) {
                        channel.chipVoice = static_cast<ChipVoice>(voice);
                        seq.updateChannelConfigs();
                    }
                }

                // The range, right there, because the whole reason to set
                // this is to know where the edges are.
                if (channel.chipVoice != ChipVoice::None &&
                    chipVoiceConstrainsPitch(channel.chipVoice)) {
                    ImGui::SameLine();
                    ImGui::TextDisabled(
                        "MIDI %d-%d",
                        chipLowestNote(channel.chipVoice, project.chipRegion),
                        chipHighestUsableNote(channel.chipVoice, project.chipRegion));
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "The notes this channel can actually play in tune.\n\n"
                            "Below the bottom the period register saturates, so\n"
                            "the note sounds in the wrong octave rather than not\n"
                            "at all. Above the top the register's steps are\n"
                            "further apart than the notes are, so there is no\n"
                            "chromatic scale left - which is what a chiptune lead\n"
                            "in its top octave actually is.");
                    }
                }
                ImGui::PopID();
            }

            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Play this channel through the chip's real registers.\n\n"
                    "Pitch is quantised to the period register AFTER vibrato,\n"
                    "slides, arpeggios and macros - because on the machine all\n"
                    "of those were the same eleven bits. So a vibrato is smooth\n"
                    "low down and steps in the top octave, exactly as it did.\n\n"
                    "Volume goes through the volume register too, and the three\n"
                    "chips disagree: 16 levels on a NES pulse, FOUR CODES on a\n"
                    "Game Boy wave channel, and NONE AT ALL on a NES triangle,\n"
                    "which is on or off and clicks on the way out.\n\n"
                    "Off everywhere by default. Turning it on changes how the\n"
                    "channel sounds, which is the entire point.");
            }
        }

        ImGui::Spacing();

        // Master Limiter (most important - prevent clipping)
        if (ImGui::Checkbox("Limiter", &project.masterLimiterEnabled)) {
            seq.updateMasterEffects();
        }
        if (project.masterLimiterEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (ImGui::SliderFloat("##limCeil", &project.masterLimiterCeiling, -1.0f, -0.1f, "%.1f dB")) {
                seq.updateMasterEffects();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ceiling: prevents signal from exceeding this level");
        }

        // Master Compressor (glue compression)
        if (ImGui::Checkbox("Compressor", &project.masterCompressorEnabled)) {
            seq.updateMasterEffects();
        }
        if (project.masterCompressorEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            if (ImGui::SliderFloat("##compThr", &project.masterCompThreshold, -24.0f, 0.0f, "%.0f dB")) {
                seq.updateMasterEffects();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Threshold: compress signals above this level");

            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            if (ImGui::SliderFloat("##compRat", &project.masterCompRatio, 1.0f, 8.0f, "%.1f:1")) {
                seq.updateMasterEffects();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ratio: compression amount");
        }

        // Master EQ (tonal balance)
        if (ImGui::Checkbox("EQ", &project.masterEQEnabled)) {
            seq.updateMasterEffects();
        }
        if (project.masterEQEnabled) {
            ImGui::Text("  Low:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            if (ImGui::SliderFloat("##eqLow", &project.masterEQLowGain, -12.0f, 12.0f, "%.1f dB")) {
                seq.updateMasterEffects();
            }

            ImGui::SameLine();
            ImGui::Text("Mid:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            if (ImGui::SliderFloat("##eqMid", &project.masterEQMidGain, -12.0f, 12.0f, "%.1f dB")) {
                seq.updateMasterEffects();
            }

            ImGui::SameLine();
            ImGui::Text("High:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            if (ImGui::SliderFloat("##eqHigh", &project.masterEQHighGain, -12.0f, 12.0f, "%.1f dB")) {
                seq.updateMasterEffects();
            }
        }

        // LUFS Meter (loudness monitoring)
        float lufs = seq.getMasterEffects().getLUFS();
        ImGui::Text("Loudness: %.1f LUFS", lufs);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("LUFS (Loudness Units Full Scale)\n"
                              "Streaming targets:\n"
                              "  Spotify: -14 LUFS\n"
                              "  Apple Music: -16 LUFS\n"
                              "  YouTube: -13 LUFS\n"
                              "  SoundCloud: -11 LUFS");
        }
    }

    if (ImGui::CollapsingHeader("Groove", ImGuiTreeNodeFlags_DefaultOpen)) {
        /*
         * ------------------------------------------------------------------
         * Per-row timing
         *
         * The swing slider below is one number applied to every off-beat,
         * which can express "7 5" and nothing else. This is the list, so a
         * shuffle that limps only on the first half of the beat - or
         * resolves once a bar rather than once a beat - is writable.
         *
         * A text box rather than a row of spin boxes because a groove is a
         * short list people already write down that way, copy from a forum
         * post, and compare at a glance.
         * ------------------------------------------------------------------
         */
        {
            static char grooveText[96] = "";
            static bool grooveLoaded = false;

            // Reloaded from the project whenever it disagrees, so undo,
            // loading a file and the preset buttons all show up here.
            const std::string current = grooveToText(project.groove);
            if (!grooveLoaded || current != std::string(grooveText)) {
                std::snprintf(grooveText, sizeof(grooveText), "%s",
                              current.c_str());
                grooveLoaded = true;
            }

            ImGui::TextDisabled("Ticks per row (a repeating list):");
            ImGui::SetNextItemWidth(200);
            if (ImGui::InputText("##groove", grooveText, sizeof(grooveText))) {
                g_UndoHistory.saveState(project, "Groove");
                GroovePattern parsed;
                parsed.clear();
                parsed.rowsPerBeat = project.groove.rowsPerBeat;
                grooveFromText(grooveText, parsed);
                project.groove = parsed;
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How long each row lasts, repeating.\n\n"
                    "  6 6        straight\n"
                    "  7 5        swing\n"
                    "  7 5 6 6    swung on the first half of the beat only\n\n"
                    "Only the ratios matter - \"7 5\" and \"14 10\" are the\n"
                    "same groove. A full cycle always covers the same amount\n"
                    "of time as the same number of straight rows, so a groove\n"
                    "never changes the tempo.\n\n"
                    "Empty means straight timing.");
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            int rowsPerBeat = project.groove.rowsPerBeat;
            if (ImGui::SliderInt("per beat", &rowsPerBeat, 1, 16)) {
                project.groove.rowsPerBeat = std::clamp(rowsPerBeat, 1, 16);
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How many rows the list covers in one beat.\n"
                                  "Four is one entry per sixteenth note.");
            }

            for (int i = 0; i < GROOVE_TIMING_PRESET_COUNT; ++i) {
                if (i % 3 != 0) ImGui::SameLine();
                if (ImGui::SmallButton(GROOVE_TIMING_PRESETS[i].name)) {
                    g_UndoHistory.saveState(project, "Groove Preset");
                    GroovePattern parsed;
                    parsed.clear();
                    parsed.rowsPerBeat = project.groove.rowsPerBeat;
                    grooveFromText(GROOVE_TIMING_PRESETS[i].speeds, parsed);
                    project.groove = parsed;
                    grooveLoaded = false;
                    seq.updateChannelConfigs();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n\n%s",
                                      GROOVE_TIMING_PRESETS[i].speeds,
                                      GROOVE_TIMING_PRESETS[i].description);
                }
            }

            // Said here rather than left to be discovered: the slider below
            // stops doing anything while a groove is set.
            if (project.groove.active()) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                                   "A groove is set, so the swing slider "
                                   "below is ignored.");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "A groove has already decided where every row falls.\n"
                        "Swinging it again would swing an already swung row,\n"
                        "and then neither control would mean what it says.");
                }
            }

            ImGui::Separator();
        }

        // ------------------------------------------------------------------
        // Feel presets
        //
        // The research finding these answer: nobody asks how to program a
        // beat - they ask why theirs feels mechanical and cannot pinpoint
        // what everyone else has. These are named answers. Each only sets
        // the sliders below, so a preset is a starting point, not a mode.
        // ------------------------------------------------------------------
        {
            int presetCount = 0;
            const GroovePreset* presets = groovePresets(presetCount);
            const int active = matchGroovePreset(project);

            for (int i = 0; i < presetCount; ++i) {
                if (i % 3 != 0) ImGui::SameLine();

                const bool isActive = (i == active);
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }
                if (ImGui::Button(presets[i].name, ImVec2(92, 24))) {
                    g_UndoHistory.saveState(project, "Groove Preset");
                    applyGroovePreset(project, presets[i]);
                }
                if (isActive) ImGui::PopStyleColor();

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", presets[i].description);
                }
            }
            if (active < 0) {
                ImGui::TextDisabled("(custom - the sliders below are yours)");
            }
        }
        ImGui::Separator();

        // Row 4: Swing/Groove settings
        ImGui::Text("Swing:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        float swingPct = project.swing * 100.0f;
        if (ImGui::SliderFloat("##swing", &swingPct, 0.0f, 100.0f, "%.0f%%")) {
            project.swing = swingPct / 100.0f;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shifts off-beat notes forward for groove feel");

        ImGui::SameLine();
        ImGui::Text("Grid:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        const char* gridItems[] = { "1/8", "1/16", "1/32" };
        float gridValues[] = { 0.5f, 0.25f, 0.125f };
        int gridIdx = 0;
        if (project.swingGrid <= 0.125f) gridIdx = 2;
        else if (project.swingGrid <= 0.25f) gridIdx = 1;
        else gridIdx = 0;
        if (ImGui::Combo("##swingGrid", &gridIdx, gridItems, 3)) {
            project.swingGrid = gridValues[gridIdx];
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Note grid for swing (8th, 16th, 32nd)");

        ImGui::SameLine();
        ImGui::Checkbox("Humanize", &project.humanize);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add random timing/velocity variation");

        if (project.humanize) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            float timePct = project.humanizeAmount * 1000.0f;  // Convert to ms
            if (ImGui::SliderFloat("##humTime", &timePct, 0.0f, 50.0f, "%.0fms")) {
                project.humanizeAmount = timePct / 1000.0f;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Timing variation amount");

            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            float velPct = project.humanizeVelocity * 100.0f;
            if (ImGui::SliderFloat("##humVel", &velPct, 0.0f, 30.0f, "%.0f%%")) {
                project.humanizeVelocity = velPct / 100.0f;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Velocity variation amount");
        }
    }

    ImGui::End();
}


/*
 * Every oscillator type's short name, in enum order.
 *
 * File scope rather than local to the Sound Palette because the Browser
 * lists the same instruments: a second copy of this table would have
 * drifted the first time an engine was added to one and not the other.
 *
 * It stays a C array so the two static_asserts below it keep working. They
 * are the reason inserting FMSynth mid-enum lasted one compile instead of
 * reaching a user as every instrument past Custom playing the wrong sound.
 */
inline constexpr const char* OSC_NAMES[] = {
        // Oscillators (7)
        "Pulse", "Triangle", "Sawtooth", "Sine", "Noise", "Supersaw", "Custom",
        // Synths (10)
        "Lead", "Pad", "Bass", "Pluck", "Arp", "Organ", "Strings", "Brass", "Chip", "Bell",
        // Synthwave (6)
        "SW Lead", "SW Bass", "SW Pad", "SW Arp", "SW Chord", "SW FM",
        // Techno (5)
        "Acid", "Stab", "Hoover", "Rave", "Reese",
        // Hip Hop (4)
        "Sub808", "LoFi", "Vinyl", "Trap",
        // Additional (3)
        "Gated", "Poly", "Sync",
        // Drums (21) - indices 35-55
        "Kick", "Kick808", "KickHard", "KickSoft",
        "Snare", "Snare808", "SnareRim", "Clap",
        "HiHat", "HiHatOpen", "HiHatPedal",
        "Tom", "TomLow", "TomHigh",
        "Crash", "Ride",
        "Cowbell", "Clave", "Conga", "Maracas", "Tambourine",
        // Reggaeton Instruments (7) - indices 56-62
        "Reggae Bass", "Latin Brass", "Guira", "Bongo", "Timbale", "Dembow808", "DembowSnare",
        // High-Accuracy Recreations (2) - indices 63-64
        "Vocoder", "Kavinsky Bass",
        // Six-operator FM - index 65
        "FM",
        // Multisample - index 66
        "Sampler",
        // Granular and the analogue drum model - indices 67-68
        "Granular", "Drum Model"
};

inline constexpr int OSC_NAME_COUNT =
    static_cast<int>(sizeof(OSC_NAMES) / sizeof(OSC_NAMES[0]));

static_assert(static_cast<int>(OscillatorType::DrumModel) + 1 == OSC_NAME_COUNT,
              "the last OscillatorType must be the last OSC_NAMES entry");

inline const char* oscillatorTypeName(OscillatorType type) {
    const int index = static_cast<int>(type);
    if (index < 0 || index >= OSC_NAME_COUNT) return "?";
    return OSC_NAMES[index];
}

/*
 * Declared here, defined with the Plugins panel further down.
 *
 * The File commands need it - loading a project has to rebuild the running
 * plugin instances, or the new project's racks are drawn from its data
 * while the old project's plugins are still the ones making sound.
 */
inline void ApplyPluginChanges(Project& project, Sequencer& seq);

/*
 * Load a showcase over the current project.
 *
 * Through the undo history like any other edit, which is what makes trying
 * one safe: somebody curious about the FM engine should not have to think
 * about whether they are about to lose what they were working on.
 */
inline void LoadShowcase(Project& project, UIState& ui, Sequencer& seq,
                         Showcase which) {
    g_UndoHistory.saveState(project, "Load Demo");

    project = makeShowcase(which);
    clampProjectToValidRanges(project);

    ui.selectedPattern = 0;
    ui.selectedChannel = 0;
    ui.selectedNoteIndex = -1;
    ui.selectedNoteIndices.clear();

    // Not the path of whatever was open before, or saving would write the
    // demo over the user's own file.
    ui.projectFilePath.clear();

    seq.setPosition(0.0f);
    seq.updateChannelConfigs();
}

// ============================================================================
// Commands
//
// Every global command in one table. What used to be a shortcut buried in
// the frame loop is now a row with a name, so it can be searched for, shown
// in a list, and rebound.
// ============================================================================

static ActionRegistry g_Actions;

inline ActionRegistry& actionRegistry() { return g_Actions; }

struct CommandPaletteState {
    bool open = false;
    bool justOpened = false;
    char query[128] = {};
    int selected = 0;

    // What ran last, so the palette can confirm it. A command that succeeds
    // silently and closes the window looks identical to one that did
    // nothing at all.
    std::string lastRan;
};

static CommandPaletteState g_Palette;

inline CommandPaletteState& commandPaletteState() { return g_Palette; }

inline void OpenCommandPalette() {
    g_Palette.open = true;
    g_Palette.justOpened = true;
    g_Palette.query[0] = '\0';
    g_Palette.selected = 0;
}

/*
 * Build the table.
 *
 * Idempotent - add() replaces by id - so calling it twice cannot produce
 * duplicate rows, which matters because a test builds its own registry.
 *
 * The defaults deliberately match the keys the program already answered to,
 * so nobody's fingers have to change. Two of them were previously promised
 * and not implemented: the Save button's tooltip has always said Ctrl+S,
 * and nothing anywhere handled it.
 */
inline void registerDefaultActions(ActionRegistry& registry) {
    auto key = [](ImGuiKey k, bool ctrl = false, bool shift = false,
                  bool alt = false) {
        Shortcut shortcut;
        shortcut.key = k;
        shortcut.ctrl = ctrl;
        shortcut.shift = shift;
        shortcut.alt = alt;
        return shortcut;
    };

    auto add = [&](const char* id, const char* label, const char* category,
                   const char* hint, Shortcut shortcut,
                   std::function<void(ActionContext&)> run,
                   bool opensDialog = false) {
        Action action;
        action.id = id;
        action.label = label;
        action.category = category;
        action.hint = hint;
        action.defaultShortcut = shortcut;
        action.opensDialog = opensDialog;
        action.run = std::move(run);
        registry.add(std::move(action));
    };

    // ---- Transport ----------------------------------------------------------
    add("transport.playpause", "Play / Pause", "Transport",
        "Start or pause playback", key(ImGuiKey_Space),
        [](ActionContext& c) {
            if (c.seq->isPlaying()) c.seq->pause();
            else c.seq->play();
        });

    add("transport.stop", "Stop", "Transport",
        "Stop and return to the start", key(ImGuiKey_Escape, false, true),
        [](ActionContext& c) { c.seq->stop(); });

    add("transport.rewind", "Rewind to Start", "Transport",
        "Move the playhead to the beginning", key(ImGuiKey_Home),
        [](ActionContext& c) { c.seq->setPosition(0.0f); });

    // ---- File ---------------------------------------------------------------
    add("file.new", "New Project", "File",
        "Start again from an empty project", key(ImGuiKey_N, true),
        [](ActionContext& c) {
            *c.project = Project();
            c.ui->selectedPattern = 0;
            c.ui->selectedNoteIndex = -1;
            c.ui->selectedNoteIndices.clear();
            c.ui->projectFilePath.clear();
            g_UndoHistory.clear();
            c.seq->updateChannelConfigs();
            ApplyPluginChanges(*c.project, *c.seq);
        });

    add("file.save", "Save Project", "File",
        "Save, asking for a name the first time", key(ImGuiKey_S, true),
        [](ActionContext& c) {
            std::string path = c.ui->projectFilePath;
            if (path.empty()) {
                path = saveFileDialog("Chiptune Projects (*.ctp)\0*.ctp\0", "ctp");
                if (path.empty()) return;
                c.ui->projectFilePath = path;
            }
            saveProject(*c.project, path);
        },
        /*opensDialog=*/true);

    add("file.saveas", "Save Project As...", "File",
        "Save under a new name", key(ImGuiKey_S, true, true),
        [](ActionContext& c) {
            const std::string path =
                saveFileDialog("Chiptune Projects (*.ctp)\0*.ctp\0", "ctp");
            if (path.empty()) return;
            c.ui->projectFilePath = path;
            saveProject(*c.project, path);
        },
        /*opensDialog=*/true);

    add("file.open", "Open Project...", "File",
        "Load a .ctp project", key(ImGuiKey_O, true),
        [](ActionContext& c) {
            const std::string path = openFileDialog(
                "Chiptune Projects (*.ctp)\0*.ctp\0All Files (*.*)\0*.*\0", "ctp");
            if (path.empty()) return;
            if (!loadProject(*c.project, path)) return;
            c.ui->projectFilePath = path;
            c.ui->selectedPattern = 0;
            c.ui->selectedNoteIndex = -1;
            c.ui->selectedNoteIndices.clear();
            g_UndoHistory.clear();
            c.seq->updateChannelConfigs();
            ApplyPluginChanges(*c.project, *c.seq);
        },
        /*opensDialog=*/true);

    // ---- Edit ---------------------------------------------------------------
    add("edit.undo", "Undo", "Edit", "Step back through your changes",
        key(ImGuiKey_Z, true), [](ActionContext&) { RequestUndo(); });

    add("edit.redo", "Redo", "Edit", "Step forward again",
        key(ImGuiKey_Y, true), [](ActionContext&) { RequestRedo(); });

    // ---- Views --------------------------------------------------------------
    struct ViewEntry { const char* id; const char* label; ViewMode mode; ImGuiKey key; };
    static const ViewEntry VIEWS[] = {
        {"view.pianoroll",   "Piano Roll",     ViewMode::PianoRoll,    ImGuiKey_F1},
        {"view.tracker",     "Tracker",        ViewMode::Tracker,      ImGuiKey_F2},
        {"view.arrangement", "Arrangement",    ViewMode::Arrangement,  ImGuiKey_F3},
        {"view.mixer",       "Mixer",          ViewMode::Mixer,        ImGuiKey_F5},
        {"view.pad",         "Pad Controller", ViewMode::PadController, ImGuiKey_F6},
        {"view.voice",       "Voice",          ViewMode::Voice,         ImGuiKey_F11},
    };
    for (const ViewEntry& entry : VIEWS) {
        const ViewMode mode = entry.mode;
        add(entry.id, entry.label, "View", "Switch the main view",
            key(entry.key), [mode](ActionContext& c) { c.ui->currentView = mode; });
    }

    // ---- Panels --------------------------------------------------------------
    struct PanelEntry { const char* id; const char* label; bool UIState::* flag; ImGuiKey key; };
    static const PanelEntry PANELS[] = {
        {"panel.macros",    "Macro Editor",      &UIState::showMacroEditor,      ImGuiKey_F4},
        {"panel.spectrum",  "Spectrum Analyzer", &UIState::showSpectrumAnalyzer, ImGuiKey_F7},
        {"panel.midi",      "MIDI Input",        &UIState::showMIDIInput,        ImGuiKey_F8},
        {"panel.automation","Automation",        &UIState::showAutomation,       ImGuiKey_F9},
        {"panel.wavetable", "Wavetable Editor",  &UIState::showWavetableEditor,  ImGuiKey_F10},
    };
    for (const PanelEntry& entry : PANELS) {
        bool UIState::* flag = entry.flag;
        add(entry.id, (std::string("Toggle ") + entry.label).c_str(), "Panels",
            "Show or hide the panel", key(entry.key),
            [flag](ActionContext& c) { c.ui->*flag = !(c.ui->*flag); });
    }

    // ---- Editing modes --------------------------------------------------------
    struct ModeEntry { const char* id; const char* label; PianoRollMode mode; };
    static const ModeEntry MODES[] = {
        {"mode.draw",   "Draw Mode",   PianoRollMode::Draw},
        {"mode.select", "Select Mode", PianoRollMode::Select},
        {"mode.erase",  "Erase Mode",  PianoRollMode::Erase},
    };
    for (const ModeEntry& entry : MODES) {
        const PianoRollMode mode = entry.mode;
        // No default key: D, S and E are already handled by the piano roll
        // itself, where they are only live when it has focus. Binding them
        // globally as well would fire both.
        add(entry.id, entry.label, "Edit", "Change what clicking does",
            Shortcut(), [mode](ActionContext& c) { c.ui->pianoRollMode = mode; });
    }

    // ---- Channels ---------------------------------------------------------------
    add("channel.next", "Next Channel", "Channel", "Select the next channel",
        Shortcut(), [](ActionContext& c) {
            const int count = std::max(1, c.project->activeChannelCount());
            c.ui->selectedChannel = (c.ui->selectedChannel + 1) % count;
        });

    add("channel.previous", "Previous Channel", "Channel",
        "Select the previous channel", Shortcut(), [](ActionContext& c) {
            const int count = std::max(1, c.project->activeChannelCount());
            c.ui->selectedChannel = (c.ui->selectedChannel + count - 1) % count;
        });

    add("channel.mute", "Mute Selected Channel", "Channel",
        "Toggle mute on the selected channel", Shortcut(),
        [](ActionContext& c) {
            const int ch = std::clamp(c.ui->selectedChannel, 0,
                                      Project::MAX_CHANNELS - 1);
            ChannelConfig& channel = c.project->channels[size_t(ch)];
            channel.muted = !channel.muted;
            c.seq->updateChannelConfigs();
        });

    add("channel.solo", "Solo Selected Channel", "Channel",
        "Toggle solo on the selected channel", Shortcut(),
        [](ActionContext& c) {
            const int ch = std::clamp(c.ui->selectedChannel, 0,
                                      Project::MAX_CHANNELS - 1);
            ChannelConfig& channel = c.project->channels[size_t(ch)];
            channel.solo = !channel.solo;
            c.seq->updateChannelConfigs();
        });

    // ---- Workspace ----------------------------------------------------------------
    add("layout.reset", "Reset Window Layout", "Workspace",
        "Put every panel back where it started", key(ImGuiKey_0, true),
        [](ActionContext& c) { c.ui->pendingLayoutFrames = 2; });

    add("view.ghosts", "Toggle Ghost Notes", "View",
        "Show the other channels' notes behind this one", Shortcut(),
        [](ActionContext& c) { c.ui->showGhostNotes = !c.ui->showGhostNotes; });

    // ---- Help -----------------------------------------------------------------------
    add("help.palette", "Command Palette", "Help",
        "Search every command by name", key(ImGuiKey_P, true),
        [](ActionContext&) { OpenCommandPalette(); });

    add("help.shortcuts", "Keyboard Shortcuts", "Help",
        "See and change every key", key(ImGuiKey_Slash, true),
        [](ActionContext& c) { c.ui->showShortcuts = !c.ui->showShortcuts; });

    add("panel.plugins", "Plugins", "Panels",
        "Hosted VST and CLAP plugins on this channel", Shortcut(),
        [](ActionContext& c) { c.ui->showPlugins = !c.ui->showPlugins; });

    add("panel.scopes", "Scopes", "Panels",
        "See each channel's waveform, and the master as an X-Y plot",
        Shortcut(),
        [](ActionContext& c) { c.ui->showScopes = !c.ui->showScopes; });

    add("panel.notefx", "Note FX", "Panels",
        "Chords, arpeggios and strums on this channel, without writing notes",
        Shortcut(),
        [](ActionContext& c) { c.ui->showNoteFX = !c.ui->showNoteFX; });

    // The demos, each reachable by name. A demo nobody can find
    // demonstrates nothing.
    for (int s = 0; s < int(Showcase::Count); ++s) {
        const Showcase which = static_cast<Showcase>(s);
        add((std::string("demo.") + std::to_string(s)).c_str(),
            (std::string("Demo: ") + showcaseName(which)).c_str(),
            "File", showcaseDescription(which), Shortcut(),
            [which](ActionContext& c) {
                LoadShowcase(*c.project, *c.ui, *c.seq, which);
            });
    }

    add("help.browser", "Browser", "Help",
        "Samples, instruments, presets and files", key(ImGuiKey_B, true),
        [](ActionContext& c) { c.ui->showBrowser = !c.ui->showBrowser; });
}

/*
 * Run whatever key was pressed.
 *
 * Called once per frame from the main loop. Returns true if a command ran,
 * so the caller knows the key was spoken for.
 */
inline bool DispatchActions(Project& project, UIState& ui, Sequencer& seq) {
    ActionContext context;
    context.project = &project;
    context.ui = &ui;
    context.seq = &seq;
    return g_Actions.dispatch(context) >= 0;
}

// ============================================================================
// The command palette
// ============================================================================
inline void DrawCommandPalette(Project& project, UIState& ui, Sequencer& seq) {
    CommandPaletteState& state = g_Palette;
    if (!state.open) return;

    ActionContext context;
    context.project = &project;
    context.ui = &ui;
    context.seq = &seq;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 centre(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                        viewport->WorkPos.y + viewport->WorkSize.y * 0.28f);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Always);

    if (!ImGui::Begin("Command Palette", &state.open,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    // The search box takes the keyboard the moment the palette opens, so
    // the shortcut that opened it flows straight into typing.
    if (state.justOpened) {
        ImGui::SetKeyboardFocusHere();
        state.justOpened = false;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##palettequery", "Type a command...",
                                 state.query, sizeof(state.query))) {
        state.selected = 0;   // a new query invalidates the old highlight
    }

    const std::vector<ActionMatch> matches = g_Actions.search(state.query);

    // Arrow keys move the highlight. Handled before the list is drawn so
    // the scroll can follow in the same frame.
    if (!matches.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            state.selected = (state.selected + 1) % int(matches.size());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            state.selected = (state.selected + int(matches.size()) - 1) %
                             int(matches.size());
        }
    }
    state.selected = std::clamp(state.selected, 0,
                                std::max(0, int(matches.size()) - 1));

    bool runNow = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                  ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    int runIndex = matches.empty() ? -1 : state.selected;

    ImGui::Separator();

    if (matches.empty()) {
        ImGui::TextDisabled("Nothing matches \"%s\"", state.query);
    }

    ImGui::BeginChild("##palettelist", ImVec2(0, 320), false);
    for (size_t i = 0; i < matches.size(); ++i) {
        const Action& action = g_Actions.all()[size_t(matches[i].index)];
        const bool selected = (int(i) == state.selected);
        const bool enabled = action.isEnabled(context);

        ImGui::PushID(int(i));

        if (!enabled) ImGui::BeginDisabled();
        if (ImGui::Selectable("##row", selected,
                              ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(0, 34))) {
            state.selected = int(i);
            runNow = true;
            runIndex = int(i);
        }
        if (!enabled) ImGui::EndDisabled();

        // The row is drawn over the selectable rather than inside it, so
        // the label, the category and the key can each have their own
        // colour without three separate widgets fighting for the click.
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddText(ImVec2(rowMin.x + 8, rowMin.y + 4),
                      ImGui::GetColorU32(enabled ? ImGuiCol_Text
                                                 : ImGuiCol_TextDisabled),
                      action.label.c_str());
        draw->AddText(ImVec2(rowMin.x + 8, rowMin.y + 18),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled),
                      (action.category + "  -  " + action.hint).c_str());

        const std::string keys = shortcutText(action.shortcut);
        if (!keys.empty()) {
            const float width = ImGui::CalcTextSize(keys.c_str()).x;
            draw->AddText(ImVec2(rowMax.x - width - 10, rowMin.y + 10),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled), keys.c_str());
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextDisabled("Up/Down to choose, Enter to run, Escape to close");

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) state.open = false;

    // Run last, and outside the list: an action that changes the registry
    // or the project must not do so while the loop above is walking it.
    if (runNow && runIndex >= 0 && runIndex < int(matches.size())) {
        const int index = matches[size_t(runIndex)].index;
        Action& action = g_Actions.all()[size_t(index)];
        if (action.run && action.isEnabled(context)) {
            state.lastRan = action.label;
            state.open = false;
            auto run = action.run;
            run(context);
        }
    }

    ImGui::End();
}

// ============================================================================
// Keyboard shortcuts, and changing them
// ============================================================================
inline void DrawShortcutsPanel(Project& project, UIState& ui, Sequencer& seq) {
    (void)project; (void)seq;
    if (!ui.showShortcuts) return;

    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Keyboard Shortcuts", &ui.showShortcuts)) {
        ImGui::End();
        return;
    }

    // Which row is listening for a key. One at a time, by id rather than by
    // index, so re-sorting or adding a command cannot move the capture onto
    // a different row.
    static std::string s_capturing;

    ImGui::TextWrapped(
        "Every command, and the key it answers to. Click a key to change it, "
        "then press the combination you want. Escape cancels; Backspace "
        "clears it.");

    if (g_Actions.anyRebound()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset all")) {
            g_Actions.resetAllToDefaults();
            g_Actions.saveBindings(keybindingsPath(ui.settingsDirectory));
        }
    }

    ImGui::Separator();

    std::string currentCategory;
    for (Action& action : g_Actions.all()) {
        if (action.category != currentCategory) {
            currentCategory = action.category;
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.75f, 1.0f, 1.0f), "%s",
                               currentCategory.c_str());
            ImGui::Separator();
        }

        ImGui::PushID(action.id.c_str());

        ImGui::TextUnformatted(action.label.c_str());
        if (ImGui::IsItemHovered() && !action.hint.empty()) {
            ImGui::SetTooltip("%s", action.hint.c_str());
        }

        ImGui::SameLine(300.0f);

        const bool capturing = (s_capturing == action.id);
        const std::string keys = shortcutText(action.shortcut);
        const char* buttonLabel =
            capturing ? "Press a key..." : (keys.empty() ? "unbound" : keys.c_str());

        if (capturing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.5f, 0.1f, 1.0f));
        }
        if (ImGui::Button(buttonLabel, ImVec2(150, 0))) {
            s_capturing = capturing ? std::string() : action.id;
        }
        if (capturing) ImGui::PopStyleColor();

        if (capturing) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                s_capturing.clear();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
                action.shortcut = Shortcut();
                s_capturing.clear();
                g_Actions.saveBindings(keybindingsPath(ui.settingsDirectory));
            } else {
                const Shortcut pressed = ActionRegistry::capturePressedShortcut();
                if (pressed.valid()) {
                    action.shortcut = pressed;
                    s_capturing.clear();
                    g_Actions.saveBindings(keybindingsPath(ui.settingsDirectory));
                }
            }
        }

        // A conflict is reported, not refused: two commands on one key is
        // occasionally deliberate, and a modal complaint in a settings
        // panel is worse than a line of text next to the row.
        const int clash = g_Actions.conflict(action.shortcut, action.id);
        if (clash >= 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "also %s",
                               g_Actions.all()[size_t(clash)].label.c_str());
        }

        if (action.shortcut != action.defaultShortcut) {
            ImGui::SameLine();
            if (ImGui::SmallButton("reset")) {
                action.shortcut = action.defaultShortcut;
                g_Actions.saveBindings(keybindingsPath(ui.settingsDirectory));
            }
        }

        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextDisabled(
        "The piano roll's D/S/E, the tracker's cursor keys and the note-entry\n"
        "keyboard are not listed: they mean different things depending on\n"
        "which panel has focus, and are not global commands.");

    ImGui::End();
}


// ============================================================================
// The browser
//
// Samples, instruments, presets and files, in one place you can drag out of.
// The model - scanning, filtering, what a drag carries - is in Browser.h;
// this is the panel and what happens when something is dropped.
// ============================================================================


/*
 * Which family an instrument belongs to, for the browser's second column.
 *
 * The boundaries are the same ones the Sound Palette draws its headers at.
 * They are indices rather than a field on the enum because that is what the
 * enum's order already encodes.
 */
inline const char* oscillatorCategoryName(OscillatorType type) {
    const int index = static_cast<int>(type);
    if (index <= 6)  return "Oscillator";
    if (index <= 16) return "Synth";
    if (index <= 22) return "Synthwave";
    if (index <= 27) return "Techno";
    if (index <= 31) return "Hip Hop";
    if (index <= 34) return "Additional";
    if (index <= 55) return "Drum";
    if (index <= 62) return "Reggaeton";
    if (index <= 64) return "Recreation";
    return "Engine";
}

/*
 * The shipped engine presets, flattened into one list.
 *
 * Each entry remembers which engine it belongs to, because applying one
 * means writing into a different struct depending on the answer - and a
 * preset applied to the wrong engine is silent, not loud.
 */
inline std::vector<BrowserEntry> collectPresetEntries() {
    std::vector<BrowserEntry> entries;

    auto append = [&](int engine, const char* engineName, const char* name,
                      const char* description, int index) {
        BrowserEntry entry;
        entry.kind = BrowserEntryKind::Preset;
        entry.name = name;
        entry.detail = engineName;
        entry.id = index;
        entry.engine = engine;
        (void)description;
        entries.push_back(entry);
    };

    for (int i = 0; i < FM_PRESET_COUNT; ++i) {
        append(static_cast<int>(OscillatorType::FMSynth), "FM",
               FM_PRESETS[i].name,
               FM_PRESETS[i].description, i);
    }
    for (int i = 0; i < DRUM_PRESET_COUNT; ++i) {
        append(static_cast<int>(OscillatorType::DrumModel), "Drum Model",
               DRUM_PRESETS[i].name,
               DRUM_PRESETS[i].description, i);
    }
    for (int i = 0; i < GRANULAR_PRESET_COUNT; ++i) {
        append(static_cast<int>(OscillatorType::Granular), "Granular",
               GRANULAR_PRESETS[i].name,
               GRANULAR_PRESETS[i].description, i);
    }
    return entries;
}

/*
 * Put a preset on a channel, switching it to the engine the preset is for.
 *
 * Switching the engine is the point: choosing "Bell" from a list and
 * hearing the channel's existing sawtooth would read as the preset having
 * done nothing.
 */
inline bool applyPresetToChannel(Project& project, Sequencer& seq,
                                 int channelIndex, int engine, int index) {
    if (channelIndex < 0 || channelIndex >= Project::MAX_CHANNELS) return false;
    ChannelConfig& channel = project.channels[static_cast<size_t>(channelIndex)];

    const OscillatorType type = static_cast<OscillatorType>(engine);

    if (type == OscillatorType::FMSynth) {
        if (index < 0 || index >= FM_PRESET_COUNT) return false;
        g_UndoHistory.saveState(project, "Apply Preset");
        FM_PRESETS[index].apply(channel.oscillator.fm);
    } else if (type == OscillatorType::DrumModel) {
        if (index < 0 || index >= DRUM_PRESET_COUNT) return false;
        g_UndoHistory.saveState(project, "Apply Preset");
        DRUM_PRESETS[index].apply(channel.oscillator.drumModel);
    } else if (type == OscillatorType::Granular) {
        if (index < 0 || index >= GRANULAR_PRESET_COUNT) return false;
        g_UndoHistory.saveState(project, "Apply Preset");
        GRANULAR_PRESETS[index].apply(channel.oscillator.granular);
    } else {
        return false;
    }

    channel.oscillator.type = type;
    seq.updateChannelConfigs();
    return true;
}

static BrowserState g_Browser;

inline BrowserState& browserState() { return g_Browser; }

// What the project already holds.
inline std::vector<BrowserEntry> collectSampleEntries(const Project& project) {
    std::vector<BrowserEntry> entries;
    for (int id = 0; id < project.samplePool.count(); ++id) {
        const Sample* sample = project.samplePool.getSample(id);
        if (sample == nullptr) continue;

        BrowserEntry entry;
        entry.kind = BrowserEntryKind::Sample;
        entry.name = sample->name.empty() ? ("Sample " + std::to_string(id))
                                          : sample->name;
        entry.id = id;

        char detail[64];
        snprintf(detail, sizeof(detail), "%.2f s  -  %d Hz",
                 sample->lengthSeconds, sample->sampleRate);
        entry.detail = detail;
        entry.path = sample->filepath;
        entries.push_back(entry);
    }
    return entries;
}

// Every oscillator type, by the name the rest of the UI uses for it.
inline std::vector<BrowserEntry> collectInstrumentEntries() {
    std::vector<BrowserEntry> entries;
    for (int type = 0; type < OSC_NAME_COUNT; ++type) {
        BrowserEntry entry;
        entry.kind = BrowserEntryKind::Instrument;
        entry.name = oscillatorTypeName(static_cast<OscillatorType>(type));
        entry.detail = oscillatorCategoryName(static_cast<OscillatorType>(type));
        entry.id = type;
        entries.push_back(entry);
    }
    return entries;
}

/*
 * Drop a browser entry onto a channel.
 *
 * Shared by the mixer strip, the channel editor and the tracker header, so
 * the three cannot drift apart in what a drop means.
 */
inline bool applyBrowserDropToChannel(Project& project, Sequencer& seq,
                                      int channelIndex,
                                      const BrowserPayload& payload) {
    if (channelIndex < 0 || channelIndex >= Project::MAX_CHANNELS) return false;
    ChannelConfig& channel = project.channels[static_cast<size_t>(channelIndex)];

    switch (payload.kind) {
        case BrowserEntryKind::Instrument: {
            if (payload.id < 0 ||
                payload.id >= OSC_NAME_COUNT) return false;
            g_UndoHistory.saveState(project, "Set Instrument");
            channel.oscillator.type = static_cast<OscillatorType>(payload.id);
            seq.updateChannelConfigs();
            return true;
        }
        case BrowserEntryKind::Preset:
            return applyPresetToChannel(project, seq, channelIndex,
                                        payload.engine, payload.id);

        case BrowserEntryKind::Sample: {
            // A sample on a channel means the sampler engine plays it.
            if (project.samplePool.getSample(payload.id) == nullptr) return false;
            g_UndoHistory.saveState(project, "Load Sample");
            channel.oscillator.type = OscillatorType::Sampler;
            channel.oscillator.sampler.zoneCount = 1;
            channel.oscillator.sampler.zones[0] = SampleZone();
            channel.oscillator.sampler.zones[0].sampleId = payload.id;
            seq.updateChannelConfigs();
            return true;
        }
        default:
            return false;
    }
}

/*
 * Drop a browser entry onto the arrangement at a beat and a channel.
 *
 * Only samples and audio files make a clip. Dropping an instrument here
 * would have to invent a pattern to go with it, and inventing content the
 * user did not ask for is worse than doing nothing.
 */
inline bool applyBrowserDropToTimeline(Project& project, float beat,
                                       int channelIndex,
                                       const BrowserPayload& payload) {
    int sampleId = -1;

    if (payload.kind == BrowserEntryKind::Sample) {
        sampleId = payload.id;
    } else if (payload.kind == BrowserEntryKind::File) {
        // Loading here rather than at drag time: a file dragged across the
        // window and dropped somewhere useless should not have cost a disk
        // read and a pool entry.
        sampleId = project.samplePool.loadSample(payload.path);
    }

    const Sample* sample = project.samplePool.getSample(sampleId);
    if (sample == nullptr) return false;

    g_UndoHistory.saveState(project, "Add Audio Clip");

    Clip clip;
    clip.type = ClipType::Audio;
    clip.sampleId = sampleId;
    clip.channelIndex = std::clamp(channelIndex, 0,
                                   std::max(0, project.activeChannelCount() - 1));
    clip.startBeat = std::max(0.0f, beat);

    // As long as the audio actually is, at the tempo where it lands - a clip
    // whose length does not match its sample either repeats or cuts off,
    // and both look like a bug rather than a default.
    const float bpm = std::max(1.0f, project.bpmAt(clip.startBeat));
    clip.lengthBeats = std::max(0.25f, sample->lengthSeconds * bpm / 60.0f);
    clip.gain = 1.0f;
    project.arrangement.push_back(clip);
    return true;
}

/*
 * The drop target half of the above, for a channel.
 *
 * A free function because three panels need it and each would otherwise
 * write the same six lines of BeginDragDropTarget slightly differently.
 */
inline bool BrowserChannelDropTarget(Project& project, Sequencer& seq,
                                     int channelIndex) {
    if (!ImGui::BeginDragDropTarget()) return false;

    bool applied = false;
    if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(BROWSER_PAYLOAD_TYPE)) {
        if (payload->DataSize == static_cast<int>(sizeof(BrowserPayload))) {
            BrowserPayload dropped;
            std::memcpy(&dropped, payload->Data, sizeof(dropped));
            applied = applyBrowserDropToChannel(project, seq, channelIndex, dropped);
        }
    }
    ImGui::EndDragDropTarget();
    return applied;
}

inline void DrawBrowser(Project& project, UIState& ui, Sequencer& seq) {
    if (!ui.showBrowser) return;

    ImGui::SetNextWindowSize(ImVec2(320, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Browser", &ui.showBrowser)) {
        ImGui::End();
        return;
    }

    BrowserState& state = g_Browser;

    if (ImGui::BeginTabBar("##browsertabs")) {
        for (int t = 0; t < static_cast<int>(BrowserTab::Count); ++t) {
            const BrowserTab tab = static_cast<BrowserTab>(t);
            if (ImGui::BeginTabItem(browserTabName(tab))) {
                state.tab = tab;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##browsersearch", "Search...", state.search,
                             sizeof(state.search));

    // ---- What this tab is showing -------------------------------------------
    std::vector<BrowserEntry> entries;

    switch (state.tab) {
        case BrowserTab::Samples:
            entries = collectSampleEntries(project);
            break;

        case BrowserTab::Instruments:
            entries = collectInstrumentEntries();
            break;

        case BrowserTab::Presets:
            entries = collectPresetEntries();
            break;

        case BrowserTab::Files: {
            if (state.directory.empty()) {
                std::error_code error;
                state.directory = std::filesystem::current_path(error).string();
                if (error) state.directory = ".";
                state.filesScanned = false;
            }

            ImGui::TextDisabled("%s", state.directory.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", state.directory.c_str());
            }
            if (ImGui::SmallButton("Refresh")) state.filesScanned = false;
            ImGui::SameLine();
            if (ImGui::SmallButton("Choose folder...")) {
                const std::string picked = openFileDialog(
                    "Audio and Projects\0*.wav;*.mp3;*.flac;*.ogg;*.ctp\0"
                    "All Files (*.*)\0*.*\0", "wav");
                if (!picked.empty()) {
                    std::error_code error;
                    const std::filesystem::path parent =
                        std::filesystem::path(picked).parent_path();
                    if (std::filesystem::is_directory(parent, error) && !error) {
                        state.directory = parent.string();
                        state.filesScanned = false;
                    }
                }
            }

            // Scanned on demand. Walking a directory every frame would put a
            // syscall per file into the frame budget, which a sample library
            // on a network drive will not survive.
            if (!state.filesScanned) {
                scanDirectory(state.directory, state.files);
                state.filesScanned = true;
            }
            entries = state.files;
            break;
        }

        default:
            break;
    }

    const std::vector<BrowserEntry> shown =
        filterEntries(entries, state.search);

    ImGui::Separator();

    if (shown.empty()) {
        if (state.search[0] != '\0') {
            ImGui::TextDisabled("Nothing matches \"%s\"", state.search);
        } else if (state.tab == BrowserTab::Samples) {
            ImGui::TextWrapped(
                "No samples yet. Record a take, or drop a .wav onto the "
                "arrangement, and it will appear here.");
        } else {
            ImGui::TextDisabled("Nothing here.");
        }
    }

    ImGui::BeginChild("##browserlist", ImVec2(0, -28), false);
    for (size_t i = 0; i < shown.size(); ++i) {
        const BrowserEntry& entry = shown[i];
        ImGui::PushID(static_cast<int>(i));

        const bool selected = (state.selected == static_cast<int>(i));
        const std::string label =
            (entry.kind == BrowserEntryKind::Directory ? "[ ] " : "") + entry.name;

        if (ImGui::Selectable(label.c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
            state.selected = static_cast<int>(i);

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                switch (entry.kind) {
                    case BrowserEntryKind::Directory:
                        state.directory = entry.path;
                        state.filesScanned = false;
                        state.selected = -1;
                        break;

                    case BrowserEntryKind::File: {
                        BrowserPayload payload = makePayload(entry);
                        if (applyBrowserDropToTimeline(
                                project, 0.0f, ui.selectedChannel, payload)) {
                            state.lastAction = "Added " + entry.name +
                                               " to the arrangement";
                        } else {
                            state.lastAction = "Could not read " + entry.name;
                        }
                        break;
                    }

                    default: {
                        BrowserPayload payload = makePayload(entry);
                        if (applyBrowserDropToChannel(project, seq,
                                                      ui.selectedChannel, payload)) {
                            state.lastAction =
                                entry.name + " on " +
                                project.channels[static_cast<size_t>(
                                    std::clamp(ui.selectedChannel, 0,
                                               Project::MAX_CHANNELS - 1))].name;
                        }
                        break;
                    }
                }
            }
        }

        // Anything but a directory can be dragged out.
        if (entry.kind != BrowserEntryKind::Directory &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const BrowserPayload payload = makePayload(entry);
            ImGui::SetDragDropPayload(BROWSER_PAYLOAD_TYPE, &payload,
                                      sizeof(payload));
            ImGui::TextUnformatted(entry.name.c_str());
            ImGui::TextDisabled("Drop on a channel or the arrangement");
            ImGui::EndDragDropSource();
        }

        if (!entry.detail.empty()) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.0f);
            ImGui::TextDisabled("%s", entry.detail.c_str());
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    // A browser that silently succeeds looks broken, so say what happened.
    if (!state.lastAction.empty()) {
        ImGui::TextDisabled("%s", state.lastAction.c_str());
    } else {
        // Short enough to survive the narrow column the browser docks into.
        ImGui::TextDisabled("Double-click, or drag out");
    }

    ImGui::End();
}


// ============================================================================
// Plugins
//
// The rack of hosted plugins on the selected channel, and the list of what
// is installed. See PluginHost.h for what this build can and cannot host -
// no format loader ships, so the list is honest about finding plugins it
// cannot open rather than pretending they are not there.
// ============================================================================

static PluginManager g_Plugins;

inline PluginManager& pluginManager() { return g_Plugins; }

struct PluginPanelState {
    bool scanning = false;
    std::string lastScanSummary;
    std::vector<std::string> problems;   // from the last chain rebuild
    int selectedAvailable = -1;
    char search[64] = {};
};

static PluginPanelState g_PluginPanel;

inline PluginPanelState& pluginPanelState() { return g_PluginPanel; }

/*
 * Rebuild the audio-side chains after the rack changes.
 *
 * Always through here, never by editing project.channels[..].plugins and
 * hoping: the running instances are what makes sound, and a rack edited
 * without a rebuild is a change the user made and cannot hear.
 */
inline void ApplyPluginChanges(Project& project, Sequencer& seq) {
    seq.rebuildPluginChains(g_Plugins, &g_PluginPanel.problems);
}

// ============================================================================
// Oscilloscopes
// ============================================================================
/*
 * One waveform tile per channel, and an X-Y plot of the master.
 *
 * A level meter says a channel is doing something; it cannot say what. On
 * chip work that gap is most of the job - a duty change and a volume change
 * look identical on a meter, a wave with a DC offset looks perfectly
 * healthy, and two channels beating against each other look like two
 * channels. All of it is obvious the moment you can see the shape.
 */
inline void DrawScopes(Project& project, UIState& ui, Sequencer& seq) {
    if (!ui.showScopes) return;

    ImGui::SetNextWindowSize(ImVec2(560, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scopes", &ui.showScopes)) {
        ImGui::End();
        return;
    }

    static bool s_showXY = true;
    static int s_xyLeft = 0;
    static int s_xyRight = 1;
    static float s_gain = 1.0f;

    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Gain", &s_gain, 0.25f, 8.0f, "%.2fx");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Display only. A quiet channel is still a shape.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("X-Y", &s_showXY);

    ImGui::Separator();

    const float sampleRate = seq.sampleRate();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // A tile per channel, wrapped to the window's width.
    const float available = ImGui::GetContentRegionAvail().x;
    const float tileWidth = std::max(110.0f, std::min(190.0f, available * 0.48f));
    const float tileHeight = tileWidth * 0.5f;
    const int perRow = std::max(1, static_cast<int>(available / (tileWidth + 8.0f)));

    // Only the channels that exist in this project, not all thirty-two: a
    // wall of flat lines for channels nobody has used is noise, and it
    // pushes the ones that matter off the bottom.
    const int channelCount =
        std::clamp(static_cast<int>(project.channels.size()), 1,
                   Project::MAX_CHANNELS);

    std::vector<float> window(ScopeBuffer::SIZE / 2);

    int drawnInRow = 0;
    for (int ch = 0; ch < channelCount; ++ch) {
        const ChannelConfig& config = project.channels[static_cast<size_t>(ch)];

        if (drawnInRow > 0) ImGui::SameLine();

        ImGui::PushID(3500 + ch);
        ImGui::BeginGroup();

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 corner(origin.x + tileWidth, origin.y + tileHeight);

        const bool selected = (ch == ui.selectedChannel);
        draw->AddRectFilled(origin, corner, IM_COL32(14, 14, 22, 255));
        draw->AddRect(origin, corner,
                      selected ? IM_COL32(90, 200, 255, 255)
                               : IM_COL32(70, 70, 90, 255));

        const float centre = origin.y + tileHeight * 0.5f;
        draw->AddLine(ImVec2(origin.x, centre), ImVec2(corner.x, centre),
                      IM_COL32(50, 50, 62, 255));

        const float frequency = seq.channelFrequency(ch);
        const int count = std::min(scopeWindowForPitch(frequency, sampleRate),
                                   static_cast<int>(window.size()));

        const bool sounding = seq.channelScope(ch).read(window.data(), count);

        if (sounding) {
            const ImU32 colour = config.muted ? IM_COL32(120, 120, 130, 200)
                                              : IM_COL32(90, 230, 160, 255);
            ImVec2 previous(origin.x, centre);
            for (int i = 0; i < count; ++i) {
                const float x = origin.x + tileWidth * float(i) / float(count - 1);
                const float value = std::clamp(window[static_cast<size_t>(i)] * s_gain,
                                               -1.0f, 1.0f);
                const ImVec2 point(x, centre - value * tileHeight * 0.45f);
                if (i > 0) draw->AddLine(previous, point, colour, 1.4f);
                previous = point;
            }
        } else {
            // Said, not drawn as a flat line: a flat line at zero and a
            // channel that is not playing look the same, and one of them is
            // a bug.
            draw->AddText(ImVec2(origin.x + 8.0f, centre - 7.0f),
                          IM_COL32(90, 90, 105, 255), "silent");
        }

        // Clicking a tile selects that channel, which is the thing you want
        // to do next after noticing something on it.
        ImGui::InvisibleButton("##tile", ImVec2(tileWidth, tileHeight));
        if (ImGui::IsItemClicked()) ui.selectedChannel = ch;
        if (ImGui::IsItemHovered() && frequency > 1.0f) {
            ImGui::SetTooltip("%s\n%.1f Hz, %d samples shown",
                              config.name.c_str(), frequency, count);
        }

        ImGui::TextDisabled("%.14s", config.name.c_str());
        ImGui::EndGroup();
        ImGui::PopID();

        ++drawnInRow;
        if (drawnInRow >= perRow) drawnInRow = 0;
    }

    if (!s_showXY) {
        ImGui::End();
        return;
    }

    ImGui::Separator();

    /*
     * The X-Y plot.
     *
     * One signal against another rather than either against time. Two
     * identical signals draw a diagonal line, two that are out of phase draw
     * the opposite diagonal, and anything uncorrelated fills the square - so
     * a mix that has collapsed to mono, or one whose sides have gone out of
     * phase, is visible here and on nothing else in the program.
     */
    ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "X-Y");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::SliderInt("##x", &s_xyLeft, -1, channelCount - 1,
                     s_xyLeft < 0 ? "Master L" : "Ch %d");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::SliderInt("##y", &s_xyRight, -1, channelCount - 1,
                     s_xyRight < 0 ? "Master R" : "Ch %d");

    const float side = std::max(120.0f, std::min(240.0f,
                                                 ImGui::GetContentRegionAvail().x));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner(origin.x + side, origin.y + side);

    draw->AddRectFilled(origin, corner, IM_COL32(14, 14, 22, 255));
    draw->AddRect(origin, corner, IM_COL32(70, 70, 90, 255));
    draw->AddLine(ImVec2(origin.x, origin.y + side * 0.5f),
                  ImVec2(corner.x, origin.y + side * 0.5f),
                  IM_COL32(45, 45, 58, 255));
    draw->AddLine(ImVec2(origin.x + side * 0.5f, origin.y),
                  ImVec2(origin.x + side * 0.5f, corner.y),
                  IM_COL32(45, 45, 58, 255));

    {
        constexpr int POINTS = 512;
        std::vector<float> xs(POINTS), ys(POINTS);

        const ScopeBuffer& sourceX =
            (s_xyLeft < 0) ? seq.masterScopeLeft() : seq.channelScope(s_xyLeft);
        const ScopeBuffer& sourceY =
            (s_xyRight < 0) ? seq.masterScopeRight() : seq.channelScope(s_xyRight);

        /*
         * Both read untriggered and from the same length of history.
         *
         * Triggering each side independently would align them to their own
         * zero crossings and destroy the very thing this plot measures: the
         * phase relationship between them.
         */
        const bool anyX = sourceX.read(xs.data(), POINTS, 0.0f);
        const bool anyY = sourceY.read(ys.data(), POINTS, 0.0f);

        if (anyX || anyY) {
            ImVec2 previous(0.0f, 0.0f);
            for (int i = 0; i < POINTS; ++i) {
                const float x = std::clamp(xs[static_cast<size_t>(i)] * s_gain,
                                           -1.0f, 1.0f);
                const float y = std::clamp(ys[static_cast<size_t>(i)] * s_gain,
                                           -1.0f, 1.0f);
                const ImVec2 point(origin.x + side * (0.5f + x * 0.45f),
                                   origin.y + side * (0.5f - y * 0.45f));
                if (i > 0) draw->AddLine(previous, point, IM_COL32(255, 180, 90, 140), 1.0f);
                previous = point;
            }
        } else {
            draw->AddText(ImVec2(origin.x + 10.0f, origin.y + side * 0.5f),
                          IM_COL32(90, 90, 105, 255), "silent");
        }
    }

    ImGui::Dummy(ImVec2(side, side));
    ImGui::TextDisabled(
        "A diagonal is two signals in phase. The other diagonal is out of "
        "phase. A filled square is uncorrelated.");

    ImGui::End();
}

// ============================================================================
// Note FX rack
// ============================================================================
/*
 * The rack that transforms notes rather than samples.
 *
 * The panel is built around one idea: you should be able to see what the
 * rack is doing without playing anything. The preview at the bottom runs a
 * middle C through the rack exactly as the sequencer will and prints what
 * comes out, so "Chord + Arpeggio at an eighth" is a list of notes and
 * times rather than a guess.
 */
inline void DrawNoteFXPanel(Project& project, UIState& ui, Sequencer& seq) {
    if (!ui.showNoteFX) return;

    ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Note FX", &ui.showNoteFX)) {
        ImGui::End();
        return;
    }

    const int channel = std::clamp(ui.selectedChannel, 0, Project::MAX_CHANNELS - 1);
    ChannelConfig& config = project.channels[static_cast<size_t>(channel)];
    std::vector<NoteFXSlot>& rack = config.noteFX;

    bool changed = false;

    ImGui::Text("%s", config.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%d module%s)", int(rack.size()),
                        rack.size() == 1 ? "" : "s");

    if (rack.empty()) {
        ImGui::TextWrapped(
            "Nothing yet. These change WHICH notes play, not how they sound - "
            "the pattern keeps the note you wrote, and this decides what is "
            "made of it. Add one below.");
    }

    ImGui::Separator();

    int removeAt = -1;
    int moveFrom = -1;
    int moveTo = -1;

    for (size_t i = 0; i < rack.size(); ++i) {
        NoteFXSlot& slot = rack[i];
        ImGui::PushID(static_cast<int>(2400 + i));

        if (ImGui::Checkbox("##on", &slot.enabled)) changed = true;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Switch this module off without losing its settings");
        }
        ImGui::SameLine();

        ImGui::SetNextItemWidth(130.0f);
        int typeIndex = static_cast<int>(slot.type);
        if (ImGui::BeginCombo("##type", noteFXName(slot.type))) {
            for (int t = 0; t < NOTE_FX_TYPE_COUNT; ++t) {
                const NoteFXType candidate = static_cast<NoteFXType>(t);
                if (ImGui::Selectable(noteFXName(candidate), t == typeIndex)) {
                    // Only the type changes. Every other field is kept, so
                    // trying Chord and going back to Arpeggio does not throw
                    // away the arpeggio settings - which is why the file
                    // format writes all of them for every slot.
                    slot.type = candidate;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        // Order matters: Chord then Arpeggio is a running arpeggio, and
        // Arpeggio then Chord is a chord on every step of one.
        ImGui::SameLine();
        if (ImGui::ArrowButton("##up", ImGuiDir_Up) && i > 0) {
            moveFrom = static_cast<int>(i);
            moveTo = static_cast<int>(i) - 1;
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##down", ImGuiDir_Down) && i + 1 < rack.size()) {
            moveFrom = static_cast<int>(i);
            moveTo = static_cast<int>(i) + 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeAt = static_cast<int>(i);

        // ---- The parameters this module actually reads ---------------------
        ImGui::Indent(24.0f);
        ImGui::SetNextItemWidth(-110.0f);

        switch (slot.type) {
        case NoteFXType::Transpose:
            if (ImGui::SliderInt("Semitones", &slot.semitones, -24, 24)) changed = true;
            break;

        case NoteFXType::Chord: {
            if (ImGui::BeginCombo("Shape", chordShapeName(slot.chordShape))) {
                for (int s = 0; s < CHORD_SHAPE_COUNT; ++s) {
                    if (ImGui::Selectable(chordShapeName(s), s == slot.chordShape)) {
                        slot.chordShape = s;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Checkbox("Drop the top voice an octave",
                                &slot.chordInversion)) {
                changed = true;
            }
            if (slot.chordShape == static_cast<int>(ChordShape::Diatonic)) {
                ImGui::SetNextItemWidth(-110.0f);
                if (ImGui::SliderInt("Key", &slot.scaleRoot, 0, 11,
                                     noteName(slot.scaleRoot))) {
                    changed = true;
                }
                ImGui::SetNextItemWidth(-110.0f);
                if (ImGui::SliderInt("Scale", &slot.scaleType, 0, SCALE_COUNT - 1,
                                     scaleName(slot.scaleType))) {
                    changed = true;
                }
            }
            break;
        }

        case NoteFXType::Arpeggio: {
            if (ImGui::BeginCombo("Direction", noteArpModeName(slot.arpMode))) {
                for (int m = 0; m < NOTE_ARP_MODE_COUNT; ++m) {
                    if (ImGui::Selectable(noteArpModeName(m), m == slot.arpMode)) {
                        slot.arpMode = m;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(-110.0f);
            if (ImGui::SliderFloat("Rate", &slot.arpRate, 1.0f / 32.0f, 1.0f,
                                   "%.3f beats")) {
                changed = true;
            }
            ImGui::SetNextItemWidth(-110.0f);
            if (ImGui::SliderInt("Octaves", &slot.arpOctaves, 1, 4)) changed = true;
            ImGui::SetNextItemWidth(-110.0f);
            if (ImGui::SliderFloat("Gate", &slot.arpGate, 0.05f, 1.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How much of each step sounds. Low is staccato.");
            }
            ImGui::TextDisabled("Put a Chord above this to arpeggiate a chord.");
            break;
        }

        case NoteFXType::Strum:
            if (ImGui::SliderFloat("Gap", &slot.strumBeats, 0.0f, 0.25f,
                                   "%.3f beats")) {
                changed = true;
            }
            if (ImGui::Checkbox("Top note first", &slot.strumDown)) changed = true;
            break;

        case NoteFXType::Octave:
            if (ImGui::SliderInt("Octaves", &slot.octaves, -3, 3)) changed = true;
            ImGui::SetNextItemWidth(-110.0f);
            if (ImGui::SliderFloat("Level", &slot.mix, 0.0f, 1.0f, "%.2f")) {
                changed = true;
            }
            break;

        case NoteFXType::ScaleSnap:
            if (ImGui::SliderInt("Key", &slot.scaleRoot, 0, 11,
                                 noteName(slot.scaleRoot))) {
                changed = true;
            }
            ImGui::SetNextItemWidth(-110.0f);
            if (ImGui::SliderInt("Scale", &slot.scaleType, 0, SCALE_COUNT - 1,
                                 scaleName(slot.scaleType))) {
                changed = true;
            }
            break;

        case NoteFXType::Range:
            if (ImGui::SliderInt("Lowest", &slot.lowPitch, 0, 127)) changed = true;
            ImGui::SetNextItemWidth(-110.0f);
            if (ImGui::SliderInt("Highest", &slot.highPitch, 0, 127)) changed = true;
            if (ImGui::Checkbox("Pass what is outside instead",
                                &slot.rangeInvert)) {
                changed = true;
            }
            break;

        case NoteFXType::Velocity:
            if (ImGui::SliderFloat("Scale", &slot.velocityScale, 0.0f, 2.0f,
                                   "%.2f")) {
                changed = true;
            }
            ImGui::SetNextItemWidth(-110.0f);
            if (ImGui::SliderFloat("Fixed", &slot.velocityFixed, 0.0f, 1.0f,
                                   "%.2f")) {
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Above zero this replaces the level instead "
                                  "of scaling it");
            }
            ImGui::SetNextItemWidth(-110.0f);
            if (ImGui::SliderFloat("Random", &slot.velocityRandom, 0.0f, 0.5f,
                                   "%.2f")) {
                changed = true;
            }
            break;
        }

        ImGui::Unindent(24.0f);
        ImGui::Separator();
        ImGui::PopID();
    }

    if (moveFrom >= 0 && moveTo >= 0 && moveTo < static_cast<int>(rack.size())) {
        std::swap(rack[static_cast<size_t>(moveFrom)],
                  rack[static_cast<size_t>(moveTo)]);
        changed = true;
    }
    if (removeAt >= 0) {
        g_UndoHistory.saveState(project, "Remove Note FX");
        rack.erase(rack.begin() + removeAt);
        changed = true;
    }

    // ---- Add ----------------------------------------------------------------
    static int s_addType = static_cast<int>(NoteFXType::Chord);
    const bool full = rack.size() >= static_cast<size_t>(MAX_NOTE_FX);

    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##add", noteFXName(static_cast<NoteFXType>(s_addType)))) {
        for (int t = 0; t < NOTE_FX_TYPE_COUNT; ++t) {
            if (ImGui::Selectable(noteFXName(static_cast<NoteFXType>(t)),
                                  t == s_addType)) {
                s_addType = t;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    if (full) ImGui::BeginDisabled();
    if (ImGui::Button("Add module")) {
        g_UndoHistory.saveState(project, "Add Note FX");
        NoteFXSlot slot;
        slot.type = static_cast<NoteFXType>(s_addType);
        rack.push_back(slot);
        changed = true;
    }
    if (full) ImGui::EndDisabled();
    if (full) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%d is the limit)", MAX_NOTE_FX);
    }

    // ---- What it does, without playing anything -----------------------------
    //
    // The same call the sequencer makes, on one middle C. A rack you have to
    // press play to understand is a rack you dial in by accident.
    ImGui::Separator();
    {
        NoteFXRack live;
        buildNoteFXRack(rack, live);

        NoteVoice voices[MAX_NOTE_VOICES];
        voices[0].startBeat = 0.0f;
        voices[0].endBeat = 1.0f;
        voices[0].velocity = 0.8f;
        voices[0].pitch = 60;

        const int count = applyNoteFX(live, voices, 1, MAX_NOTE_VOICES, 0x5EEDu);

        ImGui::TextDisabled("One beat of C4 becomes %d note%s:", count,
                            count == 1 ? "" : "s");

        std::string line;
        for (int i = 0; i < count && i < 12; ++i) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%s%d@%.2f",
                          noteName(voices[i].pitch % 12),
                          voices[i].pitch / 12 - 1, voices[i].startBeat);
            if (!line.empty()) line += "  ";
            line += buffer;
        }
        if (count > 12) line += "  ...";
        if (count == 0) line = "(nothing - the rack is filtering it out)";
        ImGui::TextWrapped("%s", line.c_str());
    }

    /*
     * One sync point, at the end of the frame's edits.
     *
     * The audio thread reads a fixed-size copy of this rack rather than the
     * vector itself, and this is what refreshes it. Doing it per widget
     * would refresh eight times while somebody drags a slider.
     */
    if (changed) seq.updateChannelConfigs();

    ImGui::End();
}

inline void DrawPluginsPanel(Project& project, UIState& ui, Sequencer& seq) {
    if (!ui.showPlugins) return;

    ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plugins", &ui.showPlugins)) {
        ImGui::End();
        return;
    }

    PluginPanelState& state = g_PluginPanel;
    const int channel = std::clamp(ui.selectedChannel, 0, Project::MAX_CHANNELS - 1);
    ChannelConfig& config = project.channels[static_cast<size_t>(channel)];

    /*
     * What this build can actually do.
     *
     * Said once, at the top, rather than discovered by loading a plugin and
     * getting an error: somebody with a folder full of VST3s deserves to
     * know before they go looking.
     */
    {
        std::string hosted;
        const PluginFormat FORMATS[] = {PluginFormat::VST2, PluginFormat::VST3,
                                        PluginFormat::CLAP};
        for (PluginFormat format : FORMATS) {
            if (!g_Plugins.registry().supports(format)) continue;
            if (!hosted.empty()) hosted += ", ";
            hosted += pluginFormatName(format);
        }

        if (hosted.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "This build cannot load plugins.");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Scanning still works, and a project keeps its plugins and\n"
                    "their settings - so a project made elsewhere opens here\n"
                    "without losing them. See docs/ARCHITECTURE.md.");
            }
        } else {
            ImGui::Text("Hosting: %s", hosted.c_str());
        }
    }

    ImGui::Separator();

    // ---- The rack on this channel --------------------------------------------
    ImGui::Text("%s", config.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%d plugin%s)", int(config.plugins.size()),
                        config.plugins.size() == 1 ? "" : "s");

    if (config.plugins.empty()) {
        ImGui::TextWrapped(
            "No plugins on this channel. Choose one below and press Add.");
    }

    int removeAt = -1;
    for (size_t i = 0; i < config.plugins.size(); ++i) {
        PluginSlot& slot = config.plugins[i];
        ImGui::PushID(static_cast<int>(2200 + i));

        PluginChain& chain = seq.pluginChain(channel);
        IPluginInstance* instance = chain.at(static_cast<int>(i));

        if (ImGui::Checkbox("##enabled", &slot.enabled)) {
            ApplyPluginChanges(project, seq);
        }
        ImGui::SameLine();

        // A slot whose plugin is not loaded is drawn differently and says
        // so: an empty-looking row would read as the settings being gone,
        // when in fact they are all still in the project.
        if (instance == nullptr) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "%s",
                               slot.name.empty() ? "(unnamed)" : slot.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("- not loaded");
        } else {
            ImGui::Text("%s", slot.name.c_str());
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
        if (ImGui::SmallButton(slot.bypassed ? "byp" : "on")) {
            slot.bypassed = !slot.bypassed;
            chain.setBypassed(static_cast<int>(i), slot.bypassed);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeAt = static_cast<int>(i);

        // The parameters, from the running plugin. Only from a running one:
        // the saved values alone have no names or ranges to draw.
        if (instance != nullptr && instance->parameterCount() > 0) {
            if (ImGui::TreeNode("Parameters")) {
                for (int p = 0; p < instance->parameterCount(); ++p) {
                    const PluginParameter& parameter = instance->parameter(p);
                    float value = parameter.value;

                    ImGui::PushID(p);
                    ImGui::SetNextItemWidth(-90.0f);
                    if (ImGui::SliderFloat(parameter.name.c_str(), &value,
                                           0.0f, 1.0f, "%.3f")) {
                        instance->setParameter(p, value);

                        // Written back to the project as it moves, so
                        // closing without touching anything else still
                        // saves what was heard.
                        if (slot.parameterValues.size() <=
                            static_cast<size_t>(p)) {
                            slot.parameterValues.resize(
                                static_cast<size_t>(instance->parameterCount()),
                                0.0f);
                        }
                        slot.parameterValues[static_cast<size_t>(p)] = value;
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        } else if (instance == nullptr && !slot.parameterValues.empty()) {
            ImGui::TextDisabled("   %d saved parameter%s kept",
                                int(slot.parameterValues.size()),
                                slot.parameterValues.size() == 1 ? "" : "s");
        }

        ImGui::PopID();
    }

    if (removeAt >= 0) {
        g_UndoHistory.saveState(project, "Remove Plugin");
        config.plugins.erase(config.plugins.begin() + removeAt);
        ApplyPluginChanges(project, seq);
    }

    // Anything that could not be loaded, said plainly.
    if (!state.problems.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Could not load:");
        for (const std::string& problem : state.problems) {
            ImGui::TextWrapped("%s", problem.c_str());
        }
    }

    ImGui::Separator();

    // ---- What is installed ------------------------------------------------------
    if (ImGui::Button("Scan for plugins")) {
        const ScanResult result = g_Plugins.scan();

        char summary[192];
        snprintf(summary, sizeof(summary),
                 "%d found, %d files looked at, %d folders missing",
                 int(result.found.size()), result.filesExamined,
                 result.directoriesMissing);
        state.lastScanSummary = summary;

        g_Plugins.saveCache(pluginCachePath(ui.settingsDirectory));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Looks in the standard plugin folders for this system.");
    }

    if (!state.lastScanSummary.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", state.lastScanSummary.c_str());
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pluginsearch", "Search installed...",
                             state.search, sizeof(state.search));

    ImGui::BeginChild("##installed", ImVec2(0, 150), true);
    if (!g_Plugins.scanned()) {
        ImGui::TextDisabled("Not scanned yet.");
    } else if (g_Plugins.available().empty()) {
        ImGui::TextWrapped("Nothing found in the standard plugin folders.");
    }

    const std::string query = state.search;
    for (size_t i = 0; i < g_Plugins.available().size(); ++i) {
        const PluginDescriptor& plugin = g_Plugins.available()[i];
        if (!query.empty() && fuzzyScore(query, plugin.name) < 0) continue;

        ImGui::PushID(static_cast<int>(2400 + i));
        const bool selected = (state.selectedAvailable == static_cast<int>(i));
        if (ImGui::Selectable(plugin.name.c_str(), selected)) {
            state.selectedAvailable = static_cast<int>(i);
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50.0f);
        ImGui::TextDisabled("%s", pluginFormatName(plugin.format));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", plugin.path.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();

    const bool canAdd = state.selectedAvailable >= 0 &&
                        state.selectedAvailable <
                            static_cast<int>(g_Plugins.available().size());
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("Add to channel", ImVec2(-1, 0))) {
        const PluginDescriptor& plugin =
            g_Plugins.available()[static_cast<size_t>(state.selectedAvailable)];

        g_UndoHistory.saveState(project, "Add Plugin");

        PluginSlot slot;
        slot.format = plugin.format;
        slot.path = plugin.path;
        slot.uid = plugin.uid;
        slot.name = plugin.name;
        config.plugins.push_back(slot);

        ApplyPluginChanges(project, seq);
    }
    if (!canAdd) ImGui::EndDisabled();

    /*
     * What the latency costs, said rather than hidden.
     *
     * This used to read "nothing compensates for it yet", which was true and
     * is no longer: every channel is now delayed to meet the latest one, so
     * the parts stay in time with each other. What the user still pays is
     * the monitoring delay - the whole mix is this far behind the keyboard -
     * and that is the number worth showing.
     */
    const int latency = seq.compensatedLatencySamples();
    if (latency > 0) {
        const float ms = 1000.0f * static_cast<float>(latency) /
                         std::max(1.0f, seq.sampleRate());
        ImGui::TextDisabled("Latency: %d samples (%.1f ms), compensated",
                            latency, ms);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Plugins and studio effects cannot produce output the instant\n"
                "they receive input. Every channel is delayed to match the\n"
                "latest one, so the parts stay in time with each other.\n\n"
                "What is left is monitoring delay: notes you play live are\n"
                "heard this far behind the key. Bypass the offending effect\n"
                "while tracking if it gets in the way.");
        }
    }

    ImGui::End();
}

// ============================================================================
// File Menu Bar
// ============================================================================
inline void DrawFileMenu(Project& project, UIState& ui, Sequencer& seq) {
    // Set initial window position on first use (top, next to Transport)
    ImGui::SetNextWindowPos(ImVec2(370, 35), ImGuiCond_FirstUseEver);
    // Tall enough for the demo buttons underneath the file and export rows.
    ImGui::SetNextWindowSize(ImVec2(400, 190), ImGuiCond_FirstUseEver);
    ImGui::Begin("File", nullptr, ImGuiWindowFlags_NoCollapse);

    // New project
    if (ImGui::Button("New", ImVec2(60, 25))) {
        project = Project();  // Reset to default
        ui.selectedPattern = 0;
        ui.selectedNoteIndex = -1;
        ui.selectedNoteIndices.clear();
        ui.projectFilePath = "";
        g_UndoHistory.clear();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create new project");

    ImGui::SameLine();

    // Load project
    if (ImGui::Button("Load", ImVec2(60, 25))) {
        std::string path = openFileDialog(
            "Chiptune Projects (*.ctp)\0*.ctp\0All Files (*.*)\0*.*\0",
            "ctp");
        if (!path.empty()) {
            if (loadProject(project, path)) {
                ui.projectFilePath = path;
                ui.selectedPattern = 0;
                ui.selectedNoteIndex = -1;
                ui.selectedNoteIndices.clear();
                g_UndoHistory.clear();
                seq.updateChannelConfigs();
                ApplyPluginChanges(project, seq);
            }
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load project file (.ctp)");

    ImGui::SameLine();

    // Save project
    if (ImGui::Button("Save", ImVec2(60, 25))) {
        if (ui.projectFilePath.empty()) {
            std::string path = saveFileDialog(
                "Chiptune Projects (*.ctp)\0*.ctp\0",
                "ctp");
            if (!path.empty()) {
                ui.projectFilePath = path;
                saveProject(project, path);
            }
        } else {
            saveProject(project, ui.projectFilePath);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save project (Ctrl+S)");

    ImGui::SameLine();

    // Save As
    if (ImGui::Button("Save As", ImVec2(70, 25))) {
        std::string path = saveFileDialog(
            "Chiptune Projects (*.ctp)\0*.ctp\0",
            "ctp");
        if (!path.empty()) {
            ui.projectFilePath = path;
            saveProject(project, path);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save project as new file");

    ImGui::SameLine(0, 30);

    // Export section
    ImGui::Text("Export:");
    ImGui::SameLine();

    static bool showExportPopup = false;
    static bool showMp3ExportPopup = false;
    static float exportDuration = 16.0f;
    static int mp3Bitrate = 192;
    static std::string exportStatus = "";

    // Calculate default duration helper
    auto calcDefaultDuration = [&]() {
        if (ui.selectedPattern >= 0 && ui.selectedPattern < static_cast<int>(project.patterns.size())) {
            const Pattern& pat = project.patterns[ui.selectedPattern];
            float maxEnd = 0.0f;
            for (const Note& n : pat.notes) {
                float end = n.startTime + n.duration;
                if (end > maxEnd) maxEnd = end;
            }
            exportDuration = std::max(4.0f, maxEnd + 1.0f);
        }
    };

    if (ImGui::Button("WAV", ImVec2(50, 25))) {
        showExportPopup = true;
        calcDefaultDuration();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Export to WAV audio file");

    ImGui::SameLine();

    if (ImGui::Button("MP3", ImVec2(50, 25))) {
        showMp3ExportPopup = true;
        calcDefaultDuration();
    }
    if (ImGui::IsItemHovered()) {
        std::string tooltip = "Export to MP3 audio file\n" + getMp3EncoderStatus();
        ImGui::SetTooltip("%s", tooltip.c_str());
    }

    ImGui::SameLine();

    /*
     * FLAC.
     *
     * Lossless, so there is nothing to choose and nothing to get wrong - the
     * file is the mix. It is what an archive or a hand-off to somebody else
     * wants, and a chiptune compresses extremely well: square waves are
     * about as predictable as audio gets, so the file is often a third the
     * size of the WAV.
     */
    if (ImGui::Button("FLAC", ImVec2(50, 25))) {
        std::string path = saveFileDialog("FLAC Files (*.flac)\0*.flac\0", "flac");
        if (!path.empty()) {
            if (path.find(".flac") == std::string::npos) path += ".flac";
            std::string message;
            exportFlac(project, seq, path, exportDuration, &message);
            exportStatus = message;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Lossless, and about a third the size of the WAV - square waves\n"
            "compress extremely well.\n\n"
            "Rendered at 24 bits, because a lossless format that threw away\n"
            "eight bits on the way in would be lossless about the wrong\n"
            "thing.\n\n"
            "Needs ffmpeg on the PATH, and nothing else.");
    }

    ImGui::SameLine();

    if (ImGui::Button("MIDI", ImVec2(50, 25))) {
        std::string path = saveFileDialog(
            "MIDI Files (*.mid)\0*.mid\0",
            "mid");
        if (!path.empty()) {
            // Ensure .mid extension
            if (path.find(".mid") == std::string::npos) {
                path += ".mid";
            }
            bool success = exportProjectToMIDI(project, path);
            if (success) {
                exportStatus = "MIDI exported successfully!";
            } else {
                exportStatus = "MIDI export failed!";
            }
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Export to MIDI file for use in other DAWs");

    /*
     * And back the other way.
     *
     * Beside the export button rather than in a menu, because the pair is
     * the point: a part written somewhere else, brought in, given a chip
     * voice. Everything it needs has been in the build all along - the MIDI
     * library was already vendored and linked - which is why the browser
     * lists .mid files it could not open.
     */
    ImGui::SameLine();
    if (ImGui::Button("Import MIDI", ImVec2(90, 25))) {
        const std::string path = openFileDialog(
            "MIDI Files\0*.mid;*.midi\0All files\0*.*\0", "mid");
        if (!path.empty()) {
            g_UndoHistory.saveState(project, "Import MIDI");
            const MidiImportReport report = importMidiFile(path, project);
            exportStatus = report.summary();
            if (report.ok) {
                // The notes changed, so everything downstream of them has to
                // be told - and an imported project is untrusted input in
                // exactly the way a loaded file is.
                clampProjectToValidRanges(project);
                seq.updateChannelConfigs();
                seq.setPosition(0.0f);
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Read a MIDI file in as patterns - one per track, one track per\n"
            "channel.\n\n"
            "Notes, timing, length and velocity come across. Instruments do\n"
            "not: a MIDI file has no sound in it, only a program number that\n"
            "means something to a General MIDI synth and nothing here. So\n"
            "your channels keep the voices you gave them.\n\n"
            "Replaces the patterns and the arrangement. Undo brings them\n"
            "back.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Stems", ImVec2(58, 25))) {
        // One WAV per channel, each rendered with the others muted - what
        // anyone opening them in another DAW expects by "stems".
        std::string path = saveFileDialog("Folder\0*.*\0", "");
        if (!path.empty()) {
            // The dialog returns a file path; use its folder.
            const size_t slash = path.find_last_of("/\\");
            const std::string dir = (slash == std::string::npos)
                                    ? path : path.substr(0, slash);

            calcDefaultDuration();
            StemExportResult result = exportStems(project, seq, dir, exportDuration, true);

            if (!result.failures.empty()) {
                exportStatus = "Stem export failed: " + result.failures.front();
            } else if (result.written == 0) {
                exportStatus = "Nothing to export - every channel is silent";
            } else {
                exportStatus = "Exported " + std::to_string(result.written) +
                               " stems" +
                               (result.skipped > 0
                                    ? " (" + std::to_string(result.skipped) + " silent skipped)"
                                    : "");
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Export one WAV per channel, each with the others muted.\n"
                          "Silent channels are skipped. Pick any file in the target\n"
                          "folder - only the folder is used.");
    }

    // WAV Export popup
    if (showExportPopup) {
        ImGui::OpenPopup("Export WAV");
    }

    if (ImGui::BeginPopupModal("Export WAV", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Export audio to WAV file");
        ImGui::Separator();

        ImGui::SetNextItemWidth(150);
        ImGui::DragFloat("Duration (beats)", &exportDuration, 1.0f, 1.0f, 256.0f, "%.0f");

        float durationSec = exportDuration * 60.0f / project.bpm;
        ImGui::Text("Duration: %.1f seconds at %.0f BPM", durationSec, project.bpm);

        /*
         * How much resolution to keep.
         *
         * 16 for anything going out to be listened to - it is the CD format
         * and every player reads it. 24 for anything going somewhere else to
         * be worked on, because it leaves 48 dB more room under the noise
         * floor, and because the dither that makes 16 bits behave is the
         * wrong thing to add to a file that has more processing ahead of it.
         */
        static int wavDepth = 0;
        ImGui::SetNextItemWidth(150);
        ImGui::Combo("Bit depth", &wavDepth, "16-bit (CD)\0" "24-bit (studio)\0");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "16-bit is what a player expects, and is dithered on the way\n"
                "down so a long fade does not break into steps.\n\n"
                "24-bit is undithered and keeps 48 dB more room below the\n"
                "noise floor - use it if the file is going anywhere else to\n"
                "be processed.");
        }

        ImGui::Separator();

        if (ImGui::Button("Export", ImVec2(100, 0))) {
            std::string path = saveFileDialog(
                "WAV Audio (*.wav)\0*.wav\0",
                "wav");
            if (!path.empty()) {
                const WavBitDepth depth = (wavDepth == 1)
                    ? WavBitDepth::TwentyFour : WavBitDepth::Sixteen;
                if (exportWav(project, seq, path, exportDuration, depth)) {
                    exportStatus = (wavDepth == 1) ? "Exported 24-bit WAV."
                                                   : "Exported 16-bit WAV.";
                } else {
                    exportStatus = "Export failed!";
                }
            }
            showExportPopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            showExportPopup = false;
            ImGui::CloseCurrentPopup();
        }

        if (!exportStatus.empty()) {
            ImGui::TextColored(
                exportStatus.find("failed") != std::string::npos
                    ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.3f, 1, 0.3f, 1),
                "%s", exportStatus.c_str());
        }

        ImGui::EndPopup();
    }

    // MP3 Export popup
    if (showMp3ExportPopup) {
        ImGui::OpenPopup("Export MP3");
    }

    if (ImGui::BeginPopupModal("Export MP3", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Export audio to MP3 file");
        ImGui::Separator();

        // Show encoder status
        std::string encoderStatus = getMp3EncoderStatus();
        bool hasEncoder = isLameAvailable() || isFFmpegAvailable();
        ImGui::TextColored(
            hasEncoder ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.5f, 0.3f, 1),
            "%s", encoderStatus.c_str());

        ImGui::Separator();

        ImGui::SetNextItemWidth(150);
        ImGui::DragFloat("Duration (beats)", &exportDuration, 1.0f, 1.0f, 256.0f, "%.0f");

        float durationSec = exportDuration * 60.0f / project.bpm;
        ImGui::Text("Duration: %.1f seconds at %.0f BPM", durationSec, project.bpm);

        ImGui::Spacing();

        ImGui::SetNextItemWidth(150);
        ImGui::SliderInt("Bitrate (kbps)", &mp3Bitrate, 128, 320);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher = better quality, larger file");

        ImGui::Separator();

        // Disable export button if no encoder
        if (!hasEncoder) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Export", ImVec2(100, 0))) {
            std::string path = saveFileDialog(
                "MP3 Audio (*.mp3)\0*.mp3\0",
                "mp3");
            if (!path.empty()) {
                if (exportMp3(project, seq, path, exportDuration, mp3Bitrate)) {
                    exportStatus = "MP3 export successful!";
                } else {
                    exportStatus = "MP3 export failed!";
                }
            }
            showMp3ExportPopup = false;
            ImGui::CloseCurrentPopup();
        }

        if (!hasEncoder) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            showMp3ExportPopup = false;
            ImGui::CloseCurrentPopup();
        }

        if (!hasEncoder) {
            ImGui::Spacing();
            ImGui::TextWrapped("Install LAME or FFmpeg and add to PATH to enable MP3 export.");
        }

        if (!exportStatus.empty() && exportStatus.find("MP3") != std::string::npos) {
            ImGui::TextColored(
                exportStatus.find("failed") != std::string::npos
                    ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.3f, 1, 0.3f, 1),
                "%s", exportStatus.c_str());
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // Show current file path
    if (!ui.projectFilePath.empty()) {
        // Extract just filename
        size_t lastSlash = ui.projectFilePath.find_last_of("/\\");
        std::string filename = (lastSlash != std::string::npos)
            ? ui.projectFilePath.substr(lastSlash + 1)
            : ui.projectFilePath;
        ImGui::TextDisabled("| %s", filename.c_str());
    }

    /*
     * The demos.
     *
     * Five engines, four equalisers and six reverb algorithms are all
     * reachable from a dropdown and invisible until somebody happens to
     * choose one. These are complete mastered pieces built around them, so
     * a person can hear what the program does inside a minute rather than
     * by poking at panels.
     */
    ImGui::Separator();
    ImGui::TextUnformatted("Demos");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Finished, mastered pieces showing what the engines sound like.\n"
            "Loading one replaces the project - Ctrl+Z brings yours back.");
    }

    for (int s = 0; s < int(Showcase::Count); ++s) {
        const Showcase which = static_cast<Showcase>(s);
        ImGui::PushID(4400 + s);
        if (ImGui::Button(showcaseName(which), ImVec2(150, 24))) {
            LoadShowcase(project, ui, seq, which);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", showcaseDescription(which));
        }
        ImGui::PopID();
        if ((s % 2) == 0) ImGui::SameLine();
    }

    ImGui::End();
}

// ============================================================================
// Piano Roll Editor - Full Featured
// ============================================================================
inline void DrawPianoRoll(Project& project, UIState& ui, Sequencer& seq) {
    // Set initial window position on first use (main center area)
    ImGui::SetNextWindowPos(ImVec2(220, 135), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Piano Roll", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

    if (ui.selectedPattern < 0 || ui.selectedPattern >= static_cast<int>(project.patterns.size())) {
        ImGui::Text("No pattern selected");
        ImGui::End();
        return;
    }

    Pattern& pattern = project.patterns[ui.selectedPattern];

    // ========================================================================
    // Toolbar Row 1: Pattern info and mode selection
    // ========================================================================
    ImGui::Text("Pattern: %s", pattern.name.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::DragInt("Length", &pattern.length, 1, 1, 9999);  // Essentially unlimited
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pattern length in beats (auto-extends as you add notes)");

    // Grid snap. This was hardcoded to a 1/16 note everywhere, which made
    // triplets - and so shuffle and 6/8 - impossible to write.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72);
    if (ImGui::BeginCombo("Snap", snapLabel(ui.snapDivision))) {
        for (int i = 0; i < static_cast<int>(SnapDivision::Count); ++i) {
            const SnapDivision division = static_cast<SnapDivision>(i);
            if (ImGui::Selectable(snapLabel(division), division == ui.snapDivision)) {
                ui.snapDivision = division;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Grid snap.\n"
                          "Hold Alt to place off the grid for one gesture.\n"
                          "[ and ] step through the divisions.");
    }

    // Ghost notes. A pattern only has neighbours once it is on the timeline,
    // because a channel is bound by the clip and not by the pattern - so the
    // toggle says why it has nothing to show rather than appearing broken.
    ImGui::SameLine();
    {
        bool ghostsOn = ui.showGhostNotes;
        if (ImGui::Checkbox("Ghosts", &ghostsOn)) {
            ui.showGhostNotes = ghostsOn;
        }
        if (ImGui::IsItemHovered()) {
            const size_t ghostCount =
                collectGhostNotes(project, ui.selectedPattern).size();
            bool placed = false;
            for (const Clip& clip : project.arrangement) {
                if (clip.patternIndex == ui.selectedPattern) { placed = true; break; }
            }

            if (!placed) {
                ImGui::SetTooltip(
                    "Show what the other channels play here (Alt+V).\n\n"
                    "This pattern is not on the timeline yet, so it has no\n"
                    "neighbours to show - a channel is decided by the clip\n"
                    "that places a pattern, not by the pattern itself.");
            } else {
                ImGui::SetTooltip(
                    "Show what the other channels play here (Alt+V).\n"
                    "%zu note(s) from other channels overlap this pattern.",
                    ghostCount);
            }
        }
    }

    // Zoom controls
    ImGui::SameLine(0, 15);
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::SliderFloat("##ZoomX", &ui.zoomX, 0.25f, 4.0f, "X:%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Horizontal zoom (Ctrl+Wheel)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::SliderFloat("##ZoomY", &ui.zoomY, 0.5f, 3.0f, "Y:%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vertical zoom (Ctrl+Shift+Wheel)");
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        ui.zoomX = 1.0f;
        ui.zoomY = 1.0f;
        ui.scrollX = 0.0f;
        ui.scrollY = 0.0f;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset zoom and scroll to default");

    ImGui::SameLine(0, 20);
    ImGui::Text("Mode:");
    ImGui::SameLine();

    // Mode buttons with visual indication
    bool isSelect = (ui.pianoRollMode == PianoRollMode::Select);
    bool isDraw = (ui.pianoRollMode == PianoRollMode::Draw);
    bool isErase = (ui.pianoRollMode == PianoRollMode::Erase);

    // The active mode is marked with the theme's own accent rather than a
    // hardcoded green/blue, so the toolbar belongs to whatever theme is on.
    // Erase keeps red: it is the destructive mode, and red earns its meaning
    // by being reserved for exactly that.
    //
    // The label needs a colour picked against the fill, not the theme's Text.
    // On Matrix the accent and the text are both bright green, so an active
    // button was a green rectangle with an invisible label.
    auto pushActiveMode = [](const ImVec4& fill) {
        ImGui::PushStyleColor(ImGuiCol_Button, fill);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fill);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, fill);
        const float lum = 0.2126f * fill.x + 0.7152f * fill.y + 0.0722f * fill.z;
        ImGui::PushStyleColor(ImGuiCol_Text, lum > 0.55f ? ImVec4(0.05f, 0.05f, 0.07f, 1.0f)
                                                         : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    };

    const ImVec4 modeAccent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    const ImVec4 eraseRed(0.80f, 0.22f, 0.22f, 1.0f);

    if (isSelect) pushActiveMode(modeAccent);
    if (ImGui::Button("Select (S)")) ui.pianoRollMode = PianoRollMode::Select;
    if (isSelect) ImGui::PopStyleColor(4);

    ImGui::SameLine();
    if (isDraw) pushActiveMode(modeAccent);
    if (ImGui::Button("Draw (D)")) ui.pianoRollMode = PianoRollMode::Draw;
    if (isDraw) ImGui::PopStyleColor(4);

    ImGui::SameLine();
    if (isErase) pushActiveMode(eraseRed);
    if (ImGui::Button("Erase (E)")) ui.pianoRollMode = PianoRollMode::Erase;
    if (isErase) ImGui::PopStyleColor(4);

    // ========================================================================
    // Toolbar Row 2: Actions
    // ========================================================================
    // Check if we have any selected notes (single or multiple)
    bool hasSelectedNote = (ui.selectedNoteIndex >= 0 &&
                           ui.selectedNoteIndex < static_cast<int>(pattern.notes.size()));
    bool hasMultiSelection = !ui.selectedNoteIndices.empty();
    bool hasAnySelection = hasSelectedNote || hasMultiSelection;

    // Show selection count, and what it is
    //
    // The chord name is here as well as on the right-click because a feature
    // that only exists on a right click is a feature nobody finds. This line
    // already had the space and was already about the selection.
    if (hasMultiSelection) {
        ImGui::Text("Selected: %d notes", static_cast<int>(ui.selectedNoteIndices.size()));

        if (ui.selectedNoteIndices.size() >= 2) {
            std::vector<int> pitches;
            for (int index : ui.selectedNoteIndices) {
                if (index >= 0 && index < static_cast<int>(pattern.notes.size())) {
                    pitches.push_back(pattern.notes[static_cast<size_t>(index)].pitch);
                }
            }
            const ChordIdentification id = identifyChord(pitches, 3);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.8f, 1.0f));
            ImGui::TextUnformatted(id.matches.empty() ? id.summary.c_str()
                                                      : id.matches[0].name.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                if (id.matches.empty()) {
                    ImGui::SetTooltip("%s", id.summary.c_str());
                } else {
                    ImGui::SetTooltip("%s\n\nRight-click the selection for the "
                                      "other readings.",
                                      id.matches[0].longName.c_str());
                }
            }
            if (ImGui::IsItemClicked()) ui.requestChordPopup = true;
        }

        ImGui::SameLine();
    }

    // Copy button - supports single and multi-selection
    if (!hasAnySelection) ImGui::BeginDisabled();
    if (ImGui::Button("Copy")) {
        g_NoteClipboard.clear();

        // Collect notes to copy
        std::vector<int> indicesToCopy;
        if (hasMultiSelection) {
            indicesToCopy = ui.selectedNoteIndices;
        } else if (hasSelectedNote) {
            indicesToCopy.push_back(ui.selectedNoteIndex);
        }

        if (!indicesToCopy.empty()) {
            // Find base time and pitch for relative positioning
            float minTime = 999999.0f;
            int minPitch = 999;
            for (int idx : indicesToCopy) {
                if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                    const Note& note = pattern.notes[idx];
                    g_NoteClipboard.push_back(note);
                    if (note.startTime < minTime) minTime = note.startTime;
                    if (note.pitch < minPitch) minPitch = note.pitch;
                }
            }
            g_ClipboardBaseTime = minTime;
            g_ClipboardBasePitch = minPitch;
        }
    }
    if (!hasAnySelection) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+C - Copy selected notes");

    ImGui::SameLine();

    // Paste button
    if (g_NoteClipboard.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Paste")) {
        if (!g_NoteClipboard.empty()) {
            // Save state for undo
            g_UndoHistory.saveState(project, "Paste Notes");

            float pasteTime = std::fmod(seq.getCurrentBeat(), static_cast<float>(pattern.length));
            ui.selectedNoteIndices.clear();

            for (const Note& clipNote : g_NoteClipboard) {
                Note newNote = clipNote;
                float offset = clipNote.startTime - g_ClipboardBaseTime;
                newNote.startTime = pasteTime + offset;
                while (newNote.startTime >= pattern.length) newNote.startTime -= pattern.length;
                while (newNote.startTime < 0) newNote.startTime += pattern.length;
                pattern.notes.push_back(newNote);
                ui.selectedNoteIndices.push_back(static_cast<int>(pattern.notes.size()) - 1);
            }
            if (!ui.selectedNoteIndices.empty()) {
                ui.selectedNoteIndex = ui.selectedNoteIndices[0];
            }
        }
    }
    if (g_NoteClipboard.empty()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+V - Paste at playhead");

    ImGui::SameLine();

    // Transpose. Applies to the selection, or the whole pattern when
    // nothing is selected, which is the common case for "move this idea".
    {
        auto targetIndices = [&]() {
            std::vector<int> targets = ui.selectedNoteIndices;
            if (targets.empty() && ui.selectedNoteIndex >= 0) {
                targets.push_back(ui.selectedNoteIndex);
            }
            if (targets.empty()) {
                targets.resize(pattern.notes.size());
                for (size_t i = 0; i < pattern.notes.size(); ++i) {
                    targets[i] = static_cast<int>(i);
                }
            }
            return targets;
        };

        auto doTranspose = [&](int semitones) {
            std::vector<int> targets = targetIndices();
            if (targets.empty()) return;
            g_UndoHistory.saveState(project, "Transpose");
            transposeNotes(pattern.notes, targets, semitones, true);
        };

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("-12##tr")) doTranspose(-12);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Down an octave");
        ImGui::SameLine();
        if (ImGui::Button("-1##tr")) doTranspose(-1);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Down a semitone.\n"
                              "Shift+Down does the same, Ctrl+Shift+Down an octave.\n"
                              "Applies to the selection, or the whole pattern.");
        }
        ImGui::SameLine();
        if (ImGui::Button("+1##tr")) doTranspose(1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up a semitone");
        ImGui::SameLine();
        if (ImGui::Button("+12##tr")) doTranspose(12);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up an octave");

        ImGui::SameLine();
        if (ImGui::Button("To Scale")) {
            std::vector<int> targets = targetIndices();
            if (!targets.empty()) {
                g_UndoHistory.saveState(project, "Snap To Scale");
                snapNotesToScale(pattern.notes, targets,
                                 g_ToolsScaleRoot, g_ToolsScaleType);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pull every note onto %s %s.\n"
                              "Pick the key in Tools > Scale Lock.",
                              noteName(g_ToolsScaleRoot), scaleName(g_ToolsScaleType));
        }

        // Mirror and retrograde existed tested in NoteTransforms.h since
        // 3.5.0 with no way to reach them - the C6 gap the roadmap audit
        // named. Cheap variations on a phrase, which is exactly what the
        // "make a variation" advice asks for.
        ImGui::SameLine();
        if (ImGui::Button("Mirror")) {
            std::vector<int> targets = targetIndices();
            if (!targets.empty()) {
                g_UndoHistory.saveState(project, "Mirror");
                // The selection's own middle, so the phrase stays in its
                // register instead of leaping to wherever C4 reflects it.
                int lo = 127, hi = 0;
                for (int index : targets) {
                    if (index < 0 || index >= static_cast<int>(pattern.notes.size())) continue;
                    lo = std::min(lo, pattern.notes[static_cast<size_t>(index)].pitch);
                    hi = std::max(hi, pattern.notes[static_cast<size_t>(index)].pitch);
                }
                invertNotes(pattern.notes, targets, (lo + hi) / 2);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Flip the selection upside down around its own\n"
                              "middle - a rising line becomes a falling one.\n"
                              "Applies to the selection, or the whole pattern.");
        }

        ImGui::SameLine();
        if (ImGui::Button("Reverse")) {
            std::vector<int> targets = targetIndices();
            if (targets.size() >= 2) {
                g_UndoHistory.saveState(project, "Reverse");
                reverseNotesInTime(pattern.notes, targets);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Play the selection backwards in time.\n"
                              "Pitches stay where they are.");
        }

        // The same flag the Tools panel's Scale Lock section sets - but
        // reachable from where drawing actually happens. It used to live
        // only inside a collapsed header in a background tab, under a
        // different name than its own header, and the lesson sent people
        // hunting for it there.
        ImGui::SameLine();
        ImGui::Checkbox("In Key", &g_ToolsScaleLock);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Notes you draw snap to %s %s.\n"
                              "Pick a different key in Tools > Scale Lock.",
                              noteName(g_ToolsScaleRoot), scaleName(g_ToolsScaleType));
        }
        if (g_ToolsScaleLock) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s %s", noteName(g_ToolsScaleRoot),
                                scaleName(g_ToolsScaleType));
        }
    }

    // Delete button - supports single and multi-selection
    if (!hasAnySelection) ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        // Save state for undo
        g_UndoHistory.saveState(project, "Delete Notes");

        // Collect indices to delete
        std::vector<int> indicesToDelete;
        if (hasMultiSelection) {
            indicesToDelete = ui.selectedNoteIndices;
        } else if (hasSelectedNote) {
            indicesToDelete.push_back(ui.selectedNoteIndex);
        }

        // Sort in descending order to delete from end first
        std::sort(indicesToDelete.begin(), indicesToDelete.end(), std::greater<int>());

        for (int idx : indicesToDelete) {
            if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                pattern.notes.erase(pattern.notes.begin() + idx);
            }
        }

        ui.selectedNoteIndex = -1;
        ui.selectedNoteIndices.clear();
    }
    if (!hasAnySelection) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete/Backspace");

    ImGui::SameLine();

    // Select All button
    if (pattern.notes.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Select All")) {
        ui.selectedNoteIndices.clear();
        for (size_t i = 0; i < pattern.notes.size(); ++i) {
            ui.selectedNoteIndices.push_back(static_cast<int>(i));
        }
        if (!ui.selectedNoteIndices.empty()) {
            ui.selectedNoteIndex = ui.selectedNoteIndices[0];
        }
    }
    if (pattern.notes.empty()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+A - Select all notes");

    ImGui::SameLine();

    // Clear all button with confirmation
    static bool showClearConfirm = false;
    if (ImGui::Button("Clear All")) {
        if (pattern.notes.empty()) {
            // Nothing to clear
        } else {
            showClearConfirm = true;
        }
    }

    // Clear confirmation popup
    if (showClearConfirm) {
        ImGui::OpenPopup("Confirm Clear");
    }
    if (ImGui::BeginPopupModal("Confirm Clear", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete all %zu notes in this pattern?", pattern.notes.size());
        ImGui::Separator();
        if (ImGui::Button("Yes, Clear All", ImVec2(120, 0))) {
            pattern.notes.clear();
            ui.selectedNoteIndex = -1;
            showClearConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showClearConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0, 15);

    // ========================================================================
    // Hi-Hat Roll Generator Tool
    // ========================================================================
    static bool showRollPopup = false;
    static float rollStartBeat = 0.0f;
    static float rollEndBeat = 4.0f;
    static int rollDensity = 2;  // 0=8th, 1=16th, 2=32nd
    static int rollHatType = 0;  // 0=closed, 1=open, 2=pedal
    static int rollVelocityMode = 0;  // 0=flat, 1=crescendo, 2=decrescendo
    static float rollVelocityStart = 0.8f;
    static float rollVelocityEnd = 0.8f;
    static int rollPitch = 42;  // Default hi-hat pitch

    if (ImGui::Button("Roll Gen")) {
        showRollPopup = true;
        rollStartBeat = std::fmod(seq.getCurrentBeat(), static_cast<float>(pattern.length));
        rollEndBeat = std::min(rollStartBeat + 2.0f, static_cast<float>(pattern.length));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Generate hi-hat rolls and fills");

    // Roll generator popup
    if (showRollPopup) {
        ImGui::OpenPopup("Roll Generator");
    }
    if (ImGui::BeginPopupModal("Roll Generator", &showRollPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Generate Hi-Hat Roll");
        ImGui::Separator();

        // Position controls
        ImGui::Text("Position:");
        ImGui::SliderFloat("Start (beat)##roll", &rollStartBeat, 0.0f, static_cast<float>(pattern.length) - 0.125f, "%.2f");
        ImGui::SliderFloat("End (beat)##roll", &rollEndBeat, rollStartBeat + 0.125f, static_cast<float>(pattern.length), "%.2f");

        // Duration presets
        ImGui::Text("Quick Length:");
        ImGui::SameLine();
        if (ImGui::SmallButton("1/4")) { rollEndBeat = std::min(rollStartBeat + 0.25f, static_cast<float>(pattern.length)); }
        ImGui::SameLine();
        if (ImGui::SmallButton("1/2")) { rollEndBeat = std::min(rollStartBeat + 0.5f, static_cast<float>(pattern.length)); }
        ImGui::SameLine();
        if (ImGui::SmallButton("1 beat")) { rollEndBeat = std::min(rollStartBeat + 1.0f, static_cast<float>(pattern.length)); }
        ImGui::SameLine();
        if (ImGui::SmallButton("2 beats")) { rollEndBeat = std::min(rollStartBeat + 2.0f, static_cast<float>(pattern.length)); }
        ImGui::SameLine();
        if (ImGui::SmallButton("4 beats")) { rollEndBeat = std::min(rollStartBeat + 4.0f, static_cast<float>(pattern.length)); }

        ImGui::Separator();

        // Density
        ImGui::Text("Density:");
        ImGui::RadioButton("8th##roll", &rollDensity, 0); ImGui::SameLine();
        ImGui::RadioButton("16th##roll", &rollDensity, 1); ImGui::SameLine();
        ImGui::RadioButton("32nd##roll", &rollDensity, 2); ImGui::SameLine();
        ImGui::RadioButton("64th##roll", &rollDensity, 3);

        ImGui::Separator();

        // Hi-hat type
        ImGui::Text("Hi-Hat Type:");
        ImGui::RadioButton("Closed##hat", &rollHatType, 0); ImGui::SameLine();
        ImGui::RadioButton("Open##hat", &rollHatType, 1); ImGui::SameLine();
        ImGui::RadioButton("Pedal##hat", &rollHatType, 2);

        // Pitch
        ImGui::SliderInt("Pitch##roll", &rollPitch, 36, 84, "MIDI %d");

        ImGui::Separator();

        // Velocity
        ImGui::Text("Velocity Mode:");
        ImGui::RadioButton("Flat##vel", &rollVelocityMode, 0); ImGui::SameLine();
        ImGui::RadioButton("Crescendo##vel", &rollVelocityMode, 1); ImGui::SameLine();
        ImGui::RadioButton("Decrescendo##vel", &rollVelocityMode, 2);

        if (rollVelocityMode == 0) {
            ImGui::SliderFloat("Velocity##rollvel", &rollVelocityStart, 0.1f, 1.0f, "%.2f");
            rollVelocityEnd = rollVelocityStart;
        } else {
            ImGui::SliderFloat("Start Velocity##roll", &rollVelocityStart, 0.1f, 1.0f, "%.2f");
            ImGui::SliderFloat("End Velocity##roll", &rollVelocityEnd, 0.1f, 1.0f, "%.2f");
        }

        ImGui::Separator();

        // Preview info
        float stepSize = 0.5f;  // 8th notes
        if (rollDensity == 1) stepSize = 0.25f;  // 16th
        else if (rollDensity == 2) stepSize = 0.125f;  // 32nd
        else if (rollDensity == 3) stepSize = 0.0625f;  // 64th

        int numNotes = static_cast<int>((rollEndBeat - rollStartBeat) / stepSize);
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "Will generate %d notes", numNotes);

        ImGui::Separator();

        // Generate button
        if (ImGui::Button("Generate Roll", ImVec2(140, 30))) {
            // Save state for undo
            g_UndoHistory.saveState(project, "Generate Drum Roll");

            // Determine oscillator type
            OscillatorType hatOsc = OscillatorType::HiHat;
            if (rollHatType == 1) hatOsc = OscillatorType::HiHatOpen;
            else if (rollHatType == 2) hatOsc = OscillatorType::HiHatPedal;

            // Generate notes
            float t = rollStartBeat;
            int noteIndex = 0;
            ui.selectedNoteIndices.clear();

            while (t < rollEndBeat - 0.001f) {
                Note newNote;
                newNote.startTime = t;
                newNote.duration = stepSize * 0.8f;  // Slightly shorter than step
                newNote.pitch = rollPitch;
                newNote.oscillatorType = hatOsc;

                // Calculate velocity based on mode
                float progress = (numNotes > 1) ? static_cast<float>(noteIndex) / static_cast<float>(numNotes - 1) : 0.0f;
                if (rollVelocityMode == 0) {
                    newNote.velocity = rollVelocityStart;
                } else if (rollVelocityMode == 1) {
                    // Crescendo
                    newNote.velocity = rollVelocityStart + (rollVelocityEnd - rollVelocityStart) * progress;
                } else {
                    // Decrescendo
                    newNote.velocity = rollVelocityStart + (rollVelocityEnd - rollVelocityStart) * progress;
                }

                pattern.notes.push_back(newNote);
                ui.selectedNoteIndices.push_back(static_cast<int>(pattern.notes.size()) - 1);

                t += stepSize;
                noteIndex++;
            }

            if (!ui.selectedNoteIndices.empty()) {
                ui.selectedNoteIndex = ui.selectedNoteIndices[0];
            }

            showRollPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##roll", ImVec2(100, 30))) {
            showRollPopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine(0, 20);
    ImGui::Text("Notes: %zu", pattern.notes.size());

    ImGui::SameLine(0, 20);
    if (hasSelectedNote) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Selected: Note %d", ui.selectedNoteIndex);
    } else {
        ImGui::TextDisabled("No note selected");
    }

    ImGui::Separator();

    // ========================================================================
    // Piano roll canvas
    // ========================================================================
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y = std::max(canvasSize.y, 400.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Dimensions
    const float keyWidth = 60.0f;
    const float noteHeight = 16.0f * ui.zoomY;
    const float beatWidth = 40.0f * ui.zoomX;
    const int visibleOctaves = 6;
    const int lowestNote = 24;  // C1 (lower bass range)
    const int highestNote = lowestNote + visibleOctaves * 12;
    const float resizeHandleWidth = 8.0f;  // Width of resize handle at note edge
    const float scrollbarHeight = 14.0f;   // Reserve space for horizontal scrollbar

    // Effective drawing area (accounting for scrollbar)
    float effectiveCanvasHeight = canvasSize.y - scrollbarHeight;

    // Calculate dynamic grid width based on notes
    float maxNoteEnd = static_cast<float>(pattern.length);
    for (const Note& n : pattern.notes) {
        float noteEnd = n.startTime + n.duration;
        if (noteEnd > maxNoteEnd) maxNoteEnd = noteEnd;
    }
    // Add padding (4 beats) and round up to next measure
    float dynamicLength = std::ceil((maxNoteEnd + 4.0f) / project.beatsPerMeasure) * project.beatsPerMeasure;
    dynamicLength = std::max(dynamicLength, static_cast<float>(pattern.length));

    float gridHeight = (highestNote - lowestNote) * noteHeight;
    float gridWidth = dynamicLength * beatWidth;

    // Background. Every colour in the roll comes from the active theme's
    // palette - hardcoding them here is why the editor used to look identical
    // under all ten themes while only the window chrome changed.
    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        g_PianoRollColors.background);

    // Piano keys (clip to effective area above scrollbar)
    for (int note = lowestNote; note < highestNote; ++note) {
        float y = canvasPos.y + (highestNote - note - 1) * noteHeight - ui.scrollY;
        if (y < canvasPos.y - noteHeight || y > canvasPos.y + effectiveCanvasHeight) continue;

        int noteInOctave = note % 12;
        bool isBlack = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
                       noteInOctave == 8 || noteInOctave == 10);

        ImU32 keyColor = isBlack ? g_PianoRollColors.keyBlack : g_PianoRollColors.keyWhite;
        drawList->AddRectFilled(
            ImVec2(canvasPos.x, y),
            ImVec2(canvasPos.x + keyWidth, y + noteHeight),
            keyColor);

        // Key label
        if (noteInOctave == 0) {  // C notes
            drawList->AddText(ImVec2(canvasPos.x + 5, y + 2),
                IM_COL32(200, 200, 200, 255), noteToString(note).c_str());
        }

        // Horizontal grid line
        drawList->AddLine(
            ImVec2(canvasPos.x + keyWidth, y),
            ImVec2(canvasPos.x + keyWidth + gridWidth, y),
            g_PianoRollColors.gridLine);
    }

    // Beat grid lines with subdivisions (use dynamic length for extended grid)
    // Draw subdivisions: 16th notes (0.25), 8th notes (0.5), quarter notes (1.0), measures
    int gridBeats = static_cast<int>(dynamicLength);
    float subdivision = 0.25f;  // 16th note subdivisions
    int totalSubdivisions = static_cast<int>(dynamicLength / subdivision) + 1;

    for (int sub = 0; sub <= totalSubdivisions; ++sub) {
        float beatPos = sub * subdivision;
        float x = canvasPos.x + keyWidth + beatPos * beatWidth - ui.scrollX;
        if (x < canvasPos.x + keyWidth || x > canvasPos.x + canvasSize.x) continue;

        // Determine line style based on beat position
        ImU32 lineColor;
        float lineThickness = 1.0f;
        int beatInt = static_cast<int>(beatPos);
        float beatFrac = beatPos - beatInt;

        if (beatInt == pattern.length && beatFrac < 0.01f) {
            // Pattern end marker
            lineColor = g_PianoRollColors.gridLinePattern;
            lineThickness = 3.0f;
        } else if (beatFrac < 0.01f && beatInt % project.beatsPerMeasure == 0) {
            // Measure line (beat 0, 4, 8, ...) - brightest, thickest
            lineColor = g_PianoRollColors.gridLineMeasure;
            lineThickness = 2.0f;
        } else if (beatFrac < 0.01f) {
            // Whole beat (quarter note)
            lineColor = g_PianoRollColors.gridLine;
            lineThickness = 1.5f;
        } else if (std::abs(beatFrac - 0.5f) < 0.01f) {
            // Half beat (8th note) - medium brightness
            lineColor = IM_COL32(60, 60, 70, 255);
            lineThickness = 1.0f;
        } else {
            // Quarter beat subdivision (16th note) - dimmest, thinnest
            lineColor = IM_COL32(40, 40, 45, 255);
            lineThickness = 1.0f;
        }

        drawList->AddLine(
            ImVec2(x, canvasPos.y),
            ImVec2(x, canvasPos.y + gridHeight),
            lineColor, lineThickness);
    }

    // ========================================================================
    // Cross-channel ghost notes
    //
    // What the other channels are playing while this pattern plays, drawn
    // faintly in their own channel colour. Off by default; Alt+V toggles.
    // Drawn before the real notes so it can never sit on top of them.
    // ========================================================================
    if (ui.showGhostNotes) {
        const std::vector<GhostNote> ghosts =
            collectGhostNotes(project, ui.selectedPattern);

        for (const GhostNote& ghost : ghosts) {
            if (ghost.pitch < lowestNote || ghost.pitch >= highestNote) continue;

            const float gx = canvasPos.x + keyWidth + ghost.startTime * beatWidth - ui.scrollX;
            const float gy = canvasPos.y + (highestNote - ghost.pitch - 1) * noteHeight - ui.scrollY;
            const float gw = ghost.duration * beatWidth;

            if (gx + gw < canvasPos.x + keyWidth || gx > canvasPos.x + canvasSize.x) continue;
            if (gy + noteHeight < canvasPos.y || gy > canvasPos.y + effectiveCanvasHeight) continue;

            const ImU32 base = channelColor(ghost.channelIndex);
            drawList->AddRectFilled(
                ImVec2(std::max(gx + 1, canvasPos.x + keyWidth), gy + 2),
                ImVec2(gx + gw - 1, gy + noteHeight - 2),
                widgets::withAlpha(base, 0.20f), 2.0f);
            drawList->AddRect(
                ImVec2(std::max(gx + 1, canvasPos.x + keyWidth), gy + 2),
                ImVec2(gx + gw - 1, gy + noteHeight - 2),
                widgets::withAlpha(base, 0.45f), 2.0f);
        }
    }

    // ========================================================================
    // Draw notes with selection highlight and fade indicators
    // ========================================================================
    for (size_t i = 0; i < pattern.notes.size(); ++i) {
        const Note& note = pattern.notes[i];
        if (note.pitch < lowestNote || note.pitch >= highestNote) continue;

        float x = canvasPos.x + keyWidth + note.startTime * beatWidth - ui.scrollX;
        float y = canvasPos.y + (highestNote - note.pitch - 1) * noteHeight - ui.scrollY;
        float w = note.duration * beatWidth;

        // Skip if off-screen (clip to effective area above scrollbar)
        if (x + w < canvasPos.x + keyWidth || x > canvasPos.x + canvasSize.x) continue;
        if (y + noteHeight < canvasPos.y || y > canvasPos.y + effectiveCanvasHeight) continue;

        bool isSelected = (static_cast<int>(i) == ui.selectedNoteIndex);
        // Also check multi-selection
        if (!isSelected) {
            for (int selIdx : ui.selectedNoteIndices) {
                if (selIdx == static_cast<int>(i)) {
                    isSelected = true;
                    break;
                }
            }
        }
        // Notes take the theme's note colour, tinted toward the channel's
        // identity colour. Every theme defines noteDefault/noteSelected and
        // nothing was reading them, so notes stayed the same eight colours
        // under all ten themes - jarring on Game Boy, invisible on the light
        // ones. The channel tint keeps multi-channel work legible.
        // Only a hint of the channel colour. At 0.35 it dominated the
        // theme's own note colour - orange notes on a four-shade olive
        // Game Boy - and the piano roll only ever shows one channel
        // anyway, so identity is doing little work here.
        ImU32 noteColor = blendColors(g_PianoRollColors.noteDefault,
                                      channelColor(ui.selectedChannel), 0.18f);

        if (isSelected) {
            noteColor = g_PianoRollColors.noteSelected;
        }

        // Note rectangle
        drawList->AddRectFilled(
            ImVec2(x, y + 1),
            ImVec2(x + w - 1, y + noteHeight - 1),
            noteColor);

        // Selection border
        if (isSelected) {
            drawList->AddRect(
                ImVec2(x - 1, y),
                ImVec2(x + w, y + noteHeight),
                IM_COL32(255, 200, 0, 255), 0.0f, 0, 2.0f);

            // Resize handle indicator (right edge)
            drawList->AddRectFilled(
                ImVec2(x + w - resizeHandleWidth, y + 1),
                ImVec2(x + w - 1, y + noteHeight - 1),
                IM_COL32(255, 150, 0, 200));
        }

        // Fade in indicator (gradient on left)
        if (note.fadeIn > 0.0f) {
            float fadeWidth = note.fadeIn * beatWidth;
            fadeWidth = std::min(fadeWidth, w * 0.5f);
            for (int fi = 0; fi < static_cast<int>(fadeWidth); fi += 2) {
                float alpha = static_cast<float>(fi) / fadeWidth;
                drawList->AddLine(
                    ImVec2(x + fi, y + 2),
                    ImVec2(x + fi, y + noteHeight - 2),
                    IM_COL32(0, 0, 0, static_cast<int>((1.0f - alpha) * 150)));
            }
        }

        // Fade out indicator (gradient on right)
        if (note.fadeOut > 0.0f) {
            float fadeWidth = note.fadeOut * beatWidth;
            fadeWidth = std::min(fadeWidth, w * 0.5f);
            for (int fi = 0; fi < static_cast<int>(fadeWidth); fi += 2) {
                float alpha = static_cast<float>(fi) / fadeWidth;
                drawList->AddLine(
                    ImVec2(x + w - fi - 1, y + 2),
                    ImVec2(x + w - fi - 1, y + noteHeight - 2),
                    IM_COL32(0, 0, 0, static_cast<int>((1.0f - alpha) * 150)));
            }
        }

        // Velocity indicator (brightness bar at bottom)
        drawList->AddRectFilled(
            ImVec2(x + 1, y + noteHeight - 4),
            ImVec2(x + 1 + (w - 2) * note.velocity, y + noteHeight - 2),
            IM_COL32(255, 255, 255, 120));
    }

    // Draw box selection rectangle
    if (ui.isBoxSelecting) {
        float minBeat = std::min(ui.boxSelectStartX, ui.boxSelectEndX);
        float maxBeat = std::max(ui.boxSelectStartX, ui.boxSelectEndX);
        int minPitch = static_cast<int>(std::min(ui.boxSelectStartY, ui.boxSelectEndY));
        int maxPitch = static_cast<int>(std::max(ui.boxSelectStartY, ui.boxSelectEndY));

        float boxX1 = canvasPos.x + keyWidth + minBeat * beatWidth - ui.scrollX;
        float boxX2 = canvasPos.x + keyWidth + maxBeat * beatWidth - ui.scrollX;
        float boxY1 = canvasPos.y + (highestNote - maxPitch - 1) * noteHeight - ui.scrollY;
        float boxY2 = canvasPos.y + (highestNote - minPitch) * noteHeight - ui.scrollY;

        // Selection box fill
        drawList->AddRectFilled(
            ImVec2(boxX1, boxY1),
            ImVec2(boxX2, boxY2),
            IM_COL32(100, 150, 255, 50));

        // Selection box border
        drawList->AddRect(
            ImVec2(boxX1, boxY1),
            ImVec2(boxX2, boxY2),
            IM_COL32(100, 150, 255, 200), 0.0f, 0, 2.0f);
    }

    // ========================================================================
    // Draw paste preview (ghost notes) if in paste preview mode
    // ========================================================================
    if (ui.isPastePreviewing && !g_NoteClipboard.empty()) {
        // Calculate mouse position for ghost notes
        ImVec2 mousePos = ImGui::GetMousePos();
        float ghostRelX = mousePos.x - canvasPos.x - keyWidth + ui.scrollX;
        float ghostRelY = mousePos.y - canvasPos.y + ui.scrollY;
        int ghostBaseNote = highestNote - 1 - static_cast<int>(ghostRelY / noteHeight);
        float ghostBaseBeat = snapBeat(ghostRelX / beatWidth, effectiveSnap(ui), project);  // Snap to 1/4 beat

        // Calculate pitch offset from clipboard base
        int pitchOffset = ghostBaseNote - g_ClipboardBasePitch;

        // Draw each ghost note
        for (const Note& clipNote : g_NoteClipboard) {
            float timeOffset = clipNote.startTime - g_ClipboardBaseTime;
            float noteTime = ghostBaseBeat + timeOffset;
            int notePitch = clipNote.pitch + pitchOffset;

            // Skip if out of range
            if (notePitch < lowestNote || notePitch >= highestNote) continue;

            float x = canvasPos.x + keyWidth + noteTime * beatWidth - ui.scrollX;
            float y = canvasPos.y + (highestNote - notePitch - 1) * noteHeight - ui.scrollY;
            float w = clipNote.duration * beatWidth;

            // Ghost note style - semi-transparent with dashed border effect
            ImU32 ghostColor = IM_COL32(100, 200, 255, 100);  // Light blue, semi-transparent
            ImU32 ghostBorder = IM_COL32(100, 200, 255, 200);

            // Fill
            drawList->AddRectFilled(
                ImVec2(x, y + 1),
                ImVec2(x + w - 1, y + noteHeight - 1),
                ghostColor);

            // Border
            drawList->AddRect(
                ImVec2(x, y),
                ImVec2(x + w, y + noteHeight),
                ghostBorder, 0.0f, 0, 2.0f);
        }

        // Draw helper text
        drawList->AddText(
            ImVec2(canvasPos.x + keyWidth + 10, canvasPos.y + 10),
            IM_COL32(100, 200, 255, 255),
            "Click to place | Escape to cancel | Right-click to cancel");
    }

    // ========================================================================
    // Draw pattern preview (ghost notes) if in pattern preview mode
    // ========================================================================
    if (g_IsPatternPreviewing && g_PreviewPatternIndex >= 0 && g_PreviewPatternIndex < g_NumDrumPatterns) {
        const DrumPattern& dp = g_DrumPatterns[g_PreviewPatternIndex];

        // Calculate mouse position for ghost notes
        ImVec2 mousePos = ImGui::GetMousePos();
        float ghostRelX = mousePos.x - canvasPos.x - keyWidth + ui.scrollX;
        float ghostRelY = mousePos.y - canvasPos.y + ui.scrollY;
        float ghostBaseBeat = snapBeat(ghostRelX / beatWidth, effectiveSnap(ui), project);  // Snap to 1/4 beat
        if (ghostBaseBeat < 0) ghostBaseBeat = 0;

        // Draw each ghost note from the pattern
        for (int i = 0; i < dp.noteCount; ++i) {
            const PatternNote& pn = dp.notes[i];
            float noteTime = ghostBaseBeat + pn.beat;
            int notePitch = pn.pitch;

            // Skip if out of visible range
            if (notePitch < lowestNote || notePitch >= highestNote) continue;

            float x = canvasPos.x + keyWidth + noteTime * beatWidth - ui.scrollX;
            float y = canvasPos.y + (highestNote - notePitch - 1) * noteHeight - ui.scrollY;
            float w = pn.duration * beatWidth;

            // Ghost note style - green tint for patterns
            ImU32 ghostColor = IM_COL32(100, 255, 150, 100);  // Light green, semi-transparent
            ImU32 ghostBorder = IM_COL32(100, 255, 150, 200);

            // Fill
            drawList->AddRectFilled(
                ImVec2(x, y + 1),
                ImVec2(x + w - 1, y + noteHeight - 1),
                ghostColor);

            // Border
            drawList->AddRect(
                ImVec2(x, y),
                ImVec2(x + w, y + noteHeight),
                ghostBorder, 0.0f, 0, 2.0f);
        }

        // Draw helper text with pattern name
        char helpText[128];
        snprintf(helpText, sizeof(helpText), "%s pattern | Click to place | Escape to cancel", dp.name);
        drawList->AddText(
            ImVec2(canvasPos.x + keyWidth + 10, canvasPos.y + 10),
            IM_COL32(100, 255, 150, 255),
            helpText);
    }

    // ========================================================================
    // Draw sample track preview (ghost notes) if in sample track preview mode
    // ========================================================================
    if (g_IsSampleTrackPreviewing && g_PreviewSampleTrackIndex >= 0 && g_PreviewSampleTrackIndex < g_NumSampleTracks) {
        const SampleTrack& st = g_SampleTracks[g_PreviewSampleTrackIndex];

        // Calculate mouse position for ghost notes
        ImVec2 mousePos = ImGui::GetMousePos();
        float ghostRelX = mousePos.x - canvasPos.x - keyWidth + ui.scrollX;
        float ghostBaseBeat = snapBeat(ghostRelX / beatWidth, effectiveSnap(ui), project);  // Snap to 1/4 beat
        if (ghostBaseBeat < 0) ghostBaseBeat = 0;

        // Draw each ghost note from the sample track
        for (int i = 0; i < st.noteCount; ++i) {
            const TrackNote& tn = st.notes[i];
            float noteTime = ghostBaseBeat + tn.beat;
            int notePitch = tn.pitch;

            // Skip if out of visible range
            if (notePitch < lowestNote || notePitch >= highestNote) continue;

            float x = canvasPos.x + keyWidth + noteTime * beatWidth - ui.scrollX;
            float y = canvasPos.y + (highestNote - notePitch - 1) * noteHeight - ui.scrollY;
            float w = tn.duration * beatWidth;

            // Ghost note style - purple/magenta tint for sample tracks
            ImU32 ghostColor = IM_COL32(200, 100, 255, 100);  // Light purple, semi-transparent
            ImU32 ghostBorder = IM_COL32(200, 100, 255, 200);

            // Fill
            drawList->AddRectFilled(
                ImVec2(x, y + 1),
                ImVec2(x + w - 1, y + noteHeight - 1),
                ghostColor);

            // Border
            drawList->AddRect(
                ImVec2(x, y),
                ImVec2(x + w, y + noteHeight),
                ghostBorder, 0.0f, 0, 2.0f);
        }

        // Draw helper text with track name
        char helpText[256];
        snprintf(helpText, sizeof(helpText), "%s (%s) - %d notes | Click to place | Escape to cancel",
                 st.name, st.genre, st.noteCount);
        drawList->AddText(
            ImVec2(canvasPos.x + keyWidth + 10, canvasPos.y + 10),
            IM_COL32(200, 100, 255, 255),
            helpText);

        // Also show suggested BPM
        char bpmText[64];
        snprintf(bpmText, sizeof(bpmText), "Suggested BPM: %d", st.bpm);
        drawList->AddText(
            ImVec2(canvasPos.x + keyWidth + 10, canvasPos.y + 26),
            IM_COL32(200, 100, 255, 200),
            bpmText);
    }

    // Playhead (with latency compensation for audio buffer)
    // Audio buffer is 512 frames * 2 (double buffer) at 44100 Hz = ~23ms
    // Convert to beats: latency_seconds * (BPM / 60)
    float latencyCompensation = (512.0f * 2.0f / 44100.0f) * (project.bpm / 60.0f);
    float compensatedBeat = seq.getCurrentBeat() - latencyCompensation;
    if (compensatedBeat < 0) compensatedBeat += pattern.length;  // Wrap around
    float playheadX = canvasPos.x + keyWidth +
        std::fmod(compensatedBeat, static_cast<float>(pattern.length)) * beatWidth - ui.scrollX;
    if (playheadX >= canvasPos.x + keyWidth && playheadX <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(
            ImVec2(playheadX, canvasPos.y),
            ImVec2(playheadX, canvasPos.y + gridHeight),
            g_PianoRollColors.playhead, 2.0f);
    }

    // ========================================================================
    // Handle mouse input
    // ========================================================================
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##pianoroll", canvasSize);

    // Keyboard shortcuts for modes
    if (ImGui::IsWindowFocused()) {
        bool ctrl = ImGui::GetIO().KeyCtrl;

        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) ui.pianoRollMode = PianoRollMode::Select;
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) ui.pianoRollMode = PianoRollMode::Draw;
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_E)) ui.pianoRollMode = PianoRollMode::Erase;

        // Delete selected notes
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            bool hasNotesToDelete = !ui.selectedNoteIndices.empty() ||
                (ui.selectedNoteIndex >= 0 && ui.selectedNoteIndex < static_cast<int>(pattern.notes.size()));

            if (hasNotesToDelete) {
                // Save state for undo before deleting
                g_UndoHistory.saveState(project, "Delete Notes");

                if (!ui.selectedNoteIndices.empty()) {
                    // Sort indices in descending order to delete from end first
                    std::sort(ui.selectedNoteIndices.begin(), ui.selectedNoteIndices.end(), std::greater<int>());
                    for (int idx : ui.selectedNoteIndices) {
                        if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                            pattern.notes.erase(pattern.notes.begin() + idx);
                        }
                    }
                    ui.selectedNoteIndices.clear();
                    ui.selectedNoteIndex = -1;
                } else if (ui.selectedNoteIndex >= 0 && ui.selectedNoteIndex < static_cast<int>(pattern.notes.size())) {
                    pattern.notes.erase(pattern.notes.begin() + ui.selectedNoteIndex);
                    ui.selectedNoteIndex = -1;
                }
            }
        }

        // Escape to deselect or cancel paste/pattern/sample track preview
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (ui.isPastePreviewing) {
                ui.isPastePreviewing = false;
            } else if (g_IsPatternPreviewing) {
                g_IsPatternPreviewing = false;
                g_PreviewPatternIndex = -1;
            } else if (g_IsSampleTrackPreviewing) {
                g_IsSampleTrackPreviewing = false;
                g_PreviewSampleTrackIndex = -1;
            } else {
                ui.selectedNoteIndex = -1;
                ui.selectedNoteIndices.clear();
            }
        }

        // Alt+V shows what the other channels are playing underneath.
        if (ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_V)) {
            ui.showGhostNotes = !ui.showGhostNotes;
        }

        // ---- Transpose -------------------------------------------------
        //
        // There was no transpose anywhere in this program - not on a
        // selection, not on a pattern, not on the song. Shift moves by a
        // semitone, Ctrl+Shift by an octave.
        {
            const bool shift = ImGui::GetIO().KeyShift;
            int transposeBy = 0;
            if (shift && ImGui::IsKeyPressed(ImGuiKey_UpArrow))   transposeBy = ctrl ? 12 : 1;
            if (shift && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) transposeBy = ctrl ? -12 : -1;

            if (transposeBy != 0) {
                std::vector<int> targets = ui.selectedNoteIndices;
                if (targets.empty() && ui.selectedNoteIndex >= 0) {
                    targets.push_back(ui.selectedNoteIndex);
                }
                if (!targets.empty()) {
                    g_UndoHistory.saveState(project, "Transpose");
                    transposeNotes(pattern.notes, targets, transposeBy, true);
                }
            }
        }

        // [ and ] step the grid division, the way every tracker does it
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
            ui.snapDivision = cycleSnap(ui.snapDivision, -1);
        }
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
            ui.snapDivision = cycleSnap(ui.snapDivision, 1);
        }

        // Undo (Ctrl+Z) - queued, see RequestUndo
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            RequestUndo();
        }

        // Redo (Ctrl+Y)
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            RequestRedo();
        }

        // Copy (Ctrl+C) - supports multiple selection
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            g_NoteClipboard.clear();

            // Collect notes to copy
            std::vector<int> indicesToCopy;
            if (!ui.selectedNoteIndices.empty()) {
                indicesToCopy = ui.selectedNoteIndices;
            } else if (ui.selectedNoteIndex >= 0 && ui.selectedNoteIndex < static_cast<int>(pattern.notes.size())) {
                indicesToCopy.push_back(ui.selectedNoteIndex);
            }

            if (!indicesToCopy.empty()) {
                // Find the earliest start time and lowest pitch for relative positioning
                float minTime = std::numeric_limits<float>::max();
                int minPitch = 127;

                for (int idx : indicesToCopy) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        const Note& note = pattern.notes[idx];
                        if (note.startTime < minTime) minTime = note.startTime;
                        if (note.pitch < minPitch) minPitch = note.pitch;
                    }
                }

                g_ClipboardBaseTime = minTime;
                g_ClipboardBasePitch = minPitch;

                // Copy all selected notes
                for (int idx : indicesToCopy) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        g_NoteClipboard.push_back(pattern.notes[idx]);
                    }
                }
            }
        }

        // Paste (Ctrl+V) - enter paste preview mode (ghost notes follow mouse)
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
            if (!g_NoteClipboard.empty() && !ui.isPastePreviewing) {
                ui.isPastePreviewing = true;
            }
        }

        // Cut (Ctrl+X) - copy then delete, supports multiple selection
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X)) {
            g_NoteClipboard.clear();

            // Collect notes to cut
            std::vector<int> indicesToCut;
            if (!ui.selectedNoteIndices.empty()) {
                indicesToCut = ui.selectedNoteIndices;
            } else if (ui.selectedNoteIndex >= 0 && ui.selectedNoteIndex < static_cast<int>(pattern.notes.size())) {
                indicesToCut.push_back(ui.selectedNoteIndex);
            }

            if (!indicesToCut.empty()) {
                // Save state for undo before cutting
                g_UndoHistory.saveState(project, "Cut Notes");

                // Find the earliest start time and lowest pitch for relative positioning
                float minTime = std::numeric_limits<float>::max();
                int minPitch = 127;

                for (int idx : indicesToCut) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        const Note& note = pattern.notes[idx];
                        if (note.startTime < minTime) minTime = note.startTime;
                        if (note.pitch < minPitch) minPitch = note.pitch;
                    }
                }

                g_ClipboardBaseTime = minTime;
                g_ClipboardBasePitch = minPitch;

                // Copy all selected notes to clipboard
                for (int idx : indicesToCut) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        g_NoteClipboard.push_back(pattern.notes[idx]);
                    }
                }

                // Delete notes (sort descending to delete from end first)
                std::sort(indicesToCut.begin(), indicesToCut.end(), std::greater<int>());
                for (int idx : indicesToCut) {
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        pattern.notes.erase(pattern.notes.begin() + idx);
                    }
                }

                ui.selectedNoteIndex = -1;
                ui.selectedNoteIndices.clear();
            }
        }

        // Select All (Ctrl+A)
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
            ui.selectedNoteIndices.clear();
            for (size_t i = 0; i < pattern.notes.size(); ++i) {
                ui.selectedNoteIndices.push_back(static_cast<int>(i));
            }
            if (!ui.selectedNoteIndices.empty()) {
                ui.selectedNoteIndex = ui.selectedNoteIndices[0];
            }
        }

    }

    // Handle drag and drop from Sound Palette
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OSC_TYPE")) {
            int oscType = *(const int*)payload->Data;

            ImVec2 mousePos = ImGui::GetMousePos();
            float relX = mousePos.x - canvasPos.x - keyWidth + ui.scrollX;
            float relY = mousePos.y - canvasPos.y + ui.scrollY;

            int droppedNote = highestNote - 1 - static_cast<int>(relY / noteHeight);
            float droppedBeat = relX / beatWidth;

            if (relX >= 0 && droppedNote >= lowestNote && droppedNote < highestNote) {
                Note newNote;
                newNote.pitch = std::clamp(droppedNote, lowestNote, highestNote - 1);
                newNote.startTime = snapBeat(droppedBeat, effectiveSnap(ui), project);
                newNote.oscillatorType = static_cast<OscillatorType>(oscType);  // Per-note oscillator

                // Drums auto-adjust duration based on BPM and selected duration variant
                if (isDrumType(newNote.oscillatorType)) {
                    float decayTime = getDrumDecayTime(newNote.oscillatorType);
                    newNote.duration = decayTime * (project.bpm / 60.0f) * g_SelectedDurationMult;
                } else {
                    newNote.duration = 0.5f;
                }

                newNote.velocity = 0.8f;
                pattern.notes.push_back(newNote);
                ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;

                // Auto-extend pattern length if note goes past current end
                float noteEnd = newNote.startTime + newNote.duration;
                if (noteEnd > pattern.length) {
                    pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Mouse position calculations
    ImVec2 mousePos = ImGui::GetMousePos();
    float relX = mousePos.x - canvasPos.x - keyWidth + ui.scrollX;
    float relY = mousePos.y - canvasPos.y + ui.scrollY;
    int hoveredNote = highestNote - 1 - static_cast<int>(relY / noteHeight);
    float hoveredBeat = relX / beatWidth;

    // Find note under cursor
    int noteUnderCursor = -1;
    bool onResizeHandle = false;
    for (size_t i = 0; i < pattern.notes.size(); ++i) {
        const Note& note = pattern.notes[i];
        float noteX = note.startTime * beatWidth;
        float noteW = note.duration * beatWidth;

        // A 1/16 note at default zoom is a few pixels wide, and a fixed
        // 8px handle measured inside it left nothing to click - the user's
        // exact words were "when I try, it just places another note". The
        // hit zone extends narrow notes to a minimum clickable width, and
        // the handle scales down so the note keeps a draggable body.
        const float hitW = std::max(noteW, 14.0f);
        if (hoveredNote == note.pitch &&
            relX >= noteX && relX < noteX + hitW) {
            noteUnderCursor = static_cast<int>(i);
            // Check if on resize handle (right edge)
            const float grabW = std::max(4.0f, std::min(resizeHandleWidth,
                                                        noteW * 0.5f));
            if (relX >= noteX + noteW - grabW) {
                onResizeHandle = true;
            }
            break;
        }
    }

    // Set cursor based on context
    if (onResizeHandle && (ui.pianoRollMode == PianoRollMode::Select ||
                           ui.pianoRollMode == PianoRollMode::Draw)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (ui.isDraggingNote) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    if (ImGui::IsItemHovered()) {
        // Handle paste preview placement (before regular mode handling)
        if (ui.isPastePreviewing && !g_NoteClipboard.empty()) {
            // Left click places the notes
            if (ImGui::IsMouseClicked(0) && relX >= 0) {
                // Save state for undo
                g_UndoHistory.saveState(project, "Paste Notes");

                // Calculate placement position
                float placeBeat = snapBeat(hoveredBeat, effectiveSnap(ui), project);
                int pitchOffset = hoveredNote - g_ClipboardBasePitch;

                // Clear selection and prepare to select pasted notes
                ui.selectedNoteIndices.clear();

                for (const Note& clipNote : g_NoteClipboard) {
                    Note newNote = clipNote;
                    float timeOffset = clipNote.startTime - g_ClipboardBaseTime;
                    newNote.startTime = placeBeat + timeOffset;
                    newNote.pitch = clipNote.pitch + pitchOffset;

                    // Clamp pitch to valid range
                    newNote.pitch = std::clamp(newNote.pitch, lowestNote, highestNote - 1);

                    // Keep time positive (no wrapping needed, grid extends dynamically)
                    if (newNote.startTime < 0) newNote.startTime = 0;

                    pattern.notes.push_back(newNote);
                    ui.selectedNoteIndices.push_back(static_cast<int>(pattern.notes.size()) - 1);

                    // Auto-extend pattern length if note goes past current end
                    float noteEnd = newNote.startTime + newNote.duration;
                    if (noteEnd > pattern.length) {
                        pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
                    }
                }

                // Select the first pasted note as primary
                if (!ui.selectedNoteIndices.empty()) {
                    ui.selectedNoteIndex = ui.selectedNoteIndices[0];

                    // Play preview sound for first pasted note
                    const Note& firstNote = pattern.notes[ui.selectedNoteIndex];
                    seq.previewNote(firstNote.pitch, firstNote.velocity, firstNote.oscillatorType);
                }

                // Exit paste preview mode
                ui.isPastePreviewing = false;
            }

            // Right click cancels paste preview
            if (ImGui::IsMouseClicked(1)) {
                ui.isPastePreviewing = false;
            }
        }
        // Handle pattern preview placement
        else if (g_IsPatternPreviewing && g_PreviewPatternIndex >= 0 && g_PreviewPatternIndex < g_NumDrumPatterns) {
            const DrumPattern& dp = g_DrumPatterns[g_PreviewPatternIndex];

            // Left click places the pattern
            if (ImGui::IsMouseClicked(0) && relX >= 0) {
                // Save state for undo
                g_UndoHistory.saveState(project, "Place Drum Pattern");

                // Calculate placement position (snap to 1/4 beat)
                float placeBeat = snapBeat(hoveredBeat, effectiveSnap(ui), project);
                if (placeBeat < 0) placeBeat = 0;

                // Clear selection and prepare to select placed notes
                ui.selectedNoteIndices.clear();

                // Add all notes from the pattern template
                for (int j = 0; j < dp.noteCount; ++j) {
                    const PatternNote& pn = dp.notes[j];
                    Note newNote;
                    newNote.pitch = pn.pitch;
                    newNote.startTime = placeBeat + pn.beat;
                    newNote.oscillatorType = pn.osc;
                    newNote.duration = pn.duration;
                    newNote.velocity = 0.8f;
                    pattern.notes.push_back(newNote);
                    ui.selectedNoteIndices.push_back(static_cast<int>(pattern.notes.size()) - 1);

                    // Auto-extend pattern length if needed
                    float noteEnd = newNote.startTime + newNote.duration;
                    if (noteEnd > pattern.length) {
                        pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
                    }
                }

                // Select the first placed note as primary
                if (!ui.selectedNoteIndices.empty()) {
                    ui.selectedNoteIndex = ui.selectedNoteIndices[0];
                }

                // Exit pattern preview mode
                g_IsPatternPreviewing = false;
                g_PreviewPatternIndex = -1;
            }

            // Right click cancels pattern preview
            if (ImGui::IsMouseClicked(1)) {
                g_IsPatternPreviewing = false;
                g_PreviewPatternIndex = -1;
            }
        }
        // Handle sample track preview placement
        else if (g_IsSampleTrackPreviewing && g_PreviewSampleTrackIndex >= 0 && g_PreviewSampleTrackIndex < g_NumSampleTracks) {
            const SampleTrack& st = g_SampleTracks[g_PreviewSampleTrackIndex];

            // Left click places the sample track
            if (ImGui::IsMouseClicked(0) && relX >= 0) {
                // Save state for undo
                g_UndoHistory.saveState(project, "Place Sample Track");

                // Calculate placement position
                float placeBeat;
                if (st.fixedPosition) {
                    // Fixed position tracks always start at beat 0
                    placeBeat = 0.0f;
                } else {
                    // Relative position tracks snap to 1/4 beat based on mouse position
                    placeBeat = snapBeat(hoveredBeat, effectiveSnap(ui), project);
                    if (placeBeat < 0) placeBeat = 0;
                }

                // Clear selection and prepare to select placed notes
                ui.selectedNoteIndices.clear();

                // Get genre-specific effects and apply to channel config
                GenreEffects genreFx = getGenreEffects(st.genre);
                auto& channelConfig = project.channels[ui.selectedChannel];

                // Apply ALL genre effects to channel config for authentic sound
                // Reverb
                channelConfig.reverbEnabled = genreFx.reverbEnabled;
                channelConfig.reverbMix = genreFx.reverbMix;
                channelConfig.reverbRoomSize = genreFx.reverbRoomSize;
                channelConfig.reverbDamping = genreFx.reverbDamping;
                // Chorus
                channelConfig.chorusEnabled = genreFx.chorusEnabled;
                channelConfig.chorusMix = genreFx.chorusMix;
                channelConfig.chorusRate = genreFx.chorusRate;
                // Delay
                channelConfig.delayEnabled = genreFx.delayEnabled;
                channelConfig.delayMix = genreFx.delayMix;
                channelConfig.delayTime = genreFx.delayTime;
                channelConfig.delayFeedback = genreFx.delayFeedback;
                // Stereo Widener (essential for synthwave)
                channelConfig.stereoWidenerEnabled = genreFx.stereoWidenerEnabled;
                channelConfig.stereoWidenerWidth = genreFx.stereoWidenerWidth;
                channelConfig.stereoWidenerHaas = genreFx.stereoWidenerHaas;
                channelConfig.stereoWidenerMix = genreFx.stereoWidenerMix;
                // Tape Saturation (analog warmth)
                channelConfig.tapeSaturationEnabled = genreFx.tapeSaturationEnabled;
                channelConfig.tapeDrive = genreFx.tapeDrive;
                channelConfig.tapeWarmth = genreFx.tapeWarmth;
                channelConfig.tapeCompression = genreFx.tapeCompression;
                channelConfig.tapeMix = genreFx.tapeMix;
                // Filter
                channelConfig.filterEnabled = genreFx.filterEnabled;
                channelConfig.filterType = genreFx.filterType;
                channelConfig.filterCutoff = genreFx.filterCutoff;
                channelConfig.filterResonance = genreFx.filterResonance;
                // Distortion
                channelConfig.distortionEnabled = genreFx.distortionEnabled;
                channelConfig.distortionType = genreFx.distortionType;
                channelConfig.distortionDrive = genreFx.distortionDrive;
                channelConfig.distortionMix = genreFx.distortionMix;
                // Bitcrusher
                channelConfig.bitcrusherEnabled = genreFx.bitcrusherEnabled;
                channelConfig.bitDepth = genreFx.bitDepth;
                channelConfig.sampleRateDiv = genreFx.sampleRateDiv;
                // Phaser
                channelConfig.phaserEnabled = genreFx.phaserEnabled;
                channelConfig.phaserRate = genreFx.phaserRate;
                channelConfig.phaserDepth = genreFx.phaserDepth;
                channelConfig.phaserFeedback = genreFx.phaserFeedback;
                // Tremolo
                channelConfig.tremoloEnabled = genreFx.tremoloEnabled;
                channelConfig.tremoloRate = genreFx.tremoloRate;
                channelConfig.tremoloDepth = genreFx.tremoloDepth;
                // Sidechain
                channelConfig.sidechainEnabled = genreFx.sidechainEnabled;
                channelConfig.sidechainAmount = genreFx.sidechainAmount;
                channelConfig.sidechainRelease = genreFx.sidechainRelease;
                // === NEW: Advanced Effects ===
                channelConfig.eqEnabled = genreFx.eqEnabled;
                channelConfig.eqLow = genreFx.eqLow;
                channelConfig.eqMid = genreFx.eqMid;
                channelConfig.eqHigh = genreFx.eqHigh;
                
                channelConfig.compressorEnabled = genreFx.compressorEnabled;
                channelConfig.compThreshold = genreFx.compThreshold;
                channelConfig.compRatio = genreFx.compRatio;
                channelConfig.compAttack = genreFx.compAttack;
                channelConfig.compRelease = genreFx.compRelease;
                channelConfig.compGain = genreFx.compGain;
                
                channelConfig.filterEnvEnabled = genreFx.filterEnvEnabled;
                channelConfig.filterEnvAmount = genreFx.filterEnvAmount;
                channelConfig.filterEnvAttack = genreFx.filterEnvAttack;
                channelConfig.filterEnvDecay = genreFx.filterEnvDecay;

                // Smart "Load Full Song" Logic
                // 1. Clear existing project data to start fresh
                project.patterns.clear();
                project.arrangement.clear();
                g_UndoHistory.clear();
                
                // 2. Setup Channels with meaningful names and defaults
                // Channel 0: Drums (Beat)
                project.channels[0].name = "Drums";
                project.channels[0].volume = 0.9f;
                project.channels[0].pan = 0.0f;
                // Channel 1: Bass (Low End)
                project.channels[1].name = "Bass";
                project.channels[1].volume = 0.85f;
                project.channels[1].pan = 0.0f;
                // Channel 2: Lead (Melody)
                project.channels[2].name = "Lead";
                project.channels[2].volume = 0.8f;
                project.channels[2].pan = 0.0f;
                // Channel 3: Pad/Chords (Harmony)
                project.channels[3].name = "Pad/Chords";
                project.channels[3].volume = 0.7f;
                project.channels[3].pan = 0.0f; // Wide stereo will be applied by effects
                // Channel 4: Extra/FX
                project.channels[4].name = "Extra";
                
                // 3. Create Patterns for each instrument group
                project.patterns.resize(5);
                project.patterns[0].name = "Drums Main";
                project.patterns[1].name = "Bassline";
                project.patterns[2].name = "Lead Melody";
                project.patterns[3].name = "Chords/Pad";
                project.patterns[4].name = "Extra";
                
                // 4. Apply Genre Effects to appropriate channels
                // GenreEffects genreFx = getGenreEffects(st.genre); // Already declared above
                
                // Apply specific FX logic
                // Drums (Ch 0): Compression, EQ
                project.channels[0].compressorEnabled = true;
                project.channels[0].compThreshold = 0.6f;
                project.channels[0].compRatio = 4.0f;
                project.channels[0].eqEnabled = true;
                project.channels[0].eqLow = 1.2f; // Boost kick
                project.channels[0].eqHigh = 1.1f; // Crisp hats
                
                // Bass (Ch 1): Mono, Sidechain from Drums
                project.channels[1].stereoWidenerEnabled = false; // Keep bass centered
                project.channels[1].sidechainEnabled = genreFx.sidechainEnabled;
                project.channels[1].sidechainSource = 0; // Duck when Drums play
                project.channels[1].sidechainAmount = genreFx.sidechainAmount;
                project.channels[1].sidechainRelease = genreFx.sidechainRelease;
                project.channels[1].eqEnabled = true;
                project.channels[1].eqLow = 1.1f;
                project.channels[1].filterEnvEnabled = genreFx.filterEnvEnabled;
                project.channels[1].filterEnvAmount = genreFx.filterEnvAmount;
                
                // Lead (Ch 2): Delay, Reverb
                applyGenreEffects(project.channels[2], genreFx); // Base genre FX
                project.channels[2].delayEnabled = true; // Leads usually need delay
                project.channels[2].sidechainEnabled = false; // Leads usually don't duck as hard
                
                // Pad (Ch 3): Stereo Width, Chorus, Reverb
                applyGenreEffects(project.channels[3], genreFx);
                project.channels[3].stereoWidenerEnabled = true;
                project.channels[3].stereoWidenerWidth = 0.8f;
                project.channels[3].sidechainEnabled = genreFx.sidechainEnabled; // Pads often duck
                project.channels[3].sidechainSource = 0;

                /*
                 * And the mix as a whole.
                 *
                 * Every channel above is carefully treated and the master
                 * bus was left completely untouched - no glue compression,
                 * no tone, no level staging. That is why these landed quiet
                 * and loose no matter how much care went into the parts:
                 * mastering is the stage that makes separate parts sound
                 * like one record, and there was not one.
                 */
                applyMastering(project, st.genre);

                // === MASTERING: Specific Tweaks for "Nightcall" ===
                if (strcmp(st.name, "Nightcall") == 0) {
                    // 1. Drums: Tight and Punchy
                    // Gated Reverb Snare trick
                    project.channels[0].reverbEnabled = true;
                    project.channels[0].reverbMix = 0.3f;
                    project.channels[0].reverbRoomSize = 0.4f;
                    project.channels[0].reverbDamping = 0.1f;
                    project.channels[0].compThreshold = 0.5f;
                    project.channels[0].compRatio = 4.0f;
                    project.channels[0].compGain = 1.4f;
                    
                    // 2. Bass: Driving Analog Pluck (Sawtooth based)
                    // Oscillator is now Sawtooth (set in loop below or default)
                    // Use Filter Env to create the pluck
                    project.channels[1].filterEnabled = true;
                    project.channels[1].filterType = 0; // Lowpass
                    project.channels[1].filterCutoff = 400.0f; // Low base
                    project.channels[1].filterResonance = 0.6f; // Resonant squelch
                    
                    project.channels[1].filterEnvEnabled = true;
                    project.channels[1].filterEnvAmount = 0.7f; // High modulation
                    project.channels[1].filterEnvAttack = 0.01f; // Instant
                    project.channels[1].filterEnvDecay = 0.25f; // Short pluck
                    
                    // Distortion for growl
                    project.channels[1].distortionEnabled = true;
                    project.channels[1].distortionType = 0; // Tanh
                    project.channels[1].distortionDrive = 3.0f; // Heavy drive
                    project.channels[1].distortionMix = 0.6f;
                    
                    // Sidechain
                    project.channels[1].sidechainEnabled = true;
                    project.channels[1].sidechainSource = 0;
                    project.channels[1].sidechainAmount = 0.8f; // Heavy pump
                    project.channels[1].sidechainRelease = 0.15f;

                    // 3. Lead: Talkbox/Vocoder Simulation (Sawtooth based)
                    // NEW: Use Formant Filter for authentic vowel sound
                    project.channels[2].formantEnabled = true;
                    project.channels[2].formantVowel = 3; // "O" sound (dark/hollow like Kavinsky)
                    project.channels[2].formantResonance = 6.0f;
                    
                    // EQ to shape the vocoder tone
                    project.channels[2].eqEnabled = true;
                    project.channels[2].eqLow = 0.5f;   // Cut low mud
                    project.channels[2].eqMid = 1.4f;   // Boost speech presence
                    project.channels[2].eqHigh = 0.8f;  // Soften highs
                    project.channels[2].eqMidFreq = 1000.0f;
                    
                    // Bitcrusher for digital grit
                    project.channels[2].bitcrusherEnabled = true;
                    project.channels[2].bitDepth = 12.0f;
                    project.channels[2].sampleRateDiv = 1.0f; // High fidelity but crushed
                    
                    // Saturation for warmth
                    project.channels[2].tapeSaturationEnabled = true;
                    project.channels[2].tapeDrive = 2.0f;
                    project.channels[2].tapeMix = 0.5f;
                    
                    // Distortion for edge
                    project.channels[2].distortionEnabled = true;
                    project.channels[2].distortionDrive = 2.0f;
                    project.channels[2].distortionMix = 0.4f;
                    
                    // Chorus for width
                    project.channels[2].chorusEnabled = true;
                    project.channels[2].chorusMix = 0.4f;
                    
                    // Compressor to flatten it
                    project.channels[2].compressorEnabled = true;
                    project.channels[2].compRatio = 8.0f; // Limiting
                    project.channels[2].compThreshold = 0.2f;
                    project.channels[2].compGain = 1.5f;

                    // 4. Pads: Background Atmosphere
                    project.channels[3].volume = 0.5f;
                    project.channels[3].reverbMix = 0.6f;
                    project.channels[3].stereoWidenerWidth = 1.0f;
                }
                else if (strcmp(st.name, "Resonance") == 0) {
                    // === HOME - Resonance (Chillwave/Synthwave) ===
                    
                    // 1. Drums: Lo-fi Crunch
                    project.channels[0].bitcrusherEnabled = true;
                    project.channels[0].bitDepth = 12.0f;
                    project.channels[0].tapeSaturationEnabled = true;
                    project.channels[0].tapeDrive = 1.5f;
                    
                    // 2. Bass: Resonant Squelch
                    // Needs to be punchy but warm
                    project.channels[1].filterEnabled = true;
                    project.channels[1].filterType = 0; // Lowpass
                    project.channels[1].filterCutoff = 600.0f;
                    project.channels[1].filterResonance = 0.7f; // High resonance
                    
                    project.channels[1].filterEnvEnabled = true;
                    project.channels[1].filterEnvAmount = 0.5f;
                    project.channels[1].filterEnvDecay = 0.4f;
                    
                    project.channels[1].chorusEnabled = true; // Wide bass (unusual but fits Resonance style)
                    project.channels[1].chorusMix = 0.3f;

                    // 3. Lead: Plucky and Delayed
                    project.channels[2].delayEnabled = true;
                    project.channels[2].delayTime = 0.35f;
                    project.channels[2].delayFeedback = 0.5f;
                    project.channels[2].reverbEnabled = true;
                    project.channels[2].reverbMix = 0.4f;
                    
                    // 4. Pads: Massive Phaser/Flanger wash
                    project.channels[3].phaserEnabled = true;
                    project.channels[3].phaserRate = 0.2f; // Slow sweep
                    project.channels[3].phaserDepth = 0.8f;
                    project.channels[3].stereoWidenerEnabled = true;
                    project.channels[3].stereoWidenerWidth = 1.0f;
                }
                else if (strcmp(st.name, "Comfort Chain") == 0) {
                    // === Instupendo - Comfort Chain (Lo-Fi) ===
                    
                    // Global Lo-Fi Vibe (simulated on channels)
                    
                    // Keys/Lead: The main focus - Warbly and muffled
                    project.channels[2].filterEnabled = true;
                    project.channels[2].filterCutoff = 1200.0f; // Muffled
                    
                    project.channels[2].vibratoEnabled = true; // Pitch warble (Tape wobble)
                    // Note: Vibrato is per-note usually, but we can set defaults here if we had channel-strip vibrato.
                    // Instead use Chorus with high depth/low rate for wobble
                    project.channels[2].chorusEnabled = true;
                    project.channels[2].chorusRate = 1.5f;
                    project.channels[2].chorusDepth = 0.008f;
                    project.channels[2].chorusMix = 1.0f; // Full wet for vibrato effect
                    
                    project.channels[2].tapeSaturationEnabled = true;
                    project.channels[2].tapeDrive = 2.0f; // Driven
                    project.channels[2].tapeWarmth = 0.8f; // Very warm/dark
                    
                    // Drums: Soft and distant
                    project.channels[0].eqEnabled = true;
                    project.channels[0].eqHigh = 0.5f; // Cut highs
                    project.channels[0].reverbEnabled = true;
                    project.channels[0].reverbMix = 0.3f;
                }
                else if (strcmp(st.genre, "Techno") == 0) {
                    // === TECHNO MASTERING ===
                    
                    // Rumble Bass
                    project.channels[1].reverbEnabled = true;
                    project.channels[1].reverbRoomSize = 0.8f;
                    project.channels[1].reverbDamping = 0.9f; // Dark rumble
                    project.channels[1].reverbMix = 0.4f;
                    project.channels[1].eqEnabled = true;
                    project.channels[1].eqLow = 1.5f; // Huge low end
                    
                    // Drums: Aggressive Distortion
                    project.channels[0].distortionEnabled = true;
                    project.channels[0].distortionDrive = 2.5f;
                    project.channels[0].distortionMix = 0.4f;
                    project.channels[0].compressorEnabled = true;
                    project.channels[0].compRatio = 8.0f;
                }
                else if (strcmp(st.genre, "Trap") == 0 || strcmp(st.genre, "Hip Hop") == 0) {
                    // === TRAP/HIP HOP MASTERING ===
                    
                    // 808 Bass: Distortion is key
                    project.channels[1].distortionEnabled = true;
                    project.channels[1].distortionType = 1; // Hard clip
                    project.channels[1].distortionDrive = 3.0f;
                    project.channels[1].distortionMix = 0.5f;
                    
                    // Hi-Hats (Lead/Extra): Brightness
                    project.channels[4].eqEnabled = true; // Extra often has hats in this mapping
                    project.channels[4].eqHigh = 1.5f;
                }
                
                // 5. Distribute Notes
                for (int j = 0; j < st.noteCount; ++j) {
                    const TrackNote& tn = st.notes[j];
                    Note newNote;
                    newNote.pitch = tn.pitch;
                    newNote.startTime = tn.beat;
                    newNote.oscillatorType = tn.osc;
                    newNote.duration = tn.duration;
                    newNote.velocity = tn.velocity;
                    newNote.vibrato = tn.vibrato;
                    newNote.vibratoSpeed = tn.vibratoSpeed;
                    
                    // Determine target pattern/channel based on instrument type
                    int targetIdx = 4; // Default to Extra
                    
                    if (isDrumType(tn.osc)) {
                        targetIdx = 0; // Drums
                    } else if (tn.osc == OscillatorType::SynthBass || tn.osc == OscillatorType::SynthwaveBass || 
                               tn.osc == OscillatorType::AcidBass || tn.osc == OscillatorType::SubBass808 || 
                               tn.osc == OscillatorType::Reese || tn.osc == OscillatorType::ReggaetonBass ||
                               tn.osc == OscillatorType::Triangle) { // Triangle often used as bass
                        targetIdx = 1; // Bass
                    } else if (tn.osc == OscillatorType::SynthPad || tn.osc == OscillatorType::SynthwavePad ||
                               tn.osc == OscillatorType::SynthwaveChord || tn.osc == OscillatorType::GatedPad ||
                               tn.osc == OscillatorType::SynthStrings || tn.osc == OscillatorType::RaveChord) {
                        targetIdx = 3; // Pad/Chords
                    } else {
                        targetIdx = 2; // Lead (everything else)
                    }
                    
                    // Add to specific pattern
                    project.patterns[targetIdx].notes.push_back(newNote);
                    
                    // Configure channel oscillator based on the first note found for that channel
                    // (Simple heuristic: assume channel uses one main sound, though notes can override)
                    if (project.patterns[targetIdx].notes.size() == 1 && !isDrumType(tn.osc)) {
                        project.channels[targetIdx].oscillator.type = tn.osc;
                    }
                }
                
                // 6. Arrange Clips
                // Add a clip for each non-empty pattern
                for (int i = 0; i < 5; ++i) {
                    if (!project.patterns[i].notes.empty()) {
                        Clip clip;
                        clip.patternIndex = i;
                        clip.channelIndex = i; // Channel 0-4
                        clip.startBeat = 0.0f;
                        clip.lengthBeats = static_cast<float>(st.lengthBeats);
                        project.arrangement.push_back(clip);
                        
                        // Set pattern length
                        project.patterns[i].length = st.lengthBeats;
                    }
                }
                
                // Set Song Properties
                project.bpm = static_cast<float>(st.bpm);
                project.songLength = static_cast<float>(st.lengthBeats);
                seq.setBPM(project.bpm);
                
                // Refresh view
                ui.selectedPattern = 0; // Select Drums by default
                ui.selectedChannel = 0;
                
                g_IsSampleTrackPreviewing = false;
                g_PreviewSampleTrackIndex = -1;
            }

            // Right click cancels sample track preview
            if (ImGui::IsMouseClicked(1)) {
                g_IsSampleTrackPreviewing = false;
                g_PreviewSampleTrackIndex = -1;
            }
        }
        // Handle mouse down (normal mode)
        else if (ImGui::IsMouseClicked(0) && relX >= 0) {
            switch (ui.pianoRollMode) {
                case PianoRollMode::Select:
                    if (noteUnderCursor >= 0) {
                        // Check if clicked note is part of existing multi-selection
                        bool isPartOfMultiSelection = false;
                        for (int idx : ui.selectedNoteIndices) {
                            if (idx == noteUnderCursor) {
                                isPartOfMultiSelection = true;
                                break;
                            }
                        }

                        // Check if this is a drum (drums can't be resized)
                        bool isDrumNote = isDrumType(pattern.notes[noteUnderCursor].oscillatorType);

                        if (onResizeHandle && !isDrumNote) {
                            // Save state for undo before resizing (drums can't resize)
                            g_UndoHistory.saveState(project, "Resize Note");

                            if (isPartOfMultiSelection && ui.selectedNoteIndices.size() > 1) {
                                // Start multi-resize: resize all selected notes
                                ui.isResizingMultiple = true;
                                ui.isResizingNote = false;
                                ui.dragStartBeat = hoveredBeat;

                                // Store original durations of all selected notes
                                ui.multiResizeStartDurations.clear();
                                for (int idx : ui.selectedNoteIndices) {
                                    ui.multiResizeStartDurations.push_back(pattern.notes[idx].duration);
                                }
                            } else {
                                // Single note resize
                                ui.selectedNoteIndex = noteUnderCursor;
                                ui.isResizingNote = true;
                                ui.isResizingMultiple = false;
                                ui.dragStartDuration = pattern.notes[noteUnderCursor].duration;
                                ui.dragStartBeat = hoveredBeat;
                            }
                        } else if (!onResizeHandle) {
                            // Store mouse position for drag threshold check
                            ImVec2 mousePos = ImGui::GetMousePos();
                            ui.pendingDragStartX = mousePos.x;
                            ui.pendingDragStartY = mousePos.y;

                            if (isPartOfMultiSelection && ui.selectedNoteIndices.size() > 1) {
                                // Set up pending multi-drag (will start after threshold)
                                ui.isPendingMultiDrag = true;
                                ui.dragAnchorBeat = hoveredBeat;
                                ui.dragAnchorPitch = hoveredNote;

                                // Pre-calculate offsets for when drag actually starts
                                ui.multiDragOffsets.clear();
                                for (int idx : ui.selectedNoteIndices) {
                                    const Note& note = pattern.notes[idx];
                                    float beatOffset = note.startTime - hoveredBeat;
                                    int pitchOffset = note.pitch - hoveredNote;
                                    ui.multiDragOffsets.push_back({beatOffset, pitchOffset});
                                }
                            } else {
                                // Set up pending single drag - select note but don't drag yet
                                ui.selectedNoteIndex = noteUnderCursor;
                                ui.selectedNoteIndices.clear();
                                ui.isPendingDrag = true;
                                ui.pendingDragNoteIndex = noteUnderCursor;
                                ui.dragStartBeat = pattern.notes[noteUnderCursor].startTime;
                                ui.dragStartPitch = pattern.notes[noteUnderCursor].pitch;
                            }
                        }
                    } else {
                        // Start box selection on empty space
                        ui.isBoxSelecting = true;
                        ui.boxSelectStartX = hoveredBeat;
                        ui.boxSelectStartY = static_cast<float>(hoveredNote);
                        ui.boxSelectEndX = hoveredBeat;
                        ui.boxSelectEndY = static_cast<float>(hoveredNote);
                        ui.selectedNoteIndex = -1;
                        ui.selectedNoteIndices.clear();
                    }
                    break;

                case PianoRollMode::Draw:
                    {
                        // The pencil never checked what was under it: a
                        // click on an existing note stamped another note on
                        // top. Every DAW's pencil resizes on the edge and
                        // moves on the body; drawing is for empty space.
                        if (noteUnderCursor >= 0) {
                            const bool drumNote =
                                isDrumType(pattern.notes[noteUnderCursor].oscillatorType);
                            if (onResizeHandle && !drumNote) {
                                g_UndoHistory.saveState(project, "Resize Note");
                                ui.selectedNoteIndex = noteUnderCursor;
                                ui.selectedNoteIndices.clear();
                                ui.isResizingNote = true;
                                ui.isResizingMultiple = false;
                                ui.dragStartDuration =
                                    pattern.notes[noteUnderCursor].duration;
                                ui.dragStartBeat = hoveredBeat;
                            } else {
                                ImVec2 mousePos = ImGui::GetMousePos();
                                ui.pendingDragStartX = mousePos.x;
                                ui.pendingDragStartY = mousePos.y;
                                ui.selectedNoteIndex = noteUnderCursor;
                                ui.selectedNoteIndices.clear();
                                ui.isPendingDrag = true;
                                ui.pendingDragNoteIndex = noteUnderCursor;
                                ui.dragStartBeat =
                                    pattern.notes[noteUnderCursor].startTime;
                                ui.dragStartPitch =
                                    pattern.notes[noteUnderCursor].pitch;
                            }
                            break;
                        }

                        // Save state for undo before adding note
                        g_UndoHistory.saveState(project, "Draw Note");

                        // Check if a chord is selected
                        if (g_SelectedChordIndex >= 0 && g_SelectedChordIndex < g_NumChordPresets) {
                            // Place chord notes
                            const ChordPreset& chord = g_ChordPresets[g_SelectedChordIndex];
                            int rootPitch = std::clamp(hoveredNote, lowestNote, highestNote - chord.intervals[chord.noteCount - 1]);
                            float startTime = snapBeat(hoveredBeat, effectiveSnap(ui), project);

                            ui.selectedNoteIndices.clear();

                            for (int i = 0; i < chord.noteCount; ++i) {
                                Note newNote;
                                newNote.pitch = std::clamp(rootPitch + chord.intervals[i], lowestNote, highestNote - 1);
                                newNote.startTime = startTime;
                                newNote.oscillatorType = chord.defaultOsc;
                                newNote.duration = 0.5f;  // Half beat for chords
                                newNote.velocity = 0.75f;

                                pattern.notes.push_back(newNote);
                                ui.selectedNoteIndices.push_back(static_cast<int>(pattern.notes.size()) - 1);

                                // Auto-extend pattern length
                                float noteEnd = newNote.startTime + newNote.duration;
                                if (noteEnd > pattern.length) {
                                    pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
                                }
                            }

                            // Select root note and preview chord
                            if (!ui.selectedNoteIndices.empty()) {
                                ui.selectedNoteIndex = ui.selectedNoteIndices[0];
                                // Play preview of root note
                                seq.previewNote(rootPitch, 0.75f, chord.defaultOsc);
                            }
                        } else {
                            // Single note mode (original behavior)
                            Note newNote;
                            newNote.pitch = std::clamp(hoveredNote, lowestNote, highestNote - 1);

                            // "Snap to Scale" set a flag that nothing read.
                            // This is the thing it always claimed to do.
                            if (g_ToolsScaleLock) {
                                newNote.pitch = std::clamp(
                                    snapToScale(newNote.pitch, g_ToolsScaleRoot, g_ToolsScaleType),
                                    lowestNote, highestNote - 1);
                            }

                            newNote.startTime = snapBeat(hoveredBeat, effectiveSnap(ui), project);

                            // Use selected palette item's oscillator type if one is selected
                            if (g_SelectedPaletteItem >= 0) {
                                newNote.oscillatorType = static_cast<OscillatorType>(g_SelectedPaletteItem);
                            }

                            // Set duration based on sound type - drums auto-adjust to BPM
                            if (isDrumType(newNote.oscillatorType)) {
                                float decayTime = getDrumDecayTime(newNote.oscillatorType);
                                // Apply duration multiplier from palette selection
                                newNote.duration = decayTime * (project.bpm / 60.0f) * g_SelectedDurationMult;
                            } else {
                                newNote.duration = 0.25f;
                            }

                            newNote.velocity = 0.8f;
                            pattern.notes.push_back(newNote);
                            ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;

                            // Play preview sound when note is placed
                            seq.previewNote(newNote.pitch, newNote.velocity, newNote.oscillatorType);

                            // Auto-extend pattern length if note goes past current end
                            float noteEnd = newNote.startTime + newNote.duration;
                            if (noteEnd > pattern.length) {
                                // Round up to next measure
                                pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
                            }
                        }
                    }
                    break;

                case PianoRollMode::Erase:
                    if (noteUnderCursor >= 0) {
                        // Save state for undo before erasing
                        g_UndoHistory.saveState(project, "Erase Note");

                        pattern.notes.erase(pattern.notes.begin() + noteUnderCursor);
                        if (ui.selectedNoteIndex == noteUnderCursor) {
                            ui.selectedNoteIndex = -1;
                        } else if (ui.selectedNoteIndex > noteUnderCursor) {
                            ui.selectedNoteIndex--;
                        }
                    }
                    break;
            }
        }

        // Handle dragging
        if (ImGui::IsMouseDown(0)) {
            // Check if pending drag should convert to actual drag (threshold exceeded)
            ImVec2 mousePos = ImGui::GetMousePos();
            float dragDistX = mousePos.x - ui.pendingDragStartX;
            float dragDistY = mousePos.y - ui.pendingDragStartY;
            float dragDist = std::sqrt(dragDistX * dragDistX + dragDistY * dragDistY);

            if (ui.isPendingDrag && dragDist > UIState::DRAG_THRESHOLD) {
                // Convert pending drag to actual drag
                g_UndoHistory.saveState(project, "Move Note");
                ui.isPendingDrag = false;
                ui.isDraggingNote = true;
            }
            if (ui.isPendingMultiDrag && dragDist > UIState::DRAG_THRESHOLD) {
                // Convert pending multi-drag to actual multi-drag
                g_UndoHistory.saveState(project, "Move Notes");
                ui.isPendingMultiDrag = false;
                ui.isDraggingMultiple = true;
            }

            if (ui.isDraggingNote && ui.selectedNoteIndex >= 0) {
                // Single note drag
                Note& note = pattern.notes[ui.selectedNoteIndex];
                float newBeat = snapBeat(hoveredBeat, effectiveSnap(ui), project);
                int newPitch = std::clamp(hoveredNote, lowestNote, highestNote - 1);
                note.startTime = std::max(0.0f, newBeat);
                note.pitch = newPitch;
            }
            if (ui.isDraggingMultiple && !ui.selectedNoteIndices.empty()) {
                // Multi-note drag - move all selected notes together
                float snappedBeat = snapBeat(hoveredBeat, effectiveSnap(ui), project);
                int currentPitch = hoveredNote;

                // Apply offset to each selected note
                for (size_t i = 0; i < ui.selectedNoteIndices.size() && i < ui.multiDragOffsets.size(); ++i) {
                    int idx = ui.selectedNoteIndices[i];
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        Note& note = pattern.notes[idx];
                        float beatOffset = ui.multiDragOffsets[i].first;
                        int pitchOffset = ui.multiDragOffsets[i].second;

                        float newBeat = snappedBeat + beatOffset;
                        int newPitch = currentPitch + pitchOffset;

                        note.startTime = std::max(0.0f, snapBeat(newBeat, effectiveSnap(ui), project));
                        note.pitch = std::clamp(newPitch, lowestNote, highestNote - 1);
                    }
                }
            }
            if (ui.isResizingNote && ui.selectedNoteIndex >= 0) {
                Note& note = pattern.notes[ui.selectedNoteIndex];
                // Drums can't be resized - they have fixed duration
                if (!isDrumType(note.oscillatorType)) {
                    float deltaBeats = hoveredBeat - ui.dragStartBeat;
                    float newDuration = ui.dragStartDuration + deltaBeats;
                    note.duration = snapDuration(newDuration, effectiveSnap(ui), project);
                }
            }
            // Handle multi-note resize
            if (ui.isResizingMultiple && !ui.selectedNoteIndices.empty()) {
                float deltaBeats = hoveredBeat - ui.dragStartBeat;

                // Apply delta to all selected notes (additive resize)
                for (size_t i = 0; i < ui.selectedNoteIndices.size() && i < ui.multiResizeStartDurations.size(); ++i) {
                    int idx = ui.selectedNoteIndices[i];
                    if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                        Note& note = pattern.notes[idx];
                        // Skip drums - they have fixed duration
                        if (!isDrumType(note.oscillatorType)) {
                            float newDuration = ui.multiResizeStartDurations[i] + deltaBeats;
                            note.duration = snapDuration(newDuration, effectiveSnap(ui), project);
                        }
                    }
                }
            }
            // Update box selection end point
            if (ui.isBoxSelecting) {
                ui.boxSelectEndX = hoveredBeat;
                ui.boxSelectEndY = static_cast<float>(hoveredNote);
            }
        }

        // Handle mouse release
        if (ImGui::IsMouseReleased(0)) {
            ui.isDraggingNote = false;
            ui.isDraggingMultiple = false;
            ui.isResizingNote = false;
            ui.isResizingMultiple = false;
            ui.isPendingDrag = false;
            ui.isPendingMultiDrag = false;
            ui.pendingDragNoteIndex = -1;
            ui.multiDragOffsets.clear();
            ui.multiResizeStartDurations.clear();

            // Complete box selection - find all notes in the box
            if (ui.isBoxSelecting) {
                ui.isBoxSelecting = false;
                ui.selectedNoteIndices.clear();

                float minBeat = std::min(ui.boxSelectStartX, ui.boxSelectEndX);
                float maxBeat = std::max(ui.boxSelectStartX, ui.boxSelectEndX);
                int minPitch = static_cast<int>(std::min(ui.boxSelectStartY, ui.boxSelectEndY));
                int maxPitch = static_cast<int>(std::max(ui.boxSelectStartY, ui.boxSelectEndY));

                for (size_t i = 0; i < pattern.notes.size(); ++i) {
                    const Note& note = pattern.notes[i];
                    float noteEnd = note.startTime + note.duration;

                    // Check if note overlaps with selection box
                    if (note.pitch >= minPitch && note.pitch <= maxPitch &&
                        noteEnd > minBeat && note.startTime < maxBeat) {
                        ui.selectedNoteIndices.push_back(static_cast<int>(i));
                    }
                }

                // Set single selection to first selected note (for compatibility)
                if (!ui.selectedNoteIndices.empty()) {
                    ui.selectedNoteIndex = ui.selectedNoteIndices[0];
                }
            }
        }

        /*
         * Right click: name the chord if there is one, otherwise delete.
         *
         * The delete has been the right-click for the whole life of this
         * program and it is muscle memory, so it is kept for the case it has
         * always covered - a right click on a note with nothing selected.
         * What changes is that a right click on a SELECTION now means "tell
         * me about this" rather than "throw one of these away", which is the
         * safer reading of the gesture anyway: deleting one note out of a
         * selected chord was never a thing anybody wanted.
         */
        if (ImGui::IsMouseClicked(1)) {
            if (ui.selectedNoteIndices.size() >= 2) {
                ui.requestChordPopup = true;
            } else if (noteUnderCursor >= 0) {
                pattern.notes.erase(pattern.notes.begin() + noteUnderCursor);
                if (ui.selectedNoteIndex == noteUnderCursor) {
                    ui.selectedNoteIndex = -1;
                } else if (ui.selectedNoteIndex > noteUnderCursor) {
                    ui.selectedNoteIndex--;
                }
            }
        }

        // Show hover info
        ImGui::BeginTooltip();
        ImGui::Text("%s (Beat %.2f)", noteToString(hoveredNote).c_str(), hoveredBeat);
        if (noteUnderCursor >= 0) {
            const Note& n = pattern.notes[noteUnderCursor];
            ImGui::Text("Note: %s, Dur: %.2f, Vel: %.0f%%",
                noteToString(n.pitch).c_str(), n.duration, n.velocity * 100);
        }
        ImGui::EndTooltip();
    } else {
        // Release drag if mouse leaves area
        if (ImGui::IsMouseReleased(0)) {
            ui.isDraggingNote = false;
            ui.isDraggingMultiple = false;
            ui.isResizingNote = false;
            ui.isResizingMultiple = false;
            ui.multiDragOffsets.clear();
            ui.multiResizeStartDurations.clear();
        }
    }

    /*
     * ---- What did I just play? -------------------------------------------
     *
     * OUTSIDE the hover branch, deliberately. A popup has to be submitted
     * every frame it is open, and the moment it appears the mouse is over
     * the popup rather than over the grid - so drawing it inside the
     * "hovering the canvas" block would close it on the frame after it
     * opened, which looks exactly like a right click that did nothing.
     *
     * Recomputed each frame rather than cached at open time. It is a dozen
     * arithmetic operations over a handful of notes, and it means the answer
     * follows the selection instead of going stale behind it.
     */
    if (ui.requestChordPopup) {
        ui.requestChordPopup = false;
        ImGui::OpenPopup("ChordIdentify");
    }

    if (ImGui::BeginPopup("ChordIdentify")) {
        std::vector<int> pitches;
        for (int index : ui.selectedNoteIndices) {
            if (index >= 0 && index < static_cast<int>(pattern.notes.size())) {
                pitches.push_back(pattern.notes[static_cast<size_t>(index)].pitch);
            }
        }

        const ChordIdentification id = identifyChord(pitches);

        ImGui::TextDisabled("%d note%s selected", static_cast<int>(pitches.size()),
                            pitches.size() == 1 ? "" : "s");
        ImGui::Separator();

        if (id.matches.empty()) {
            // One note or two. Naming {C, G} as "C major, no third" would be
            // inventing the note that decides major from minor, which is the
            // only thing anybody wants to know about it.
            ImGui::TextWrapped("%s", id.summary.c_str());
        } else {
            const ChordMatch& best = id.matches[0];

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.8f, 1.0f));
            ImGui::Text("%s", best.name.c_str());
            ImGui::PopStyleColor();
            ImGui::TextDisabled("%s", best.longName.c_str());

            /*
             * The honesty line. A symmetric chord has no single root - a
             * diminished 7th is equally itself on all four of its notes -
             * and an approximate match had to ignore something you played.
             * Either way the name above is less certain than it looks, and
             * the one place that must be said is next to it.
             */
            if (best.symmetric) {
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "Symmetric: this shape repeats, so any of its notes is "
                    "equally the root. The name below the line is one of "
                    "several equally correct readings.");
            } else if (!best.exact) {
                ImGui::Spacing();
                if (best.extra > 0) {
                    ImGui::TextWrapped("Approximate - %d note%s in the "
                                       "selection %s not part of it.",
                                       best.extra, best.extra == 1 ? "" : "s",
                                       best.extra == 1 ? "is" : "are");
                } else {
                    ImGui::TextWrapped("A note short of the full chord, which "
                                       "is normal - the 5th is the first note "
                                       "most voicings drop.");
                }
            }

            // Only readings that are as good as the best one. A chord
            // reached by ignoring a note you played is not a second opinion.
            const std::vector<ChordMatch> alternatives = alternativeReadings(id);
            if (!alternatives.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Also reads as");
                for (const ChordMatch& other : alternatives) {
                    ImGui::BulletText("%s", other.name.c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", other.longName.c_str());
                    }
                }
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Copy name")) {
            ImGui::SetClipboardText(id.matches.empty() ? id.summary.c_str()
                                                       : id.matches[0].name.c_str());
        }
        if (ImGui::MenuItem("Delete selected notes")) {
            // Right-clicking a selection no longer deletes, so the delete
            // that used to live on this gesture has to still be reachable
            // from it. Undo-able, which the old right-click delete never was.
            g_UndoHistory.saveState(project, "Delete Notes");
            std::vector<int> sorted = ui.selectedNoteIndices;
            std::sort(sorted.begin(), sorted.end(), std::greater<int>());
            for (int index : sorted) {
                if (index >= 0 && index < static_cast<int>(pattern.notes.size())) {
                    pattern.notes.erase(pattern.notes.begin() + index);
                }
            }
            ui.selectedNoteIndices.clear();
            ui.selectedNoteIndex = -1;
        }
        ImGui::EndPopup();
    }

    // Scroll and zoom handling
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift) {
            // Ctrl+Shift+Wheel = Vertical zoom
            ui.zoomY = std::clamp(ui.zoomY + wheel * 0.1f, 0.5f, 3.0f);
        } else if (ImGui::GetIO().KeyCtrl) {
            // Ctrl+Wheel = Horizontal zoom
            ui.zoomX = std::clamp(ui.zoomX + wheel * 0.1f, 0.25f, 4.0f);
        } else if (ImGui::GetIO().KeyShift) {
            // Shift+Wheel = Horizontal scroll
            ui.scrollX = std::max(0.0f, ui.scrollX - wheel * 50.0f);
        } else {
            // Wheel = Vertical scroll (account for scrollbar space)
            ui.scrollY = std::clamp(ui.scrollY - wheel * 30.0f, 0.0f, gridHeight - effectiveCanvasHeight);
        }
    }

    // Middle mouse button panning
    static bool isMiddleMousePanning = false;
    static ImVec2 panStartMousePos;
    static float panStartScrollX = 0.0f;
    static float panStartScrollY = 0.0f;

    if (ImGui::IsWindowHovered() || isMiddleMousePanning) {
        if (ImGui::IsMouseClicked(2)) {  // Middle mouse button
            isMiddleMousePanning = true;
            panStartMousePos = ImGui::GetMousePos();
            panStartScrollX = ui.scrollX;
            panStartScrollY = ui.scrollY;
        }
    }

    if (isMiddleMousePanning) {
        if (ImGui::IsMouseDown(2)) {
            ImVec2 currentMousePos = ImGui::GetMousePos();
            float deltaX = panStartMousePos.x - currentMousePos.x;
            float deltaY = panStartMousePos.y - currentMousePos.y;

            ui.scrollX = std::max(0.0f, panStartScrollX + deltaX);
            ui.scrollY = std::clamp(panStartScrollY + deltaY, 0.0f, std::max(0.0f, gridHeight - effectiveCanvasHeight));

            // Change cursor to indicate panning
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else {
            isMiddleMousePanning = false;
        }
    }

    // Clamp scrollX to valid range
    float maxScrollX = std::max(0.0f, gridWidth - (canvasSize.x - keyWidth));
    ui.scrollX = std::clamp(ui.scrollX, 0.0f, maxScrollX);

    // ========================================================================
    // Horizontal scrollbar (uses scrollbarHeight defined above)
    // ========================================================================
    float scrollableWidth = canvasSize.x - keyWidth;
    float contentWidth = gridWidth;

    if (contentWidth > scrollableWidth) {
        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + keyWidth, canvasPos.y + canvasSize.y - scrollbarHeight));

        // Calculate scrollbar thumb size and position
        float thumbRatio = scrollableWidth / contentWidth;
        float thumbWidth = std::max(30.0f, scrollableWidth * thumbRatio);
        float scrollRatio = ui.scrollX / maxScrollX;
        float thumbX = scrollRatio * (scrollableWidth - thumbWidth);

        // Draw scrollbar background
        drawList->AddRectFilled(
            ImVec2(canvasPos.x + keyWidth, canvasPos.y + canvasSize.y - scrollbarHeight),
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
            IM_COL32(30, 30, 35, 255));

        // Draw scrollbar thumb
        ImVec2 thumbMin(canvasPos.x + keyWidth + thumbX, canvasPos.y + canvasSize.y - scrollbarHeight + 2);
        ImVec2 thumbMax(canvasPos.x + keyWidth + thumbX + thumbWidth, canvasPos.y + canvasSize.y - 2);

        // Check if mouse is hovering over thumb
        ImVec2 mousePos = ImGui::GetMousePos();
        bool thumbHovered = mousePos.x >= thumbMin.x && mousePos.x <= thumbMax.x &&
                            mousePos.y >= thumbMin.y && mousePos.y <= thumbMax.y;

        static bool isDraggingScrollbar = false;
        static float dragStartX = 0.0f;
        static float dragStartScrollX = 0.0f;

        // Handle scrollbar dragging
        if (thumbHovered && ImGui::IsMouseClicked(0)) {
            isDraggingScrollbar = true;
            dragStartX = mousePos.x;
            dragStartScrollX = ui.scrollX;
        }

        if (isDraggingScrollbar) {
            if (ImGui::IsMouseDown(0)) {
                float deltaX = mousePos.x - dragStartX;
                float scrollDelta = (deltaX / (scrollableWidth - thumbWidth)) * maxScrollX;
                ui.scrollX = std::clamp(dragStartScrollX + scrollDelta, 0.0f, maxScrollX);
            } else {
                isDraggingScrollbar = false;
            }
        }

        // Click on scrollbar track to jump
        if (!isDraggingScrollbar && !thumbHovered &&
            mousePos.x >= canvasPos.x + keyWidth && mousePos.x <= canvasPos.x + canvasSize.x &&
            mousePos.y >= canvasPos.y + canvasSize.y - scrollbarHeight && mousePos.y <= canvasPos.y + canvasSize.y) {
            if (ImGui::IsMouseClicked(0)) {
                float clickRatio = (mousePos.x - canvasPos.x - keyWidth - thumbWidth / 2) / (scrollableWidth - thumbWidth);
                ui.scrollX = std::clamp(clickRatio * maxScrollX, 0.0f, maxScrollX);
            }
        }

        // Draw thumb with hover/active state
        ImU32 thumbColor = isDraggingScrollbar ? IM_COL32(120, 140, 180, 255) :
                           (thumbHovered ? IM_COL32(100, 120, 160, 255) : IM_COL32(70, 80, 100, 255));
        drawList->AddRectFilled(thumbMin, thumbMax, thumbColor, 4.0f);
    }

    ImGui::End();
}

// ============================================================================
// Tracker View
// ============================================================================
// A tracker row is a moment in the song, and each column asks what is
// playing on that channel at that moment. That is what a tracker has always
// been - the order list and the pattern data seen at once - and it is the
// only reading the data model supports, since a Pattern has no channel and
// neither does a Note. See TrackerGrid.h.
//
// The previous version searched the selected pattern for the first note whose
// start rounded to the step and printed it into all eight columns, ignoring
// channel. It was read-only, and its own comment called itself a
// simplification.

// ============================================================================
// Voice to Notes
//
// Sing a line or beatbox a groove and get notes in the pattern.
//
// The detection is AudioAnalyzer's, which has been in the tree for a long
// time and was reachable only from a separate executable that worked on a
// WAV on disk. Using it meant recording in one program, exporting a .ctp and
// opening it in the other - enough friction that the feature effectively did
// not exist.
//
// The audio callback fills a lock-free ring and does nothing else; every
// FFT, autocorrelation and classification happens right here on the UI
// thread. That split is the whole design, and it is the bug that was fixed
// in 3.3 when the spectrum analyzer ran its FFT inside the callback.
// ============================================================================
static VoicePanelState g_Voice;
static VoiceCaptureDevice g_VoiceDevice;

inline VoicePanelState& voicePanelState() { return g_Voice; }

// Drain whatever the microphone has produced since the last frame.
//
// Called once per frame whether or not the panel is visible, because a ring
// nobody drains fills up and starts dropping audio - and a take that was
// silently truncated while the panel was behind another tab is worse than
// one that never recorded.
inline void PollVoiceCapture() {
    if (!g_VoiceDevice.running()) return;

    static std::vector<float> scratch;
    if (scratch.size() < 4096) scratch.resize(4096);

    g_Voice.tracker.setSampleRate(g_VoiceDevice.sampleRate());
    g_Voice.takeSampleRate = g_VoiceDevice.sampleRate();

    size_t got = 0;
    while ((got = g_VoiceDevice.ring().read(scratch.data(), scratch.size())) > 0) {
        if (g_Voice.liveMode) {
            g_Voice.tracker.process(scratch.data(), got);
        }
        if (g_Voice.capturingTake) {
            // Ten minutes at 48 kHz. A cap rather than unbounded growth: a
            // panel left armed overnight should not eat the machine.
            constexpr size_t MAX_TAKE = 48000u * 60u * 10u;
            if (g_Voice.take.size() + got <= MAX_TAKE) {
                g_Voice.take.insert(g_Voice.take.end(), scratch.data(),
                                    scratch.data() + got);
            } else {
                g_Voice.capturingTake = false;
            }
        }
        if (got < scratch.size()) break;
    }

    g_Voice.levelHistory[static_cast<size_t>(g_Voice.levelCursor)] =
        g_VoiceDevice.peak();
    g_Voice.levelCursor = (g_Voice.levelCursor + 1) %
                          static_cast<int>(g_Voice.levelHistory.size());
    g_VoiceDevice.decayMeter();
}


// ============================================================================
// Teaching the vocal drum classifier
// ============================================================================

static DrumClassifier g_VocalDrums;

inline DrumClassifier& vocalDrumClassifier() { return g_VocalDrums; }

/*
 * The teaching block, shown in drum mode.
 *
 * Every example is taken from the LAST HIT the tracker heard, rather than
 * from a separate recording mode. The user beatboxes, sees what it guessed,
 * and corrects it - which is both less work than a training wizard and
 * teaches on exactly the sounds they are actually going to make.
 */

/*
 * The kit picker.
 *
 * Two independent choices, presented as such: which drums are in play, and
 * what each one sounds like. Only the first affects detection, and it
 * affects it a great deal - taking the hi-hat out of the kit takes breathy
 * snares from being wrong a third of the time to never.
 */
inline void DrawDrumKitPicker(UIState& ui) {
    DrumKit& kit = g_Voice.options.kit;

    if (!ImGui::TreeNode("Kit")) return;

    // ---- Ready-made kits -----------------------------------------------------
    int presetCount = 0;
    const DrumKitPreset* presets = drumKitPresets(presetCount);

    for (int i = 0; i < presetCount; ++i) {
        ImGui::PushID(i);
        if (ImGui::SmallButton(presets[i].name)) {
            // The instruments are kept when only the shape is being chosen,
            // so picking "Kick + Snare" does not silently undo somebody's
            // 808 selection.
            const DrumKit chosen = presets[i].make();
            const bool changesVoices =
                (std::string(presets[i].name) == "808s");

            kit.useKick = chosen.useKick;
            kit.useSnare = chosen.useSnare;
            kit.useHatClosed = chosen.useHatClosed;
            kit.useHatOpen = chosen.useHatOpen;

            if (changesVoices) {
                kit.kickInstrument = chosen.kickInstrument;
                kit.snareInstrument = chosen.snareInstrument;
                kit.hatClosedInstrument = chosen.hatClosedInstrument;
            }
            g_Voice.tracker.setKit(kit);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", presets[i].description);
        ImGui::PopID();

        if ((i % 3) != 2 && i + 1 < presetCount) ImGui::SameLine();
    }

    ImGui::Spacing();

    // ---- Which drums, and what they sound like --------------------------------
    struct Row { DrumClass drum; bool* enabled; OscillatorType* instrument; };
    const Row ROWS[] = {
        {DrumClass::Kick,      &kit.useKick,      &kit.kickInstrument},
        {DrumClass::Snare,     &kit.useSnare,     &kit.snareInstrument},
        {DrumClass::HatClosed, &kit.useHatClosed, &kit.hatClosedInstrument},
        {DrumClass::HatOpen,   &kit.useHatOpen,   &kit.hatOpenInstrument},
    };

    for (const Row& row : ROWS) {
        ImGui::PushID(static_cast<int>(row.drum));

        if (ImGui::Checkbox(drumClassName(row.drum), row.enabled)) {
            /*
             * A kit with nothing in it would produce no notes at all, which
             * looks exactly like the microphone having failed. Switching
             * off the last drum is refused rather than allowed to produce
             * silence.
             */
            if (kit.count() == 0) *row.enabled = true;
            g_Voice.tracker.setKit(kit);
        }

        if (*row.enabled) {
            ImGui::SameLine(110.0f);
            // What to say for this drum, given what else is in the kit -
            // the best sound for a snare depends on whether a kick is
            // there competing for it.
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.95f, 1.0f), "say %s",
                               kitPhonemeFor(kit, row.drum));

            ImGui::SameLine(210.0f);
            ImGui::SetNextItemWidth(-1);
            int type = static_cast<int>(*row.instrument);
            if (ImGui::Combo("##voice", &type, OSC_NAMES, OSC_NAME_COUNT)) {
                *row.instrument = static_cast<OscillatorType>(
                    std::clamp(type, 0, OSC_NAME_COUNT - 1));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Which instrument this drum becomes. Taste only - it\n"
                    "changes nothing about how the sound is recognised.");
            }
        }

        ImGui::PopID();
    }

    // The accuracy argument, said where the decision is made.
    if (kit.count() >= 4) {
        ImGui::TextDisabled(
            "All four is the least reliable. Fewer drums are recognised\n"
            "better, because the ones you take out cannot be chosen wrongly.");
    }

    ImGui::TreePop();
}

inline void DrawVocalDrumTraining(UIState& ui) {
    if (!ImGui::TreeNode("Teach your drum sounds")) return;

    /*
     * The prescribed sounds, said plainly.
     *
     * The counterintuitive research finding is that sticking to a fixed set
     * of mouth sounds beats using whatever you like by a wide margin - so
     * the sounds are named here rather than left to be guessed at. /t/ is
     * deliberately not among them: it is what most people reach for and it
     * cannot be told apart from "tss" and "tshh" reliably.
     */
    ImGui::TextWrapped(
        "These four separate best. Beatbox a hit, then press the drum it "
        "was meant to be - it learns from what you just played.");

    const bool haveHit = g_Voice.tracker.haveLastTimbre();
    if (!haveHit) {
        ImGui::TextDisabled("Make a sound first, then teach it.");
    }

    for (int c = 0; c < int(DrumClass::Count); ++c) {
        const DrumClass label = static_cast<DrumClass>(c);

        // Teaching a drum that is not in the kit cannot affect anything,
        // so offering it is an invitation to waste time.
        if (!g_Voice.options.kit.enabled(label)) continue;

        ImGui::PushID(c);

        const int taught = g_VocalDrums.countFor(label);

        ImGui::BeginDisabled(!haveHit);
        if (ImGui::Button(drumClassName(label), ImVec2(90, 0))) {
            g_VocalDrums.addExample(g_Voice.tracker.lastTimbre(), label);
            g_VocalDrums.save(drumTrainingPath(ui.settingsDirectory));
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        // From the kit, so this agrees with the picker above rather than
        // always naming the four-piece sounds.
        ImGui::TextDisabled("say %s",
                            kitPhonemeFor(g_Voice.options.kit, label));

        ImGui::SameLine(220.0f);
        // Green once there are enough of this one to be usable.
        const bool enough = taught >= DrumClassifier::MIN_PER_CLASS;
        ImGui::TextColored(enough ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f)
                                  : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "%d", taught);

        if (taught > 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("undo")) {
                g_VocalDrums.removeLast(label);
                g_VocalDrums.save(drumTrainingPath(ui.settingsDirectory));
            }
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    /*
     * Which classifier is actually deciding, said out loud.
     *
     * Teaching two examples and assuming it is now using them - when it
     * silently needs three of two different sounds - is the kind of thing
     * that makes people conclude the feature does not work.
     */
    if (g_VocalDrums.trained()) {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
                           "Using your sounds (%d taught)",
                           g_VocalDrums.exampleCount());
    } else {
        ImGui::TextDisabled("Using the built-in rules.");
        ImGui::TextWrapped(
            "Teach at least %d each of two different drums to use yours "
            "instead.", DrumClassifier::MIN_PER_CLASS);
    }

    if (g_VocalDrums.exampleCount() > 0) {
        if (ImGui::SmallButton("Forget everything")) {
            g_VocalDrums.clear();
            g_VocalDrums.save(drumTrainingPath(ui.settingsDirectory));
        }
    }

    ImGui::TreePop();
}

inline void DrawVoicePanel(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::SetNextWindowPos(ImVec2(320, 200), ImGuiCond_FirstUseEver);
    // Tall enough that the whole thing fits at the default size rather than
    // relying on the footer below to rescue it.
    ImGui::SetNextWindowSize(ImVec2(430, 620), ImGuiCond_FirstUseEver);
    ImGui::Begin("Voice to Notes");

    /*
     * Everything above the commit block scrolls; the commit block does not.
     *
     * The panel's content outgrew its window, which put "Add to Pattern" -
     * the one thing the panel exists to do - below the fold, reachable only
     * by scrolling. A panel whose primary action is off screen is one people
     * conclude is broken.
     *
     * The footer is sized from the frame height rather than a pixel count so
     * it stays right at any font scale or theme padding.
     */
    float footerHeight = ImGui::GetFrameHeightWithSpacing() * 1.4f +
                         ImGui::GetTextLineHeightWithSpacing() * 2.2f;

    /*
     * Never more than half the panel.
     *
     * Docked into a short column the footer would otherwise take the whole
     * window and leave the scrolling part zero pixels high - which trades
     * an unreachable Add button for unreachable capture controls. The footer
     * scrolls instead when there is genuinely no room, which is the lesser
     * of the two failures.
     */
    const float available = ImGui::GetContentRegionAvail().y;
    footerHeight = std::min(footerHeight, available * 0.55f);

    ImGui::BeginChild("##voicescroll", ImVec2(0.0f, -footerHeight), false);

    // ---- Input device ------------------------------------------------------
    if (!g_Voice.devicesEnumerated) {
        g_Voice.devices = g_VoiceDevice.enumerate();
        g_Voice.devicesEnumerated = true;
        if (!g_Voice.devices.empty() && g_Voice.selectedDevice < 0) {
            g_Voice.selectedDevice = 0;
        }
    }

    ImGui::TextUnformatted("Input");
    ImGui::SetNextItemWidth(-70);
    const char* deviceLabel =
        (g_Voice.selectedDevice >= 0 &&
         g_Voice.selectedDevice < static_cast<int>(g_Voice.devices.size()))
            ? g_Voice.devices[static_cast<size_t>(g_Voice.selectedDevice)].name.c_str()
            : "No microphone found";

    if (ImGui::BeginCombo("##voicedev", deviceLabel)) {
        for (size_t i = 0; i < g_Voice.devices.size(); ++i) {
            const bool selected = (static_cast<int>(i) == g_Voice.selectedDevice);
            if (ImGui::Selectable(g_Voice.devices[i].name.c_str(), selected)) {
                g_Voice.selectedDevice = static_cast<int>(i);
                if (g_VoiceDevice.running()) {
                    g_VoiceDevice.shutdown();
                    g_Voice.armed = false;
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) {
        g_Voice.devicesEnumerated = false;
    }

    ImGui::Separator();

    // ---- Arm ---------------------------------------------------------------
    const bool running = g_VoiceDevice.running();
    if (!running) {
        if (ImGui::Button("Start Listening", ImVec2(150, 28))) {
            const VoiceCaptureDevice::DeviceInfo* device = nullptr;
            if (g_Voice.selectedDevice >= 0 &&
                g_Voice.selectedDevice < static_cast<int>(g_Voice.devices.size())) {
                device = &g_Voice.devices[static_cast<size_t>(g_Voice.selectedDevice)];
            }
            if (g_VoiceDevice.start(device)) {
                g_Voice.armed = true;
                g_Voice.lastError.clear();
                g_Voice.tracker.setSampleRate(g_VoiceDevice.sampleRate());
                g_Voice.tracker.reset();
            } else {
                g_Voice.lastError = "Could not open that microphone";
            }
        }
    } else {
        if (ImGui::Button("Stop Listening", ImVec2(150, 28))) {
            g_VoiceDevice.shutdown();
            g_Voice.armed = false;
            g_Voice.capturingTake = false;
        }
    }

    ImGui::SameLine();
    ImGui::Checkbox("Live", &g_Voice.liveMode);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Live shows the note as you sing it, which is how you tell\n"
            "immediately whether the thing is following you.\n\n"
            "Off records the whole take first and analyses it afterwards.\n"
            "That is more accurate, because it can look at what comes next\n"
            "before deciding where a note ended.");
    }

    if (!g_Voice.lastError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s",
                           g_Voice.lastError.c_str());
    }

    // ---- Level -------------------------------------------------------------
    {
        const float peak = running ? g_VoiceDevice.peak() : 0.0f;
        ImGui::ProgressBar(std::min(1.0f, peak), ImVec2(-1, 12), "");
        if (running && peak < 0.005f) {
            ImGui::TextDisabled("nothing coming in - check the input above");
        }

        // Audio the UI failed to collect. Said plainly rather than left to
        // look like inaccuracy.
        const uint64_t dropped = g_VoiceDevice.dropped();
        if (dropped > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                               "%llu samples dropped",
                               static_cast<unsigned long long>(dropped));
        }
    }

    ImGui::Separator();

    // ---- What to detect ----------------------------------------------------
    ImGui::SetNextItemWidth(150);
    const char* modes = "Melody\0Drums / beatbox\0Rhythm\0Polyphonic\0";
    // The tracker uses the taught sounds when there are enough of them.
    g_Voice.tracker.setClassifier(&g_VocalDrums);

    /*
     * Publish the kit for Project Check.
     *
     * The kit is a property of how somebody is recording rather than of the
     * song, so it lives here rather than in the project - but the audit
     * still has to see it to report a kit with everything switched off.
     */
    voiceKitForAudit() = g_Voice.options.kit;

    if (ImGui::Combo("Detect", &g_Voice.analysisMode, modes)) {
        g_Voice.tracker.setMode(g_Voice.analysisMode == 1 ? LiveVoiceMode::Drums
                                                          : LiveVoiceMode::Melodic);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Melody     one note at a time - sing a line\n"
            "Drums      kick, snare and hat from a beatbox pattern\n"
            "Rhythm     onsets only, keeping one pitch\n"
            "Polyphonic more than one note at once (offline only)");
    }

    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderFloat("Sensitivity", &g_Voice.sensitivity, 0.0f, 1.0f, "%.2f")) {
        g_Voice.tracker.setSensitivity(g_Voice.sensitivity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "How far above the recent average a moment has to be to count\n"
            "as a hit. Turn it up if quiet taps are being missed, down if\n"
            "one hit is arriving as two.");
    }

    // The kit and the teaching belong with the other drum settings, and
    // have nothing to say about a hummed melody.
    if (g_Voice.analysisMode == 1) {
        DrawDrumKitPicker(ui);
        DrawVocalDrumTraining(ui);
    }

    // ---- Live readout ------------------------------------------------------
    if (g_Voice.liveMode && running) {
        ImGui::Separator();
        ImGui::TextUnformatted("Hearing");

        if (g_Voice.analysisMode == 1) {
            const char* lastHit = "-";
            if (!g_Voice.tracker.hits().empty()) {
                switch (g_Voice.tracker.hits().back().drumType) {
                    case 0: lastHit = "KICK"; break;
                    case 2: lastHit = "HAT"; break;
                    case 3: lastHit = "OPEN HAT"; break;
                    default: lastHit = "SNARE"; break;
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.95f, 0.7f, 1.0f));
            ImGui::Text("%s", lastHit);
            ImGui::PopStyleColor();
        } else {
            const int note = g_Voice.tracker.currentNote();
            if (note < 0) {
                ImGui::TextDisabled("-");
            } else {
                static const char* names[12] = {"C", "C#", "D", "D#", "E", "F",
                                                "F#", "G", "G#", "A", "A#", "B"};
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.95f, 0.7f, 1.0f));
                ImGui::Text("%s%d   %.1f Hz", names[note % 12], (note / 12) - 1,
                            g_Voice.tracker.currentFrequency());
                ImGui::PopStyleColor();
            }
        }

        ImGui::Text("%d hit%s captured",
                    static_cast<int>(g_Voice.tracker.hits().size()),
                    g_Voice.tracker.hits().size() == 1 ? "" : "s");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##livehits")) g_Voice.tracker.clearHits();
    }

    // ---- Offline take ------------------------------------------------------
    if (!g_Voice.liveMode) {
        ImGui::Separator();
        if (!g_Voice.capturingTake) {
            if (ImGui::Button("Record Take", ImVec2(120, 24)) && running) {
                g_Voice.take.clear();
                g_Voice.capturingTake = true;
                g_Voice.detected.clear();
            }
            if (!running && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Start listening first.");
            }
        } else {
            if (ImGui::Button("Stop Take", ImVec2(120, 24))) {
                g_Voice.capturingTake = false;
            }
        }
        ImGui::SameLine();
        ImGui::Text("%.1f s", static_cast<float>(g_Voice.take.size()) /
                              static_cast<float>(std::max(1, g_Voice.takeSampleRate)));

        ImGui::SameLine();
        if (ImGui::Button("Import WAV")) {
            const std::string path = openFileDialog(
                "Audio Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0", "wav");
            if (!path.empty()) {
                SamplePool pool;
                const int id = pool.loadSample(path);
                const Sample* sample = pool.getSample(id);
                if (sample != nullptr) {
                    g_Voice.take = sample->audioData;
                    g_Voice.takeSampleRate = sample->sampleRate;
                    g_Voice.detected.clear();
                } else {
                    g_Voice.lastError = "Could not read that file";
                }
            }
        }

        ImGui::BeginDisabled(g_Voice.take.empty());
        if (ImGui::Button("Analyse", ImVec2(120, 24))) {
            const float threshold = 0.12f - 0.10f * g_Voice.sensitivity;
            switch (g_Voice.analysisMode) {
                case 1:
                    g_Voice.detected = AudioAnalyzer::analyzeDrums(
                        g_Voice.take, g_Voice.takeSampleRate, threshold);
                    break;
                case 2:
                    g_Voice.detected = AudioAnalyzer::analyzeRhythm(
                        g_Voice.take, g_Voice.takeSampleRate, threshold);
                    break;
                case 3:
                    g_Voice.detected = AudioAnalyzer::analyzePolyphonic(
                        g_Voice.take, g_Voice.takeSampleRate, threshold);
                    break;
                default:
                    g_Voice.detected = AudioAnalyzer::analyzeMelody(
                        g_Voice.take, g_Voice.takeSampleRate, threshold);
                    break;
            }
            g_Voice.detectedKey = AudioAnalyzer::detectKey(g_Voice.take,
                                                           g_Voice.takeSampleRate);
            g_Voice.keyDetected = true;
        }
        ImGui::EndDisabled();

        if (!g_Voice.detected.empty()) {
            ImGui::SameLine();
            ImGui::Text("%d found", static_cast<int>(g_Voice.detected.size()));
        }
        if (g_Voice.keyDetected) {
            static const char* names[12] = {"C", "C#", "D", "D#", "E", "F",
                                            "F#", "G", "G#", "A", "A#", "B"};
            ImGui::TextDisabled("key: %s %s",
                                names[g_Voice.detectedKey.root % 12],
                                g_Voice.detectedKey.isMinor ? "minor" : "major");
        }
    }

    // ---- Insert ------------------------------------------------------------
    ImGui::Separator();

    ImGui::SetNextItemWidth(110);
    int snapIndex = static_cast<int>(g_Voice.options.snap);
    if (ImGui::BeginCombo("Quantise", snapLabel(g_Voice.options.snap))) {
        for (int i = 0; i < static_cast<int>(SnapDivision::Count); ++i) {
            const bool selected = (i == snapIndex);
            if (ImGui::Selectable(snapLabel(static_cast<SnapDivision>(i)), selected)) {
                g_Voice.options.snap = static_cast<SnapDivision>(i);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Off keeps the timing exactly as you sang it. A line that lands\n"
            "30 ms early is played, not wrong - putting it on the grid is a\n"
            "choice, and not always the one you want.");
    }


    /*
     * How hard to quantise, and what to leave alone.
     *
     * A hard snap is the fastest way to make a played part sound
     * programmed, so the strength is a dial rather than a switch - but it
     * defaults to a full snap, because that is what this has always done
     * and a default that moves under people is worse than one they have to
     * change.
     */
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat("Strength", &g_Voice.options.quantizeStrength,
                       0.0f, 1.0f, "%.0f%%",
                       ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "How far toward the grid each note moves.\n"
            "100%% is a hard snap. Around 70%% tightens the timing while\n"
            "leaving the push and pull that makes it sound played.");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("Keep", &g_Voice.options.quantizeRange,
                       0.05f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Notes further than this from the grid are left where they are.\n"
            "A flam or a grace note sits well off the beat on purpose, and\n"
            "dragging it in is the quantise failure people notice most.\n"
            "0.5 pulls in everything.");
    }

    /*
     * Measure the grid rather than assume one.
     *
     * Quantising a triplet passage onto sixteenths destroys it, and that is
     * exactly what happens when the grid is a setting rather than something
     * read off the performance.
     */
    {
        const bool haveTiming = g_Voice.liveMode
            ? g_Voice.tracker.hits().size() >= 4
            : g_Voice.detected.size() >= 4;

        ImGui::BeginDisabled(!haveTiming);
        if (ImGui::SmallButton("Match the take")) {
            std::vector<float> onsets;
            if (g_Voice.liveMode) {
                for (const LiveHit& hit : g_Voice.tracker.hits()) {
                    onsets.push_back(project.secondsToBeat(hit.timeSeconds));
                }
            } else {
                for (const DetectedNote& note : g_Voice.detected) {
                    onsets.push_back(project.secondsToBeat(note.startTime));
                }
            }

            const GridEstimate grid = detectGrid(onsets, project.beatsPerMeasure);
            if (grid.confident) {
                g_Voice.options.snap = grid.division;
                g_Voice.gridMessage = std::string("Grid: ") +
                                      snapLabel(grid.division);
            } else {
                // Said rather than silently applied. A grid guessed from a
                // performance that has none is worse than the setting the
                // user already had.
                g_Voice.gridMessage = "No clear grid - left as it was";
            }

            const float swing = detectSwing(onsets, 1.0f);
            g_Voice.options.swingRatio = swing;
            if (swing > 0.56f) {
                char text[64];
                snprintf(text, sizeof(text), "  swing %.0f%%", swing * 100.0f);
                g_Voice.gridMessage += text;
            }
        }
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Works out which grid you actually played on - straight or\n"
                "triplet - and how much you swung, then uses that instead of\n"
                "the setting above.");
        }

        if (!g_Voice.gridMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", g_Voice.gridMessage.c_str());
        }
    }

    ImGui::SetNextItemWidth(100);
    ImGui::DragInt("Shift", &g_Voice.options.transpose, 0.1f, -48, 48, "%+d st");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Move what you sang by semitones on the way in - which is how a\n"
            "bassline gets written by someone who cannot sing that low.");
    }

    ImGui::Checkbox("Use my dynamics", &g_Voice.options.useVelocity);
    ImGui::SameLine();

    const size_t pending = g_Voice.liveMode ? g_Voice.tracker.hits().size()
                                            : g_Voice.detected.size();

    /*
     * Where the take is going.
     *
     * This used to be nowhere: notes went into whatever pattern happened to
     * be selected in another panel, under whatever name it already had,
     * always at beat zero - and nothing here said which pattern that was.
     * The most common outcome of a good take was discovering it had
     * overwritten something else.
     */

    /*
     * Optional tidying.
     *
     * Both off by default: they change what was played, and a correction
     * that is confidently wrong is worse than none because somebody has to
     * find it and undo it.
     */
    if (g_Voice.analysisMode == 1) {
        // Beatboxing: complete a steady pattern that dropped a hit.
        ImGui::Checkbox("Fill missing hits", &g_Voice.drumFill.enabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Beatbox a steady hi-hat and drop one, and this puts it back.\n"
                "Only where the pattern is already steady and exactly one hit\n"
                "is missing - a real rest is left as a rest, and it never\n"
                "adds more than a quarter of what you played.");
        }
    } else {
        // Humming: keep the line in key.
        ImGui::Checkbox("Keep in key", &g_Voice.keySnap.enabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Pulls notes that fell out of the key back into it.\n"
                "Long notes are left alone - a held note outside the key is a\n"
                "choice, not a mistake - and nothing is snapped at all when\n"
                "the key is unclear.");
        }

        if (g_Voice.keySnap.enabled) {
            ImGui::SameLine();

            /*
             * Which key it will snap to, said out loud.
             *
             * Snapping to a key the user did not expect is the way this
             * feature ruins a take, so the answer is on screen before the
             * button is pressed rather than after.
             */
            if (g_Voice.keyDetected) {
                static const char* NAMES[] = {"C", "C#", "D", "D#", "E", "F",
                                              "F#", "G", "G#", "A", "A#", "B"};
                ImGui::TextDisabled("%s %s",
                                    NAMES[g_Voice.detectedKey.root % 12],
                                    g_Voice.detectedKey.isMinor ? "minor" : "major");
            } else {
                ImGui::TextDisabled("(analyse a take to find the key)");
            }
        }
    }

    if (!g_Voice.postMessage.empty()) {
        ImGui::TextDisabled("%s", g_Voice.postMessage.c_str());
    }


    /*
     * The tempo of what was actually played.
     *
     * Humming to a click is a skill, and requiring it before the tool will
     * listen is why features like this go unused. This finds the tempo of a
     * freely played take instead.
     *
     * What to DO with the answer is deliberately left to the user rather
     * than applied automatically: adopting their tempo and fitting them to
     * the project's are both reasonable and they are opposites, and getting
     * it wrong silently rewrites the timing of everything already written.
     */
    {
        const bool haveEnough = g_Voice.liveMode
            ? g_Voice.tracker.hits().size() >= 4
            : g_Voice.detected.size() >= 4;

        ImGui::BeginDisabled(!haveEnough);
        if (ImGui::SmallButton("Find the tempo")) {
            std::vector<float> onsets;
            if (g_Voice.liveMode) {
                for (const LiveHit& hit : g_Voice.tracker.hits()) {
                    onsets.push_back(hit.timeSeconds);
                }
            } else {
                for (const DetectedNote& note : g_Voice.detected) {
                    onsets.push_back(note.startTime);
                }
            }
            g_Voice.tempo = estimateTempo(onsets);
            g_Voice.tempoChecked = true;
        }
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Works out what tempo you played at, so you do not have to\n"
                "hum along to a click.");
        }

        if (g_Voice.tempoChecked) {
            ImGui::SameLine();

            if (!g_Voice.tempo.valid) {
                ImGui::TextDisabled("Not enough to tell");
            } else if (g_Voice.tempo.confidence < 0.7f) {
                // Said rather than offered. Rewriting the project tempo from
                // a take that had none is not recoverable by ear.
                ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.4f, 1.0f),
                                   "About %.0f BPM, but unsteady",
                                   g_Voice.tempo.bpm);
            } else {
                ImGui::Text("%.0f BPM", g_Voice.tempo.bpm);

                const bool differs =
                    std::fabs(g_Voice.tempo.bpm - project.bpm) > 1.0f;
                if (differs) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Use it")) {
                        g_UndoHistory.saveState(project, "Tempo From Voice");
                        project.bpm = std::clamp(g_Voice.tempo.bpm, 20.0f, 999.0f);
                    }
                    if (ImGui::IsItemHovered()) {
                        /*
                         * The warning is the point.
                         *
                         * Changing the project tempo moves everything
                         * already written, which is fine in an empty
                         * project and destructive in a full one - and the
                         * person pressing this is thinking about their hum,
                         * not about the parts they wrote yesterday.
                         */
                        ImGui::SetTooltip(
                            "Set the project to %.0f BPM.\n\n"
                            "The project is at %.0f. Everything already "
                            "written\nwill play at the new tempo.",
                            g_Voice.tempo.bpm, project.bpm);
                    }
                }
            }
        }
    }

    ImGui::SeparatorText("Destination");

    // ---- Which pattern -------------------------------------------------------
    {
        const bool toNew = (g_Voice.targetPattern < 0);
        std::string label = "+ New pattern";
        if (!toNew && g_Voice.targetPattern <
                          static_cast<int>(project.patterns.size())) {
            label = project.patterns[static_cast<size_t>(
                        g_Voice.targetPattern)].name;
        }

        ImGui::SetNextItemWidth(-90.0f);
        if (ImGui::BeginCombo("Pattern", label.c_str())) {
            // New first, because it is the default and the safe choice - it
            // cannot destroy anything that is already there.
            if (ImGui::Selectable("+ New pattern", toNew)) {
                g_Voice.targetPattern = -1;
            }
            for (size_t i = 0; i < project.patterns.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const bool selected = (static_cast<int>(i) == g_Voice.targetPattern);
                if (ImGui::Selectable(project.patterns[i].name.c_str(), selected)) {
                    g_Voice.targetPattern = static_cast<int>(i);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }

    // ---- What it is called ----------------------------------------------------
    if (g_Voice.targetPattern < 0) {
        // Named before it exists, so the take arrives with a name rather
        // than as the fourth thing called "Pattern".
        if (g_Voice.newPatternName[0] == '\0') {
            snprintf(g_Voice.newPatternName, sizeof(g_Voice.newPatternName),
                     "%s %d", voiceRoleName(g_Voice.options.role),
                     static_cast<int>(project.patterns.size() + 1));
        }
        ImGui::SetNextItemWidth(-90.0f);
        ImGui::InputText("Name", g_Voice.newPatternName,
                         sizeof(g_Voice.newPatternName));
    } else if (g_Voice.targetPattern < static_cast<int>(project.patterns.size())) {
        // And an existing one can be renamed from here, rather than making
        // somebody go and find the Patterns panel to do it.
        Pattern& target = project.patterns[static_cast<size_t>(g_Voice.targetPattern)];
        if (!g_Voice.renaming) {
            ImGui::Text("Into \"%s\"", target.name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Rename")) {
                snprintf(g_Voice.renameBuffer, sizeof(g_Voice.renameBuffer),
                         "%s", target.name.c_str());
                g_Voice.renaming = true;
            }
        } else {
            ImGui::SetNextItemWidth(-140.0f);
            const bool committed = ImGui::InputText(
                "##rename", g_Voice.renameBuffer, sizeof(g_Voice.renameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::SmallButton("OK") || committed) {
                if (g_Voice.renameBuffer[0] != '\0') {
                    g_UndoHistory.saveState(project, "Rename Pattern");
                    target.name = g_Voice.renameBuffer;
                }
                g_Voice.renaming = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) g_Voice.renaming = false;
        }
    }

    /*
     * Replacing only means something when there is something there.
     *
     * A new pattern is empty, so "replace" and "add alongside" do exactly
     * the same thing - and a control that visibly does nothing is one people
     * assume is broken, or worse, one they trust to be protecting them.
     */
    {
        const bool toNew = (g_Voice.targetPattern < 0);
        if (toNew) ImGui::BeginDisabled();
        ImGui::Checkbox("Replace what is there", &g_Voice.replacePattern);
        if (toNew) ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                toNew ? "A new pattern is empty - there is nothing to replace."
                      : "On, the take replaces what is in the pattern.\n"
                        "Off, it is added alongside.");
        }
    }

    // ---- Where it starts --------------------------------------------------------
    {
        ImGui::SetNextItemWidth(-90.0f);
        const char* placements = "From the start\0At the playhead\0At a bar\0";
        ImGui::Combo("Start", &g_Voice.placement, placements);

        if (g_Voice.placement ==
            static_cast<int>(VoicePanelState::Placement::Bar)) {
            ImGui::SetNextItemWidth(-90.0f);
            ImGui::InputInt("Bar", &g_Voice.placementBar);
            g_Voice.placementBar = std::max(1, g_Voice.placementBar);
        }
    }

    const float startBeat = placementStartBeat(
        g_Voice.placement, g_Voice.placementBar,
        seq.getCurrentBeat(), project.beatsPerMeasure);

    ImGui::EndChild();

    // ---- What is about to happen, in one line ------------------------------------
    //
    // Said before the button rather than discovered after it. Every question
    // somebody has at this moment - how many, where to, called what, from
    // which bar - answered where they are looking.
    {
        const int bar = static_cast<int>(startBeat /
                            float(std::max(1, project.beatsPerMeasure))) + 1;
        const std::string name = (g_Voice.targetPattern < 0)
            ? std::string(g_Voice.newPatternName)
            : project.patterns[static_cast<size_t>(
                  std::clamp(g_Voice.targetPattern, 0,
                             static_cast<int>(project.patterns.size()) - 1))].name;

        if (pending == 0) {
            ImGui::TextDisabled("Nothing captured yet.");
        } else {
            ImGui::TextWrapped("%d note%s %s \"%s\", from bar %d.",
                               static_cast<int>(pending),
                               pending == 1 ? "" : "s",
                               g_Voice.replacePattern ? "replacing" : "into",
                               name.c_str(), bar);
        }
    }

    ImGui::BeginDisabled(pending == 0);
    if (ImGui::Button("Add to Pattern", ImVec2(-1, 30))) {
        std::vector<Note> notes;
        if (g_Voice.liveMode) {
            notes = hitsToNotes(g_Voice.tracker.hits(), project.tempoMap,
                                project.bpm, project.beatsPerMeasure,
                                g_Voice.options);
        } else {
            notes = detectedToNotes(g_Voice.detected, project.tempoMap,
                                    project.bpm, project.beatsPerMeasure,
                                    g_Voice.options);
        }

        /*
         * The optional tidying, before anything is written.
         *
         * Here rather than inside hitsToNotes because these are choices
         * about the result, not part of the conversion - and because doing
         * them here keeps the conversion a pure function of the take, which
         * is what makes the strength dial reversible.
         */
        g_Voice.postMessage.clear();

        if (g_Voice.keySnap.enabled && !notes.empty()) {
            g_Voice.keySnap.scaleRoot = g_Voice.detectedKey.root;
            // The scale tables put major at 0 and minor at 1.
            g_Voice.keySnap.scaleType = g_Voice.detectedKey.isMinor ? 1 : 0;

            const KeySnapReport report = snapNotesToKey(
                notes, g_Voice.keySnap,
                g_Voice.keyDetected ? g_Voice.detectedKey.confidence : 0.0f);

            if (!report.ranAtAll) {
                g_Voice.postMessage = "Key unclear - nothing snapped";
            } else if (report.moved > 0) {
                char text[96];
                snprintf(text, sizeof(text), "Snapped %d note%s into key",
                         report.moved, report.moved == 1 ? "" : "s");
                g_Voice.postMessage = text;
            }
        }

        if (g_Voice.drumFill.enabled && !notes.empty()) {
            const DrumFillReport report = fillDrumGaps(notes, g_Voice.drumFill);
            if (report.added > 0) {
                char text[96];
                snprintf(text, sizeof(text), "Filled %d missing hit%s",
                         report.added, report.added == 1 ? "" : "s");
                if (!g_Voice.postMessage.empty()) g_Voice.postMessage += ", ";
                g_Voice.postMessage += text;
            }
        }

        if (!notes.empty()) {
            // Through the same undo history as every other edit, so a take
            // that came out wrong is one Ctrl+Z away.
            g_UndoHistory.saveState(project, "Voice to Notes");

            // Move it to where it was asked to start, before anything is
            // written, so the pattern length below accounts for the offset.
            placeNotesAt(notes, startBeat);

            int index = g_Voice.targetPattern;
            if (index < 0 || index >= static_cast<int>(project.patterns.size())) {
                Pattern fresh;
                fresh.name = (g_Voice.newPatternName[0] != '\0')
                    ? g_Voice.newPatternName
                    : (std::string(voiceRoleName(g_Voice.options.role)) + " take");
                project.patterns.push_back(fresh);
                index = static_cast<int>(project.patterns.size()) - 1;
            }

            Pattern& pattern = project.patterns[static_cast<size_t>(index)];
            if (g_Voice.replacePattern) pattern.notes.clear();
            for (Note& note : notes) {
                clampNoteToValidRanges(note);
                pattern.notes.push_back(note);
            }

            // The pattern has to be long enough to hold what was just sung,
            // or the end of the take is written and never plays.
            pattern.length = patternLengthFor(pattern.notes, pattern.length);

            /*
             * Select what was just made.
             *
             * The piano roll draws the selected pattern, so this is what
             * puts the take in front of the user instead of leaving them to
             * go and find it. It is also the answer to "where did my notes
             * go" - they are on screen.
             */
            ui.selectedPattern = index;
            g_Voice.targetPattern = index;
            g_Voice.newPatternName[0] = '\0';

            if (g_Voice.liveMode) g_Voice.tracker.clearHits();
            else g_Voice.detected.clear();

            seq.updateChannelConfigs();
        }
    }
    ImGui::EndDisabled();

    if (pending == 0) {
        ImGui::TextDisabled(g_Voice.liveMode ? "sing or beatbox something first"
                                             : "record or import a take, then analyse");
    } else {
        ImGui::Text("%d note%s ready", static_cast<int>(pending),
                    pending == 1 ? "" : "s");
    }

    ImGui::End();
}


// ============================================================================
// Project Check
//
// Settings that are legal, saved, shown as enabled, and doing nothing - or
// silently cancelling something else. A send into a muted bus. A modulation
// route pointing at an engine the channel is not running. A soloed channel
// three screens away that is why nothing else is audible.
//
// The rules live in SettingsAudit.h with no ImGui in them, so they are
// testable; this only draws the result. It never changes anything: a "fix it
// for me" button that silently unmutes a channel would be worse than the
// confusion it solves.
// ============================================================================
inline void DrawProjectCheck(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::SetNextWindowPos(ImVec2(360, 240), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
    ImGui::Begin("Project Check");

    /*
     * Recomputed on a timer rather than every frame.
     *
     * The audit walks every channel, every clip and every note looking for
     * stacked chords, which is fine once a second and wasteful sixty times a
     * second. A second is also short enough that it feels live when the user
     * is changing something to make a warning go away.
     */
    static std::vector<AuditFinding> findings;
    static float sinceRefresh = 1e9f;
    sinceRefresh += ImGui::GetIO().DeltaTime;

    const bool refreshNow = ImGui::Button("Re-check");
    if (refreshNow || sinceRefresh > 1.0f) {
        findings = auditProject(project);
        sinceRefresh = 0.0f;
    }

    const int problems = auditCount(findings, AuditSeverity::Problem);
    const int warnings = auditCount(findings, AuditSeverity::Warning);
    const int notes = auditCount(findings, AuditSeverity::Note);

    ImGui::SameLine();
    if (findings.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f),
                           "Nothing to report.");
    } else {
        ImGui::Text("%d problem%s, %d warning%s, %d note%s",
                    problems, problems == 1 ? "" : "s",
                    warnings, warnings == 1 ? "" : "s",
                    notes, notes == 1 ? "" : "s");
    }

    ImGui::Separator();

    if (findings.empty()) {
        ImGui::TextWrapped(
            "This panel looks for settings that are switched on and doing "
            "nothing: an effect with its mix at zero, a send into a muted "
            "bus, a modulation route pointing at an engine this channel is "
            "not running, a soloed channel that is silencing everything "
            "else.\n\n"
            "It never changes anything. It only says what it found.");
        ImGui::End();
        return;
    }

    for (size_t i = 0; i < findings.size(); ++i) {
        const AuditFinding& finding = findings[i];
        ImGui::PushID(static_cast<int>(i));

        ImVec4 colour;
        switch (finding.severity) {
            case AuditSeverity::Problem: colour = ImVec4(1.0f, 0.45f, 0.45f, 1.0f); break;
            case AuditSeverity::Warning: colour = ImVec4(1.0f, 0.78f, 0.35f, 1.0f); break;
            case AuditSeverity::Note:
            default:                     colour = ImVec4(0.62f, 0.72f, 0.85f, 1.0f); break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextUnformatted(auditSeverityName(finding.severity));
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextWrapped("%s", finding.what.c_str());

        ImGui::Indent();
        ImGui::TextWrapped("%s", finding.why.c_str());

        // TextColored does not wrap, so the suggested fix was being clipped
        // at the panel edge - and the fix is the half of a finding the user
        // actually needs. Colour pushed around a wrapped call instead.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.80f, 0.70f, 1.0f));
        ImGui::TextWrapped("%s", finding.fix.c_str());
        ImGui::PopStyleColor();

        // Jump straight to the channel it is about, since half of these are
        // "something on a channel you are not looking at".
        if (finding.channel >= 0 && finding.channel < Project::MAX_CHANNELS) {
            if (ImGui::SmallButton("Go to channel")) {
                ui.selectedChannel = finding.channel;
            }
        }
        ImGui::Unindent();

        if (i + 1 < findings.size()) ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::End();
}


// ============================================================================
// Take Lanes
//
// Record a part several times, then build the keeper out of the best moments
// of each by swiping across the lanes.
//
// The comp is an editing model: swiping changes which take wins where, and
// flattening turns those choices into ordinary audio clips on the
// arrangement. Playback never sees a comp - it sees clips - which is what
// lets comping reuse the whole tested audio-clip path rather than
// duplicating trimming, fades and sample-rate conversion inside a second one.
// ============================================================================
struct TakeLanesState {
    int selectedGroup = -1;

    // Loop recording. Each pass around the loop closes the current take and
    // starts another, which is the entire point of it.
    bool recording = false;
    bool loopStack = true;
    float takeStartBeat = 0.0f;
    float lastBeat = 0.0f;
    std::vector<float> pending;     // audio captured for the take in progress

    // Punch: record only inside this range, so a good take can be repaired
    // in one place without risking the rest of it.
    bool punchEnabled = false;
    float punchIn = 0.0f;
    float punchOut = 4.0f;

    // Swipe state.
    bool swiping = false;
    int swipeTake = -1;
    float swipeAnchor = 0.0f;
};

static TakeLanesState g_TakeLanes;

inline TakeLanesState& takeLanesState() { return g_TakeLanes; }

/*
 * Close the take in progress and put its audio into the pool.
 *
 * Called at every loop wrap and when recording stops. Returns the take index
 * or -1 if there was nothing worth keeping - a pass with almost no audio is
 * a false start, and adding it would fill the lanes with silence.
 */
inline int commitPendingTake(Project& project, CompGroup& group,
                             TakeLanesState& state, int sampleRate,
                             float endBeat) {
    if (state.pending.size() < static_cast<size_t>(sampleRate) / 20) {
        state.pending.clear();
        return -1;
    }

    Sample sample;
    sample.name = "Take " + std::to_string(group.takes.size() + 1);
    sample.sampleRate = sampleRate;
    sample.audioData = std::move(state.pending);
    sample.lengthSeconds = static_cast<float>(sample.audioData.size()) /
                           static_cast<float>(std::max(1, sampleRate));
    sample.isLoaded = true;
    state.pending.clear();

    const int id = project.samplePool.addSample(sample);
    if (id < 0) return -1;

    const float length = std::max(0.05f, endBeat - state.takeStartBeat);
    return comp::addTake(group, id, state.takeStartBeat, length, "");
}

inline void DrawTakeLanes(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::SetNextWindowPos(ImVec2(300, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(680, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Take Lanes");

    TakeLanesState& state = g_TakeLanes;

    // ---- Group selection ---------------------------------------------------
    if (project.compGroups.empty()) {
        ImGui::TextWrapped(
            "Record a part several times, then build the keeper out of the "
            "best moments of each.\n\n"
            "Each pass becomes a lane. Drag across a lane to choose it for "
            "that stretch; the result is written to the arrangement as "
            "ordinary audio clips, so you can trim and fade them afterwards "
            "like anything else.");
        ImGui::Separator();

        if (ImGui::Button("New comp on this channel", ImVec2(-1, 30))) {
            g_UndoHistory.saveState(project, "New Comp");
            CompGroup group;
            group.channelIndex = std::clamp(ui.selectedChannel, 0,
                                            Project::MAX_CHANNELS - 1);
            group.name = project.channels[static_cast<size_t>(group.channelIndex)].name +
                         " comp";
            group.startBeat = 0.0f;
            group.lengthBeats = 16.0f;
            project.compGroups.push_back(group);
            state.selectedGroup = static_cast<int>(project.compGroups.size()) - 1;
        }
        ImGui::End();
        return;
    }

    if (state.selectedGroup < 0 ||
        state.selectedGroup >= static_cast<int>(project.compGroups.size())) {
        state.selectedGroup = 0;
    }

    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("##compgroup",
            project.compGroups[static_cast<size_t>(state.selectedGroup)].name.c_str())) {
        for (size_t i = 0; i < project.compGroups.size(); ++i) {
            const bool selected = (static_cast<int>(i) == state.selectedGroup);
            if (ImGui::Selectable(project.compGroups[i].name.c_str(), selected)) {
                state.selectedGroup = static_cast<int>(i);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    CompGroup& group = project.compGroups[static_cast<size_t>(state.selectedGroup)];

    ImGui::SameLine();
    if (ImGui::SmallButton("+ Comp")) {
        g_UndoHistory.saveState(project, "New Comp");
        CompGroup fresh;
        fresh.channelIndex = std::clamp(ui.selectedChannel, 0,
                                        Project::MAX_CHANNELS - 1);
        fresh.name = "Comp " + std::to_string(project.compGroups.size() + 1);
        project.compGroups.push_back(fresh);
        state.selectedGroup = static_cast<int>(project.compGroups.size()) - 1;
    }

    ImGui::SameLine();
    ImGui::Text("on %s",
                project.channels[static_cast<size_t>(
                    std::clamp(group.channelIndex, 0, Project::MAX_CHANNELS - 1))]
                    .name.c_str());

    // ---- Recording ----------------------------------------------------------
    {
        VoiceCaptureDevice& device = g_VoiceDevice;

        if (!state.recording) {
            if (ImGui::Button("Record Take", ImVec2(120, 26))) {
                if (!device.running()) {
                    const VoiceCaptureDevice::DeviceInfo* input = nullptr;
                    if (g_Voice.selectedDevice >= 0 &&
                        g_Voice.selectedDevice <
                            static_cast<int>(g_Voice.devices.size())) {
                        input = &g_Voice.devices[
                            static_cast<size_t>(g_Voice.selectedDevice)];
                    }
                    device.start(input);
                }
                if (device.running()) {
                    state.recording = true;
                    state.pending.clear();
                    state.takeStartBeat = seq.getCurrentBeat();
                    state.lastBeat = state.takeStartBeat;
                }
            }
        } else {
            if (ImGui::Button("Stop", ImVec2(120, 26))) {
                g_UndoHistory.saveState(project, "Record Take");
                commitPendingTake(project, group, state, device.sampleRate(),
                                  seq.getCurrentBeat());
                comp::flattenAll(project);
                state.recording = false;
            }
        }

        ImGui::SameLine();
        ImGui::Checkbox("Stack loops", &state.loopStack);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Every pass around the loop becomes its own lane, so you can\n"
                "keep playing and comp afterwards. This is what loop\n"
                "recording is for.");
        }

        ImGui::SameLine();
        ImGui::Checkbox("Punch", &state.punchEnabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Record only inside a range, so a good take can be repaired\n"
                "in one place without risking the rest of it.");
        }
        if (state.punchEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180);
            ImGui::DragFloatRange2("##punch", &state.punchIn, &state.punchOut,
                                   0.25f, 0.0f, 100000.0f, "in %.1f", "out %.1f");
        }

        if (state.recording) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "REC  %.1f",
                               seq.getCurrentBeat());
        }
    }

    ImGui::Separator();

    // ---- The lanes ------------------------------------------------------------
    const ImVec2 canvas = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Wide enough for the mute button, the name field and the delete button
    // together - at 130 the delete button sat on top of the first beat of
    // every lane.
    const float headerWidth = 154.0f;
    const float laneHeight = 26.0f;
    const float compHeight = 22.0f;
    const float gridLeft = canvas.x + headerWidth;
    const float gridRight = canvas.x + available.x;
    const float span = std::max(1.0f, group.lengthBeats);

    auto beatToX = [&](float beat) {
        return gridLeft + (beat - group.startBeat) / span * (gridRight - gridLeft);
    };
    auto xToBeat = [&](float x) {
        return group.startBeat +
               (x - gridLeft) / std::max(1.0f, gridRight - gridLeft) * span;
    };

    // The comp strip: what actually plays.
    draw->AddRectFilled(ImVec2(gridLeft, canvas.y),
                        ImVec2(gridRight, canvas.y + compHeight),
                        IM_COL32(24, 24, 30, 255));
    draw->AddText(ImVec2(canvas.x + 4, canvas.y + 3),
                  IM_COL32(200, 200, 215, 255), "Comp");

    for (const CompSegment& segment : group.segments) {
        if (segment.takeIndex < 0) continue;
        const float x0 = beatToX(segment.startBeat);
        const float x1 = beatToX(segment.endBeat);
        draw->AddRectFilled(ImVec2(x0 + 1, canvas.y + 2),
                            ImVec2(x1 - 1, canvas.y + compHeight - 2),
                            channelColor(segment.takeIndex));
    }

    // One row per take.
    const float lanesTop = canvas.y + compHeight + 4.0f;
    for (size_t t = 0; t < group.takes.size(); ++t) {
        Take& take = group.takes[t];
        const float y = lanesTop + static_cast<float>(t) * laneHeight;

        ImGui::PushID(static_cast<int>(1500 + t));

        draw->AddRectFilled(ImVec2(gridLeft, y),
                            ImVec2(gridRight, y + laneHeight - 2.0f),
                            take.muted ? IM_COL32(28, 28, 32, 255)
                                       : IM_COL32(38, 38, 46, 255));

        // The take's own extent, so a punched-in pass is visibly shorter.
        const float takeX0 = beatToX(take.startBeat);
        const float takeX1 = beatToX(take.endBeat());
        const ImU32 colour = take.muted ? IM_COL32(70, 70, 80, 255)
                                        : channelColor(static_cast<int>(t));
        draw->AddRectFilled(ImVec2(takeX0 + 1, y + 3),
                            ImVec2(takeX1 - 1, y + laneHeight - 5.0f),
                            (colour & 0x00FFFFFFu) | 0x60000000u);

        // Where this take is the one being used.
        for (const CompSegment& segment : group.segments) {
            if (segment.takeIndex != static_cast<int>(t)) continue;
            draw->AddRectFilled(ImVec2(beatToX(segment.startBeat) + 1, y + 3),
                                ImVec2(beatToX(segment.endBeat) - 1,
                                       y + laneHeight - 5.0f),
                                colour);
        }

        // The header: name, mute, and how much of the comp it won.
        ImGui::SetCursorScreenPos(ImVec2(canvas.x + 2, y + 3));
        if (ImGui::SmallButton(take.muted ? "M##mute" : "m##mute")) {
            g_UndoHistory.saveState(project, "Mute Take");
            take.muted = !take.muted;
            comp::flattenAll(project);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        char nameBuffer[48];
        snprintf(nameBuffer, sizeof(nameBuffer), "%s", take.name.c_str());
        if (ImGui::InputText("##takename", nameBuffer, sizeof(nameBuffer))) {
            take.name = nameBuffer;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x##take")) {
            g_UndoHistory.saveState(project, "Delete Take");
            comp::removeTake(group, static_cast<int>(t));
            comp::flattenAll(project);
            ImGui::PopID();
            break;
        }

        ImGui::PopID();
    }

    /*
     * Bar lines and the playhead, over the top of the lanes.
     *
     * Drawn last and translucent so they read as a grid rather than
     * repainting the segment colours. Without them the lanes are coloured
     * bars against a flat background, and there is no way to tell where in
     * the music a swipe actually landed - which is the one thing you need
     * to know while comping.
     */
    {
        const float gridBottom = lanesTop +
            static_cast<float>(group.takes.size()) * laneHeight;
        const int beatsPerBar = std::max(1, project.meterAt(group.startBeat).numerator);

        for (int beat = 0; beat <= static_cast<int>(std::ceil(span)); ++beat) {
            const float x = beatToX(group.startBeat + static_cast<float>(beat));
            if (x < gridLeft || x > gridRight) continue;
            const bool bar = (beat % beatsPerBar) == 0;
            draw->AddLine(ImVec2(x, canvas.y), ImVec2(x, gridBottom),
                          bar ? IM_COL32(200, 200, 220, 90)
                              : IM_COL32(160, 160, 180, 34),
                          bar ? 1.5f : 1.0f);
        }

        // Where playback is, so a lane can be judged against what is heard.
        const float playhead = seq.getCurrentBeat();
        if (playhead >= group.startBeat && playhead <= group.startBeat + span) {
            const float x = beatToX(playhead);
            draw->AddLine(ImVec2(x, canvas.y), ImVec2(x, gridBottom),
                          IM_COL32(255, 230, 120, 220), 2.0f);
        }
    }

    // ---- Swiping ---------------------------------------------------------------
    //
    // Drag across a lane to choose that take for the stretch you dragged.
    // This is the one gesture comping actually is.
    {
        const float lanesBottom = lanesTop +
            static_cast<float>(group.takes.size()) * laneHeight;

        ImGui::SetCursorScreenPos(ImVec2(gridLeft, lanesTop));
        ImGui::InvisibleButton("##swipe",
                               ImVec2(std::max(1.0f, gridRight - gridLeft),
                                      std::max(1.0f, lanesBottom - lanesTop)));

        const ImVec2 mouse = ImGui::GetMousePos();

        if (ImGui::IsItemActive() && !state.swiping) {
            const int lane = static_cast<int>((mouse.y - lanesTop) / laneHeight);
            if (lane >= 0 && lane < static_cast<int>(group.takes.size())) {
                state.swiping = true;
                state.swipeTake = lane;
                state.swipeAnchor = xToBeat(mouse.x);
                g_UndoHistory.saveState(project, "Comp Swipe");
            }
        }

        if (state.swiping) {
            const float to = xToBeat(mouse.x);
            const float x0 = beatToX(std::min(state.swipeAnchor, to));
            const float x1 = beatToX(std::max(state.swipeAnchor, to));
            draw->AddRect(ImVec2(x0, lanesTop), ImVec2(x1, lanesBottom),
                          IM_COL32(255, 255, 255, 200), 0.0f, 0, 2.0f);

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                comp::chooseTake(group, state.swipeAnchor, to, state.swipeTake);
                comp::flattenAll(project);
                state.swiping = false;
                state.swipeTake = -1;
            }
        }
    }

    ImGui::End();
}

/*
 * Drain the microphone into the take in progress, and split at loop wraps.
 *
 * Called every frame from the main loop, next to the voice panel's own poll -
 * a ring nobody reads fills up and starts dropping audio, and a take
 * silently truncated because the panel was behind another tab is worse than
 * one that never recorded.
 *
 * Takes the beat rather than the sequencer because that is the only thing it
 * needs from one, and a wrap is then something a test can simply hand it.
 */
inline void PollTakeRecording(Project& project, float beat) {
    TakeLanesState& state = g_TakeLanes;
    if (!state.recording) return;
    if (state.selectedGroup < 0 ||
        state.selectedGroup >= static_cast<int>(project.compGroups.size())) {
        return;
    }

    CompGroup& group = project.compGroups[static_cast<size_t>(state.selectedGroup)];

    // Only inside the punch range, if one is set.
    if (comp::insidePunch(beat, state.punchEnabled, state.punchIn, state.punchOut)) {
        static std::vector<float> scratch(4096);
        size_t got = 0;
        while ((got = g_VoiceDevice.ring().read(scratch.data(), scratch.size())) > 0) {
            state.pending.insert(state.pending.end(), scratch.data(),
                                 scratch.data() + got);
            if (got < scratch.size()) break;
        }
    }

    /*
     * A loop wrap ends the pass.
     *
     * Detected by the playhead going backwards, which is what a wrap looks
     * like from here - the sequencer does not announce it. Each pass becomes
     * its own lane, which is the entire point of loop recording.
     */
    if (state.loopStack && beat < state.lastBeat - 1e-3f) {
        commitPendingTake(project, group, state, g_VoiceDevice.sampleRate(),
                          state.lastBeat);
        comp::flattenAll(project);
        state.takeStartBeat = beat;
    }
    state.lastBeat = beat;
}


// ============================================================================
// Voice Mode
//
// One decision at a time: what are you making, make it, keep it. Everything
// else - the detector, the register, the note lengths, the instrument, the
// channel - follows from the first answer.
//
// See VoiceMode.h for why this exists alongside the Voice to Notes panel
// rather than instead of it.
// ============================================================================

static VoiceModeState g_VoiceMode;

inline VoiceModeState& voiceModeState() { return g_VoiceMode; }

// Convert whatever is captured, with the settings the role implies.
inline std::vector<Note> voiceModeConvert(Project& project, VoiceModeState& mode) {
    VoiceToNotesOptions options = g_Voice.options;
    options.role = mode.role;
    options.placeInRegister = true;
    options.transpose = 0;      // the role decides the register, not a nudge

    std::vector<Note> notes;
    if (g_Voice.liveMode) {
        notes = hitsToNotes(g_Voice.tracker.hits(), project.tempoMap, project.bpm,
                            project.beatsPerMeasure, options);
    } else {
        notes = detectedToNotes(g_Voice.detected, project.tempoMap, project.bpm,
                                project.beatsPerMeasure, options);
    }

    for (Note& note : notes) clampNoteToValidRanges(note);
    return notes;
}

inline void DrawVoiceMode(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::Begin("Voice");

    VoiceModeState& mode = g_VoiceMode;
    VoiceCaptureDevice& device = g_VoiceDevice;

    // The tracker needs to know what it is listening for, and the taught
    // drum sounds apply here exactly as they do in the panel.
    g_Voice.tracker.setClassifier(&g_VocalDrums);

    ImGui::TextWrapped(
        "Sing your song a part at a time. Each one plays while you record "
        "the next, so you are playing along with what you have made rather "
        "than into silence.");
    ImGui::Separator();

    // ---- What the song is so far ---------------------------------------------
    if (!mode.parts.empty()) {
        ImGui::TextUnformatted("Your song so far");

        int removeAt = -1;
        for (size_t i = 0; i < mode.parts.size(); ++i) {
            VoicePart& part = mode.parts[i];
            ImGui::PushID(static_cast<int>(3300 + i));

            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.0f), "%s",
                               voiceRoleName(part.role));
            ImGui::SameLine(110.0f);
            ImGui::Text("%s", part.name.c_str());
            ImGui::SameLine(300.0f);
            ImGui::TextDisabled("%d notes", part.noteCount);

            ImGui::SameLine(390.0f);
            if (part.channelIndex >= 0 && part.channelIndex < Project::MAX_CHANNELS) {
                ChannelConfig& channel =
                    project.channels[static_cast<size_t>(part.channelIndex)];
                if (ImGui::SmallButton(channel.muted ? "unmute" : "mute")) {
                    channel.muted = !channel.muted;
                    seq.updateChannelConfigs();
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("remove")) removeAt = static_cast<int>(i);

            ImGui::PopID();
        }

        if (removeAt >= 0) {
            g_UndoHistory.saveState(project, "Remove Voice Part");
            removeVoicePart(project, mode, removeAt);
            seq.updateChannelConfigs();
        }
        ImGui::Separator();
    }

    // ---- Step 1: what are you making? -------------------------------------------
    ImGui::TextUnformatted("What are you making?");

    const VoiceRole ROLES[] = {VoiceRole::Drums, VoiceRole::Bass,
                               VoiceRole::Lead, VoiceRole::Chords};
    for (VoiceRole role : ROLES) {
        const bool selected = (mode.role == role);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.35f, 1.0f));
        }
        if (ImGui::Button(voiceRoleName(role), ImVec2(110, 34))) {
            mode.role = role;
            // The role decides what to listen for. Asking the user to also
            // pick a detector is asking them to know something they should
            // not have to.
            g_Voice.analysisMode = analysisModeForRole(role);
            g_Voice.tracker.setMode(role == VoiceRole::Drums
                                        ? LiveVoiceMode::Drums
                                        : LiveVoiceMode::Melodic);
        }
        if (selected) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::TextDisabled("%s", roleHint(mode.role));

    ImGui::Spacing();

    // ---- Step 2: make it ----------------------------------------------------------
    const bool recording = (mode.step == VoiceModeStep::Recording);

    if (!recording) {
        if (ImGui::Button("Record", ImVec2(160, 44))) {
            mode.warning.clear();
            mode.message.clear();

            if (!device.running()) {
                const VoiceCaptureDevice::DeviceInfo* input = nullptr;
                if (g_Voice.selectedDevice >= 0 &&
                    g_Voice.selectedDevice <
                        static_cast<int>(g_Voice.devices.size())) {
                    input = &g_Voice.devices[
                        static_cast<size_t>(g_Voice.selectedDevice)];
                }
                device.start(input);
            }

            if (!device.running()) {
                // Said plainly. A record button that does nothing is the
                // worst possible failure here, because the user will sing a
                // whole part before finding out.
                mode.warning =
                    "No microphone. Pick one in the Voice to Notes panel.";
            } else {
                g_Voice.tracker.setSampleRate(device.sampleRate());
                g_Voice.tracker.reset();
                g_Voice.take.clear();
                g_Voice.takeSampleRate = device.sampleRate();
                g_Voice.capturingTake = true;
                g_Voice.liveMode = true;
                mode.step = VoiceModeStep::Recording;

                // Play what is already there, so the next part is sung
                // against the song rather than into silence.
                if (!mode.parts.empty()) {
                    seq.setPosition(0.0f);
                    seq.play();
                }
            }
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Stop", ImVec2(160, 44))) {
            g_Voice.capturingTake = false;
            seq.stop();

            mode.pending = voiceModeConvert(project, mode);
            mode.step = VoiceModeStep::Review;

            if (mode.pending.empty()) {
                mode.warning = "Nothing was heard. Try singing a little louder, "
                               "or turn Sensitivity up in the panel.";
                mode.step = VoiceModeStep::Choose;
            }
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "listening...");

        // What it is hearing right now, so a take that is going wrong can be
        // abandoned rather than discovered afterwards.
        ImGui::SameLine(340.0f);
        if (mode.role == VoiceRole::Drums) {
            ImGui::Text("%d hits", static_cast<int>(g_Voice.tracker.hits().size()));
        } else {
            const int note = g_Voice.tracker.currentNote();
            if (note >= 0) {
                static const char* NAMES[] = {"C", "C#", "D", "D#", "E", "F",
                                              "F#", "G", "G#", "A", "A#", "B"};
                ImGui::Text("%s%d", NAMES[note % 12], (note / 12) - 1);
            } else {
                ImGui::TextDisabled("-");
            }
        }
    }

    // ---- Step 3: keep it? ------------------------------------------------------------
    if (mode.step == VoiceModeStep::Review && !mode.pending.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Here is what it heard");

        ImGui::Text("%d note%s", static_cast<int>(mode.pending.size()),
                    mode.pending.size() == 1 ? "" : "s");

        /*
         * A small picture of the take.
         *
         * Counting notes tells you almost nothing about whether the take is
         * any good; seeing its shape tells you at a glance whether the tune
         * you sang came back as the tune you sang.
         */
        {
            const ImVec2 canvas = ImGui::GetCursorScreenPos();
            const float width = std::max(120.0f, ImGui::GetContentRegionAvail().x - 20.0f);
            const float height = 90.0f;
            ImDrawList* draw = ImGui::GetWindowDrawList();

            draw->AddRectFilled(canvas, ImVec2(canvas.x + width, canvas.y + height),
                                IM_COL32(22, 22, 28, 255));

            float lastBeat = 1.0f;
            int lowest = 127, highest = 0;
            for (const Note& note : mode.pending) {
                lastBeat = std::max(lastBeat, note.startTime + note.duration);
                lowest = std::min(lowest, note.pitch);
                highest = std::max(highest, note.pitch);
            }
            if (highest <= lowest) highest = lowest + 12;

            for (const Note& note : mode.pending) {
                const float x0 = canvas.x + (note.startTime / lastBeat) * width;
                const float x1 = canvas.x +
                    ((note.startTime + note.duration) / lastBeat) * width;
                const float t = float(note.pitch - lowest) /
                                float(std::max(1, highest - lowest));
                const float y = canvas.y + height - 6.0f - t * (height - 14.0f);

                draw->AddRectFilled(ImVec2(x0, y), ImVec2(std::max(x1, x0 + 2.0f),
                                                          y + 4.0f),
                                    IM_COL32(120, 200, 255, 255));
            }
            ImGui::Dummy(ImVec2(width, height + 6.0f));
        }

        if (ImGui::Button("Keep it", ImVec2(120, 32))) {
            g_UndoHistory.saveState(project, "Voice Part");

            const VoicePart part = commitVoicePart(project, mode, mode.pending);
            if (part.channelIndex < 0) {
                mode.warning = "No free channel left for another part.";
            } else {
                mode.parts.push_back(part);
                mode.message = part.name + " added";
                seq.updateChannelConfigs();
            }

            mode.pending.clear();
            g_Voice.tracker.clearHits();
            g_Voice.detected.clear();
            mode.step = VoiceModeStep::Choose;
        }

        ImGui::SameLine();
        if (ImGui::Button("Try again", ImVec2(120, 32))) {
            // Only the take in hand is thrown away. Nothing already kept is
            // ever at risk from this button.
            mode.pending.clear();
            g_Voice.tracker.clearHits();
            g_Voice.detected.clear();
            mode.step = VoiceModeStep::Choose;
        }
    }

    if (!mode.warning.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s",
                           mode.warning.c_str());
    } else if (!mode.message.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", mode.message.c_str());
    }

    ImGui::End();
}

inline void DrawTrackerView(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::SetNextWindowPos(ImVec2(930, 645), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tracker", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

    ui.trackerRowsPerBeat = std::clamp(ui.trackerRowsPerBeat, 1, 16);
    ui.trackerOctave = std::clamp(ui.trackerOctave, 0, 9);
    ui.trackerEditStep = std::clamp(ui.trackerEditStep, 0, 16);

    const float stepBeats = 1.0f / static_cast<float>(ui.trackerRowsPerBeat);
    const int rowCount = trackerRowCount(project, stepBeats);

    // ========================================================================
    // Toolbar
    // ========================================================================
    {
        const bool editing = ui.trackerEditMode;
        if (editing) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 50, 60, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(210, 70, 80, 255));
        }
        if (ImGui::Button(editing ? "EDIT ON" : "edit off", ImVec2(90, 0))) {
            ui.trackerEditMode = !ui.trackerEditMode;
        }
        if (editing) ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Space toggles edit mode.\nWith it on, the letter keys write notes.");
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::DragInt("Oct", &ui.trackerOctave, 0.1f, 0, 9);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Base octave for typed notes");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::DragInt("Step", &ui.trackerEditStep, 0.1f, 0, 16);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Rows the cursor advances after entering a note.\n0 keeps it in place.");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    {
        static const int RESOLUTIONS[] = {1, 2, 4, 8, 16};
        static const char* RES_LABELS[] = {"1/4", "1/8", "1/16", "1/32", "1/64"};
        int resIndex = 2;
        for (int i = 0; i < 5; ++i) {
            if (RESOLUTIONS[i] == ui.trackerRowsPerBeat) resIndex = i;
        }
        if (ImGui::Combo("Res", &resIndex, RES_LABELS, 5)) {
            ui.trackerRowsPerBeat = RESOLUTIONS[resIndex];
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much time one row covers");

    ImGui::SameLine();
    ImGui::Checkbox("Follow", &ui.trackerFollowPlayhead);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move the cursor with the playhead");

    ImGui::SameLine();
    ImGui::TextDisabled("| A-K = notes, W/E/T/Y/U = sharps, Del clears");

    ImGui::Separator();

    // ========================================================================
    // Column headers
    // ========================================================================
    const float rowNumWidth = 52.0f;
    const float cellWidth = 104.0f;

    ImGui::Text("Row");
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        ImGui::SameLine(rowNumWidth + ch * cellWidth);
        const ImU32 colour = channelColor(ch);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colour), "%s",
                           project.channels[ch].name.c_str());
    }
    ImGui::Separator();

    // ========================================================================
    // Keyboard
    //
    // Handled before the grid is drawn so the cursor moves and the view
    // scrolls in the same frame the key is pressed.
    // ========================================================================
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    bool cursorMoved = false;

    if (focused && !ImGui::GetIO().WantTextInput) {
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        const bool shift = ImGui::GetIO().KeyShift;

        if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
            ui.trackerEditMode = !ui.trackerEditMode;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   { --ui.trackerCursorRow; cursorMoved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { ++ui.trackerCursorRow; cursorMoved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp))    { ui.trackerCursorRow -= 16; cursorMoved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown))  { ui.trackerCursorRow += 16; cursorMoved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Home))      { ui.trackerCursorRow = 0; cursorMoved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_End))       { ui.trackerCursorRow = rowCount - 1; cursorMoved = true; }

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  { --ui.trackerCursorChannel; cursorMoved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { ++ui.trackerCursorChannel; cursorMoved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
            ui.trackerCursorChannel += shift ? -1 : 1;
            cursorMoved = true;
        }

        // Octave, the way every tracker binds it.
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_UpArrow))   ++ui.trackerOctave;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) --ui.trackerOctave;

        ui.trackerCursorChannel = std::clamp(ui.trackerCursorChannel, 0, Project::MAX_CHANNELS - 1);
        ui.trackerCursorRow = std::clamp(ui.trackerCursorRow, 0,
                                         (rowCount > 0) ? rowCount - 1 : 0);

        if (ui.trackerEditMode) {
            const float cursorBeat = static_cast<float>(ui.trackerCursorRow) * stepBeats;

            if (ImGui::IsKeyPressed(ImGuiKey_Delete) ||
                ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                g_UndoHistory.saveState(project, "Clear Note");
                if (!clearTrackerNote(project, ui.trackerCursorChannel,
                                      cursorBeat, stepBeats)) {
                    // Nothing was there; do not spend an undo step on it.
                    g_UndoHistory.undo(project);
                }
                ui.trackerCursorRow += ui.trackerEditStep;
                cursorMoved = true;
            }

            // The same layout the pad controller uses, so the two agree.
            struct TrackerKey { ImGuiKey key; int offset; };
            static const TrackerKey NOTE_KEYS[] = {
                {ImGuiKey_A, 0}, {ImGuiKey_W, 1}, {ImGuiKey_S, 2}, {ImGuiKey_E, 3},
                {ImGuiKey_D, 4}, {ImGuiKey_F, 5}, {ImGuiKey_T, 6}, {ImGuiKey_G, 7},
                {ImGuiKey_Y, 8}, {ImGuiKey_H, 9}, {ImGuiKey_U, 10}, {ImGuiKey_J, 11},
                {ImGuiKey_K, 12}, {ImGuiKey_O, 13}, {ImGuiKey_L, 14},
                {ImGuiKey_P, 15}, {ImGuiKey_Semicolon, 16}
            };

            for (const TrackerKey& mapping : NOTE_KEYS) {
                if (ctrl || !ImGui::IsKeyPressed(mapping.key)) continue;

                const int pitch = ui.trackerOctave * 12 + mapping.offset;
                if (pitch < 0 || pitch > 127) continue;

                const OscillatorType osc =
                    project.channels[ui.trackerCursorChannel].oscillator.type;

                g_UndoHistory.saveState(project, "Tracker Note");
                const int written = writeTrackerNote(project, ui.trackerCursorChannel,
                                                     cursorBeat, stepBeats, pitch, osc);
                if (written >= 0) {
                    seq.previewNote(pitch, 0.8f, osc, 0.35f);
                    ui.trackerCursorRow += ui.trackerEditStep;
                    cursorMoved = true;
                } else {
                    g_UndoHistory.undo(project);   // nothing was placed
                }
                break;
            }

            ui.trackerCursorRow = std::clamp(ui.trackerCursorRow, 0,
                                             (rowCount > 0) ? rowCount - 1 : 0);
        }
    }

    // The playhead drags the cursor along, unless the user is editing - it
    // would be unusable if the cursor kept jumping out from under them.
    if (ui.trackerFollowPlayhead && seq.isPlaying() && !ui.trackerEditMode) {
        const int playRow = static_cast<int>(seq.getCurrentBeat() / stepBeats);
        if (playRow != ui.trackerCursorRow) {
            ui.trackerCursorRow = std::clamp(playRow, 0, (rowCount > 0) ? rowCount - 1 : 0);
            cursorMoved = true;
        }
    }

    // ========================================================================
    // Grid
    // ========================================================================
    ImGui::BeginChild("TrackerGrid", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (rowCount <= 0) {
        ImGui::TextDisabled("The song is empty. Place a clip in the arrangement,");
        ImGui::TextDisabled("or turn on edit mode and type - a pattern will be made for you.");
    } else {
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

        // Keep the cursor on screen after a keyboard move. Done here rather
        // than with SetScrollHereY because the clipper means the cursor row
        // is usually not submitted as a widget at all.
        if (cursorMoved) {
            const float target = static_cast<float>(ui.trackerCursorRow) * rowHeight;
            const float top = ImGui::GetScrollY();
            const float bottom = top + ImGui::GetContentRegionAvail().y - rowHeight;
            if (target < top) {
                ImGui::SetScrollY(target);
            } else if (target > bottom) {
                ImGui::SetScrollY(target - ImGui::GetContentRegionAvail().y + rowHeight * 2.0f);
            }
        }

        const int rowsPerBar = ui.trackerRowsPerBeat *
                               ((project.beatsPerMeasure > 0) ? project.beatsPerMeasure : 4);
        const int playRow = seq.isPlaying()
            ? static_cast<int>(seq.getCurrentBeat() / stepBeats) : -1;

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImGuiListClipper clipper;
        clipper.Begin(rowCount, rowHeight);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const float absBeat = static_cast<float>(row) * stepBeats;
                const bool isBar = (rowsPerBar > 0) && (row % rowsPerBar == 0);
                const bool isBeat = (row % ui.trackerRowsPerBeat) == 0;
                const bool isPlayRow = (row == playRow);

                // Bar and beat shading, so the grid can be read at a glance.
                if (isBar || isBeat || isPlayRow) {
                    const ImVec2 p0 = ImGui::GetCursorScreenPos();
                    const float width = rowNumWidth + Project::MAX_CHANNELS * cellWidth;
                    const ImU32 shade = isPlayRow ? IM_COL32(240, 200, 60, 45)
                                      : isBar     ? IM_COL32(255, 255, 255, 20)
                                                  : IM_COL32(255, 255, 255, 8);
                    drawList->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + rowHeight), shade);
                }

                if (isPlayRow) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%03d", row);
                } else if (isBar) {
                    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "%03d", row);
                } else {
                    ImGui::TextDisabled("%03d", row);
                }

                for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
                    ImGui::SameLine(rowNumWidth + ch * cellWidth);

                    const TrackerCell cell =
                        readTrackerCell(project, ch, absBeat, stepBeats);

                    char label[64];
                    if (cell.hasNote) {
                        const std::string name = noteToString(cell.pitch);
                        const std::string osc = oscillatorTypeToString(cell.oscillator);
                        snprintf(label, sizeof(label), "%-4s %.2s##r%dc%d",
                                 name.c_str(), osc.c_str(), row, ch);
                    } else if (cell.patternIndex >= 0) {
                        // A clip is here but this row is empty.
                        snprintf(label, sizeof(label), "---##r%dc%d", row, ch);
                    } else {
                        // No clip at all: typing here would create one.
                        snprintf(label, sizeof(label), "   ##r%dc%d", row, ch);
                    }

                    const bool isCursor = (row == ui.trackerCursorRow &&
                                           ch == ui.trackerCursorChannel);

                    if (cell.hasNote) {
                        ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::ColorConvertU32ToFloat4(channelColor(ch)));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.42f, 0.48f, 1.0f));
                    }

                    if (ImGui::Selectable(label, isCursor,
                                          ImGuiSelectableFlags_None,
                                          ImVec2(cellWidth - 6.0f, 0.0f))) {
                        ui.trackerCursorRow = row;
                        ui.trackerCursorChannel = ch;
                    }
                    ImGui::PopStyleColor();
                }
            }
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

// ============================================================================
// Arrangement Timeline
// ============================================================================
inline void DrawArrangement(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::SetNextWindowPos(ImVec2(220, 835), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900, 150), ImGuiCond_FirstUseEver);
    ImGui::Begin("Arrangement", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

    // ========================================================================
    // Toolbar
    // ========================================================================
    ImGui::Text("Song Length:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    int songLen = static_cast<int>(project.songLength);
    if (ImGui::InputInt("##songlen", &songLen, 4, 16)) {
        project.songLength = std::clamp(static_cast<float>(songLen), 4.0f, 256.0f);
    }
    ImGui::SameLine();
    ImGui::Text("beats");

    ImGui::SameLine(0, 20);
    ImGui::Text("Pattern:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##arrpattern", ui.selectedPattern >= 0 && ui.selectedPattern < static_cast<int>(project.patterns.size())
            ? project.patterns[ui.selectedPattern].name.c_str() : "Select...")) {
        for (size_t i = 0; i < project.patterns.size(); ++i) {
            bool isSelected = (static_cast<int>(i) == ui.selectedPattern);
            if (ImGui::Selectable(project.patterns[i].name.c_str(), isSelected)) {
                ui.selectedPattern = static_cast<int>(i);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0, 20);
    if (ImGui::Button("Clear All##arr")) {
        project.arrangement.clear();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove all clips from arrangement");

    ImGui::SameLine(0, 10);
    static int selectedClipIndex = -1;
    if (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(project.arrangement.size())) {
        if (ImGui::Button("Delete Selected##arr")) {
            g_UndoHistory.saveState(project, "Delete Clip");
            project.arrangement.erase(project.arrangement.begin() + selectedClipIndex);
            selectedClipIndex = -1;
        }

        // An audio clip and a pattern clip are edited by different things.
        // Transpose means nothing to a recording, and gain and fades mean
        // nothing to a pattern reference, so the toolbar shows one set or
        // the other rather than a row of controls half of which are dead.
        if (selectedClipIndex >= 0 &&
            project.arrangement[selectedClipIndex].type == ClipType::Audio) {
            Clip& selected = project.arrangement[selectedClipIndex];

            ImGui::SameLine(0, 12);
            ImGui::SetNextItemWidth(100);
            float gain = selected.gain;
            if (ImGui::DragFloat("##clipgain", &gain, 0.005f, 0.0f, 4.0f, "Gain: %.2f")) {
                g_UndoHistory.saveState(project, "Clip Gain");
                selected.gain = std::clamp(gain, 0.0f, 4.0f);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Level of this clip, before the channel fader.");
            }

            ImGui::SameLine(0, 8);
            ImGui::SetNextItemWidth(95);
            float fadeIn = selected.fadeInBeats;
            if (ImGui::DragFloat("##clipfi", &fadeIn, 0.02f, 0.0f, 64.0f, "In: %.2f b")) {
                g_UndoHistory.saveState(project, "Clip Fade In");
                selected.fadeInBeats = std::clamp(fadeIn, 0.0f, 64.0f);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Fade-in length, in beats - so it follows the tempo.");
            }

            ImGui::SameLine(0, 8);
            ImGui::SetNextItemWidth(100);
            float fadeOut = selected.fadeOutBeats;
            if (ImGui::DragFloat("##clipfo", &fadeOut, 0.02f, 0.0f, 64.0f, "Out: %.2f b")) {
                g_UndoHistory.saveState(project, "Clip Fade Out");
                selected.fadeOutBeats = std::clamp(fadeOut, 0.0f, 64.0f);
            }

            ImGui::SameLine(0, 8);
            ImGui::SetNextItemWidth(110);
            float trimStart = selected.trimStartSeconds;
            if (ImGui::DragFloat("##cliptrim", &trimStart, 0.005f, 0.0f, 3600.0f,
                                 "Start: %.3f s")) {
                g_UndoHistory.saveState(project, "Clip Trim");
                selected.trimStartSeconds = std::max(0.0f, trimStart);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Where playback starts inside the sample.\n"
                    "Use it to cut off silence before the first hit.");
            }

            ImGui::SameLine(0, 8);
            bool loopClip = selected.loopClip;
            if (ImGui::Checkbox("Loop##clip", &loopClip)) {
                g_UndoHistory.saveState(project, "Clip Loop");
                selected.loopClip = loopClip;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Repeat the sample to fill the clip.\n"
                    "Off, the clip falls silent once the sample runs out.");
            }

            /*
             * Fit the clip's audio to its length on the timeline.
             *
             * This is where time stretching is actually wanted, and it is
             * deliberately not an insert effect: an insert gets one sample
             * per sample, so there is nowhere for the extra time to go. Here
             * there is - the whole buffer is rewritten.
             *
             * It writes a NEW sample rather than editing the existing one,
             * because the pool is shared and another clip may be using it.
             */
            ImGui::SameLine(0, 12);
            if (ImGui::Button("Fit to Clip")) {
                const Sample* source = project.samplePool.getSample(selected.sampleId);
                if (source != nullptr && source->lengthSeconds > 0.01f) {
                    const float wantSeconds =
                        project.beatToSeconds(selected.startBeat + selected.lengthBeats) -
                        project.beatToSeconds(selected.startBeat);
                    const float ratio = wantSeconds / source->lengthSeconds;

                    Sample stretched = *source;
                    stretched.audioData = timeStretch(source->audioData, ratio);
                    stretched.lengthSeconds =
                        static_cast<float>(stretched.audioData.size()) /
                        static_cast<float>(std::max(1, stretched.sampleRate));
                    stretched.name = source->name + " (fitted)";
                    stretched.filepath.clear();   // no longer what is on disk

                    const int id = project.samplePool.addSample(stretched);
                    if (id >= 0) {
                        g_UndoHistory.saveState(project, "Fit Audio to Clip");
                        selected.sampleId = id;
                        selected.trimStartSeconds = 0.0f;
                        selected.trimEndSeconds = 0.0f;
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Stretch the audio to exactly fill this clip, without\n"
                    "changing its pitch - which is what you want for fitting\n"
                    "an imported break to the project tempo.\n\n"
                    "Writes a new sample rather than editing the original,\n"
                    "since another clip may be using it.");
            }

            const Sample* s = project.samplePool.getSample(selected.sampleId);
            if (s == nullptr) {
                ImGui::SameLine(0, 12);
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "audio missing");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "The file this clip pointed at could not be found.\n"
                        "The clip and its edits are intact - use Import Audio\n"
                        "to point it at the file again.");
                }
            }
        }
        // Per-clip transpose: the same pattern placed at 0, +8, +3, +10 is
        // a whole progression from one bassline. The pattern is untouched;
        // only this placement moves.
        else if (selectedClipIndex >= 0) {
            ImGui::SameLine(0, 12);
            ImGui::SetNextItemWidth(110);
            Clip& selected = project.arrangement[selectedClipIndex];
            int transpose = selected.transpose;
            if (ImGui::DragInt("##cliptr", &transpose, 0.1f, -48, 48,
                               (transpose == 0) ? "Transpose: 0"
                                                : "Transpose: %+d st")) {
                g_UndoHistory.saveState(project, "Transpose Clip");
                selected.transpose = std::clamp(transpose, -48, 48);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Shift every note of this placement by semitones.\n"
                    "The pattern itself is untouched, so the same pattern\n"
                    "placed four times at 0, +8, +3 and +10 plays a whole\n"
                    "progression from one bassline.");
            }
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Delete Selected##arr");
        ImGui::EndDisabled();
    }

    // ---- Audio in ---------------------------------------------------------
    //
    // The sample pool and the audio recorder both existed and had no way in;
    // this is it. An imported file lands on the selected channel at the
    // playhead, sized to its own length, so it is immediately in the right
    // place rather than at beat zero on channel zero.
    ImGui::SameLine(0, 20);
    if (ImGui::Button("Import Audio##arr")) {
        const std::string path = openFileDialog(
            "Audio Files (*.wav;*.mp3;*.ogg;*.flac)\0*.wav;*.mp3;*.ogg;*.flac\0"
            "All Files (*.*)\0*.*\0",
            "wav");
        if (!path.empty()) {
            const int id = project.samplePool.loadSample(path);
            if (id >= 0) {
                const Sample* s = project.samplePool.getSample(id);
                g_UndoHistory.saveState(project, "Import Audio");

                // If an audio clip is selected, this relinks it - which is
                // how a user fixes a file that moved without losing the trim
                // and the fades they already set.
                if (selectedClipIndex >= 0 &&
                    selectedClipIndex < static_cast<int>(project.arrangement.size()) &&
                    project.arrangement[selectedClipIndex].type == ClipType::Audio) {
                    project.arrangement[selectedClipIndex].sampleId = id;
                } else {
                    Clip clip;
                    clip.type = ClipType::Audio;
                    clip.sampleId = id;
                    clip.channelIndex = std::clamp(ui.selectedChannel, 0,
                                                   project.activeChannelCount() - 1);
                    clip.startBeat = std::max(0.0f,
                        snapBeat(seq.getCurrentBeat(), effectiveSnap(ui),
                                 project.beatsPerMeasure));

                    // Sized to the audio's own length so it plays whole.
                    const float secondsPerBeat =
                        60.0f / ((project.bpm > 1.0f) ? project.bpm : 120.0f);
                    const float beats = (s != nullptr && secondsPerBeat > 0.0f)
                        ? s->lengthSeconds / secondsPerBeat : 4.0f;
                    clip.lengthBeats = std::clamp(beats, 0.25f, 256.0f);

                    project.arrangement.push_back(clip);
                    selectedClipIndex = static_cast<int>(project.arrangement.size()) - 1;
                }
            } else if (std::find(project.missingSamples.begin(),
                                 project.missingSamples.end(), path) ==
                       project.missingSamples.end()) {
                // A file that will not decode is reported through the same
                // list as one that could not be found, rather than through a
                // second mechanism that says the same thing differently.
                project.missingSamples.push_back(path);
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Place a WAV, MP3, OGG or FLAC on the timeline as an audio clip.\n"
            "It lands on the selected channel at the playhead and runs\n"
            "through that channel's volume, pan, effects and sends - so a\n"
            "recorded part can use the channel's reverb like a played one.\n\n"
            "With an audio clip selected, this relinks that clip instead,\n"
            "which is how you repair one whose file moved.");
    }

    // ---- Song structure ---------------------------------------------------
    //
    // The project had one bpm and one time signature for the whole song. A
    // chiptune with a slow intro, a double-time B section or a 6/8 bridge
    // could not be written at all.
    ImGui::SameLine(0, 20);
    if (ImGui::Button("Structure##arr")) ImGui::OpenPopup("SongStructure");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Tempo changes, time-signature changes, markers and regions.\n"
            "Right-clicking the strip under the loop ruler also drops a\n"
            "marker where you click.");
    }

    if (ImGui::BeginPopup("SongStructure")) {
        const float playhead = std::max(0.0f, seq.getCurrentBeat());

        ImGui::TextDisabled("Everything below applies from a beat onward.");
        ImGui::Separator();

        // ---- Tempo ---------------------------------------------------------
        ImGui::TextUnformatted("Tempo changes");
        if (ImGui::Button("Add at playhead##tempo")) {
            g_UndoHistory.saveState(project, "Add Tempo Change");
            project.tempoMap.setTempo(
                snapBeatNearest(playhead, effectiveSnap(ui), project),
                project.bpmAt(playhead));
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "A tempo change takes effect at its beat and holds until the\n"
                "next one. There is no ramp - an accelerando is a different\n"
                "feature, and pretending a step is a ramp would put the\n"
                "playhead somewhere it is not.");
        }

        for (int i = 0; i < project.tempoMap.tempoCount(); ++i) {
            ImGui::PushID(1000 + i);
            const TempoChange change = project.tempoMap.tempoAt(i);

            ImGui::SetNextItemWidth(80);
            float beat = change.beat;
            if (ImGui::DragFloat("##tbeat", &beat, 0.25f, 0.0f, 100000.0f, "@ %.2f")) {
                g_UndoHistory.saveState(project, "Move Tempo Change");
                project.tempoMap.removeTempoAt(i);
                project.tempoMap.setTempo(std::max(0.0f, beat), change.bpm);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            float bpm = change.bpm;
            if (ImGui::DragFloat("##tbpm", &bpm, 0.5f, TempoMap::MIN_BPM,
                                 TempoMap::MAX_BPM, "%.1f BPM")) {
                g_UndoHistory.saveState(project, "Tempo Change");
                project.tempoMap.setTempo(change.beat, bpm);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                g_UndoHistory.saveState(project, "Remove Tempo Change");
                project.tempoMap.removeTempoAt(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        ImGui::Separator();

        // ---- Meter ---------------------------------------------------------
        ImGui::TextUnformatted("Time signature changes");
        if (ImGui::Button("Add at playhead##meter")) {
            const MeterChange here = project.meterAt(playhead);
            g_UndoHistory.saveState(project, "Add Meter Change");
            project.tempoMap.setMeter(
                snapBeatNearest(playhead, effectiveSnap(ui), project),
                here.numerator, here.denominator);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Bar lines and Snap-to-Bar both follow this, so a 6/8\n"
                "section snaps to six eighths rather than to the project's\n"
                "opening signature.");
        }

        for (int i = 0; i < project.tempoMap.meterCount(); ++i) {
            ImGui::PushID(2000 + i);
            const MeterChange change = project.tempoMap.meterAt(i);

            ImGui::SetNextItemWidth(80);
            float beat = change.beat;
            if (ImGui::DragFloat("##mbeat", &beat, 0.25f, 0.0f, 100000.0f, "@ %.2f")) {
                g_UndoHistory.saveState(project, "Move Meter Change");
                project.tempoMap.removeMeterAt(i);
                project.tempoMap.setMeter(std::max(0.0f, beat),
                                          change.numerator, change.denominator);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55);
            int numerator = change.numerator;
            if (ImGui::DragInt("##mnum", &numerator, 0.1f, 1, 32, "%d")) {
                g_UndoHistory.saveState(project, "Meter Change");
                project.tempoMap.setMeter(change.beat, numerator, change.denominator);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("/");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            // Powers of two only: MIDI stores the denominator as its base-2
            // logarithm, so a 4/5 bar has no representation at all.
            const char* denominators = "1\0" "2\0" "4\0" "8\0" "16\0" "32\0";
            int denomIndex = 2;
            for (int d = 0, value = 1; d < 6; ++d, value *= 2) {
                if (value == change.denominator) denomIndex = d;
            }
            if (ImGui::Combo("##mden", &denomIndex, denominators)) {
                g_UndoHistory.saveState(project, "Meter Change");
                project.tempoMap.setMeter(change.beat, change.numerator,
                                          1 << denomIndex);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                g_UndoHistory.saveState(project, "Remove Meter Change");
                project.tempoMap.removeMeterAt(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        ImGui::Separator();

        // ---- Markers and regions -------------------------------------------
        ImGui::TextUnformatted("Markers");
        if (ImGui::Button("Add at playhead##marker") &&
            static_cast<int>(project.markers.size()) < MAX_MARKERS) {
            g_UndoHistory.saveState(project, "Add Marker");
            Marker marker;
            marker.beat = snapBeatNearest(playhead, effectiveSnap(ui), project);
            marker.name = "Marker " + std::to_string(project.markers.size() + 1);
            marker.color = channelColor(static_cast<int>(project.markers.size()));
            project.markers.push_back(marker);
            sortMarkers(project.markers);
        }

        for (size_t i = 0; i < project.markers.size(); ++i) {
            ImGui::PushID(3000 + static_cast<int>(i));
            Marker& marker = project.markers[i];

            ImGui::SetNextItemWidth(80);
            if (ImGui::DragFloat("##kbeat", &marker.beat, 0.25f, 0.0f, 100000.0f,
                                 "@ %.2f")) {
                marker.beat = std::max(0.0f, marker.beat);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130);
            char nameBuffer[64];
            snprintf(nameBuffer, sizeof(nameBuffer), "%s", marker.name.c_str());
            if (ImGui::InputText("##kname", nameBuffer, sizeof(nameBuffer))) {
                marker.name = nameBuffer;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("go")) {
                seq.setPosition(marker.beat);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                g_UndoHistory.saveState(project, "Remove Marker");
                project.markers.erase(project.markers.begin() +
                                      static_cast<long>(i));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Regions");
        if (ImGui::Button("Add from loop range##region") &&
            static_cast<int>(project.regions.size()) < MAX_REGIONS) {
            const PlaybackState& pb = seq.getState();
            const float a = pb.loopRangeActive ? pb.loopStart : playhead;
            const float b = pb.loopRangeActive ? pb.loopEnd : playhead + 8.0f;
            if (b > a) {
                g_UndoHistory.saveState(project, "Add Region");
                Region region;
                region.startBeat = a;
                region.endBeat = b;
                region.name = "Section " + std::to_string(project.regions.size() + 1);
                region.color = channelColor(static_cast<int>(project.regions.size()) + 3);
                project.regions.push_back(region);
                sortRegions(project.regions);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Names the span you have looped, so the structure you were\n"
                "working on stays labelled after you move the loop.");
        }

        for (size_t i = 0; i < project.regions.size(); ++i) {
            ImGui::PushID(4000 + static_cast<int>(i));
            Region& region = project.regions[i];

            ImGui::SetNextItemWidth(130);
            char nameBuffer[64];
            snprintf(nameBuffer, sizeof(nameBuffer), "%s", region.name.c_str());
            if (ImGui::InputText("##rname", nameBuffer, sizeof(nameBuffer))) {
                region.name = nameBuffer;
            }
            ImGui::SameLine();
            ImGui::Text("%.1f - %.1f", region.startBeat, region.endBeat);
            ImGui::SameLine();
            if (ImGui::SmallButton("loop")) {
                seq.setLoopRange(region.startBeat, region.endBeat);
                seq.setLoopEnabled(true);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                g_UndoHistory.saveState(project, "Remove Region");
                project.regions.erase(project.regions.begin() +
                                      static_cast<long>(i));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine(0, 20);
    ImGui::TextDisabled("Double-click to add clip | Right-click clip to delete | Drag to move");

    // A file that could not be found is worth saying once, plainly, rather
    // than leaving the user to wonder why a clip is silent.
    if (!project.missingSamples.empty()) {
        ImGui::SameLine(0, 16);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%d audio file%s missing",
                           static_cast<int>(project.missingSamples.size()),
                           project.missingSamples.size() == 1 ? "" : "s");
        if (ImGui::IsItemHovered()) {
            std::string list;
            for (const std::string& p : project.missingSamples) {
                list += p;
                list += "\n";
            }
            ImGui::SetTooltip(
                "These files could not be found when the project loaded:\n\n%s\n"
                "Their clips kept their place and their edits. Select one and\n"
                "use Import Audio to point it at the file again.", list.c_str());
        }
    }

    ImGui::Separator();

    // ========================================================================
    // Canvas setup
    // ========================================================================
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y = std::max(canvasSize.y, 290.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float trackHeight = 30.0f;
    const float headerWidth = 100.0f;
    const float beatWidth = 20.0f * ui.zoomX;

    /*
     * The rulers sit ABOVE the lanes.
     *
     * They used to be drawn under the last lane, at canvasPos.y + laneCount
     * * trackHeight. At eight channels that was 240px down and on screen. At
     * thirty-two it is 960px down - off the bottom of a panel nowhere near
     * that tall, which made the loop range unreachable by mouse. A position
     * that depends on the channel count is the bug; this one does not.
     */
    const float loopBarH = 16.0f;
    const float regionBarH = 15.0f;   // region bands and their names
    const float markerBarH = 16.0f;   // marker flags and their names
    // Plus a lane for the bar numbers, which shared the marker row and drew
    // straight through the marker names.
    const float rulerHeight = loopBarH + regionBarH + markerBarH + 16.0f;
    const float tracksTop = canvasPos.y + rulerHeight;

    // Static state for dragging
    static bool isDraggingClip = false;
    static int draggingClipIndex = -1;
    static float dragStartBeat = 0.0f;
    static int dragStartChannel = 0;

    // Background
    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(25, 25, 30, 255));

    // Channel headers and tracks
    // The arrangement draws one lane per active channel. It was eight,
    // hardcoded, in seven separate places.
    const int laneCount = project.activeChannelCount();
    for (int ch = 0; ch < laneCount; ++ch) {
        float y = tracksTop + ch * trackHeight;

        // Header
        ImU32 headerColor = (ch == ui.selectedChannel)
            ? IM_COL32(60, 60, 70, 255)
            : IM_COL32(40, 40, 45, 255);
        drawList->AddRectFilled(
            ImVec2(canvasPos.x, y),
            ImVec2(canvasPos.x + headerWidth, y + trackHeight - 1),
            headerColor);
        drawList->AddText(ImVec2(canvasPos.x + 5, y + 8),
            channelColor(ch), project.channels[ch].name.c_str());

        // Track background
        ImU32 trackColor = (ch % 2 == 0) ? IM_COL32(35, 35, 40, 255) : IM_COL32(30, 30, 35, 255);
        drawList->AddRectFilled(
            ImVec2(canvasPos.x + headerWidth, y),
            ImVec2(canvasPos.x + canvasSize.x, y + trackHeight - 1),
            trackColor);
    }

    // Beat grid
    //
    // Bar lines are walked through the meter map rather than found by
    // `beat % beatsPerMeasure`. A song that changes to 3/4 at bar 8 has bar
    // lines at 32, 35, 38 - the modulo puts every one of them after the
    // change in the wrong place, and the grid then snaps somewhere the ruler
    // did not draw. Snap-to-Bar walks the same beatOfBar().
    {
        // Collect this screen's bar lines once, so the per-beat loop below
        // can ask "is this one" without re-walking the map each time.
        std::vector<float> barBeats;
        for (int bar = 0; bar < 4096; ++bar) {
            const float barBeat = project.beatOfBar(bar);
            if (barBeat > project.songLength) break;
            barBeats.push_back(barBeat);
            // A degenerate map could return the same beat forever.
            if (bar > 0 && barBeat <= barBeats[static_cast<size_t>(bar) - 1]) break;
        }

        for (float beat = 0; beat < project.songLength; beat += 1.0f) {
            float x = canvasPos.x + headerWidth + beat * beatWidth - ui.scrollX;
            if (x < canvasPos.x + headerWidth || x > canvasPos.x + canvasSize.x) continue;

            drawList->AddLine(
                ImVec2(x, tracksTop),
                ImVec2(x, tracksTop + laneCount * trackHeight),
                IM_COL32(45, 45, 50, 255));
        }

        for (size_t bar = 0; bar < barBeats.size(); ++bar) {
            const float beat = barBeats[bar];
            const float x = canvasPos.x + headerWidth + beat * beatWidth - ui.scrollX;
            if (x < canvasPos.x + headerWidth || x > canvasPos.x + canvasSize.x) continue;

            drawList->AddLine(
                ImVec2(x, tracksTop),
                ImVec2(x, tracksTop + laneCount * trackHeight),
                IM_COL32(80, 80, 90, 255));

            // The bar number goes at the TOP now, on the ruler, where it is
            // legible whatever the channel count.
            char beatLabel[16];
            snprintf(beatLabel, sizeof(beatLabel), "%d", static_cast<int>(beat));
            drawList->AddText(ImVec2(x + 2, tracksTop - 14.0f),
                IM_COL32(120, 120, 135, 255), beatLabel);
        }
    }

    // ========================================================================
    // Loop range ruler
    //
    // loopStart and loopEnd have been in PlaybackState since the beginning,
    // but nothing could set them and the engine ignored loopEnd anyway.
    // Looping a couple of bars to iterate on them is the central motion of
    // writing a chiptune, so it gets a strip of its own under the tracks.
    // ========================================================================
    const float gridLeft = canvasPos.x + headerWidth;
    const float gridRight = canvasPos.x + canvasSize.x;
    const float loopBarY = canvasPos.y;

    auto beatToScreenX = [&](float beat) {
        return gridLeft + beat * beatWidth - ui.scrollX;
    };
    auto screenXToBeat = [&](float x) {
        return (beatWidth > 0.0f) ? (x - gridLeft + ui.scrollX) / beatWidth : 0.0f;
    };

    drawList->AddRectFilled(ImVec2(gridLeft, loopBarY),
                            ImVec2(gridRight, loopBarY + loopBarH),
                            IM_COL32(22, 22, 28, 255));

    {
        const ImVec2 mousePos = ImGui::GetMousePos();
        const bool overLoopBar = mousePos.x >= gridLeft && mousePos.x <= gridRight &&
                                 mousePos.y >= loopBarY &&
                                 mousePos.y <= loopBarY + loopBarH;

        auto beatUnderMouse = [&]() {
            return std::max(0.0f, snapBeatNearest(screenXToBeat(mousePos.x),
                                                  effectiveSnap(ui),
                                                  project.beatsPerMeasure));
        };

        if (overLoopBar && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            ui.isDraggingLoopRange = true;
            ui.loopDragAnchorBeat = beatUnderMouse();
        }

        if (ui.isDraggingLoopRange) {
            const float other = beatUnderMouse();
            seq.setLoopRange(std::min(ui.loopDragAnchorBeat, other),
                             std::max(ui.loopDragAnchorBeat, other));

            if (ImGui::IsMouseReleased(0)) {
                ui.isDraggingLoopRange = false;
                // A click with no drag clears the range rather than leaving a
                // zero-length loop that would freeze the playhead in place.
                if (std::fabs(other - ui.loopDragAnchorBeat) < MIN_LOOP_BEATS) {
                    seq.clearLoopRange();
                }
            }
        }
    }

    {
        const PlaybackState& pb = seq.getState();
        if (pb.loopRangeActive) {
            const float xa = std::max(gridLeft, beatToScreenX(pb.loopStart));
            const float xb = std::min(gridRight, beatToScreenX(pb.loopEnd));
            if (xb > xa) {
                // Shade the looping span across the tracks so the range is
                // readable without looking down at the ruler.
                drawList->AddRectFilled(ImVec2(xa, tracksTop),
                                        ImVec2(xb, tracksTop + laneCount * trackHeight),
                                        IM_COL32(90, 170, 255, 22));
                drawList->AddRectFilled(ImVec2(xa, loopBarY),
                                        ImVec2(xb, loopBarY + loopBarH),
                                        IM_COL32(90, 170, 255, 150));
                drawList->AddLine(ImVec2(xa, tracksTop),
                                  ImVec2(xa, loopBarY + loopBarH),
                                  IM_COL32(150, 205, 255, 220), 1.5f);
                drawList->AddLine(ImVec2(xb, tracksTop),
                                  ImVec2(xb, loopBarY + loopBarH),
                                  IM_COL32(150, 205, 255, 220), 1.5f);

                char loopLabel[48];
                snprintf(loopLabel, sizeof(loopLabel), "LOOP %.2f - %.2f",
                         pb.loopStart, pb.loopEnd);
                drawList->AddText(ImVec2(xa + 5, loopBarY + 1),
                                  IM_COL32(255, 255, 255, 230), loopLabel);
            }
        } else {
            drawList->AddText(ImVec2(gridLeft + 6, loopBarY + 1),
                              IM_COL32(110, 110, 125, 255),
                              "drag here to set a loop range");
        }
    }

    // ========================================================================
    // Markers, regions and tempo changes
    //
    // All three live on one strip under the loop ruler. They are the song's
    // structure - "the chorus starts here", "it slows down here" - and a
    // structure you cannot see is a structure you have to remember.
    // ========================================================================
    const float regionBarY = loopBarY + loopBarH + 1.0f;
    const float markerBarY = regionBarY + regionBarH + 1.0f;

    drawList->AddRectFilled(ImVec2(gridLeft, regionBarY),
                            ImVec2(gridRight, markerBarY + markerBarH),
                            IM_COL32(18, 18, 24, 255));

    // Regions first, as a filled band behind the markers.
    for (const Region& region : project.regions) {
        const float xa = beatToScreenX(region.startBeat);
        const float xb = beatToScreenX(region.endBeat);
        if (xb < gridLeft || xa > gridRight) continue;

        const float ca = std::max(xa, gridLeft);
        const float cb = std::min(xb, gridRight);
        const ImU32 fill = (region.color & 0x00FFFFFFu) | 0x40000000u;
        drawList->AddRectFilled(ImVec2(ca, regionBarY),
                                ImVec2(cb, regionBarY + regionBarH), fill);
        drawList->AddRect(ImVec2(ca, regionBarY),
                          ImVec2(cb, regionBarY + regionBarH), region.color);
        if (!region.name.empty() && cb - ca > 30.0f) {
            drawList->AddText(ImVec2(ca + 4, regionBarY + 1),
                              IM_COL32(235, 235, 245, 255), region.name.c_str());
        }
    }

    // Then the markers, as a flag with a line dropped through the tracks so
    // it can be read against the clips it names.
    for (const Marker& marker : project.markers) {
        const float mx = beatToScreenX(marker.beat);
        if (mx < gridLeft || mx > gridRight) continue;

        drawList->AddLine(ImVec2(mx, markerBarY),
                          ImVec2(mx, tracksTop + laneCount * trackHeight),
                          (marker.color & 0x00FFFFFFu) | 0x70000000u);
        drawList->AddTriangleFilled(ImVec2(mx, markerBarY),
                                    ImVec2(mx + 9, markerBarY + 4),
                                    ImVec2(mx, markerBarY + 8),
                                    marker.color);
        if (!marker.name.empty()) {
            drawList->AddText(ImVec2(mx + 11, markerBarY + 1),
                              IM_COL32(225, 225, 235, 255), marker.name.c_str());
        }
    }

    // Tempo and meter changes sit on the same strip, right-aligned in their
    // own colour, because they answer a different question than a marker
    // does and mixing them would make both harder to read.
    for (int i = 0; i < project.tempoMap.tempoCount(); ++i) {
        const TempoChange& change = project.tempoMap.tempoAt(i);
        const float tx = beatToScreenX(change.beat);
        if (tx < gridLeft || tx > gridRight) continue;

        drawList->AddLine(ImVec2(tx, markerBarY),
                          ImVec2(tx, tracksTop + laneCount * trackHeight),
                          IM_COL32(255, 190, 90, 130));
        // Right-aligned against its own line. A tempo change usually sits on
        // a bar line, and the bar number is drawn to the RIGHT of that line -
        // so a left-aligned label lands straight on top of it.
        char label[24];
        snprintf(label, sizeof(label), "%.0f", change.bpm);
        const float labelWidth = ImGui::CalcTextSize(label).x;
        drawList->AddText(ImVec2(tx - labelWidth - 3.0f, markerBarY + markerBarH + 1),
                          IM_COL32(255, 200, 110, 255), label);
    }
    for (int i = 0; i < project.tempoMap.meterCount(); ++i) {
        const MeterChange& change = project.tempoMap.meterAt(i);
        const float tx = beatToScreenX(change.beat);
        if (tx < gridLeft || tx > gridRight) continue;

        char label[24];
        // Right-aligned for the same reason: a meter change lands on a bar
        // line, and a region starting there names itself to the right of it.
        snprintf(label, sizeof(label), "%d/%d", change.numerator, change.denominator);
        const float labelWidth = ImGui::CalcTextSize(label).x;
        drawList->AddText(ImVec2(tx - labelWidth - 3.0f, regionBarY + 1),
                          IM_COL32(140, 230, 190, 255), label);
    }

    // Right-click the strip to drop a marker where the cursor is.
    {
        const ImVec2 mousePos = ImGui::GetMousePos();
        const bool overStrip = mousePos.x >= gridLeft && mousePos.x <= gridRight &&
                               mousePos.y >= regionBarY &&
                               mousePos.y <= markerBarY + markerBarH;

        if (overStrip && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(1) &&
            static_cast<int>(project.markers.size()) < MAX_MARKERS) {
            const float beat = std::max(0.0f,
                snapBeatNearest(screenXToBeat(mousePos.x), effectiveSnap(ui), project));

            g_UndoHistory.saveState(project, "Add Marker");
            Marker marker;
            marker.beat = beat;
            marker.name = "Marker " + std::to_string(project.markers.size() + 1);
            marker.color = channelColor(static_cast<int>(project.markers.size()));
            project.markers.push_back(marker);
            sortMarkers(project.markers);
        }

        if (overStrip && project.markers.empty() && project.regions.empty()) {
            drawList->AddText(ImVec2(gridLeft + 6, markerBarY + 1),
                              IM_COL32(110, 110, 125, 255),
                              "right-click to drop a marker");
        }
    }

    // Draw clips
    int hoveredClipIndex = -1;
    for (size_t i = 0; i < project.arrangement.size(); ++i) {
        const auto& clip = project.arrangement[i];
        float x = canvasPos.x + headerWidth + clip.startBeat * beatWidth - ui.scrollX;
        float y = tracksTop + clip.channelIndex * trackHeight;
        float w = clip.lengthBeats * beatWidth;

        if (x + w < canvasPos.x + headerWidth || x > canvasPos.x + canvasSize.x) continue;

        // Check if hovered
        ImVec2 mousePos = ImGui::GetMousePos();
        bool isHovered = (mousePos.x >= x && mousePos.x <= x + w &&
                          mousePos.y >= y && mousePos.y <= y + trackHeight);
        bool isSelected = (static_cast<int>(i) == selectedClipIndex);

        if (isHovered) hoveredClipIndex = static_cast<int>(i);

        ImU32 clipColor = channelColor(clip.channelIndex);
        ImU32 borderColor = isSelected ? IM_COL32(255, 255, 255, 255)
                         : isHovered ? IM_COL32(200, 200, 200, 200)
                         : IM_COL32(0, 0, 0, 100);

        // Clip body
        drawList->AddRectFilled(
            ImVec2(x + 1, y + 2),
            ImVec2(x + w - 1, y + trackHeight - 3),
            clipColor);

        // Border for selected/hovered
        if (isSelected || isHovered) {
            drawList->AddRect(
                ImVec2(x + 1, y + 2),
                ImVec2(x + w - 1, y + trackHeight - 3),
                borderColor, 0.0f, 0, 2.0f);
        }

        if (clip.type == ClipType::Audio) {
            /*
             * An audio clip is drawn as its waveform, not as a coloured bar.
             *
             * A bar tells you a clip is there; the waveform tells you where
             * the hits are, which is the whole reason you line a recording up
             * against a grid. It is drawn one vertical line per screen pixel
             * from the min/max of the samples that fall under that pixel -
             * peak envelope, not decimation, because decimating a 48 kHz
             * sample down to a few hundred pixels aliases transients away and
             * a drum hit vanishes from a picture of a drum hit.
             */
            const Sample* sample = project.samplePool.getSample(clip.sampleId);
            const float bodyTop = y + 2.0f;
            const float bodyBot = y + trackHeight - 3.0f;
            const float midY = (bodyTop + bodyBot) * 0.5f;
            const float halfH = (bodyBot - bodyTop) * 0.5f - 1.0f;

            // Only the part of the clip actually on screen.
            const float clipLeft = std::max(x + 1.0f, canvasPos.x + headerWidth);
            const float clipRight = std::min(x + w - 1.0f, canvasPos.x + canvasSize.x);

            if (sample != nullptr && !sample->audioData.empty() && clipRight > clipLeft) {
                const float secondsPerBeat =
                    60.0f / ((project.bpm > 1.0f) ? project.bpm : 120.0f);
                const float regionEnd = (clip.trimEndSeconds > clip.trimStartSeconds)
                    ? clip.trimEndSeconds : sample->lengthSeconds;
                const float regionLength = std::max(1e-6f,
                                                    regionEnd - clip.trimStartSeconds);
                const auto frameCount = static_cast<float>(sample->audioData.size());

                const ImU32 waveColor = IM_COL32(15, 15, 20, 220);
                for (float px = clipLeft; px < clipRight; px += 1.0f) {
                    const float beatAt = (px - x) / beatWidth;
                    float intoSample = beatAt * secondsPerBeat + clip.trimStartSeconds;
                    if (clip.loopClip) {
                        intoSample = clip.trimStartSeconds +
                            std::fmod(std::max(0.0f, intoSample - clip.trimStartSeconds),
                                      regionLength);
                    } else if (intoSample >= regionEnd) {
                        break;              // played out; nothing left to draw
                    }

                    const float nextBeat = (px + 1.0f - x) / beatWidth;
                    const float span = std::max(1.0f,
                        (nextBeat - beatAt) * secondsPerBeat *
                        static_cast<float>(sample->sampleRate));

                    const auto from = static_cast<size_t>(
                        std::max(0.0f, intoSample * static_cast<float>(sample->sampleRate)));
                    const auto to = static_cast<size_t>(
                        std::min(frameCount, static_cast<float>(from) + span));
                    if (from >= sample->audioData.size() || to <= from) continue;

                    float lo = 0.0f, hi = 0.0f;
                    for (size_t f = from; f < to; ++f) {
                        const float v = sample->audioData[f];
                        if (v < lo) lo = v;
                        if (v > hi) hi = v;
                    }
                    drawList->AddLine(ImVec2(px, midY - hi * halfH * clip.gain),
                                      ImVec2(px, midY - lo * halfH * clip.gain),
                                      waveColor);
                }
            }

            // A clip whose sample went missing is drawn as a hatched, named
            // placeholder rather than an empty box, so the user can see the
            // edit survived and what to relink.
            if (sample == nullptr) {
                drawList->AddRect(ImVec2(x + 1, y + 2),
                                  ImVec2(x + w - 1, y + trackHeight - 3),
                                  IM_COL32(220, 90, 90, 220), 0.0f, 0, 1.0f);
                drawList->AddText(ImVec2(x + 4, y + 8),
                                  IM_COL32(255, 200, 200, 255), "missing audio");
            } else {
                char audioLabel[96];
                snprintf(audioLabel, sizeof(audioLabel), "%s", sample->name.c_str());
                drawList->AddText(ImVec2(x + 4, y + 1),
                                  IM_COL32(0, 0, 0, 180), audioLabel);
            }

            // Fade handles, drawn as the triangles they describe.
            if (clip.fadeInBeats > 0.0f) {
                const float fx = x + clip.fadeInBeats * beatWidth;
                drawList->AddTriangleFilled(ImVec2(x + 1, bodyBot),
                                            ImVec2(std::min(fx, x + w - 1), bodyTop),
                                            ImVec2(x + 1, bodyTop),
                                            IM_COL32(0, 0, 0, 110));
            }
            if (clip.fadeOutBeats > 0.0f) {
                const float fx = x + w - clip.fadeOutBeats * beatWidth;
                drawList->AddTriangleFilled(ImVec2(x + w - 1, bodyBot),
                                            ImVec2(std::max(fx, x + 1), bodyTop),
                                            ImVec2(x + w - 1, bodyTop),
                                            IM_COL32(0, 0, 0, 110));
            }
            continue;
        }

        // Pattern name, with the transpose beside it when there is one -
        // two placements of one pattern that sound different must look
        // different, or the arrangement reads as a copy-paste error.
        if (clip.patternIndex >= 0 && clip.patternIndex < static_cast<int>(project.patterns.size())) {
            char clipLabel[80];
            if (clip.transpose != 0) {
                snprintf(clipLabel, sizeof(clipLabel), "%s %+d",
                         project.patterns[clip.patternIndex].name.c_str(),
                         clip.transpose);
            } else {
                snprintf(clipLabel, sizeof(clipLabel), "%s",
                         project.patterns[clip.patternIndex].name.c_str());
            }
            drawList->AddText(ImVec2(x + 4, y + 8),
                IM_COL32(0, 0, 0, 255), clipLabel);
        }
    }

    // Playhead
    float playheadX = canvasPos.x + headerWidth + seq.getCurrentBeat() * beatWidth - ui.scrollX;
    if (playheadX >= canvasPos.x + headerWidth && playheadX <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(
            ImVec2(playheadX, canvasPos.y),
            ImVec2(playheadX, tracksTop + laneCount * trackHeight),
            IM_COL32(255, 80, 80, 255), 2.0f);

        // Playhead triangle
        drawList->AddTriangleFilled(
            ImVec2(playheadX - 6, canvasPos.y),
            ImVec2(playheadX + 6, canvasPos.y),
            ImVec2(playheadX, canvasPos.y + 10),
            IM_COL32(255, 80, 80, 255));
    }

    // Song end marker
    float songEndX = canvasPos.x + headerWidth + project.songLength * beatWidth - ui.scrollX;
    if (songEndX >= canvasPos.x + headerWidth && songEndX <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(
            ImVec2(songEndX, tracksTop),
            ImVec2(songEndX, tracksTop + laneCount * trackHeight),
            IM_COL32(150, 80, 80, 200), 2.0f);
        drawList->AddText(ImVec2(songEndX + 4, tracksTop + 4),
            IM_COL32(150, 80, 80, 255), "END");
    }

    // ========================================================================
    // Handle mouse input
    // ========================================================================
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##arrangement", canvasSize);

    /*
     * Dropping out of the browser onto the timeline.
     *
     * The clip lands on the lane and the beat it was dropped on, snapped to
     * the grid, rather than at the start of the song - dropping something
     * precisely and watching it jump to zero is the behaviour that makes
     * drag-and-drop feel broken.
     *
     * Attached to the canvas button so the drop target covers exactly what
     * the eye reads as the timeline.
     */
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* dropped =
                ImGui::AcceptDragDropPayload(BROWSER_PAYLOAD_TYPE)) {
            if (dropped->DataSize == static_cast<int>(sizeof(BrowserPayload))) {
                BrowserPayload payload;
                std::memcpy(&payload, dropped->Data, sizeof(payload));

                const ImVec2 dropPos = ImGui::GetMousePos();
                const float dropX =
                    dropPos.x - canvasPos.x - headerWidth + ui.scrollX;
                float dropBeat = std::max(0.0f, dropX / std::max(1.0f, beatWidth));
                dropBeat = snapBeat(dropBeat, effectiveSnap(ui), project);

                int dropChannel =
                    static_cast<int>((dropPos.y - tracksTop) / trackHeight);
                dropChannel = std::clamp(dropChannel, 0,
                                         std::max(0, laneCount - 1));

                if (applyBrowserDropToTimeline(project, dropBeat, dropChannel,
                                               payload)) {
                    ui.selectedChannel = dropChannel;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemHovered() || isDraggingClip) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float relX = mousePos.x - canvasPos.x - headerWidth + ui.scrollX;
        int hoveredChannel = static_cast<int>((mousePos.y - tracksTop) / trackHeight);
        hoveredChannel = std::clamp(hoveredChannel, 0, 7);

        // Left click to select channel or clip
        if (ImGui::IsMouseClicked(0)) {
            if (mousePos.x < canvasPos.x + headerWidth) {
                // Click on header - select channel
                if (hoveredChannel >= 0 && hoveredChannel < 8) {
                    ui.selectedChannel = hoveredChannel;
                }
            } else if (hoveredClipIndex >= 0) {
                // Click on clip - select and start drag
                selectedClipIndex = hoveredClipIndex;
                isDraggingClip = true;
                draggingClipIndex = hoveredClipIndex;
                dragStartBeat = project.arrangement[hoveredClipIndex].startBeat;
                dragStartChannel = project.arrangement[hoveredClipIndex].channelIndex;
            } else {
                // Click on empty space - deselect
                selectedClipIndex = -1;
            }
        }

        // Drag clip
        if (isDraggingClip && draggingClipIndex >= 0 && draggingClipIndex < static_cast<int>(project.arrangement.size())) {
            if (ImGui::IsMouseDown(0)) {
                Clip& clip = project.arrangement[draggingClipIndex];
                float newBeat = std::max(0.0f, std::floor(relX / beatWidth));
                clip.startBeat = newBeat;
                clip.channelIndex = hoveredChannel;
            } else {
                isDraggingClip = false;
                draggingClipIndex = -1;
            }
        }

        // Double-click to add clip
        if (ImGui::IsMouseDoubleClicked(0) && relX >= 0 && hoveredChannel >= 0 && hoveredChannel < 8 && hoveredClipIndex < 0) {
            Clip newClip;
            newClip.channelIndex = hoveredChannel;
            newClip.patternIndex = ui.selectedPattern;
            newClip.startBeat = std::floor(relX / beatWidth);
            newClip.lengthBeats = (ui.selectedPattern >= 0 &&
                ui.selectedPattern < static_cast<int>(project.patterns.size()))
                ? static_cast<float>(project.patterns[ui.selectedPattern].length)
                : 16.0f;
            project.arrangement.push_back(newClip);
            selectedClipIndex = static_cast<int>(project.arrangement.size()) - 1;
        }

        // Right-click to delete clip
        if (ImGui::IsMouseClicked(1) && hoveredClipIndex >= 0) {
            project.arrangement.erase(project.arrangement.begin() + hoveredClipIndex);
            if (selectedClipIndex == hoveredClipIndex) selectedClipIndex = -1;
            else if (selectedClipIndex > hoveredClipIndex) selectedClipIndex--;
        }

        // Right-click on empty space - context menu
        if (ImGui::IsMouseClicked(1) && hoveredClipIndex < 0 && relX >= 0) {
            ImGui::OpenPopup("ArrangementContextMenu");
        }
    }

    // Context menu
    if (ImGui::BeginPopup("ArrangementContextMenu")) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float relX = mousePos.x - canvasPos.x - headerWidth + ui.scrollX;
        int targetChannel = static_cast<int>((mousePos.y - tracksTop) / trackHeight);
        targetChannel = std::clamp(targetChannel, 0, 7);
        float targetBeat = std::floor(relX / beatWidth);

        ImGui::Text("Add Pattern at beat %.0f, Ch %d", targetBeat, targetChannel + 1);
        ImGui::Separator();

        for (size_t i = 0; i < project.patterns.size(); ++i) {
            if (ImGui::MenuItem(project.patterns[i].name.c_str())) {
                Clip newClip;
                newClip.channelIndex = targetChannel;
                newClip.patternIndex = static_cast<int>(i);
                newClip.startBeat = targetBeat;
                newClip.lengthBeats = static_cast<float>(project.patterns[i].length);
                project.arrangement.push_back(newClip);
            }
        }
        ImGui::EndPopup();
    }

    // Scroll
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (ImGui::GetIO().KeyCtrl) {
            ui.zoomX = std::clamp(ui.zoomX + wheel * 0.1f, 0.25f, 4.0f);
        } else {
            ui.scrollX = std::max(0.0f, ui.scrollX - wheel * 50.0f);
        }
    }

    // Delete key to remove selected clip
    if (ImGui::IsWindowFocused() && selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(project.arrangement.size())) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            project.arrangement.erase(project.arrangement.begin() + selectedClipIndex);
            selectedClipIndex = -1;
        }
    }

    ImGui::End();
}

// ============================================================================
// Mixer
// ============================================================================
inline void DrawMixer(Project& project, UIState& ui, Sequencer& seq) {
    // Set initial window position on first use (bottom center)
    ImGui::SetNextWindowPos(ImVec2(220, 645), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 260), ImGuiCond_FirstUseEver);
    ImGui::Begin("Mixer", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

    // Meter state has to persist between frames for the peak hold and the
    // release slope, so it lives here rather than in UIState - nothing else
    // needs it, and it is purely presentational.
    static widgets::MeterState channelMeters[Project::MAX_CHANNELS];

    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool configChanged = false;

    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        auto& channel = project.channels[ch];
        const bool isSelected = (ch == ui.selectedChannel);

        ImGui::PushID(ch);
        ImGui::BeginGroup();

        const ImVec2 stripOrigin = ImGui::GetCursorScreenPos();
        const float stripWidth = 86.0f;

        // The selected strip gets a tinted panel behind it, so which channel
        // the editors are pointed at is obvious without reading anything.
        if (isSelected) {
            draw->AddRectFilled(
                ImVec2(stripOrigin.x - 4.0f, stripOrigin.y - 4.0f),
                ImVec2(stripOrigin.x + stripWidth, stripOrigin.y + 218.0f),
                widgets::withAlpha(widgets::accentColor(), 0.13f), 6.0f);
            draw->AddRect(
                ImVec2(stripOrigin.x - 4.0f, stripOrigin.y - 4.0f),
                ImVec2(stripOrigin.x + stripWidth, stripOrigin.y + 218.0f),
                widgets::withAlpha(widgets::accentColor(), 0.55f), 6.0f);
        }

        // Channel name, in that channel's identity colour
        const ImVec4 labelColor(
            ((channelColor(ch) >> 0) & 0xFF) / 255.0f,
            ((channelColor(ch) >> 8) & 0xFF) / 255.0f,
            ((channelColor(ch) >> 16) & 0xFF) / 255.0f,
            1.0f);
        ImGui::TextColored(labelColor, "%.10s", channel.name.c_str());

        // An instrument, a preset or a sample dropped on the strip's name
        // lands on that channel. The name is the part of a strip that reads
        // as "this channel", which is why it is the target rather than the
        // fader.
        if (BrowserChannelDropTarget(project, seq, ch)) configChanged = true;

        // Fader and meter side by side, the way a console lays them out
        ImGui::BeginGroup();
        if (ImGui::VSliderFloat("##vol", ImVec2(26, 132), &channel.volume, 0.0f, 1.0f, "")) {
            configChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Volume: %.0f%%", channel.volume * 100.0f);
        }
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, 5.0f);
        widgets::LevelMeter("##meter", seq.getChannelLevel(ch), channelMeters[ch],
                            ImVec2(11.0f, 132.0f));

        // Pan as a real knob rather than a slider that looks like volume
        if (widgets::Knob("Pan", &channel.pan, -1.0f, 1.0f, 0.0f, 17.0f, "%.2f")) {
            configChanged = true;
        }

        // Mute and solo, unmistakably on or off
        if (widgets::ToggleSwitch("##mute", &channel.muted, 30.0f)) configChanged = true;
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(channel.muted ? ImVec4(1.0f, 0.45f, 0.40f, 1.0f)
                                         : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled],
                           "M");
        ImGui::SameLine(0.0f, 6.0f);
        if (widgets::ToggleSwitch("##solo", &channel.solo, 30.0f)) configChanged = true;
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(channel.solo ? ImVec4(1.0f, 0.85f, 0.35f, 1.0f)
                                        : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled],
                           "S");

        if (isSelected) {
            ImGui::TextDisabled("editing");
        } else if (ImGui::Button("Select", ImVec2(72, 0))) {
            ui.selectedChannel = ch;
        }

        ImGui::EndGroup();
        ImGui::PopID();

        if (ch < Project::Project::MAX_CHANNELS - 1) ImGui::SameLine(0.0f, 12.0f);
    }

    // Volume, pan, mute and solo are read straight from the Project by the
    // mixer, but everything else routes through the synth - keep the sync
    // honest rather than relying on which happens to be which.
    if (configChanged) {
        seq.updateChannelConfigs();
    }

    ImGui::End();
}

// ============================================================================
// Channel Editor (Oscillator & Effects)
// ============================================================================
inline void DrawChannelEditor(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::SetNextWindowPos(ImVec2(1130, 385), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 250), ImGuiCond_FirstUseEver);
    ImGui::Begin("Channel Editor");

    if (ui.selectedChannel < 0 || ui.selectedChannel >= 8) {
        ImGui::Text("No channel selected");
        ImGui::End();
        return;
    }

    auto& channel = project.channels[ui.selectedChannel];
    auto& osc = channel.oscillator;

    // Half the channel's identity colour, half the theme's text. Enough to
    // tie this panel to its mixer strip without printing a red label in the
    // middle of a monochrome theme.
    {
        const ImU32 identity = channelColor(ui.selectedChannel);
        const ImVec4 themeText = ImGui::GetStyle().Colors[ImGuiCol_Text];
        const ImVec4 blended(
            (((identity >> 0) & 0xFF) / 255.0f) * 0.45f + themeText.x * 0.55f,
            (((identity >> 8) & 0xFF) / 255.0f) * 0.45f + themeText.y * 0.55f,
            (((identity >> 16) & 0xFF) / 255.0f) * 0.45f + themeText.z * 0.55f,
            1.0f);
        ImGui::TextColored(blended, "Channel: %s", channel.name.c_str());
    }

    ImGui::Separator();

    // Oscillator settings
    if (ImGui::CollapsingHeader("Oscillator", ImGuiTreeNodeFlags_DefaultOpen)) {
        /*
         * The basic engines, mapped by VALUE rather than by list position.
         *
         * This used to be six names indexed straight into OscillatorType.
         * The enum's sixth entry is Supersaw, not Custom - so choosing
         * "Custom" set the channel to Supersaw, and the wavetable controls
         * never appeared no matter what you picked. Any type not in this
         * short list (a drum, a genre instrument, one chosen from the Sound
         * Palette) shows its own name and is left alone rather than being
         * silently rewritten to whatever sits at index zero.
         */
        struct OscChoice { const char* label; OscillatorType type; };
        static const OscChoice OSC_CHOICES[] = {
            {"Pulse",     OscillatorType::Pulse},
            {"Triangle",  OscillatorType::Triangle},
            {"Sawtooth",  OscillatorType::Sawtooth},
            {"Sine",      OscillatorType::Sine},
            {"Noise",     OscillatorType::Noise},
            {"Supersaw",  OscillatorType::Supersaw},
            {"Wavetable", OscillatorType::Custom},
            {"FM",        OscillatorType::FMSynth},
            {"Sampler",   OscillatorType::Sampler},
            {"Granular",  OscillatorType::Granular},
            {"Drum Model", OscillatorType::DrumModel},
        };

        const std::string currentName = oscillatorTypeToString(osc.type);
        if (ImGui::BeginCombo("Type##osc", currentName.c_str())) {
            for (const OscChoice& choice : OSC_CHOICES) {
                const bool selected = (osc.type == choice.type);
                if (ImGui::Selectable(choice.label, selected)) {
                    osc.type = choice.type;
                    seq.updateChannelConfigs();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "The basic engines. The Sound Palette has the full list of\n"
                "instruments, drums and recreations; picking one there sets\n"
                "this to a type that is not in this short menu, and it stays\n"
                "set rather than being reset to Pulse.");
        }

        if (osc.type == OscillatorType::Pulse) {
            if (ImGui::SliderFloat("Pulse Width", &osc.pulseWidth, 0.05f, 0.95f, "%.0f%%")) {
                seq.updateChannelConfigs();
            }

            // Preset buttons
            if (ImGui::Button("12.5%")) { osc.pulseWidth = 0.125f; seq.updateChannelConfigs(); }
            ImGui::SameLine();
            if (ImGui::Button("25%")) { osc.pulseWidth = 0.25f; seq.updateChannelConfigs(); }
            ImGui::SameLine();
            if (ImGui::Button("50%")) { osc.pulseWidth = 0.50f; seq.updateChannelConfigs(); }
            ImGui::SameLine();
            if (ImGui::Button("75%")) { osc.pulseWidth = 0.75f; seq.updateChannelConfigs(); }
        }

        if (osc.type == OscillatorType::Triangle) {
            if (ImGui::SliderFloat("Triangle Slope", &osc.triangleSlope, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
        }

        if (osc.type == OscillatorType::Noise) {
            if (ImGui::Checkbox("Short Mode (metallic)", &osc.noiseShortMode)) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The 2A03's periodic noise mode: the shift register\n"
                                  "repeats every 93 steps, so it reads as a metallic\n"
                                  "tone rather than as hiss.");
            }

            // Real hardware offers sixteen fixed rates rather than a sweep,
            // and that stepping is most of what makes NES noise recognisable.
            const char* periodNames[] = {
                "Track note",
                "0 - highest", "1", "2", "3", "4", "5", "6", "7",
                "8", "9", "10", "11", "12", "13", "14", "15 - lowest"
            };
            int periodIndex = osc.noisePeriod + 1;   // -1 becomes 0
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::Combo("Noise Period", &periodIndex, periodNames,
                             IM_ARRAYSIZE(periodNames))) {
                osc.noisePeriod = periodIndex - 1;
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("One of the sixteen noise rates the NES actually had.\n"
                                  "Track note follows the keyboard instead, which no\n"
                                  "hardware did but is useful for tuned percussion.");
            }
        }

        ImGui::SliderFloat("Detune (cents)", &osc.detune, -100.0f, 100.0f);
    }

        // ---- Wavetable ------------------------------------------------------
        //
        // Custom used to be a triangle wearing a different name: the whole
        // wavetable editor wrote into banks that nothing played. These are
        // the controls that connect the two.
        if (osc.type == OscillatorType::Custom &&
            ImGui::CollapsingHeader("Wavetable", ImGuiTreeNodeFlags_DefaultOpen)) {
            const int bankCount = std::max(1, std::min(
                static_cast<int>(project.wavetableBanks.size()),
                WavetableLibrary::MAX_BANKS));

            ImGui::SetNextItemWidth(160);
            const char* bankName =
                (osc.wavetableBank >= 0 &&
                 osc.wavetableBank < static_cast<int>(project.wavetableBanks.size()))
                    ? project.wavetableBanks[static_cast<size_t>(osc.wavetableBank)].name.c_str()
                    : "Default";
            if (ImGui::BeginCombo("Bank", bankName)) {
                for (int i = 0; i < bankCount; ++i) {
                    const bool selected = (i == osc.wavetableBank);
                    const char* name =
                        (i < static_cast<int>(project.wavetableBanks.size()))
                            ? project.wavetableBanks[static_cast<size_t>(i)].name.c_str()
                            : "Default";
                    if (ImGui::Selectable(name, selected)) {
                        osc.wavetableBank = i;
                        seq.updateChannelConfigs();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Which bank of drawn waveforms this channel plays.\n"
                    "Draw them in the Wavetable Editor.");
            }

            if (ImGui::SliderFloat("Morph", &osc.wavetableMorph, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Where between the bank's waveforms this sits.\n"
                    "0 is the first table, 1 the last, and everything\n"
                    "between is a blend of the two nearest.");
            }

            if (ImGui::SliderFloat("Morph Sweep", &osc.wavetableMorphSweep,
                                   -1.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How far the morph travels over the life of each note.\n"
                    "A table that does not move is a sampled waveform; one\n"
                    "that does is the reason to have a bank at all.");
            }

            if (osc.wavetableMorphSweep != 0.0f) {
                if (ImGui::SliderFloat("Sweep Time", &osc.wavetableSweepTime,
                                       0.01f, 5.0f, "%.2f s")) {
                    seq.updateChannelConfigs();
                }
            }
        }

        // ---- Modulation -----------------------------------------------------
        //
        // Any source to any destination. This is what makes the engines
        // above worth having: a wavetable whose morph never moves is a
        // sampled waveform, and an FM patch whose index is fixed is one
        // timbre.
        //
        // Forced open for capture, so the smoke case actually renders the
        // route rows rather than one collapsed header.
        if (g_ExpandModulation) {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        if (ImGui::CollapsingHeader("Modulation")) {
            ModMatrix& m = osc.modMatrix;

            /*
             * The matrix is the hardest of these to start with: an empty one
             * does nothing, and the first useful route is not obvious. These
             * four answer "what is this for" faster than any amount of
             * tooltip.
             */
            ImGui::TextDisabled("Start from:");
            for (int i = 0; i < MOD_PRESET_COUNT; ++i) {
                if (i > 0 && i % 2 != 0) ImGui::SameLine();
                if (ImGui::SmallButton(MOD_PRESETS[i].name)) {
                    g_UndoHistory.saveState(project, "Modulation Preset");
                    MOD_PRESETS[i].apply(m);
                    seq.updateChannelConfigs();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", MOD_PRESETS[i].description);
                }
            }
            ImGui::Separator();

            ImGui::SetNextItemWidth(110);
            if (ImGui::DragInt("Polyphony", &m.polyphonyLimit, 0.1f, 0, 32,
                               m.polyphonyLimit == 0 ? "Max" : "%d voices")) {
                m.polyphonyLimit = std::clamp(m.polyphonyLimit, 0, 32);
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How many notes this channel may sound at once.\n"
                    "Not only about cost: a mono bass that steals its own\n"
                    "voice is a different instrument from one that stacks,\n"
                    "and a granular pad at 32 voices is a wall, not a cloud.");
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            if (ImGui::DragFloat("Bend", &m.pitchBendSemitones, 0.1f, 0.0f,
                                 24.0f, "%.0f st")) {
                seq.updateChannelConfigs();
            }

            // ---- The LFOs ----------------------------------------------------
            for (int i = 0; i < ModMatrix::LFO_COUNT; ++i) {
                ImGui::PushID(900 + i);
                LFOConfig& lfo = m.lfos[static_cast<size_t>(i)];

                char label[16];
                snprintf(label, sizeof(label), "LFO %d", i + 1);
                if (ImGui::TreeNode(label)) {
                    ImGui::SetNextItemWidth(150);
                    int shape = static_cast<int>(lfo.shape);
                    if (ImGui::BeginCombo("Shape",
                            lfoShapeName(static_cast<LFOShape>(shape)))) {
                        for (int sIndex = 0;
                             sIndex < static_cast<int>(LFOShape::Count); ++sIndex) {
                            const bool selected = (sIndex == shape);
                            if (ImGui::Selectable(
                                    lfoShapeName(static_cast<LFOShape>(sIndex)),
                                    selected)) {
                                lfo.shape = static_cast<LFOShape>(sIndex);
                                seq.updateChannelConfigs();
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::SliderFloat("Rate", &lfo.rateHz, 0.01f, 40.0f,
                                           "%.2f Hz")) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::SliderFloat("Delay", &lfo.delaySeconds, 0.0f,
                                           4.0f, "%.2f s")) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::SliderFloat("Fade in", &lfo.fadeSeconds, 0.0f,
                                           4.0f, "%.2f s")) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Vibrato that arrives the instant a note does\n"
                            "sounds mechanical. Every wind and string player\n"
                            "brings it in after the note has spoken.");
                    }
                    if (ImGui::Checkbox("Retrigger", &lfo.retrigger)) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Restart the phase on every note. Off, the LFO\n"
                            "free-runs and notes played together sit at\n"
                            "different points in the cycle - which is what a\n"
                            "pad wants and a rhythmic sync does not.");
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            // ---- The second envelope ----------------------------------------
            if (ImGui::TreeNode("Envelope 2")) {
                ImGui::TextDisabled(
                    "A second envelope, free to be any shape - the amplitude "
                    "one has to fit the note.");
                bool changed = false;
                changed |= ImGui::SliderFloat("Attack##e2", &m.env2Attack, 0.0f, 4.0f, "%.3f s");
                changed |= ImGui::SliderFloat("Decay##e2", &m.env2Decay, 0.0f, 8.0f, "%.3f s");
                changed |= ImGui::SliderFloat("Sustain##e2", &m.env2Sustain, 0.0f, 1.0f);
                changed |= ImGui::SliderFloat("Release##e2", &m.env2Release, 0.0f, 8.0f, "%.3f s");
                if (changed) seq.updateChannelConfigs();
                ImGui::TreePop();
            }

            // ---- The routes ---------------------------------------------------
            ImGui::Separator();
            ImGui::Text("%d route%s", m.routeCount, m.routeCount == 1 ? "" : "s");
            ImGui::SameLine();
            if (ImGui::SmallButton("Add route") &&
                m.routeCount < ModMatrix::MAX_ROUTES) {
                g_UndoHistory.saveState(project, "Add Modulation Route");
                // Something audible by default, so a new route is not a row
                // of dashes that appears to do nothing.
                m.addRoute(ModRoute{ModSource::LFO1, ModDestination::Pitch,
                                    0.1f, true});
                seq.updateChannelConfigs();
            }

            for (int r = 0; r < m.routeCount; ++r) {
                ImGui::PushID(950 + r);
                ModRoute& route = m.routes[static_cast<size_t>(r)];

                if (ImGui::Checkbox("##on", &route.enabled)) {
                    seq.updateChannelConfigs();
                }
                ImGui::SameLine();

                ImGui::SetNextItemWidth(120);
                if (ImGui::BeginCombo("##src", modSourceName(route.source))) {
                    for (int sIndex = 0;
                         sIndex < static_cast<int>(ModSource::Count); ++sIndex) {
                        const auto candidate = static_cast<ModSource>(sIndex);
                        const bool selected = (route.source == candidate);
                        if (ImGui::Selectable(modSourceName(candidate), selected)) {
                            route.source = candidate;
                            seq.updateChannelConfigs();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                ImGui::TextUnformatted("->");
                ImGui::SameLine();

                ImGui::SetNextItemWidth(140);
                if (ImGui::BeginCombo("##dst",
                        modDestinationName(route.destination))) {
                    for (int dIndex = 0;
                         dIndex < static_cast<int>(ModDestination::Count); ++dIndex) {
                        const auto candidate = static_cast<ModDestination>(dIndex);
                        const bool selected = (route.destination == candidate);
                        if (ImGui::Selectable(modDestinationName(candidate),
                                              selected)) {
                            route.destination = candidate;
                            seq.updateChannelConfigs();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(90);
                if (ImGui::DragFloat("##amt", &route.amount, 0.005f, -1.0f,
                                     1.0f, "%.2f")) {
                    seq.updateChannelConfigs();
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    g_UndoHistory.saveState(project, "Remove Modulation Route");
                    for (int k = r; k + 1 < m.routeCount; ++k) {
                        m.routes[static_cast<size_t>(k)] =
                            m.routes[static_cast<size_t>(k) + 1];
                    }
                    --m.routeCount;
                    seq.updateChannelConfigs();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            if (m.routeCount == 0) {
                ImGui::TextDisabled(
                    "Nothing routed. An LFO on Wavetable Morph or FM\n"
                    "Brightness is the quickest way to hear what this does.");
            }
        }

        // ---- Granular -------------------------------------------------------
        //
        // Cuts a recording into very short windowed fragments and plays a
        // stream of them. Move the read position slowly and you get a
        // time-stretch that does not change pitch; hold it still and you get
        // a drone made from one moment of the recording.
        if (osc.type == OscillatorType::Granular &&
            ImGui::CollapsingHeader("Granular", ImGuiTreeNodeFlags_DefaultOpen)) {
            GranularConfig& g = osc.granular;

            const Sample* source = project.samplePool.getSample(g.sampleId);
            ImGui::Text("Source: %s",
                        source != nullptr ? source->name.c_str() : "none");
            ImGui::SameLine();
            if (ImGui::SmallButton("Load##gran")) {
                const std::string path = openFileDialog(
                    "Audio Files (*.wav;*.mp3;*.ogg;*.flac)\0"
                    "*.wav;*.mp3;*.ogg;*.flac\0All Files (*.*)\0*.*\0", "wav");
                if (!path.empty()) {
                    const int id = project.samplePool.loadSample(path);
                    if (id >= 0) {
                        g_UndoHistory.saveState(project, "Granular Source");
                        g.sampleId = id;
                        seq.updateChannelConfigs();
                    }
                }
            }

            ImGui::TextDisabled("Start from:");
            for (int i = 0; i < GRANULAR_PRESET_COUNT; ++i) {
                if (i > 0) ImGui::SameLine();
                if (ImGui::SmallButton(GRANULAR_PRESETS[i].name)) {
                    g_UndoHistory.saveState(project, "Granular Preset");
                    // These keep the loaded sample deliberately: replacing
                    // the whole config would throw away the audio the user
                    // just picked, which is not what "try this setting"
                    // should do.
                    GRANULAR_PRESETS[i].apply(g);
                    seq.updateChannelConfigs();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", GRANULAR_PRESETS[i].description);
                }
            }
            ImGui::Separator();

            if (ImGui::SliderFloat("Position", &g.position, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Where in the recording the grains come from.");
            }

            if (ImGui::SliderFloat("Scan", &g.positionRate, -2.0f, 2.0f, "%.2fx")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How fast the read position travels on its own.\n"
                    "0 freezes it - a drone from one moment of the sound.\n"
                    "Negative plays backwards. Neither changes the pitch,\n"
                    "which is the thing granular is really for.");
            }

            if (ImGui::SliderFloat("Spray", &g.spray, 0.0f, 0.5f, "%.3f s")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How far grains scatter around the read position.\n"
                    "At zero the grains line up and you hear a tone at the\n"
                    "grain rate instead of a texture.");
            }

            if (ImGui::SliderFloat("Grain", &g.grainSeconds, 0.002f, 0.5f, "%.3f s")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::SliderFloat("Density", &g.grainsPerSecond, 1.0f, 200.0f,
                                   "%.0f /s")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Grains per second. Density times grain length is how\n"
                    "many sound at once; below one you hear gaps, and two to\n"
                    "four is smooth. The level is compensated, so this\n"
                    "changes texture rather than volume.");
            }

            if (ImGui::SliderFloat("Window", &g.windowShape, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "0 is a smooth bell - the most washed sound.\n"
                    "1 is flat-topped with short fades, which keeps\n"
                    "transients and sounds rhythmic rather than smeared.");
            }

            if (ImGui::SliderFloat("Pitch", &g.pitchSemitones, -24.0f, 24.0f,
                                   "%.1f st")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::SliderFloat("Pitch scatter", &g.pitchJitter, 0.0f, 12.0f,
                                   "%.1f st")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::SliderFloat("Reverse", &g.reverseChance, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The chance each grain plays backwards. Reversed grains\n"
                    "inside a forward stream are a granular signature.");
            }
            if (ImGui::Checkbox("Follow note##gran", &g.followNote)) {
                seq.updateChannelConfigs();
            }
        }

        // ---- Modelled drums --------------------------------------------------
        //
        // The 21 existing drum oscillators are good and they are fixed - you
        // can pick Kick808 or KickHard and that is the whole of your control
        // over a kick. This is the editable version, modelled the way the
        // analogue machines made them.
        if (osc.type == OscillatorType::DrumModel &&
            ImGui::CollapsingHeader("Drum Model", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrumModelConfig& d = osc.drumModel;

            ImGui::TextDisabled("Start from:");
            for (int i = 0; i < DRUM_PRESET_COUNT; ++i) {
                if (i > 0 && i % 3 != 0) ImGui::SameLine();
                if (ImGui::SmallButton(DRUM_PRESETS[i].name)) {
                    g_UndoHistory.saveState(project, "Drum Preset");
                    DRUM_PRESETS[i].apply(d);
                    seq.updateChannelConfigs();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", DRUM_PRESETS[i].description);
                }
            }
            ImGui::Separator();

            int voice = static_cast<int>(d.voice);
            const char* voices = "Kick\0Snare\0Hi-hat\0";
            ImGui::SetNextItemWidth(140);
            if (ImGui::Combo("Voice", &voice, voices)) {
                d.voice = static_cast<DrumVoiceType>(
                    std::clamp(voice, 0, static_cast<int>(DrumVoiceType::Count) - 1));
                seq.updateChannelConfigs();
            }

            if (ImGui::SliderFloat("Tune", &d.tuneHz, 20.0f, 800.0f, "%.0f Hz")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::SliderFloat("Decay", &d.decaySeconds, 0.01f, 3.0f, "%.3f s")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::SliderFloat("Snap", &d.snap, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The attack transient - the click on a kick, the stick\n"
                    "on a snare. It is what makes a drum cut through a mix\n"
                    "rather than merely being loud.");
            }
            if (ImGui::SliderFloat("Level##drum", &d.level, 0.0f, 2.0f)) {
                seq.updateChannelConfigs();
            }

            if (d.voice == DrumVoiceType::Kick) {
                if (ImGui::SliderFloat("Sweep", &d.pitchSweepSemitones,
                                       0.0f, 60.0f, "%.0f st")) {
                    seq.updateChannelConfigs();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "How far above the fundamental the pitch starts.\n"
                        "This sweep IS the thump; turn it off and you have\n"
                        "the static sine an 808 tail needs to work as a bass.");
                }
                if (ImGui::SliderFloat("Sweep time", &d.pitchSweepSeconds,
                                       0.001f, 0.5f, "%.3f s")) {
                    seq.updateChannelConfigs();
                }
            }

            if (d.voice == DrumVoiceType::Snare) {
                if (ImGui::SliderFloat("Shell / snares", &d.noiseMix, 0.0f, 1.0f)) {
                    seq.updateChannelConfigs();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "0 is all tuned shell, 1 is all rattle. The single\n"
                        "most useful control a snare has - it moves the\n"
                        "sound from a rimshot to a clap.");
                }
                if (ImGui::SliderFloat("Rattle tone", &d.noiseTone, 0.0f, 1.0f)) {
                    seq.updateChannelConfigs();
                }
            }

            if (d.voice == DrumVoiceType::HiHat) {
                if (ImGui::SliderFloat("Sizzle", &d.hatHighpass, 0.0f, 1.0f)) {
                    seq.updateChannelConfigs();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "How much low end is filtered out. The hat is six\n"
                        "square waves at the 808's inharmonic ratios, which\n"
                        "is why it sounds metallic rather than like filtered\n"
                        "noise - but it needs the low end taken off.");
                }
            }

            if (ImGui::SliderFloat("Vel -> Level##drum", &d.velocityToLevel,
                                   0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
        }

        // ---- Multisample ----------------------------------------------------
        //
        // One recording stretched across the whole keyboard sounds like a
        // tape played at the wrong speed, because the formants move with the
        // pitch. Several recordings, each used near where it was made, is
        // what every sampler since the Mirage has done.
        if (osc.type == OscillatorType::Sampler &&
            ImGui::CollapsingHeader("Sampler", ImGuiTreeNodeFlags_DefaultOpen)) {
            SamplerInstrument& sampler = osc.sampler;

            ImGui::Text("%d zone%s", sampler.zoneCount,
                        sampler.zoneCount == 1 ? "" : "s");
            ImGui::SameLine();
            if (ImGui::SmallButton("Add from file")) {
                const std::string path = openFileDialog(
                    "Audio Files (*.wav;*.mp3;*.ogg;*.flac)\0"
                    "*.wav;*.mp3;*.ogg;*.flac\0All Files (*.*)\0*.*\0", "wav");
                if (!path.empty()) {
                    const int id = project.samplePool.loadSample(path);
                    if (id >= 0) {
                        g_UndoHistory.saveState(project, "Add Sample Zone");
                        SampleZone zone;
                        zone.sampleId = id;
                        // A new zone covers the whole keyboard so it is
                        // audible immediately; narrowing it is the next
                        // thing you do, and an inaudible new zone reads as
                        // the button not working.
                        zone.lowKey = 0;
                        zone.highKey = 127;
                        zone.rootKey = 60;
                        sampler.addZone(zone);
                        seq.updateChannelConfigs();
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Load a recording and map it. Give each zone the keys and\n"
                    "the velocities it should cover; zones with the same\n"
                    "ranges are alternate takes and get cycled between, so a\n"
                    "fast repeated hit does not phase against itself.");
            }

            if (ImGui::SliderFloat("Layer Blend", &sampler.velocityCrossfade,
                                   0.0f, 0.5f, "%.2f")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How far either side of a velocity boundary two layers\n"
                    "blend. At zero the instrument changes character on one\n"
                    "note of a crescendo, which is very audible.");
            }
            if (ImGui::SliderFloat("Vel -> Level", &sampler.velocityToLevel,
                                   0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }

            for (int z = 0; z < sampler.zoneCount; ++z) {
                ImGui::PushID(800 + z);
                SampleZone& zone = sampler.zones[static_cast<size_t>(z)];

                const Sample* sample = project.samplePool.getSample(zone.sampleId);
                char label[96];
                snprintf(label, sizeof(label), "Zone %d - %s", z + 1,
                         sample != nullptr ? sample->name.c_str() : "no sample");

                if (ImGui::TreeNode(label)) {
                    if (ImGui::DragIntRange2("Keys", &zone.lowKey, &zone.highKey,
                                             0.5f, 0, 127)) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::DragInt("Root", &zone.rootKey, 0.5f, 0, 127)) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "The key this was recorded at. Everything else in\n"
                            "the zone is pitch-shifted away from it, so a\n"
                            "wrong root puts the whole zone out of tune.");
                    }
                    if (ImGui::DragFloatRange2("Velocity", &zone.lowVelocity,
                                               &zone.highVelocity, 0.005f,
                                               0.0f, 1.0f, "%.2f")) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::SliderFloat("Gain", &zone.gain, 0.0f, 2.0f)) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::SliderFloat("Pan", &zone.pan, -1.0f, 1.0f)) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::SliderFloat("Tune", &zone.tuneCents, -100.0f,
                                           100.0f, "%.0f c")) {
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::Checkbox("Loop", &zone.loop)) {
                        seq.updateChannelConfigs();
                    }
                    if (zone.loop) {
                        if (ImGui::DragFloat("Loop start", &zone.loopStartSeconds,
                                             0.001f, 0.0f, 60.0f, "%.3f s")) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::DragFloat("Loop end", &zone.loopEndSeconds,
                                             0.001f, 0.0f, 60.0f, "%.3f s")) {
                            seq.updateChannelConfigs();
                        }
                    }
                    if (ImGui::SmallButton("Remove")) {
                        g_UndoHistory.saveState(project, "Remove Sample Zone");
                        for (int k = z; k + 1 < sampler.zoneCount; ++k) {
                            sampler.zones[static_cast<size_t>(k)] =
                                sampler.zones[static_cast<size_t>(k) + 1];
                        }
                        --sampler.zoneCount;
                        seq.updateChannelConfigs();
                        ImGui::TreePop();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        // ---- Six-operator FM ------------------------------------------------
        //
        // The tracker faked FM with two fixed presets, SynthBell and
        // SynthwaveFM, which were hardcoded two-operator arrangements with
        // nothing editable. FM is more on-brand for a chip tracker than most
        // of a modern DAW's toolbox: the YM2612 in the Mega Drive is
        // six-operator FM, and the DX7 that defined the eighties is too.
        if (osc.type == OscillatorType::FMSynth &&
            ImGui::CollapsingHeader("FM", ImGuiTreeNodeFlags_DefaultOpen)) {
            /*
             * Starting points.
             *
             * Six operators of ratio, level, detune and four envelope stages
             * is sixty-odd numbers, and nobody discovers a bell by dragging
             * them one at a time. These four are what FM is known for, and
             * each is a different algorithm - four variations on one routing
             * would look tidier and teach nothing.
             */
            ImGui::TextDisabled("Start from:");
            for (int i = 0; i < FM_PRESET_COUNT; ++i) {
                if (i > 0) ImGui::SameLine();
                if (ImGui::SmallButton(FM_PRESETS[i].name)) {
                    g_UndoHistory.saveState(project, "FM Preset");
                    FM_PRESETS[i].apply(osc.fm);
                    seq.updateChannelConfigs();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", FM_PRESETS[i].description);
                }
            }

            /*
             * Or from somebody else's patch.
             *
             * There are decades of YM2612 voicings in the world as .tfi and
             * .vgi files, which every Mega Drive tracker reads and writes.
             * Opening one is a bass or a brass stab arriving in one click
             * rather than after forty sliders.
             */
            ImGui::SameLine();
            if (ImGui::SmallButton("Open .tfi / .vgi")) {
                const std::string path = openFileDialog(
                    "Mega Drive patch\0*.tfi;*.vgi\0All files\0*.*\0", "tfi");
                if (!path.empty()) {
                    FMPatch imported;
                    std::string patchName;
                    const ChipImportError result =
                        importFMPatchFile(path, imported, &patchName);

                    if (result == ChipImportError::Ok) {
                        g_UndoHistory.saveState(project, "Import FM Patch");
                        osc.fm = imported;
                        g_ChipImportMessage =
                            "Loaded " + patchName +
                            " - a translation, not the chip: key scaling, "
                            "SSG-EG and the LFO have nowhere to go here.";
                        seq.updateChannelConfigs();
                    } else {
                        // Said plainly. A refused import that looks like
                        // nothing happening is worse than an error.
                        g_ChipImportMessage =
                            std::string("Could not import: ") +
                            chipImportErrorText(result);
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Import a YM2612 patch from TFM Music Maker or VGM Music\n"
                    "Maker. Four operators, the algorithm and the envelopes\n"
                    "come across; key scaling, SSG-EG and the chip LFO have\n"
                    "no equivalent here and are dropped.");
            }

            if (!g_ChipImportMessage.empty()) {
                ImGui::TextWrapped("%s", g_ChipImportMessage.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("x##chipimport")) {
                    g_ChipImportMessage.clear();
                }
            }

            ImGui::Separator();

            ImGui::SetNextItemWidth(230);
            int preset = std::clamp(osc.fmAlgorithmPreset, 0,
                                    static_cast<int>(FMAlgorithmPreset::Count) - 1);
            if (ImGui::BeginCombo("Algorithm",
                    fmAlgorithmName(static_cast<FMAlgorithmPreset>(preset)))) {
                for (int i = 0; i < static_cast<int>(FMAlgorithmPreset::Count); ++i) {
                    const bool selected = (i == preset);
                    if (ImGui::Selectable(
                            fmAlgorithmName(static_cast<FMAlgorithmPreset>(i)),
                            selected)) {
                        osc.fmAlgorithmPreset = i;
                        // Selecting a preset replaces the routing. The
                        // feedback is deliberately kept: it is a dial the
                        // user set, not part of the shape they picked.
                        const float feedback = osc.fm.algorithm.feedback;
                        osc.fm.algorithm =
                            fmAlgorithmFromPreset(static_cast<FMAlgorithmPreset>(i));
                        osc.fm.algorithm.feedback = feedback;
                        seq.updateChannelConfigs();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Which operators modulate which. This is the single\n"
                    "biggest decision in an FM patch - it decides whether\n"
                    "you get a bell, a brass section or a metallic bass.");
            }

            if (ImGui::SliderFloat("Brightness", &osc.fm.index, 0.0f, 12.0f, "%.2f")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The modulation index: how far the modulators push the\n"
                    "carriers' phase. This is what 'brighter' means in FM,\n"
                    "and unlike a filter the harmonics do not fade in - they\n"
                    "swap places, which is why FM sounds like nothing else.");
            }

            if (ImGui::SliderFloat("Feedback", &osc.fm.algorithm.feedback,
                                   0.0f, 1.0f, "%.2f")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The top operator modulating itself. Past about 0.7 it\n"
                    "turns into a sawtooth, which is the classic way to get\n"
                    "a bright bass out of FM.");
            }

            if (ImGui::TreeNode("Operators")) {
                ImGui::TextDisabled(
                    "Operator 6 is the top modulator, 1 is usually the "
                    "carrier.");
                for (int i = FM_OPERATORS - 1; i >= 0; --i) {
                    ImGui::PushID(700 + i);
                    FMOperator& op = osc.fm.operators[static_cast<size_t>(i)];

                    char label[24];
                    snprintf(label, sizeof(label), "Op %d", i + 1);
                    if (ImGui::TreeNode(label)) {
                        if (ImGui::Checkbox("Enabled", &op.enabled)) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::SliderFloat("Ratio", &op.ratio, 0.0f, 16.0f, "%.3f")) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "This operator's frequency as a multiple of\n"
                                "the note. Whole numbers give harmonic,\n"
                                "musical spectra; everything else gives the\n"
                                "inharmonic metallic timbres FM is known for.");
                        }
                        if (ImGui::SliderFloat("Level", &op.level, 0.0f, 1.0f)) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::SliderFloat("Detune", &op.detuneCents,
                                               -100.0f, 100.0f, "%.0f c")) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::SliderFloat("Attack", &op.attack, 0.0f, 2.0f, "%.3f s")) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::SliderFloat("Decay", &op.decay, 0.0f, 5.0f, "%.3f s")) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::SliderFloat("Sustain", &op.sustain, 0.0f, 1.0f)) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::SliderFloat("Release", &op.release, 0.0f, 5.0f, "%.3f s")) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::SliderFloat("Velocity", &op.velocitySensitivity,
                                               0.0f, 1.0f)) {
                            seq.updateChannelConfigs();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "How much playing harder opens this operator\n"
                                "up. On a MODULATOR this makes hard playing\n"
                                "brighter rather than merely louder, which is\n"
                                "what made FM keyboards feel expressive.");
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }


    // Envelope
    if (ImGui::CollapsingHeader("Envelope (ADSR)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Attack", &channel.envelope.attack, 0.001f, 2.0f, "%.3f s");
        ImGui::SliderFloat("Decay", &channel.envelope.decay, 0.001f, 2.0f, "%.3f s");
        // Display sustain as 0-100% (value is 0.0-1.0)
        float sustainPercent = channel.envelope.sustain * 100.0f;
        if (ImGui::SliderFloat("Sustain", &sustainPercent, 0.0f, 100.0f, "%.0f%%")) {
            channel.envelope.sustain = sustainPercent / 100.0f;
        }
        ImGui::SliderFloat("Release", &channel.envelope.release, 0.001f, 2.0f, "%.3f s");
    }

    // Filter Envelope (Per-voice)
    if (ImGui::CollapsingHeader("Filter Envelope")) {
        ImGui::Checkbox("Enable Filter Env", &channel.filterEnvEnabled);
        if (channel.filterEnvEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Amount##fenv", &channel.filterEnvAmount, -1.0f, 1.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Modulation amount (negative for inverted)");
            ImGui::SliderFloat("Attack##fenv", &channel.filterEnvAttack, 0.001f, 1.0f, "%.3f s");
            ImGui::SliderFloat("Decay##fenv", &channel.filterEnvDecay, 0.001f, 2.0f, "%.3f s");
            ImGui::Unindent();
        }
    }

    // EQ
    if (ImGui::CollapsingHeader("3-Band EQ")) {
        ImGui::Checkbox("Enable EQ", &channel.eqEnabled);
        if (channel.eqEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Low Gain", &channel.eqLow, 0.0f, 2.0f, "%.2f x");
            ImGui::SliderFloat("Mid Gain", &channel.eqMid, 0.0f, 2.0f, "%.2f x");
            ImGui::SliderFloat("High Gain", &channel.eqHigh, 0.0f, 2.0f, "%.2f x");
            
            // Optional frequency controls (advanced)
            if (ImGui::TreeNode("Frequencies")) {
                ImGui::SliderFloat("Low Freq", &channel.eqLowFreq, 50.0f, 500.0f, "%.0f Hz");
                ImGui::SliderFloat("Mid Freq", &channel.eqMidFreq, 200.0f, 2000.0f, "%.0f Hz");
                ImGui::SliderFloat("High Freq", &channel.eqHighFreq, 1000.0f, 10000.0f, "%.0f Hz");
                ImGui::TreePop();
            }
            ImGui::Unindent();
        }
    }

    // Compressor
    if (ImGui::CollapsingHeader("Compressor")) {
        ImGui::Checkbox("Enable Compressor", &channel.compressorEnabled);
        if (channel.compressorEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Threshold##comp", &channel.compThreshold, 0.0f, 1.0f);
            ImGui::SliderFloat("Ratio##comp", &channel.compRatio, 1.0f, 20.0f, "1:%.1f");
            ImGui::SliderFloat("Attack##comp", &channel.compAttack, 0.001f, 0.1f, "%.3f s");
            ImGui::SliderFloat("Release##comp", &channel.compRelease, 0.01f, 1.0f, "%.2f s");
            ImGui::SliderFloat("Makeup Gain", &channel.compGain, 1.0f, 4.0f, "%.2f x");
            ImGui::Unindent();
        }
    }
    
    // Formant Filter (Vocoder Effect)
    // ========================================================================
    // Pitch and time
    //
    // Three phase-vocoder effects. Each costs a window of latency - about
    // 23 ms at 44.1 kHz - which is unavoidable rather than sloppy: a bin's
    // true frequency cannot be known without seeing two frames of it. That
    // is why the header says so rather than leaving it to be discovered.
    // ========================================================================
    if (ImGui::CollapsingHeader("Pitch & Time")) {
        ImGui::TextDisabled("~23 ms latency each. Studio effects, not for "
                            "monitoring while you play.");

        if (ImGui::Checkbox("Pitch Shift", &channel.pitchShiftEnabled)) {
            seq.updateChannelConfigs();
        }
        if (channel.pitchShiftEnabled) {
            if (ImGui::SliderFloat("Shift##ps", &channel.pitchShiftSemitones,
                                   -24.0f, 24.0f, "%.2f st")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Moves the pitch and the formants together, which is why\n"
                    "a voice an octave up is a chipmunk. Add Formant Shift\n"
                    "the other way to keep it sounding like the same singer.");
            }
            if (ImGui::SliderFloat("Mix##ps", &channel.pitchShiftMix, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Blend against the dry signal. Around 0.5 with a shift\n"
                    "of +12 or -12 is the classic octave doubler.");
            }
        }

        ImGui::Separator();
        if (ImGui::Checkbox("Formant Shift", &channel.formantShiftEnabled)) {
            seq.updateChannelConfigs();
        }
        if (channel.formantShiftEnabled) {
            if (ImGui::SliderFloat("Shift##fs", &channel.formantShiftSemitones,
                                   -24.0f, 24.0f, "%.2f st")) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Moves the resonances WITHOUT moving the notes - it\n"
                    "changes the apparent size of the singer at constant\n"
                    "pitch. Up is smaller, down is larger.");
            }
            if (ImGui::SliderFloat("Mix##fs", &channel.formantShiftMix, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
        }

        ImGui::Separator();
        if (ImGui::Checkbox("Auto-Tune", &channel.autoTuneEnabled)) {
            seq.updateChannelConfigs();
        }
        if (channel.autoTuneEnabled) {
            if (ImGui::SliderFloat("Retune##at", &channel.autoTuneStrength,
                                   0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How fast it pulls to the note, and the whole character\n"
                    "of the effect. Slow leaves the performance intact and\n"
                    "only nudges the settled pitch. All the way up snaps\n"
                    "between scale degrees with no glide, which is the sound\n"
                    "the effect became famous for.");
            }

            static const char* NOTE_NAMES[12] = {
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            ImGui::SetNextItemWidth(80);
            int root = std::clamp(channel.autoTuneRoot, 0, 11);
            if (ImGui::BeginCombo("Key##at", NOTE_NAMES[root])) {
                for (int i = 0; i < 12; ++i) {
                    const bool selected = (i == root);
                    if (ImGui::Selectable(NOTE_NAMES[i], selected)) {
                        channel.autoTuneRoot = i;
                        seq.updateChannelConfigs();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // The common scales as buttons. A twelve-checkbox grid is the
            // honest representation and nobody wants to click it; these are
            // what people actually reach for.
            ImGui::SameLine();
            struct ScalePreset { const char* name; int mask; };
            static const ScalePreset SCALES[] = {
                {"Chromatic", 0b111111111111},
                {"Major",     0b101010110101},
                {"Minor",     0b010110101101},
                {"Penta",     0b001010010101},
            };
            for (const ScalePreset& scale : SCALES) {
                ImGui::SameLine();
                const bool active = (channel.autoTuneScaleMask == scale.mask);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }
                if (ImGui::SmallButton(scale.name)) {
                    channel.autoTuneScaleMask = scale.mask;
                    seq.updateChannelConfigs();
                }
                if (active) ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Which notes it is allowed to pull to. A tighter scale\n"
                    "corrects harder, because more sung pitches are wrong.");
            }

            if (ImGui::SliderFloat("Mix##at", &channel.autoTuneMix, 0.0f, 1.0f)) {
                seq.updateChannelConfigs();
            }
        }
    }

    if (ImGui::CollapsingHeader("Formant Filter (Vocoder)")) {
        ImGui::Checkbox("Enable Formant", &channel.formantEnabled);
        if (channel.formantEnabled) {
            ImGui::Indent();
            const char* vowels[] = {"A (ah)", "E (eh)", "I (ee)", "O (oh)", "U (oo)"};
            ImGui::Combo("Vowel", &channel.formantVowel, vowels, IM_ARRAYSIZE(vowels));
            ImGui::SliderFloat("Resonance##formant", &channel.formantResonance, 1.0f, 10.0f, "%.1f");
            ImGui::Unindent();
        }
    }

    // Effects
    if (ImGui::CollapsingHeader("Sends")) {
        ChannelConfig& sendChannel = project.channels[ui.selectedChannel];

        ImGui::TextDisabled("A copy of this channel, sent to a bus.");

        for (int slot = 0; slot < MAX_SENDS_PER_CHANNEL; ++slot) {
            SendConfig& send = sendChannel.sends[static_cast<size_t>(slot)];
            ImGui::PushID(1000 + slot);

            const char* label = (send.destination < 0)
                ? "off"
                : project.auxBuses[static_cast<size_t>(send.destination)].name.c_str();

            ImGui::SetNextItemWidth(110);
            if (ImGui::BeginCombo("##senddest", label)) {
                if (ImGui::Selectable("off", send.destination < 0)) {
                    send.destination = -1;
                }
                for (int bus = 0; bus < Project::MAX_AUX_BUSES; ++bus) {
                    if (ImGui::Selectable(
                            project.auxBuses[static_cast<size_t>(bus)].name.c_str(),
                            send.destination == bus)) {
                        send.destination = bus;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            ImGui::SliderFloat("##sendlevel", &send.level, 0.0f, 1.0f, "%.2f");

            ImGui::SameLine();
            bool pre = send.preFader;
            if (ImGui::Checkbox("pre", &pre)) send.preFader = pre;
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Take the send before this channel's volume,\n"
                                  "so pulling the channel down leaves the send\n"
                                  "standing - how a dry signal fades into its\n"
                                  "own reverb tail.");
            }

            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Sidechain source");
        ImGui::SetNextItemWidth(160);
        {
            const char* scLabel = (sendChannel.sidechainBus >= 0)
                ? project.auxBuses[static_cast<size_t>(sendChannel.sidechainBus)].name.c_str()
                : "channel tap (legacy)";
            if (ImGui::BeginCombo("##scbus", scLabel)) {
                if (ImGui::Selectable("channel tap (legacy)",
                                      sendChannel.sidechainBus < 0)) {
                    sendChannel.sidechainBus = -1;
                }
                for (int bus = 0; bus < Project::MAX_AUX_BUSES; ++bus) {
                    if (ImGui::Selectable(
                            project.auxBuses[static_cast<size_t>(bus)].name.c_str(),
                            sendChannel.sidechainBus == bus)) {
                        sendChannel.sidechainBus = bus;
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Duck this channel from a bus's signal.\n"
                              "The legacy option taps one channel directly,\n"
                              "which is how projects before v4 work.");
        }
    }

    if (CollapsingHeaderExpandable("Effect Rack", g_ExpandEffectRack)) {
        auto& rackFx = seq.getSynth(ui.selectedChannel).effects();

        ImGui::TextDisabled("Signal flows top to bottom. Drag to reorder.");

        // Reordering has to reach the project, not just the live chain, or
        // it is lost on save. The chain is what the audio thread reads; the
        // ChannelConfig is what gets written to the file.
        auto publishOrder = [&](ChannelConfig& config) {
            const auto& current = rackFx.rack();
            config.fxSlotCount = current.count;
            for (int i = 0; i < current.count && i < MAX_FX_SLOTS; ++i) {
                config.fxOrder[static_cast<size_t>(i)] =
                    static_cast<uint8_t>(current.slots[i]);
            }
        };

        ChannelConfig& channelConfig = project.channels[ui.selectedChannel];

        const auto& order = rackFx.rack();
        int moveFrom = -1;
        int moveTo = -1;

        for (int slot = 0; slot < order.count; ++slot) {
            const EffectType type = order.slots[slot];
            const bool on = rackFx.isEnabled(type);

            ImGui::PushID(slot);

            // The enable flag is the same one the panels below write, so the
            // rack and the old checkboxes can never disagree.
            bool enabled = on;
            if (ImGui::Checkbox("##rackon", &enabled)) {
                g_UndoHistory.saveState(project, "Toggle Effect");
                rackFx.setEnabled(type, enabled);
                // Mirror into the config so it survives a save.
                switch (type) {
                    case EffectType::EQ:             channelConfig.eqEnabled = enabled; break;
                    case EffectType::TapeSaturation: channelConfig.tapeSaturationEnabled = enabled; break;
                    case EffectType::Formant:        channelConfig.formantEnabled = enabled; break;
                    case EffectType::Compressor:     channelConfig.compressorEnabled = enabled; break;
                    case EffectType::Bitcrusher:     channelConfig.bitcrusherEnabled = enabled; break;
                    case EffectType::Distortion:     channelConfig.distortionEnabled = enabled; break;
                    case EffectType::Filter:         channelConfig.filterEnabled = enabled; break;
                    case EffectType::Tremolo:        channelConfig.tremoloEnabled = enabled; break;
                    case EffectType::Phaser:         channelConfig.phaserEnabled = enabled; break;
                    case EffectType::Flanger:        channelConfig.flangerEnabled = enabled; break;
                    case EffectType::Chorus:         channelConfig.chorusEnabled = enabled; break;
                    case EffectType::Delay:          channelConfig.delayEnabled = enabled; break;
                    case EffectType::Reverb:         channelConfig.reverbEnabled = enabled; break;
                    default: break;   // RingMod has no config field yet
                }
            }
            ImGui::SameLine();

            // Theme colours only - an enabled slot uses the theme's own text
            // colour, a bypassed one the disabled variant. No literals here,
            // so every theme and the contrast assertions still hold.
            const ImVec4 label = enabled
                ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

            char row[64];
            snprintf(row, sizeof(row), "%2d  %s", slot + 1, effectDisplayName(type));

            ImGui::PushStyleColor(ImGuiCol_Text, label);
            ImGui::Selectable(row, false, 0, ImVec2(190.0f, 0.0f));
            ImGui::PopStyleColor();

            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                const int target = slot + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
                if (target >= 0 && target < order.count) {
                    moveFrom = slot;
                    moveTo = target;
                    ImGui::ResetMouseDragDelta();
                }
            }
            if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
                ImGui::SetTooltip("Drag to move %s earlier or later in the chain.\n"
                                  "Its own controls are below.",
                                  effectDisplayName(type));
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            float& wet = rackFx.mix[static_cast<size_t>(type)];
            if (ImGui::SliderFloat("##rackmix", &wet, 0.0f, 1.0f, "mix %.2f")) {
                wet = std::clamp(wet, 0.0f, 1.0f);
            }

            ImGui::PopID();
        }

        // Applied after the loop so the list is not mutated while drawing it.
        if (moveFrom >= 0) {
            g_UndoHistory.saveState(project, "Reorder Effects");
            rackFx.moveSlot(moveFrom, moveTo);
            publishOrder(channelConfig);
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset to classic order")) {
            g_UndoHistory.saveState(project, "Reset Effect Order");
            rackFx.resetOrderToClassic();
            channelConfig.fxSlotCount = 0;   // 0 means classic
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The fixed order every version before the rack\n"
                              "used: EQ, tape, formant, compressor, bitcrush,\n"
                              "distortion, filter, ring mod, tremolo, phaser,\n"
                              "flanger, chorus, delay, reverb.");
        }
        if (!rackFx.isClassicOrder()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(reordered)");
        }
    }

    if (ImGui::CollapsingHeader("Effects")) {
        auto& fx = seq.getSynth(ui.selectedChannel).effects();

        // Bitcrusher
        ImGui::Checkbox("Bitcrusher", &channel.bitcrusherEnabled);
        if (fx.bitcrusherEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Bit Depth", &channel.bitDepth, 1.0f, 16.0f);
            ImGui::SliderFloat("Sample Rate Div", &channel.sampleRateDiv, 1.0f, 32.0f);
            ImGui::Unindent();
        }

        // Distortion
        ImGui::Checkbox("Distortion", &channel.distortionEnabled);
        if (fx.distortionEnabled) {
            ImGui::Indent();
            const char* distTypes[] = {"Tanh", "Hard Clip", "Foldback", "Asymmetric"};
            int distType = static_cast<int>(fx.distortion.type);
            ImGui::Combo("Type##dist", &distType, distTypes, IM_ARRAYSIZE(distTypes));
            fx.distortion.type = static_cast<DistortionType>(distType);
            ImGui::SliderFloat("Drive##dist", &channel.distortionDrive, 1.0f, 10.0f);
            ImGui::SliderFloat("Mix##dist", &channel.distortionMix, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Filter
        ImGui::Checkbox("Filter", &channel.filterEnabled);
        if (fx.filterEnabled) {
            ImGui::Indent();
            const char* filterTypes[] = {"Low Pass", "High Pass", "Band Pass"};
            int filterType = static_cast<int>(fx.filter.type);
            ImGui::Combo("Type##filter", &filterType, filterTypes, IM_ARRAYSIZE(filterTypes));
            fx.filter.type = static_cast<FilterType>(filterType);
            ImGui::SliderFloat("Cutoff##filter", &channel.filterCutoff, 20.0f, 10000.0f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Resonance##filter", &channel.filterResonance, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Delay
        ImGui::Checkbox("Delay", &channel.delayEnabled);
        if (fx.delayEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Time##delay", &channel.delayTime, 0.01f, 1.0f, "%.3f s");
            ImGui::SliderFloat("Feedback##delay", &channel.delayFeedback, 0.0f, 0.95f);
            ImGui::SliderFloat("Mix##delay", &channel.delayMix, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Chorus
        ImGui::Checkbox("Chorus", &channel.chorusEnabled);
        if (fx.chorusEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Rate##chorus", &channel.chorusRate, 0.1f, 5.0f, "%.2f Hz");
            ImGui::SliderFloat("Depth##chorus", &channel.chorusDepth, 0.0f, 0.02f);
            ImGui::SliderFloat("Mix##chorus", &channel.chorusMix, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Phaser
        ImGui::Checkbox("Phaser", &channel.phaserEnabled);
        if (fx.phaserEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Rate##phaser", &channel.phaserRate, 0.1f, 2.0f, "%.2f Hz");
            ImGui::SliderFloat("Depth##phaser", &channel.phaserDepth, 0.0f, 1.0f);
            ImGui::SliderFloat("Feedback##phaser", &channel.phaserFeedback, 0.0f, 0.95f);
            ImGui::Unindent();
        }

        // Tremolo
        ImGui::Checkbox("Tremolo", &channel.tremoloEnabled);
        if (fx.tremoloEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Rate##trem", &channel.tremoloRate, 0.5f, 20.0f, "%.1f Hz");
            ImGui::SliderFloat("Depth##trem", &channel.tremoloDepth, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // Ring Mod
        ImGui::Checkbox("Ring Modulator", &channel.ringModEnabled);
        if (fx.ringModEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Freq##ring", &channel.ringModFrequency, 20.0f, 2000.0f, "%.0f Hz");
            ImGui::SliderFloat("Mix##ring", &channel.ringModMix, 0.0f, 1.0f);
            ImGui::Unindent();
        }

        // ---- Tilt -------------------------------------------------------------
        //
        // One control, and the one most people actually want most of the
        // time: brighter or darker without deciding which band to touch.
        ImGui::Checkbox("Tilt EQ", &channel.tiltEqEnabled);
        if (channel.tiltEqEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Tilt##tilt", &channel.tiltEqAmount,
                               -12.0f, 12.0f, "%.1f dB");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Right is brighter, left is darker. The two ends pivot\n"
                    "around the centre frequency, so the overall level\n"
                    "barely moves - it is a tone control, not a fader.");
            }
            ImGui::SliderFloat("Pivot##tilt", &channel.tiltEqCentre,
                               50.0f, 8000.0f, "%.0f Hz");
            ImGui::Unindent();
        }

        // ---- Graphic ----------------------------------------------------------
        ImGui::Checkbox("Graphic EQ", &channel.graphicEqEnabled);
        if (channel.graphicEqEnabled) {
            ImGui::Indent();
            ImGui::TextDisabled("Ten octave bands. The curve is the sliders.");

            // Vertical sliders, because a graphic EQ that does not look like
            // its own response curve gives up the only thing it is good at.
            for (int band = 0; band < GraphicEQ::BANDS; ++band) {
                if (band > 0) ImGui::SameLine();
                ImGui::PushID(1200 + band);
                ImGui::VSliderFloat("##band", ImVec2(20, 100),
                                    &channel.graphicEqGains[static_cast<size_t>(band)],
                                    -18.0f, 18.0f, "");
                if (ImGui::IsItemHovered()) {
                    const float centre = GraphicEQ::CENTRES[static_cast<size_t>(band)];
                    ImGui::SetTooltip("%.0f Hz: %+.1f dB", centre,
                                      channel.graphicEqGains[static_cast<size_t>(band)]);
                }
                ImGui::PopID();
            }

            if (ImGui::SmallButton("Flat##graphic")) {
                for (float& gain : channel.graphicEqGains) gain = 0.0f;
            }
            ImGui::Unindent();
        }

        // ---- Dynamic ----------------------------------------------------------
        ImGui::Checkbox("Dynamic EQ", &channel.dynamicEqEnabled);
        if (channel.dynamicEqEnabled) {
            ImGui::Indent();
            ImGui::TextDisabled(
                "Acts only when the band is actually loud, so a note that\n"
                "turns boxy on the hits is fixed without the quiet ones\n"
                "going thin.");
            ImGui::SliderFloat("Freq##dyn", &channel.dynamicEqFrequency,
                               20.0f, 18000.0f, "%.0f Hz");
            ImGui::SliderFloat("Q##dyn", &channel.dynamicEqQ, 0.2f, 10.0f, "%.2f");
            ImGui::SliderFloat("Threshold##dyn", &channel.dynamicEqThreshold,
                               -60.0f, 0.0f, "%.1f dB");
            ImGui::SliderFloat("Range##dyn", &channel.dynamicEqRange,
                               -24.0f, 24.0f, "%.1f dB");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Negative ducks the band when it gets loud; positive\n"
                    "lifts it. Ducking is the usual one.");
            }
            ImGui::SliderFloat("Attack##dyn", &channel.dynamicEqAttack,
                               0.001f, 0.2f, "%.3f s");
            ImGui::SliderFloat("Release##dyn", &channel.dynamicEqRelease,
                               0.005f, 1.0f, "%.3f s");
            ImGui::Unindent();
        }

        // ---- Convolution ----------------------------------------------------
        //
        // An algorithmic reverb approximates a space. This one convolves with
        // a recording of an actual space, so there is nothing to tune - the
        // room is the data.
        ImGui::Checkbox("Convolution Reverb", &channel.convolutionEnabled);
        if (channel.convolutionEnabled) {
            ImGui::Indent();

            const int slots = IRLibrary::SLOTS;
            int selected = std::clamp(channel.convolutionIR, 0, slots - 1);

            ImGui::SetNextItemWidth(160);
            if (ImGui::BeginCombo("Space##conv",
                    impulseResponseName(static_cast<ImpulseResponse>(selected)))) {
                for (int i = 0; i < slots; ++i) {
                    const bool isSelected = (i == selected);
                    if (ImGui::Selectable(
                            impulseResponseName(static_cast<ImpulseResponse>(i)),
                            isSelected)) {
                        channel.convolutionIR = i;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The built-in spaces are synthesised, which gives a\n"
                    "convincing tail but none of the early reflections that\n"
                    "make one real room sound different from another.\n"
                    "Load a recorded impulse response for that.");
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Load IR...")) {
                const std::string path = openFileDialog(
                    "Impulse Responses (*.wav)\0*.wav\0All Files (*.*)\0*.*\0",
                    "wav");
                if (!path.empty()) {
                    // Through the sample pool, so an IR gets the same
                    // decoding and rate conversion as any other audio.
                    SamplePool pool;
                    const int id = pool.loadSample(path);
                    const Sample* sample = pool.getSample(id);
                    if (sample != nullptr && !sample->audioData.empty()) {
                        seq.setCustomIR(sample->audioData);
                        channel.convolutionIR =
                            static_cast<int>(ImpulseResponse::Custom);
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "A recorded impulse response. Longer than two seconds is\n"
                    "truncated - past that the cost stops being worth what\n"
                    "the extra tail adds.");
            }

            ImGui::SliderFloat("Mix##conv", &channel.convolutionMix, 0.0f, 1.0f);

            ImGui::TextDisabled("~12 ms latency on this channel.");
            ImGui::Unindent();
        }

        // Reverb
        ImGui::Checkbox("Reverb", &channel.reverbEnabled);
        if (fx.reverbEnabled) {
            ImGui::Indent();

            /*
             * The algorithm, bound to the CONFIG rather than to the live
             * chain like the sliders below it.
             *
             * That inconsistency is deliberate and temporary: the config is
             * the only one of the two that is saved, and changing the
             * algorithm has to re-size the tank's delay lines, which only
             * happens through applyEffectsConfig. The surrounding controls
             * writing straight to the chain is a bug of its own and is
             * fixed separately.
             */
            const int algorithmCount = static_cast<int>(ReverbAlgorithm::Count);
            int algorithm = std::clamp(channel.reverbAlgorithm, 0, algorithmCount - 1);

            ImGui::SetNextItemWidth(150);
            if (ImGui::BeginCombo("Algorithm##rev",
                    reverbAlgorithmName(static_cast<ReverbAlgorithm>(algorithm)))) {
                for (int i = 0; i < algorithmCount; ++i) {
                    const ReverbAlgorithm candidate = static_cast<ReverbAlgorithm>(i);
                    const bool selected = (i == algorithm);
                    if (ImGui::Selectable(reverbAlgorithmName(candidate), selected)) {
                        channel.reverbAlgorithm = i;
                        seq.updateChannelConfigs();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", reverbAlgorithmBlurb(candidate));
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("%s", reverbAlgorithmBlurb(
                static_cast<ReverbAlgorithm>(algorithm)));

            ImGui::SliderFloat("Room Size##rev", &channel.reverbRoomSize, 0.1f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Size of the virtual room (larger = longer decay)");
            ImGui::SliderFloat("Damping##rev", &channel.reverbDamping, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("High frequency absorption (higher = darker reverb)");
            ImGui::SliderFloat("Mix##rev", &channel.reverbMix, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wet/dry mix (0 = dry only, 1 = wet only)");

            // Quick presets for reverb
            ImGui::Text("Presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Small Room##rev")) {
                fx.reverb.roomSize = 0.3f;
                fx.reverb.damping = 0.5f;
                fx.reverb.mix = 0.2f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hall##rev")) {
                fx.reverb.roomSize = 0.7f;
                fx.reverb.damping = 0.3f;
                fx.reverb.mix = 0.35f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Cathedral##rev")) {
                fx.reverb.roomSize = 0.95f;
                fx.reverb.damping = 0.2f;
                fx.reverb.mix = 0.5f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Plate##rev")) {
                fx.reverb.roomSize = 0.5f;
                fx.reverb.damping = 0.6f;
                fx.reverb.mix = 0.3f;
            }
            ImGui::Unindent();
        }

        // Stereo Widener (for lush synthwave pads - classic 80s wide sound)
        ImGui::Checkbox("Stereo Widener", &channel.stereoWidenerEnabled);
        if (fx.stereoWidenerEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Width##sw", &channel.stereoWidenerWidth, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stereo width (0 = mono, 1 = ultra wide)");
            ImGui::SliderFloat("Haas Delay##sw", &channel.stereoWidenerHaas, 0.005f, 0.035f, "%.3f sec");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Haas effect delay (10-30ms creates stereo perception)");
            ImGui::SliderFloat("Mix##sw", &channel.stereoWidenerMix, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wet/dry mix");

            // Quick presets for stereo widener
            ImGui::Text("Presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Subtle##sw")) {
                fx.stereoWidener.width = 0.3f;
                fx.stereoWidener.haasDelay = 0.010f;
                fx.stereoWidener.mix = 0.4f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Wide Pad##sw")) {
                fx.stereoWidener.width = 0.7f;
                fx.stereoWidener.haasDelay = 0.020f;
                fx.stereoWidener.mix = 0.6f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Ultra Wide##sw")) {
                fx.stereoWidener.width = 1.0f;
                fx.stereoWidener.haasDelay = 0.030f;
                fx.stereoWidener.mix = 0.8f;
            }
            ImGui::Unindent();
        }

        // Tape Saturation (warm analog character - classic 80s tape sound)
        ImGui::Checkbox("Tape Saturation", &channel.tapeSaturationEnabled);
        if (fx.tapeSaturationEnabled) {
            ImGui::Indent();
            ImGui::SliderFloat("Drive##tape", &channel.tapeDrive, 1.0f, 3.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Saturation amount (1 = clean, 3 = heavily saturated)");
            ImGui::SliderFloat("Warmth##tape", &channel.tapeWarmth, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("High frequency roll-off (higher = darker, warmer)");
            ImGui::SliderFloat("Compression##tape", &channel.tapeCompression, 0.0f, 0.8f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Soft compression (tape limiting characteristic)");
            ImGui::SliderFloat("Mix##tape", &channel.tapeMix, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wet/dry mix");

            // Quick presets for tape saturation
            ImGui::Text("Presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Subtle Warmth##tape")) {
                fx.tapeSaturation.drive = 1.3f;
                fx.tapeSaturation.warmth = 0.3f;
                fx.tapeSaturation.compression = 0.2f;
                fx.tapeSaturation.mix = 0.4f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Cassette##tape")) {
                fx.tapeSaturation.drive = 1.8f;
                fx.tapeSaturation.warmth = 0.5f;
                fx.tapeSaturation.compression = 0.4f;
                fx.tapeSaturation.mix = 0.6f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hot Tape##tape")) {
                fx.tapeSaturation.drive = 2.5f;
                fx.tapeSaturation.warmth = 0.7f;
                fx.tapeSaturation.compression = 0.6f;
                fx.tapeSaturation.mix = 0.8f;
            }
            ImGui::Unindent();
        }

        // Sidechain Compression (for that classic EDM pumping effect)
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Sidechain Compression");
        ImGui::Checkbox("Enable Sidechain", &channel.sidechainEnabled);
        if (fx.sidechainEnabled) {
            ImGui::Indent();

            // Source channel selector
            ImGui::Text("Duck this channel when source plays");
            const char* channelNames[] = {"Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8"};
            int srcIdx = fx.sidechainSource;
            if (srcIdx < 0) srcIdx = 0;
            if (ImGui::Combo("Source Channel", &srcIdx, channelNames, IM_ARRAYSIZE(channelNames))) {
                fx.sidechainSource = srcIdx;
            }
            if (fx.sidechainSource == ui.selectedChannel) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Warning: Source is same as target!");
            }

            ImGui::SliderFloat("Threshold##sc", &channel.sidechainThreshold, 0.01f, 0.9f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Level at which ducking starts");

            ImGui::SliderFloat("Amount##sc", &channel.sidechainAmount, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much to duck (1.0 = full silence)");

            ImGui::SliderFloat("Attack##sc", &channel.sidechainAttack, 0.001f, 0.1f, "%.3f s");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How fast the duck kicks in");

            ImGui::SliderFloat("Release##sc", &channel.sidechainRelease, 0.05f, 1.0f, "%.2f s");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How fast the volume returns");

            // Visual feedback - show current gain reduction
            float gainRed = fx.sidechain.getGainReduction();
            ImGui::ProgressBar(gainRed, ImVec2(-1, 0), "");
            ImGui::SameLine(0, 0);
            ImGui::Text(" Ducking: %.0f%%", gainRed * 100.0f);

            // Quick presets
            ImGui::Text("Presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Subtle##sc")) {
                fx.sidechain.threshold = 0.3f;
                fx.sidechain.amount = 0.3f;
                fx.sidechain.attack = 0.005f;
                fx.sidechain.release = 0.2f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Normal##sc")) {
                fx.sidechain.threshold = 0.2f;
                fx.sidechain.amount = 0.6f;
                fx.sidechain.attack = 0.005f;
                fx.sidechain.release = 0.15f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Heavy##sc")) {
                fx.sidechain.threshold = 0.1f;
                fx.sidechain.amount = 0.9f;
                fx.sidechain.attack = 0.002f;
                fx.sidechain.release = 0.1f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Pumping##sc")) {
                fx.sidechain.threshold = 0.05f;
                fx.sidechain.amount = 1.0f;
                fx.sidechain.attack = 0.001f;
                fx.sidechain.release = 0.25f;
            }

            ImGui::Unindent();
        }
    }

    /*
     * Push this channel's settings into its synth, once, at the end.
     *
     * The effect controls above used to write straight into the live chain,
     * which meant none of them was saved and all of them were reverted the
     * next time anything called updateChannelConfigs(). They write to the
     * project now, which fixes both - but the engine still has to be told.
     *
     * One sync here rather than a call bolted onto each of the forty-five
     * controls: a list of forty-five is a list that grows a forty-sixth
     * without one, and that control then appears to do nothing until
     * something else happens to sync. This cannot be forgotten.
     *
     * It is only the selected channel, not all thirty-two, and in the steady
     * state it copies fields and allocates nothing - the reverb tank only
     * re-prepares when the algorithm actually changes.
     */
    if (ui.selectedChannel >= 0 && ui.selectedChannel < Project::MAX_CHANNELS) {
        seq.getSynth(ui.selectedChannel)
           .setChannelConfig(project.channels[ui.selectedChannel]);
    }

    ImGui::End();
}

// ============================================================================
// Pattern List
// ============================================================================
inline void DrawPatternList(Project& project, UIState& ui) {
    ImGui::SetNextWindowPos(ImVec2(10, 645), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 180), ImGuiCond_FirstUseEver);
    ImGui::Begin("Patterns");

    if (ImGui::Button("+ New")) {
        Pattern p;
        p.name = "Pattern " + std::to_string(project.patterns.size() + 1);
        project.patterns.push_back(p);
    }
    ImGui::SameLine();

    // Delete button - only enabled if pattern is selected and we have more than 1 pattern
    bool canDelete = ui.selectedPattern >= 0 &&
                     ui.selectedPattern < static_cast<int>(project.patterns.size()) &&
                     project.patterns.size() > 1;
    if (!canDelete) ImGui::BeginDisabled();
    if (ImGui::Button("Delete") && canDelete) {
        project.patterns.erase(project.patterns.begin() + ui.selectedPattern);
        // Adjust selection
        if (ui.selectedPattern >= static_cast<int>(project.patterns.size())) {
            ui.selectedPattern = static_cast<int>(project.patterns.size()) - 1;
        }
        ui.selectedNoteIndex = -1;
        ui.selectedNoteIndices.clear();
    }
    if (!canDelete) ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (project.patterns.size() <= 1) {
            ImGui::SetTooltip("Cannot delete last pattern");
        } else {
            ImGui::SetTooltip("Delete selected pattern (Del)");
        }
    }

    ImGui::Separator();

    // Track if this window is focused for keyboard shortcuts
    bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    for (size_t i = 0; i < project.patterns.size(); ++i) {
        auto& pattern = project.patterns[i];
        bool isSelected = (static_cast<int>(i) == ui.selectedPattern);

        if (ImGui::Selectable(pattern.name.c_str(), isSelected)) {
            ui.selectedPattern = static_cast<int>(i);
            ui.selectedNoteIndex = -1;
            ui.selectedNoteIndices.clear();
        }
    }

    // Handle Delete key when window is focused
    if (windowFocused && ImGui::IsKeyPressed(ImGuiKey_Delete) && canDelete) {
        project.patterns.erase(project.patterns.begin() + ui.selectedPattern);
        if (ui.selectedPattern >= static_cast<int>(project.patterns.size())) {
            ui.selectedPattern = static_cast<int>(project.patterns.size()) - 1;
        }
        ui.selectedNoteIndex = -1;
        ui.selectedNoteIndices.clear();
    }

    ImGui::End();
}

// ============================================================================
// Note Editor Panel - Edit selected note properties
// ============================================================================
inline void DrawNoteEditor(Project& project, UIState& ui) {
    ImGui::SetNextWindowPos(ImVec2(1130, 135), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 240), ImGuiCond_FirstUseEver);
    ImGui::Begin("Note Editor");

    if (ui.selectedPattern < 0 || ui.selectedPattern >= static_cast<int>(project.patterns.size())) {
        ImGui::TextDisabled("No pattern selected");
        ImGui::End();
        return;
    }

    Pattern& pattern = project.patterns[ui.selectedPattern];

    // Check for multi-selection
    bool hasMultiSelection = !ui.selectedNoteIndices.empty() && ui.selectedNoteIndices.size() > 1;
    bool hasSingleSelection = ui.selectedNoteIndex >= 0 && ui.selectedNoteIndex < static_cast<int>(pattern.notes.size());

    // Validate multi-selection indices
    std::vector<int> validIndices;
    if (hasMultiSelection) {
        for (int idx : ui.selectedNoteIndices) {
            if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                validIndices.push_back(idx);
            }
        }
        hasMultiSelection = validIndices.size() > 1;
    }

    if (!hasMultiSelection && !hasSingleSelection) {
        ImGui::TextDisabled("No note selected");
        ImGui::Separator();
        ImGui::TextWrapped("Select a note in the Piano Roll to edit its properties.");
        ImGui::TextWrapped("Use Select mode (S) and click on a note.");
        ImGui::End();
        return;
    }

    // ==========================================================================
    // MULTI-NOTE EDITING MODE
    // ==========================================================================
    if (hasMultiSelection) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.8f, 1.0f), "Editing %d Notes", static_cast<int>(validIndices.size()));
        ImGui::Separator();

        // Helper to check if values are mixed
        auto checkMixed = [&](auto getter) {
            auto firstVal = getter(pattern.notes[validIndices[0]]);
            for (size_t i = 1; i < validIndices.size(); ++i) {
                if (getter(pattern.notes[validIndices[i]]) != firstVal) return true;
            }
            return false;
        };

        // Helper to apply value to all selected notes
        auto applyToAll = [&](auto setter) {
            for (int idx : validIndices) {
                setter(pattern.notes[idx]);
            }
        };

        // Use first note as reference
        Note& refNote = pattern.notes[validIndices[0]];

        // Duration editing (absolute)
        if (ImGui::CollapsingHeader("Duration", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool durationMixed = checkMixed([](const Note& n) { return n.duration; });

            float displayDuration = refNote.duration;
            ImGui::SetNextItemWidth(120);
            if (durationMixed) {
                ImGui::TextDisabled("(mixed values)");
            }
            if (ImGui::DragFloat("Duration##multi", &displayDuration, 0.0625f, 0.0625f, 16.0f, "%.3f")) {
                applyToAll([displayDuration](Note& n) { n.duration = std::max(0.0625f, displayDuration); });
            }

            ImGui::Text("Quick:");
            ImGui::SameLine();
            if (ImGui::SmallButton("1/16##md")) applyToAll([](Note& n) { n.duration = 0.0625f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("1/8##md")) applyToAll([](Note& n) { n.duration = 0.125f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("1/4##md")) applyToAll([](Note& n) { n.duration = 0.25f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("1/2##md")) applyToAll([](Note& n) { n.duration = 0.5f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("1##md")) applyToAll([](Note& n) { n.duration = 1.0f; });
        }

        // Velocity editing
        if (ImGui::CollapsingHeader("Velocity", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool velocityMixed = checkMixed([](const Note& n) { return n.velocity; });

            float displayVelocity = refNote.velocity * 100.0f;
            ImGui::SetNextItemWidth(200);
            if (velocityMixed) {
                ImGui::TextDisabled("(mixed values)");
            }
            if (ImGui::SliderFloat("##velocity_multi", &displayVelocity, 0.0f, 100.0f, "%.0f%%")) {
                float newVel = displayVelocity / 100.0f;
                applyToAll([newVel](Note& n) { n.velocity = newVel; });
            }

            if (ImGui::SmallButton("25%##mv")) applyToAll([](Note& n) { n.velocity = 0.25f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("50%##mv")) applyToAll([](Note& n) { n.velocity = 0.5f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("75%##mv")) applyToAll([](Note& n) { n.velocity = 0.75f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("100%##mv")) applyToAll([](Note& n) { n.velocity = 1.0f; });
        }

        // Fade editing
        if (ImGui::CollapsingHeader("Fade", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Fade In:");
            float displayFadeIn = refNote.fadeIn;
            ImGui::SetNextItemWidth(200);
            if (ImGui::SliderFloat("##fadein_multi", &displayFadeIn, 0.0f, 1.0f, "%.3f beats")) {
                applyToAll([displayFadeIn](Note& n) { n.fadeIn = std::min(displayFadeIn, n.duration * 0.5f); });
            }
            if (ImGui::SmallButton("0##mfi")) applyToAll([](Note& n) { n.fadeIn = 0.0f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("1/16##mfi")) applyToAll([](Note& n) { n.fadeIn = std::min(0.0625f, n.duration * 0.5f); });
            ImGui::SameLine();
            if (ImGui::SmallButton("1/8##mfi")) applyToAll([](Note& n) { n.fadeIn = std::min(0.125f, n.duration * 0.5f); });

            ImGui::Spacing();

            ImGui::Text("Fade Out:");
            float displayFadeOut = refNote.fadeOut;
            ImGui::SetNextItemWidth(200);
            if (ImGui::SliderFloat("##fadeout_multi", &displayFadeOut, 0.0f, 1.0f, "%.3f beats")) {
                applyToAll([displayFadeOut](Note& n) { n.fadeOut = std::min(displayFadeOut, n.duration * 0.5f); });
            }
            if (ImGui::SmallButton("0##mfo")) applyToAll([](Note& n) { n.fadeOut = 0.0f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("1/16##mfo")) applyToAll([](Note& n) { n.fadeOut = std::min(0.0625f, n.duration * 0.5f); });
            ImGui::SameLine();
            if (ImGui::SmallButton("1/8##mfo")) applyToAll([](Note& n) { n.fadeOut = std::min(0.125f, n.duration * 0.5f); });
        }

        // Effects editing
        if (ImGui::CollapsingHeader("Note Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Vibrato:");
            float displayVibrato = refNote.vibrato;
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderFloat("##vibrato_multi", &displayVibrato, 0.0f, 1.0f, "%.2f")) {
                applyToAll([displayVibrato](Note& n) { n.vibrato = displayVibrato; });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("0##mvib")) applyToAll([](Note& n) { n.vibrato = 0.0f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("0.3##mvib")) applyToAll([](Note& n) { n.vibrato = 0.3f; });
            ImGui::SameLine();
            if (ImGui::SmallButton("0.5##mvib")) applyToAll([](Note& n) { n.vibrato = 0.5f; });

            ImGui::Text("Arpeggio presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Off##marp")) applyToAll([](Note& n) { n.arpeggio = 0x00; });
            ImGui::SameLine();
            if (ImGui::SmallButton("Maj##marp")) applyToAll([](Note& n) { n.arpeggio = 0x47; });
            ImGui::SameLine();
            if (ImGui::SmallButton("Min##marp")) applyToAll([](Note& n) { n.arpeggio = 0x37; });
            ImGui::SameLine();
            if (ImGui::SmallButton("Oct##marp")) applyToAll([](Note& n) { n.arpeggio = 0xC0; });

            ImGui::Text("Slide:");
            float displaySlide = refNote.slide;
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderFloat("##slide_multi", &displaySlide, -12.0f, 12.0f, "%.1f st")) {
                applyToAll([displaySlide](Note& n) { n.slide = displaySlide; });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("0##mslide")) applyToAll([](Note& n) { n.slide = 0.0f; });

            ImGui::Separator();
            if (ImGui::Button("Reset All Effects##multi")) {
                applyToAll([](Note& n) {
                    n.vibrato = 0.0f;
                    n.slide = 0.0f;
                    n.arpeggio = 0;
                });
            }
        }

        ImGui::Separator();

        // Actions for multi-selection
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Delete Selected Notes")) {
            // Sort indices in descending order for safe deletion
            std::sort(validIndices.begin(), validIndices.end(), std::greater<int>());
            for (int idx : validIndices) {
                pattern.notes.erase(pattern.notes.begin() + idx);
            }
            ui.selectedNoteIndex = -1;
            ui.selectedNoteIndices.clear();
        }
        ImGui::PopStyleColor();

        ImGui::End();
        return;
    }

    // ==========================================================================
    // SINGLE NOTE EDITING MODE (original behavior)
    // ==========================================================================
    Note& note = pattern.notes[ui.selectedNoteIndex];

    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Editing Note %d", ui.selectedNoteIndex);
    ImGui::Separator();

    // Pitch editing
    if (ImGui::CollapsingHeader("Pitch", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Current pitch display
        ImGui::Text("Current: %s (MIDI %d)", noteToString(note.pitch).c_str(), note.pitch);

        // Semitone adjustment
        ImGui::Text("Semitone:");
        ImGui::SameLine();
        if (ImGui::Button("-1")) {
            note.pitch = std::max(0, note.pitch - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("+1")) {
            note.pitch = std::min(127, note.pitch + 1);
        }

        ImGui::SameLine(0, 20);

        // Octave adjustment
        ImGui::Text("Octave:");
        ImGui::SameLine();
        if (ImGui::Button("-12")) {
            note.pitch = std::max(0, note.pitch - 12);
        }
        ImGui::SameLine();
        if (ImGui::Button("+12")) {
            note.pitch = std::min(127, note.pitch + 12);
        }

        // Direct MIDI input
        ImGui::SetNextItemWidth(100);
        int midiNote = note.pitch;
        if (ImGui::InputInt("MIDI Note", &midiNote)) {
            note.pitch = std::clamp(midiNote, 0, 127);
        }

        // Quick note buttons (one octave)
        ImGui::Text("Quick Notes:");
        const char* noteLabels[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int currentOctave = note.pitch / 12;
        for (int i = 0; i < 12; ++i) {
            if (i > 0) ImGui::SameLine();
            bool isCurrentNote = (note.pitch % 12 == i);
            if (isCurrentNote) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            }
            if (ImGui::SmallButton(noteLabels[i])) {
                note.pitch = currentOctave * 12 + i;
            }
            if (isCurrentNote) {
                ImGui::PopStyleColor();
            }
        }

        // Octave selector
        ImGui::Text("Octave:");
        ImGui::SameLine();
        for (int oct = 0; oct <= 8; ++oct) {
            if (oct > 0) ImGui::SameLine();
            char octLabel[4];
            snprintf(octLabel, sizeof(octLabel), "%d", oct);
            bool isCurrentOct = (currentOctave == oct);
            if (isCurrentOct) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            }
            if (ImGui::SmallButton(octLabel)) {
                note.pitch = oct * 12 + (note.pitch % 12);
            }
            if (isCurrentOct) {
                ImGui::PopStyleColor();
            }
        }
    }

    // Position
    if (ImGui::CollapsingHeader("Position & Duration", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(120);
        if (ImGui::DragFloat("Start (beats)", &note.startTime, 0.0625f, 0.0f, 1000.0f, "%.3f")) {
            note.startTime = std::max(0.0f, note.startTime);
        }

        ImGui::SetNextItemWidth(120);
        if (ImGui::DragFloat("Duration (beats)", &note.duration, 0.0625f, 0.0625f, 16.0f, "%.3f")) {
            note.duration = std::max(0.0625f, note.duration);
        }

        // Quick duration buttons
        ImGui::Text("Quick:");
        ImGui::SameLine();
        if (ImGui::SmallButton("1/16")) note.duration = 0.0625f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/8")) note.duration = 0.125f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/4")) note.duration = 0.25f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/2")) note.duration = 0.5f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1")) note.duration = 1.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("2")) note.duration = 2.0f;
    }

    // Velocity
    if (ImGui::CollapsingHeader("Velocity", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(200);
        // Display velocity as 0-100% (value is 0.0-1.0)
        float velocityPercent = note.velocity * 100.0f;
        if (ImGui::SliderFloat("##velocity", &velocityPercent, 0.0f, 100.0f, "%.0f%%")) {
            note.velocity = velocityPercent / 100.0f;
        }

        // Quick velocity buttons
        if (ImGui::SmallButton("25%")) note.velocity = 0.25f;
        ImGui::SameLine();
        if (ImGui::SmallButton("50%")) note.velocity = 0.5f;
        ImGui::SameLine();
        if (ImGui::SmallButton("75%")) note.velocity = 0.75f;
        ImGui::SameLine();
        if (ImGui::SmallButton("100%")) note.velocity = 1.0f;
    }

    // Fade In/Out
    if (ImGui::CollapsingHeader("Fade", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Fade In:");
        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("##fadein", &note.fadeIn, 0.0f, note.duration * 0.5f, "%.3f beats");

        // Quick fade in buttons
        if (ImGui::SmallButton("0##fi")) note.fadeIn = 0.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/16##fi")) note.fadeIn = 0.0625f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/8##fi")) note.fadeIn = 0.125f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/4##fi")) note.fadeIn = 0.25f;

        ImGui::Spacing();

        ImGui::Text("Fade Out:");
        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("##fadeout", &note.fadeOut, 0.0f, note.duration * 0.5f, "%.3f beats");

        // Quick fade out buttons
        if (ImGui::SmallButton("0##fo")) note.fadeOut = 0.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/16##fo")) note.fadeOut = 0.0625f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/8##fo")) note.fadeOut = 0.125f;
        ImGui::SameLine();
        if (ImGui::SmallButton("1/4##fo")) note.fadeOut = 0.25f;

        // Ensure fades don't exceed note duration
        float maxFade = note.duration * 0.5f;
        note.fadeIn = std::min(note.fadeIn, maxFade);
        note.fadeOut = std::min(note.fadeOut, maxFade);
    }

    // Per-note effects
    if (ImGui::CollapsingHeader("Note Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Vibrato
        ImGui::Text("Vibrato (pitch wobble):");
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("##Vibrato", &note.vibrato, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton("0##vib")) note.vibrato = 0.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("0.3##vib")) note.vibrato = 0.3f;
        ImGui::SameLine();
        if (ImGui::SmallButton("0.5##vib")) note.vibrato = 0.5f;

        // Arpeggio (tracker-style 0xy effect)
        ImGui::Text("Arpeggio (chord cycling):");
        int arpX = (note.arpeggio >> 4) & 0x0F;
        int arpY = note.arpeggio & 0x0F;
        ImGui::SetNextItemWidth(60);
        if (ImGui::SliderInt("X##arp", &arpX, 0, 12, "+%d")) {
            note.arpeggio = (arpX << 4) | (note.arpeggio & 0x0F);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        if (ImGui::SliderInt("Y##arp", &arpY, 0, 12, "+%d")) {
            note.arpeggio = (note.arpeggio & 0xF0) | (arpY & 0x0F);
        }
        // Arpeggio presets
        ImGui::Text("Presets:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Off##arp")) note.arpeggio = 0x00;
        ImGui::SameLine();
        if (ImGui::SmallButton("Maj##arp")) note.arpeggio = 0x47;  // +4, +7 = major chord
        ImGui::SameLine();
        if (ImGui::SmallButton("Min##arp")) note.arpeggio = 0x37;  // +3, +7 = minor chord
        ImGui::SameLine();
        if (ImGui::SmallButton("Oct##arp")) note.arpeggio = 0xC0;  // +12, +0 = octave

        // Slide/Portamento
        ImGui::Text("Slide (pitch start offset):");
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("##Slide", &note.slide, -12.0f, 12.0f, "%.1f st");
        ImGui::SameLine();
        if (ImGui::SmallButton("0##slide")) note.slide = 0.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("-12##slide")) note.slide = -12.0f;  // Slide up from octave below
        ImGui::SameLine();
        if (ImGui::SmallButton("+12##slide")) note.slide = 12.0f;   // Slide down from octave above

        /*
         * Tie, and what it does to the slide above.
         *
         * These two controls sit together because together they are tone
         * portamento, and apart they are two different things. Untied, the
         * slide is a scoop into the note. Tied, it is the rate at which the
         * voice glides from the previous note to this one - and with the
         * slide at zero, a tie is a plain legato that changes pitch without
         * re-striking.
         */
        if (ImGui::Checkbox("Tie to the note before", &note.tie)) {
            // Nothing to recompute: the sequencer reads it per note.
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Keep the voice that is already sounding instead of starting\n"
                "a new one. The envelope carries on, so there is no re-strike.\n\n"
                "With a slide above, this is tone portamento: the pitch glides\n"
                "from the previous note at that many semitones per second.\n"
                "With no slide, the pitch simply changes.\n\n"
                "A tie with nothing sounding is just a note.");
        }

        if (note.tie) {
            ImGui::SameLine();
            if (note.slide != 0.0f) {
                ImGui::TextDisabled("(glides at %.1f semitones/sec)",
                                    std::fabs(note.slide));
            } else {
                ImGui::TextDisabled("(legato - set a slide to glide)");
            }
        }

        ImGui::Separator();

        // ------------------------------------------------------------------
        // Timing effects
        //
        // These four fields were declared on Note and written to the .ctp
        // file all along, but nothing could edit them and nothing played
        // them. They are the classic tracker commands - EDxx note delay,
        // ECxx note cut, Qxy retrigger - and they are how chiptune gets
        // flams and stutters without spending another channel.
        // ------------------------------------------------------------------
        ImGui::Text("Chance this note plays:");
        ImGui::SetNextItemWidth(120);
        {
            float percent = note.probability * 100.0f;
            if (ImGui::SliderFloat("##Probability", &percent, 0.0f, 100.0f, "%.0f%%")) {
                note.probability = std::clamp(percent / 100.0f, 0.0f, 1.0f);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Rolled fresh on every pass through the loop.\n"
                              "A sixteen-step loop repeats a lot; this is the\n"
                              "cheapest way to stop it sounding like one.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("100%##prob")) note.probability = 1.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("50%##prob")) note.probability = 0.5f;

        ImGui::Separator();
        ImGui::TextDisabled("Timing");

        ImGui::Text("Delay (push the note later):");
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("##NoteDelay", &note.noteDelay, 0.0f, 1.0f, "%.3f beats");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Offsets this note alone. Two notes on the same\n"
                              "beat with different delays give you a flam.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("0##ndel")) note.noteDelay = 0.0f;

        ImGui::Text("Cut (end early):");
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("##NoteCut", &note.noteCut, 0.0f, 4.0f, "%.3f beats");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Silences the note this far in, ignoring its\n"
                              "length. 0 means no cut.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("0##ncut")) note.noteCut = 0.0f;

        ImGui::Text("Retrigger (stutter):");
        ImGui::SetNextItemWidth(90);
        ImGui::SliderInt("##RetrigCount", &note.retriggerCount, 0, 8, "%d hits");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("##RetrigSpeed", &note.retriggerSpeed,
                           0.0156f, 0.5f, "%.3f beats");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Chops the note into repeated hits. Hits never\n"
                              "sound past the note's own end.");
        }

        ImGui::Text("Echo (decaying repeats):");
        ImGui::SetNextItemWidth(90);
        ImGui::SliderInt("##EchoRepeats", &note.echoRepeats, 0, 4, "%d taps");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("##EchoDelay", &note.echoDelay, 0.0156f, 2.0f, "%.3f beats");
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Decay##echo", &note.echoDecay, 0.05f, 0.95f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Repeats of this note, each quieter than the last.\n"
                              "Costs voices on the same channel.");
        }

        ImGui::Separator();

        // ------------------------------------------------------------------
        // Pitch sweep
        //
        // Fully implemented in the audio path since the NES sweep unit went
        // in, and never given a control. It is the laser/zap sound.
        // ------------------------------------------------------------------
        ImGui::TextDisabled("Pitch Sweep");
        int sweepMode = static_cast<int>(note.sweepDirection);
        ImGui::SetNextItemWidth(120);
        if (ImGui::Combo("##SweepDir", &sweepMode, "Off\0Up\0Down\0")) {
            note.sweepDirection = static_cast<SweepDirection>(sweepMode);
        }
        if (note.sweepDirection != SweepDirection::None) {
            ImGui::SetNextItemWidth(120);
            ImGui::SliderFloat("Speed##sweep", &note.sweepSpeed, 0.1f, 20.0f, "%.1f st/beat");
            ImGui::SetNextItemWidth(120);
            ImGui::SliderFloat("Range##sweep", &note.sweepAmount, 1.0f, 48.0f, "%.0f st");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Zap")) {
            note.sweepDirection = SweepDirection::Down;
            note.sweepSpeed = 12.0f;
            note.sweepAmount = 24.0f;
        }

        ImGui::Separator();
        // Reset all effects
        if (ImGui::Button("Reset All Effects")) {
            const Note defaults;
            note.vibrato = defaults.vibrato;
            note.slide = defaults.slide;
            note.arpeggio = defaults.arpeggio;
            note.noteDelay = defaults.noteDelay;
            note.noteCut = defaults.noteCut;
            note.retriggerCount = defaults.retriggerCount;
            note.retriggerSpeed = defaults.retriggerSpeed;
            note.echoRepeats = defaults.echoRepeats;
            note.echoDelay = defaults.echoDelay;
            note.echoDecay = defaults.echoDecay;
            note.sweepDirection = defaults.sweepDirection;
            note.sweepSpeed = defaults.sweepSpeed;
            note.sweepAmount = defaults.sweepAmount;
            note.probability = defaults.probability;
        }
    }

    ImGui::Separator();

    // Actions
    if (ImGui::Button("Duplicate Note")) {
        Note newNote = note;
        newNote.startTime += note.duration;  // Place after current note
        pattern.notes.push_back(newNote);
        ui.selectedNoteIndex = static_cast<int>(pattern.notes.size()) - 1;

        // Auto-extend pattern length if note goes past current end
        float noteEnd = newNote.startTime + newNote.duration;
        if (noteEnd > pattern.length) {
            pattern.length = static_cast<int>(std::ceil(noteEnd / project.beatsPerMeasure)) * project.beatsPerMeasure;
        }
    }

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Delete Note")) {
        pattern.notes.erase(pattern.notes.begin() + ui.selectedNoteIndex);
        ui.selectedNoteIndex = -1;
    }
    ImGui::PopStyleColor();

    ImGui::End();
}

// ============================================================================
// View Tabs
// ============================================================================
// Panels a genre puts in front of you. Applied once, when the genre is
// chosen - not every frame, or the View menu could never override it.
//
// Everything deliberately does nothing here. It means "no opinion", so
// switching back to it leaves whatever you had open still open, rather than
// closing panels you asked for.
inline void ApplyGenrePanels(UIState& ui) {
    if (ui.genre == Genre::Everything) return;

    const GenreProfile& profile = genreProfile(ui.genre);
    ui.showMacroEditor = profile.macroEditor;
    ui.showWavetableEditor = profile.wavetableEditor;
    ui.showSpectrumAnalyzer = profile.spectrumAnalyzer;
    ui.showAutomation = profile.automation;
}

// The next-step hint, drawn right-aligned in the main menu bar.
//
// It lived in the Views panel first, and was clipped straight out of sight:
// that panel is docked into a 16% strip and already carries five view
// buttons and the focus row. The menu bar is always visible and costs no
// layout space, which is where a status line belongs.
inline void DrawNextStepHint(const Project& project, UIState& ui) {
    if (!ui.showNextStep) return;

    const NextStep step = suggestNextStep(project);
    if (!step.valid) return;

    char label[160];
    snprintf(label, sizeof(label), "Next: %s", step.headline);

    const float textWidth = ImGui::CalcTextSize(label).x;
    const float dismissWidth = ImGui::CalcTextSize("(?)  x").x + 24.0f;
    const float target = ImGui::GetWindowWidth() - textWidth - dismissWidth - 16.0f;

    // On a narrow window the menus themselves come first; a hint that
    // overlapped them would be worse than one that is absent.
    if (target <= ImGui::GetCursorPosX() + 8.0f) return;

    ImGui::SameLine(target);
    ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.00f, 1.0f), "%s", label);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", step.detail);

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", step.detail);

    ImGui::SameLine();
    if (ImGui::SmallButton("x##nextstep")) ui.showNextStep = false;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hide these suggestions.\nView > Next Step Hints "
                          "brings them back.");
    }
}

inline void DrawViewTabs(Project& project, UIState& ui) {
    // Set initial window position on first use (top right)
    ImGui::SetNextWindowPos(ImVec2(780, 35), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 80), ImGuiCond_FirstUseEver);
    ImGui::Begin("Views", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Piano Roll", ImVec2(100, 30))) {
        ui.currentView = ViewMode::PianoRoll;
    }
    ImGui::SameLine();
    if (ImGui::Button("Tracker", ImVec2(100, 30))) {
        ui.currentView = ViewMode::Tracker;
    }
    ImGui::SameLine();
    if (ImGui::Button("Arrange", ImVec2(100, 30))) {
        ui.currentView = ViewMode::Arrangement;
    }
    ImGui::SameLine();
    if (ImGui::Button("Mixer", ImVec2(100, 30))) {
        ui.currentView = ViewMode::Mixer;
    }

    // ------------------------------------------------------------------
    // Genre focus
    //
    // The workspace decides what you are doing; the genre decides what is
    // worth having in front of you while you do it. It hides nothing - the
    // View menu still lists every panel and the palette keeps a switch that
    // brings all of it straight back.
    // ------------------------------------------------------------------
    ImGui::Separator();
    ImGui::TextUnformatted("Focus:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##genrefocus", genreName(ui.genre))) {
        for (int i = 0; i < static_cast<int>(Genre::Count); ++i) {
            const Genre candidate = static_cast<Genre>(i);
            if (ImGui::Selectable(genreName(candidate), candidate == ui.genre)) {
                ui.genre = candidate;
                ui.paletteShowEverything = false;
                ApplyGenrePanels(ui);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", genreProfile(candidate).blurb);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Puts the tools for one kind of music in front of you.\n"
                          "Nothing is removed - every panel stays in the View menu,\n"
                          "and the palette has a switch to show all of it again.");
    }

    if (ui.genre != Genre::Everything) {
        ImGui::SameLine();
        if (ImGui::Button("Apply Defaults")) {
            const GenreProfile& profile = genreProfile(ui.genre);
            project.bpm = profile.bpm;
            project.swing = profile.swing;
            g_ToolsScaleRoot = profile.scaleRoot;
            g_ToolsScaleType = profile.scaleType;
        }
        if (ImGui::IsItemHovered()) {
            const GenreProfile& profile = genreProfile(ui.genre);
            ImGui::SetTooltip("Set the tempo to %.0f, swing to %.0f%% and the key\n"
                              "to %s %s.\n\n"
                              "This one changes your project, which is why it is a\n"
                              "button and not part of choosing the focus.",
                              profile.bpm, profile.swing * 100.0f,
                              noteName(profile.scaleRoot), scaleName(profile.scaleType));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Pad", ImVec2(100, 30))) {
        ui.currentView = ViewMode::PadController;
    }

    // Voice Mode belongs beside the other views, not only behind a shortcut
    // and a palette entry - a mode nobody can find is a mode nobody uses.
    ImGui::SameLine();
    if (ImGui::Button("Voice", ImVec2(100, 30))) {
        ui.currentView = ViewMode::Voice;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Build a song by singing it, a part at a time.");
    }

    ImGui::End();
}

// ============================================================================
// Sound Palette - Visual icons for each oscillator type
// ============================================================================
inline void DrawWaveformIcon(ImDrawList* drawList, ImVec2 pos, ImVec2 size, OscillatorType type, ImU32 color) {
    float cx = pos.x + size.x * 0.5f;
    float cy = pos.y + size.y * 0.5f;
    float hw = size.x * 0.4f;
    float hh = size.y * 0.35f;

    switch (type) {
        case OscillatorType::Pulse: {
            // Square wave icon
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh), ImVec2(pos.x + 4, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(pos.x + 4, cy - hh), ImVec2(cx, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh), ImVec2(cx, cy + hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh), ImVec2(pos.x + size.x - 4, cy + hh), color, 2.0f);
            break;
        }
        case OscillatorType::Triangle: {
            // Triangle wave icon
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(cx - hw/2, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx - hw/2, cy - hh), ImVec2(cx + hw/2, cy + hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx + hw/2, cy + hh), ImVec2(pos.x + size.x - 4, cy), color, 2.0f);
            break;
        }
        case OscillatorType::Sawtooth: {
            // Sawtooth wave icon
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh), ImVec2(cx, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh), ImVec2(cx, cy + hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh), ImVec2(pos.x + size.x - 4, cy - hh), color, 2.0f);
            break;
        }
        case OscillatorType::Sine: {
            // Sine wave icon (approximated with line segments)
            ImVec2 prev(pos.x + 4, cy);
            for (int i = 1; i <= 16; ++i) {
                float t = static_cast<float>(i) / 16.0f;
                float x = pos.x + 4 + t * (size.x - 8);
                float y = cy - std::sin(t * 6.28f) * hh;
                drawList->AddLine(prev, ImVec2(x, y), color, 2.0f);
                prev = ImVec2(x, y);
            }
            break;
        }
        case OscillatorType::Noise: {
            // Noise icon (random lines)
            float x = pos.x + 4;
            float step = (size.x - 8) / 8.0f;
            for (int i = 0; i < 8; ++i) {
                float y1 = cy + ((i * 7 + 3) % 5 - 2) * hh * 0.5f;
                float y2 = cy + (((i+1) * 7 + 3) % 5 - 2) * hh * 0.5f;
                drawList->AddLine(ImVec2(x, y1), ImVec2(x + step, y2), color, 2.0f);
                x += step;
            }
            break;
        }
        case OscillatorType::Supersaw: {
            // Supersaw icon - multiple stacked saws
            float sw = (size.x - 8) / 3.0f;
            for (int i = 0; i < 3; ++i) {
                float xOffset = pos.x + 4 + i * sw;
                float yOffset = (i - 1) * hh * 0.15f;  // Slight vertical offset for depth
                float thickness = (i == 1) ? 2.5f : 1.5f;  // Center saw is thicker
                drawList->AddLine(ImVec2(xOffset, cy + hh * 0.7f + yOffset),
                                  ImVec2(xOffset + sw - 2, cy - hh * 0.7f + yOffset), color, thickness);
                drawList->AddLine(ImVec2(xOffset + sw - 2, cy - hh * 0.7f + yOffset),
                                  ImVec2(xOffset + sw - 2, cy + hh * 0.7f + yOffset), color, thickness);
            }
            break;
        }
        case OscillatorType::Custom: {
            // Custom wave icon (wavy line)
            drawList->AddText(ImVec2(pos.x + size.x/2 - 8, pos.y + size.y/2 - 6), color, "~");
            break;
        }

        // The configurable engines. Short glyphs rather than waveform
        // drawings, because none of them HAS one fixed waveform - that is
        // the whole point of each. Without these they fell to the default
        // case and drew nothing at all, which in a palette of icons reads
        // as an empty button.
        case OscillatorType::FMSynth: {
            drawList->AddText(ImVec2(pos.x + size.x / 2 - 9, pos.y + size.y / 2 - 7),
                              color, "FM");
            break;
        }
        case OscillatorType::Sampler: {
            drawList->AddText(ImVec2(pos.x + size.x / 2 - 12, pos.y + size.y / 2 - 7),
                              color, "SMP");
            break;
        }
        case OscillatorType::Granular: {
            // A scatter of dots, which is what a grain cloud looks like.
            for (int i = 0; i < 9; ++i) {
                const float fx = pos.x + 8.0f + float((i * 7) % 34);
                const float fy = pos.y + 8.0f + float((i * 11) % 18);
                drawList->AddCircleFilled(ImVec2(fx, fy), 1.6f, color);
            }
            break;
        }
        case OscillatorType::DrumModel: {
            drawList->AddCircle(ImVec2(pos.x + size.x / 2, pos.y + size.y / 2),
                                9.0f, color, 0, 1.6f);
            drawList->AddLine(ImVec2(pos.x + size.x / 2 - 9, pos.y + size.y / 2),
                              ImVec2(pos.x + size.x / 2 + 9, pos.y + size.y / 2),
                              color, 1.2f);
            break;
        }

        // ================================================================
        // SYNTHS
        // ================================================================
        case OscillatorType::SynthLead: {
            // Lead - double saw icon
            float sw = hw * 0.4f;
            drawList->AddLine(ImVec2(cx - sw, cy + hh*0.6f), ImVec2(cx, cy - hh*0.6f), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.6f), ImVec2(cx, cy + hh*0.6f), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh*0.6f), ImVec2(cx + sw, cy - hh*0.6f), color, 2.0f);
            // Second saw slightly offset
            drawList->AddLine(ImVec2(cx - sw + 3, cy + hh*0.4f), ImVec2(cx + 3, cy - hh*0.4f), color, 1.5f);
            break;
        }
        case OscillatorType::SynthPad: {
            // Pad - soft waves
            ImVec2 prev(pos.x + 4, cy);
            for (int i = 1; i <= 12; ++i) {
                float t = static_cast<float>(i) / 12.0f;
                float x = pos.x + 4 + t * (size.x - 8);
                float y = cy - std::sin(t * 6.28f * 0.5f) * hh * 0.6f;
                drawList->AddLine(prev, ImVec2(x, y), color, 2.5f);
                prev = ImVec2(x, y);
            }
            break;
        }
        case OscillatorType::SynthBass: {
            // Bass - thick low wave
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(cx - hw*0.3f, cy + hh*0.8f), color, 3.0f);
            drawList->AddLine(ImVec2(cx - hw*0.3f, cy + hh*0.8f), ImVec2(cx + hw*0.3f, cy - hh*0.4f), color, 3.0f);
            drawList->AddLine(ImVec2(cx + hw*0.3f, cy - hh*0.4f), ImVec2(pos.x + size.x - 4, cy), color, 3.0f);
            break;
        }
        case OscillatorType::SynthPluck: {
            // Pluck - sharp attack then decay
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(pos.x + 8, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(pos.x + 8, cy - hh), ImVec2(pos.x + size.x - 4, cy + hh*0.3f), color, 2.0f);
            break;
        }
        case OscillatorType::SynthArp: {
            // Arp - staircase pattern
            float step = (size.x - 8) / 4.0f;
            for (int i = 0; i < 4; ++i) {
                float x = pos.x + 4 + i * step;
                float y = cy - hh + (i % 2) * hh * 1.5f;
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + step - 2, y + 4), color);
            }
            break;
        }
        case OscillatorType::SynthOrgan: {
            // Organ - drawbar-like lines
            for (int i = 0; i < 5; ++i) {
                float x = pos.x + 6 + i * (size.x - 12) / 5.0f;
                float h = hh * (0.5f + (4 - i) * 0.15f);
                drawList->AddLine(ImVec2(x, cy + h), ImVec2(x, cy - h), color, 2.5f);
            }
            break;
        }
        case OscillatorType::SynthStrings: {
            // Strings - wavy lines
            for (int j = -1; j <= 1; ++j) {
                ImVec2 prev(pos.x + 4, cy + j * hh * 0.4f);
                for (int i = 1; i <= 8; ++i) {
                    float t = static_cast<float>(i) / 8.0f;
                    float x = pos.x + 4 + t * (size.x - 8);
                    float y = cy + j * hh * 0.4f - std::sin(t * 6.28f + j) * hh * 0.2f;
                    drawList->AddLine(prev, ImVec2(x, y), color, 1.5f);
                    prev = ImVec2(x, y);
                }
            }
            break;
        }
        case OscillatorType::SynthBrass: {
            // Brass - bold angular
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh*0.5f), ImVec2(cx - hw*0.2f, cy - hh*0.8f), color, 2.5f);
            drawList->AddLine(ImVec2(cx - hw*0.2f, cy - hh*0.8f), ImVec2(cx + hw*0.2f, cy + hh*0.8f), color, 2.5f);
            drawList->AddLine(ImVec2(cx + hw*0.2f, cy + hh*0.8f), ImVec2(pos.x + size.x - 4, cy - hh*0.5f), color, 2.5f);
            break;
        }
        case OscillatorType::SynthChip: {
            // Chip - narrow pulse
            float pw = hw * 0.2f;
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh), ImVec2(pos.x + 4, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(pos.x + 4, cy - hh), ImVec2(cx - pw, cy - hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx - pw, cy - hh), ImVec2(cx - pw, cy + hh), color, 2.0f);
            drawList->AddLine(ImVec2(cx - pw, cy + hh), ImVec2(pos.x + size.x - 4, cy + hh), color, 2.0f);
            break;
        }
        case OscillatorType::SynthBell: {
            // Bell - star/sparkle shape
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.5f, color, 8, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.8f), ImVec2(cx, cy + hh*0.8f), color, 1.5f);
            drawList->AddLine(ImVec2(cx - hh*0.8f, cy), ImVec2(cx + hh*0.8f, cy), color, 1.5f);
            break;
        }

        // ================================================================
        // SYNTHWAVE PRESETS
        // ================================================================
        case OscillatorType::SynthwaveLead: {
            // SW Lead - PWM pulse with modulation wave
            float pw = hw * 0.3f;
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh*0.5f), ImVec2(pos.x + 4, cy - hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(pos.x + 4, cy - hh*0.5f), ImVec2(cx - pw, cy - hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(cx - pw, cy - hh*0.5f), ImVec2(cx - pw, cy + hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(cx - pw, cy + hh*0.5f), ImVec2(cx + pw, cy + hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(cx + pw, cy + hh*0.5f), ImVec2(cx + pw, cy - hh*0.3f), color, 2.0f);
            // Modulation wave on top
            for (int i = 0; i < 3; ++i) {
                float x = pos.x + 8 + i * 10;
                drawList->AddCircle(ImVec2(x, cy - hh*0.8f), 2.0f, color, 6, 1.5f);
            }
            break;
        }
        case OscillatorType::SynthwaveBass: {
            // SW Bass - thick sub wave with harmonics
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(cx - hw*0.4f, cy + hh*0.9f), color, 4.0f);
            drawList->AddLine(ImVec2(cx - hw*0.4f, cy + hh*0.9f), ImVec2(cx, cy - hh*0.3f), color, 4.0f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.3f), ImVec2(cx + hw*0.4f, cy + hh*0.5f), color, 3.0f);
            drawList->AddLine(ImVec2(cx + hw*0.4f, cy + hh*0.5f), ImVec2(pos.x + size.x - 4, cy), color, 2.0f);
            break;
        }
        case OscillatorType::SynthwavePad: {
            // SW Pad - multiple soft overlapping waves (supersaw)
            for (int j = -2; j <= 2; ++j) {
                float offset = j * 3.0f;
                float alpha = 1.0f - std::abs(j) * 0.2f;
                ImU32 layerColor = IM_COL32(
                    (color >> 0) & 0xFF,
                    (color >> 8) & 0xFF,
                    (color >> 16) & 0xFF,
                    static_cast<int>(((color >> 24) & 0xFF) * alpha));
                ImVec2 prev(pos.x + 4, cy + offset);
                for (int i = 1; i <= 8; ++i) {
                    float t = static_cast<float>(i) / 8.0f;
                    float x = pos.x + 4 + t * (size.x - 8);
                    float y = cy + offset + std::sin(t * 6.28f * 1.5f + j * 0.5f) * hh * 0.4f;
                    drawList->AddLine(prev, ImVec2(x, y), layerColor, 1.5f);
                    prev = ImVec2(x, y);
                }
            }
            break;
        }
        case OscillatorType::SynthwaveArp: {
            // SW Arp - fast staircase with glow dots
            float step = (size.x - 8) / 5.0f;
            for (int i = 0; i < 5; ++i) {
                float x = pos.x + 4 + i * step;
                float y = cy - hh + (i % 3) * hh * 0.8f;
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + step - 3, y + 3), color);
                drawList->AddCircleFilled(ImVec2(x + 2, y + 1.5f), 2.5f, color);
            }
            break;
        }
        case OscillatorType::SynthwaveChord: {
            // SW Chord - stacked rectangles (polyphonic)
            float rh = hh * 0.3f;
            drawList->AddRectFilled(ImVec2(cx - hw*0.6f, cy - rh*2.5f), ImVec2(cx + hw*0.6f, cy - rh*1.2f), color);
            drawList->AddRectFilled(ImVec2(cx - hw*0.5f, cy - rh*0.5f), ImVec2(cx + hw*0.5f, cy + rh*0.5f), color);
            drawList->AddRectFilled(ImVec2(cx - hw*0.6f, cy + rh*1.2f), ImVec2(cx + hw*0.6f, cy + rh*2.5f), color);
            break;
        }
        case OscillatorType::SynthwaveFM: {
            // SW FM - modulator carrier visualization
            drawList->AddCircle(ImVec2(cx - hw*0.3f, cy), hh * 0.4f, color, 8, 2.0f);  // Modulator
            drawList->AddCircleFilled(ImVec2(cx + hw*0.3f, cy), hh * 0.5f, color);     // Carrier
            drawList->AddLine(ImVec2(cx - hw*0.3f + hh*0.4f, cy), ImVec2(cx + hw*0.3f - hh*0.5f, cy), color, 2.0f);  // Connection
            // FM sidebands
            drawList->AddLine(ImVec2(cx + hw*0.3f, cy - hh*0.7f), ImVec2(cx + hw*0.3f, cy - hh*0.5f), color, 1.5f);
            drawList->AddLine(ImVec2(cx + hw*0.3f, cy + hh*0.5f), ImVec2(cx + hw*0.3f, cy + hh*0.7f), color, 1.5f);
            break;
        }

        // ================================================================
        // KICKS
        // ================================================================
        case OscillatorType::Kick: {
            // Kick drum icon - large filled circle
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.8f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.8f, IM_COL32(255, 255, 255, 100), 12, 2.0f);
            break;
        }
        case OscillatorType::Kick808: {
            // 808 Kick - double ring (deeper)
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.9f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.6f, IM_COL32(0, 0, 0, 150), 12, 2.0f);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.9f, IM_COL32(255, 255, 255, 100), 12, 2.0f);
            break;
        }
        case OscillatorType::KickHard: {
            // Hard Kick - circle with impact lines
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.7f, color);
            drawList->AddLine(ImVec2(cx - hh, cy), ImVec2(cx - hh*0.8f, cy), color, 2.0f);
            drawList->AddLine(ImVec2(cx + hh*0.8f, cy), ImVec2(cx + hh, cy), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh), ImVec2(cx, cy - hh*0.8f), color, 2.0f);
            break;
        }
        case OscillatorType::KickSoft: {
            // Soft Kick - outlined circle with gradient feel
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.8f, color, 12, 3.0f);
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.4f, color);
            break;
        }

        // ================================================================
        // SNARES
        // ================================================================
        case OscillatorType::Snare: {
            // Snare drum icon - circle with X
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.7f, color, 12, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.5f, cy - hh*0.5f), ImVec2(cx + hh*0.5f, cy + hh*0.5f), color, 2.0f);
            drawList->AddLine(ImVec2(cx + hh*0.5f, cy - hh*0.5f), ImVec2(cx - hh*0.5f, cy + hh*0.5f), color, 2.0f);
            break;
        }
        case OscillatorType::Snare808: {
            // 808 Snare - circle with horizontal lines (wires)
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.7f, color, 12, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.5f, cy - hh*0.2f), ImVec2(cx + hh*0.5f, cy - hh*0.2f), color, 1.5f);
            drawList->AddLine(ImVec2(cx - hh*0.5f, cy + hh*0.2f), ImVec2(cx + hh*0.5f, cy + hh*0.2f), color, 1.5f);
            break;
        }
        case OscillatorType::SnareRim: {
            // Rimshot - small circle with outer ring
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.3f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.7f, color, 12, 2.0f);
            break;
        }
        case OscillatorType::Clap: {
            // Clap - multiple small circles (hands)
            drawList->AddCircleFilled(ImVec2(cx - hh*0.4f, cy - hh*0.2f), hh * 0.25f, color);
            drawList->AddCircleFilled(ImVec2(cx + hh*0.4f, cy - hh*0.2f), hh * 0.25f, color);
            drawList->AddCircleFilled(ImVec2(cx - hh*0.2f, cy + hh*0.3f), hh * 0.25f, color);
            drawList->AddCircleFilled(ImVec2(cx + hh*0.2f, cy + hh*0.3f), hh * 0.25f, color);
            break;
        }

        // ================================================================
        // HI-HATS
        // ================================================================
        case OscillatorType::HiHat: {
            // Closed Hi-hat - two overlapping circles
            drawList->AddCircle(ImVec2(cx - 3, cy), hh * 0.5f, color, 8, 2.0f);
            drawList->AddCircle(ImVec2(cx + 3, cy), hh * 0.5f, color, 8, 2.0f);
            break;
        }
        case OscillatorType::HiHatOpen: {
            // Open Hi-hat - two circles with gap
            drawList->AddCircle(ImVec2(cx - 4, cy - 2), hh * 0.45f, color, 8, 2.0f);
            drawList->AddCircle(ImVec2(cx + 4, cy + 2), hh * 0.45f, color, 8, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.6f, cy + hh*0.5f), ImVec2(cx + hh*0.6f, cy + hh*0.5f), color, 1.5f);
            break;
        }
        case OscillatorType::HiHatPedal: {
            // Pedal Hi-hat - single circle with pedal line
            drawList->AddCircle(ImVec2(cx, cy - hh*0.2f), hh * 0.4f, color, 8, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh*0.2f), ImVec2(cx, cy + hh*0.7f), color, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.3f, cy + hh*0.7f), ImVec2(cx + hh*0.3f, cy + hh*0.7f), color, 2.0f);
            break;
        }

        // ================================================================
        // TOMS
        // ================================================================
        case OscillatorType::Tom: {
            // Mid Tom - oval with center line
            drawList->AddEllipse(ImVec2(cx, cy), ImVec2(hw * 0.5f, hh * 0.5f), color, 0.0f, 12, 2.0f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.3f), ImVec2(cx, cy + hh*0.3f), color, 2.0f);
            break;
        }
        case OscillatorType::TomLow: {
            // Floor Tom - larger oval, lower
            drawList->AddEllipse(ImVec2(cx, cy + 2), ImVec2(hw * 0.6f, hh * 0.6f), color, 0.0f, 12, 2.0f);
            drawList->AddLine(ImVec2(cx - hh*0.3f, cy + 2), ImVec2(cx + hh*0.3f, cy + 2), color, 2.0f);
            break;
        }
        case OscillatorType::TomHigh: {
            // High Tom - smaller oval, higher
            drawList->AddEllipse(ImVec2(cx, cy - 2), ImVec2(hw * 0.4f, hh * 0.4f), color, 0.0f, 12, 2.0f);
            drawList->AddCircleFilled(ImVec2(cx, cy - 2), hh * 0.15f, color);
            break;
        }

        // ================================================================
        // CYMBALS
        // ================================================================
        case OscillatorType::Crash: {
            // Crash cymbal - large triangle/splash
            drawList->AddTriangle(
                ImVec2(cx, cy - hh*0.8f),
                ImVec2(cx - hw*0.7f, cy + hh*0.5f),
                ImVec2(cx + hw*0.7f, cy + hh*0.5f),
                color, 2.0f);
            drawList->AddLine(ImVec2(cx - hw*0.5f, cy - hh*0.3f), ImVec2(cx + hw*0.5f, cy + hh*0.2f), color, 1.5f);
            break;
        }
        case OscillatorType::Ride: {
            // Ride cymbal - circle with center dot
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.7f, color, 12, 2.0f);
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.2f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.45f, color, 8, 1.0f);
            break;
        }

        // ================================================================
        // PERCUSSION
        // ================================================================
        case OscillatorType::Cowbell: {
            // Cowbell - trapezoid shape
            drawList->AddQuadFilled(
                ImVec2(cx - hw*0.3f, cy - hh*0.6f),
                ImVec2(cx + hw*0.3f, cy - hh*0.6f),
                ImVec2(cx + hw*0.5f, cy + hh*0.6f),
                ImVec2(cx - hw*0.5f, cy + hh*0.6f),
                color);
            break;
        }
        case OscillatorType::Clave: {
            // Clave - two crossed sticks
            drawList->AddLine(ImVec2(cx - hw*0.6f, cy - hh*0.4f), ImVec2(cx + hw*0.2f, cy + hh*0.6f), color, 3.0f);
            drawList->AddLine(ImVec2(cx - hw*0.2f, cy + hh*0.6f), ImVec2(cx + hw*0.6f, cy - hh*0.4f), color, 3.0f);
            break;
        }
        case OscillatorType::Conga: {
            // Conga - tall oval drum
            drawList->AddEllipse(ImVec2(cx, cy - hh*0.2f), ImVec2(hw * 0.35f, hh * 0.3f), color, 0.0f, 10, 2.0f);
            drawList->AddLine(ImVec2(cx - hw*0.35f, cy - hh*0.2f), ImVec2(cx - hw*0.4f, cy + hh*0.6f), color, 2.0f);
            drawList->AddLine(ImVec2(cx + hw*0.35f, cy - hh*0.2f), ImVec2(cx + hw*0.4f, cy + hh*0.6f), color, 2.0f);
            drawList->AddLine(ImVec2(cx - hw*0.4f, cy + hh*0.6f), ImVec2(cx + hw*0.4f, cy + hh*0.6f), color, 2.0f);
            break;
        }
        case OscillatorType::Maracas: {
            // Maracas - circle with handle
            drawList->AddCircleFilled(ImVec2(cx, cy - hh*0.3f), hh * 0.4f, color);
            drawList->AddLine(ImVec2(cx, cy + hh*0.1f), ImVec2(cx, cy + hh*0.7f), color, 3.0f);
            break;
        }
        case OscillatorType::Tambourine: {
            // Tambourine - circle with jingles (dots around edge)
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.6f, color, 12, 2.0f);
            for (int j = 0; j < 6; ++j) {
                float angle = j * 3.14159f * 2.0f / 6.0f;
                float jx = cx + std::cos(angle) * hh * 0.6f;
                float jy = cy + std::sin(angle) * hh * 0.6f;
                drawList->AddCircleFilled(ImVec2(jx, jy), hh * 0.12f, color);
            }
            break;
        }

        // ================================================================
        // TECHNO / ELECTRONIC PRESETS
        // ================================================================
        case OscillatorType::AcidBass: {
            // TB-303 style - resonant filter peak shape
            drawList->AddLine(ImVec2(pos.x + 4, cy + hh*0.6f), ImVec2(cx - hw*0.2f, cy - hh*0.9f), color, 2.5f);
            drawList->AddLine(ImVec2(cx - hw*0.2f, cy - hh*0.9f), ImVec2(cx + hw*0.1f, cy + hh*0.3f), color, 2.5f);
            drawList->AddLine(ImVec2(cx + hw*0.1f, cy + hh*0.3f), ImVec2(pos.x + size.x - 4, cy + hh*0.5f), color, 2.0f);
            // Resonance spike
            drawList->AddCircleFilled(ImVec2(cx - hw*0.2f, cy - hh*0.9f), 3.0f, color);
            break;
        }
        case OscillatorType::TechnoStab: {
            // Short stab - sharp attack, quick decay
            drawList->AddRectFilled(ImVec2(cx - hw*0.4f, cy - hh*0.6f), ImVec2(cx + hw*0.4f, cy + hh*0.2f), color);
            drawList->AddLine(ImVec2(cx + hw*0.4f, cy - hh*0.4f), ImVec2(cx + hw*0.8f, cy + hh*0.6f), color, 2.0f);
            break;
        }
        case OscillatorType::Hoover: {
            // Hoover - detuned saws sweeping
            for (int i = -2; i <= 2; ++i) {
                float yOff = i * hh * 0.25f;
                float thickness = (i == 0) ? 2.5f : 1.5f;
                drawList->AddLine(ImVec2(pos.x + 4, cy + yOff + hh*0.3f), ImVec2(cx, cy + yOff - hh*0.3f), color, thickness);
                drawList->AddLine(ImVec2(cx, cy + yOff - hh*0.3f), ImVec2(cx, cy + yOff + hh*0.3f), color, thickness);
            }
            break;
        }
        case OscillatorType::RaveChord: {
            // Rave chord - stacked piano keys
            float kw = hw * 0.25f;
            for (int i = 0; i < 4; ++i) {
                float x = cx - hw*0.5f + i * kw;
                drawList->AddRectFilled(ImVec2(x, cy - hh*0.5f), ImVec2(x + kw - 2, cy + hh*0.5f), color);
            }
            break;
        }
        case OscillatorType::Reese: {
            // Reese bass - two detuned saws creating movement
            ImVec2 prev1(pos.x + 4, cy);
            ImVec2 prev2(pos.x + 4, cy);
            for (int i = 1; i <= 12; ++i) {
                float t = static_cast<float>(i) / 12.0f;
                float x = pos.x + 4 + t * (size.x - 8);
                float y1 = cy + std::sin(t * 12.0f) * hh * 0.4f;
                float y2 = cy + std::sin(t * 12.0f + 0.5f) * hh * 0.4f;
                drawList->AddLine(prev1, ImVec2(x, y1), color, 2.0f);
                drawList->AddLine(prev2, ImVec2(x, y2), IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, 150), 1.5f);
                prev1 = ImVec2(x, y1);
                prev2 = ImVec2(x, y2);
            }
            break;
        }

        // ================================================================
        // HIP HOP PRESETS
        // ================================================================
        case OscillatorType::SubBass808: {
            // Deep 808 sub - very thick low sine
            drawList->AddCircleFilled(ImVec2(cx, cy), hh * 0.9f, color);
            drawList->AddCircle(ImVec2(cx, cy), hh * 0.6f, IM_COL32(0, 0, 0, 100), 12, 2.0f);
            // Sub indicator
            drawList->AddText(ImVec2(cx - 6, cy - 6), IM_COL32(0, 0, 0, 200), "S");
            break;
        }
        case OscillatorType::LoFiKeys: {
            // Lo-fi keys - dusty piano keys with vinyl crackle dots
            float kw = hw * 0.3f;
            for (int i = 0; i < 3; ++i) {
                float x = cx - hw*0.4f + i * kw;
                drawList->AddRectFilled(ImVec2(x, cy - hh*0.6f), ImVec2(x + kw - 3, cy + hh*0.6f), color);
            }
            // Dust particles
            drawList->AddCircleFilled(ImVec2(cx - hw*0.5f, cy - hh*0.3f), 2.0f, color);
            drawList->AddCircleFilled(ImVec2(cx + hw*0.6f, cy + hh*0.2f), 1.5f, color);
            break;
        }
        case OscillatorType::VinylNoise: {
            // Vinyl crackle - scattered dots
            for (int i = 0; i < 10; ++i) {
                float x = pos.x + 6 + (i * 13 + 7) % static_cast<int>(size.x - 12);
                float y = cy + ((i * 7 + 3) % 5 - 2) * hh * 0.4f;
                float r = 1.0f + (i % 3) * 0.5f;
                drawList->AddCircleFilled(ImVec2(x, y), r, color);
            }
            break;
        }
        case OscillatorType::TrapLead: {
            // Trap lead - plucky square wave
            drawList->AddLine(ImVec2(pos.x + 4, cy), ImVec2(pos.x + 6, cy - hh*0.8f), color, 2.5f);
            drawList->AddLine(ImVec2(pos.x + 6, cy - hh*0.8f), ImVec2(cx, cy - hh*0.8f), color, 2.5f);
            drawList->AddLine(ImVec2(cx, cy - hh*0.8f), ImVec2(cx, cy + hh*0.4f), color, 2.0f);
            drawList->AddLine(ImVec2(cx, cy + hh*0.4f), ImVec2(pos.x + size.x - 4, cy + hh*0.1f), color, 1.5f);
            break;
        }

        // ================================================================
        // ADDITIONAL SYNTHWAVE
        // ================================================================
        case OscillatorType::GatedPad: {
            // Gated pad - rhythmic blocks
            float bw = (size.x - 8) / 6.0f;
            for (int i = 0; i < 6; ++i) {
                float alpha = (i % 2 == 0) ? 1.0f : 0.3f;
                ImU32 blockColor = IM_COL32((color >> 0) & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, static_cast<int>(((color >> 24) & 0xFF) * alpha));
                drawList->AddRectFilled(ImVec2(pos.x + 4 + i * bw, cy - hh*0.5f), ImVec2(pos.x + 4 + (i + 1) * bw - 2, cy + hh*0.5f), blockColor);
            }
            break;
        }
        case OscillatorType::PolySynth: {
            // Poly synth - multiple layered waves
            for (int j = -1; j <= 1; ++j) {
                ImVec2 prev(pos.x + 4, cy + j * hh * 0.3f);
                for (int i = 1; i <= 10; ++i) {
                    float t = static_cast<float>(i) / 10.0f;
                    float x = pos.x + 4 + t * (size.x - 8);
                    float y = cy + j * hh * 0.3f + std::sin(t * 6.28f + j * 0.8f) * hh * 0.35f;
                    float thickness = (j == 0) ? 2.5f : 1.5f;
                    drawList->AddLine(prev, ImVec2(x, y), color, thickness);
                    prev = ImVec2(x, y);
                }
            }
            break;
        }
        case OscillatorType::SyncLead: {
            // Hard sync - zigzag with discontinuities
            float lastY = cy;
            for (int i = 0; i < 4; ++i) {
                float x1 = pos.x + 4 + i * (size.x - 8) / 4.0f;
                float x2 = x1 + (size.x - 8) / 4.0f;
                drawList->AddLine(ImVec2(x1, lastY), ImVec2(x1, cy - hh*0.7f), color, 2.0f);
                drawList->AddLine(ImVec2(x1, cy - hh*0.7f), ImVec2(x2 - 2, cy + hh*0.5f), color, 2.0f);
                lastY = cy - hh*0.3f;  // Jump point
            }
            break;
        }

        default:
            // Fallback - simple rectangle
            drawList->AddRect(ImVec2(cx - hw*0.5f, cy - hh*0.5f),
                             ImVec2(cx + hw*0.5f, cy + hh*0.5f), color, 0.0f, 0, 2.0f);
            break;
    }
}

// Helper to draw a drum item with duration variant
inline void DrawDrumVariant(ImDrawList* drawList, int oscIndex, const char* name, const char* desc,
                            float durationMult, const char* durationLabel,
                            Project& project, UIState& ui, Sequencer& seq) {
    // Fixed button width but with visual length indicator inside
    ImVec2 itemSize(90, 32);
    ImVec2 pos = ImGui::GetCursorScreenPos();

    bool isPaletteSelected = (g_SelectedPaletteItem == oscIndex && std::abs(g_SelectedDurationMult - durationMult) < 0.01f);

    // Colors based on selection state
    ImU32 bgColor = isPaletteSelected ? IM_COL32(140, 80, 80, 255) : IM_COL32(45, 40, 40, 200);
    ImU32 textColor = isPaletteSelected ? IM_COL32(255, 220, 220, 255) : IM_COL32(200, 150, 150, 255);
    ImU32 borderColor = isPaletteSelected ? IM_COL32(255, 150, 150, 255) : IM_COL32(80, 60, 60, 255);

    // Duration bar color - more vibrant
    ImU32 durationBarColor = durationMult < 0.75f ? IM_COL32(80, 200, 80, 255) :   // Short = bright green
                             durationMult > 1.5f ? IM_COL32(200, 80, 80, 255) :    // Long = bright red
                             IM_COL32(200, 200, 80, 255);                           // Normal = bright yellow

    // Draw background
    drawList->AddRectFilled(pos, ImVec2(pos.x + itemSize.x, pos.y + itemSize.y), bgColor, 4.0f);
    drawList->AddRect(pos, ImVec2(pos.x + itemSize.x, pos.y + itemSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 2.5f : 1.0f);

    // Draw duration label at top
    const char* shortLabel = durationMult < 0.75f ? "SHORT" : durationMult > 1.5f ? "LONG" : "NORMAL";
    ImVec2 textSize = ImGui::CalcTextSize(shortLabel);
    float textX = pos.x + (itemSize.x - textSize.x) * 0.5f;
    drawList->AddText(ImVec2(textX, pos.y + 3), textColor, shortLabel);

    // Draw visual length bar at bottom - this shows the relative duration clearly
    float barMaxWidth = itemSize.x - 8.0f;
    float barWidth = barMaxWidth * (durationMult / 2.0f);  // 0.5x = 25%, 1.0x = 50%, 2.0x = 100%
    barWidth = std::max(barWidth, 10.0f);  // Minimum visible width

    float barHeight = 8.0f;
    float barY = pos.y + itemSize.y - barHeight - 4.0f;
    float barX = pos.x + 4.0f;

    // Draw bar background (gray)
    drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barMaxWidth, barY + barHeight),
                           IM_COL32(30, 30, 30, 200), 3.0f);

    // Draw actual duration bar
    drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight),
                           durationBarColor, 3.0f);

    // Unique ID using both oscillator and duration
    char buttonId[64];
    snprintf(buttonId, sizeof(buttonId), "drum_%d_%.1f", oscIndex, durationMult);

    // Invisible button for interaction
    ImGui::InvisibleButton(buttonId, itemSize);

    if (ImGui::IsItemClicked()) {
        if (isPaletteSelected) {
            g_SelectedPaletteItem = -1;
            g_SelectedDurationMult = 1.0f;
        } else {
            g_SelectedPaletteItem = oscIndex;
            g_SelectedDurationMult = durationMult;
            g_SelectedChordIndex = -1;  // Clear chord selection when selecting a drum
            ui.pianoRollMode = PianoRollMode::Draw;
            project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(oscIndex);
            seq.updateChannelConfigs();
        }
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%s (%s)", name, shortLabel);
        ImGui::TextDisabled("%s", desc);
        ImGui::TextDisabled("Duration: %.1fx (scales with BPM)", durationMult);
        if (isPaletteSelected) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "SELECTED");
        }
        ImGui::EndTooltip();
    }
}

// Helper to draw a drum category with expandable variations
// ============================================================================
// Category-tinted headers
//
// The sound palette colour-codes its groups - drums, oscillators, synths,
// chords - which genuinely helps you find things. It used to do that with
// hardcoded colours, so the palette ignored the theme completely: maroon and
// purple headers sitting inside the Game Boy's four-shade green.
//
// The category hue is kept, but only as a tint over the theme's own Header
// colour. Groups stay distinguishable; the panel stays on-theme.
// ============================================================================
inline void PushCategoryHeaderStyle(const ImVec4& tint) {
    const ImVec4* colors = ImGui::GetStyle().Colors;

    auto blend = [&](ImGuiCol slot, float amount) {
        const ImVec4 base = colors[slot];
        return ImVec4(base.x + (tint.x - base.x) * amount,
                      base.y + (tint.y - base.y) * amount,
                      base.z + (tint.z - base.z) * amount,
                      base.w);
    };

    // Enough tint to tell groups apart, not enough to leave the palette.
    ImGui::PushStyleColor(ImGuiCol_Header, blend(ImGuiCol_Header, 0.38f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, blend(ImGuiCol_HeaderHovered, 0.32f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, blend(ImGuiCol_HeaderActive, 0.26f));
}

inline void DrawDrumCategory(const char* categoryName, bool& expanded,
                             const int* oscIndices, const char** names, const char** descs, int count,
                             Project& project, UIState& ui, Sequencer& seq) {
    // Held back by the current genre. Nothing is removed - the palette's
    // "show everything" switch brings it straight back, and says how much
    // is being held back in the first place.
    if (!ui.paletteShowEverything &&
        !genreShowsDrumCategory(ui.genre, categoryName)) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    PushCategoryHeaderStyle(ImVec4(1.00f, 0.67f, 0.67f, 1.0f));

    if (ImGui::CollapsingHeader(categoryName, expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        expanded = true;
        ImGui::Indent(10.0f);

        for (int i = 0; i < count; ++i) {
            int oscIdx = oscIndices[i];
            ImGui::PushID(oscIdx * 100);

            // Show drum name as sub-header
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.7f, 1.0f), "%s", names[i]);

            // Duration variations in a row
            DrawDrumVariant(drawList, oscIdx, names[i], descs[i], 0.5f, "(Short)", project, ui, seq);
            ImGui::SameLine();
            DrawDrumVariant(drawList, oscIdx, names[i], descs[i], 1.0f, "(Normal)", project, ui, seq);
            ImGui::SameLine();
            DrawDrumVariant(drawList, oscIdx, names[i], descs[i], 2.0f, "(Long)", project, ui, seq);

            ImGui::PopID();
        }

        ImGui::Unindent(10.0f);
    } else {
        expanded = false;
    }

    ImGui::PopStyleColor(3);
}

inline void DrawSoundPalette(Project& project, UIState& ui, Sequencer& seq) {
    // Set initial window position on first use (left column)
    ImGui::SetNextWindowPos(ImVec2(10, 135), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Sound Palette");

    ImGui::Text("Click to select, then draw on Piano Roll");
    ImGui::Separator();

    const char* const* oscNames = OSC_NAMES;
    const char* oscDesc[] = {
        // Oscillators (7) - indices 0-6
        "Square wave - Classic NES", "Triangle - Soft, flute-like", "Sawtooth - Rich, buzzy",
        "Sine - Pure, clean", "Noise - Percussion", "7 detuned saws - Massive", "Custom - Adjustable",
        // Synths (10) - indices 7-16
        "Thick detuned saws", "Soft, atmospheric", "Deep punchy bass", "Short, plucky",
        "Crisp arpeggios", "Classic drawbar", "Lush ensemble", "Rich, brassy", "12.5% chiptune", "FM bell/chime",
        // Synthwave (6) - indices 17-22
        "80s PWM lead - Bright, cutting", "808 saw bass - Deep sub", "Supersaw pad - Lush, wide",
        "Crisp arp - Tight sequences", "Poly stab - Chord hits", "DX7 FM - Brass/keys",
        // Techno (5) - indices 23-27
        "TB-303 acid bass", "Techno chord stab", "Classic hoover", "Rave piano", "Reese bass",
        // Hip Hop (4) - indices 28-31
        "Deep 808 sub", "Dusty lo-fi keys", "Vinyl crackle", "Trap lead",
        // Additional (3) - indices 32-34
        "Gated pad", "Poly synth", "Sync lead",
        // Drums (21) - indices 35-55
        "Standard pitch sweep", "Deep 808 sub-bass", "Punchy tight", "Soft warm",
        "Standard with noise", "808 more tonal", "Rimshot clicky", "Hand clap bursts",
        "Closed", "Open longer", "Pedal very short",
        "Mid tom", "Floor tom", "High tom",
        "Crash long", "Ride sustained",
        "808 cowbell", "Wood block", "Conga", "Shaker", "Jingly",
        // Reggaeton Instruments (7) - indices 56-62
        "Punchy reggaeton bass", "Latin brass stab", "Scraped dembow", "Latin bongo", "Metallic timbale", "Reggaeton kick", "Tight clap snare",
        // High-Accuracy Recreations (2) - indices 63-64
        "Formant-filtered talkbox lead", "Resonant filtered saw bass",
        // Six-operator FM - index 65
        "Six-operator FM: bells, e-pianos, brass, metallic basses",
        // Multisample - index 66
        "Multisample: key zones, velocity layers, round-robin",
        // Granular and the analogue drum model - indices 67-68
        "Granular: clouds, freezes and time-stretch from a sample",
        "Analogue drum model: an editable kick, snare and hat"
    };

    constexpr int NUM_OSCILLATORS = 7;  // Pulse, Triangle, Sawtooth, Sine, Noise, Supersaw, Custom
    constexpr int NUM_SYNTHS = 28;  // 10 original + 6 synthwave + 5 techno + 4 hip-hop + 3 additional (reggaeton synths are in Reggaeton section)
    constexpr int NUM_RECREATIONS = 2;      // Vocoder, KavinskyBass
    constexpr int RECREATIONS_START = static_cast<int>(OscillatorType::Vocoder);

    // The configurable engines, which live after the recreations. Derived
    // rather than written down: a hardcoded count here is how the palette
    // came to be silently missing four entries in the first place.
    constexpr int ENGINES_START = static_cast<int>(OscillatorType::FMSynth);
    constexpr int NUM_ENGINES =
        static_cast<int>(OscillatorType::DrumModel) - ENGINES_START + 1;
    static_assert(ENGINES_START == RECREATIONS_START + NUM_RECREATIONS,
                  "the engines must follow the recreations with no gap, or "
                  "the palette skips whatever falls between them");
    // Plus FM, which lives at the end of the enum. This assert is the reason
    // the mistake of inserting FMSynth mid-enum lasted one compile instead of
    // reaching a user as every instrument past Custom playing the wrong sound.
    static_assert(RECREATIONS_START + NUM_RECREATIONS + 4 == OSC_NAME_COUNT,
                  "OSC_NAMES must stay index-aligned with OscillatorType");
    // Whatever is last in the enum must be last in the table. This assert is
    // the reason inserting FMSynth mid-enum lasted one compile instead of
    // reaching a user as every instrument past Custom playing wrong.
    // The matching assert on the last enum entry now lives beside the
    // table itself, at file scope.
    static_assert(OSC_NAME_COUNT > 0, "OSC_NAMES must not be empty");
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // ========== OSCILLATORS (Collapsible) ==========
    PushCategoryHeaderStyle(ImVec4(0.67f, 1.00f, 0.67f, 1.0f));

    if (ImGui::CollapsingHeader("Oscillators", g_PaletteExpanded_Oscillators ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        g_PaletteExpanded_Oscillators = true;
        ImVec2 iconSize(55, 35);
        for (int i = 0; i < NUM_OSCILLATORS; ++i) {
            ImGui::PushID(i);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool isPaletteSelected = (g_SelectedPaletteItem == i);

            ImU32 bgColor = isPaletteSelected ? IM_COL32(80, 120, 80, 255) : IM_COL32(40, 50, 40, 255);
            ImU32 waveColor = isPaletteSelected ? IM_COL32(200, 255, 200, 255) : IM_COL32(100, 180, 100, 255);
            ImU32 borderColor = isPaletteSelected ? IM_COL32(150, 255, 150, 255) : IM_COL32(70, 90, 70, 255);

            drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), bgColor, 4.0f);
            drawList->AddRect(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 2.0f : 1.0f);
            DrawWaveformIcon(drawList, pos, iconSize, static_cast<OscillatorType>(i), waveColor);

            ImGui::InvisibleButton("##osc", iconSize);
            if (ImGui::IsItemClicked()) {
                if (g_SelectedPaletteItem == i) {
                    g_SelectedPaletteItem = -1;
                } else {
                    g_SelectedPaletteItem = i;
                    g_SelectedDurationMult = 1.0f;
                    g_SelectedChordIndex = -1;  // Clear chord selection when selecting an oscillator
                    ui.pianoRollMode = PianoRollMode::Draw;
                    project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(i);
                    seq.updateChannelConfigs();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", oscNames[i]);
                ImGui::TextDisabled("%s", oscDesc[i]);
                ImGui::EndTooltip();
            }
            if (i < NUM_OSCILLATORS - 1) ImGui::SameLine();
            ImGui::PopID();
        }
    } else {
        g_PaletteExpanded_Oscillators = false;
    }
    ImGui::PopStyleColor(3);

    // ========== SYNTHS (Collapsible) ==========
    PushCategoryHeaderStyle(ImVec4(0.71f, 0.57f, 1.00f, 1.0f));

    if (ImGui::CollapsingHeader("Synths", g_PaletteExpanded_Synths ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        g_PaletteExpanded_Synths = true;
        ImVec2 iconSize(50, 32);
        for (int i = NUM_OSCILLATORS; i < NUM_OSCILLATORS + NUM_SYNTHS; ++i) {
            ImGui::PushID(i);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool isPaletteSelected = (g_SelectedPaletteItem == i);

            ImU32 bgColor = isPaletteSelected ? IM_COL32(100, 80, 140, 255) : IM_COL32(45, 40, 55, 255);
            ImU32 waveColor = isPaletteSelected ? IM_COL32(220, 180, 255, 255) : IM_COL32(150, 120, 180, 255);
            ImU32 borderColor = isPaletteSelected ? IM_COL32(200, 150, 255, 255) : IM_COL32(80, 70, 100, 255);

            drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), bgColor, 4.0f);
            drawList->AddRect(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 2.0f : 1.0f);
            DrawWaveformIcon(drawList, pos, iconSize, static_cast<OscillatorType>(i), waveColor);

            ImGui::InvisibleButton("##synth", iconSize);
            if (ImGui::IsItemClicked()) {
                if (g_SelectedPaletteItem == i) {
                    g_SelectedPaletteItem = -1;
                } else {
                    g_SelectedPaletteItem = i;
                    g_SelectedDurationMult = 1.0f;
                    g_SelectedChordIndex = -1;  // Clear chord selection when selecting a synth
                    ui.pianoRollMode = PianoRollMode::Draw;
                    project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(i);
                    seq.updateChannelConfigs();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", oscNames[i]);
                ImGui::TextDisabled("%s", oscDesc[i]);
                ImGui::EndTooltip();
            }
            int idx = i - NUM_OSCILLATORS;
            if ((idx + 1) % 5 != 0 && i < NUM_OSCILLATORS + NUM_SYNTHS - 1) ImGui::SameLine();
            ImGui::PopID();
        }
    } else {
        g_PaletteExpanded_Synths = false;
    }
    ImGui::PopStyleColor(3);

    // ========== RECREATIONS (High-accuracy signature sounds) ==========
    PushCategoryHeaderStyle(ImVec4(1.00f, 0.57f, 0.86f, 1.0f));

    if (ImGui::CollapsingHeader("Recreations", g_PaletteExpanded_Recreations ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        g_PaletteExpanded_Recreations = true;
        ImVec2 iconSize(50, 32);
        for (int i = RECREATIONS_START; i < RECREATIONS_START + NUM_RECREATIONS; ++i) {
            ImGui::PushID(i);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool isPaletteSelected = (g_SelectedPaletteItem == i);

            ImU32 bgColor = isPaletteSelected ? IM_COL32(140, 80, 120, 255) : IM_COL32(55, 40, 50, 255);
            ImU32 waveColor = isPaletteSelected ? IM_COL32(255, 180, 230, 255) : IM_COL32(180, 120, 160, 255);
            ImU32 borderColor = isPaletteSelected ? IM_COL32(255, 150, 220, 255) : IM_COL32(100, 70, 90, 255);

            drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), bgColor, 4.0f);
            drawList->AddRect(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y), borderColor, 4.0f, 0, isPaletteSelected ? 2.0f : 1.0f);
            DrawWaveformIcon(drawList, pos, iconSize, static_cast<OscillatorType>(i), waveColor);

            ImGui::InvisibleButton("##recreation", iconSize);
            if (ImGui::IsItemClicked()) {
                if (g_SelectedPaletteItem == i) {
                    g_SelectedPaletteItem = -1;
                } else {
                    g_SelectedPaletteItem = i;
                    g_SelectedDurationMult = 1.0f;
                    g_SelectedChordIndex = -1;
                    ui.pianoRollMode = PianoRollMode::Draw;
                    project.channels[ui.selectedChannel].oscillator.type = static_cast<OscillatorType>(i);
                    seq.updateChannelConfigs();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", oscNames[i]);
                ImGui::TextDisabled("%s", oscDesc[i]);
                ImGui::EndTooltip();
            }
            if (i < RECREATIONS_START + NUM_RECREATIONS - 1) ImGui::SameLine();
            ImGui::PopID();
        }
    } else {
        g_PaletteExpanded_Recreations = false;
    }
    ImGui::PopStyleColor(3);

    // ========== ENGINES (Configurable instruments) ==========
    //
    // Wavetable, FM, the multisample sampler, granular and the modelled drum
    // kit. These were reachable only from the Channel Editor's Type
    // dropdown: the palette drew types 0 to 64 and stopped, so five whole
    // engines were invisible in the one panel whose job is to show what the
    // program can do.
    PushCategoryHeaderStyle(ImVec4(0.55f, 0.90f, 1.00f, 1.0f));

    if (ImGui::CollapsingHeader("Engines",
            g_PaletteExpanded_Engines ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        g_PaletteExpanded_Engines = true;

        ImGui::TextDisabled("Pick one, then set it up in the Channel Editor.");

        // Wavetable is OscillatorType::Custom, which lives back at index 6
        // with the plain oscillators - so it is listed here as well. It
        // belongs with the engines far more than it belongs beside Sine.
        //
        // The names must match ALL_ENGINES in Genres.h, which is what the
        // genre focus filters on; a mismatch would hide an engine forever,
        // so there is a test comparing the two lists.
        struct EngineEntry { OscillatorType type; const char* focusKey; };
        static const EngineEntry ENGINE_ENTRIES[] = {
            {OscillatorType::Custom,    "Wavetable"},
            {OscillatorType::FMSynth,   "FM"},
            {OscillatorType::Sampler,   "Sampler"},
            {OscillatorType::Granular,  "Granular"},
            {OscillatorType::DrumModel, "Drum Model"},
        };
        const int engineCount =
            static_cast<int>(sizeof(ENGINE_ENTRIES) / sizeof(ENGINE_ENTRIES[0]));

        ImVec2 iconSize(50, 32);
        int drawn = 0;
        for (int e = 0; e < engineCount; ++e) {
            // Same filter-on-attention rule as the chord and drum sections:
            // a genre decides what is put in front of you, and the palette's
            // "show everything" switch brings the rest back.
            if (!ui.paletteShowEverything &&
                !genreShowsEngine(ui.genre, ENGINE_ENTRIES[e].focusKey)) {
                continue;
            }

            const int i = static_cast<int>(ENGINE_ENTRIES[e].type);
            ImGui::PushID(20000 + i);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            const bool isPaletteSelected = (g_SelectedPaletteItem == i);

            const ImU32 bgColor = isPaletteSelected ? IM_COL32(40, 95, 130, 255)
                                                    : IM_COL32(34, 46, 56, 255);
            const ImU32 waveColor = isPaletteSelected ? IM_COL32(170, 230, 255, 255)
                                                      : IM_COL32(110, 160, 190, 255);
            const ImU32 borderColor = isPaletteSelected ? IM_COL32(120, 210, 255, 255)
                                                        : IM_COL32(60, 90, 110, 255);

            drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y),
                                    bgColor, 4.0f);
            drawList->AddRect(pos, ImVec2(pos.x + iconSize.x, pos.y + iconSize.y),
                              borderColor, 4.0f, 0, isPaletteSelected ? 2.0f : 1.0f);
            DrawWaveformIcon(drawList, pos, iconSize, ENGINE_ENTRIES[e].type, waveColor);

            ImGui::InvisibleButton("##engine", iconSize);
            if (ImGui::IsItemClicked()) {
                if (g_SelectedPaletteItem == i) {
                    g_SelectedPaletteItem = -1;
                } else {
                    g_SelectedPaletteItem = i;
                    g_SelectedDurationMult = 1.0f;
                    g_SelectedChordIndex = -1;
                    ui.pianoRollMode = PianoRollMode::Draw;
                    project.channels[ui.selectedChannel].oscillator.type =
                        ENGINE_ENTRIES[e].type;
                    seq.updateChannelConfigs();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", oscNames[i]);
                ImGui::TextDisabled("%s", oscDesc[i]);
                ImGui::EndTooltip();
            }
            // Wrapped on the count DRAWN, not the loop index: with a genre
            // filtering some out, using the index leaves ragged gaps where
            // the hidden ones would have been.
            ++drawn;
            if (drawn % 4 != 0) ImGui::SameLine();
            ImGui::PopID();
        }

        // ImGui::SameLine() after the last icon would pull whatever comes
        // next up beside it.
        if (drawn % 4 != 0) ImGui::NewLine();

        if (drawn == 0) {
            ImGui::TextDisabled("None for this genre. Use Show Everything.");
        }
    } else {
        g_PaletteExpanded_Engines = false;
    }
    ImGui::PopStyleColor(3);

    // ========== CHORDS (Organized by Genre) ==========
    ImGui::Separator();
    // What the current focus is holding back, and the way out of it. Shown
    // above the sections themselves so it is read before the absence is
    // noticed, rather than after hunting for something that is missing.
    if (ui.genre != Genre::Everything) {
        const int hidden = genreHiddenSectionCount(
            ui.genre, ALL_CHORD_SETS, ALL_CHORD_SET_COUNT,
            ALL_DRUM_SETS, ALL_DRUM_SET_COUNT);
        if (hidden > 0) {
            ImGui::Checkbox("Show everything", &ui.paletteShowEverything);
            ImGui::SameLine();
            ImGui::TextDisabled("(%d hidden by %s focus)", hidden, genreName(ui.genre));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%d palette section%s are set aside for now.\n"
                                  "Nothing has been removed.",
                                  hidden, (hidden == 1) ? "" : "s");
            }
            ImGui::Separator();
        }
    }

    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "CHORDS (click to select, then draw)");

    // Helper lambda to draw chord buttons for a genre
    auto DrawChordGenre = [&](const char* genre, bool& expanded, const char* headerLabel) {
        if (!ui.paletteShowEverything && !genreShowsChordSet(ui.genre, genre)) {
            return;
        }

        PushCategoryHeaderStyle(ImVec4(0.75f, 0.50f, 1.00f, 1.0f));

        if (ImGui::CollapsingHeader(headerLabel, expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            expanded = true;
            ImGui::Indent(5.0f);

            // Count chords in this genre to calculate width
            int chordCount = 0;
            for (int i = 0; i < g_NumChordPresets; ++i) {
                if (strcmp(g_ChordPresets[i].genre, genre) == 0) chordCount++;
            }

            // Create horizontal scrolling child region
            float buttonWidth = 80.0f;
            float spacing = 4.0f;
            float totalWidth = chordCount * (buttonWidth + spacing) + 10.0f;
            float availWidth = ImGui::GetContentRegionAvail().x;
            float childHeight = 40.0f;  // Button height + padding

            ImGui::BeginChild(headerLabel, ImVec2(0, childHeight), false,
                ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);

            for (int i = 0; i < g_NumChordPresets; ++i) {
                const ChordPreset& chord = g_ChordPresets[i];
                if (strcmp(chord.genre, genre) != 0) continue;

                ImGui::PushID(i + 5000);  // Unique ID for chords

                bool isSelected = (g_SelectedChordIndex == i);
                ImVec2 buttonSize(80, 30);
                ImVec2 pos = ImGui::GetCursorScreenPos();

                // Button colors
                ImU32 bgColor = isSelected ? IM_COL32(100, 70, 140, 255) : IM_COL32(50, 40, 70, 255);
                ImU32 borderColor = isSelected ? IM_COL32(180, 140, 220, 255) : IM_COL32(90, 70, 110, 255);
                ImU32 textColor = isSelected ? IM_COL32(255, 230, 255, 255) : IM_COL32(180, 160, 200, 255);

                drawList->AddRectFilled(pos, ImVec2(pos.x + buttonSize.x, pos.y + buttonSize.y), bgColor, 4.0f);
                drawList->AddRect(pos, ImVec2(pos.x + buttonSize.x, pos.y + buttonSize.y), borderColor, 4.0f, 0, isSelected ? 2.0f : 1.0f);

                // Draw chord name centered
                ImVec2 textSize = ImGui::CalcTextSize(chord.name);
                float textX = pos.x + (buttonSize.x - textSize.x) * 0.5f;
                float textY = pos.y + (buttonSize.y - textSize.y) * 0.5f;
                drawList->AddText(ImVec2(textX, textY), textColor, chord.name);

                ImGui::InvisibleButton("##chord", buttonSize);
                if (ImGui::IsItemClicked()) {
                    if (g_SelectedChordIndex == i) {
                        g_SelectedChordIndex = -1;
                        g_SelectedPaletteItem = -1;
                    } else {
                        g_SelectedChordIndex = i;
                        g_SelectedPaletteItem = -1;  // Deselect individual sounds
                        ui.pianoRollMode = PianoRollMode::Draw;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", chord.name);
                    ImGui::TextDisabled("%s", chord.description);
                    ImGui::Text("Notes: %d", chord.noteCount);
                    ImGui::EndTooltip();
                }

                ImGui::SameLine();
                ImGui::PopID();
            }
            ImGui::EndChild();  // End horizontal scroll region
            ImGui::Unindent(5.0f);
        } else {
            expanded = false;
        }
        ImGui::PopStyleColor(3);
    };

    // Draw all chord genres
    DrawChordGenre("Pop", g_PaletteExpanded_Chords_Pop, "Pop Chords");
    DrawChordGenre("Jazz", g_PaletteExpanded_Chords_Jazz, "Jazz Chords");
    DrawChordGenre("Rock", g_PaletteExpanded_Chords_Rock, "Rock Power Chords");
    DrawChordGenre("EDM", g_PaletteExpanded_Chords_EDM, "EDM Chords");
    DrawChordGenre("HipHop", g_PaletteExpanded_Chords_HipHop, "Hip Hop");
    DrawChordGenre("Reggaeton", g_PaletteExpanded_Chords_Reggaeton, "Reggaeton Chords");
    DrawChordGenre("Synthwave", g_PaletteExpanded_Chords_Synthwave, "Synthwave/Outrun");
    DrawChordGenre("Chiptune", g_PaletteExpanded_Chords_Chiptune, "Chiptune 8-bit");

    // ========== DRUMS (Expandable Categories) ==========
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "DRUMS (click category to expand)");

    // Kicks (indices: 7 oscillators + 28 synths = 35 base)
    {
        const int indices[] = { 35, 36, 37, 38 };
        const char* names[] = { "Kick", "Kick808", "KickHard", "KickSoft" };
        const char* descs[] = { "Standard pitch sweep", "Deep 808 sub-bass", "Punchy tight", "Soft warm" };
        DrawDrumCategory("Kicks", g_PaletteExpanded_Kicks, indices, names, descs, 4, project, ui, seq);
    }

    // Snares
    {
        const int indices[] = { 39, 40, 41, 42 };
        const char* names[] = { "Snare", "Snare808", "SnareRim", "Clap" };
        const char* descs[] = { "Standard with noise", "808 more tonal", "Rimshot clicky", "Hand clap bursts" };
        DrawDrumCategory("Snares & Claps", g_PaletteExpanded_Snares, indices, names, descs, 4, project, ui, seq);
    }

    // Hi-Hats
    {
        const int indices[] = { 43, 44, 45 };
        const char* names[] = { "HiHat", "HiHatOpen", "HiHatPedal" };
        const char* descs[] = { "Closed hi-hat", "Open longer decay", "Pedal very short" };
        DrawDrumCategory("Hi-Hats", g_PaletteExpanded_HiHats, indices, names, descs, 3, project, ui, seq);
    }

    // Toms
    {
        const int indices[] = { 46, 47, 48 };
        const char* names[] = { "Tom", "TomLow", "TomHigh" };
        const char* descs[] = { "Mid tom", "Floor tom low pitch", "High tom" };
        DrawDrumCategory("Toms", g_PaletteExpanded_Toms, indices, names, descs, 3, project, ui, seq);
    }

    // Cymbals
    {
        const int indices[] = { 49, 50 };
        const char* names[] = { "Crash", "Ride" };
        const char* descs[] = { "Crash cymbal long decay", "Ride cymbal sustained" };
        DrawDrumCategory("Cymbals", g_PaletteExpanded_Cymbals, indices, names, descs, 2, project, ui, seq);
    }

    // Percussion
    {
        const int indices[] = { 51, 52, 53, 54, 55 };
        const char* names[] = { "Cowbell", "Clave", "Conga", "Maracas", "Tambourine" };
        const char* descs[] = { "808 cowbell", "Wood block click", "Conga drum", "Shaker", "Jingly metallic" };
        DrawDrumCategory("Percussion", g_PaletteExpanded_Percussion, indices, names, descs, 5, project, ui, seq);
    }

    // Reggaeton (synths + drums)
    {
        const int indices[] = { 56, 57, 58, 59, 60, 61, 62 };
        const char* names[] = { "Reggae Bass", "Latin Brass", "Guira", "Bongo", "Timbale", "Dembow 808", "Dembow Snare" };
        const char* descs[] = { "Punchy reggaeton bass", "Latin brass stab", "Scraped metal dembow", "Latin bongo", "Metallic timbale", "Reggaeton kick", "Tight clap snare" };
        DrawDrumCategory("Reggaeton Drums", g_PaletteExpanded_Reggaeton, indices, names, descs, 7, project, ui, seq);
    }

    // ========== PATTERN TEMPLATES ==========
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "PATTERNS (click to insert)");

    PushCategoryHeaderStyle(ImVec4(0.50f, 0.75f, 1.00f, 1.0f));

    if (ImGui::CollapsingHeader("Drum Patterns", g_PaletteExpanded_Patterns ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        g_PaletteExpanded_Patterns = true;
        ImGui::Indent(10.0f);

        Pattern& pattern = project.patterns[ui.selectedPattern];

        for (int i = 0; i < g_NumDrumPatterns; ++i) {
            const DrumPattern& dp = g_DrumPatterns[i];
            ImGui::PushID(i + 1000);  // Unique ID for patterns

            ImVec2 buttonSize(130, 40);
            ImVec2 pos = ImGui::GetCursorScreenPos();

            // Button colors
            ImU32 bgColor = IM_COL32(40, 50, 70, 255);
            ImU32 borderColor = IM_COL32(80, 100, 140, 255);
            ImU32 textColor = IM_COL32(180, 200, 255, 255);

            drawList->AddRectFilled(pos, ImVec2(pos.x + buttonSize.x, pos.y + buttonSize.y), bgColor, 5.0f);
            drawList->AddRect(pos, ImVec2(pos.x + buttonSize.x, pos.y + buttonSize.y), borderColor, 5.0f, 0, 1.5f);

            // Pattern name
            drawList->AddText(ImVec2(pos.x + 8, pos.y + 4), textColor, dp.name);
            // Description in smaller text
            ImU32 descColor = IM_COL32(120, 140, 180, 255);
            drawList->AddText(ImVec2(pos.x + 8, pos.y + 20), descColor, dp.description);

            // Note count indicator
            char noteCountStr[16];
            snprintf(noteCountStr, sizeof(noteCountStr), "%d notes", dp.noteCount);
            ImVec2 textSize = ImGui::CalcTextSize(noteCountStr);
            drawList->AddText(ImVec2(pos.x + buttonSize.x - textSize.x - 6, pos.y + 4), descColor, noteCountStr);

            ImGui::InvisibleButton("##pattern", buttonSize);

            // Check if this pattern is being previewed
            bool isBeingPreviewed = (g_IsPatternPreviewing && g_PreviewPatternIndex == i);

            if (ImGui::IsItemClicked()) {
                if (isBeingPreviewed) {
                    // Cancel preview if clicking the same pattern
                    g_IsPatternPreviewing = false;
                    g_PreviewPatternIndex = -1;
                } else {
                    // Enter preview mode for this pattern
                    g_IsPatternPreviewing = true;
                    g_PreviewPatternIndex = i;
                    ui.isPastePreviewing = false;  // Cancel any paste preview
                }
            }

            if (ImGui::IsItemHovered() || isBeingPreviewed) {
                // Highlight on hover or when previewing
                ImU32 highlightColor = isBeingPreviewed ? IM_COL32(100, 255, 150, 255) : IM_COL32(150, 180, 255, 255);
                drawList->AddRect(pos, ImVec2(pos.x + buttonSize.x, pos.y + buttonSize.y),
                                 highlightColor, 5.0f, 0, 2.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", dp.name);
                    ImGui::TextDisabled("%s", dp.description);
                    ImGui::TextDisabled("%d notes, %d beats", dp.noteCount, dp.lengthBeats);
                    if (isBeingPreviewed) {
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "PREVIEWING - Click piano roll to place");
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.5f, 1.0f), "Press Escape or click here to cancel");
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Click to preview, then click piano roll to place");
                    }
                    ImGui::EndTooltip();
                }
            }

            // Show "PREVIEW" indicator on the button if active
            if (isBeingPreviewed) {
                ImU32 previewColor = IM_COL32(100, 255, 150, 255);
                drawList->AddRectFilled(ImVec2(pos.x + buttonSize.x - 55, pos.y + 2),
                                        ImVec2(pos.x + buttonSize.x - 2, pos.y + 14),
                                        IM_COL32(50, 100, 50, 255), 3.0f);
                drawList->AddText(ImVec2(pos.x + buttonSize.x - 52, pos.y + 2), previewColor, "PREVIEW");
            }

            // 2 patterns per row
            if (i % 2 == 0 && i < g_NumDrumPatterns - 1) {
                ImGui::SameLine();
            }

            ImGui::PopID();
        }

        ImGui::Unindent(10.0f);
    } else {
        g_PaletteExpanded_Patterns = false;
    }
    ImGui::PopStyleColor(3);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "SAMPLE TRACKS (full songs)");

    PushCategoryHeaderStyle(ImVec4(0.75f, 0.50f, 1.00f, 1.0f));

    if (ImGui::CollapsingHeader("Sample Tracks", g_PaletteExpanded_SampleTracks ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        g_PaletteExpanded_SampleTracks = true;
        ImGui::Indent(10.0f);

        // Group tracks by genre
        const char* genres[] = {"Synthwave", "Techno", "Chiptune", "Hip Hop", "Trap", "House", "Reggaeton"};
        bool* genreExpanded[] = {&g_PaletteExpanded_SynthwaveTracks, &g_PaletteExpanded_TechnoTracks,
                                 &g_PaletteExpanded_ChiptuneTracks, &g_PaletteExpanded_HipHopTracks,
                                 &g_PaletteExpanded_TrapTracks, &g_PaletteExpanded_HouseTracks,
                                 &g_PaletteExpanded_ReggaetonTracks};
        ImU32 genreColors[] = {
            IM_COL32(255, 100, 200, 255),  // Synthwave - pink
            IM_COL32(100, 255, 200, 255),  // Techno - cyan
            IM_COL32(100, 255, 100, 255),  // Chiptune - green
            IM_COL32(255, 200, 100, 255),  // Hip Hop - orange
            IM_COL32(255, 100, 100, 255),  // Trap - red
            IM_COL32(100, 200, 255, 255),  // House - blue
            IM_COL32(255, 215, 0, 255),    // Reggaeton - gold
        };

        for (int g = 0; g < 7; ++g) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(genreColors[g]));
            if (ImGui::TreeNodeEx(genres[g], *genreExpanded[g] ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                *genreExpanded[g] = true;
                ImGui::PopStyleColor();

                // Find all tracks of this genre
                for (int i = 0; i < g_NumSampleTracks; ++i) {
                    const SampleTrack& st = g_SampleTracks[i];
                    if (strcmp(st.genre, genres[g]) != 0) continue;

                    ImGui::PushID(i + 2000);  // Unique ID for sample tracks

                    ImVec2 buttonSize(160, 50);
                    ImVec2 pos = ImGui::GetCursorScreenPos();

                    // Button colors - purple theme for sample tracks
                    ImU32 bgColor = IM_COL32(50, 35, 70, 255);
                    ImU32 borderColor = genreColors[g];
                    ImU32 textColor = IM_COL32(220, 200, 255, 255);

                    drawList->AddRectFilled(pos, ImVec2(pos.x + buttonSize.x, pos.y + buttonSize.y), bgColor, 5.0f);
                    drawList->AddRect(pos, ImVec2(pos.x + buttonSize.x, pos.y + buttonSize.y), borderColor, 5.0f, 0, 1.5f);

                    // Track name
                    drawList->AddText(ImVec2(pos.x + 8, pos.y + 4), textColor, st.name);
                    // Description in smaller text
                    ImU32 descColor = IM_COL32(160, 140, 200, 255);
                    drawList->AddText(ImVec2(pos.x + 8, pos.y + 20), descColor, st.description);
                    // Note count and BPM
                    char infoStr[32];
                    snprintf(infoStr, sizeof(infoStr), "%d notes | %d BPM", st.noteCount, st.bpm);
                    drawList->AddText(ImVec2(pos.x + 8, pos.y + 34), descColor, infoStr);

                    ImGui::InvisibleButton("##sampletrack", buttonSize);

                    // Check if this track is being previewed
                    bool isBeingPreviewed = (g_IsSampleTrackPreviewing && g_PreviewSampleTrackIndex == i);

                    if (ImGui::IsItemClicked()) {
                        if (isBeingPreviewed) {
                            // Cancel preview if clicking the same track
                            g_IsSampleTrackPreviewing = false;
                            g_PreviewSampleTrackIndex = -1;
                        } else {
                            // Enter preview mode for this track
                            g_IsSampleTrackPreviewing = true;
                            g_PreviewSampleTrackIndex = i;
                            g_IsPatternPreviewing = false;  // Cancel pattern preview
                            ui.isPastePreviewing = false;   // Cancel paste preview
                        }
                    }

                    if (ImGui::IsItemHovered() || isBeingPreviewed) {
                        // Highlight on hover or when previewing
                        ImU32 highlightColor = isBeingPreviewed ? IM_COL32(200, 100, 255, 255) : IM_COL32(180, 150, 255, 255);
                        drawList->AddRect(pos, ImVec2(pos.x + buttonSize.x, pos.y + buttonSize.y),
                                         highlightColor, 5.0f, 0, 2.0f);
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(genreColors[g]), "%s", st.name);
                            ImGui::TextDisabled("%s - %s", st.genre, st.description);
                            ImGui::TextDisabled("%d notes, %d beats, suggested BPM: %d", st.noteCount, st.lengthBeats, st.bpm);
                            if (isBeingPreviewed) {
                                ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "PREVIEWING - Click piano roll to place");
                                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.5f, 1.0f), "Press Escape or click here to cancel");
                            } else {
                                ImGui::TextColored(ImVec4(0.6f, 0.5f, 0.8f, 1.0f), "Click to preview, then click piano roll to place");
                            }
                            ImGui::EndTooltip();
                        }
                    }

                    // Show "PREVIEW" indicator on the button if active
                    if (isBeingPreviewed) {
                        ImU32 previewColor = IM_COL32(200, 100, 255, 255);
                        drawList->AddRectFilled(ImVec2(pos.x + buttonSize.x - 55, pos.y + 2),
                                                ImVec2(pos.x + buttonSize.x - 2, pos.y + 14),
                                                IM_COL32(80, 40, 100, 255), 3.0f);
                        drawList->AddText(ImVec2(pos.x + buttonSize.x - 52, pos.y + 2), previewColor, "PREVIEW");
                    }

                    ImGui::PopID();
                }

                ImGui::TreePop();
            } else {
                *genreExpanded[g] = false;
                ImGui::PopStyleColor();
            }
        }

        ImGui::Unindent(10.0f);
    } else {
        g_PaletteExpanded_SampleTracks = false;
    }
    ImGui::PopStyleColor(3);

    ImGui::Separator();

    // Show selected item
    if (g_SelectedPaletteItem >= 0) {
        ImVec4 color;
        const char* typeStr;
        if (g_SelectedPaletteItem < NUM_OSCILLATORS) {
            color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
            typeStr = "Oscillator";
        } else if (g_SelectedPaletteItem < NUM_OSCILLATORS + NUM_SYNTHS) {
            color = ImVec4(0.8f, 0.6f, 1.0f, 1.0f);
            typeStr = "Synth";
        } else if (g_SelectedPaletteItem >= RECREATIONS_START) {
            color = ImVec4(1.0f, 0.6f, 0.9f, 1.0f);
            typeStr = "Recreation";
        } else {
            color = ImVec4(1.0f, 0.6f, 0.6f, 1.0f);
            typeStr = "Drum";
        }
        ImGui::TextColored(color, "Selected: %s", oscNames[g_SelectedPaletteItem]);
        if (g_SelectedPaletteItem >= NUM_OSCILLATORS + NUM_SYNTHS &&
            g_SelectedPaletteItem < RECREATIONS_START) {
            // Show duration for drums
            const char* durStr = (g_SelectedDurationMult < 0.75f) ? "Short" :
                                 (g_SelectedDurationMult > 1.5f) ? "Long" : "Normal";
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", durStr);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            g_SelectedPaletteItem = -1;
            g_SelectedDurationMult = 1.0f;
        }
    } else {
        ImGui::TextDisabled("No sound selected");
    }

    ImGui::End();
}

// ============================================================================
// Pad Controller - MPC-style live performance interface
// ============================================================================

// Helper: Draw a rotary knob
inline bool DrawKnob(const char* label, float* value, float minVal = 0.0f, float maxVal = 1.0f,
                     float radius = 30.0f, ImU32 color = IM_COL32(100, 180, 255, 255)) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center(pos.x + radius, pos.y + radius);

    bool changed = false;

    // Invisible button for interaction
    ImGui::InvisibleButton(label, ImVec2(radius * 2, radius * 2 + 20));
    bool isActive = ImGui::IsItemActive();
    bool isHovered = ImGui::IsItemHovered();

    if (isActive && ImGui::IsMouseDragging(0)) {
        float delta = -ImGui::GetIO().MouseDelta.y * 0.005f;
        *value = std::clamp(*value + delta * (maxVal - minVal), minVal, maxVal);
        changed = true;
    }

    // Knob colors
    ImU32 bgColor = isHovered ? IM_COL32(60, 60, 70, 255) : IM_COL32(40, 40, 50, 255);
    ImU32 borderColor = isActive ? IM_COL32(255, 200, 100, 255) : color;

    // Draw knob body
    drawList->AddCircleFilled(center, radius, bgColor);
    drawList->AddCircle(center, radius, borderColor, 32, 2.5f);

    // Draw value arc (from 7 o'clock to 5 o'clock = 240 degrees)
    float normalizedValue = (*value - minVal) / (maxVal - minVal);
    float startAngle = 2.356f;  // 135 degrees in radians (7 o'clock)
    float endAngle = startAngle + normalizedValue * 4.712f;  // 270 degrees range

    // Draw arc segments
    ImVec2 prevPoint(center.x + std::cos(startAngle) * (radius - 5),
                     center.y + std::sin(startAngle) * (radius - 5));
    for (int i = 1; i <= 32; ++i) {
        float t = static_cast<float>(i) / 32.0f;
        float angle = startAngle + t * normalizedValue * 4.712f;
        if (angle > endAngle) break;
        ImVec2 point(center.x + std::cos(angle) * (radius - 5),
                     center.y + std::sin(angle) * (radius - 5));
        drawList->AddLine(prevPoint, point, color, 4.0f);
        prevPoint = point;
    }

    // Draw indicator line
    float indicatorAngle = startAngle + normalizedValue * 4.712f;
    ImVec2 indicatorEnd(center.x + std::cos(indicatorAngle) * (radius - 10),
                        center.y + std::sin(indicatorAngle) * (radius - 10));
    drawList->AddLine(center, indicatorEnd, IM_COL32(255, 255, 255, 200), 3.0f);

    // Label below
    ImVec2 textPos(pos.x, pos.y + radius * 2 + 2);
    drawList->AddText(textPos, IM_COL32(200, 200, 200, 255), label);

    return changed;
}

// Helper: Draw a pad button
inline bool DrawPad(int index, const PadAssignment& pad, bool isActive, float velocity,
                    ImVec2 size, Sequencer& sequencer, PadControllerState& state) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Create unique ID
    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "##pad_%d", index);

    bool triggered = false;

    ImGui::InvisibleButton(idBuf, size);
    bool isHovered = ImGui::IsItemHovered();
    bool isPressed = ImGui::IsItemActive();

    // Trigger on press
    if (ImGui::IsItemActivated()) {
        triggered = true;
        state.padActive[index] = true;
        state.padVelocity[index] = 0.8f;

        // Play the sound
        sequencer.previewNote(pad.midiNote, 0.8f, pad.oscillatorType, 0.5f);
    }

    // Release
    if (ImGui::IsItemDeactivated()) {
        state.padActive[index] = false;
    }

    // Calculate colors with glow effect
    ImU32 padColor = pad.color;
    float r = ((padColor >> 24) & 0xFF) / 255.0f;
    float g = ((padColor >> 16) & 0xFF) / 255.0f;
    float b = ((padColor >> 8) & 0xFF) / 255.0f;

    float brightness = isActive ? 1.5f : (isPressed ? 1.3f : (isHovered ? 1.1f : 0.7f));
    r = std::min(r * brightness, 1.0f);
    g = std::min(g * brightness, 1.0f);
    b = std::min(b * brightness, 1.0f);

    ImU32 fillColor = IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255),
                                static_cast<int>(b * 255), 255);
    ImU32 borderColor = isActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 80, 90, 255);

    // Draw pad with rounded corners
    float rounding = 8.0f;
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), fillColor, rounding);
    drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, rounding, 0, 2.0f);

    // Glow effect when active
    if (isActive) {
        drawList->AddRect(ImVec2(pos.x - 2, pos.y - 2),
                          ImVec2(pos.x + size.x + 2, pos.y + size.y + 2),
                          IM_COL32(255, 255, 255, 100), rounding + 2, 0, 3.0f);
    }

    // Draw label centered
    ImVec2 textSize = ImGui::CalcTextSize(pad.label.c_str());
    ImVec2 textPos(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f);
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), pad.label.c_str());

    return triggered;
}

// Helper: Draw a piano key
inline bool DrawPianoKey(int keyIndex, int octaveOffset, bool isBlack, bool isActive,
                         ImVec2 pos, ImVec2 size, Sequencer& sequencer,
                         OscillatorType sound, PadControllerState& state) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Calculate MIDI note
    // keyIndex 0 = C, 1 = C#, 2 = D, etc.
    static const int keyToNote[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    int midiNote = (octaveOffset * 12) + keyToNote[keyIndex % 12];

    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "##key_%d_%d", octaveOffset, keyIndex);

    bool triggered = false;

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(idBuf, size);
    bool isHovered = ImGui::IsItemHovered();
    bool isPressed = ImGui::IsItemActive();

    if (ImGui::IsItemActivated()) {
        triggered = true;
        sequencer.previewNote(midiNote, 0.8f, sound, 0.5f);

        // Record if recording
        if (state.isRecording) {
            // Note: Recording logic handled in Sequencer
        }
    }

    // Colors
    ImU32 fillColor, borderColor;
    if (isBlack) {
        fillColor = isActive ? IM_COL32(100, 100, 255, 255) :
                   (isPressed ? IM_COL32(80, 80, 100, 255) :
                   (isHovered ? IM_COL32(50, 50, 60, 255) : IM_COL32(30, 30, 40, 255)));
        borderColor = IM_COL32(20, 20, 30, 255);
    } else {
        fillColor = isActive ? IM_COL32(150, 150, 255, 255) :
                   (isPressed ? IM_COL32(200, 200, 220, 255) :
                   (isHovered ? IM_COL32(240, 240, 245, 255) : IM_COL32(255, 255, 255, 255)));
        borderColor = IM_COL32(100, 100, 110, 255);
    }

    // Draw key
    float rounding = isBlack ? 2.0f : 4.0f;
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), fillColor, rounding);
    drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, rounding);

    return triggered;
}

inline void DrawPadController(Project& project, UIState& ui, Sequencer& sequencer) {
    auto& state = ui.padController;

    // ==========================================================================
    // Computer Keyboard Input (ASDF row = white keys, QWERTY row = black keys)
    // Standard piano layout on QWERTY keyboard
    // ==========================================================================
    if (!ImGui::GetIO().WantTextInput) {
        int baseNote = state.keyboardOctave * 12;  // Base MIDI note for current octave

        // White keys: A S D F G H J K L ; (C D E F G A B C D E)
        // MIDI offsets: 0 2 4 5 7 9 11 12 14 16
        struct KeyMapping { ImGuiKey key; int noteOffset; int keyIndex; };
        static const KeyMapping whiteKeys[] = {
            {ImGuiKey_A, 0, 0},   // C
            {ImGuiKey_S, 2, 1},   // D
            {ImGuiKey_D, 4, 2},   // E
            {ImGuiKey_F, 5, 3},   // F
            {ImGuiKey_G, 7, 4},   // G
            {ImGuiKey_H, 9, 5},   // A
            {ImGuiKey_J, 11, 6},  // B
            {ImGuiKey_K, 12, 7},  // C (next octave)
            {ImGuiKey_L, 14, 8},  // D (next octave)
            {ImGuiKey_Semicolon, 16, 9}  // E (next octave)
        };

        // Black keys: W E   T Y U   O P (C# D#   F# G# A#   C# D#)
        // MIDI offsets: 1 3   6 8 10  13 15
        static const KeyMapping blackKeys[] = {
            {ImGuiKey_W, 1, 10},  // C#
            {ImGuiKey_E, 3, 11},  // D#
            {ImGuiKey_T, 6, 12},  // F#
            {ImGuiKey_Y, 8, 13},  // G#
            {ImGuiKey_U, 10, 14}, // A#
            {ImGuiKey_O, 13, 15}, // C# (next octave)
            {ImGuiKey_P, 15, 16}  // D# (next octave)
        };

        // Process white keys
        for (const auto& km : whiteKeys) {
            int midiNote = baseNote + km.noteOffset;
            if (ImGui::IsKeyPressed(km.key)) {
                sequencer.previewNote(midiNote, 0.8f, state.keyboardSound, 0.5f);
                if (km.keyIndex < PadControllerState::NUM_KEYS) {
                    state.keyActive[km.keyIndex] = true;
                }
                // Record if recording
                if (state.isRecording) {
                    RecordedNoteEvent evt;
                    evt.pitch = midiNote;
                    evt.velocity = 0.8f;
                    evt.timestamp = sequencer.getCurrentBeat();
                    evt.oscillatorType = state.keyboardSound;
                    evt.duration = 0.5f;
                    evt.isNoteOn = true;
                    state.recordedEvents.push_back(evt);
                }
            }
            if (ImGui::IsKeyReleased(km.key)) {
                if (km.keyIndex < PadControllerState::NUM_KEYS) {
                    state.keyActive[km.keyIndex] = false;
                }
            }
        }

        // Process black keys
        for (const auto& km : blackKeys) {
            int midiNote = baseNote + km.noteOffset;
            if (ImGui::IsKeyPressed(km.key)) {
                sequencer.previewNote(midiNote, 0.8f, state.keyboardSound, 0.5f);
                if (km.keyIndex < PadControllerState::NUM_KEYS) {
                    state.keyActive[km.keyIndex] = true;
                }
                // Record if recording
                if (state.isRecording) {
                    RecordedNoteEvent evt;
                    evt.pitch = midiNote;
                    evt.velocity = 0.8f;
                    evt.timestamp = sequencer.getCurrentBeat();
                    evt.oscillatorType = state.keyboardSound;
                    evt.duration = 0.5f;
                    evt.isNoteOn = true;
                    state.recordedEvents.push_back(evt);
                }
            }
            if (ImGui::IsKeyReleased(km.key)) {
                if (km.keyIndex < PadControllerState::NUM_KEYS) {
                    state.keyActive[km.keyIndex] = false;
                }
            }
        }

        // Octave up/down with Z and X keys
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
            state.keyboardOctave = std::max(0, state.keyboardOctave - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_X)) {
            state.keyboardOctave = std::min(7, state.keyboardOctave + 1);
        }

        // Number keys 1-8 trigger pads (top row = pads 0-7, with shift = pads 8-15)
        static const ImGuiKey padKeys[] = {
            ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
            ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8
        };
        bool shift = ImGui::GetIO().KeyShift;
        const auto& currentBank = state.getCurrentBank();
        for (int i = 0; i < 8; ++i) {
            int padIdx = shift ? (i + 8) : i;
            if (ImGui::IsKeyPressed(padKeys[i])) {
                const auto& pad = currentBank[padIdx];
                sequencer.previewNote(pad.midiNote, 0.8f, pad.oscillatorType, 0.5f);
                state.padActive[padIdx] = true;
                state.padVelocity[padIdx] = 0.8f;
                // Record if recording
                if (state.isRecording) {
                    RecordedNoteEvent evt;
                    evt.pitch = pad.midiNote;
                    evt.velocity = 0.8f;
                    evt.timestamp = sequencer.getCurrentBeat();
                    evt.oscillatorType = pad.oscillatorType;
                    evt.duration = 0.25f;
                    evt.isNoteOn = true;
                    state.recordedEvents.push_back(evt);
                }
            }
            if (ImGui::IsKeyReleased(padKeys[i])) {
                state.padActive[padIdx] = false;
            }
        }

        // Bank switch with Tab
        if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
            state.currentBank = 1 - state.currentBank;
        }
    }

    // Set initial window position on first use (bottom left)
    ImGui::SetNextWindowPos(ImVec2(10, 545), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Pad Controller", nullptr, ImGuiWindowFlags_NoCollapse);

    ImVec2 windowSize = ImGui::GetContentRegionAvail();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Layout dimensions
    float padSize = std::min(80.0f, (windowSize.x - 400) / 4.5f);
    float knobRadius = 25.0f;
    float keyboardHeight = 100.0f;

    // ==========================================================================
    // Top Section: Bank Select + Record Controls + Arpeggiator
    // ==========================================================================
    ImGui::BeginChild("TopControls", ImVec2(0, 60), true);

    // Genre preset selector
    ImGui::Text("Genre:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    const char* genreNames[] = {"General", "Synthwave", "Techno", "Hip Hop", "Drum Kit"};
    int currentGenreInt = static_cast<int>(state.currentGenre);
    if (ImGui::Combo("##genre", &currentGenreInt, genreNames, 5)) {
        state.loadGenrePreset(static_cast<PadGenre>(currentGenreInt));
    }
    ImGui::SameLine();

    // Bank selection
    ImGui::Text("Bank:");
    ImGui::SameLine();
    if (ImGui::RadioButton("A (Drums)", state.currentBank == 0)) {
        state.currentBank = 0;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("B (Synths)", state.currentBank == 1)) {
        state.currentBank = 1;
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // =============================================
    // PLAY/STOP Button - For playback without recording
    // =============================================
    bool isPlaying = sequencer.isPlaying() && !state.isRecording;

    if (isPlaying) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 150, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 200, 80, 255));
    }

    if (ImGui::Button(isPlaying ? "STOP" : "PLAY", ImVec2(60, 40))) {
        if (isPlaying) {
            // Stop playback AND clear preview pattern
            // This ensures REC starts fresh without old notes playing
            sequencer.stop();
            sequencer.clearPreviewPattern();
        } else {
            // Start playback of current pattern
            sequencer.setPreviewPattern(ui.selectedPattern, ui.selectedChannel);
            sequencer.stop();  // Reset position
            sequencer.play();  // Start playing
        }
    }

    if (isPlaying) {
        ImGui::PopStyleColor(2);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isPlaying ?
            "Stop playback" :
            "Play current pattern");
    }

    ImGui::SameLine();

    // =============================================
    // RECORD Button - Records notes while playing
    // =============================================
    ImVec2 recPos = ImGui::GetCursorScreenPos();

    // Color the button based on state
    if (state.isRecording) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 50, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 80, 80, 255));
    }

    if (ImGui::Button(state.isRecording ? "STOP REC" : "REC", ImVec2(80, 40))) {
        if (state.isRecording) {
            // =============================================
            // STOP RECORDING - Save notes to pattern
            // =============================================
            state.isRecording = false;
            state.recordArmed = false;

            // Stop playback
            sequencer.stop();

            // Transfer recorded events to pattern
            if (!state.recordedEvents.empty() && ui.selectedPattern < static_cast<int>(project.patterns.size())) {
                Pattern& pattern = project.patterns[ui.selectedPattern];
                for (const auto& evt : state.recordedEvents) {
                    if (evt.isNoteOn) {
                        Note note;
                        note.pitch = evt.pitch;
                        note.velocity = evt.velocity;
                        note.startTime = state.quantizeEnabled ?
                                         state.quantizeBeat(evt.timestamp - state.recordStartBeat) :
                                         evt.timestamp - state.recordStartBeat;
                        note.duration = evt.duration;
                        note.oscillatorType = evt.oscillatorType;
                        pattern.notes.push_back(note);
                    }
                }
                // Note: Don't auto-play recording - user can click PLAY to hear it
                // This prevents confusion when clicking REC again
            }
            state.recordedEvents.clear();
        } else {
            // =============================================
            // START RECORDING - Immediate start, no arm step
            // =============================================

            // CRITICAL: Stop everything first to silence any playing notes
            sequencer.stop();  // This calls allNotesOff() internally

            // Clear the preview pattern so old notes don't play
            sequencer.clearPreviewPattern();

            // Now set recording state
            state.isRecording = true;
            state.recordArmed = true;
            state.recordStartBeat = 0.0f;
            state.recordedEvents.clear();

            // Start the sequencer for timing purposes only
            // No notes will play since preview pattern is cleared
            sequencer.play();
            state.recordStartBeat = sequencer.getCurrentBeat();
        }
    }

    if (state.isRecording) {
        ImGui::PopStyleColor(2);
    }

    // Tooltip with instructions
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(state.isRecording ?
            "Click to STOP recording and save notes" :
            "Click to START recording\n"
            "Then play pads/keyboard\n"
            "Notes are saved when you stop");
    }

    // Draw recording indicator (blinking red dot)
    if (state.isRecording) {
        float time = static_cast<float>(ImGui::GetTime());
        float alpha = (std::sin(time * 6.0f) + 1.0f) * 0.5f;
        ImVec2 indicatorPos(recPos.x + 85, recPos.y + 20);
        drawList->AddCircleFilled(indicatorPos, 8, IM_COL32(255, 0, 0, static_cast<int>(alpha * 255)));
        drawList->AddCircle(indicatorPos, 10, IM_COL32(255, 100, 100, 150), 12, 2.0f);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Quantize", &state.quantizeEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    const char* quantItems[] = {"1/4", "1/8", "1/16", "1/32"};
    static int quantIndex = 2;  // Default to 1/16
    if (ImGui::Combo("##quant", &quantIndex, quantItems, 4)) {
        const float quantVals[] = {1.0f, 0.5f, 0.25f, 0.125f};
        state.quantizeResolution = quantVals[quantIndex];
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Arpeggiator controls
    ImGui::Checkbox("ARP", &state.arpEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    const char* arpModeNames[] = {"Up", "Down", "Up-Down", "Random"};
    if (ImGui::Combo("##arpmode", &state.arpMode, arpModeNames, 4)) {
        // arpMode is already an int (0-3)
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    const char* arpRateNames[] = {"1/4", "1/8", "1/16", "1/32"};
    int arpRateInt = static_cast<int>(state.arpRate);
    if (ImGui::Combo("##arprate", &arpRateInt, arpRateNames, 4)) {
        state.arpRate = static_cast<ArpRate>(arpRateInt);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::SliderInt("Oct", &state.arpOctaves, 1, 4);

    ImGui::EndChild();

    // ==========================================================================
    // Main Section: Pads + Knobs + Waveform
    // ==========================================================================
    ImGui::BeginChild("MainSection", ImVec2(0, windowSize.y - keyboardHeight - 100), false);

    // Left side: 4x4 Pad Grid
    ImGui::BeginChild("PadGrid", ImVec2(padSize * 4 + 30, 0), true);

    const auto& currentBank = state.getCurrentBank();
    float padding = 5.0f;

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            int idx = row * 4 + col;

            if (col > 0) ImGui::SameLine();

            ImVec2 padPos = ImGui::GetCursorScreenPos();
            DrawPad(idx, currentBank[idx], state.padActive[idx], state.padVelocity[idx],
                   ImVec2(padSize, padSize), sequencer, state);

            // Record the event if recording
            if (state.isRecording && ImGui::IsItemActivated()) {
                RecordedNoteEvent evt;
                evt.pitch = currentBank[idx].midiNote;
                evt.velocity = 0.8f;
                evt.timestamp = sequencer.getCurrentBeat();
                evt.oscillatorType = currentBank[idx].oscillatorType;
                evt.duration = 0.25f;  // Default duration, could be adjusted
                evt.isNoteOn = true;
                state.recordedEvents.push_back(evt);
            }
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // Right side: Knobs + Waveform
    ImGui::BeginChild("KnobsAndWaveform", ImVec2(0, 0), true);

    // Parameter Knobs (2 rows of 4)
    const char* knobLabels[] = {"Attack", "Decay", "Sustain", "Release",
                                 "Width", "Detune", "Filter", "Volume"};
    ImU32 knobColors[] = {
        IM_COL32(255, 100, 100, 255),  // Attack - red
        IM_COL32(255, 180, 100, 255),  // Decay - orange
        IM_COL32(100, 255, 100, 255),  // Sustain - green
        IM_COL32(100, 180, 255, 255),  // Release - blue
        IM_COL32(255, 100, 255, 255),  // Width - magenta
        IM_COL32(100, 255, 255, 255),  // Detune - cyan
        IM_COL32(255, 255, 100, 255),  // Filter - yellow
        IM_COL32(200, 200, 200, 255)   // Volume - white
    };

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 4; ++col) {
            int idx = row * 4 + col;
            if (col > 0) ImGui::SameLine();

            float minVal = (idx == 5) ? -1.0f : 0.0f;  // Detune can be negative
            float maxVal = 1.0f;
            if (idx < 4) {
                // ADSR values in seconds
                maxVal = (idx == 0 || idx == 3) ? 2.0f : 1.0f;
            }

            if (DrawKnob(knobLabels[idx], &state.knobValues[idx], minVal, maxVal,
                        knobRadius, knobColors[idx])) {
                // Apply knob values to the preview channel synth (channel 7)
                const int previewChannel = 7;  // Project::MAX_CHANNELS - 1
                auto& synth = sequencer.getSynth(previewChannel);

                // Build envelope and oscillator config from knob values
                Envelope env;
                env.attack = state.knobValues[0];   // Knob 0: Attack
                env.decay = state.knobValues[1];    // Knob 1: Decay
                env.sustain = state.knobValues[2];  // Knob 2: Sustain
                env.release = state.knobValues[3];  // Knob 3: Release

                OscillatorConfig osc;
                osc.pulseWidth = state.knobValues[4];  // Knob 4: Width
                osc.detune = state.knobValues[5] * 100.0f;  // Knob 5: Detune (-100 to +100 cents)

                synth.setConfig(osc, env);

                // Update project channel volume for preview channel
                project.channels[previewChannel].volume = state.knobValues[7];  // Knob 7: Volume
            }
        }
        ImGui::Spacing();
    }

    // Apply knob values every frame (not just on change) for live control
    {
        const int previewChannel = 7;
        Envelope env;
        env.attack = state.knobValues[0];
        env.decay = state.knobValues[1];
        env.sustain = state.knobValues[2];
        env.release = state.knobValues[3];

        OscillatorConfig osc;
        osc.pulseWidth = state.knobValues[4];
        osc.detune = state.knobValues[5] * 100.0f;

        sequencer.getSynth(previewChannel).setConfig(osc, env);
        project.channels[previewChannel].volume = state.knobValues[7];
    }

    ImGui::Separator();

    // Waveform Display
    ImGui::Text("Waveform");
    ImVec2 wavePos = ImGui::GetCursorScreenPos();
    ImVec2 waveSize(ImGui::GetContentRegionAvail().x - 10, 80);

    // Background
    drawList->AddRectFilled(wavePos, ImVec2(wavePos.x + waveSize.x, wavePos.y + waveSize.y),
                           IM_COL32(20, 20, 30, 255), 4.0f);
    drawList->AddRect(wavePos, ImVec2(wavePos.x + waveSize.x, wavePos.y + waveSize.y),
                     IM_COL32(60, 60, 80, 255), 4.0f);

    // Draw waveform from buffer
    float centerY = wavePos.y + waveSize.y * 0.5f;
    drawList->AddLine(ImVec2(wavePos.x, centerY),
                     ImVec2(wavePos.x + waveSize.x, centerY),
                     IM_COL32(40, 40, 60, 255));

    if (state.waveformBuffer[0] != 0.0f || sequencer.isPlaying()) {
        ImVec2 prevPoint(wavePos.x, centerY);
        for (int i = 0; i < PadControllerState::WAVEFORM_SAMPLES - 1; ++i) {
            int readIdx = (state.waveformWritePos + i) % PadControllerState::WAVEFORM_SAMPLES;
            float sample = state.waveformBuffer[readIdx];
            float x = wavePos.x + (static_cast<float>(i) / PadControllerState::WAVEFORM_SAMPLES) * waveSize.x;
            float y = centerY - sample * waveSize.y * 0.4f;
            y = std::clamp(y, wavePos.y + 2, wavePos.y + waveSize.y - 2);

            ImVec2 point(x, y);
            drawList->AddLine(prevPoint, point, IM_COL32(0, 255, 150, 200), 1.5f);
            prevPoint = point;
        }
    }

    ImGui::Dummy(waveSize);

    // Keyboard sound selector
    ImGui::Spacing();
    ImGui::Text("Keyboard Sound:");
    ImGui::SameLine();

    const char* soundNames[] = {"Lead", "Pad", "Bass", "Pluck", "Saw", "Pulse", "Tri", "Super"};
    OscillatorType soundTypes[] = {
        OscillatorType::SynthLead, OscillatorType::SynthPad, OscillatorType::SynthBass,
        OscillatorType::SynthPluck, OscillatorType::Sawtooth, OscillatorType::Pulse,
        OscillatorType::Triangle, OscillatorType::Supersaw
    };

    for (int i = 0; i < 8; ++i) {
        if (i > 0) ImGui::SameLine();
        bool selected = state.keyboardSound == soundTypes[i];
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(100, 150, 255, 255));
        }
        if (ImGui::SmallButton(soundNames[i])) {
            state.keyboardSound = soundTypes[i];
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();

    ImGui::EndChild();

    // ==========================================================================
    // Bottom Section: 2-Octave Keyboard
    // ==========================================================================
    ImGui::BeginChild("Keyboard", ImVec2(0, keyboardHeight), true);

    ImVec2 keyboardPos = ImGui::GetCursorScreenPos();
    float whiteKeyWidth = (windowSize.x - 30) / 15.0f;  // 15 white keys for 2 octaves + 1
    float whiteKeyHeight = keyboardHeight - 20;
    float blackKeyWidth = whiteKeyWidth * 0.65f;
    float blackKeyHeight = whiteKeyHeight * 0.6f;

    // Octave controls
    ImGui::Text("Octave: %d", state.keyboardOctave);
    ImGui::SameLine();
    if (ImGui::SmallButton("-")) {
        state.keyboardOctave = std::max(0, state.keyboardOctave - 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        state.keyboardOctave = std::min(7, state.keyboardOctave + 1);
    }

    // Draw white keys first
    static const int whiteKeyNotes[] = {0, 2, 4, 5, 7, 9, 11};  // C D E F G A B
    float x = keyboardPos.x;

    for (int oct = 0; oct < 2; ++oct) {
        for (int i = 0; i < 7; ++i) {
            int keyIndex = whiteKeyNotes[i];
            int midiNote = (state.keyboardOctave + oct) * 12 + keyIndex;

            ImVec2 keyPos(x, keyboardPos.y + 20);
            ImVec2 keySize(whiteKeyWidth - 2, whiteKeyHeight);

            DrawPianoKey(keyIndex + oct * 12, state.keyboardOctave, false,
                        state.keyActive[oct * 12 + i], keyPos, keySize,
                        sequencer, state.keyboardSound, state);

            // Record if recording and key was triggered
            if (state.isRecording && ImGui::IsItemActivated()) {
                RecordedNoteEvent evt;
                evt.pitch = midiNote;
                evt.velocity = 0.8f;
                evt.timestamp = sequencer.getCurrentBeat();
                evt.oscillatorType = state.keyboardSound;
                evt.duration = 0.5f;
                evt.isNoteOn = true;
                state.recordedEvents.push_back(evt);
            }

            x += whiteKeyWidth;
        }
    }

    // Add final C
    {
        int midiNote = (state.keyboardOctave + 2) * 12;
        ImVec2 keyPos(x, keyboardPos.y + 20);
        ImVec2 keySize(whiteKeyWidth - 2, whiteKeyHeight);
        DrawPianoKey(0, state.keyboardOctave + 2, false, false, keyPos, keySize,
                    sequencer, state.keyboardSound, state);
    }

    // Draw black keys on top
    static const int blackKeyOffsets[] = {1, 3, -1, 6, 8, 10, -1};  // C# D# skip F# G# A# skip
    x = keyboardPos.x + whiteKeyWidth - blackKeyWidth * 0.5f;

    for (int oct = 0; oct < 2; ++oct) {
        for (int i = 0; i < 7; ++i) {
            if (blackKeyOffsets[i] >= 0) {
                int keyIndex = blackKeyOffsets[i];

                ImVec2 keyPos(x, keyboardPos.y + 20);
                ImVec2 keySize(blackKeyWidth, blackKeyHeight);

                DrawPianoKey(keyIndex, state.keyboardOctave + oct, true, false,
                            keyPos, keySize, sequencer, state.keyboardSound, state);
            }
            x += whiteKeyWidth;
        }
    }

    ImGui::EndChild();

    ImGui::End();
}

// ============================================================================
// Tools Panel - Production Tools for Synthwave Beats
// ============================================================================

// Global state for tools
static int g_ToolsDrumGenre = 0;  // 0=Synthwave, 1=Outrun, 2=Darksynth, 3=Italo, 4=Techno, 5=Retrowave
static float g_ToolsDrumDensity = 0.5f;  // 0.0-1.0 (sparse to busy)
static bool g_ToolsDrumAddCrash = true;  // Add crash at measure start

static int g_ToolsArpMode = 0;  // 0=Up, 1=Down, 2=UpDown, 3=Random
static int g_ToolsArpRate = 2;  // 0=8th, 1=16th, 2=32nd
static int g_ToolsArpOctaves = 2;  // 1-4
static float g_ToolsArpGate = 0.8f;  // Note length as fraction of step

static int g_ToolsBassStyle = 0;  // 0=Octave Pulse, 1=Root+Fifth, 2=Walking, 3=Arp
static int g_ToolsBassRoot = 0;  // Root note (0=C, 1=C#, etc.)
static int g_ToolsBassOctave = 2;  // Base octave

static int g_ToolsVelocityCurve = 0;  // 0=Linear, 1=Exp, 2=Log, 3=S-Curve
static float g_ToolsVelocityStart = 0.5f;
static float g_ToolsVelocityEnd = 1.0f;

static int g_ToolsFillIntensity = 1;  // 0=Light, 1=Medium, 2=Heavy
static int g_ToolsFillStyle = 0;  // 0=Snare, 1=Tom, 2=Hihat, 3=Mixed

static float g_ToolsVariationAmount = 0.3f;  // How much to vary
static bool g_ToolsVariationTiming = true;  // Vary note timing
static bool g_ToolsVariationVelocity = true;  // Vary velocity
static bool g_ToolsVariationPitch = false;  // Vary pitch (for non-drums)

static int g_ToolsLayerOctave = 1;  // Octave offset for layer
static float g_ToolsLayerDetune = 5.0f;  // Detune in cents
static float g_ToolsLayerVelocity = 0.7f;  // Velocity of layer

static float g_ToolsHumanizeTiming = 0.02f;  // Timing variation in beats
static float g_ToolsHumanizeVelocity = 0.15f;  // Velocity variation (0-1)

// The scale tables and helpers now live in Scales.h, out of ImGui's reach
// and testable on their own.

// Helper: Generate drum pattern based on genre and density
inline void generateDrumPattern(Pattern& pattern, int genre, float density, bool addCrash, float bpm) {
    pattern.notes.clear();

    // Base patterns for each genre (4 bars = 16 beats at 4/4)
    // density affects hi-hat subdivision and ghost notes

    auto addDrum = [&](float beat, OscillatorType type, int pitch, float velocity, float dur) {
        Note n;
        n.startTime = beat;
        n.pitch = pitch;
        n.oscillatorType = type;
        n.velocity = velocity;
        n.duration = dur;
        pattern.notes.push_back(n);
    };

    // Calculate durations based on BPM for drums
    float kickDur = 0.25f;
    float snareDur = 0.25f;
    float hatDur = 0.125f;

    int bars = 4;
    int beatsPerBar = 4;

    // Hi-hat density: 0.0 = quarter notes, 0.5 = 8ths, 1.0 = 16ths with ghost notes
    float hatStep = density < 0.3f ? 1.0f : (density < 0.7f ? 0.5f : 0.25f);
    bool addGhosts = density > 0.6f;

    for (int bar = 0; bar < bars; ++bar) {
        float barStart = static_cast<float>(bar * beatsPerBar);

        // Crash at bar start (optional)
        if (addCrash && bar == 0) {
            addDrum(barStart, OscillatorType::Crash, 49, 0.8f, 0.5f);
        }

        switch (genre) {
            case 0:  // Synthwave - driving 4/4 with open hats
            case 5:  // Retrowave (similar)
                addDrum(barStart, OscillatorType::Kick808, 36, 1.0f, kickDur);
                addDrum(barStart + 1.0f, OscillatorType::Snare808, 38, 0.9f, snareDur);
                addDrum(barStart + 2.0f, OscillatorType::Kick808, 36, 1.0f, kickDur);
                addDrum(barStart + 3.0f, OscillatorType::Snare808, 38, 0.9f, snareDur);
                // Open hat on offbeats
                addDrum(barStart + 0.5f, OscillatorType::HiHatOpen, 46, 0.6f, hatDur);
                addDrum(barStart + 1.5f, OscillatorType::HiHatOpen, 46, 0.5f, hatDur);
                addDrum(barStart + 2.5f, OscillatorType::HiHatOpen, 46, 0.6f, hatDur);
                addDrum(barStart + 3.5f, OscillatorType::HiHatOpen, 46, 0.5f, hatDur);
                if (addGhosts) {
                    addDrum(barStart + 2.75f, OscillatorType::Kick808, 36, 0.5f, kickDur);  // Ghost kick
                }
                break;

            case 1:  // Outrun - faster, more aggressive
                addDrum(barStart, OscillatorType::KickHard, 36, 1.0f, kickDur);
                addDrum(barStart + 0.5f, OscillatorType::KickHard, 36, 0.7f, kickDur);
                addDrum(barStart + 1.0f, OscillatorType::Snare808, 38, 0.95f, snareDur);
                addDrum(barStart + 2.0f, OscillatorType::KickHard, 36, 1.0f, kickDur);
                addDrum(barStart + 2.5f, OscillatorType::KickHard, 36, 0.7f, kickDur);
                addDrum(barStart + 3.0f, OscillatorType::Snare808, 38, 0.95f, snareDur);
                // Fast closed hats
                for (float h = 0; h < 4.0f; h += hatStep) {
                    float vel = (std::fmod(h, 1.0f) < 0.01f) ? 0.7f : 0.4f;
                    addDrum(barStart + h, OscillatorType::HiHat, 42, vel, hatDur);
                }
                break;

            case 2:  // Darksynth - heavy, industrial
                addDrum(barStart, OscillatorType::KickHard, 36, 1.0f, kickDur);
                addDrum(barStart + 1.0f, OscillatorType::Snare808, 38, 1.0f, snareDur);
                addDrum(barStart + 1.0f, OscillatorType::Clap, 39, 0.7f, snareDur);  // Layered clap
                addDrum(barStart + 2.0f, OscillatorType::KickHard, 36, 1.0f, kickDur);
                addDrum(barStart + 2.75f, OscillatorType::KickHard, 36, 0.8f, kickDur);  // Syncopation
                addDrum(barStart + 3.0f, OscillatorType::Snare808, 38, 1.0f, snareDur);
                addDrum(barStart + 3.0f, OscillatorType::Clap, 39, 0.7f, snareDur);
                // Ride cymbal
                for (float h = 0; h < 4.0f; h += 0.5f) {
                    addDrum(barStart + h, OscillatorType::Ride, 51, 0.4f, hatDur);
                }
                break;

            case 3:  // Italo Disco - bouncy, upbeat
                addDrum(barStart, OscillatorType::Kick, 36, 0.95f, kickDur);
                addDrum(barStart + 1.0f, OscillatorType::Kick, 36, 0.95f, kickDur);
                addDrum(barStart + 1.0f, OscillatorType::Clap, 39, 0.8f, snareDur);
                addDrum(barStart + 2.0f, OscillatorType::Kick, 36, 0.95f, kickDur);
                addDrum(barStart + 3.0f, OscillatorType::Kick, 36, 0.95f, kickDur);
                addDrum(barStart + 3.0f, OscillatorType::Clap, 39, 0.8f, snareDur);
                // Offbeat open hats (disco style)
                addDrum(barStart + 0.5f, OscillatorType::HiHatOpen, 46, 0.7f, hatDur * 2);
                addDrum(barStart + 1.5f, OscillatorType::HiHatOpen, 46, 0.7f, hatDur * 2);
                addDrum(barStart + 2.5f, OscillatorType::HiHatOpen, 46, 0.7f, hatDur * 2);
                addDrum(barStart + 3.5f, OscillatorType::HiHatOpen, 46, 0.7f, hatDur * 2);
                break;

            case 4:  // Techno - four on the floor
                addDrum(barStart, OscillatorType::Kick, 36, 1.0f, kickDur);
                addDrum(barStart + 1.0f, OscillatorType::Kick, 36, 1.0f, kickDur);
                addDrum(barStart + 2.0f, OscillatorType::Kick, 36, 1.0f, kickDur);
                addDrum(barStart + 3.0f, OscillatorType::Kick, 36, 1.0f, kickDur);
                // Clap on 2 and 4
                addDrum(barStart + 1.0f, OscillatorType::Clap, 39, 0.85f, snareDur);
                addDrum(barStart + 3.0f, OscillatorType::Clap, 39, 0.85f, snareDur);
                // Hi-hats
                for (float h = 0; h < 4.0f; h += hatStep) {
                    bool isOffbeat = std::fmod(h, 1.0f) > 0.4f && std::fmod(h, 1.0f) < 0.6f;
                    OscillatorType hatType = isOffbeat ? OscillatorType::HiHatOpen : OscillatorType::HiHat;
                    float vel = isOffbeat ? 0.6f : 0.5f;
                    addDrum(barStart + h, hatType, isOffbeat ? 46 : 42, vel, hatDur);
                }
                break;
        }
    }

    pattern.length = bars * beatsPerBar;
}

// Helper: Apply arpeggiator to selected notes
inline void applyArpeggiator(Pattern& pattern, const std::vector<int>& selectedIndices,
                             int mode, int rateDiv, int octaves, float gate) {
    if (selectedIndices.empty()) return;

    // Collect selected notes
    std::vector<Note> selectedNotes;
    for (int idx : selectedIndices) {
        if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
            selectedNotes.push_back(pattern.notes[idx]);
        }
    }
    if (selectedNotes.empty()) return;

    // Find the chord (distinct pitches at similar start times)
    std::vector<int> chordPitches;
    float baseTime = selectedNotes[0].startTime;
    OscillatorType oscType = selectedNotes[0].oscillatorType;
    float baseVelocity = selectedNotes[0].velocity;

    for (const auto& n : selectedNotes) {
        bool found = false;
        for (int p : chordPitches) {
            if (p == n.pitch) { found = true; break; }
        }
        if (!found) chordPitches.push_back(n.pitch);
    }

    if (chordPitches.empty()) return;
    std::sort(chordPitches.begin(), chordPitches.end());

    // Extend chord across octaves
    std::vector<int> arpNotes;
    for (int oct = 0; oct < octaves; ++oct) {
        for (int p : chordPitches) {
            arpNotes.push_back(p + oct * 12);
        }
    }

    // Generate arp pattern
    float step = (rateDiv == 0) ? 0.5f : (rateDiv == 1) ? 0.25f : 0.125f;
    float totalDuration = selectedNotes[0].duration;
    if (totalDuration < step * 4) totalDuration = step * 16;  // Minimum 4 notes

    // Remove original selected notes
    std::vector<int> sortedIndices = selectedIndices;
    std::sort(sortedIndices.rbegin(), sortedIndices.rend());
    for (int idx : sortedIndices) {
        if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
            pattern.notes.erase(pattern.notes.begin() + idx);
        }
    }

    // Add arp notes
    int noteCount = static_cast<int>(totalDuration / step);
    for (int i = 0; i < noteCount; ++i) {
        int noteIndex;
        switch (mode) {
            case 0:  // Up
                noteIndex = i % static_cast<int>(arpNotes.size());
                break;
            case 1:  // Down
                noteIndex = static_cast<int>(arpNotes.size()) - 1 - (i % static_cast<int>(arpNotes.size()));
                break;
            case 2:  // Up-Down
                {
                    int cycle = static_cast<int>(arpNotes.size()) * 2 - 2;
                    int pos = i % cycle;
                    if (pos < static_cast<int>(arpNotes.size())) {
                        noteIndex = pos;
                    } else {
                        noteIndex = cycle - pos;
                    }
                }
                break;
            case 3:  // Random
                noteIndex = rand() % static_cast<int>(arpNotes.size());
                break;
            default:
                noteIndex = i % static_cast<int>(arpNotes.size());
        }

        Note n;
        n.startTime = baseTime + i * step;
        n.pitch = arpNotes[noteIndex];
        n.duration = step * gate;
        n.velocity = baseVelocity * (0.8f + 0.2f * (static_cast<float>(rand()) / RAND_MAX));
        n.oscillatorType = oscType;
        pattern.notes.push_back(n);
    }
}

// Helper: Generate bass pattern
inline void generateBassPattern(Pattern& pattern, int style, int root, int octave, int bars = 4) {
    auto addNote = [&](float beat, int pitch, float dur, float vel) {
        Note n;
        n.startTime = beat;
        n.pitch = pitch;
        n.duration = dur;
        n.velocity = vel;
        n.oscillatorType = OscillatorType::SynthBass;
        pattern.notes.push_back(n);
    };

    int rootNote = root + (octave + 1) * 12;  // +1 because octave 2 is C2
    int fifth = rootNote + 7;
    int octaveUp = rootNote + 12;

    for (int bar = 0; bar < bars; ++bar) {
        float barStart = static_cast<float>(bar * 4);

        switch (style) {
            case 0:  // Octave Pulse - classic synthwave
                addNote(barStart, rootNote, 0.25f, 1.0f);
                addNote(barStart + 0.5f, octaveUp, 0.25f, 0.8f);
                addNote(barStart + 1.0f, rootNote, 0.25f, 0.9f);
                addNote(barStart + 1.5f, octaveUp, 0.25f, 0.7f);
                addNote(barStart + 2.0f, rootNote, 0.25f, 1.0f);
                addNote(barStart + 2.5f, octaveUp, 0.25f, 0.8f);
                addNote(barStart + 3.0f, rootNote, 0.25f, 0.9f);
                addNote(barStart + 3.5f, octaveUp, 0.25f, 0.7f);
                break;

            case 1:  // Root + Fifth
                addNote(barStart, rootNote, 0.75f, 1.0f);
                addNote(barStart + 1.0f, fifth, 0.75f, 0.85f);
                addNote(barStart + 2.0f, rootNote, 0.75f, 1.0f);
                addNote(barStart + 3.0f, fifth, 0.5f, 0.85f);
                addNote(barStart + 3.5f, rootNote, 0.25f, 0.7f);
                break;

            case 2:  // Walking bass
                {
                    int notes[] = {0, 2, 4, 5, 7, 5, 4, 2};  // Scale walk
                    for (int i = 0; i < 8; ++i) {
                        addNote(barStart + i * 0.5f, rootNote + notes[i], 0.4f, 0.9f - i * 0.02f);
                    }
                }
                break;

            case 3:  // Arp style
                addNote(barStart, rootNote, 0.2f, 1.0f);
                addNote(barStart + 0.25f, rootNote + 4, 0.2f, 0.8f);  // Major third
                addNote(barStart + 0.5f, fifth, 0.2f, 0.85f);
                addNote(barStart + 0.75f, octaveUp, 0.2f, 0.75f);
                addNote(barStart + 1.0f, rootNote, 0.2f, 0.95f);
                addNote(barStart + 1.25f, rootNote + 4, 0.2f, 0.8f);
                addNote(barStart + 1.5f, fifth, 0.2f, 0.85f);
                addNote(barStart + 1.75f, octaveUp, 0.2f, 0.75f);
                addNote(barStart + 2.0f, rootNote, 0.2f, 1.0f);
                addNote(barStart + 2.25f, rootNote + 4, 0.2f, 0.8f);
                addNote(barStart + 2.5f, fifth, 0.2f, 0.85f);
                addNote(barStart + 2.75f, octaveUp, 0.2f, 0.75f);
                addNote(barStart + 3.0f, rootNote, 0.2f, 0.95f);
                addNote(barStart + 3.25f, rootNote + 4, 0.2f, 0.8f);
                addNote(barStart + 3.5f, fifth, 0.2f, 0.85f);
                addNote(barStart + 3.75f, rootNote - 2, 0.2f, 0.7f);  // Leading tone
                break;
        }
    }

    pattern.length = bars * 4;
}

// Helper: Apply velocity curve to selected notes
inline void applyVelocityCurve(Pattern& pattern, const std::vector<int>& selectedIndices,
                               int curveType, float startVel, float endVel) {
    if (selectedIndices.size() < 2) return;

    // Sort by start time
    std::vector<std::pair<float, int>> timeIdx;
    for (int idx : selectedIndices) {
        if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
            timeIdx.push_back({pattern.notes[idx].startTime, idx});
        }
    }
    std::sort(timeIdx.begin(), timeIdx.end());

    int count = static_cast<int>(timeIdx.size());
    for (int i = 0; i < count; ++i) {
        float t = static_cast<float>(i) / (count - 1);
        float vel;

        switch (curveType) {
            case 0:  // Linear
                vel = startVel + (endVel - startVel) * t;
                break;
            case 1:  // Exponential
                vel = startVel + (endVel - startVel) * (t * t);
                break;
            case 2:  // Logarithmic
                vel = startVel + (endVel - startVel) * std::sqrt(t);
                break;
            case 3:  // S-Curve
                {
                    float s = t * t * (3.0f - 2.0f * t);  // Smoothstep
                    vel = startVel + (endVel - startVel) * s;
                }
                break;
            default:
                vel = startVel + (endVel - startVel) * t;
        }

        pattern.notes[timeIdx[i].second].velocity = std::max(0.1f, std::min(1.0f, vel));
    }
}

// Helper: Generate drum fill
inline void generateFill(Pattern& pattern, float fillStart, int intensity, int style) {
    auto addDrum = [&](float beat, OscillatorType type, int pitch, float velocity, float dur) {
        Note n;
        n.startTime = beat;
        n.pitch = pitch;
        n.oscillatorType = type;
        n.velocity = velocity;
        n.duration = dur;
        pattern.notes.push_back(n);
    };

    // Fill duration: 0.5 beats (light) to 2 beats (heavy)
    float fillDur = (intensity == 0) ? 0.5f : (intensity == 1) ? 1.0f : 2.0f;
    float step = (intensity == 0) ? 0.25f : (intensity == 1) ? 0.125f : 0.0625f;

    int noteCount = static_cast<int>(fillDur / step);

    for (int i = 0; i < noteCount; ++i) {
        float beat = fillStart - fillDur + i * step;
        float vel = 0.7f + 0.3f * (static_cast<float>(i) / noteCount);  // Crescendo

        switch (style) {
            case 0:  // Snare roll
                addDrum(beat, OscillatorType::Snare808, 38, vel, step * 0.9f);
                break;
            case 1:  // Tom fill
                {
                    OscillatorType toms[] = {OscillatorType::TomHigh, OscillatorType::Tom, OscillatorType::TomLow};
                    int tomIdx = i % 3;
                    int pitches[] = {50, 47, 43};
                    addDrum(beat, toms[tomIdx], pitches[tomIdx], vel, step * 0.9f);
                }
                break;
            case 2:  // Hi-hat roll
                addDrum(beat, (i % 2 == 0) ? OscillatorType::HiHat : OscillatorType::HiHatOpen,
                       (i % 2 == 0) ? 42 : 46, vel * 0.8f, step * 0.8f);
                break;
            case 3:  // Mixed
                {
                    int choice = rand() % 4;
                    if (choice == 0) addDrum(beat, OscillatorType::Snare808, 38, vel, step * 0.9f);
                    else if (choice == 1) addDrum(beat, OscillatorType::Tom, 47, vel, step * 0.9f);
                    else if (choice == 2) addDrum(beat, OscillatorType::HiHat, 42, vel * 0.7f, step * 0.8f);
                    else addDrum(beat, OscillatorType::Clap, 39, vel * 0.8f, step * 0.9f);
                }
                break;
        }
    }

    // Add crash at fill start
    addDrum(fillStart, OscillatorType::Crash, 49, 0.9f, 0.5f);
}

// Helper: Create pattern variation
inline void createVariation(Pattern& pattern, float amount, bool varyTiming, bool varyVelocity, bool varyPitch) {
    for (auto& note : pattern.notes) {
        if (varyTiming) {
            float timingOffset = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * amount * 0.25f;
            note.startTime = std::max(0.0f, note.startTime + timingOffset);
        }

        if (varyVelocity) {
            float velOffset = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * amount * 0.3f;
            note.velocity = std::max(0.1f, std::min(1.0f, note.velocity + velOffset));
        }

        if (varyPitch && !isDrumType(note.oscillatorType)) {
            // Occasionally shift by octave or add harmony
            if (static_cast<float>(rand()) / RAND_MAX < amount * 0.2f) {
                int shift = (rand() % 2 == 0) ? 12 : -12;  // Octave shift
                note.pitch = std::max(24, std::min(96, note.pitch + shift));
            }
        }
    }
}

// Helper: Quick layer (duplicate selection with modifications)
inline void quickLayer(Pattern& pattern, const std::vector<int>& selectedIndices,
                       int octaveOffset, float detuneCents, float layerVelocity) {
    std::vector<Note> newNotes;

    for (int idx : selectedIndices) {
        if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
            Note n = pattern.notes[idx];
            n.pitch += octaveOffset * 12;
            n.velocity *= layerVelocity;

            // Apply detune to oscillator config (via note's detune if we had it)
            // For now, slight timing offset simulates detune chorus effect
            n.startTime += 0.005f;  // 5ms offset

            newNotes.push_back(n);
        }
    }

    for (const auto& n : newNotes) {
        pattern.notes.push_back(n);
    }
}

// Helper: Humanize selected notes
inline void humanizeSelected(Pattern& pattern, const std::vector<int>& selectedIndices,
                             float timingAmount, float velocityAmount) {
    for (int idx : selectedIndices) {
        if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
            Note& n = pattern.notes[idx];

            // Timing humanization
            float timeOffset = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * timingAmount;
            n.startTime = std::max(0.0f, n.startTime + timeOffset);

            // Velocity humanization
            float velOffset = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * velocityAmount;
            n.velocity = std::max(0.1f, std::min(1.0f, n.velocity + velOffset));
        }
    }
}

// A Tools section, unless the current focus has set it aside. Shares the
// palette's "show everything" switch, so one toggle governs the whole focus
// rather than one per panel.
inline bool GenreToolSection(const UIState& ui, const char* name,
                             ImGuiTreeNodeFlags flags = 0) {
    if (!ui.paletteShowEverything && !genreShowsTool(ui.genre, name)) return false;
    return ImGui::CollapsingHeader(name, flags);
}

inline void DrawToolsPanel(Project& project, UIState& ui, Sequencer& seq) {
    ImGui::SetNextWindowPos(ImVec2(220, 135), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_None);

    // ------------------------------------------------------------------
    // Quick Start
    //
    // The patterns a style is built on - a dembow, a boom bap, an octave
    // bass - as buttons, so the tedious part of starting is optional. Each
    // writes ordinary notes into the selected pattern: one undo step,
    // editable and deletable like anything placed by hand. The manual path
    // is untouched, and this whole section folds away.
    // ------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Quick Start", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ui.selectedPattern < 0 ||
            ui.selectedPattern >= static_cast<int>(project.patterns.size())) {
            ImGui::TextDisabled("Select a pattern to write into.");
        } else {
            Pattern& target = project.patterns[ui.selectedPattern];
            const int keyRoot = 60 + g_ToolsScaleRoot;

            KitVoices voices;
            if (ui.selectedChannel >= 0 && ui.selectedChannel < Project::MAX_CHANNELS) {
                voices.pitched = project.channels[ui.selectedChannel].oscillator.type;
            }

            int recipeCount = 0;
            const KitRecipe* recipes = kitRecipes(recipeCount);

            for (int categoryIndex = 0;
                 categoryIndex < static_cast<int>(KitCategory::Count);
                 ++categoryIndex) {
                const KitCategory category = static_cast<KitCategory>(categoryIndex);

                // Nothing suits this genre in this category: skip the label
                // rather than showing an empty heading.
                if (countRecipesForGenre(ui.genre, category) == 0) continue;

                ImGui::TextDisabled("%s", kitCategoryName(category));

                int shown = 0;
                for (int i = 0; i < recipeCount; ++i) {
                    const KitRecipe& recipe = recipes[i];
                    if (recipe.category != category) continue;
                    if (!ui.paletteShowEverything &&
                        !recipeSuitsGenre(recipe, ui.genre)) {
                        continue;
                    }

                    if (shown % 2 == 1) ImGui::SameLine();
                    ++shown;

                    if (ImGui::Button(recipe.name, ImVec2(150, 26))) {
                        g_UndoHistory.saveState(project, recipe.name);
                        const int added = applyKitRecipe(target, recipe, 0.0f,
                                                         4, keyRoot, voices);
                        if (added > 0) {
                            const float extent = 16.0f;
                            if (extent > static_cast<float>(target.length)) {
                                target.length = static_cast<int>(extent);
                            }
                        } else {
                            // Nothing was written - the pattern is full. Do
                            // not leave a phantom step on the undo stack.
                            g_UndoHistory.undo(project);
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\n\nWrites four bars of ordinary "
                                          "notes into '%s'.\nCtrl+Z undoes it; "
                                          "existing notes are kept.",
                                          recipe.description, target.name.c_str());
                    }
                }
            }
            ImGui::TextDisabled("Or draw everything yourself below - these are "
                                "just a faster pencil.");
        }
        ImGui::Separator();
    }

    // What the current focus has set aside, and the way out of it.
    if (ui.genre != Genre::Everything) {
        const int hidden = genreHiddenToolCount(ui.genre, ALL_TOOL_SECTIONS,
                                                ALL_TOOL_SECTION_COUNT);
        if (hidden > 0) {
            ImGui::Checkbox("Show everything##tools", &ui.paletteShowEverything);
            ImGui::SameLine();
            ImGui::TextDisabled("(%d hidden by %s focus)", hidden, genreName(ui.genre));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%d generator%s set aside for now.\n"
                                  "Nothing has been removed - this is the same\n"
                                  "switch the Sound Palette carries.",
                                  hidden, (hidden == 1) ? " is" : "s are");
            }
            ImGui::Separator();
        }
    }

    if (ui.selectedPattern < 0 || ui.selectedPattern >= static_cast<int>(project.patterns.size())) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No pattern selected");
        ImGui::End();
        return;
    }

    Pattern& pattern = project.patterns[ui.selectedPattern];

    // ========================================================================
    // 0. Euclidean Rhythms
    //
    // Distributing k hits as evenly as possible over n steps produces a
    // startling number of the world's actual drum patterns - E(3,8) is the
    // tresillo, E(5,16) the bossa clave. It is the fastest route from an
    // empty pattern to a rhythm worth keeping.
    // ========================================================================
    if (GenreToolSection(ui, "Euclidean Rhythms", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int euclidSteps = 16;
        static int euclidPulses = 5;
        static int euclidRotation = 0;
        static int euclidInstrument = 0;
        static float euclidBars = 4.0f;

        ImGui::SliderInt("Steps", &euclidSteps, 2, 32);
        ImGui::SliderInt("Pulses", &euclidPulses, 0, euclidSteps);
        ImGui::SliderInt("Rotate", &euclidRotation, 0, std::max(0, euclidSteps - 1));

        const char* instruments[] = {"Kick", "Snare", "Hi-Hat", "Clap", "Cowbell", "Selected sound"};
        ImGui::Combo("Instrument", &euclidInstrument, instruments, IM_ARRAYSIZE(instruments));
        ImGui::SliderFloat("Over beats", &euclidBars, 1.0f, 16.0f, "%.0f");

        // A live preview of where the hits land, which is far quicker to
        // read than the numbers are.
        {
            const std::vector<bool> preview =
                generators::euclideanPattern(euclidSteps, euclidPulses, euclidRotation);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const float cell = std::max(4.0f, width / float(std::max(1, euclidSteps)));
            const float height = 18.0f;

            for (int i = 0; i < euclidSteps; ++i) {
                const float x0 = origin.x + cell * i + 1.0f;
                const float x1 = origin.x + cell * (i + 1) - 1.0f;
                const bool hit = preview[size_t(i)];
                // Beat boundaries get a brighter ground so the figure can be
                // read against the pulse.
                const bool onBeat = (euclidSteps % 4 == 0) && (i % (euclidSteps / 4) == 0);
                draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height),
                                    hit ? widgets::accentColor()
                                        : ImGui::GetColorU32(onBeat ? ImGuiCol_FrameBgHovered
                                                                    : ImGuiCol_FrameBg),
                                    2.0f);
            }
            ImGui::Dummy(ImVec2(width, height + 4.0f));
        }

        if (ImGui::Button("Add to pattern", ImVec2(-1, 0))) {
            generators::EuclideanVoice voice;
            voice.steps = euclidSteps;
            voice.pulses = euclidPulses;
            voice.rotation = euclidRotation;

            switch (euclidInstrument) {
                case 0: voice.instrument = OscillatorType::Kick808; voice.pitch = 36; break;
                case 1: voice.instrument = OscillatorType::Snare;   voice.pitch = 38; break;
                case 2: voice.instrument = OscillatorType::HiHat;   voice.pitch = 42;
                        voice.velocity = 0.6f; break;
                case 3: voice.instrument = OscillatorType::Clap;    voice.pitch = 39; break;
                case 4: voice.instrument = OscillatorType::Cowbell; voice.pitch = 56; break;
                default:
                    voice.instrument = project.channels[ui.selectedChannel].oscillator.type;
                    voice.pitch = 60;
                    break;
            }

            const std::vector<Note> notes = generators::generateEuclidean(voice, euclidBars);
            for (const Note& n : notes) pattern.notes.push_back(n);
            seq.updateChannelConfigs();
        }

        if (ImGui::Button("Generate full kit", ImVec2(-1, 0))) {
            const std::vector<Note> kit = generators::generateEuclideanKit(
                euclidSteps, 4, 2, std::max(1, euclidSteps * 2 / 3), euclidBars);
            for (const Note& n : kit) pattern.notes.push_back(n);
            seq.updateChannelConfigs();
        }

        if (ImGui::BeginCombo("Preset", "Classic rhythms...")) {
            for (const generators::EuclideanPreset& preset : generators::euclideanPresets()) {
                if (ImGui::Selectable(preset.name)) {
                    euclidSteps = preset.steps;
                    euclidPulses = preset.pulses;
                    euclidRotation = preset.rotation;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", preset.description);
            }
            ImGui::EndCombo();
        }
    }

    // ========================================================================
    // Chord progressions
    //
    // Beside the Euclidean generator because they are the same kind of tool:
    // a short description of a musical idea, turned into notes you then own
    // and can edit. The chords come from the SCALE rather than from a fixed
    // interval table, which is what makes "1 5 6 4" in a minor key a
    // different set of chords from the same digits in a major one.
    // ========================================================================
    if (GenreToolSection(ui, "Chord Progression")) {
        static char progressionText[64] = "1 5 6 4";
        static int progressionRoot = 0;
        static int progressionScale = 0;
        static int progressionOctave = 4;
        static int progressionVoices = 3;
        static bool progressionSpread = false;
        static float progressionBars = 4.0f;

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("Degrees", progressionText, sizeof(progressionText));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Which degrees of the scale, one chord per bar.\n\n"
                "  1 5 6 4      the one in most songs\n"
                "  I V vi IV    the same thing, written the published way\n"
                "  2 5 1        a jazz turnaround\n\n"
                "Upper and lower case are both accepted and neither changes\n"
                "anything: the scale decides whether a chord is major or\n"
                "minor, so the chord on the sixth degree of a major key is\n"
                "minor whether you write VI or vi.");
        }

        ImGui::SetNextItemWidth(110);
        ImGui::SliderInt("Key", &progressionRoot, 0, 11, noteName(progressionRoot));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::SliderInt("Scale", &progressionScale, 0, SCALE_COUNT - 1,
                         scaleName(progressionScale));

        ImGui::SetNextItemWidth(110);
        ImGui::SliderInt("Octave", &progressionOctave, 1, 7);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderInt("Notes", &progressionVoices, 2, 4);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Three is a triad, four adds the seventh.");
        }

        ImGui::Checkbox("Spread voicing", &progressionSpread);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Drops the root an octave, which is what stops a\n"
                              "stack of close triads sounding like an organ.");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("Bar", &progressionBars, 1.0f, 8.0f, "%.0f beats");

        // What it will produce, before it produces it - the same idea as the
        // note rack's preview. A progression is four numbers, and four
        // numbers do not tell you what they sound like.
        {
            std::vector<int> degrees;
            generators::progressionFromText(progressionText, degrees);

            std::string preview;
            for (int degree : degrees) {
                generators::ProgressionVoice one;
                one.degrees = {degree};
                one.root = progressionRoot;
                one.scaleType = progressionScale;
                one.octave = progressionOctave;
                one.voices = progressionVoices;

                const std::vector<Note> chord =
                    generators::generateProgression(one, 4.0f);
                if (chord.empty()) continue;

                int lowest = 127;
                for (const Note& note : chord) lowest = std::min(lowest, note.pitch);

                // Major, minor or diminished, from the intervals rather than
                // from the numeral - which is the point of the whole thing.
                bool minorThird = false;
                bool flatFifth = false;
                for (const Note& note : chord) {
                    const int interval = note.pitch - lowest;
                    if (interval == 3) minorThird = true;
                    if (interval == 6) flatFifth = true;
                }

                if (!preview.empty()) preview += "  ";
                preview += noteName(lowest % 12);
                if (flatFifth) preview += "dim";
                else if (minorThird) preview += "m";
            }

            if (preview.empty()) preview = "(nothing to play - check the degrees)";
            ImGui::TextDisabled("%s", preview.c_str());
        }

        if (ImGui::Button("Write to pattern", ImVec2(-1, 0))) {
            std::vector<int> degrees;
            if (generators::progressionFromText(progressionText, degrees)) {
                g_UndoHistory.saveState(project, "Chord Progression");

                generators::ProgressionVoice voice;
                voice.degrees = degrees;
                voice.root = progressionRoot;
                voice.scaleType = progressionScale;
                voice.octave = progressionOctave;
                voice.voices = progressionVoices;
                voice.spread = progressionSpread;

                const std::vector<Note> notes =
                    generators::generateProgression(voice, progressionBars);
                for (const Note& note : notes) {
                    if (pattern.notes.size() >= size_t(Pattern::MAX_NOTES)) break;
                    pattern.notes.push_back(note);
                }
                seq.updateChannelConfigs();
            }
        }

        if (ImGui::BeginCombo("Preset##progression", "Common progressions...")) {
            for (const generators::ProgressionPreset& preset :
                 generators::progressionPresets()) {
                if (ImGui::Selectable(preset.name)) {
                    std::snprintf(progressionText, sizeof(progressionText), "%s",
                                  preset.degrees);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n\n%s", preset.degrees,
                                      preset.description);
                }
            }
            ImGui::EndCombo();
        }
    }

    // ========================================================================
    // 1. Drum Pattern Generator
    // ========================================================================
    if (GenreToolSection(ui, "Drum Pattern Generator", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* genres[] = {"Synthwave", "Outrun", "Darksynth", "Italo Disco", "Techno", "Retrowave"};
        ImGui::Combo("Genre", &g_ToolsDrumGenre, genres, IM_ARRAYSIZE(genres));
        ImGui::SliderFloat("Density", &g_ToolsDrumDensity, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Add Crash", &g_ToolsDrumAddCrash);

        if (ImGui::Button("Generate Drums")) {
            generateDrumPattern(pattern, g_ToolsDrumGenre, g_ToolsDrumDensity, g_ToolsDrumAddCrash, project.bpm);
            ui.selectedNoteIndices.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Variation##drum")) {
            // Regenerate with slight randomization
            g_ToolsDrumDensity += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.2f;
            g_ToolsDrumDensity = std::max(0.0f, std::min(1.0f, g_ToolsDrumDensity));
            generateDrumPattern(pattern, g_ToolsDrumGenre, g_ToolsDrumDensity, g_ToolsDrumAddCrash, project.bpm);
        }
        ImGui::Separator();
    }

    // ========================================================================
    // 2. Arpeggiator
    // ========================================================================
    if (GenreToolSection(ui, "Arpeggiator")) {
        const char* modes[] = {"Up", "Down", "Up-Down", "Random"};
        ImGui::Combo("Mode", &g_ToolsArpMode, modes, IM_ARRAYSIZE(modes));

        const char* rates[] = {"8th", "16th", "32nd"};
        ImGui::Combo("Rate", &g_ToolsArpRate, rates, IM_ARRAYSIZE(rates));

        ImGui::SliderInt("Octaves", &g_ToolsArpOctaves, 1, 4);
        ImGui::SliderFloat("Gate", &g_ToolsArpGate, 0.1f, 1.0f, "%.2f");

        if (ui.selectedNoteIndices.empty()) {
            ImGui::TextDisabled("Select notes first");
        } else {
            if (ImGui::Button("Apply Arp")) {
                applyArpeggiator(pattern, ui.selectedNoteIndices, g_ToolsArpMode,
                                g_ToolsArpRate, g_ToolsArpOctaves, g_ToolsArpGate);
                ui.selectedNoteIndices.clear();
            }
        }
        ImGui::Separator();
    }

    // ========================================================================
    // 3. Bass Pattern Generator
    // ========================================================================
    if (GenreToolSection(ui, "Bass Generator")) {
        const char* styles[] = {"Octave Pulse", "Root + Fifth", "Walking", "Arp Style"};
        ImGui::Combo("Style", &g_ToolsBassStyle, styles, IM_ARRAYSIZE(styles));

        const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        ImGui::Combo("Root", &g_ToolsBassRoot, notes, IM_ARRAYSIZE(notes));
        ImGui::SliderInt("Octave", &g_ToolsBassOctave, 1, 4);

        if (ImGui::Button("Generate Bass")) {
            // Clear existing bass notes or add to pattern
            generateBassPattern(pattern, g_ToolsBassStyle, g_ToolsBassRoot, g_ToolsBassOctave);
            ui.selectedNoteIndices.clear();
        }
        ImGui::Separator();
    }

    // ========================================================================
    // 4. Scale Lock + Highlighting
    // ========================================================================
    if (GenreToolSection(ui, "Scale Lock")) {
        const char* scales[] = {"Major", "Minor", "Dorian", "Phrygian", "Lydian",
                                "Mixolydian", "Locrian", "Harmonic Minor",
                                "Pentatonic Maj", "Pentatonic Min", "Blues"};
        ImGui::Combo("Scale", &g_ToolsScaleType, scales, IM_ARRAYSIZE(scales));

        const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        ImGui::Combo("Root##scale", &g_ToolsScaleRoot, notes, IM_ARRAYSIZE(notes));

        ImGui::Checkbox("Highlight In-Scale", &g_ToolsScaleHighlight);
        ImGui::Checkbox("Snap to Scale", &g_ToolsScaleLock);

        if (!ui.selectedNoteIndices.empty() && ImGui::Button("Snap Selected to Scale")) {
            for (int idx : ui.selectedNoteIndices) {
                if (idx >= 0 && idx < static_cast<int>(pattern.notes.size())) {
                    pattern.notes[idx].pitch = snapToScale(pattern.notes[idx].pitch,
                                                          g_ToolsScaleRoot, g_ToolsScaleType);
                }
            }
        }
        ImGui::Separator();
    }

    // ========================================================================
    // 5. Velocity Curve Painter
    // ========================================================================
    if (GenreToolSection(ui, "Velocity Curve")) {
        const char* curves[] = {"Linear", "Exponential", "Logarithmic", "S-Curve"};
        ImGui::Combo("Curve", &g_ToolsVelocityCurve, curves, IM_ARRAYSIZE(curves));
        ImGui::SliderFloat("Start", &g_ToolsVelocityStart, 0.1f, 1.0f);
        ImGui::SliderFloat("End", &g_ToolsVelocityEnd, 0.1f, 1.0f);

        if (ui.selectedNoteIndices.size() < 2) {
            ImGui::TextDisabled("Select 2+ notes");
        } else {
            if (ImGui::Button("Apply Curve")) {
                applyVelocityCurve(pattern, ui.selectedNoteIndices, g_ToolsVelocityCurve,
                                  g_ToolsVelocityStart, g_ToolsVelocityEnd);
            }
        }
        ImGui::Separator();
    }

    // ========================================================================
    // 6. Fill Generator
    // ========================================================================
    if (GenreToolSection(ui, "Fill Generator")) {
        const char* intensities[] = {"Light", "Medium", "Heavy"};
        ImGui::Combo("Intensity", &g_ToolsFillIntensity, intensities, IM_ARRAYSIZE(intensities));

        const char* fillStyles[] = {"Snare Roll", "Tom Fill", "Hi-Hat Roll", "Mixed"};
        ImGui::Combo("Style##fill", &g_ToolsFillStyle, fillStyles, IM_ARRAYSIZE(fillStyles));

        if (ImGui::Button("Add Fill at Bar 4")) {
            generateFill(pattern, 16.0f, g_ToolsFillIntensity, g_ToolsFillStyle);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Fill at Bar 8")) {
            generateFill(pattern, 32.0f, g_ToolsFillIntensity, g_ToolsFillStyle);
        }
        ImGui::Separator();
    }

    // ========================================================================
    // 7. Pattern Variation
    // ========================================================================
    if (GenreToolSection(ui, "Pattern Variation")) {
        ImGui::SliderFloat("Amount", &g_ToolsVariationAmount, 0.0f, 1.0f);
        ImGui::Checkbox("Vary Timing", &g_ToolsVariationTiming);
        ImGui::Checkbox("Vary Velocity", &g_ToolsVariationVelocity);
        ImGui::Checkbox("Vary Pitch", &g_ToolsVariationPitch);

        if (ImGui::Button("Create Variation")) {
            createVariation(pattern, g_ToolsVariationAmount, g_ToolsVariationTiming,
                           g_ToolsVariationVelocity, g_ToolsVariationPitch);
        }
        ImGui::Separator();
    }

    // ========================================================================
    // 8. Quick Layer
    // ========================================================================
    if (GenreToolSection(ui, "Quick Layer")) {
        ImGui::SliderInt("Octave Offset", &g_ToolsLayerOctave, -2, 2);
        ImGui::SliderFloat("Detune (cents)", &g_ToolsLayerDetune, 0.0f, 50.0f);
        ImGui::SliderFloat("Layer Volume", &g_ToolsLayerVelocity, 0.1f, 1.0f);

        if (ui.selectedNoteIndices.empty()) {
            ImGui::TextDisabled("Select notes first");
        } else {
            if (ImGui::Button("Add Layer")) {
                quickLayer(pattern, ui.selectedNoteIndices, g_ToolsLayerOctave,
                          g_ToolsLayerDetune, g_ToolsLayerVelocity);
            }
        }
        ImGui::Separator();
    }

    // ========================================================================
    // 9. Humanize Selected
    // ========================================================================
    if (GenreToolSection(ui, "Humanize")) {
        ImGui::SliderFloat("Timing Var", &g_ToolsHumanizeTiming, 0.0f, 0.1f, "%.3f beats");
        ImGui::SliderFloat("Velocity Var", &g_ToolsHumanizeVelocity, 0.0f, 0.5f);

        if (ui.selectedNoteIndices.empty()) {
            ImGui::TextDisabled("Select notes first");
        } else {
            if (ImGui::Button("Humanize Selected")) {
                humanizeSelected(pattern, ui.selectedNoteIndices,
                                g_ToolsHumanizeTiming, g_ToolsHumanizeVelocity);
            }
        }
    }

    ImGui::End();
}

// ============================================================================
// Spectrum Analyzer Window
// ============================================================================
inline void renderSpectrumAnalyzer(Sequencer& seq) {
    ImGui::SetNextWindowSize(ImVec2(800, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Spectrum Analyzer", nullptr, ImGuiWindowFlags_NoCollapse);

    auto& analyzer = seq.getSpectrumAnalyzer();

    // The analyzer does its FFT here, on the UI thread, at whatever rate we
    // redraw. The audio thread only fills a ring buffer.
    analyzer.update();

    // Get all magnitudes
    std::vector<float> magnitudes = analyzer.getAllMagnitudes();

    if (magnitudes.empty()) {
        ImGui::Text("No audio data");
        ImGui::End();
        return;
    }

    // Draw spectrum as bars
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.y < 50) canvas_size.y = 200;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Background
    draw_list->AddRectFilled(canvas_pos,
                            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                            IM_COL32(20, 20, 30, 255));

    // Frequency bins to display (20 Hz to 20 kHz)
    const int numBinsToShow = std::min(512, (int)magnitudes.size());

    // Logarithmic frequency scale for better visualization
    const float minFreq = 20.0f;
    const float maxFreq = 20000.0f;

    // Draw frequency bars
    float barWidth = canvas_size.x / numBinsToShow;

    for (int i = 0; i < numBinsToShow; i++) {
        // Logarithmic bin mapping for perceptually uniform display
        float logPos = (float)i / numBinsToShow;
        float freq = minFreq * std::pow(maxFreq / minFreq, logPos);
        int binIdx = analyzer.frequencyToBin(freq);

        if (binIdx >= 0 && binIdx < (int)magnitudes.size()) {
            float magnitude = magnitudes[binIdx];

            // Bar height
            float barHeight = magnitude * canvas_size.y;

            // Color gradient (green -> yellow -> red)
            ImU32 color;
            if (magnitude < 0.5f) {
                // Green to yellow
                float t = magnitude / 0.5f;
                color = IM_COL32(
                    (int)(0 + t * 255),
                    255,
                    0,
                    200
                );
            } else {
                // Yellow to red
                float t = (magnitude - 0.5f) / 0.5f;
                color = IM_COL32(
                    255,
                    (int)(255 - t * 255),
                    0,
                    200
                );
            }

            // Draw bar from bottom
            float x = canvas_pos.x + i * barWidth;
            float yTop = canvas_pos.y + canvas_size.y - barHeight;
            float yBottom = canvas_pos.y + canvas_size.y;

            draw_list->AddRectFilled(
                ImVec2(x, yTop),
                ImVec2(x + barWidth - 1, yBottom),
                color
            );
        }
    }

    // Draw frequency labels
    const char* freqLabels[] = { "20Hz", "100Hz", "1kHz", "10kHz", "20kHz" };
    const float freqValues[] = { 20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f };

    for (int i = 0; i < 5; i++) {
        float logPos = std::log(freqValues[i] / minFreq) / std::log(maxFreq / minFreq);
        float x = canvas_pos.x + logPos * canvas_size.x;

        // Vertical line
        draw_list->AddLine(
            ImVec2(x, canvas_pos.y),
            ImVec2(x, canvas_pos.y + canvas_size.y),
            IM_COL32(80, 80, 100, 100),
            1.0f
        );

        // Label
        draw_list->AddText(
            ImVec2(x + 2, canvas_pos.y + 5),
            IM_COL32(180, 180, 200, 255),
            freqLabels[i]
        );
    }

    // Dummy item to reserve space
    ImGui::Dummy(canvas_size);

    // Display peak frequency
    float peakFreq = analyzer.getPeakFrequency();
    ImGui::Text("Peak Frequency: %.1f Hz", peakFreq);

    ImGui::End();
}

// ============================================================================
// MIDI Input Window
// ============================================================================
inline void renderMIDIInput(Sequencer& seq, UIState& uiState) {
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("MIDI Input", nullptr, ImGuiWindowFlags_NoCollapse);

    auto& midiInput = seq.getMIDIInput();

    // Device selection
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "MIDI Device:");
    ImGui::SameLine();

    auto devices = midiInput.getDevices();
    int currentDevice = midiInput.getCurrentDevice();

    if (devices.empty()) {
        ImGui::Text("No MIDI devices found");
    } else {
        std::vector<const char*> deviceNames;
        for (const auto& dev : devices) {
            deviceNames.push_back(dev.name.c_str());
        }

        int selectedIdx = currentDevice;
        if (selectedIdx < 0) selectedIdx = 0;

        if (ImGui::Combo("##midiDevice", &selectedIdx, deviceNames.data(), (int)deviceNames.size())) {
            if (midiInput.openDevice(selectedIdx)) {
                // Successfully opened
            } else {
                ImGui::OpenPopup("MIDI Error");
            }
        }
    }

    // Error popup
    if (ImGui::BeginPopupModal("MIDI Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Failed to open MIDI device:");
        ImGui::TextWrapped("%s", midiInput.getErrorMessage().c_str());
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // Recording controls
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Recording:");

    const char* recordModeItems[] = { "Off", "Replace", "Overdub" };
    int recordModeIdx = (int)midiInput.getRecordMode();
    if (ImGui::Combo("Record Mode", &recordModeIdx, recordModeItems, 3)) {
        midiInput.setRecordMode((MIDIInput::RecordMode)recordModeIdx);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off: Just play through\nReplace: Erase and record new\nOverdub: Add to existing notes");
    }

    // Quantization
    bool quantizeEnabled = midiInput.isQuantizeEnabled();
    if (ImGui::Checkbox("Quantize", &quantizeEnabled)) {
        midiInput.setQuantizeEnabled(quantizeEnabled);
    }

    if (quantizeEnabled) {
        ImGui::SameLine();
        const char* gridItems[] = { "1/4", "1/8", "1/16", "1/32" };
        float gridValues[] = { 1.0f, 0.5f, 0.25f, 0.125f };

        float currentGrid = midiInput.getQuantization();
        int gridIdx = 2; // Default to 1/16
        for (int i = 0; i < 4; i++) {
            if (std::abs(currentGrid - gridValues[i]) < 0.01f) {
                gridIdx = i;
                break;
            }
        }

        if (ImGui::Combo("Grid", &gridIdx, gridItems, 4)) {
            midiInput.setQuantization(gridValues[gridIdx]);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Quantize recorded notes to grid");
        }
    }

    ImGui::Separator();

    // Input channel selection
    ImGui::Text("Record to Channel:");
    ImGui::SameLine();
    int channel = uiState.selectedChannel;
    if (ImGui::SliderInt("##inputChannel", &channel, 0, 7, "Ch %d")) {
        uiState.selectedChannel = channel;
    }

    // Status
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Status:");

    if (currentDevice >= 0 && currentDevice < (int)devices.size()) {
        ImGui::Text("Connected: %s", devices[currentDevice].name.c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No device connected");
    }

    if (midiInput.getRecordMode() != MIDIInput::RecordMode::Off) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "RECORDING");
    }

    ImGui::End();
}

// ============================================================================
// Automation Editor Window
// ============================================================================
inline void renderAutomation(Project& project, UIState& uiState, const PlaybackState& playbackState) {
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Automation", nullptr, ImGuiWindowFlags_NoCollapse);

    // ========================================================================
    // Toolbar - Add/Remove Lanes
    // ========================================================================
    if (ImGui::Button("+ Add Lane")) {
        AutomationLane newLane;
        newLane.name = "New Automation";
        newLane.channelIndex = uiState.selectedChannel;
        newLane.target = AutomationTarget::Volume;
        newLane.color = 0xFF00AAFF + (project.automationLanes.size() * 0x112233);
        project.automationLanes.push_back(newLane);
    }
    ImGui::SameLine();

    static int selectedLaneIndex = -1;
    if (selectedLaneIndex >= 0 && selectedLaneIndex < (int)project.automationLanes.size()) {
        if (ImGui::Button("- Remove Lane")) {
            project.automationLanes.erase(project.automationLanes.begin() + selectedLaneIndex);
            selectedLaneIndex = -1;
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("- Remove Lane");
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::Text("| Lanes: %zu", project.automationLanes.size());

    ImGui::Separator();

    // ========================================================================
    // Lane List + Editor (Split view)
    // ========================================================================
    ImGui::BeginChild("LaneList", ImVec2(250, 0), true);
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Automation Lanes:");

    for (int i = 0; i < (int)project.automationLanes.size(); i++) {
        auto& lane = project.automationLanes[i];

        // Lane item (selectable)
        bool isSelected = (selectedLaneIndex == i);
        ImVec4 laneColor = ImColor(lane.color);

        ImGui::PushID(i);
        if (ImGui::Selectable(lane.getDisplayName().c_str(), isSelected)) {
            selectedLaneIndex = i;
        }

        // Show enabled checkbox
        ImGui::SameLine(220);
        if (ImGui::Checkbox("##enabled", &lane.enabled)) {
            // Toggle enabled
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    // ========================================================================
    // Lane Editor (Right panel)
    // ========================================================================
    ImGui::SameLine();
    ImGui::BeginChild("LaneEditor", ImVec2(0, 0), true);

    if (selectedLaneIndex >= 0 && selectedLaneIndex < (int)project.automationLanes.size()) {
        auto& lane = project.automationLanes[selectedLaneIndex];
        auto& curve = lane.curve;

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Lane Settings:");
        ImGui::Separator();

        // Channel selection
        const char* channelNames[] = { "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8", "Master" };
        int channelIdx = lane.channelIndex < 0 ? 8 : lane.channelIndex;
        if (ImGui::Combo("Channel", &channelIdx, channelNames, 9)) {
            lane.channelIndex = (channelIdx == 8) ? -1 : channelIdx;
        }

        // Parameter selection
        const char* parameterNames[] = {
            "Volume", "Pan", "Filter Cutoff", "Filter Resonance",
            "Reverb Mix", "Delay Mix", "Chorus Mix", "Distortion Drive",
            "Bitcrusher", "Phaser Rate", "Flanger Rate", "Tremolo Rate",
            "Compressor", "EQ Low", "EQ Mid", "EQ High",
            "Stereo Width", "Tape Drive",
            "Master Volume", "Master EQ Low", "Master EQ Mid", "Master EQ High",
            "Master Compressor", "Master Limiter"
        };
        int targetIdx = (int)lane.target;
        if (ImGui::Combo("Parameter", &targetIdx, parameterNames, 24)) {
            lane.target = (AutomationTarget)targetIdx;
        }

        // Interpolation type
        const char* interpNames[] = { "Linear", "Bezier", "Step" };
        int interpIdx = (int)curve.interpolation;
        if (ImGui::Combo("Interpolation", &interpIdx, interpNames, 3)) {
            curve.interpolation = (InterpolationType)interpIdx;
        }

        ImGui::Separator();

        // ====================================================================
        // Curve Editor Canvas
        // ====================================================================
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Curve Editor:");
        ImGui::Text("Click to add points | Right-click point to delete | Drag to move");

        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        ImVec2 canvas_sz = ImVec2(500, 200);
        if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
        if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;
        ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(20, 20, 30, 255));
        draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(100, 100, 120, 255));

        // Grid lines
        for (int i = 0; i <= 4; i++) {
            float y = canvas_p0.y + (canvas_sz.y * i / 4);
            draw_list->AddLine(ImVec2(canvas_p0.x, y), ImVec2(canvas_p1.x, y), IM_COL32(50, 50, 60, 150));
        }
        for (int i = 0; i <= 8; i++) {
            float x = canvas_p0.x + (canvas_sz.x * i / 8);
            draw_list->AddLine(ImVec2(x, canvas_p0.y), ImVec2(x, canvas_p1.y), IM_COL32(50, 50, 60, 150));
        }

        // Playback cursor
        float songLength = project.songLength;
        if (songLength > 0.0f) {
            float cursorX = canvas_p0.x + (playbackState.currentBeat / songLength) * canvas_sz.x;
            draw_list->AddLine(ImVec2(cursorX, canvas_p0.y), ImVec2(cursorX, canvas_p1.y), IM_COL32(255, 100, 100, 200), 2.0f);
        }

        ImGui::InvisibleButton("canvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool isHovered = ImGui::IsItemHovered();
        const bool isActive = ImGui::IsItemActive();
        const ImVec2 mousePos = ImGui::GetIO().MousePos;

        // Draw automation curve
        if (curve.points.size() >= 2) {
            for (size_t i = 0; i < curve.points.size() - 1; i++) {
                const auto& p1 = curve.points[i];
                const auto& p2 = curve.points[i + 1];

                float x1 = canvas_p0.x + (p1.time / songLength) * canvas_sz.x;
                float y1 = canvas_p1.y - (p1.value * canvas_sz.y);
                float x2 = canvas_p0.x + (p2.time / songLength) * canvas_sz.x;
                float y2 = canvas_p1.y - (p2.value * canvas_sz.y);

                // Draw interpolated curve (multiple line segments)
                int segments = 20;
                for (int seg = 0; seg < segments; seg++) {
                    float t1 = (float)seg / segments;
                    float t2 = (float)(seg + 1) / segments;

                    float time1 = p1.time + t1 * (p2.time - p1.time);
                    float time2 = p1.time + t2 * (p2.time - p1.time);

                    float val1 = curve.evaluate(time1);
                    float val2 = curve.evaluate(time2);

                    float sx1 = canvas_p0.x + (time1 / songLength) * canvas_sz.x;
                    float sy1 = canvas_p1.y - (val1 * canvas_sz.y);
                    float sx2 = canvas_p0.x + (time2 / songLength) * canvas_sz.x;
                    float sy2 = canvas_p1.y - (val2 * canvas_sz.y);

                    draw_list->AddLine(ImVec2(sx1, sy1), ImVec2(sx2, sy2), IM_COL32(0, 200, 255, 255), 2.0f);
                }
            }
        }

        // Draw automation points
        static int draggingPointIndex = -1;
        for (int i = 0; i < (int)curve.points.size(); i++) {
            const auto& point = curve.points[i];
            float px = canvas_p0.x + (point.time / songLength) * canvas_sz.x;
            float py = canvas_p1.y - (point.value * canvas_sz.y);

            bool isPointHovered = (mousePos.x - px) * (mousePos.x - px) + (mousePos.y - py) * (mousePos.y - py) < 36.0f;

            // Draw point
            ImU32 pointColor = isPointHovered ? IM_COL32(255, 255, 0, 255) : IM_COL32(255, 150, 0, 255);
            draw_list->AddCircleFilled(ImVec2(px, py), 5.0f, pointColor);
            draw_list->AddCircle(ImVec2(px, py), 5.0f, IM_COL32(255, 255, 255, 200), 12, 1.5f);

            // Drag point
            if (isHovered && isActive && isPointHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                draggingPointIndex = i;
            }

            // Delete point (right-click)
            if (isHovered && isPointHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                curve.removePoint(i);
                break;
            }
        }

        // Handle dragging
        if (draggingPointIndex >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float newTime = ((mousePos.x - canvas_p0.x) / canvas_sz.x) * songLength;
            float newValue = 1.0f - ((mousePos.y - canvas_p0.y) / canvas_sz.y);
            newTime = std::clamp(newTime, 0.0f, songLength);
            newValue = std::clamp(newValue, 0.0f, 1.0f);
            curve.movePoint(draggingPointIndex, newTime, newValue);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            draggingPointIndex = -1;
        }

        // Add new point (click on canvas)
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            bool clickedOnPoint = false;
            for (int i = 0; i < (int)curve.points.size(); i++) {
                const auto& point = curve.points[i];
                float px = canvas_p0.x + (point.time / songLength) * canvas_sz.x;
                float py = canvas_p1.y - (point.value * canvas_sz.y);
                if ((mousePos.x - px) * (mousePos.x - px) + (mousePos.y - py) * (mousePos.y - py) < 36.0f) {
                    clickedOnPoint = true;
                    break;
                }
            }

            if (!clickedOnPoint) {
                float newTime = ((mousePos.x - canvas_p0.x) / canvas_sz.x) * songLength;
                float newValue = 1.0f - ((mousePos.y - canvas_p0.y) / canvas_sz.y);
                newTime = std::clamp(newTime, 0.0f, songLength);
                newValue = std::clamp(newValue, 0.0f, 1.0f);
                curve.addPoint(newTime, newValue);
            }
        }

        ImGui::Separator();

        // Point list
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Points: %zu", curve.points.size());
        if (ImGui::Button("Clear All Points")) {
            curve.clear();
        }

    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Select a lane to edit");
    }

    ImGui::EndChild();

    ImGui::End();
}

// ============================================================================
// Wavetable Editor Window
// ============================================================================
inline void renderWavetableEditor(Project& project, UIState& uiState,
                                  Sequencer& seq) {
    ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Wavetable Editor", nullptr, ImGuiWindowFlags_NoCollapse);

    /*
     * Republish the band-limited tables when anything here changes them.
     *
     * Rebuilding is seven FFTs per table, so it happens when the drawing
     * settles rather than on every mouse-move sample: ImGui reports the end
     * of a drag, and that is the moment to do it. Without this the editor
     * would still be a drawing toy - a better connected one, but the sound
     * would not follow the picture until something else happened to
     * reconfigure the channels.
     */
    struct WavetableRepublish {
        Sequencer& sequencer;
        bool dirty = false;
        ~WavetableRepublish() { if (dirty) sequencer.updateWavetables(); }
    } republish{seq};

    ImGui::TextDisabled(
        "Draw a waveform here, then set a channel's oscillator to Custom "
        "and point it at this bank.");
    ImGui::Separator();

    // Ensure at least one bank exists
    if (project.wavetableBanks.empty()) {
        project.wavetableBanks.push_back(WavetableBank());
    }

    // ========================================================================
    // Bank Selection
    // ========================================================================
    static int selectedBankIndex = 0;
    static int selectedTableIndex = 0;

    if (selectedBankIndex >= (int)project.wavetableBanks.size()) {
        selectedBankIndex = 0;
    }

    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Wavetable Bank:");
    ImGui::SameLine();

    std::vector<const char*> bankNames;
    for (const auto& bank : project.wavetableBanks) {
        bankNames.push_back(bank.name.c_str());
    }

    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("##bank", &selectedBankIndex, bankNames.data(), (int)bankNames.size())) {
        selectedTableIndex = 0;
    }

    ImGui::SameLine();
    if (ImGui::Button("+ New Bank")) {
        project.wavetableBanks.push_back(WavetableBank());
        selectedBankIndex = (int)project.wavetableBanks.size() - 1;
        selectedTableIndex = 0;
    }

    if (project.wavetableBanks.size() > 1) {
        ImGui::SameLine();
        if (ImGui::Button("- Delete Bank")) {
            project.wavetableBanks.erase(project.wavetableBanks.begin() + selectedBankIndex);
            selectedBankIndex = std::clamp(selectedBankIndex, 0, (int)project.wavetableBanks.size() - 1);
        }
    }

    auto& currentBank = project.wavetableBanks[selectedBankIndex];

    ImGui::Separator();

    // ========================================================================
    // Wavetable Selection within Bank
    // ========================================================================
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Wavetables in Bank:");

    ImGui::BeginChild("WavetableList", ImVec2(200, 150), true);
    for (int i = 0; i < (int)currentBank.tables.size(); i++) {
        bool isSelected = (i == selectedTableIndex);
        if (ImGui::Selectable(currentBank.tables[i].name.c_str(), isSelected)) {
            selectedTableIndex = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button("+ Add Table", ImVec2(120, 0))) {
        if (currentBank.tables.size() < WavetableBank::MAX_TABLES) {
            Wavetable newTable;
            newTable.name = "New " + std::to_string(currentBank.tables.size() + 1);
            currentBank.addTable(newTable);
            selectedTableIndex = (int)currentBank.tables.size() - 1;
        }
    }
    if (currentBank.tables.size() > 1) {
        if (ImGui::Button("- Remove Table", ImVec2(120, 0))) {
            if (selectedTableIndex < (int)currentBank.tables.size()) {
                currentBank.tables.erase(currentBank.tables.begin() + selectedTableIndex);
                selectedTableIndex = std::clamp(selectedTableIndex, 0, (int)currentBank.tables.size() - 1);
            }
        }
    }
    ImGui::EndGroup();

    if (currentBank.tables.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "No wavetables in bank!");
        ImGui::End();
        return;
    }

    if (selectedTableIndex >= (int)currentBank.tables.size()) {
        selectedTableIndex = 0;
    }

    auto& currentTable = currentBank.tables[selectedTableIndex];

    ImGui::Separator();

    // ========================================================================
    // Waveform Drawing Canvas
    // ========================================================================
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Waveform Editor:");
    ImGui::Text("Click and drag to draw | Right-click to erase");

    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImVec2(650, 200);
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(20, 20, 30, 255));
    draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(100, 100, 120, 255));

    // Grid lines
    float centerY = canvas_p0.y + canvas_sz.y * 0.5f;
    draw_list->AddLine(ImVec2(canvas_p0.x, centerY), ImVec2(canvas_p1.x, centerY), IM_COL32(80, 80, 90, 255));

    for (int i = 1; i <= 4; i++) {
        float y = centerY + (canvas_sz.y * 0.25f * i);
        draw_list->AddLine(ImVec2(canvas_p0.x, y), ImVec2(canvas_p1.x, y), IM_COL32(50, 50, 60, 150));
        y = centerY - (canvas_sz.y * 0.25f * i);
        draw_list->AddLine(ImVec2(canvas_p0.x, y), ImVec2(canvas_p1.x, y), IM_COL32(50, 50, 60, 150));
    }

    // Draw waveform
    for (int i = 0; i < Wavetable::TABLE_SIZE - 1; i++) {
        float x0 = canvas_p0.x + (float)i / Wavetable::TABLE_SIZE * canvas_sz.x;
        float y0 = centerY - currentTable.samples[i] * canvas_sz.y * 0.45f;
        float x1 = canvas_p0.x + (float)(i + 1) / Wavetable::TABLE_SIZE * canvas_sz.x;
        float y1 = centerY - currentTable.samples[i + 1] * canvas_sz.y * 0.45f;

        draw_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 200, 255, 255), 2.0f);
    }

    // Handle mouse input for drawing
    ImGui::SetCursorScreenPos(canvas_p0);
    ImGui::InvisibleButton("##wavetable_canvas", canvas_sz);

    if (ImGui::IsItemActive()) {
        ImVec2 mousePos = ImGui::GetMousePos();
        if (mousePos.x >= canvas_p0.x && mousePos.x <= canvas_p1.x &&
            mousePos.y >= canvas_p0.y && mousePos.y <= canvas_p1.y) {

            int index = (int)((mousePos.x - canvas_p0.x) / canvas_sz.x * Wavetable::TABLE_SIZE);
            index = std::clamp(index, 0, Wavetable::TABLE_SIZE - 1);

            float value = -(mousePos.y - centerY) / (canvas_sz.y * 0.45f);
            value = std::clamp(value, -1.0f, 1.0f);

            // Left mouse = draw, right mouse = erase
            if (ImGui::IsMouseDown(0)) {
                currentTable.samples[index] = value;
                // Smooth nearby samples for better drawing
                if (index > 0) currentTable.samples[index - 1] = (currentTable.samples[index - 1] + value) * 0.5f;
                if (index < Wavetable::TABLE_SIZE - 1) currentTable.samples[index + 1] = (currentTable.samples[index + 1] + value) * 0.5f;
            } else if (ImGui::IsMouseDown(1)) {
                currentTable.samples[index] = 0.0f;
            }
        }
    }

    ImGui::Dummy(ImVec2(0, 10)); // Spacer

    // ========================================================================
    // Preset Buttons
    // ========================================================================
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Initialize Preset:");
    if (ImGui::Button("Sine", ImVec2(100, 0))) {
        currentTable.initSine();
    }
    ImGui::SameLine();
    if (ImGui::Button("Saw", ImVec2(100, 0))) {
        currentTable.initSaw();
    }
    ImGui::SameLine();
    if (ImGui::Button("Square", ImVec2(100, 0))) {
        currentTable.initSquare();
    }
    ImGui::SameLine();
    if (ImGui::Button("Triangle", ImVec2(100, 0))) {
        currentTable.initTriangle();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(100, 0))) {
        currentTable.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Normalize", ImVec2(100, 0))) {
        currentTable.normalize();
    }

    ImGui::Separator();

    // ========================================================================
    // Wave tools
    //
    // Editing rather than drawing. Every one of these was previously a
    // matter of redrawing 256 samples by hand.
    // ========================================================================
    {
        float* samples = currentTable.samples.data();
        constexpr int SIZE = Wavetable::TABLE_SIZE;

        static int s_interp = static_cast<int>(WaveInterp::Linear);
        bool edited = false;

        if (CollapsingHeaderExpandable("Wave Tools", g_ExpandWaveTools)) {
            ImGui::SetNextItemWidth(160);
            ImGui::Combo("Interpolation", &s_interp,
                         "None (stepped)\0Linear\0Cosine\0Cubic\0");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How Scale X reads between samples.\n"
                    "None is stepped, and is the honest choice for chip work.");
            }

            const WaveInterp mode =
                static_cast<WaveInterp>(std::clamp(s_interp, 0,
                                                   WAVE_INTERP_COUNT - 1));

            if (ImGui::Button("x2 cycles", ImVec2(100, 0))) {
                waveScaleX(samples, SIZE, 2.0f, mode);
                edited = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Two cycles of this wave in the table - an "
                                  "octave up without changing the note");
            }
            ImGui::SameLine();
            if (ImGui::Button("Half", ImVec2(100, 0))) {
                waveScaleX(samples, SIZE, 0.5f, mode);
                edited = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Shift left", ImVec2(100, 0))) {
                waveOffsetX(samples, SIZE, -SIZE / 16);
                edited = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Shift right", ImVec2(100, 0))) {
                waveOffsetX(samples, SIZE, SIZE / 16);
                edited = true;
            }

            if (ImGui::Button("Smooth", ImVec2(100, 0))) {
                waveSmooth(samples, SIZE, 2);
                edited = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Invert", ImVec2(100, 0))) {
                waveInvert(samples, SIZE);
                edited = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reverse", ImVec2(100, 0))) {
                waveReverse(samples, SIZE);
                edited = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove DC", ImVec2(100, 0))) {
                waveRemoveDC(samples, SIZE);
                edited = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "A wave that does not average to zero thumps on every\n"
                    "note and pulls on anything downstream with memory.\n"
                    "Inaudible as a tone, very audible as everything else\n"
                    "misbehaving.");
            }

            ImGui::Separator();

            /*
             * The chip constraints, made real.
             *
             * Not an effect that sounds a bit like hardware: the output only
             * contains values a real wave channel could store. The Game Boy
             * keeps 32 samples of 4 bits, which is the default here.
             */
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Fit to hardware:");

            static int s_steps = 32;
            static int s_levels = 16;

            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Steps", &s_steps, 4, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Levels", &s_levels, 2, 64);
            ImGui::SameLine();
            if (ImGui::Button("Apply##chipfit", ImVec2(80, 0))) {
                waveFitToChip(samples, SIZE, s_steps, s_levels);
                edited = true;
            }

            ImGui::TextDisabled(
                "Game Boy: 32 steps, 16 levels.  PC Engine: 32 steps, 32 "
                "levels.  NES DPCM-era wave: 16 steps.");
        }

        // ====================================================================
        // Shapes - build a wave out of parts instead of drawing it
        // ====================================================================
        static WaveShapeSpec s_shape;
        static bool s_shapeLive = false;

        if (CollapsingHeaderExpandable("Shapes", g_ExpandWaveTools)) {
            ImGui::TextDisabled(
                "Stack whole waveforms at chosen harmonics. The exponent "
                "bends a component toward its extremes without the aliasing "
                "that clipping one would produce.");

            bool shapeChanged = false;

            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderInt("Components", &s_shape.count, 1,
                                 MAX_WAVE_SHAPES)) {
                shapeChanged = true;
            }

            for (int c = 0; c < s_shape.count; ++c) {
                WaveShapeComponent& part = s_shape.components[c];
                ImGui::PushID(3300 + c);
                ImGui::Separator();

                ImGui::SetNextItemWidth(110);
                if (ImGui::Combo("##kind", &part.kind,
                                 "Sine\0Triangle\0Saw\0Pulse\0Noise\0")) {
                    shapeChanged = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110);
                if (ImGui::SliderInt("Harm", &part.harmonic, 1, 32)) {
                    shapeChanged = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110);
                if (ImGui::SliderFloat("Amp", &part.amplitude, -1.0f, 1.0f,
                                       "%.2f")) {
                    shapeChanged = true;
                }

                ImGui::SetNextItemWidth(110);
                if (ImGui::SliderFloat("Phase", &part.phase, 0.0f, 1.0f, "%.2f")) {
                    shapeChanged = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110);
                if (ImGui::SliderFloat("Exp", &part.exponent, 0.2f, 5.0f, "%.2f")) {
                    shapeChanged = true;
                }
                if (static_cast<WaveShapeKind>(part.kind) == WaveShapeKind::Pulse) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(110);
                    if (ImGui::SliderFloat("Duty", &part.duty, 0.01f, 0.99f,
                                           "%.2f")) {
                        shapeChanged = true;
                    }
                }
                ImGui::PopID();
            }

            ImGui::Separator();
            if (ImGui::Checkbox("Normalise", &s_shape.normalise)) shapeChanged = true;
            ImGui::SameLine();
            ImGui::Checkbox("Update as I move things", &s_shapeLive);

            ImGui::SetNextItemWidth(110);
            if (ImGui::SliderInt("Fit steps", &s_shape.steps, 0, 128)) {
                shapeChanged = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            if (ImGui::SliderInt("Fit levels", &s_shape.levels, 0, 64)) {
                shapeChanged = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(0 = leave alone)");

            if (ImGui::Button("Build into this table", ImVec2(180, 0)) ||
                (s_shapeLive && shapeChanged)) {
                renderWaveShapes(s_shape, samples, SIZE);
                edited = true;
            }
        }

        // One republish for whatever the frame's buttons did. Rebuilding the
        // band-limited mips is seven FFTs per table, so it happens once here
        // rather than inside each button.
        if (edited) republish.dirty = true;
    }

    ImGui::Separator();

    // ========================================================================
    // Morph Control (if multiple tables in bank)
    // ========================================================================
    if (currentBank.tables.size() > 1) {
        static float morphPosition = 0.0f;
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Morph Position:");
        ImGui::SliderFloat("##morph", &morphPosition, 0.0f, 1.0f, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Morphs between %d wavetables in the bank", (int)currentBank.tables.size());
        }

        // Preview morphed waveform
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Morphed Waveform Preview:");
        ImVec2 preview_p0 = ImGui::GetCursorScreenPos();
        ImVec2 preview_sz = ImVec2(650, 100);
        ImVec2 preview_p1 = ImVec2(preview_p0.x + preview_sz.x, preview_p0.y + preview_sz.y);

        draw_list->AddRectFilled(preview_p0, preview_p1, IM_COL32(20, 20, 30, 255));
        draw_list->AddRect(preview_p0, preview_p1, IM_COL32(100, 100, 120, 255));

        float previewCenterY = preview_p0.y + preview_sz.y * 0.5f;
        draw_list->AddLine(ImVec2(preview_p0.x, previewCenterY), ImVec2(preview_p1.x, previewCenterY), IM_COL32(80, 80, 90, 255));

        // Draw morphed waveform
        for (int i = 0; i < 256; i++) {
            float phase = (float)i / 256.0f;
            float sample = currentBank.lookupMorph(phase, morphPosition);

            float x = preview_p0.x + (float)i / 256.0f * preview_sz.x;
            float y = previewCenterY - sample * preview_sz.y * 0.45f;

            if (i > 0) {
                float prevPhase = (float)(i - 1) / 256.0f;
                float prevSample = currentBank.lookupMorph(prevPhase, morphPosition);
                float prevX = preview_p0.x + (float)(i - 1) / 256.0f * preview_sz.x;
                float prevY = previewCenterY - prevSample * preview_sz.y * 0.45f;

                draw_list->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), IM_COL32(255, 150, 0, 255), 2.0f);
            }
        }

        ImGui::Dummy(preview_sz);
    }

    /*
     * Rebuild when the tables have actually changed.
     *
     * Driven by a checksum of the data rather than by flagging each of the
     * two dozen controls that can write to it: that sort of list grows one
     * stale entry and silently stops working, which is precisely how the
     * editor came to be disconnected from the engine in the first place.
     * A few thousand floats a frame is nothing next to seven FFTs per table.
     */
    {
        double checksum = 0.0;
        for (size_t b = 0; b < project.wavetableBanks.size() &&
                           b < WavetableLibrary::MAX_BANKS; ++b) {
            const WavetableBank& bank = project.wavetableBanks[b];
            checksum += double(bank.tables.size()) * 7919.0;
            for (size_t t = 0; t < bank.tables.size(); ++t) {
                for (int i = 0; i < Wavetable::TABLE_SIZE; i += 4) {
                    checksum += double(bank.tables[t].samples[size_t(i)]) *
                                double(i + 1) * double(t + 1);
                }
            }
        }

        static double lastChecksum = 0.0;
        static bool primed = false;
        if (!primed || checksum != lastChecksum) {
            republish.dirty = primed;   // not on the very first draw
            lastChecksum = checksum;
            primed = true;
        }
    }

    ImGui::End();
}

// Export scale highlighting state for piano roll
inline bool isNoteHighlighted(int pitch) {
    if (!g_ToolsScaleHighlight) return false;
    return isNoteInScale(pitch, g_ToolsScaleRoot, g_ToolsScaleType);
}

inline bool shouldSnapToScale() {
    return g_ToolsScaleLock;
}

inline int getScaleSnappedPitch(int pitch) {
    if (!g_ToolsScaleLock) return pitch;
    return snapToScale(pitch, g_ToolsScaleRoot, g_ToolsScaleType);
}

/*
 * The lesson panel.
 *
 * Floating on purpose, and never dockable: the lesson follows the user
 * across views, and docking it into one workspace would strand it there.
 *
 * The rules it renders under: an action step shows its goal and waits -
 * there is no button that claims completion without evidence - but Skip is
 * always present, because a tutorial you cannot leave is a hostage
 * situation. Once a goal is met it stays met (latched in the progress
 * mask), so deleting notes later cannot walk the lesson backwards.
 */
inline void DrawTutorialPanel(Project& project, UIState& ui,
                              bool isPlaying, bool loopRangeActive) {
    if (!g_Tutorial.active) return;

    if (isPlaying) g_Tutorial.hasPlayed = true;

    TutorialContext context;
    context.project = &project;
    context.hasPlayed = g_Tutorial.hasPlayed;
    context.loopRangeActive = loopRangeActive;
    context.projectSaved = !ui.projectFilePath.empty();

    const bool currentDone = updateTutorial(g_Tutorial, context);

    int stepCount = 0;
    const TutorialStep* steps = tutorialSteps(stepCount);
    if (g_Tutorial.step >= stepCount) {
        g_Tutorial.active = false;
        return;
    }
    const TutorialStep& step = steps[g_Tutorial.step];

    // Raise the window a step points at, once per step.
    if (g_Tutorial.focusedStep != g_Tutorial.step) {
        if (step.focusWindow != nullptr) {
            ImGui::SetWindowFocus(step.focusWindow);
        }
        g_Tutorial.focusedStep = g_Tutorial.step;
    }

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 420.0f, 60.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390.0f, 0.0f), ImGuiCond_Always);

    bool open = true;
    if (ImGui::Begin("Lesson: your first track", &open,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_AlwaysAutoResize)) {

        // Progress dots: filled = done, ring = current, dim = ahead.
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 dotPos = ImGui::GetCursorScreenPos();
        for (int i = 0; i < stepCount; ++i) {
            const float cx = dotPos.x + 8.0f + i * 20.0f;
            const float cy = dotPos.y + 8.0f;
            if (g_Tutorial.stepDone(i) ||
                (i < g_Tutorial.step)) {
                drawList->AddCircleFilled(ImVec2(cx, cy), 5.0f,
                                          IM_COL32(120, 220, 140, 255));
            } else if (i == g_Tutorial.step) {
                drawList->AddCircle(ImVec2(cx, cy), 5.0f,
                                    IM_COL32(140, 200, 255, 255), 0, 2.0f);
            } else {
                drawList->AddCircleFilled(ImVec2(cx, cy), 3.0f,
                                          IM_COL32(110, 110, 125, 160));
            }
        }
        ImGui::Dummy(ImVec2(stepCount * 20.0f, 18.0f));

        ImGui::TextDisabled("Step %d of %d", g_Tutorial.step + 1, stepCount);
        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.00f, 1.0f), "%s", step.title);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 370.0f);
        ImGui::TextUnformatted(step.body);
        ImGui::PopTextWrapPos();
        if (step.hint != nullptr && step.hint[0] != '\0') {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 370.0f);
            ImGui::TextDisabled("%s", step.hint);
            ImGui::PopTextWrapPos();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // The status line an action step lives by.
        if (step.kind == TutorialStepKind::Action) {
            if (currentDone) {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f),
                                   "Done - nice.");
            } else {
                ImGui::TextDisabled("Waiting for you - the lesson will "
                                    "notice when it is there.");
            }
        }

        if (g_Tutorial.step > 0) {
            if (ImGui::Button("Back")) {
                --g_Tutorial.step;
                g_Tutorial.focusedStep = -1;
            }
            ImGui::SameLine();
        }

        const bool isLast = (g_Tutorial.step == stepCount - 1);
        if (currentDone) {
            if (ImGui::Button(isLast ? "Finish" : "Next", ImVec2(90, 0))) {
                if (isLast) {
                    g_Tutorial.active = false;
                } else {
                    ++g_Tutorial.step;
                    g_Tutorial.focusedStep = -1;
                }
            }
        } else {
            if (ImGui::Button("Skip step")) {
                g_Tutorial.completedMask |= (1u << g_Tutorial.step);
                if (isLast) {
                    g_Tutorial.active = false;
                } else {
                    ++g_Tutorial.step;
                    g_Tutorial.focusedStep = -1;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Moves on without doing this one.\n"
                                  "Every tool works with or without the lesson.");
            }
        }
    }
    ImGui::End();

    // The X closes the lesson entirely; Help > Lesson starts it fresh.
    if (!open) g_Tutorial.active = false;
}

// Apply a queued undo or redo. main.cpp calls this at the top of the frame,
// before any code takes a Pattern& into project.patterns - restoring a
// snapshot rebuilds that vector, so doing it mid-frame would dangle every
// reference the drawing code is holding. See RequestUndo.
inline bool ApplyPendingHistory(Project& project, UIState& ui, Sequencer& seq) {
    if (!g_UndoRequested && !g_RedoRequested) return false;

    const bool wantUndo = g_UndoRequested;
    g_UndoRequested = false;
    g_RedoRequested = false;

    const bool changed = wantUndo ? g_UndoHistory.undo(project)
                                  : g_UndoHistory.redo(project);
    if (!changed) return false;

    // The restored project may hold fewer patterns than the one on screen,
    // and every selection index points at notes that no longer exist.
    const int patternCount = static_cast<int>(project.patterns.size());
    if (ui.selectedPattern >= patternCount) {
        ui.selectedPattern = (patternCount > 0) ? patternCount - 1 : 0;
    }
    if (ui.selectedPattern < 0) ui.selectedPattern = 0;

    ui.selectedNoteIndex = -1;
    ui.selectedNoteIndices.clear();

    // Any gesture in flight refers to the old note list.
    ui.isDraggingNote = false;
    ui.isDraggingMultiple = false;
    ui.isResizingNote = false;
    ui.isResizingMultiple = false;
    ui.isPendingDrag = false;
    ui.isPendingMultiDrag = false;

    // Channel settings are part of the project, so the synths need resyncing
    // or the restored mix would be invisible to the audio thread.
    seq.updateChannelConfigs();
    return true;
}

} // namespace ChiptuneTracker
