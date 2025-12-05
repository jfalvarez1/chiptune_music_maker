# Chiptune Tracker

A tracker-style DAW for creating authentic chiptune music, built from scratch in C++20.

## Overview

Chiptune Tracker is a lightweight, real-time digital audio workstation designed for composing retro-style music. It emulates the distinctive sounds of vintage hardware like the NES 2A03 and Game Boy LR35902 using mathematically accurate synthesis.

### Design Philosophy

- **Zero allocations in the audio thread** - Lock-free ring buffers for UI-to-audio communication
- **PolyBLEP antialiased oscillators** - Alias-free square, sawtooth, and triangle waves
- **LFSR noise generation** - Authentic NES-style percussion
- **Minimal dependencies** - Only miniaudio + Dear ImGui

## Features

### Sound Generation
- **Oscillators**: PolyBLEP-corrected Pulse (variable duty), Triangle, Sawtooth, Sine
- **Noise**: 15-bit LFSR (Linear Feedback Shift Register) with short/long modes
- **ADSR Envelopes**: Full Attack, Decay, Sustain, Release control
- **Per-note sound types**: Each note can use a different oscillator

### Drum Kit (21 sounds!)
- **Kicks**: Standard, 808, Hard, Soft
- **Snares**: Standard, 808, Rimshot, Clap
- **Hi-Hats**: Closed, Open, Pedal
- **Toms**: High, Mid, Low
- **Cymbals**: Crash, Ride
- **Percussion**: Cowbell, Clave, Conga, Maracas, Tambourine

### Editing
- **Piano Roll Editor**: Visual note editing with zoom and scroll
- **Three edit modes**: Draw, Select, Erase (hotkeys: D, S, E)
- **Box selection**: Click and drag to select multiple notes
- **Dynamic timeline**: Grid automatically extends as you add notes
- **Paste preview**: Ghost notes follow mouse for precise placement at any octave
- **Full undo/redo**: 50 levels of history (Ctrl+Z / Ctrl+Y)

### File Operations
- **Project save/load**: Native .ctp format preserves all notes and settings
- **WAV export**: Render your music to high-quality audio files
- **Windows file dialogs**: Native save/open dialogs

### Effects (per channel)
- Bitcrusher
- Distortion
- Filter
- Delay

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
- [x] 21 drum sounds
- [ ] FLAC/MP3 export (requires external libs)
- [ ] VST plugin version
- [ ] Pattern arrangement view
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
