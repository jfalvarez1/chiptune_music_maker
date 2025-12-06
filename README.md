# Chiptune Tracker

A tracker-style DAW for creating authentic chiptune music, built from scratch in C++20.

## Overview

Chiptune Tracker is a lightweight, real-time digital audio workstation designed for composing retro-style music. It emulates the distinctive sounds of vintage hardware like the NES 2A03 and Game Boy LR35902 using mathematically accurate synthesis.

### Design Philosophy

- **Zero allocations in the audio thread** - Lock-free ring buffers for UI-to-audio communication
- **PolyBLEP antialiased oscillators** - Alias-free square, sawtooth, and triangle waves
- **LFSR noise generation** - Authentic NES-style percussion
- **Minimal dependencies** - Only miniaudio + Dear ImGui

### What's New in v2.11.0

**Stereo Widener & Tape Saturation - Classic 80s Sound:**
New effects for authentic synthwave production:

- **Stereo Widener** - Create that lush, wide 80s pad sound
  - Uses Haas effect and mid/side processing
  - Width control (0 = mono, 1 = ultra wide)
  - Haas delay (10-35ms) for stereo perception
  - Presets: Subtle, Wide Pad, Ultra Wide

- **Tape Saturation** - Warm analog character
  - Models tape compression and harmonic saturation
  - Drive (1-3x), Warmth, Compression controls
  - Adds even harmonics for vintage warmth
  - Presets: Subtle Warmth, Cassette, Hot Tape

- **Unison Helper** - For thick synthwave sounds
  - Multiple detuned voice calculations
  - Stereo spread parameter
  - Used internally by synth presets

**How to Use:**
1. Open **Channel Editor** (click channel in Mixer)
2. Scroll to Effects section
3. Enable **Stereo Widener** for wide pads
4. Enable **Tape Saturation** for analog warmth
5. Use presets or dial in custom settings

### What's New in v2.10.0

**Reverb Effect & Genre Effect Presets:**
Professional-quality reverb and automatic genre-based effect presets for authentic sound:

- **Algorithmic Reverb** (Schroeder-style)
  - 8 parallel comb filters + 4 series allpass filters
  - Adjustable: Room Size, Damping, Mix, Predelay
  - Creates depth and space for any sound

- **Genre Effect Presets** - Automatic effects when placing sample tracks:
  - **Synthwave**: Heavy reverb (0.4 mix), chorus, dotted 8th delay, sidechain
  - **Chiptune**: Minimal effects for authentic 8-bit (subtle delay only)
  - **Techno**: Room reverb, heavy delay feedback, sidechain compression
  - **Hip Hop/Trap**: Lo-fi vibes, subtle reverb, vintage delay
  - **House**: Big room reverb, chorus, sidechain pumping
  - **Reggaeton**: Tight room reverb, short delay for punch

- **Per-Note Dynamics** - Sample tracks now include:
  - Velocity variation for expressive dynamics
  - Vibrato on sustained notes for warmth
  - Genre-appropriate effect settings

**How to Use:**
1. Open **Mixer** window (View > Mixer or keyboard shortcut)
2. Click on a channel to select it
3. Effects are in the **Channel Editor** panel
4. Adjust Reverb, Chorus, and Delay settings
5. Sample tracks auto-apply genre effects when placed

### What's New in v2.9.0

**Tools Panel - 9 Synthwave Production Tools:**
A comprehensive suite of music creation tools in a dedicated panel:

1. **Drum Pattern Generator** - Create genre-specific drum patterns instantly
   - 6 genre presets: Synthwave, Outrun, Darksynth, Italo Disco, Techno, Retrowave
   - Density slider (25-100%) for pattern fullness
   - Variation control (0-100%) for fills and variations
   - Auto-generates kick, snare, and hi-hat patterns

2. **Arpeggiator** - Convert selected notes into arpeggiated patterns
   - Modes: Up, Down, Up-Down, Random
   - Rate control: 8th, 16th, 32nd notes
   - Gate control (25-100%) for note lengths
   - Transforms chords into flowing sequences

3. **Bass Pattern Generator** - Create basslines automatically
   - Styles: Octave Pulse, Root+Fifth, Walking, Arp
   - Root note selection (C through B)
   - Pattern length control (1-8 bars)
   - Generates rhythmic bass patterns

