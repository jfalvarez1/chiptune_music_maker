# Chiptune Tracker

A tracker-style DAW for creating authentic chiptune music, built from scratch in C++20.

# ⬇️ [Download latest release HERE](https://github.com/jfalvarez1/chiptune_music_maker/releases/latest/download/ChiptuneTracker-Windows.zip)

### **[Download latest release HERE](https://github.com/jfalvarez1/chiptune_music_maker/releases/latest/download/ChiptuneTracker-Windows.zip)** — Windows, no installer, no dependencies

Unzip and run `ChiptuneTracker.exe`. Everything is statically linked, so
there is nothing else to install. [All releases and release
notes.](https://github.com/jfalvarez1/chiptune_music_maker/releases)

## Overview

Chiptune Tracker is a real-time digital audio workstation for writing
retro-style music. It emulates vintage hardware like the NES 2A03 and the
Game Boy LR35902 with mathematically accurate synthesis, and pairs that with
five configurable modern engines, a modulation matrix, audio clips and a full
mixing chain.

- **Three ways to write**: piano roll, tracker grid and an arrangement
  timeline
- **Sixty-seven instruments** plus wavetable, FM, sampler, granular and
  modelled-drum engines
- **Sing or beatbox** a part and have it written out as notes
- **Ten themes**, three workspaces, and a genre focus that puts the right
  tools in front of you
- **Exports** to WAV (16 or 24-bit), FLAC, MP3 and MIDI, and imports MIDI

## Screenshots

Every image below is the program itself, captured from the running app.

### Piano roll

Draw notes directly, or place whole chords and drum patterns from the sound
palette. Sixty-seven instruments across oscillators, synths, drums and genre
kits, plus five configurable engines of their own.

![Piano roll](docs/images/piano-roll.png)

### The three edit modes

Draw places notes, Select picks them up for editing, Erase removes them. The
active mode is marked with the theme's accent.

| Draw | Select | Erase |
|---|---|---|
| ![Draw mode](docs/images/mode-draw.png) | ![Select mode](docs/images/mode-select.png) | ![Erase mode](docs/images/mode-erase.png) |

With notes selected, the Note Editor switches to editing the whole selection
at once — duration, velocity, fades and every tracker effect.

![Note selection and editing](docs/images/channel-editor.png)

### Instrument macros

The heart of chiptune sound design. Four step sequences per instrument, each
with its own loop and release point. Drag across the bar graph to draw the
shape.

| Volume macro | Arpeggio macro |
|---|---|
| ![Volume macro](docs/images/macro-volume.png) | ![Arpeggio macro](docs/images/macro-arpeggio.png) |

A `0 4 7` arpeggio macro fakes a whole major chord on a single channel —
the defining chiptune trick. Right-click a step to make it *fixed* rather
than relative to the played note.

### Euclidean rhythms

Distributing k onsets as evenly as possible over n steps produces a
startling number of the world's actual drum patterns — E(3,8) is the
tresillo, E(5,16) the bossa clave. Live step preview, per instrument or as a
full kit, with nine named presets.

![Euclidean rhythm generator](docs/images/tools-euclidean.png)

### Tracker view

The classic hex grid, for when you would rather type than draw.

![Tracker view](docs/images/tracker.png)

### Arrangement

Lay patterns out along a timeline to build the full song structure.

![Arrangement](docs/images/arrangement.png)

### Mixer

Eight channels with live level metering, pan knobs, mute and solo, and a full
effects chain behind each one.

Effects that cannot answer instantly — a convolution reverb, a pitch shifter,
a hosted plugin — make their channel come out late. Everything else is
delayed to meet it, including the copies going out to and coming back from
the aux buses, so a part never drifts out of time with the rest because of
what you put on it. A send used for parallel processing lands exactly on top
of the dry signal instead of comb-filtering against it.

![Mixer](docs/images/mixer.png)

### Pad controller

MPC-style pads for playing drums and auditioning sounds live.

![Pad controller](docs/images/pad.png)

### Analysis and sound design

| Spectrum analyzer | Wavetable editor |
|---|---|
| ![Spectrum analyzer](docs/images/spectrum.png) | ![Wavetable editor](docs/images/wavetable.png) |
| **Parameter automation** | **MIDI input** |
| ![Automation](docs/images/automation.png) | ![MIDI input](docs/images/midi-input.png) |

### Instrument engines

Five engines with editors of their own, found under **Engines** in the sound
palette. Each opens with a short row of starting points rather than sixty
parameters and no idea where to begin.

| Six-operator FM | Multisample sampler |
|---|---|
| ![FM](docs/images/engine-fm.png) | ![Sampler](docs/images/engine-sampler.png) |
| **Granular** | **Modelled drums** |
| ![Granular](docs/images/engine-granular.png) | ![Drum model](docs/images/engine-drums.png) |

The palette section they live in, and what a genre focus does to it —
chiptune is shown the wavetable and FM engines, and not the sampler.

![Engines in the palette](docs/images/palette-engines.png)

### Modulation

Nine sources to nine destinations, with per-voice LFOs so held notes do not
wobble in lockstep. This is what makes the engines worth having: a wavetable
whose morph never moves is a sampled waveform.

![Modulation matrix](docs/images/mod-matrix.png)

### Audio on the timeline

Recorded or imported audio on a channel, drawn as its own waveform and
running through that channel's volume, pan, effects and sends like anything
else. A clip whose file has moved keeps its place and its edits and says so.

![Audio clips](docs/images/audio-clips.png)

### Note FX

A second rack on every channel that changes *which* notes play rather than
how they sound. A Chord module turns one written note into three; an Arpeggio
turns those into a running sequence; a Strum stops them all landing at once.
Transpose, Octave, Scale, Range and Velocity too, up to eight in any order —
Chord above Arpeggio is an arpeggiated chord, the other way round is a chord
on every step of one.

Nothing is written into the pattern. The pattern keeps the note you played,
and the rack decides what is made of it — so you can change the rate, reorder
the modules, or switch the whole thing off while it plays. The panel runs a
middle C through the rack and shows you what comes out, so you can see what a
setting does without pressing play.

### Plugins

Per-channel racks for hosted VST and CLAP plugins, with a scanner that finds
what is installed and remembers it between launches.

**This build cannot load plugins yet** — none of the three SDKs ships with
it. What works today is everything around that: scanning, the rack, and the
project format. A project made where plugins *do* load keeps every plugin,
its parameters and its state when opened here, and gives them back intact —
so nothing is lost by moving a project between machines.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for what each format needs.

### Browser and command palette

One place to find what you have — the samples in the project, every
instrument and engine, the shipped presets, and the audio and project files
on disk. Drag any of it onto a channel or onto the timeline.

`Ctrl+P` opens a command palette that finds any command by name, so nothing
is hidden behind a key you would have had to be told about. `Ctrl+/` lists
every shortcut, and any of them can be rebound.

![Browser](docs/images/browser.png)

![Command palette](docs/images/command-palette.png)

### Take lanes

Record a part several times, then build the keeper out of the best moments
of each. Every pass around the loop becomes its own lane; drag across a lane
to choose it for that stretch. Punch in to repair one phrase without risking
the rest of the take.

What you comp becomes ordinary audio clips on the arrangement, so you can
trim, fade and move the result afterwards like anything else.

![Take lanes](docs/images/take-lanes.png)

### Song structure

Tempo and time-signature changes, markers and regions. Bar lines follow the
meter map rather than a modulo, and Snap-to-Bar walks the same function the
ruler draws.

![Song structure](docs/images/song-structure.png)

### Demo songs

Four finished, mastered pieces built around the engines that are hardest to
find by poking at panels — load one from the File panel and press play.

- **FM Bells** — six-operator FM, inharmonic bells over an FM bass, through
  a convolution reverb
- **Wavetable Motion** — a wavetable lead whose morph is swept as it plays
- **Granular Clouds** — granular pads through the shimmer and hall reverbs
- **Modelled Drums** — one drum algorithm tuned three ways, with a dynamic
  EQ and bus compression

Each sets its own channel levels, pans and master chain, so they are
demonstrations of what a finished mix sounds like rather than test tones.

### Voice Mode — sing your song

Build a whole song a part at a time, without touching a note editor.
Beatbox a groove, keep it, hum a bass line against what you just made, keep
that, then a melody over both. Each part plays while you record the next, so
you are playing along with your own song rather than into silence.

You make one decision — drums, bass, lead or chords — and it sets everything
else: what to listen for, which octave the part belongs in, how long the
notes are held, the instrument, and the channel. A part you have kept is
never overwritten by a later take.

It will also work out what tempo you played at, so there is no click to hum
along to, and keep a hummed line in key if you ask it to.

![Voice Mode](docs/images/voice-mode.png)

### Voice to notes

Sing a line or beatbox a groove and get notes — live as you make them, or
from a recorded take. The capture callback fills a lock-free ring and does
nothing else; every FFT happens off the audio thread.

![Voice to notes](docs/images/voice-to-notes.png)

### Pitch and time

A phase vocoder: pitch shift, formant shift and autotune. Formant shifting
moves the resonances *without* moving the notes, which is the difference
between transposing a voice and turning it into a chipmunk.

![Pitch and time](docs/images/pitch-time.png)

### Project Check

Settings that are switched on and doing nothing, or silently cancelling
something else — a send into a muted bus, an effect with its mix at zero, a
soloed channel that is why nothing else is audible. It reports and never
changes anything.

![Project Check](docs/images/project-check.png)

### On main, not yet in a release

These are in the source but not in the download above; they arrive with the
next release.

**Chip mode.** The program synthesises in float and can put any pitch on any
channel. Real chips cannot. Chip mode holds a channel to what the hardware
could actually do — pitch through the chip's period register, volume through
its volume register — *while you write*, so the limit arrives as a sound you
can react to. NES pulse, triangle and noise, and Game Boy pulse and wave.
Turn it on per channel under **Master Bus → Chip Accuracy**, or press the NES
layout button for two pulses, a triangle and a noise channel. It is off by
default, so a project saved before it existed plays exactly as it did.

**Would this run on the hardware?** Chip Accuracy opens with a verdict —
Hardware-legal, Expansion hardware, Chip-flavoured or Fakebit — and the
specific channel and setting behind it. It never suggests changing anything:
the rungs are claims, not grades, and fakebit is not worse.

**Name the chord you played.** Select notes and right-click them to be told
what they are. Where the notes honestly have more than one name — C E G A is
C6 and equally Am7 — the bass decides which leads and the other is listed
underneath.

### Workspaces

Panels dock, and panels sharing a region become tabs. Three workspaces lay
them out for the task at hand; drag anything anywhere from there, and
`Ctrl+0` resets.

| Sound Design | Mix |
|---|---|
| ![Sound design workspace](docs/images/workspace-sound.png) | ![Mix workspace](docs/images/workspace-mix.png) |

### Themes

Ten themes. Each defines its own colour *and its own geometry* — corner
radius, border weight, grab size — so they differ in shape, not just in
paint. The theme reaches the piano roll, the notes and the custom widgets,
not only the window chrome.

| Stock | Cyberpunk |
|---|---|
| ![Stock](docs/images/theme-stock.png) | ![Cyberpunk](docs/images/theme-cyberpunk.png) |
| **Synthwave** | **Vaporwave** |
| ![Synthwave](docs/images/theme-synthwave.png) | ![Vaporwave](docs/images/theme-vaporwave.png) |
| **Game Boy DMG** | **Matrix** |
| ![Game Boy](docs/images/theme-gameboy.png) | ![Matrix](docs/images/theme-matrix.png) |
| **Retro Terminal** | **Minimal** |
| ![Retro terminal](docs/images/theme-terminal.png) | ![Minimal](docs/images/theme-minimal.png) |
| **Frutiger Aero** | **Daylight** |
| ![Frutiger Aero](docs/images/theme-frutiger.png) | ![Daylight](docs/images/theme-daylight.png) |

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

### Visual Themes (10 themes)
Each theme defines its own colour *and its own geometry* - corner radius,
border weight, grab size - so they differ in shape, not only in paint.

- **Stock**: Clean dark theme (default)
- **Cyberpunk**: Neon yellow, hot pink, electric blue with data streams and glitch effects
- **Synthwave**: 80s retro with neon sunset, perspective grid, and color-cycling chasers
- **Matrix**: Green on black with falling code animation and morphing characters
- **Frutiger Aero**: Glossy Web 2.0 aesthetic with floating bubbles, clouds, and glass reflections
- **Minimal**: Clean flat design with red accent, subtle geometric animations
- **Vaporwave**: Pink/cyan retro-futurism with striped sunset, perspective grid, floating shapes
- **Retro Terminal**: Authentic CRT simulation with scanlines, phosphor glow, screen curvature, and flicker
- **Game Boy DMG**: The handheld's real four-shade green, with a dot-matrix grid
- **Daylight**: A light theme, for working in a bright room
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
- **WAV export**: 16 or 24-bit, rounded and dithered
- **FLAC export**: Lossless, and often a third the size of the WAV for square waves
- **MP3 export**: Render to MP3 (requires LAME or FFmpeg in PATH)
- **MIDI export and import**: One pattern per track, one track per channel;
  importing keeps your instruments and swaps the notes
- **Windows file dialogs**: Native save/open dialogs

### Per-Note Effects (Tracker-style!)
- **Vibrato**: Pitch wobble with adjustable depth and speed
- **Arpeggio**: Classic tracker 0xy effect - cycles through base note + semitone offsets
  - Presets: Major (4,7), Minor (3,7), Octave (12,0)
- **Portamento/Slide**: Smooth pitch transitions between notes
- **Legato and tone portamento**: A tied note keeps the sounding voice, and
  with a slide glides to it from wherever it is (the tracker's `3xx`)

### Groove & Feel
- **Swing**: Shift off-beat notes for groove (0-100%, 8th/16th/32nd grid)
- **Groove**: Per-row timing lists such as `7 5 6 6`, which never change the tempo
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

## Sound Chip Targets

| Chip | System | Channels | Chip mode |
|------|--------|----------|-----------|
| 2A03 | NES | 2 pulse, 1 triangle, 1 noise, 1 DPCM | Pulse, triangle and noise |
| LR35902 | Game Boy | 2 pulse, 1 wave, 1 noise | Pulse and wave |
| SID | Commodore 64 | 3 voices with multiple waveforms | Modelled, not yet playable |
| AY-3-8910 | Various | 3 square wave channels | Modelled, not yet playable |

The NES also runs in PAL as well as NTSC — not a scaled NTSC: pitch moves by
7.65%, the envelope clock drops from 240 Hz to 200, and the noise period
tables cross.

## Prerequisites

- **Compiler**: MSVC 2019+, GCC 10+, or Clang 12+ with C++20 support
- **CMake**: 3.20 or higher
- **OpenGL**: 3.3+ compatible GPU

### Libraries

Already in `vendor/`, so there is nothing to download:

- [miniaudio](https://miniaud.io/) — audio I/O
- [Dear ImGui](https://github.com/ocornut/imgui) — the interface, with the
  OpenGL 3 and Win32 backends
- [midifile](https://github.com/craigsapp/midifile) — MIDI import and export

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

### Quick Rebuild (when build directory exists)

For Windows with Visual Studio, from the project root:

```bash
cmake --build build --config Release
```

Output: `build/bin/Release/ChiptuneTracker.exe`

**Note**: If the app is running, close it first or the build will fail with a file lock error.

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

## What's new

**[v3.12.0 "Measured"](https://github.com/jfalvarez1/chiptune_music_maker/releases/latest)** —
MIDI import, FLAC and 24-bit export, groove, legato, PAL and a chord
progression generator. And five things that had been quietly wrong for a long
time, found by measuring: the master limiter was a hard clipper, every
exported MIDI file had a nonsensical tempo, and autosave never fired for most
edits.

**v3.11.0 "Rack"** — Note FX, a wavetable editor you can edit in, a scope on
every channel, and opening Mega Drive `.tfi` and `.vgi` patches.

**v3.10.0 "Voice"** — Voice Mode, building a song by singing it a part at a
time, and delay compensation across the whole mixer.

**v3.9.0 "Comp"** — take lanes and comping, the browser and command palette,
and the shape of plugin hosting.

**v3.8.0 "Check"** — a Project Check panel that finds settings switched on
and doing nothing, and a genre focus that now covers the instrument engines.

**v3.7.0 "Instruments"** — five configurable engines (wavetable, six-operator
FM, multisample sampler, granular, modelled drums), a modulation matrix,
audio clips on the timeline, tempo and time-signature changes, and
voice-to-notes.

Full history in [CHANGELOG.md](CHANGELOG.md), and every release is on the
[releases page](https://github.com/jfalvarez1/chiptune_music_maker/releases).

## Roadmap

The full plan lives in [docs/ROADMAP.md](docs/ROADMAP.md). The short version:

**Done in 3.0**

- [x] Instrument macros (volume, arpeggio, duty, pitch) with presets
- [x] Project format v2 - saves the mix, arrangement and every effect
- [x] Workspace layouts and a custom widget set
- [x] Headless test suite (1112 checks)
- [x] 4-bit volume quantisation
- [x] Ten visual themes, applied to the editor as well as the chrome

**Done in 3.12**

- [x] PAL: the noise period table, the frame counter and the pitch scaling
      all differ from NTSC
- [x] Groove patterns (per-row speed lists) alongside the existing swing
- [x] MIDI import
- [x] A chord progression generator, deriving chords from the scale
- [x] FLAC export, and 24-bit WAV

**On main, for the next release**

- [x] Chip mode for the NES and Game Boy — pitch and volume quantised to the
      actual registers while you compose
- [x] A visible hardware-legality verdict, from Hardware-legal to Fakebit
- [x] Game Boy envelope limits, reported by Project Check rather than
      enforced

**Next**

- [ ] Chip mode for the SID, AY-3-8910 and YM2612 — modelled in the tables,
      with no voice playing through them yet
- [ ] Fixed channel roles as an enforced layout rather than preset buttons

**Done since this list was written** — it had gone stale, and several of
these had been shipped for a while:

- [x] Authentic noise: 15-bit LFSR, both taps, all 16 NES noise periods
- [x] Legato / tie notes and true tone portamento
- [x] Euclidean rhythm generator
- [x] Sample import, with three playback paths (sampler zones, timeline audio
      clips, granular)
- [x] Autosave and crash recovery

**Not planned, and why** — see [docs/ROADMAP.md](docs/ROADMAP.md) for the
detail:

- **VGM export** needs a register-level layer this program does not have, and
  the file would not sound like your project. (Also: the VGM format has no
  SID, in any version.)
- **NSF export** needs a 6502 driver. FamiTracker ships ~5,800 lines of hand
  written assembly for the base chip alone.
- **A VST build** is blocked on a licensing decision before the first line of
  code.

## Documentation

| Document | What is in it |
|---|---|
| [CHANGELOG.md](CHANGELOG.md) | Every release, what changed and what was fixed |
| [docs/ROADMAP.md](docs/ROADMAP.md) | What is planned next |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How it is built - audio threading, antialiasing, source layout |
| [docs/TEST_PLAN.md](docs/TEST_PLAN.md) | The three test layers, and an honest list of what is not covered |
| [docs/ChiptuneTracker_Guide.html](docs/ChiptuneTracker_Guide.html) | The full in-app guide |

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
