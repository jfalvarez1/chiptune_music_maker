# Changelog

All notable changes to ChiptuneTracker.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [3.9.0] - 2026-08-31 - "Comp"

Building a keeper out of several takes, finding things by name instead of by
memorised key, and the shape of plugin hosting.

### Added

- **Take lanes and comping.** Record a part several times, then build the
  keeper out of the best moments of each. Every pass around the loop becomes
  its own lane; drag across a lane to choose it for that stretch. Punch in to
  repair one phrase without risking the rest of the take.

  The comp is an editing model, not a playback one: swiping decides which
  take wins where, and flattening turns those choices into ordinary audio
  clips on the arrangement. Playback never sees a comp - it sees clips -
  which is what lets comping reuse the whole tested audio-clip path instead
  of duplicating trimming, fades and sample-rate conversion inside a second
  one, and it means the result stays editable afterwards like anything else.

- **Command palette.** `Ctrl+P` finds any command by name. `Ctrl+/` lists
  every shortcut, and any of them can be rebound. Shortcuts used to be both
  fixed and unlisted, which meant the only way to learn that `Ctrl+0` resets
  the layout was to be told.

  Bindings save only where they differ from the default, so changing a
  default in a later version still reaches everybody who never rebound that
  command.

- **Browser.** One docked place for the samples in the project, every
  instrument and engine, the shipped presets, and the audio and project
  files on disk - with a fuzzy filter, and drag onto a channel or onto the
  timeline. Before this, finding something required already knowing it
  existed.

- **Convolution reverb**, with an impulse response library, and **five more
  reverb algorithms** on the one existing slot rather than five more slots.

- **Four more equalisers**: tilt, ten-band graphic, mid-side on the master
  bus, and dynamic. Linear-phase is deliberately absent - it costs 50-100 ms
  of latency and nothing in the mixer compensates for latency yet.

- **Plugin hosting**, as far as it can honestly go. Per-channel racks for
  VST2, VST3 and CLAP behind one interface, a scanner, and a scan cache.

  **No format loader ships.** Steinberg withdrew the VST2 SDK in 2018 and it
  cannot be redistributed; the VST3 SDK is a separate download under a dual
  GPLv3/proprietary licence, which is a licensing decision rather than a
  technical one; CLAP is MIT and is the one to finish first. Everything
  around the loaders ships and is tested, and a project made where plugins do
  load keeps every plugin, its parameters and its state when opened here.

### Fixed

- **45 Channel Editor effect controls never saved, and were silently
  reverted.** They were bound to the live effects chain rather than to the
  channel's settings, so every one of them was overwritten the next time the
  channel was touched. Five had no setting to save into at all.
- **The Save button's tooltip has always said `Ctrl+S`**, and nothing
  anywhere handled it. It does now.
- **A browser pointed at a relative path offered no way out of it** - a bare
  folder name has no parent path, so `..` was never listed.
- **The Take Lanes delete button sat on top of the first beat of every
  lane.**

### Changed

- Project format v7, for the take lanes and the plugin racks. Both are
  omit-if-default: a project that uses neither writes exactly the bytes it
  wrote at v6.

---

## [3.8.0] - 2026-08-31 - "Check"

Two things this release is about: testing the UI without a window, and
telling the user when a setting is switched on and doing nothing.

### Added

- **Headless GUI testing.** An ImGui context with a built font atlas, a fake
  display and no backend at all, driving the real panel code through real
  frames as part of the headless suite. It catches three things the
  rendering smoke test cannot: **ID stack imbalance** (a `PushID` without its
  `PopID` corrupts every widget ID after it, so unrelated controls silently
  share state - two sliders that move together, a checkbox that toggles its
  neighbour), **`Begin`/`End` imbalance** (which breaks every panel drawn
  afterwards, not just the guilty one), and **NaN geometry** (which a GPU
  discards silently, so a rendering test still sees a plausible frame while
  a control has vanished).
- **Click testing.** Real `ImGuiIO` mouse events through real widgets. A
  click is three frames - move and press, hold, release - because ImGui
  reports it on release and only if the press landed on the same widget;
  doing it in fewer silently does nothing, which is how a naive attempt at
  this passes while testing nothing. Widget positions come from ImGui via a
  probe frame rather than being guessed, so the tests do not quietly stop
  hitting anything when a layout moves. Coverage: thirteen panels, seven
  engine editors, five display sizes from 320x240 to 3840x2160, eight genre
  focuses, ten themes, and clicks and drags.
