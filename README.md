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

- **Oscillators**: PolyBLEP-corrected Square, Triangle, Sawtooth
- **Noise**: 15-bit LFSR (Linear Feedback Shift Register)
- **Effects**: Bitcrushing, Tanh Distortion (planned)
- **UI**: Immediate-mode GUI with tracker-style dark theme

## Project Structure

```
chiptune_music_maker/
├── build/                 # Build output (git-ignored)
├── src/
│   ├── main.cpp           # Application entry, ImGui setup
│   ├── AudioEngine.cpp    # Real-time audio synthesis
│   └── AudioEngine.h      # Lock-free audio architecture
├── vendor/
│   ├── miniaudio/         # Drop miniaudio.h here
│   └── imgui/             # Drop imgui source files here
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

The application opens a window with:
- Frequency slider (20Hz - 2kHz, logarithmic)
- Volume control
- Waveform selector (Sine, Square, Sawtooth, Triangle, Noise)
- Quick note buttons (C4-C5)

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

- [ ] Pattern sequencer (tracker-style)
- [ ] Piano roll editor
- [ ] Multiple channels/voices
- [ ] Envelope generators (ADSR)
- [ ] Bitcrushing effect
- [ ] Tanh distortion
- [ ] WAV export
- [ ] Project save/load

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
