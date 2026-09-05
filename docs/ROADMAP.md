# ChiptuneTracker — Roadmap

**Last updated:** 2026-09-04
**Current version:** v3.11.0

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
- [~] **A5. NaN / denormal guards.** Validation now repairs NaN and infinity
      on load, and the suite asserts finite output everywhere. Still wanted:
      a guard in the mixer itself, so a NaN arising at runtime is contained
      rather than poisoning the master bus.
- [x] **A6. Autosave and crash recovery.** Two-generation rolling autosave
      beside the user's file, never over it; the recovery file surviving to
      the next launch is itself the crash signal, and a clean exit clears it.
      Paired with a crash handler that writes a symbolised stack trace to
      `crash-log.txt`, so a crash arrives diagnosable rather than silent.
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
      *(Half done: the per-note sweep gained its UI in v3.5.0 — it was
      implemented in the audio path with no control. The per-channel unit
      and the AY envelope shapes remain.)*
- [ ] **B6. Legato / tie notes and true tone portamento** between adjacent
      notes, so slides read as one gesture rather than two events.
- [ ] **B7. Groove patterns** — per-row speed lists (e.g. `6 4 6 4`) for
      authentic tracker shuffle, alongside the existing swing control.

## Phase C — Composition and generation

- [x] **C1. Euclidean rhythm generator.** Shipped in v3.3.0 "Euclid":
      `Generators.h` with nine presets, headlessly tested, in the Tools
      panel with a live step preview.
- [ ] **C2. Chord progression generator** with genre-aware voicings, and a
      one-click "arpeggiate this progression" that writes a classic fast
      chiptune arp. *(Partly served: Quick Start writes fixed progressions
      — Minor Pop, Andalusian — and a triad arp. What remains is the
      generator itself: voicing options, and a lock per chord so
      regenerating keeps the ones you like.)*
- [x] **C3. Scale/key lock on the piano roll.** The tables and `snapToScale`
      already existed but sat several thousand lines below the piano roll in
      `UI.h`, so the "Snap to Scale" checkbox set a flag nothing could read.
      Extracted to `Scales.h`; placement honours it, and a To Scale button
      pulls an existing selection into key. Remaining: grey out-of-key rows,
      optionally snap on draw. (`snapToScale` exists; wire it to the roll.)
- [ ] **C4. Song structure templates** — intro/verse/chorus scaffolding
      that generates an arrangement, not just a pattern. *(The four-bar
      starter templates shipped and cover the blank-page half; the
      section scaffold is G4 and still open.)*
- [ ] **C5. Melody assistant** — suggest a continuation in key over the
      current progression. Deliberately a suggestion, never an autopilot.
- [x] **C6. Pattern variation tools.** Humanize and the variation tool in
      the Tools panel; transpose, To Scale, Mirror and Reverse on the piano
      roll toolbar. Mirror reflects around the selection's own middle so a
      phrase stays in its register. Thin/densify remains the one unbuilt
      verb, folded into G11's transform set.

## Phase D — Interop and export

- [ ] **D1. VGM export.** The chiptune scene's lingua franca; Furnace
      covers >80% of the spec and it is what people trade.
- [ ] **D2. NSF export** for actual NES playback/homebrew.
- [ ] **D3. MIDI import** to complement the existing export.
- [x] **D4. Per-channel stem export.** `exportStems` in `FileIO.h`, a
      Stems button beside the WAV export, and its own test group — which
      caught the missing-directory bug the first time it ran.
- [ ] **D5. Finish sample import** — synth trigger integration, sample
      browser window, and sample references in `.ctp`. `Sample.h` and the
      `Voice` fields exist; the playback path does not.

## Phase E — Interface and experience

**E0. Docking (done in 3.1).** Panels dock into shared regions and become
tabs; three workspaces build the dock tree. Researched against Reaper's
Docker + Screensets, Bitwig's docked panels and Furnace's ImGui docking,
all of which converged on the same answer.

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
- [x] **E8. First-run onboarding.** Shipped in a different shape than
      this item imagined, serving the same goal: the welcome asks what you
      are making, and the starter template puts four bars playing
      immediately. The three-step overlay idea is superseded.