- **Project Check.** Validation asks "could this crash". This asks whether
  the project is quietly not doing what its owner thinks. Every finding is a
  setting that is legal, saved, displayed as enabled, and has no effect - or
  silently cancels something else: a send into a muted bus, an effect on
  with its mix at zero, a modulation route pointing at an engine the channel
  is not running, a soloed channel three screens away that is why nothing
  else is audible. Each says what, why and what to do, and there is a test
  that none of them is missing a part.

### Changed

- **The genre focus now covers the instrument engines.** Chiptune is shown
  wavetable and FM - the Game Boy's wave channel and the Mega Drive's
  YM2612 - and not the multisample sampler, which is the one thing that era
  could not do. Hip hop is the reverse. Same rule as the rest of the focus
  system: a filter on attention, never on capability, with the Type menu
  still listing everything and the hidden count reporting honestly.
- **The Channel Editor is reorganised.** Its Oscillator section had grown to
  850 lines holding every engine plus the modulation matrix, with the noise
  controls stranded 800 lines below the other basic settings because each
  new engine was appended in front of them. Separate headers now:
  Oscillator, the engine in use, and Modulation.
- **`Theme` gained a `Count` sentinel** like every other enum here, so the
  tests stop carrying a hand-written list of all ten.
- **`docs/TEST_PLAN.md` gaps list rewritten.** It named a synthetic input
  harness as "the single biggest remaining gap"; that is closed. What
  remains is the custom-drawn editors - piano roll, arrangement, pad - which
  hit-test canvas coordinates inside an `InvisibleButton` rather than using
  ImGui widgets.

### Fixed

- **Project Check's suggested-fix line was clipped rather than wrapped.**
  `TextColored` does not wrap, and the fix is the half of a finding the user
  actually needs.

### Testing

3685 checks across 82 groups, 57 UI rendering cases. Every genre starter
template and every quick-start kit is asserted to produce no audit findings
at all - a panel that always has something in it is one everybody learns to
ignore, at which point it is worse than not having it.

---

## [3.7.0] - 2026-08-31 - "Instruments"

Five configurable instrument engines, a modulation matrix, audio on the
timeline, and song structure. Everything here defaults to off or absent, so
an existing project sounds exactly as it did.

### Added

- **Wavetable synthesis.** The editor had existed for a long time and drove
  nothing - the "Custom" oscillator ran `generateTriangle()`, so you could
  draw a waveform, watch the preview redraw, save it, reopen it, and hear a
  triangle. There is a real engine now, band-limited with mipmapped tables
  so a drawn square played three octaves up is dull rather than full of
  alias tones descending as the note ascends.
- **Six-operator FM.** A full routing matrix rather than a numbered list of
  algorithms, feedback, and a separate envelope per operator - which is
  where FM's expressiveness actually lives. Modulation is applied to phase,
  not frequency; feedback averages the last two samples, not the last one.
  Both are the standard mistakes and both are tested.
- **Multisample sampler.** Key zones, velocity layers and round-robin, so
  repeated hits do not comb-filter against each other.
- **Granular synthesis.** Grain size, density, spray, pitch scatter, reverse
  chance and window shape. Freeze the read position for a drone that holds
  one moment without moving its pitch.
- **Modelled drums.** The editable version of the 21 fixed drum voices, built
  the way the analogue circuits were: the kick's pitch sweep, the snare's
  shell-versus-rattle balance, the 808 hat's six inharmonic squares.
- **Modulation matrix.** Nine sources to nine destinations, per-voice LFOs
  and a second envelope so held notes do not wobble in lockstep, plus
  per-channel polyphony limits and pitch-bend range.
- **Audio clips on the timeline.** Recorded or imported audio on a channel,
  running through that channel's volume, pan, effects and sends like
  anything else.
- **Tempo and time-signature changes**, markers and regions. Beat-to-seconds
  is a piecewise integral now; bars are counted through the meter map rather
  than found with a modulo.
- **Voice to notes.** Sing or beatbox and get notes, live or from a recorded
  take. The capture callback fills a lock-free ring and does nothing else.
- **Pitch shift, formant shift and autotune** - a phase vocoder - plus an
  offline time stretch wired to audio clips as "Fit to Clip".
- **Starting points for every engine**, since picking one otherwise gave you
  a single sound and sixty-odd parameters.

### Fixed

Four bugs, all older than the work that found them:

- **Sample playback was a semitone and a half sharp.** The read step never
  accounted for the 48 kHz pool against the 44.1 kHz engine, so every sample
  instrument was out of tune with the synths.
- **The per-channel filter cutoff and resonance did nothing.** `process()`
  reads cached coefficients and only `setCutoff()` recomputed them, so the
  filter ran at its 1000 Hz default wherever the slider was.
- **The filter went unstable above about 7 kHz** once that was fixed,
  producing NaN - silence for the rest of the session. It had never
  surfaced because the first bug pinned the cutoff.
