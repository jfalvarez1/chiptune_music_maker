# ChiptuneTracker — Test Plan

**Last updated:** 2026-08-30 (v3.4.0)

This describes what is tested, how, and — just as importantly — **what is
not**, so nobody mistakes a green run for full coverage.

---

## Why this exists

The recurring failure in this codebase is *silent* breakage. Not crashes:
controls that look connected and are not. The filter envelope, EQ,
compressor and formant filter were all settable in the UI and none of them
reached the synth. The stereo widener and sidechain never ran at all. Seven
instruments loaded back as `Pulse`. Saving a song discarded the entire mix.
Enabling the master EQ at its default multiplied the output by zero.

None of that produces an error. You only notice by carefully listening for
something that was never there. So the tests are built to assert that
**turning a control on changes the audio**, not merely that the code runs.

---

## The rule

**Every feature ships with its coverage in the same change.** Assertions in
`tests/ChiptuneTests.cpp`, and a case in `tools/ui-smoke-test.ps1` if it has
any UI. No exceptions, because the failure mode here is silence rather than
a crash — see above.

The assertion must be that the feature *changes the audio*, not that the
code ran. The test that found the dead stereo widener renders once with the
effect off and once with it on and fails if the two buffers match. "It
returned true" would have passed on a feature that did nothing.

This has already paid for itself twice: the stem exporter's test failed the
moment its temp directory was removed, which is how we learned it never
created its output folder, and the noise tests caught that voices were
sharing one LFSR clock.

## Layer 1 — Headless suite (`ChiptuneTests`)

No window, no audio device. Renders audio into buffers and inspects it.

```bash
cmake --build build --config Release --target ChiptuneTests
build/bin/Release/ChiptuneTests.exe          # add --verbose for every check
```

**3685 checks across 82 groups.** Exit code 0 = pass, 1 = failure, and every
failure names the field or parameter involved.

Since 3.8 this layer also runs the **real UI headlessly**: an ImGui context
with a built font atlas, a fake display and no backend at all, driving the
actual panel code through real frames. See "Headless GUI" below.