- [ ] **E9. Accessibility** — a colourblind-safe palette option, UI scale
      control for HiDPI, and no information conveyed by colour alone.

## Phase F — Release engineering

- [x] **F1. `CHANGELOG.md`** with real version history.
- [x] **F2. Versioning in one place** — a `Version.h` the About dialog,
      window title and save-file header all read from.
- [x] **F3. About dialog** (currently a `TODO` in `main.cpp`).
- [x] **F4. Packaged release.** Every release since v3.3.0 is tagged
      and carries a zip that `package-release.ps1` verifies launches
      standalone from a clean directory with an empty PATH. The
      demo-project half of the original wording is F5's job.
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


---

## G. From the 2026-08-30 research pass

Four agents surveyed FL Studio, Furnace, Renoise, OpenMPT, FamiTracker, LSDj
and Bosca Ceoil, the chiptune community, and NES/Game Boy hardware behaviour.

**A note on provenance, because it matters here.** One agent fabricated
Reddit citations and vote counts when it could not reach the site, then
retracted them; a second agent later retrieved much of the same material
properly and graded every claim by how it was obtained. Items below are
marked **[verified]** only where the source was fetched and read. Anything
resting on a single unverified claim is marked **[unverified]** and should be
checked before it is built.

The strongest material was never the community research at all - it was the
audit of this codebase, which is reproducible by anyone with `grep`.

### Done in 3.5.0

- [x] Loop range on the timeline — the engine ignored `loopEnd` entirely
- [x] The four dead note effects — delay, cut, retrigger, echo
- [x] Pitch sweep UI — implemented in the audio path, no control anywhere
- [x] Project-wide undo, and an Edit menu that is not a no-op
- [x] Selectable grid snap with triplets — was hardcoded in fourteen places
- [x] Transpose — did not exist in any form
- [x] Note probability — one float, rolled per loop pass
- [x] Snap to Scale actually snapping

### Also done

- [x] **Genre focus.** The palette carries eight chord sets and seven drum
      categories, and the workspace has a panel for every feature we have
      shipped. Someone writing chiptune does not have options in the jazz
      voicings and the reggaeton percussion - they have things to scroll
      past. A genre decides what is put in front of you: which palette
      sections show, and which optional panels open. It removes nothing. The
      View menu still lists every panel, and the palette carries a switch
      that brings all of it back and says how much is being held. The default
      is Everything, which behaves exactly as before, and choosing a genre
      never touches the music - tempo, swing and key are a separate, explicit
      button, because silently retuning someone's project is not a layout
      change.

      Still worth doing on top of it: filter the Tools panel's generators the
      same way, ship a genre-appropriate starting template, and persist the
      choice across sessions.

- [x] **Guided start, kept optional.** A first-run prompt asks what kind
      of music this is, with Other as a real answer; a four-bar starter
      template per genre that already plays; a Quick Start section offering
      each style's foundational patterns - the dembow, boom bap, four on the
      floor, octave bass - as buttons that write ordinary, editable,
      undoable notes; and a one-line "next step" hint in the menu bar that
      reads the project and names one concrete thing to do, aimed squarely
      at the eight-bar-loop trap. Every piece is optional and dismissible,
      dismissals persist across sessions, and the manual path is untouched
      throughout - these are a faster pencil, not a different instrument.