- **MIDI export wrote the wrong time signature**, `x/2` rather than `x/4`, so
  any DAW importing one saw bars of twice the intended length.
- **The loop ruler was unreachable** after the channel count went to 32: it
  was positioned below the last lane, roughly 960px down.
- **The Channel Editor's type dropdown selected the wrong instrument.** Six
  names indexed straight into an enum whose sixth entry is `Supersaw`, not
  `Custom`.
- **Five engines were invisible in the Sound Palette**, which drew types 0 to
  64 and stopped.

### Testing

3604 checks, 55 UI cases at the close of this release.

---

## [3.6.0] - 2026-08-30 - "Welcome"

The research pass was unambiguous about how projects die: a blank page nobody
can start, or an eight-bar loop nobody can turn into a song. This release is
aimed at both - and every piece of it is optional, dismissible, and leaves
the manual way of working exactly as it was.

### Added

- **A first-run welcome.** The program asks what kind of music you are here
  to make - Chiptune, Synthwave, Hip Hop, Reggaeton, EDM, Rock, Lofi - with
  "Other, show me everything" as a real answer beside the rest. Asked once
  and remembered; choosing Everything is remembered as an answer too, not as
  never having been asked.
- **Genre focus.** The choice decides which palette sections, generators and
  panels are put in front of you. Nothing is removed: the View menu lists
  every panel, and both the palette and the Tools panel carry a "show
  everything" switch that says exactly how much is being held back.
  Changeable at any time from the Views panel.
- **Starter templates.** Four bars that already play - drums, a bass
  following a progression, chords and a lead, in the genre's key and tempo.
  In the welcome and under File > New From Template. Deterministic, and
  undoable like any other edit. The drums are one bar placed four times,
  because pattern reuse is the habit that turns bars into songs.
- **Quick Start kits.** The patterns each style is built on - the dembow,
  boom bap, four on the floor, octave bass, minor pop chords, a triad arp -
  as buttons at the top of the Tools panel. Each writes ordinary notes into
  the selected pattern: one undo step, editable, layerable, and never
  displacing notes already there. A faster pencil, not a different
  instrument.
- **A next-step hint.** One line in the menu bar that reads the project and
  names one concrete thing to do next. A complete loop in a single pattern
  is pushed toward a variation rather than another layer, because the layer
  is the trap. Dismissible with one click, restorable from the View menu,
  and the dismissal persists.
- **User settings.** A small tolerant file of its own - deliberately not a
  corner of imgui.ini, so deleting a broken layout does not also make the
  program forget who it is talking to. Genres are saved as stable tokens so
  a reordered enum or a reworded name cannot silently change anyone's
  choice.

### Fixed

- The next-step hint's bass/melody boundary was C3, so a template in A minor
  with its bass on A3 was told to add a bassline. A screenshot caught the
  wrong hint; the boundary is middle C now, with a regression test naming
  the incident.
- The first dembow written for the kits had six snare hits including a
  straight backbeat on beat two - precisely what a dembow is not. The 3-3-2
  test refused it; the recipe is the canonical four hits in both the kit and
  the template.
- The roadmap had drifted from reality: the Euclidean generator, stem
  export, first-run onboarding and packaged releases were all shipped but
  unchecked, and four half-done items did not say which half. Audited
  against the code and corrected.

### Testing

2090 headless checks (up from 1883) and 39 UI smoke cases (up from 32).
New groups cover the tracker grid, genre focus and its profiles, user
settings, the templates, the kits, and the next-step ladder - including
that no starter template is ever told to add something it already has.

---

## [3.5.0] - 2026-08-30 - "Reachable"

A research pass across FL Studio, Furnace, Renoise, OpenMPT, FamiTracker,
LSDj and Bosca Ceoil turned up a lot of features worth building. It also
turned up something more useful: an audit of this codebase found eight things
that were already written, stored and serialised, and that no user could
reach. Those were cheaper to fix than any new feature, and every one of them
undermined a claim the README already makes. This release is that list.

### Added

- **Loop range on the timeline.** Drag the ruler under the arrangement
  tracks to set the span playback repeats over, with the looping region
  shaded across the tracks. Pressing play drops the playhead inside the
  range, so play always audibly does something.
- **Transpose.** `Shift+Up`/`Down` by a semitone, `Ctrl+Shift` by an octave,
  plus toolbar buttons. Applies to the selection, or to the whole pattern
  when nothing is selected. Notes that would leave the playable range are
  skipped rather than clamped, so a chord cannot silently collapse onto one
  pitch.
- **Note probability.** A per-note chance, rolled fresh on every pass through
  the loop. A sixteen-step chiptune loop repeats a great deal, and this is
  the cheapest thing that stops it sounding like one.
