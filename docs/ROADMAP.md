# ChiptuneTracker — Roadmap

**Last updated:** 2026-08-30
**Current version:** v3.0.0

This is the living plan. It supersedes the point-in-time notes in
`features_implementation_summary.md` and `implementation_plan.md`, which
describe a state the code has since moved past.

---

## Where the project actually stands

An audit on 2026-08-30 found the codebase is considerably further along
than the older docs claim. Already **built and working**:

| Area | Status |
|---|---|
| Piano roll, tracker view, arrangement view, mixer, pad controller | ✅ |
| 65 oscillators/instruments incl. drums, reggaeton, synthwave | ✅ |
| Per-note tracker effects: arpeggio `0xy`, vibrato, slide, duty cycle, pitch sweep, echo, retrigger, note cut/delay, tremolo | ✅ |
| Per-channel effects chain: bitcrusher, distortion, filter, EQ, compressor, formant, delay, chorus, flanger, phaser, tremolo, sidechain, reverb, stereo widener, tape saturation | ✅ |
| Master bus: EQ, glue compressor, brick-wall limiter, LUFS meter, platform presets | ✅ |
| Spectrum analyzer (FFT), wavetable editor, parameter automation curves | ✅ |
| MIDI export (GM mapped), MIDI input recording, voice-to-note tool | ✅ |
| WAV export, undo/redo, scale snapping, 10 visual themes | ✅ |
| Instrument macros, workspace layouts, custom widgets, test suite | ✅ (3.0) |

Fixed in **3.0** (see `CHANGELOG.md` for the full list):

- Startup crash — `STATUS_STACK_OVERFLOW` from a 384 KB flanger delay line
  held by value across eight effect chains on `WinMain`'s stack.
- Filter envelope, EQ, compressor and formant filter were settable in the
  UI but never reached the synth; channel config sync is now one code path.
- `Vocoder`, `KavinskyBass`, the seven reggaeton instruments and `Supersaw`
  were missing from the save/load tables and silently loaded back as `Pulse`.
- A second MIDI export in one session emitted no program changes.

So the work ahead is **not** "build the tracker" — it is **make it a
genuinely great *chiptune* tool, make it look like nothing else, and make
it trustworthy.**

---

## Guiding principles

1. **Authenticity is the product.** Anyone can ship a subtractive synth.
   What makes a chiptune tool good is honouring the constraints of the
   chips — and letting people break them on purpose.
2. **No hexadecimal tax.** FamiStudio's lesson: the tracker aesthetic is
   great, the tracker *data entry* is a barrier. Visual first, hex optional.
3. **Every feature must survive the test suite.** Silent breakage (the
   dead filter envelope, the `Pulse` downgrade) is the main historical
   failure mode here. Wiring gaps are caught by tests, not by hoping.

---

## Phase A — Trust (do this first, it gates everything)

Nothing else is worth shipping on top of a build that crashes or silently
drops data.

- [x] **A1. Fix the startup stack overflow.**
- [x] **A2. One channel-config sync path** so UI settings cannot go dead.
- [x] **A3. Save/load name-table symmetry** for all 65 oscillators.
- [x] **A4. Headless test harness** (`ChiptuneTests` target). Exercises the
      synth, sequencer, file I/O and exporters with no window. Must cover:
      - Every oscillator renders finite, in-range audio at the extremes of
        pitch (MIDI 0 and 127), velocity (0.0 and 1.0) and duration.
      - Save → load → save round-trips byte-identically for a project that
        uses every instrument, effect and note-effect field.
      - Sequencer timing: loop boundaries, zero-length patterns, clips at
        negative and past-end beats, BPM at its limits.
      - Effects at parameter extremes produce no NaN, no infinity and no
        runaway feedback.
      - Polyphony overflow — more simultaneous notes than voices.
- [ ] **A5. NaN / denormal guards** in the audio path, with a test that
      injects a NaN and asserts the mixer recovers rather than going silent.
- [ ] **A6. Autosave and crash recovery.** Rolling autosave to a temp file;
      offer to restore on next launch.
- [x] **A7. Fix the `nul` file** and other stray artifacts in the repo
      root; tighten `.gitignore`.