| Group | What it asserts |
|---|---|
| Oscillator name tables | Every one of the 65 oscillators survives a name round trip. Catches the "loads back as Pulse" class. |
| All oscillators render | Every oscillator at MIDI 0/60/127 and velocity 0.0/1.0 produces finite, in-range audio, and is not silent at full velocity. |
| Note effect extremes | Vibrato, arpeggio, slide, duty, sweep, tremolo at their limits and all at once. Caught the phase runaway (peak 2.4 × 10⁷). |
| Channel effect extremes | All 20 effects at maximum settings, including every feedback path, plus all of them on at once. |
| Polyphony overflow | 4× the available voices, then releasing notes that were stolen or never played. |
| Sequencer edge cases | Empty projects, empty patterns, clips with negative/out-of-range indices and lengths, BPM 1–999, degenerate loops, notes with zero/negative/absurd timing, transport hammering, a null project. |
| Save/load round trip | Save → load → save is byte-identical, and every field is compared individually: channels, effects, arrangement, master bus, groove, all 19 note effects. |
| Version 1 files | Old projects still open; re-saving upgrades them. |
| Malformed files | Empty, truncated, wrong header, absurd numbers, negative counts, binary junk. Must not crash, and must leave a renderable project. |
| MIDI export | Valid header, two exports identical, program changes present in both. Caught the never-reset `static`. |
| WAV export | Valid RIFF/WAVE, and zero/negative durations rejected rather than crashing. |
| Channel config reaches the synth | **The key test.** Each of 16 effects is enabled and the render is compared against a dry one; no audible difference is a failure. This is what found the dead widener and sidechain. |
| Master bus | Limiter holds the ceiling under a deliberate overload. |
| Silence in, silence out | An idle project with every master effect on is silent — a noise floor means something self-oscillates. |
| Instrument macros | Sequence semantics (loop, release, empty, out-of-range), fixed vs relative arpeggio steps, every preset audibly changing the note, the pluck actually decaying, persistence, and a malformed macro still rendering after repair. |
| Undo/redo | 50-level cap, redo cleared by a new edit, deep round trips, undoing past the start. |
| Mute/solo/routing | Muting silences, solo isolates, master volume works, hard pan lands on one side. |
| Live playing | Triggering with the transport stopped, out-of-range channels and pitches, previewing all 65 oscillators, a 60-note chord releasing to silence. |
| Wavetables | Default bank contents, morph interpolation, empty banks, out-of-range lookups, the table cap. |
| Master bus units | Enabling any master effect at its defaults must not change the level by more than ~12 dB, and must never silence. **This is the regression test for the dB-vs-linear bug that muted the whole song.** |
| Validation | Every field fed NaN, infinity, or a wildly wrong value, then asserted usable — and the repaired project renders. |
| Long-run stability | 60 seconds of a busy, effect-heavy, looping project: no NaN, limiter holds, level has not drifted, playhead still inside the loop. |
| Spectrum analyzer | A known tone lands in the right bin; a single tone lights few bins rather than the whole spectrum (the saturation regression); level tracks amplitude; silence reads as silence; NaN input and a zero sample rate are survived. |
| Noise generator | All sixteen NES periods render; they step from bright to dark; short mode is measurably periodic and normal mode is not; four voices are louder than one (they no longer share a clock); the period persists and out-of-range values are repaired. |
| Euclidean generator | Every E(k,n) up to n=32 produces exactly k onsets; the tresillo's spacing is correct; rotation preserves the count; degenerate input is safe; generated notes are valid, ordered and audible; all nine presets match their stated pattern. |
| Stem export | One file per non-silent channel, silent ones skipped, mute/solo state restored afterwards, unsafe characters stripped from filenames, real RIFF files on disk, and a bad duration refused. |
| Grid snap | Every division produces the right step; 1/16 still floors exactly as the old hardcoded constant did, so no existing gesture changed; triplets land on thirds; snap-off passes the value through; a duration never snaps to zero; NaN and infinity are caught rather than propagated; the bracket keys wrap. |
| Loop range | A user range wins over the content extent; a zero-length range falls back rather than trapping the playhead; a range dragged right-to-left is normalised; a beat many windows past the end still folds inside; play jumps into the loop rather than starting outside it. |
| Note expansion | A plain note is one hit; delay shifts it; cut shortens it; retrigger produces evenly spaced hits and none rings past the note's end; echoes decay and stop once inaudible; a pathological note cannot exceed the fixed buffer; a zero interval or a null buffer is refused. |
| Loop and effects in the audio | Delay silences the start, cut kills the tail, echo sounds past the note's end, retrigger adds dips to the RMS envelope, pitch sweep changes the signal, and a loop range limits how far the playhead travels. |
| Scales and transforms | Every pitch snaps into every scale; transpose preserves the intervals inside a chord; safe mode skips unplayable results instead of clamping them; bad selection indices are ignored rather than crashed on; probability is stable for a given note and pass, varies across passes, is independent between notes on the same beat, survives save/load, and renders silence at zero. |
| Ghost notes | An unplaced pattern yields nothing rather than inventing neighbours; a clip on the same channel is not a neighbour; a clip elsewhere in the song is not one either; offsets translate into the edited pattern timebase, including negative ones; notes outside the window are dropped; the list is capped; only the first placement of a repeated pattern contributes. |
| Chip mixing | The curves reproduce the published hardware ratios - 1.73x for two pulses, 1.65x for triangle against pulse, 88.7% for triangle plus noise - so a refactor that moves them fails; only 2A03 voices are grouped and a supersaw renders bit-identically either way; garbage levels yield finite gains. |
| Console filters | 60 Hz is attenuated against 1 kHz; 16 kHz is rolled off; Famicom keeps far more low and top end than NES; DC settles away; a NaN sample does not poison the filter state permanently. |
| Tracker grid | Two channels show two different notes - the exact bug the old view had; a channel with no clip shows nothing; a clip owns its start beat and not its end; overlapping clips resolve to the later one; a row owns a half-open span so no note appears twice; typing replaces rather than stacks; typing into empty space creates a bar-aligned pattern and clip; a note past the pattern end extends it; the pattern budget is respected; and a note typed into the grid is audible from the sequencer. |
| Genre focus | Everything is the default and hides nothing; every other genre keeps some sections and sets others aside, and none is indistinguishable from Everything; two genres show genuinely different palettes; the hidden count the UI reports matches the profile; every profile has a name, a description and a usable tempo, swing and scale; an out-of-range genre falls back rather than reading past the table. |
| User settings | Every genre token round trips and no two collide; an unknown or null token falls back to Everything; a missing file leaves the defaults so a first run asks; choosing Everything is remembered as an answer rather than as never having been asked; a file with comments, junk lines and a truncated tail still yields the settings it does understand; an unwritable target fails quietly. |
| Genre templates | A rhythm string is read into the right hits with accents, rests, a null string and a string longer than the note budget all handled; every genre yields four populated patterns, seven valid clips, its own tempo and only playable pitches; the drum pattern is one bar placed once per bar rather than a four-bar pattern; two genres differ in tempo, swing and voice; building the same template twice gives the same music; an out-of-range genre still yields something usable; a template survives save and load and renders audio the moment it loads. |
| Genre kits | Every recipe has a name and description and writes only playable notes; the dembow snare has its four hits in the 3-3-2 grouping and none on a straight beat two - an assertion that caught the recipe itself being wrong; genre filtering matches whole words; every genre is offered drums and a bass at minimum; existing notes survive a kit applied on top; the key is honoured; zero bars, negative starts, absurd keys and the note budget are all refused safely. |
| Next step | The ladder suggests in order - template, drums, bass, melody, variation, arrangement, width, export; a bass on A3 counts as a bass, which was shipped wrong once and caught in a screenshot; no starter template is told to add something it already has. |
| Clip transpose | +12 semitones doubles the measured frequency, so the transpose provably reaches the audio; a pitch pushed past MIDI's top is silent rather than wrapped; the value survives save/load as an optional sixth token and a pre-3.7 five-token file reads as untransposed with every older field intact; a corrupt value clamps to four octaves; a ghost from a transposed clip shows the sounding pitch, not the stored one. |
| Groove presets | Every preset is named, described and in range; no two set the same feel; each matches itself after being applied and hand-moved sliders read as custom, so the highlight is honest; an off-beat note played from the ARRANGEMENT starts measurably later under swing - the regression test for the slider that was preview-only - and an on-beat note does not move. |
| Version coherence | VERSION_STRING is composed from the version ints and the window title and About dialog carry it - the title shipped saying 3.4.1 on a 3.6.0 build because the file that promised one place held the version twice. |
| Guided first track | Every step is titled, described and correctly kinded - an action step must carry evidence to check and an info step must not pretend to; the whole road is walked in order and each condition fires exactly when its thing is built, never before; a completed step stays completed when its evidence is later deleted; a null project and out-of-range indices are safe. |
| Effect rack identity | The rack in classic order is sample-identical to a frozen verbatim copy of the old fixed chain, at exact float equality, across all fourteen effects together and each alone; reordering reverb before distortion provably changes the audio; a zero-mix slot passes through; a copied chain drives its own effects rather than the original's. |
| Effect rack stability | Under a reorder storm from a second thread: no non-finite sample and a structurally intact rack with feedback effects present, and a tight amplitude bound with them removed - the two are separated because peak amplitude under feedback depends on thread interleaving and a flaky test is worse than none. |
| Effect rack persistence | A reordered rack round-trips; an untouched project writes no rack line at all; a v2 file migrates with every enabled effect intact and a live chain in classic order; duplicates and unknown effects are dropped; a rack that sanitises to nothing falls back to classic rather than going silent; an absurd slot count cannot walk off the array. |
| Aux routing graph | Self-feeding buses, two-bus loops and four-bus rings are all detected; the master is always a safe destination; a cycle is reported rather than resolved into something wrong; a bus is ordered before the bus it feeds; a file carrying a loop is repaired to the master; a clean graph is left alone; a null output array is refused. |
| Sends and buses | A send adds signal and a muted bus contributes nothing; bus volume scales the return; a pre-fader send from a SILENT channel still feeds the bus while a post-fader one sends silence; an effect on a bus strip changes the bus output; a bus feeding a bus still reaches the master; a file carrying a loop renders finite samples throughout; a send to a bus that does not exist is ignored safely. |
| Routing persistence | Sends, sidechain bus, bus name/level/pan/output and a bus rack order all round-trip; a project with no routing writes no routing lines; a v3 file loads with no sends and every bus on the master; hostile values are clamped and impossible destinations become no send. |
| Channel cap | The project and sequencer caps agree; every channel is named and no two share a name; chip-authentic mode is enforced in the AUDIO ENGINE - a note on channel 20 renders silent with it on and audible with it off; a clip stranded past the cap is moved rather than deleted; the flag round-trips; a project nobody extended still writes exactly eight CHANNEL lines while a touched channel past the eighth is written and read back. |
| MIDI channel bounds | A 32-channel project exports byte-identically twice - the program-change flags were a file-scope array of 16 indexed by the project channel, so anything past the sixteenth wrote out of bounds and the export was non-deterministic. |
| Autosave | A clean directory offers nothing to recover; a save is loadable and complete; the previous generation is kept so a crash mid-write cannot destroy the only copy; a clean exit clears the evidence; the timer fires only when something changed; a disabled autosave never writes; an unwritable target fails quietly rather than crashing. |
| Theme legibility | Links ImGui and calls `ApplyTheme` for real. Ten themes × seventeen surfaces = 170 contrast assertions, plus Header/Button remaining distinguishable, interactive alpha surviving the correction, no unset colour slots, and order-independence when switching themes. **This is what caught button labels at 1.22:1 in nine themes.** |

