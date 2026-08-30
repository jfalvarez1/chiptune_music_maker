# Changelog

All notable changes to ChiptuneTracker.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [3.4.0] - 2026-08-30 - "Legible"

### Fixed

- **Nine of the ten themes had unreadable button labels.** Seven were under
  3:1 and the worst sat at **1.22:1** - pressing a button made its own label
  almost vanish. The cause was the same everywhere: hover and active states
  brighten the fill, and ImGui draws every label in a single
  `ImGuiCol_Text`, so on a dark theme with light text the label got *harder*
  to read the more you interacted with it.

  This hid for two releases because the contrast audit only compared `Text`
  against `WindowBg`, where every theme passed comfortably. Labels are drawn
  on Button, Header, FrameBg, Tab, TitleBg and MenuBar too.

  The derive pass now walks every surface a label lands on and moves it away
  from the text - darker under light text, lighter under dark text - until
  it clears 4.5:1. Only value moves, so every theme keeps its hue. It
  applies automatically to any theme added later.

- **Game Boy, rebuilt on the DMG-01's actual colour.** The palette everyone
  copies (`0f380f / 306230 / 8bac0f / 9bbc0f`) is community convention, not
  measurement: the DMG screen is a reflective STN panel with four
  transmission levels behind a green polariser, so there is no RGB in the
  hardware to sample. Palettes taken from photographs of a running unit come
  out markedly more olive and much less saturated - around
  `1b2a09 / 0e450b / 496b22 / 9a9e3f`.

  Convenient, because the accurate direction and the comfortable direction
  turned out to be the same direction. The four sampled shades are used
  literally in the piano roll, where there is no text; the chrome uses the
  same hue family with the value spacing a label needs.

  This is the DMG-01 specifically. The Pocket and Light moved to FSTN and
  read as neutral grey; the Color, Advance, SP and Micro share none of it.

- **Channel identity colours ignored the theme**, which is how a four-shade
  olive Game Boy ended up with orange notes and a red channel label. Both
  now blend toward the theme rather than overriding it, so identity survives
  and a monochrome theme stays monochrome.

### Added

- **Theme legibility is now a test, not a hope.** `ChiptuneTests` links
  ImGui and calls `ApplyTheme` for real, then checks every one of ten themes
  against seventeen surfaces - 170 contrast assertions - plus that Header
  and Button stay distinguishable, that interactive alpha survives the
  correction, and that no colour slot is left unset.

  It also applies all ten themes in sequence and re-applies one, asserting
  the result is identical: a theme that inherits a value from whichever
  theme preceded it looks different depending on the route taken to it.

- **`tools/audit-themes.py`** reports each theme's contrast twice: as
  authored, and as it ships after the automatic correction. A large gap
  means the palette is leaning on the correction rather than being designed
  for it - true of nine themes today, and worth knowing when tuning by hand.

---

## [3.3.0] - 2026-08-30 - "Euclid"

### Fixed

- **The spectrum analyzer was a solid wall at every frequency.** Its FFT
  output was never normalised, so a 2048-point transform of any audible
  signal exceeded the "-60 dB to 0 dB" display range and every bin clamped
  to maximum. Scaling by the transform length and the window's coherent
  gain puts a full-scale sine at 0 dBFS, where it belongs.

- **The analyzer ran its entire FFT inside the audio callback** - growing a
  vector, erasing from its front, allocating scratch buffers, and taking a
  mutex the UI thread also held. Any one of those can stall the callback
  and produce a dropout. The audio thread now writes one sample into a
  lock-free ring buffer and nothing else; the windowing, transform and dB
  conversion happen on the UI thread. The transform itself is iterative
  with precomputed tables, so it allocates nothing at all.

- **Four oscillators shared state across every voice and channel.** The
  noise LFSR clock, the acid filter, the vinyl hiss filter and the gated-pad
  smoother were `static` locals inside their generators, so two notes on the
  same oscillator - or two channels - fed each other. Eight noise voices all
  advanced a single accumulator. They are per-voice now.

- **The NES noise LFSR taps were inverted.** On a 2A03 the feedback bit is
  bit0 XOR bit1 normally and bit0 XOR bit6 in short mode; the code had them
  the other way round, so "short mode" produced white noise and the default
  produced the metallic periodic tone.

- **Stem export never created its output folder**, so choosing a new
  directory wrote nothing. Caught by its own test the first time the temp
  directory was absent.

### Added