4. **Scale Lock + Highlighting** - Stay in key effortlessly
   - 7 scales: Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian
   - Root note selection for any key
   - Highlight scale notes in piano roll
   - Snap-to-scale for perfect pitch

5. **Velocity Curve Painter** - Shape dynamics across notes
   - Curve types: Linear, Exponential, Logarithmic, S-Curve
   - Start/End velocity control
   - Apply to selected notes for crescendos/decrescendos

6. **Fill Generator** - Add drum fills at pattern end
   - Fill styles: Snare Roll, Tom Cascade, Cymbal Crash, Build-Up
   - Position control (last 1-4 beats)
   - Density control (Medium, Dense, Sparse)

7. **Pattern Variation** - Create variations of existing patterns
   - Variation types: Timing, Velocity, Pitch, All
   - Amount control for subtle or dramatic changes
   - Preserves musical structure while adding interest

8. **Quick Layer** - Layer sounds for thicker textures
   - Octave offset (-2 to +2 octaves)
   - Detune amount for chorus effects
   - Velocity scaling for layered dynamics
   - Duplicate selected notes with modifications

9. **Humanize Selected** - Add natural feel to mechanical patterns
   - Timing variation (0-50ms)
   - Velocity variation (0-30%)
   - Makes robotic patterns feel more human

### What's New in v2.8.2

**Chord Presets (45 chords across 8 genres!):**
Place entire chords with a single click - perfect for quickly building progressions.
- **Pop** (6): C Major, G Major, Am, F Major, Dm7, Cadd9
- **Jazz** (6): Cmaj7, Dm9, G13, Fm7, Bbmaj7, Am7b5
- **Rock** (5): E5, A5, D5, G5, B5 (power chords)
- **EDM** (6): Am, F, C, G, Em7, Dm
- **Hip Hop** (5): Cm7, Fm9, Bbmaj7, Gm7, Ebmaj9
- **Reggaeton** (5): Am, F, Dm, E, G
- **Synthwave** (7): Fm, Cm, Ab, Eb, Bbm7, Fm9, Db - uses authentic synthwave pads/leads
- **Chiptune** (8): C/Am/G/Em/F Arps, C5, E5, Dm - 8-bit arpeggios and power chords

**How Chords Work:**
1. Open the Sound Palette and expand the "Chords" section
2. Click a genre header to expand it
3. Click a chord to select it (auto-enters Draw mode)
4. Click in the piano roll to place all chord notes at once
5. All notes are placed at the same time with genre-appropriate instruments

**Pattern Management:**
- **Delete patterns** with the Delete button or Delete key
- Select a pattern and press Delete to remove it (must have at least 1 pattern)

### What's New in v2.8.1

**Piano Roll Beat Grid Enhancement:**
- **Hierarchical beat grid lines** - Visual rhythm hierarchy for easy note placement
  - Measure lines (beat 0, 4, 8...): Brightest, 2px thick
  - Quarter notes (whole beats): Bright, 1.5px thick
  - 8th notes (half beats): Medium brightness, 1px
  - 16th notes (quarter beats): Dimmest, 1px
  - Pattern end marker: Red line, 3px thick
- Makes it easy to identify half notes, quarter notes, and subdivisions at a glance

### What's New in v2.8.0

**New NES-Authentic Chiptune Effects:**
- **Duty Cycle Control** (NES-style) - Per-note pulse width for authentic chiptune sounds
  - 12.5%: Thin, reedy sound (NES classic)
  - 25%: Hollow, slightly nasal
  - 50%: Full square wave
  - 75%: Same as 25%, inverted phase
- **Pitch Sweep** (NES sweep unit) - Automatic pitch bend for laser sounds and effects
  - Sweep Up: Pitch rises over time
  - Sweep Down: Pitch falls (classic laser/zap sound)
  - Adjustable speed and range (semitones)
- **Tremolo** - Volume modulation for wavering, pulsing sounds
  - Adjustable depth (0-100%)
  - Adjustable speed (Hz)

**Technical Improvements:**
- Extended Note struct with new effect parameters
- Effects processed in real-time audio thread
- Full integration with existing vibrato, arpeggio, and portamento effects

### What's New in v2.7.0