## Headless GUI (part of layer 1)

An ImGui context with no window and no renderer. The panels are called
exactly as `main.cpp` calls them, a frame is run, and the result is
inspected. It costs milliseconds, so it runs on every build rather than only
when someone remembers.

What it checks that a screenshot cannot:

| Check | Why it matters |
|---|---|
| ID stack balance | A `PushID` without its `PopID` corrupts every widget ID after it in that window. Unrelated controls silently start sharing state — two sliders that move together, a checkbox that toggles its neighbour. ImGui asserts in debug and misbehaves quietly in release, which is what ships. |
| `Begin`/`End` balance | An unclosed window breaks every panel drawn afterwards, not just the guilty one. |
| Vertex sanity | A divide-by-zero in layout maths produces NaN vertices, which a GPU discards silently — so a rendering test still sees a plausible frame while a control has vanished. |
| Clicks | Real `ImGuiIO` mouse events through real widgets. A click is three frames — move and press, hold, release — because ImGui reports it on release and only if the press landed on the same widget. Fewer frames silently does nothing, which is how a naive attempt at this passes while testing nothing. |

Coverage: all thirteen main panels, all seven oscillator/engine editors,
five display sizes from 320×240 to 3840×2160, all eight genre focuses, all
ten themes, and click/drag on buttons, checkboxes and drag-floats.

