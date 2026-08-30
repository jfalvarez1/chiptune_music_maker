#pragma once

/*
 * ChiptuneTracker - Instrument macros
 *
 * The single largest gap between this tool and a real chiptune tracker.
 *
 * An ADSR envelope is a synthesiser idea. Chip instruments are built from
 * *step sequences* that advance once per hardware frame - a volume macro
 * like `15 12 10 9 8 8 8` for a pluck, an arpeggio macro like `0 4 7` that
 * fakes a chord on one channel, a duty macro that sweeps the pulse width
 * for that PWM shimmer. This is how FamiTracker, Furnace and LSDJ
 * instruments are designed, and it is what makes the results sound like
 * chiptune rather than like a soft synth pretending.
 *
 * Four macros per instrument, each an independent sequence with its own
 * position, loop point and release point:
 *
 *   volume    0..15   4-bit level, the native resolution of the hardware
 *   arpeggio  semitone offsets, relative to the note or fixed (see below)
 *   duty      0..3    index into the four NES pulse widths
 *   pitch     1/16ths of a semitone, for fine drift, dives and wobble
 *
 * Arpeggio steps carry a per-step `fixed` flag, following Furnace: a
 * relative step transposes the played note, a fixed step plays that value
 * as an absolute offset from the base regardless of the note. Relative is
 * what you want for chords, fixed for drum-like noise instruments.
 *
 * Macros advance at `rateHz`, defaulting to 60 - the NES frame counter
 * rate, and the reason classic arpeggios sound the way they do.
 */

#include <vector>
#include <cstdint>
#include <algorithm>

namespace ChiptuneTracker {

// ============================================================================
// A single macro sequence
// ============================================================================
struct Macro {
    static constexpr int MAX_STEPS = 64;

    bool enabled = false;
    std::vector<int> steps;

    // Index to jump back to when the sequence runs out. -1 means "hold the
    // last value forever", which is what you want for a sustain level.
    int loopStart = -1;

    // Index to jump to when the note is released. -1 means "carry on".
    int releaseStep = -1;

    bool isActive() const { return enabled && !steps.empty(); }

    // Value at a position, honouring the loop point. Positions past the end
    // of a non-looping macro hold the final step.
    int valueAt(int position) const {
        if (steps.empty()) return 0;

        const int count = static_cast<int>(steps.size());
        if (position < count) return steps[static_cast<size_t>(std::max(0, position))];

        if (loopStart >= 0 && loopStart < count) {
            const int loopLength = count - loopStart;
            const int offset = (position - loopStart) % loopLength;
            return steps[static_cast<size_t>(loopStart + offset)];
        }

        return steps.back();
    }

    // Whether a position has run past a non-looping sequence. Used to stop
    // advancing the counter rather than letting it grow without bound.
    bool isFinished(int position) const {
        return loopStart < 0 && position >= static_cast<int>(steps.size());
    }

    void clear() {
        steps.clear();
        loopStart = -1;
        releaseStep = -1;
        enabled = false;
    }
};

// ============================================================================
// Arpeggio macros additionally need a per-step relative/fixed flag
// ============================================================================
struct ArpeggioMacro : Macro {
    // Parallel to `steps`. Missing entries are treated as relative, so a
    // macro built without touching this behaves the classic way.
    std::vector<uint8_t> fixed;

    bool isFixedAt(int position) const {
        if (steps.empty() || fixed.empty()) return false;

        const int count = static_cast<int>(steps.size());
        int index = position;
        if (index >= count) {
            if (loopStart >= 0 && loopStart < count) {
                const int loopLength = count - loopStart;
                index = loopStart + (position - loopStart) % loopLength;
            } else {
                index = count - 1;
            }
        }
        index = std::max(0, index);
        if (index >= static_cast<int>(fixed.size())) return false;
        return fixed[static_cast<size_t>(index)] != 0;
    }
};

// ============================================================================
// The macro set that makes up one instrument
// ============================================================================
struct InstrumentMacros {
    Macro volume;           // 0..15
    ArpeggioMacro arpeggio; // semitone offsets
    Macro duty;             // 0..3, index into the NES duty cycles
    Macro pitch;            // 1/16 semitone units

    // Steps per second. 60 matches the NES frame counter and is what the
    // classic fast arpeggio sound is tuned to.
    float rateHz = 60.0f;

    bool anyActive() const {
        return volume.isActive() || arpeggio.isActive() ||
               duty.isActive() || pitch.isActive();
    }

    void clear() {
        volume.clear();
        arpeggio.clear();
        arpeggio.fixed.clear();
        duty.clear();
        pitch.clear();
        rateHz = 60.0f;
    }
};

// ============================================================================
// Per-voice playback state for one macro
// ============================================================================
struct MacroState {
    int position = 0;
    float timer = 0.0f;
    bool released = false;

    void reset() {
        position = 0;
        timer = 0.0f;
        released = false;
    }

    // Advance by `dt` seconds at `rateHz` steps per second.
    void advance(const Macro& macro, float dt, float rateHz) {
        if (!macro.isActive()) return;
        if (rateHz <= 0.0f) return;

        const float stepDuration = 1.0f / rateHz;
        timer += dt;

        // A loop guard rather than `while`: a tiny rateHz combined with a
        // large dt should not spin.
        int guard = 0;
        while (timer >= stepDuration && guard < Macro::MAX_STEPS) {
            timer -= stepDuration;
            if (!macro.isFinished(position + 1)) {
                ++position;
            }
            ++guard;
        }
    }