**Window Layout & Workflow Improvements:**
- **Proper initial window layout** - Windows now open in organized positions on first launch
  - Top row: Transport, File, Views
  - Left column: Sound Palette, Patterns, Pad Controller
  - Main area: Piano Roll (center)
  - Right column: Note Editor, Channel Editor
  - Bottom: Mixer, Tracker, Arrangement

**Groove & Feel System:**
- **Swing control** (0-100%) - Shift off-beat notes forward for groove feel
  - Grid options: 8th notes, 16th notes, 32nd notes
  - Creates shuffle/swing rhythm patterns automatically
- **Humanize** - Add random variation for natural, human-like timing
  - Timing variation: 0-50ms
  - Velocity variation: 0-30%

**Hi-Hat Roll Generator Tool:**
- Quickly create drum fills and rapid hi-hat patterns
- **Density options**: 8th, 16th, 32nd, 64th notes
- **Velocity modes**: Flat, Crescendo, Decrescendo
- **Hi-hat types**: Closed, Open, Pedal

**Sidechain Compression Effect:**
- Classic EDM "pumping" effect - duck one channel based on another
- Perfect for making bass duck when kick plays
- **Presets**: Subtle (5dB), Normal (10dB), Heavy (15dB), Pumping (20dB)
- Adjustable attack, release, ratio, and threshold

### What's New in v2.6.0

**Drum Synthesis Engine Overhaul** - Complete rewrite of drum sound generation for authentic 808/909 character:
- **Kicks**: Proper pitch sweeps (150Hz → 45Hz) with exponential decay for that classic "thump"
- **Snares**: Sharp "tsk" attack with 200Hz tonal body + LFSR noise burst
- **Hi-Hats**: Metallic ring using inharmonic frequency ratios (1.0, 1.47, 1.83, 2.67)
- **Toms**: Pitch envelope (180Hz → 100Hz) for authentic "bom" sound
- **Technical**: Drums now manage their own phase increment for pitch control, bypassing ADSR

## Features

### Sound Generation
- **Oscillators**: PolyBLEP-corrected Pulse (variable duty), Triangle, Sawtooth, Sine, Supersaw
- **Supersaw**: 7 detuned sawtooth oscillators for massive, wide sounds
- **Noise**: 15-bit LFSR (Linear Feedback Shift Register) with short/long modes
- **ADSR Envelopes**: Full Attack, Decay, Sustain, Release control
- **Per-note sound types**: Each note can use a different oscillator
- **Sound Preview**: Notes play a brief preview when placed in the piano roll

### Synth Presets (16 types!)
- **Lead**: Bright cutting lead with detuned saws
- **Pad**: Soft atmospheric pad with slow attack
- **Bass**: Deep punchy bass (sub + harmonics)
- **Pluck**: Short plucky sound with fast decay
- **Arp**: Crisp arpeggio sound (pulse + fast envelope)
- **Organ**: Classic organ with additive harmonics
- **Strings**: String ensemble (detuned + slow attack)
- **Brass**: Brassy stab (saw + square + harmonics)
- **Chip**: Classic chiptune lead (NES-style 12.5% pulse)
- **Bell**: Bell/chime sound (FM-like synthesis)

### Synthwave Synths (6 types!)
- **SW Lead**: Bright PWM lead with warmth - perfect for main melodies
- **SW Bass**: Deep 808-style saw bass - massive low end
- **SW Pad**: Warm lush evolving pad - atmospheric backgrounds
- **SW Arp**: Crisp sequence/arp sound - rapid passages
- **SW Chord**: Polyphonic stab for chords - punchy chord hits
- **SW FM**: Classic DX7-style FM brass - metallic and bright

### Drum Kit (26 sounds!)
- **Kicks**: Standard, 808, Hard, Soft
- **Snares**: Standard, 808, Rimshot, Clap
- **Hi-Hats**: Closed, Open, Pedal
- **Toms**: High, Mid, Low
- **Cymbals**: Crash, Ride
- **Percussion**: Cowbell, Clave, Conga, Maracas, Tambourine
- **Reggaeton**: Guira, Bongo, Timbale, Dembow 808, Dembow Snare

### Reggaeton Instruments (7 sounds!) - Research-based authentic synthesis
- **Reggaeton Bass**: Deep 808-style bass with lo-fi character and pitch sweep
- **Latin Brass**: Punchy brass stab with odd harmonics for authentic section feel
- **Guira**: High-frequency metallic scrape (essential dembow "tsss-tsss")
- **Bongo**: Membrane resonance with inharmonic overtones and hand slap
- **Timbale**: Square-wave based with fast decay and metallic ring
- **Dembow 808**: Lo-fi unpitched kick with fast pitch sweep (12-bit style)
- **Dembow Snare**: Tight clap-like snare with 1-3kHz emphasis (no tail)