---

## Layer 2 — UI smoke test (`tools/ui-smoke-test.ps1`)

The headless suite cannot cover the renderer, because there is no window.
This drives the app's `--capture` mode through the combinations a person
would otherwise click through by hand.

```powershell
./tools/ui-smoke-test.ps1            # 41 cases
./tools/ui-smoke-test.ps1 -Quick     # 12 cases, for a fast loop
```

For each case it asserts the process exited cleanly, a frame was captured,
and the frame is not blank or near-flat — which is what a failed render, a
lost GL context or a panel that did not draw looks like.

Covered: all 5 views · all 10 themes · all 3 workspaces · the macro editor
panel · empty projects in three views · window sizes from 800×600 to
2560×800 to 900×1200 · a 2-frame startup race · a pre-docking imgui.ini
being repaired · the crash-recovery prompt.

It is deliberately **not** a pixel-comparison test. Golden images would fail
on every legitimate UI change, and a test everyone learns to ignore is worse
than no test.

## Layer 3 — Manual checklist

These need hands on a mouse. Run before tagging a release.

### Editing
- [ ] Draw, drag, resize and delete notes in the piano roll
- [ ] Box-select, multi-drag and multi-resize
- [ ] Copy/paste with the ghost-note preview; paste at the cursor
- [ ] Undo and redo across each of the above (`Ctrl+Z` / `Ctrl+Y`)
- [ ] Place a chord and a drum pattern from the palette
- [ ] Enter notes in the tracker view; check it agrees with the piano roll
- [ ] Drag clips in the arrangement view