- **Authentic NES noise.** The sixteen fixed noise periods real hardware
  offers, selectable per channel. That stepping is most of what makes NES
  noise sound like NES noise rather than like filtered hiss. "Track note"
  remains available for tuned percussion, which no hardware did but is
  useful.

- **Euclidean rhythm generator.** Distributing k onsets as evenly as
  possible over n steps produces a startling number of the world's actual
  drum patterns - E(3,8) is the tresillo, E(5,16) the bossa clave. In the
  Tools panel with a live step preview, per-instrument or as a full kit,
  and nine named presets.

- **Stem export.** One WAV per channel, each rendered with the others
  muted, so every per-channel effect behaves exactly as it does in the mix.
  Silent channels are skipped and the project's mute/solo state is restored
  afterwards.

- **Frutiger Aero, properly.** The theme read as "light blue" because it was
  missing both halves of its own aesthetic. The background now has a sky
  gradient with a sun and lens flare, drifting clouds, bokeh, a layered
  grass band with caustic light, rising glossy bubbles and tropical fish;
  the palette takes back the green half of white/green/blue.

- **A standalone executable attached to the release.** The build already
  links the CRT statically, so it runs with nothing else installed.

### Changed

- `writeWavFile` extracted so the mixdown and the stems produce byte-identical
  files from one implementation.
- `docs/TEST_PLAN.md` states the policy explicitly: every feature ships with
  its coverage in the same change.

---

## [3.2.0] - 2026-08-30 - "Palette"

A full pass over the ten themes, driven by an audit that turned up one
finding bigger than any individual theme.

### Fixed

- **39 of ImGui's 63 colour slots were never assigned by any theme.**
  Scrollbars, separators, resize grips, table headers and row striping,
  plot lines, text selection, the modal dim layer, dimmed tabs and the nav
  cursor kept ImGui's dark defaults - identically in all ten themes. On a
  dark theme that read as unfinished; on Frutiger Aero, a genuinely light
  theme, it was broken: dark grey scrollbars on a pale blue window.

  They are derived now, once, from what each theme already chose -
  scrollbars from the button family, separators from the border, plots from
  the accent - branching on the perceptual luminance of the window
  background so light and dark both come out right. A theme added later
  gets complete styling for free, and can still override any slot
  explicitly.

- **A clip could index the synth array out of bounds.** `processNoteEvents`
  validated a clip's pattern index but not its channel index, then used it
  to index eight 35 KB synths - so a clip naming channel 99 wrote megabytes
  past the array from the audio thread. It only crashed under some
  launchers, because whether an out-of-bounds write faults depends on the
  process memory layout.

- **The animated backgrounds became invisible when docking landed.** Docking
  tiles the viewport exactly, so the Matrix rain and the Synthwave sun and
  grid had nowhere to appear. The dock space is inset now and panels have
  visible gutters, so the animation frames the workspace and runs between
  panels - never underneath text.

- **Stock's Header and Button were byte-identical**, so nothing indicated
  what was pressable. Same defect in Game Boy and Daylight.

- **Minimal's accent was red.** In a DAW red means record, and also
  destructive and error - so record, delete, error and an ordinary OK
  button were one colour. The accent is teal now, and red is reserved: it
  appears nowhere in the theme except the playhead.

- **Cyberpunk's body text was pure saturated cyan**, which shimmers on
  subpixel layouts and leaves no way to emphasise anything. Near-white with
  a cyan cast now, with pure cyan reserved for accents.

- **Interactive surfaces were translucent over animated backgrounds.**
  Buttons at 0.40-0.70 alpha meant labels were read against moving colour
  in Cyberpunk, Synthwave, Vaporwave, Matrix, Retro Terminal and Daylight.
  All interactive surfaces are now at least 0.85.

- **The piano roll ignored its own theme colours.** Every theme defines
  `noteDefault` and `noteSelected`; nothing read them, so notes were the
  same eight channel colours under all ten themes. Notes now take the theme
  colour, tinted toward the channel identity.

- **The sound palette's category headers were hardcoded** - maroon and
  purple groups sitting inside the Game Boy's four-shade green. The
  category hue is a tint over the theme's own header colour now.

- The active edit-mode button used a hardcoded fill, which on Matrix was
  green text on a green button. It takes the theme accent with a label
  colour chosen against it.

### Changed

- **Synthwave and Vaporwave were two shades of the same purple.** They are
  split deliberately now: Synthwave is saturated neon on near-black,
  Vaporwave is pastel and hazy on a lifted, mid-toned ground.