### Visual Themes (8 themes!)
- **Stock**: Clean dark theme (default)
- **Cyberpunk**: Neon yellow, hot pink, electric blue with data streams and glitch effects
- **Synthwave**: 80s retro with neon sunset, perspective grid, and color-cycling chasers
- **Matrix**: Green on black with falling code animation and morphing characters (like the movie!)
- **Frutiger Aero**: Glossy Web 2.0 aesthetic with floating bubbles, clouds, and glass reflections
- **Minimal**: Clean flat design with red accent, subtle geometric animations
- **Vaporwave**: Pink/cyan retro-futurism with striped sunset, perspective grid, floating shapes
- **Retro Terminal**: Authentic CRT simulation with scanlines, phosphor glow, screen curvature, and flicker
- **High-DPI scaling**: All themes scale properly for 1440p, 4K, and ultrawide monitors

### Sample Tracks (7 genres!)
Pre-made track templates to get you started:
- **Synthwave**: Driving 80s beat at 118 BPM
- **Techno**: Minimal techno groove at 130 BPM
- **Chiptune**: NES-style 8-bit at 140 BPM
- **Hip Hop**: Classic boom bap at 90 BPM
- **Trap**: Dark trap beat at 140 BPM
- **House**: Four-on-the-floor at 125 BPM
- **Reggaeton**: Dembow beats (Perreo 95 BPM, Gasolina 100 BPM, Noche 90 BPM)

### Editing
- **Piano Roll Editor**: Visual note editing with zoom and scroll
- **Three edit modes**: Draw, Select, Erase (hotkeys: D, S, E)
- **Box selection**: Click and drag to select multiple notes
- **Multi-note drag**: Select multiple notes and drag them together
- **Dynamic timeline**: Grid automatically extends as you add notes
- **Paste preview**: Ghost notes follow mouse for precise placement at any octave
- **Zoom controls**: X/Y zoom sliders + Ctrl+Shift+Wheel for vertical zoom
- **Full undo/redo**: 50 levels of history (Ctrl+Z / Ctrl+Y)

### Sound Palette
- **Expandable categories**: Collapsible sections for Oscillators, Synths, Chords, Drums, and Reggaeton
- **Chord presets**: 45 chords across 8 genres (Pop, Jazz, Rock, EDM, Hip Hop, Reggaeton, Synthwave, Chiptune)
- **Drum categories**: Kicks, Snares, Hi-Hats, Toms, Cymbals, Percussion, Reggaeton
- **Duration variants**: Each drum has Short (0.5x), Normal (1x), and Long (2x) options
- **Click to select**: Choose a sound or chord, then click on piano roll to place

### File Operations
- **Project save/load**: Native .ctp format preserves all notes and settings
- **WAV export**: Render your music to high-quality audio files
- **MP3 export**: Render to MP3 (requires LAME or FFmpeg in PATH)
- **Windows file dialogs**: Native save/open dialogs

### Per-Note Effects (Tracker-style!)
- **Vibrato**: Pitch wobble with adjustable depth and speed
- **Arpeggio**: Classic tracker 0xy effect - cycles through base note + semitone offsets
  - Presets: Major (4,7), Minor (3,7), Octave (12,0)
- **Portamento/Slide**: Smooth pitch transitions between notes

### Groove & Feel
- **Swing**: Shift off-beat notes for groove (0-100%, 8th/16th/32nd grid)
- **Humanize**: Random timing and velocity variation for natural feel

### Tools Panel (9 Production Tools!)
- **Drum Pattern Generator**: 6 genre presets (Synthwave, Outrun, Darksynth, Italo Disco, Techno, Retrowave)
- **Arpeggiator**: Convert chords to arpeggiated patterns (Up, Down, Up-Down, Random)
- **Bass Pattern Generator**: 4 styles (Octave Pulse, Root+Fifth, Walking, Arp)
- **Scale Lock + Highlighting**: 7 scales with piano roll highlighting and snap-to-scale
- **Velocity Curve Painter**: 4 curve types (Linear, Exponential, Logarithmic, S-Curve)
- **Fill Generator**: 4 fill styles (Snare Roll, Tom Cascade, Cymbal Crash, Build-Up)
- **Pattern Variation**: Randomize timing, velocity, or pitch for variations
- **Quick Layer**: Layer sounds with octave offset and detuning
- **Humanize Selected**: Add timing and velocity variation for natural feel
- **Hi-Hat Roll Generator**: Quick fills and rolls with density and velocity control