- **Selectable grid snap**, from a bar down to a 1/32, including 1/4, 1/8 and
  1/16 triplets. `[` and `]` step through the divisions, `Alt` bypasses the
  grid for a single gesture. Without triplets, shuffle and 6/8 were simply
  unwritable.
- **A To Scale button** that pulls a selection into key.

### Fixed

- **The loop range was unreachable, and the engine ignored it anyway.**
  `loopStart` and `loopEnd` have been on `PlaybackState` since the beginning,
  but `setLoop()` was called exactly once - at startup, with
  `(false, 0, 16)` - and the playback loop never read `loopEnd` at all; it
  wrapped at the end of the content instead. Wrapping now folds with `fmod`
  rather than a single subtraction, because a range shortened under a running
  playhead can leave it several windows past the end.
- **Four note effects were stored but never played.** `noteDelay`,
  `noteCut`, `retriggerCount` and `echoRepeats` were declared on `Note` and
  written to the `.ctp` file, with zero references in the synth or the
  sequencer. They are the classic tracker commands, and they are how chiptune
  gets flams and stutters without spending another channel. All four are now
  editable and audible.
- **Pitch sweep had no control.** Fully implemented in the audio path since
  the NES sweep unit went in, and reachable from nowhere.
- **`Edit > Undo` was literally `if (ImGui::MenuItem("Undo")) {}`**, and the
  real undo covered notes in a single pattern. Editing a macro, a channel
  effect or the arrangement was not undoable at all. A snapshot is now the
  whole project, taken through the same serializer the `.ctp` format uses -
  so a field that survives save and load survives undo automatically.
- **"Snap to Scale" set a flag that nothing read.** The scale tables and
  `snapToScale` existed, but sat several thousand lines below the piano roll
  in `UI.h`, out of reach of the code that places notes.
- **Grid snap was hardcoded** as `floor(beat * 4) / 4` in fourteen separate
  places.

### Changed

- Scale tables, snap arithmetic, loop-window resolution, note expansion,
  selection transforms and the undo history all moved into headers of their
  own - `Scales.h`, `Snap.h`, `LoopRange.h`, `NoteEvents.h`,
  `NoteTransforms.h`, `UndoHistory.h`. All are free of ImGui and of `Project`
  mutation, which is what makes them testable; three of the bugs above
  existed precisely because the logic was buried inside a 553 KB UI file.
- `ProjectSerializer` gained stream-level `writeProject`/`readProject`, with
  the file functions as thin wrappers. Undo snapshots go through the same
  pair, so the two paths cannot drift apart in what they preserve.

### Testing

1883 headless checks (up from 1752) and 30 UI smoke cases (up from 28).

The new audio tests assert the feature is audible rather than that the code
ran: note delay silences the start of a note, note cut kills its tail, echo
keeps sounding past the note's end, retrigger adds dips to the RMS envelope,
a zero-probability note renders silence, and a loop range limits how far the
playhead ever travels. Probability also round-trips through save and load.

One of those tests caught a real bug during development: the NaN guard on
probability was written as `!(x < 1.0f)`, which this project's fast
floating-point setting lets the compiler fold into `x >= 1.0f` - false for
NaN, which would have silenced the note rather than playing it. It is a
bit-level check now.

---

## [3.4.1] - 2026-08-30

### Fixed

- **Upgrading from a pre-docking build left every panel scattered.** An
  `imgui.ini` written before docking existed contains positions for every
  window but no `DockId` assignments, so ImGui restored them as floating
  windows at their old coordinates while the dock space sat empty behind
  them. The app only applied its default layout when *no* ini existed, so
  nothing corrected it and there was no sign that `Ctrl+0` would.

  It now checks the dock tree itself a couple of frames in, and rebuilds the
  layout if nothing is docked. Checking the tree rather than sniffing the
  file also catches an ini truncated mid-write or hand-edited into
  uselessness.

- **The audio callback allocated on every call.** It built two `std::vector`s
  per block - roughly 86 heap allocations a second on the real-time thread -
  and `malloc` can block on a lock, which is exactly what a real-time thread
  must not do. The README's "zero allocations in the audio thread" was not
  true. The scratch buffers are now allocated once at startup, and an
  unexpectedly large block outputs silence rather than allocating or
  overrunning.

- **The sequencer pointer was a data race.** A plain pointer written by the
  main thread and read by the audio thread. Now atomic, and shutdown stops
  the device *before* retiring the pointer instead of after, so no callback
  can be in flight against state the main thread is dismantling.

### Added

- `--keep-ini` for capture mode, so the saved-layout path can be tested at
  all, and a UI smoke case that stages a real pre-docking `imgui.ini` and
  asserts the app recovers from it.

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