- **Every theme has its own geometry**, not just its own colours - corner
  radius, border weight and grab size. A Game Boy is square with heavy
  borders; Frutiger Aero is all bubbles; the terminals are hard-edged.
  Spacing and padding stay uniform, because those are about usability.
- Body text now reaches at least 4.5:1 against the window background in all
  ten themes; the lowest is Game Boy at 6.06:1.

### Added

- The screenshot gallery covers **features and modes**, not just views: the
  three piano-roll edit modes, an active note selection with the Note
  Editor populated, both macro editor tabs, and each analysis panel.
  New capture flags: `--mode`, `--select`, `--playing`, `--macro-tab`, and
  `--show` extended to every optional panel.

---

## [3.1.0] — 2026-08-30 — "Dockyard"

Panels dock. Tests grew a second layer and a written plan. One more silent
bug found and fixed.

### Added

- **Real docking.** Dear ImGui upgraded to the docking branch (1.92.6 →
  1.93.0). Panels dock into shared regions and become **tabs** when they
  share one, can be dragged anywhere, and the arrangement persists.
  This is the model the established tools converged on independently:
  Reaper's Docker with Screensets, Bitwig's resizable docked panels, and
  Furnace — itself an ImGui tracker — describing "the most flexible and
  customizable tracker interface ever". The three workspaces are now dock
  trees built with `DockBuilder`, so they are a good starting point rather
  than a cage.
- **UI smoke test** (`tools/ui-smoke-test.ps1`). 26 cases driving the app's
  `--capture` mode through every view, theme, workspace and panel, plus
  empty projects and window sizes from 800×600 to 2560×800. Each asserts
  the process exited cleanly and the frame is not blank or near-flat. It
  fills the gap the headless suite cannot reach: the renderer.
- **`docs/TEST_PLAN.md`.** What is tested, how, and — deliberately — the
  seven things that are *not*, so a green run is not mistaken for full
  coverage.
- **`--workspace` capture flag**, so workspace layouts are screenshot- and
  smoke-testable.
- **221 new headless checks** covering the gaps called out last release:
  undo/redo (cap, redo invalidation, deep round trips), mute/solo/pan
  routing, live note triggering and preview across all 65 oscillators,
  wavetable morphing and bounds, whole-project validation field by field,
  and a 60-second stability run checking for level drift and playhead
  escape.

### Fixed

- **Enabling the master EQ silenced the entire song.** `ThreeBandEQ` takes
  linear gain where 1.0 is unity; the project stores decibels, and the
  value was passed straight through. Switching the master EQ on at its
  default of 0 dB multiplied the mix by zero. The master compressor's
  threshold and makeup gain had the same mismatch. Found by the new
  long-run test, which rendered sixty seconds of perfect silence while
  every individual channel showed healthy signal.
- The Transport panel was clipped and never showed its master volume row.
- Tool windows opened *behind* the main editor, which is indistinguishable
  from not opening.
- `imgui.ini` was tracked in git, so a fresh clone inherited one machine's
  stale window layout.

### Changed

- `Combo()`'s getter signature changed upstream; the Voice-to-Note tool's
  call was updated.

---

## [3.0.0] — 2026-08-30 — "Macro"

The release that makes this a chiptune tool rather than a synth that can
play chiptune, and the release that makes it trustworthy: a headless test
suite now covers 1112 assertions, and it found the bugs listed below.

### Added

- **Instrument macros.** Four step sequences per instrument — volume
  (4-bit), arpeggio (relative or fixed per step), duty cycle and fine
  pitch — each with its own loop and release point, advancing at a
  configurable rate that defaults to the NES frame counter's 60 Hz. This
  is how chip instruments are actually designed; an ADSR envelope is a
  synthesiser idea. Eight ready-made instruments included: Pluck,
  Sustained, Major/Minor Arp, Laser Zap, Vibrato Lead, Bass Stab, Octave
  Echo.
- **Macro editor** (`F4`). Drag across a bar graph to draw a sequence,
  right-click an arpeggio step to make it fixed, with loop and release
  markers drawn on the graph.
- **Per-channel 4-bit volume quantisation.** Real chips had a 16-level
  volume DAC, and that staircase is a genuine part of the character.
- **Workspace layouts** (`Ctrl+0`, or View ▸ Workspace). Compose, Sound
  Design and Mix. Computed from the display size, so panels tile instead
  of overlapping and the layout scales from a laptop to an ultrawide.