### Pattern Arrangement
- **Timeline view**: Arrange multiple patterns into a full song
- **Drag & drop**: Move clips between channels and positions
- **Visual editing**: Double-click to add, right-click to delete
- **Context menu**: Right-click empty space to add any pattern
- **Song length control**: Set total song duration

### Effects (per channel)
- Bitcrusher
- Distortion
- Filter
- Delay
- Chorus
- Phaser
- Tremolo
- Ring Modulator
- **Reverb** (Schroeder-style algorithmic)
  - 8 parallel comb filters + 4 series allpass filters
  - Room Size, Damping, Mix controls
  - Presets: Small Room, Hall, Cathedral, Plate
- **Sidechain Compression**: Classic EDM pumping effect
  - Duck any channel based on another (e.g., duck bass when kick plays)
  - Presets: Subtle, Normal, Heavy, Pumping

## Project Structure

```
chiptune_music_maker/
├── build/                 # Build output (git-ignored)
├── docs/
│   └── ChiptuneTracker_Guide.html  # Full documentation
├── src/
│   ├── main.cpp           # Application entry, ImGui setup
│   ├── Types.h            # Core data structures
│   ├── Synthesizer.h      # Sound generation & drums
│   ├── Sequencer.h        # Playback engine
│   ├── FileIO.h           # Save/load & WAV export
│   ├── Effects.h          # Audio effects
│   └── UI.h               # ImGui interface
├── vendor/
│   ├── miniaudio/         # miniaudio.h
│   └── imgui/             # Dear ImGui source
├── CMakeLists.txt
├── .gitignore
└── README.md
```

## Prerequisites

- **Compiler**: MSVC 2019+, GCC 10+, or Clang 12+ with C++20 support
- **CMake**: 3.20 or higher
- **OpenGL**: 3.3+ compatible GPU

### Required Libraries

