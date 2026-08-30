# ChiptuneTracker — Test Plan

**Last updated:** 2026-08-30 (v3.0.0)

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

## Layer 1 — Headless suite (`ChiptuneTests`)

No window, no audio device. Renders audio into buffers and inspects it.

```bash
cmake --build build --config Release --target ChiptuneTests
build/bin/Release/ChiptuneTests.exe          # add --verbose for every check
```

**1333 checks across 20 groups.** Exit code 0 = pass, 1 = failure, and every
failure names the field or parameter involved.

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

## Layer 2 — UI smoke test (`tools/ui-smoke-test.ps1`)

The headless suite cannot cover the renderer, because there is no window.
This drives the app's `--capture` mode through the combinations a person
would otherwise click through by hand.

```powershell
./tools/ui-smoke-test.ps1            # 26 cases
./tools/ui-smoke-test.ps1 -Quick     # 12 cases, for a fast loop
```

For each case it asserts the process exited cleanly, a frame was captured,
and the frame is not blank or near-flat — which is what a failed render, a
lost GL context or a panel that did not draw looks like.

Covered: all 5 views · all 10 themes · all 3 workspaces · the macro editor
panel · empty projects in three views · window sizes from 800×600 to
2560×800 to 900×1200 · a 2-frame startup race.

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

1. **Interactive UI logic.** Every mouse-driven path — dragging notes, box
   selection, clip dragging, knob sweeps — is exercised only by the manual
   checklist. The smoke test proves the screens *draw*, not that they
   *respond*. Closing this needs a synthetic input harness that feeds
   `ImGuiIO` events; that is the single biggest remaining gap.
2. **The Tools panel generators.** The nine production tools (drum pattern
   generator, arpeggiator, bass generator, scale lock, velocity curve, fill,
   variation, quick layer, humanize) live in `UI.h` behind ImGui calls. Their
   logic should be extracted into a testable module, then tested.
3. **MP3 export.** Shells out to LAME or FFmpeg, so it depends on the
   machine. Manual only.
4. **MIDI input.** Needs real hardware. Manual only.
5. **Sample import.** `Sample.h` loads files, but nothing plays them yet, so
   there is nothing to test. Tests land with the feature.
6. **Audio correctness beyond sanity.** Tests assert audio is finite,
   bounded, non-silent and changes when it should. They do not assert it
   sounds *right* — no spectral assertions, no reference renders. A filter
   with the wrong slope would pass.
7. **Performance.** No assertion on CPU cost per block. A change that made
   the audio thread 10× slower would pass everything here.

---

## Running everything

```powershell
cmake --build build --config Release
build/bin/Release/ChiptuneTests.exe        # layer 1
./tools/ui-smoke-test.ps1                  # layer 2
./tools/generate-gallery.ps1               # refresh README screenshots
```

All three must pass before a release tag.