## Phase B — Chiptune authenticity (the differentiator)

Research across Furnace, FamiTracker/FamiStudio, ProTracker and the NES
2A03 documentation points at one dominant gap: **this synth has ADSR
envelopes where chiptune wants step macros.**

- [x] **B1. Instrument Macro System.** ⭐ The single highest-value feature.
      Per-instrument step sequences that advance on a tick, each with an
      optional loop point and release point:
      - Volume macro, arpeggio macro (with Furnace's per-step
        *relative* vs *fixed* flag), duty/waveform macro, pitch macro.
      - This is how chiptune instruments are actually designed. It replaces
        a dozen ad-hoc "instrument presets" with one system users can
        build any classic sound in.
- [ ] **B2. Chip emulation modes.** Select a chip per channel and have the
      tool honour its real constraints:
      - **NES 2A03** — 2 pulse (4 duties), 1 triangle (fixed volume), 1
        noise, 1 DPCM.
      - **Game Boy LR35902** — 2 pulse, 1 wavetable (4-bit, 32 samples),
        1 noise.
      - **C64 SID** — 3 voices, ring modulation, hard sync, shared filter.
      - **AY-3-8910 / YM2149** — 3 square, hardware envelope shapes.
      - **Genesis YM2612** — 6 FM channels.
      A "strict mode" toggle enforces the limits; off, they are suggestions.
- [ ] **B3. Authentic noise.** White vs periodic (metallic) LFSR modes and
      the NES's 16 discrete noise periods, not a continuous noise knob.
- [x] **B4. Volume quantization.** 4-bit (16-step) volume as a per-channel
      toggle — a surprisingly large part of why real chiptune sounds like
      chiptune.
- [ ] **B5. Hardware envelope shapes** (AY-style) and the NES sweep unit
      as a first-class per-channel unit rather than a per-note parameter.
- [ ] **B6. Legato / tie notes and true tone portamento** between adjacent
      notes, so slides read as one gesture rather than two events.
- [ ] **B7. Groove patterns** — per-row speed lists (e.g. `6 4 6 4`) for
      authentic tracker shuffle, alongside the existing swing control.

## Phase C — Composition and generation

- [ ] **C1. Euclidean rhythm generator.** Onsets distributed evenly over
      steps — the fastest known way to get a good drum pattern.
- [ ] **C2. Chord progression generator** with genre-aware voicings, and a
      one-click "arpeggiate this progression" that writes a classic fast
      chiptune arp.
- [ ] **C3. Scale/key lock on the piano roll** — grey out-of-key rows,
      optionally snap on draw. (`snapToScale` exists; wire it to the roll.)
- [ ] **C4. Song structure templates** — intro/verse/chorus scaffolding
      that generates an arrangement, not just a pattern.
- [ ] **C5. Melody assistant** — suggest a continuation in key over the
      current progression. Deliberately a suggestion, never an autopilot.
- [ ] **C6. Pattern variation tools** — humanize, invert, retrograde,
      transpose in scale, thin/densify.

## Phase D — Interop and export

- [ ] **D1. VGM export.** The chiptune scene's lingua franca; Furnace
      covers >80% of the spec and it is what people trade.
- [ ] **D2. NSF export** for actual NES playback/homebrew.
- [ ] **D3. MIDI import** to complement the existing export.
- [ ] **D4. Per-channel stem export** to WAV for mixing elsewhere.
- [ ] **D5. Finish sample import** — synth trigger integration, sample
      browser window, and sample references in `.ctp`. `Sample.h` and the
      `Voice` fields exist; the playback path does not.

## Phase E — Interface and experience

The theme system (8 themes with animated backgrounds) is a real asset and
better than most trackers. The gap is that the *widgets* are stock ImGui
inside beautifully themed windows.

- [x] **E1. Custom widget vocabulary.** Knobs, faders, meters and toggles
      drawn on the draw list, with gradient fills, inner shadow and a glow
      on the active element. This is what separates "an ImGui app" from
      "our app".
- [ ] **E2. Theme engine v2.** Promote themes from a `switch` of colour
      assignments to a data-driven `ThemeSpec` (palette + accent + glow
      intensity + background effect + rounding), so a new theme is a
      struct literal rather than 40 lines of copy-paste.