### Sound
- [ ] Audition every palette category; nothing silent, nothing deafening
- [ ] Draw a volume macro and hear the shape change
- [ ] Draw an arpeggio macro; confirm it sounds like a chord on one channel
- [ ] Right-click an arpeggio step and confirm fixed sounds different
- [ ] Sweep every knob in the channel editor while a note holds
- [ ] Toggle 4-bit volume and hear the quantisation
- [ ] Confirm the mixer meters track what you hear

### Layout
- [ ] Drag a panel out and dock it somewhere else
- [ ] Dock two panels together and switch between their tabs
- [ ] Switch all three workspaces; `Ctrl+0` resets
- [ ] Resize the window small and large; nothing clips or escapes
- [ ] Restart and confirm the layout persisted

### Recovery
- [ ] Kill the app from Task Manager mid-edit, relaunch, and confirm the
      recovery prompt appears with a sensible "minutes ago"
- [ ] Restore it, and confirm the notes, mix and arrangement all came back
- [ ] Discard it, relaunch, and confirm the prompt does not reappear
- [ ] Exit cleanly and relaunch; there must be no prompt

### Files and export
- [ ] Save, close, reopen; confirm the mix and arrangement survive
- [ ] Open a project saved by an older version
- [ ] Export WAV and listen to it end to end
- [ ] Export MIDI and open it in another program
- [ ] Export MP3 (needs LAME or FFmpeg on PATH)

### Hardware
- [ ] Connect a MIDI keyboard; play, record in Replace and Overdub
- [ ] Hot-swap the device without restarting

---

## Known gaps

Honest list of what nothing currently covers.

1. **The custom-drawn editors' mouse paths.** The headless GUI layer clicks
   ImGui widgets, which covers buttons, checkboxes, combos and drags. It does
   *not* cover the piano roll, the arrangement and the pad controller, which
   draw themselves into an `InvisibleButton` and do their own hit-testing
   against canvas coordinates. Dragging a note, box-selecting, and dragging a
   clip are still manual-checklist only. This is now the biggest remaining
   gap, and it is much narrower than it was.
2. **The Tools panel generators.** The nine production tools (drum pattern
   generator, arpeggiator, bass generator, scale lock, velocity curve, fill,
   variation, quick layer, humanize) live in `UI.h` behind ImGui calls. Their
   logic should be extracted into a testable module, then tested.
3. **MP3 export.** Shells out to LAME or FFmpeg, so it depends on the
   machine. Manual only.
4. **MIDI input.** Needs real hardware. Manual only.
5. **Microphone capture.** The ring buffer, the tracker and the note
   conversion are all tested against synthesised audio; opening an actual
   capture device is not. Manual only.
6. **Audio correctness beyond sanity, in most places.** Most tests assert
   audio is finite, bounded, non-silent and changes when it should. The
   engines added in 3.7 go further and assert *spectra* — that band-limiting
   removes inharmonic energy, that a pitch shift lands on the right
   fundamental, that a bell's centroid falls over its life, that formant
   shifting leaves the pitch alone — but the older effects have no such
   coverage. A reverb with the wrong decay curve would still pass.
7. **Performance.** No assertion on CPU cost per block. A change that made
   the audio thread 10× slower would pass everything here.
8. **Linear-phase EQ.** Left out because it costs 50–100 ms of latency and
   the mixer had no compensation. That reason is gone — the graph is levelled
   now — so this is a gap in the effect list rather than in the engine.

---

## Running everything

```powershell
cmake --build build --config Release
build/bin/Release/ChiptuneTests.exe        # layer 1
ctest --test-dir build -C Release          # layer 1, from a different cwd
./tools/ui-smoke-test.ps1                  # layer 2
./tools/generate-gallery.ps1               # refresh README screenshots
```

Run the headless suite *both* ways. `ctest` launches it from `build/` rather
than the repo root, and that difference once exposed an out-of-bounds write
that the same binary did not fault on when run directly - whether an
out-of-bounds write faults depends on the process memory layout.

```
```

All three must pass before a release tag.