- **Custom widget set.** Knobs with a proper sweep and shift-for-fine,
  level meters with peak hold, animated toggle switches, accent buttons.
  The mixer is rebuilt around them, fed by new per-channel level metering.
- **Two themes:** Game Boy DMG and Daylight, the first light theme.
- **Screenshot capture.** `F12` writes the rendered frame; `--capture`
  drives it from the command line with `--theme`, `--view`, `--show` and
  `--demo`, so `tools/generate-gallery.ps1` can regenerate the README
  gallery after any UI change.
- **Headless test suite** (`ChiptuneTests`). 1112 checks over every
  oscillator, note effect and channel effect at its parameter extremes,
  polyphony overflow, sequencer and arrangement edge cases, save/load
  round-trips, malformed project files, and both exporters.
- **About dialog**, and a single source of version truth in `Version.h`.

### Changed

- **Project file format v2.** v1 stored seven fields per note and nothing
  else. Saving a finished song kept the notes and silently discarded every
  channel setting, all 21 per-channel effects, the entire arrangement, the
  master bus, swing and groove, and all 19 per-note tracker effects. v2
  stores all of it. v1 files still load, with fields they never had left
  at their defaults, and re-saving upgrades them in place.
- Save and load now walk the same field tables, so a setting cannot be
  written but not read.
- The spectrum analyzer, MIDI input, automation and wavetable panels are
  opened from the View menu instead of being drawn unconditionally.
- Tool windows are submitted after the main editor view, so they open in
  front of it rather than behind it.
- The piano roll takes its background, keys and grid from the active
  theme. It was hardcoded, so the main editor looked identical under every
  theme while only the window chrome changed.
- Shared style metrics — padding, rounding, spacing — applied under every
  theme.

### Fixed

- **Startup crash** (`STATUS_STACK_OVERFLOW`). `Flanger` held a
  `std::array<float, 96000>` — 384 KB — by value for an effect that needs
  at most 11 ms of delay. Eight of those inside a `Sequencer` on
  `WinMain`'s stack overflowed the 1 MB limit before the first frame.
- **Oscillator phase ran away above the sample rate.** The wrap subtracted
  1.0 once, so any phase increment over 1.0 left the phase above 1 and it
  grew every sample; PolyBLEP then squared it. An extreme pitch sweep
  peaked at 2.4 × 10⁷ instead of ~1.0.
- **The stereo widener never ran.** The effects chain deferred it to the
  sequencer "for proper L/R handling" and the sequencer never did it.
- **Sidechain never ran.** `sidechainSource` was never copied from the
  channel config, and the sequencer skips the pass when it is unset.
- **The filter envelope, EQ, compressor and formant filter never reached
  the synth.** They were settable in the UI and by the genre presets, but
  nothing copied them across. Channel config sync is now one code path.
- **Instruments loaded back as `Pulse`.** `Vocoder`, `KavinskyBass`,
  `Supersaw` and all seven reggaeton instruments were missing from the
  save/load name tables.
- **The compressor inverted and amplified** when given a threshold at or
  below zero — the computed gain went negative.
- **`exportWav()` crashed** on a zero or negative duration: a negative
  float cast to `size_t` produced a colossal length inside `resize()`.
- **A second MIDI export in one session emitted no program changes**, so
  every instrument played back as piano. The tracking flag was a
  function-local `static` that never reset.
- **Project files were accepted with impossible values** — a BPM of 10³⁰,
  a negative song length — which then reached the audio thread.
  `ProjectValidation.h` now decides what a usable value is for every
  field, on load.
- `vendor/midifile` was committed as an embedded git repository, so a
  fresh clone would not build.
- Two new instruments, Vocoder and Kavinsky Bass, existed in the synth but
  could not be selected. They now have a Recreations section in the sound
  palette, with a `static_assert` keeping the palette index-aligned with
  the oscillator enum.

---

## [2.16.0] — Wavetable Editor

Visual waveform drawing, morphing between tables, save/load presets.

## [2.15.0] — Parameter Automation

Visual curve editor and per-channel automation lanes.

## [2.14.0] — MIDI Input Recording

Real-time MIDI keyboard input with Replace/Overdub modes and quantisation.

## [2.13.0] — Spectrum Analyzer

Real-time FFT frequency visualisation.

## [2.12.0] — Master Bus Effects

Limiter, glue compressor, EQ and LUFS metering with platform presets.

## [2.11.0] — Stereo Widener and Tape Saturation

## [2.10.0] — Reverb, Genre Effect Presets