1. **miniaudio** - Download `miniaudio.h` from [miniaud.io](https://miniaud.io/)
   - Place in `vendor/miniaudio/miniaudio.h`

2. **Dear ImGui** - Clone from [github.com/ocornut/imgui](https://github.com/ocornut/imgui)
   - Copy these files to `vendor/imgui/`:
     - `imgui.h`, `imgui.cpp`
     - `imgui_internal.h`
     - `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`
     - `imgui_demo.cpp`
     - `backends/imgui_impl_opengl3.h`, `backends/imgui_impl_opengl3.cpp`
     - `backends/imgui_impl_win32.h`, `backends/imgui_impl_win32.cpp`

## System Requirements

### For End Users (Running the .exe)
- **Windows 10/11** (64-bit)
- **OpenGL 3.3+** compatible GPU
- **No VC++ Redistributable required** - The executable is statically linked and fully standalone

Just download and run `ChiptuneTracker.exe` - no installation needed!

### Optional
- **LAME or FFmpeg** in PATH for MP3 export

## Building

### Windows (Visual Studio)

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Windows (MinGW)

```bash
mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Linux

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Usage

Run the executable from `build/bin/`:

```bash
./ChiptuneTracker
```

### Quick Start

1. **Select a sound** from the Sound Palette (automatically enters Draw mode)
2. **Click on the piano roll** to place notes
3. **Press Play** to hear your music
4. **Add drums** for rhythm - they auto-adjust duration to BPM!
5. **Save your project** with the Save button or Ctrl+S
6. **Export to WAV** when you're happy with your creation

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `D` | Draw mode |
| `S` | Select mode |
| `E` | Erase mode |
| `Ctrl+A` | Select all notes |
| `Ctrl+C` | Copy selected |
| `Ctrl+V` | Paste (shows preview - click to place) |
| `Ctrl+X` | Cut selected |
| `Ctrl+Z` | Undo |
| `Ctrl+Y` | Redo |
| `Delete` | Delete selected |
| `Escape` | Deselect / Cancel paste preview |

### Paste Preview Feature

When you press Ctrl+V, ghost notes appear following your mouse. This lets you:
- Place notes at **any position** on the timeline
- **Transpose** copied notes to different octaves
- Click to confirm placement, Escape to cancel

## Architecture

### Audio Thread Safety

The audio engine uses a **lock-free ring buffer** for UI-to-audio communication:

```
┌─────────────┐     Lock-Free Queue     ┌─────────────┐
│   UI Thread │ ──────────────────────▶ │ Audio Thread│
│  (Commands) │                          │  (Render)   │
└─────────────┘                          └─────────────┘
```

- UI thread pushes `AudioCommand` structs (frequency, volume, waveform changes)
- Audio thread consumes commands at the start of each render callback
- No mutex, no blocking, no allocations in the hot path

### PolyBLEP Antialiasing

Square and sawtooth waves use **Polynomial Bandlimited Step (PolyBLEP)** correction to eliminate aliasing artifacts at discontinuities:

```cpp
float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t*t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t*t + t + t + 1.0f;
    }
    return 0.0f;
}
```

### LFSR Noise

15-bit Linear Feedback Shift Register with taps at bits 0 and 6 (NES long-mode):

```cpp
uint16_t feedback = ((lfsr >> 0) ^ (lfsr >> 6)) & 1;
lfsr = (lfsr >> 1) | (feedback << 14);
```

## Sound Chip Targets

| Chip | System | Channels |
|------|--------|----------|
| 2A03 | NES | 2 pulse, 1 triangle, 1 noise, 1 DPCM |
| LR35902 | Game Boy | 2 pulse, 1 wave, 1 noise |
| SID | Commodore 64 | 3 voices with multiple waveforms |
| AY-3-8910 | Various | 3 square wave channels |

## Roadmap

- [x] Pattern sequencer (tracker-style)
- [x] Piano roll editor
- [x] Multiple channels/voices
- [x] Envelope generators (ADSR)
- [x] WAV export
- [x] Project save/load
- [x] Undo/Redo (50 levels)
- [x] Copy/Paste with preview
- [x] Box selection
- [x] 26 drum sounds (including Reggaeton: Guira, Bongo, Timbale, Dembow 808, Dembow Snare)
- [x] 18 synth presets (10 classic + 6 synthwave + 2 reggaeton)
- [x] Supersaw oscillator (7 detuned saws)
- [x] MP3 export (via LAME/FFmpeg)
- [x] Visual themes (8 themes: Stock, Cyberpunk, Synthwave, Matrix, Frutiger Aero, Minimal, Vaporwave, Retro Terminal)
- [x] Multi-note selection and drag
- [x] Expandable sound palette with duration variants
- [x] Sound preview on note placement
- [x] Piano roll zoom controls
- [x] High-DPI theme scaling (1440p, 4K support)
- [x] Sample tracks (7 genres: Synthwave, Techno, Chiptune, Hip Hop, Trap, House, Reggaeton)
- [x] Reggaeton instruments (bass, brass, drums)
- [x] Per-note effects (Vibrato, Arpeggio, Portamento/Slide)
- [x] Swing and humanize for groove feel
- [x] Hi-hat roll generator tool
- [x] Sidechain compression (classic EDM pumping)
- [x] Pattern arrangement view with drag & drop
- [x] Proper initial window layout (v2.7.0)
- [x] Chord presets (45 chords across 8 genres: Pop, Jazz, Rock, EDM, Hip Hop, Reggaeton, Synthwave, Chiptune)
- [x] Pattern deletion (Delete key + button)
- [x] Tools Panel with 9 production tools (Drum Pattern Generator, Arpeggiator, Bass Pattern Generator, Scale Lock, Velocity Curve, Fill Generator, Pattern Variation, Quick Layer, Humanize)
- [x] Reverb effect (Schroeder-style algorithmic with presets)
- [x] Genre effect presets (auto-apply effects for sample tracks)
- [ ] FLAC export
- [ ] VST plugin version
- [ ] MIDI import/export

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## License

MIT License - See LICENSE file for details.

## Acknowledgments

- [miniaudio](https://miniaud.io/) - Excellent single-header audio library
- [Dear ImGui](https://github.com/ocornut/imgui) - Bloat-free immediate mode GUI
- The demoscene community for keeping chiptune alive
