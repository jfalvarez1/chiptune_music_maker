# Architecture

How ChiptuneTracker is put together, for anyone reading or changing the
source. Nothing here is needed to *use* the program - see the
[README](../README.md) for that.

## Design philosophy

- **Zero allocations in the audio thread.** Lock-free ring buffers carry
  everything from the UI to the audio callback. A `malloc` on that thread is
  a dropout waiting for a busy moment.
- **PolyBLEP antialiased oscillators.** Alias-free square, sawtooth and
  triangle.
- **LFSR noise.** The NES shift register, with its real tap positions and its
  sixteen hardware periods.
- **Minimal dependencies.** miniaudio and Dear ImGui, and nothing else.

## Audio thread safety

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

## PolyBLEP antialiasing

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

## LFSR noise

15-bit Linear Feedback Shift Register with taps at bits 0 and 6 (NES long-mode):

```cpp
uint16_t feedback = ((lfsr >> 0) ^ (lfsr >> 6)) & 1;
lfsr = (lfsr >> 1) | (feedback << 14);
```

## Source layout

Header-only throughout, apart from `main.cpp` and the audio analysis.

```
src/
  main.cpp              entry point, window, ImGui and audio device setup
  Types.h               Project, ChannelConfig, Note, Pattern, Clip
  UI.h                  every panel
  Layout.h              docking, workspaces, orphaned-window adoption

  audio engine
    Synthesizer.h       voices, oscillators, per-channel effect sync
    Sequencer.h         playback, the arrangement, aux buses, the mix
    Effects.h           the insert rack and every built-in effect
    MasterEffects.h     the mastering bus
    ChipMix.h           the 2A03's non-linear DAC

  instruments
    WavetableEngine.h   band-limited wavetable playback
    FMSynth.h           six-operator FM
    Sampler.h           key zones, velocity layers, round-robin
    GranularSynth.h     grain scheduling and playback
    DrumMachine.h       modelled kick, snare and hat
    ModMatrix.h         modulation routing, LFOs, second envelope
    InstrumentPresets.h starting points for each engine

  signal processing
    PitchShift.h        phase vocoder, formants, autotune, time stretch
    FFT.h               iterative FFT plan, allocation-free per transform
    AudioAnalyzer.cpp   pitch and drum detection (the voice tool)

  song structure
    TempoMap.h          tempo and meter changes, markers, regions
    Snap.h              where a beat lands on the grid
    LoopRange.h         the loop window

  input and capture
    VoiceCapture.h      lock-free ring for microphone audio
    LiveVoice.h         real-time pitch and beatbox tracking
    VoicePanel.h        the panel, and take handling
    MIDIInput.h         MIDI note input

  files and safety
    ProjectSerializer.h the .ctp format
    ProjectValidation.h what a loaded project is allowed to contain
    SettingsAudit.h     conflicting and dead settings
    Autosave.h          periodic snapshots
    CrashHandler.h      crash reporting

tests/    the headless suite, including the headless GUI harness
tools/    smoke test, gallery generation, theme audit
vendor/   miniaudio, Dear ImGui, libremidi, midifile
```

## Where the rules live

A few headers are deliberately the single place a decision is made, and are
worth knowing about before changing behaviour elsewhere:

| File | Owns |
|---|---|
| `src/ProjectValidation.h` | What every field of a loaded project is allowed to be. A `.ctp` is untrusted input. |
| `src/ProjectSerializer.h` | The file format. Save and load walk the *same* field tables, so a setting cannot be written but not read. |
| `src/SettingsAudit.h` | What counts as a conflicting or dead setting, for the Project Check panel. |
| `src/Snap.h` | Where a beat lands on the grid. Was once duplicated across fourteen call sites. |
| `src/Genres.h` | Which palette sections, tools and engines each genre focus puts in front of you. |
| `src/Version.h` | The version, once. It was written twice and the two disagreed for two releases. |

## Plugin hosting

`src/PluginHost.h` hosts VST2, VST3 and CLAP behind one interface. A hosted
plugin is an `IEffect` like any built-in — `processBlock()` and
`latencySamples()` exist on that interface for exactly this reason, since a
plugin is block-native and reports its own latency while a built-in is
neither.

**No format loader ships.** Everything else does, and is tested: discovery,
the scan cache, the descriptor and parameter model, the lock-free parameter
queue, instantiation, the audio path, project persistence, and the
missing-plugin path. A loader is *registered* rather than compiled in, so
the tests register one and drive the whole path end to end.

| Format | What is needed to finish it |
|---|---|
| CLAP | Vendor the MIT headers, write the loader, test against a real `.clap`. The only one with no licensing question — do this one first. |
| VST3 | Steinberg's SDK is a separate download under a dual GPLv3/proprietary licence. Vendoring it is a licensing decision, not a technical one. |
| VST2 | Steinberg withdrew the SDK in 2018. It cannot be legally obtained or redistributed. A VST2 loader cannot ship. |

Two things shape the design and should survive any change to it:

- **A plugin lives outside the project, so it can always be missing.** A
  project opened where the plugin is not installed keeps the slot, the
  parameter values and the plugin's opaque state, and says so. Silently
  dropping it would throw away work the user cannot get back — the same
  rule as a moved sample.
- **A plugin insert delays its channel by one block, and that is both
  reported and compensated.** `DelayCompensation.h` levels the whole graph:
  every channel is delayed to meet the latest one, the channel sends into a
  bus are levelled against whatever else arrives there, each bus's output is
  delayed to match where it is going, and the direct path waits for the
  buses to come back. Every path from a channel to the master is the same
  length, so a channel that is quietly late is no longer a phase problem the
  user cannot see or fix — what is left is monitoring delay, which the
  plugins panel states in milliseconds.

  Writing that meant measuring the phase vocoder's delay rather than
  trusting its own report, which found it was late by twice what it claimed:
  the overlap-add read head ran ahead of the writes and picked up frames
  from the ring's previous lap. A latency an effect reports about itself is
  worth nothing until something measures it.

## Testing

See [TEST_PLAN.md](TEST_PLAN.md).