- [x] **Guided first track.** A ten-step lesson - Help > Lesson, or the
      welcome's "teach me" checkbox - that walks from empty project to saved
      track. A lesson, not a wizard: each step names a goal ("draw at least
      four notes, any notes you like") and watches the actual project until
      the goal is true, so the user does everything themselves with the real
      tools and the finished track is theirs. Completion is detected, never
      claimed; steps latch so deleting evidence later cannot walk the lesson
      backwards; every step is skippable and the whole thing closable,
      because a tutorial you cannot leave is a hostage situation.

### Next, in rough value order

- [x] **G1. Make the tracker view editable.** `DrawTrackerView` is read-only
      *and wrong*: it breaks on the first note whose `startTime` matches the
      step, ignoring channel, so it prints the same note into all eight
      columns. Its own comment says "This is a simplification". The app is
      called a tracker. **Blocker to settle first:** `Pattern` is a flat
      `vector<Note>` with no channel field — a channel is bound only when a
      `Clip` is placed. Either add a channel column to notes, or show one
      channel per open pattern. [verified — our own source]

- [x] **G2. Non-linear channel mixing.** `output += sample` sums voices
      linearly; real 2A03 hardware sums through a non-linear table, which is
      why channels duck each other and why the triangle sits ~3.3 dB louder
      against the pulses than ours does. Two pulses at full volume make
      1.73x one pulse, not 2.0x. This is the largest audible-authenticity
      gain available in the codebase and needs no architectural change.
      Pair it with the output filter chain (90 Hz HP, 440 Hz HP, 14 kHz LP)
      and a stepped 4-bit triangle. [verified — NESdev]

- [x] **G3. Cross-channel ghost notes.** Draw the other channels' notes into
      the current piano roll at low alpha in their channel colour. For
      eight-channel chiptune, where a bass is constantly written against a
      lead you cannot see, this matters more than it does in a 60-track DAW.
      [verified — Image-Line manual]

- [ ] **G4. Song sections and an arrangement scaffold.** The single densest
      finding in the community research: people write an eight-bar loop and
      cannot turn it into a song. They ask for a concrete structure to
      follow, not a principle. Named sections, Alt-drag duplication, and
      demo songs shipped so the *structure* can be copied. [verified —
      archive API, multiple threads]

- [ ] **G5. Groove tables.** Replace the single global `swing`/`swingGrid`
      pair with a list of named grooves of up to 16 per-row tick counts.
      Steal LSDj's control that changes the swing percentage while holding
      the total tick count constant, so swing does not change tempo.
      Note: our macros run on a free-running 60 Hz `rateHz`, independent of
      tempo and swing, so a macro arpeggio drifts against a swung pattern.
      [verified — Furnace, LSDj, OpenMPT docs]

- [x] **G6. Per-clip transpose.** One `int` on `Clip`. One bassline phrase
      then covers a whole progression, which directly relieves the 64-pattern
      cap. `Clip` already decouples pattern from channel; this completes it.
      [verified — LSDj chain screen]

- [ ] **G7. Pattern matrix with slim vs deep clone.** A grid of order
      positions by channel, plus the distinction between cloning a reference
      and cloning the contents, and a pattern manager that de-duplicates and
      reclaims unused slots. We have no reuse story at all today.
      [verified — Furnace, LSDj]

- [ ] **G8. Macros belong to instruments, not channels.** `ChannelConfig`
      owns the macros, so a channel can host exactly one macro'd sound
      forever — even though `Note` already carries a per-note oscillator.
      Every further macro feature is capped until this moves. This is a
      migration, not an afternoon. [verified — our own source]

- [ ] **G9. Macro slots as Sequence / ADSR / LFO.** Furnace's best
      architectural idea: one macro framework with three types, so an ADSR
      can sit on volume and an LFO on pitch sharing the same timing
      controls. We currently maintain `Envelope` and `InstrumentMacros` as
      separate parallel concepts. [verified — Furnace docs]

- [x] **G10. Surface the groove tools we already have.** Feel presets -
      Machine, Tight, Loose, Swung, Hard Swing, Lofi Drag - in the Master
      Bus groove section, each one click and each only setting the sliders
      beneath it. Fixing it exposed the real bug: `applySwing` and
      `applyHumanize` were called only in the pattern-preview path, so the
      Swing slider had never once affected a song played from the
      arrangement. Both now apply there, with a regression test measuring
      the onset of an off-beat note. Original wording follows for the
      record. Beginners do not
      lack drum patterns — they cannot diagnose stiffness. `swing`,
      `humanize`, `humanizeAmount` and `humanizeVelocity` all exist on
      `Project` and are buried in a collapsing header. Audible presets
      (Tight / Loose / Swung) address the actual complaint for almost
      nothing. [verified — community threads]

- [ ] **G11. Selection transforms with an operation mask.** Interpolate,
      gradient, scale, randomize, invert, flip, collapse/expand — over a
      toggle grid deciding which columns each operation touches.
      `NoteTransforms.h` already holds invert and reverse. [verified —
      Furnace, Renoise]

- [ ] **G12. Ship strictness as a visible tier, not a policy.** The
      community requires hardware *legality*, not hardware *origin*, and
      objects to silent impossibility rather than to impossibility. Battle
      of the Bits encodes this as a ladder from `nsf_classic` to `fakebit`.
      Mirror it with a "this would not play on real hardware" indicator
      rather than picking a side. [verified — BotB rules, chipmusic.org]

### Deliberately not doing

- **VCA faders.** A search of the FL community returned exactly one
  on-topic result. There is no demand and we have eight channels.
- **More drum patterns.** No popular thread asks for them; the eight we
  have are sufficient. The complaint is about feel, which is G10.

### Design constraints carried out of the research

**Ship it off by default.** The highest-engagement feature-request thread in
r/FL_Studio's history has a 364-point top comment telling the author to go
use Ableton. A tracker audience self-selects for constraint even harder.
Every item above should be opt-in, collapsed, or hidden until invoked.

**Two surfaces, one data model.** Bosca Ceoil and Furnace jointly show that
beginner-friendly and expert-fast are not ends of one slider — they are two
input surfaces over the same patterns. We already have both. One of them
does not work, which is G1.

**Sell the constraint.** People buy hardware to escape "too many knobs
syndrome". Eight fixed channels is a feature we have for free and currently
never mention.


---

## H. From the 2026-09-04 research and audit pass

Five parallel investigations (chip hardware, genre mastering, mastering
tooling, VGM/NSF formats, and an audit of this codebase against a wish
list), plus direct measurement of synthesised chip waveforms. What follows
is what changed as a result, what is worth doing next, and — as valuable —
what the research says not to build.

Every claim below is tagged: **[M]** measured, **[S]** from a primary
source, **[A]** from the code audit.

### Corrections the research forced

These were wrong in the shipping code, and the research is why we know:

- **[M] The limiter was a hard clipper.** `1 - exp(-1/(t·sr))` where the
  retained fraction needed `exp(-1/(t·sr))` — 0.022 instead of 0.978, so the
  envelope reached its target in one sample. Every test passed. Fixed in
  3.11.x with a lookahead and a crest-factor test that can tell a limiter
  from a clipper.
- **[M] A `tanh` after the limiter** cost 2.2 dB on every project and made
  both clipping tests vacuous.
- **[A] The EQ, compressor and limiter were one mono instance each**,
  processed left then right — crosstalk in the filtered bands and every
  dynamics time constant halved.
- **[A] The "LUFS meter" had no K-weighting**, averaged channels before
  squaring (3 dB low on anything correlated), had no gating, and summed a
  144,000-sample buffer per output sample.
- **[A] Autosave's dirty check was a fingerprint of note *counts*** — so
  changing a pitch, a velocity, a length or any mixer setting never
  triggered a save.
- **[A] The crash handler wrote a log and let go**, losing up to 90 seconds
  of work while being the one piece of code still able to write the file.
- **[M] The chiptune mastering profile pushed its high shelf up +2 dB.**
  Chip percussion is already all edge; the people who master this for a
  living roll *off* above 10 kHz. Also its stereo widener was set to 1.12 on
  a source whose measured correlation is +1.000 at any width setting — there
  is no side signal to widen.

### The measurement that changes chiptune mastering

**[M] A square wave at exactly 0 dBFS reconstructs at +2.1 dBTP. A noise
channel reconstructs at +5.4.** An infinite-slope edge cannot exist in a
band-limited signal, so a chip render sitting on the ceiling is already two
decibels over on the meter every streaming service uses — with percussion,
the shortest and least suspicious element, worst by a wide margin.

Consequences already shipped: a 4× oversampled true-peak meter, a −1.6 dBTP
chiptune ceiling against −1.0 for everything else, and a DC blocker (a 12.5%
pulse carries −0.75 of DC *by construction*; removing it raises usable peak
by 4.86 dB).

Consequence still open: **[M] the drive curve.** The first decibel of
saturation buys 1.7 LU and the next eleven buy four. A "loudness" control
that does not say this encourages exactly the wrong move.

### H1. Chip modes, as an authoring constraint — NES and Game Boy done in 3.13.x

`ChannelConfig::chipVoice`, per channel and opt-in. Pitch goes through the
period register and volume through the volume register, in the audio thread,
so the constraint arrives in the sound while there is still a decision to
make. Deliberately **not** an extension of `chipAuthentic`: that flag is
already on in saved projects and quantising their pitch would change how
finished work sounds.

Two things that only turned up once it was measured:

- **The NES mute gate sits above the keyboard.** Below period 8 the sweep
  unit silences a pulse channel outright, and period 8 rounds in from
  anything under 13.2 kHz — above MIDI 127. So no *written* note can reach
  it and only modulation can. Worth knowing before quoting 12429 Hz as a
  ceiling, which is the truncated answer rather than the rounded one.
- **"Highest usable note" has to be scanned upward.** Coming down from the
  top finds the highest note that happens to land near a period — MIDI 125
  is 1.7 cents out on a NES pulse by coincidence, with everything for two
  octaves below it a quarter-tone off. Scanned upward the honest ceiling is
  **MIDI 110**, which is lower than the usually-quoted G#8 because that
  number is where steps exceed a *semitone* and this one is a quarter-tone.

Still open on this item: **[M]** fixed channel roles are preset buttons
rather than an enforced layout, and the three chips below are modelled in
the tables but have no `ChipVoice` yet.

The research produced exact tables for five chips; the ones that matter
most:

- **NES — shipped.** 11-bit period, pulse silenced below t=8, floor at A1
  (pulse) / A0 (triangle) NTSC. Pitch resolution collapses above E5 — >50¢
  per step above G7, and **above G#8 no chromatic scale exists**. Triangle
  has no volume at all. Duty 3 (75%) is spectrally identical to duty 1 —
  only the phase differs.
- **Game Boy — shipped.** Pulse physically cannot go below **C2** (64.0 Hz);
  only the wave channel reaches under it. CH3 has 4 volume codes and no
  envelope. Envelopes are one-shot, one-direction, ≤1.64 s and **cannot
  loop** — **reported by Project Check, deliberately not enforced.** The
  registers are what a channel plays, so quantising them is playing it
  correctly; the envelope generator is what the channel *is*, and reshaping
  it would silently rewrite a patch somebody voiced by ear.
- **[S] SID**: one *shared* filter and one *global* 4-bit volume — a
  per-voice volume column has to map to sustain and say so. Ring mod and
  sync cost a voice each.
- **[S] AY**: square only, no duty register at all; one shared noise (31
  pitches) and one shared envelope for all three channels.
- **[S] YM2612**: volume must scale **only the carriers of the current
  algorithm** — scaling all four is what makes naive FM trackers change
  timbre when you change volume.

Size: the tonal half (widening `ChipFilterChain::Mode` past NES/Famicom) is
~40 lines and is still open. The authoring-constraint half was estimated at
8–12 days; the NES and Game Boy half of it came in at about 1100 lines
including tests, largely because `ChipModel.h` had already done the reading.

### H2. PAL

**[S] The noise period table differs between regions, and not monotonically**
— at index $2 PAL is 14 CPU cycles where NTSC is 16, so PAL is *brighter*
there. Region also changes the frame counter (240 Hz NTSC vs 200 Hz PAL, and
neither is 60), every pitch by 7.65%, and the YM2612's LFO rate. We ship the
NTSC table only.

Small, and it is the kind of table that is silently wrong forever if nobody
writes it down.

### H3. Legato and tone portamento — done in 3.11.x

`Note::tie`. On a tied note `slide` becomes a rate in semitones per second
and the voice glides from wherever it is. The old slide was a scoop into the
note's *own* pitch that never looked at any other note **[A]**, and stepped
in hertz — so the same setting covered a different musical distance in each
octave. The legacy path is unchanged so existing projects sound the same.

### H4. Groove patterns (per-row speed)

**[A] Absent.** `GroovePresets.h` is swing and humanize; `Groove.h` is a
quantiser for the voice path. There is no tick or row-speed concept at all,
and the engine is beat-time float rather than row/tick — which is the real
obstacle, not the feature.

~120 lines, touching the timing core. Swing then becomes a special case of
groove rather than a separate mechanism.

### H5. MIDI import

**[A] Absent, and the hard part is already paid for.** `vendor/midifile` is
vendored and linked into both the app and the tests, `smf::MidiFile::read()`
is right there, and the CMake comment already says "MIDI export/import".
~120 lines, no new dependency.

### H6. Chord progression generator

**[A] Partial.** Progressions exist only as hardcoded degree lists inside
genre kits and templates, and every triad is a fixed `{0,3,7}` minor —
so a "major" progression comes out wrong. The generator shape already exists
in `Generators.h` next to the Euclidean one.

### H7. FLAC export, and 24-bit WAV

**[A] WAV export is hardcoded to 16-bit**, and truncates rather than rounds
with no dither. FLAC is input-only. The cheap route is ffmpeg alongside the
existing MP3 path (~20 lines, reusing the encoder discovery already there);
parameterising the WAV writer is a separate, smaller job.

### Deliberately not doing, and why

**VGM export — not until a register layer exists. [S]** Furnace can export
VGM because it *is* a rack of register-level chip emulators wearing a
tracker UI: every write goes to the emulator and to a log at the same time,
and the audio is a by-product. This project is the opposite — nothing
anywhere emits an address/value pair. Building it means ~4–6 weeks for NES
only, and **the file would not sound like the project**: supersaw, granular,
convolution, the mixer and 24 channels have no representation and would
silently vanish. Revisit only after H1, at which point it becomes a
mechanical dump of state that already exists.

Also worth recording so nobody spends a day discovering it: **[S] the VGM
format has no SID.** No clock field, no opcode, in any version.

**NSF — no.** The payload is 6502 code with INIT and PLAY entry points;
there is no data-only NSF. FamiTracker ships ~5,800 lines of hand-written
6502 for the base chip alone, before expansion chips. The minimum honest
version is a register-replay player, which is strictly downstream of all the
VGM work.

**A VST build — blocked on a licensing decision, not on code. [A]** And the
architecture is against it: `UI.h` is 20,000 lines of ImGui and `main.cpp`
owns the window, the device and the project lifecycle.

### Already done, contrary to the older notes

The audit found several items still listed as missing:

- **Euclidean rhythms** — full Bjorklund with rotation and nine presets,
  wired to a Tools panel with a live preview.
- **Sample import** — a loader *and* three live playback paths (sampler
  zones, arrangement audio clips, granular). The note that "the loader
  exists, the playback path does not" is years out of date.
- **The NES LFSR** — 15-bit, both taps the right way round, all 16 NTSC
  periods against 1.789773 MHz, exposed in the UI.
- **Autosave** — a real 90-second timer, atomic temp-and-rotate write, and a
  startup restore prompt.

`README.md` has seven stale unchecked boxes for features that exist.

### Small and worth doing

- **[A] Three drum voices open-code the noise LFSR** with comments calling
  bit0^bit1 "short mode / metallic". It is the *white* tap. The sound is
  hand-tuned and fine; the comments are backwards and the three copies
  should call `generateNoise()`.
- **[A] `AudioEngine.h`/`.cpp` are dead code** — not in CMakeLists, not
  included anywhere. Its LFSR uses the periodic tap unconditionally, which
  would be a bug if it ever ran.