- [~] **E3. New themes with real effects.** Game Boy DMG (four-shade
      green with a dot-matrix grid) and Daylight (the first light theme)
      shipped in 3.0. Still wanted: Bubblegum Bass, Deep Sea (caustics),
      Solar Flare.
- [ ] **E4. Typography.** Ship a bitmap/pixel font for headers and a clean
      UI font for body text; stock ImGui font is the single biggest
      "unfinished" tell.
- [ ] **E5. Animated feedback** — channel meters that bounce, notes that
      flash on trigger, a playhead with a motion trail, transport buttons
      that pulse in time with the BPM.
- [ ] **E6. Command palette** (`Ctrl+P`) over every action, and a
      discoverable shortcut overlay (`?`).
- [x] **E7. Layout presets** — Compose / Mix / Sound Design workspaces,
      plus per-user layout persistence beyond `imgui.ini`.
- [ ] **E8. First-run onboarding** — a three-step overlay that gets a
      complete 8-bar loop playing in under a minute.
- [ ] **E9. Accessibility** — a colourblind-safe palette option, UI scale
      control for HiDPI, and no information conveyed by colour alone.

## Phase F — Release engineering

- [x] **F1. `CHANGELOG.md`** with real version history.
- [x] **F2. Versioning in one place** — a `Version.h` the About dialog,
      window title and save-file header all read from.
- [x] **F3. About dialog** (currently a `TODO` in `main.cpp`).
- [ ] **F4. Packaged release** — zip with the exe, docs and demo projects;
      tagged in git.
- [ ] **F5. Demo project pack** — one finished song per genre, which is
      also the best possible regression test.
- [x] **F6. Screenshot gallery for GitHub.** Once features are polished and
      tested, capture the app window per feature — piano roll, tracker
      view, arrangement, mixer, pad controller, wavetable editor,
      automation, spectrum analyzer, MIDI input, master effects, and each
      visual theme — and embed them in `README.md` under a Features
      section, so the repo sells the tool at a glance. Screenshots come
      *after* the UI work in Phase E, not before, or they are reshot.

---

## Ideas worth considering (not yet committed)

- **Chip-accurate sound preview in the palette** — hover an instrument and
  hear it, rather than placing a note to audition.
- **A "why does this sound wrong?" linter** — flags notes below a chip's
  frequency floor, channels exceeding chip polyphony, or masking in the
  spectrum.
- **Live-coding / performance mode** — trigger patterns from the pad
  controller with quantized launch, Ableton-Session style.
- **Piano roll ghost notes** — see other channels behind the current one.
- **Export to a playable HTML5 page** so a track can be shared as a link.
- **Version history inside the project file** — snapshot per session,
  scrubbing back to yesterday's arrangement.

---

## Sources consulted

- [Furnace tracker](https://tildearrow.org/furnace/) and its
  [instrument documentation](https://github.com/tildearrow/furnace/blob/master/doc/4-instrument/README.md)
  — macro design, relative vs fixed arpeggio steps, multi-chip support.
- [FamiStudio](https://famistudio.org/) — piano-roll-first workflow,
  envelope editing, NSF/ROM export.
- [Furnace NES system notes](https://github.com/tildearrow/furnace/blob/master/doc/7-systems/nes.md)
  and [NES APU reference](https://www.emulationonline.com/systems/nes/apu-audio/)
  — 2A03 channel layout and duty cycles.
- [ProTracker effect commands](https://www.worldofsam.org/products/protracker-effects)
  and [MilkyTracker effects](https://battleofthebits.com/lyceum/View/Milkytracker+Effects+Commands)
  — the standard effect vocabulary.
- [How to make 8-bit music](https://ozzed.net/how-to-make-8-bit-music.shtml)
  and [Mastering Chiptune](https://soundcy.com/article/how-to-make-chiptune-sound)
  — fast arpeggios as chord substitutes, duty modulation, sweep "pew".
- [dear-imgui-styles](https://github.com/GraphicsProgramming/dear-imgui-styles)
  and [ImGui styling docs](https://ocornut-imgui.mintlify.app/styling/colors-and-styles)
  — theme structure and what stock ImGui can and cannot do.