    // Jump to the release point, if the macro defines one.
    void release(const Macro& macro) {
        if (released) return;
        released = true;
        if (macro.releaseStep >= 0 &&
            macro.releaseStep < static_cast<int>(macro.steps.size())) {
            position = macro.releaseStep;
            timer = 0.0f;
        }
    }
};

// All four states for one voice.
struct VoiceMacroState {
    MacroState volume;
    MacroState arpeggio;
    MacroState duty;
    MacroState pitch;

    void reset() {
        volume.reset();
        arpeggio.reset();
        duty.reset();
        pitch.reset();
    }

    void releaseAll(const InstrumentMacros& macros) {
        volume.release(macros.volume);
        arpeggio.release(macros.arpeggio);
        duty.release(macros.duty);
        pitch.release(macros.pitch);
    }

    void advanceAll(const InstrumentMacros& macros, float dt) {
        volume.advance(macros.volume, dt, macros.rateHz);
        arpeggio.advance(macros.arpeggio, dt, macros.rateHz);
        duty.advance(macros.duty, dt, macros.rateHz);
        pitch.advance(macros.pitch, dt, macros.rateHz);
    }
};

// ============================================================================
// Ready-made instruments
//
// These are the shapes people reach for constantly, and having them one
// click away is the difference between the macro editor being a feature and
// being a chore.
// ============================================================================
struct MacroPreset {
    const char* name;
    const char* description;
    InstrumentMacros macros;
};

inline InstrumentMacros makePluck() {
    InstrumentMacros m;
    m.volume.enabled = true;
    m.volume.steps = {15, 14, 12, 10, 9, 8, 7, 7, 6, 6, 5, 5, 4, 3, 2, 1, 0};
    return m;
}

inline InstrumentMacros makeSustainedLead() {
    InstrumentMacros m;
    m.volume.enabled = true;
    m.volume.steps = {10, 13, 15, 14, 13, 12, 12};
    m.volume.loopStart = 6;          // hold at 12 until released
    m.volume.releaseStep = 6;
    m.duty.enabled = true;
    m.duty.steps = {2, 2, 1, 1};     // subtle PWM shimmer
    m.duty.loopStart = 0;
    return m;
}

inline InstrumentMacros makeMajorChordArp() {
    InstrumentMacros m;
    m.volume.enabled = true;
    m.volume.steps = {15, 13, 12, 11, 11};
    m.volume.loopStart = 4;
    m.arpeggio.enabled = true;
    m.arpeggio.steps = {0, 4, 7};    // root, major third, fifth
    m.arpeggio.loopStart = 0;
    return m;
}

inline InstrumentMacros makeMinorChordArp() {
    InstrumentMacros m = makeMajorChordArp();
    m.arpeggio.steps = {0, 3, 7};    // minor third
    return m;
}

inline InstrumentMacros makeLaserZap() {
    InstrumentMacros m;
    m.volume.enabled = true;
    m.volume.steps = {15, 13, 10, 7, 4, 2, 0};
    m.pitch.enabled = true;
    // A fast downward dive - the classic "pew"
    m.pitch.steps = {0, -32, -80, -144, -224, -320, -432};
    return m;
}

inline InstrumentMacros makeVibratoLead() {
    InstrumentMacros m;
    m.volume.enabled = true;
    m.volume.steps = {12, 15, 14, 13, 13};
    m.volume.loopStart = 4;
    m.pitch.enabled = true;
    // Delayed vibrato: flat for a beat, then a gentle wobble that loops
    m.pitch.steps = {0, 0, 0, 0, 0, 0, 0, 0, 4, 8, 4, 0, -4, -8, -4, 0};
    m.pitch.loopStart = 8;
    return m;
}

inline InstrumentMacros makeBassStab() {
    InstrumentMacros m;
    m.volume.enabled = true;
    m.volume.steps = {15, 15, 13, 11, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
    m.duty.enabled = true;
    m.duty.steps = {0, 1, 2};        // opens up as it decays
    return m;
}

inline InstrumentMacros makeOctaveEcho() {
    InstrumentMacros m;
    m.volume.enabled = true;
    m.volume.steps = {15, 12, 9, 12, 7, 9, 5, 6, 3, 4, 2, 2, 1, 0};
    m.arpeggio.enabled = true;
    m.arpeggio.steps = {0, 12};      // alternates with the octave above
    m.arpeggio.loopStart = 0;
    return m;
}

inline const std::vector<MacroPreset>& macroPresets() {
    static const std::vector<MacroPreset> presets = {
        {"Pluck",        "Sharp attack, quick decay - the workhorse lead", makePluck()},
        {"Sustained",    "Swells in and holds until released",            makeSustainedLead()},
        {"Major Arp",    "Fakes a major chord on one channel",            makeMajorChordArp()},
        {"Minor Arp",    "Fakes a minor chord on one channel",            makeMinorChordArp()},
        {"Laser Zap",    "Fast downward pitch dive - the classic pew",    makeLaserZap()},
        {"Vibrato Lead", "Flat, then a delayed wobble that loops",        makeVibratoLead()},
        {"Bass Stab",    "Punchy bass that opens up as it decays",        makeBassStab()},
        {"Octave Echo",  "Alternates with the octave for a fake delay",   makeOctaveEcho()},
    };
    return presets;
}

} // namespace ChiptuneTracker
