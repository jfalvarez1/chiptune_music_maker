/*
 * ChiptuneTracker - Headless test harness
 *
 * Exercises the synth, sequencer, effects, file I/O and exporters with no
 * window and no audio device, so every code path can be checked in CI and
 * from the command line.
 *
 * The failure mode this project has actually suffered is *silent* breakage:
 * a filter envelope that never reached the synth, instruments that loaded
 * back as Pulse, a second MIDI export that emitted no program changes. The
 * tests below are aimed squarely at that class of bug - wiring gaps and
 * bad values that never announce themselves.
 *
 * Run:  build/bin/Release/ChiptuneTests.exe [--verbose]
 * Exit: 0 = all passed, 1 = at least one failure.
 */

#include "Types.h"
#include "Synthesizer.h"
#include "Sequencer.h"
#include "FileIO.h"
#include "MIDIExport.h"
#include "Autosave.h"
#include "Snap.h"
#include "LoopRange.h"
#include "NoteEvents.h"
#include "Scales.h"
#include "NoteTransforms.h"
#include "GhostNotes.h"
#include "ChipMix.h"
#include "TrackerGrid.h"
#include "Genres.h"
#include "Settings.h"
#include "Templates.h"
#include "GenreKits.h"
#include "NextStep.h"
#include "GroovePresets.h"
#include "UndoHistory.h"

// Pulls in ApplyTheme. No window or GL context is needed: it only writes to
// ImGuiStyle, and a context can exist without a backend.
#include "imgui.h"
#include "UI.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <memory>
#include <cstdlib>

using namespace ChiptuneTracker;

// ============================================================================
// Tiny test framework
// ============================================================================
namespace {

int g_checks = 0;
int g_failures = 0;
bool g_verbose = false;
std::string g_currentTest;
std::vector<std::string> g_failureLog;

void beginTest(const char* name) {
    g_currentTest = name;
    // Always announce, and flush. If a test crashes the process outright,
    // the last line printed is what tells us which one to look at.
    std::printf("[ run ] %s\n", name);
    std::fflush(stdout);
}

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::string msg = "[FAIL] " + g_currentTest + ": " + what;
        g_failureLog.push_back(msg);
        std::printf("%s\n", msg.c_str());
        std::fflush(stdout);
    } else if (g_verbose) {
        std::printf("  ok: %s\n", what.c_str());
    }
}

// A sample is "sane" if it is a real number within a generous headroom bound.
// Anything outside this is either a NaN/inf leak or runaway feedback.
constexpr float SANE_PEAK = 16.0f;

bool isSane(float x) {
    return std::isfinite(x) && std::fabs(x) <= SANE_PEAK;
}

struct BufferStats {
    float peak = 0.0f;
    size_t peakIndex = 0;
    float rms = 0.0f;
    int nonFiniteCount = 0;
    int outOfRangeCount = 0;
    bool allZero = true;
};

BufferStats analyze(const std::vector<float>& buf) {
    BufferStats s;
    double sumSq = 0.0;
    for (size_t i = 0; i < buf.size(); ++i) {
        const float v = buf[i];
        if (!std::isfinite(v)) { ++s.nonFiniteCount; continue; }
        float a = std::fabs(v);
        if (a > SANE_PEAK) ++s.outOfRangeCount;
        if (a > s.peak) { s.peak = a; s.peakIndex = i; }
        if (a > 1e-7f) s.allZero = false;
        sumSq += double(v) * double(v);
    }
    if (!buf.empty()) s.rms = float(std::sqrt(sumSq / double(buf.size())));
    return s;
}

const char* oscName(OscillatorType t) {
    // Reuse the save-file table so this stays in sync with the enum
    static std::string cache;
    cache = oscillatorTypeToString(t);
    return cache.c_str();
}

constexpr int OSC_COUNT = static_cast<int>(OscillatorType::KavinskyBass) + 1;

// Every file a test writes goes here. The working directory is not guaranteed
// writable, and tests should not leave artifacts in the repo either way.
std::string testPath(const std::string& name) {
    const char* tmp = std::getenv("TEMP");
    if (!tmp) tmp = std::getenv("TMP");
    if (!tmp) tmp = std::getenv("TMPDIR");
    if (!tmp || !*tmp) return name; // fall back to the working directory

    std::string dir = tmp;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    return dir + "chiptune_test_" + name;
}

} // namespace

// ============================================================================
// 1. Every oscillator renders sane audio, at the extremes of its inputs
// ============================================================================
static void testAllOscillatorsRenderSanely() {
    beginTest("All oscillators render finite, bounded audio");

    const float sampleRate = 44100.0f;
    const int frames = int(sampleRate * 0.5f); // half a second per case

    struct Case { int pitch; float velocity; const char* label; };
    const Case cases[] = {
        {  0, 1.0f, "lowest MIDI pitch"   },
        { 60, 1.0f, "middle C"            },
        {127, 1.0f, "highest MIDI pitch"  },
        { 60, 0.0f, "zero velocity"       },
        { 60, 1.0f, "full velocity"       },
    };

    int silentOscillators = 0;

    for (int oscIdx = 0; oscIdx < OSC_COUNT; ++oscIdx) {
        OscillatorType osc = static_cast<OscillatorType>(oscIdx);

        for (const Case& c : cases) {
            Synthesizer synth;
            synth.setSampleRate(sampleRate);
            synth.noteOn(c.pitch, c.velocity, 0.0f, 0.0f, 0.0f, 0.4f, osc);

            std::vector<float> buf(frames);
            for (int i = 0; i < frames; ++i) {
                buf[i] = synth.process(float(i) / sampleRate);
            }

            BufferStats st = analyze(buf);
            std::string where = std::string(oscName(osc)) + " @ " + c.label;

            check(st.nonFiniteCount == 0,
                  where + " produced " + std::to_string(st.nonFiniteCount) + " non-finite samples");
            check(st.outOfRangeCount == 0,
                  where + " produced " + std::to_string(st.outOfRangeCount) +
                  " samples beyond +/-" + std::to_string(int(SANE_PEAK)) +
                  " (peak " + std::to_string(st.peak) + " at sample " +
                  std::to_string(st.peakIndex) + ")");

            // A full-velocity note in the middle of the range should make sound.
            // Silence there means the oscillator is not wired up at all.
            if (c.pitch == 60 && c.velocity >= 1.0f && st.allZero) {
                ++silentOscillators;
                check(false, where + " is completely silent - oscillator not implemented?");
            }
        }
    }

    if (g_verbose) {
        std::printf("  checked %d oscillators, %d silent\n", OSC_COUNT, silentOscillators);
    }
}

// ============================================================================
// 2. Note effects at their extremes
// ============================================================================
static void testNoteEffectExtremes() {
    beginTest("Note effects at parameter extremes");

    const float sampleRate = 44100.0f;
    const int frames = int(sampleRate * 0.5f);

    struct FxCase {
        const char* label;
        float vibrato; int arpeggio; float slide;
        DutyCycle duty; bool useDuty;
        SweepDirection sweepDir; float sweepSpd; float sweepAmt;
        float tremolo; float tremoloSpd;
    };

    const FxCase cases[] = {
        {"max vibrato",      12.0f,    0,   0.0f, DutyCycle::Duty50,   false, SweepDirection::None, 1.0f,  12.0f, 0.0f, 4.0f},
        {"max arpeggio",      0.0f, 0xFF,   0.0f, DutyCycle::Duty50,   false, SweepDirection::None, 1.0f,  12.0f, 0.0f, 4.0f},
        {"extreme slide up",  0.0f,    0,  96.0f, DutyCycle::Duty50,   false, SweepDirection::None, 1.0f,  12.0f, 0.0f, 4.0f},
        {"extreme slide dn",  0.0f,    0, -96.0f, DutyCycle::Duty50,   false, SweepDirection::None, 1.0f,  12.0f, 0.0f, 4.0f},
        {"duty 12.5%",        0.0f,    0,   0.0f, DutyCycle::Duty12_5,  true, SweepDirection::None, 1.0f,  12.0f, 0.0f, 4.0f},
        {"duty 75%",          0.0f,    0,   0.0f, DutyCycle::Duty75,    true, SweepDirection::None, 1.0f,  12.0f, 0.0f, 4.0f},
        {"sweep up fast",     0.0f,    0,   0.0f, DutyCycle::Duty50,   false, SweepDirection::Up,  99.0f,  96.0f, 0.0f, 4.0f},
        {"sweep down fast",   0.0f,    0,   0.0f, DutyCycle::Duty50,   false, SweepDirection::Down,99.0f,  96.0f, 0.0f, 4.0f},
        {"full tremolo",      0.0f,    0,   0.0f, DutyCycle::Duty50,   false, SweepDirection::None, 1.0f,  12.0f, 1.0f, 50.0f},
        {"everything at once",12.0f, 0xFF,  48.0f, DutyCycle::Duty25,   true, SweepDirection::Up,  99.0f,  96.0f, 1.0f, 50.0f},
    };

    // Run each effect case against a representative melodic oscillator and a drum
    const OscillatorType oscs[] = { OscillatorType::Pulse, OscillatorType::Supersaw, OscillatorType::Kick808 };

    for (const FxCase& c : cases) {
        for (OscillatorType osc : oscs) {
            Synthesizer synth;
            synth.setSampleRate(sampleRate);
            synth.noteOn(60, 1.0f, 0.0f, 0.0f, 0.0f, 0.4f, osc,
                         c.vibrato, c.arpeggio, c.slide,
                         c.duty, c.useDuty,
                         c.sweepDir, c.sweepSpd, c.sweepAmt,
                         c.tremolo, c.tremoloSpd);

            std::vector<float> buf(frames);
            for (int i = 0; i < frames; ++i) buf[i] = synth.process(float(i) / sampleRate);

            BufferStats st = analyze(buf);
            std::string where = std::string(c.label) + " on " + oscName(osc);
            check(st.nonFiniteCount == 0, where + " produced non-finite samples");
            check(st.outOfRangeCount == 0,
                  where + " exceeded headroom (peak " + std::to_string(st.peak) +
                  " at sample " + std::to_string(st.peakIndex) + " of " +
                  std::to_string(buf.size()) + ")");
        }
    }
}

// ============================================================================
// 3. Every effect at its parameter extremes
// ============================================================================
static void testEffectExtremes() {
    beginTest("Channel effects at parameter extremes");

    const float sampleRate = 44100.0f;
    const int frames = int(sampleRate * 1.0f);

    // Drive each effect with a loud, broadband signal - the worst case for
    // feedback-based effects (delay, flanger, phaser, reverb).
    auto makeInput = [&](int i) {
        float t = float(i) / sampleRate;
        float saw = 2.0f * std::fmod(t * 220.0f, 1.0f) - 1.0f;
        float noise = ((i * 1103515245 + 12345) & 0x7FFF) / 16384.0f - 1.0f;
        return 0.9f * saw + 0.1f * noise;
    };

    // Each lambda configures one effect at its most extreme setting.
    struct EffectCase {
        const char* label;
        void (*configure)(EffectsChain&);
    };

    const EffectCase cases[] = {
        {"delay max feedback", [](EffectsChain& fx) {
            fx.delayEnabled = true; fx.delay.delayTime = 0.001f;
            fx.delay.feedback = 0.99f; fx.delay.mix = 1.0f; }},
        {"delay zero time", [](EffectsChain& fx) {
            fx.delayEnabled = true; fx.delay.delayTime = 0.0f;
            fx.delay.feedback = 0.95f; fx.delay.mix = 1.0f; }},
        {"flanger max feedback", [](EffectsChain& fx) {
            fx.flangerEnabled = true; fx.flanger.rate = 10.0f;
            fx.flanger.depth = 0.01f; fx.flanger.feedback = 0.95f; fx.flanger.mix = 1.0f; }},
        {"flanger negative feedback", [](EffectsChain& fx) {
            fx.flangerEnabled = true; fx.flanger.rate = 0.1f;
            fx.flanger.depth = 0.001f; fx.flanger.feedback = -0.95f; fx.flanger.mix = 1.0f; }},
        {"phaser extreme", [](EffectsChain& fx) {
            fx.phaserEnabled = true; fx.phaser.rate = 10.0f;
            fx.phaser.depth = 1.0f; fx.phaser.feedback = 0.95f; }},
        {"reverb max", [](EffectsChain& fx) {
            fx.reverbEnabled = true; fx.reverb.mix = 1.0f;
            fx.reverb.roomSize = 1.0f; fx.reverb.damping = 0.0f; }},
        {"filter min cutoff max res", [](EffectsChain& fx) {
            fx.filterEnabled = true; fx.filter.cutoff = 20.0f; fx.filter.resonance = 1.0f; }},
        {"filter max cutoff max res", [](EffectsChain& fx) {
            fx.filterEnabled = true; fx.filter.cutoff = 20000.0f; fx.filter.resonance = 1.0f; }},
        {"distortion max drive", [](EffectsChain& fx) {
            fx.distortionEnabled = true; fx.distortion.drive = 100.0f; fx.distortion.mix = 1.0f; }},
        {"bitcrusher 1 bit", [](EffectsChain& fx) {
            fx.bitcrusherEnabled = true; fx.bitcrusher.bitDepth = 1;
            fx.bitcrusher.sampleRateReduction = 64; }},
        {"eq max boost", [](EffectsChain& fx) {
            fx.eqEnabled = true; fx.eq.lowGain = 12.0f; fx.eq.midGain = 12.0f; fx.eq.highGain = 12.0f; }},
        {"eq max cut", [](EffectsChain& fx) {
            fx.eqEnabled = true; fx.eq.lowGain = -12.0f; fx.eq.midGain = -12.0f; fx.eq.highGain = -12.0f; }},
        {"compressor extreme", [](EffectsChain& fx) {
            fx.compressorEnabled = true; fx.compressor.threshold = -60.0f;
            fx.compressor.ratio = 20.0f; fx.compressor.attack = 0.0f;
            fx.compressor.release = 0.0f; fx.compressor.makeupGain = 24.0f; }},
        {"chorus max", [](EffectsChain& fx) {
            fx.chorusEnabled = true; fx.chorus.mix = 1.0f;
            fx.chorus.rate = 10.0f; fx.chorus.depth = 1.0f; }},
        {"tremolo max", [](EffectsChain& fx) {
            fx.tremoloEnabled = true; fx.tremolo.rate = 50.0f; fx.tremolo.depth = 1.0f; }},
        {"stereo widener max", [](EffectsChain& fx) {
            fx.stereoWidenerEnabled = true; fx.stereoWidener.width = 2.0f;
            fx.stereoWidener.haasDelay = 0.05f; fx.stereoWidener.mix = 1.0f; }},
        {"tape saturation max", [](EffectsChain& fx) {
            fx.tapeSaturationEnabled = true; fx.tapeSaturation.drive = 1.0f;
            fx.tapeSaturation.warmth = 1.0f; fx.tapeSaturation.compression = 1.0f;
            fx.tapeSaturation.mix = 1.0f; }},
        {"formant max resonance", [](EffectsChain& fx) {
            fx.formantEnabled = true; fx.formant.resonance = 1.0f; }},
        {"sidechain max", [](EffectsChain& fx) {
            fx.sidechainEnabled = true; fx.sidechain.amount = 1.0f; fx.sidechain.release = 0.0f; }},
        {"everything on at once", [](EffectsChain& fx) {
            fx.delayEnabled = fx.flangerEnabled = fx.phaserEnabled = true;
            fx.reverbEnabled = fx.filterEnabled = fx.distortionEnabled = true;
            fx.bitcrusherEnabled = fx.eqEnabled = fx.compressorEnabled = true;
            fx.chorusEnabled = fx.tremoloEnabled = fx.stereoWidenerEnabled = true;
            fx.tapeSaturationEnabled = fx.formantEnabled = fx.sidechainEnabled = true;
            fx.delay.feedback = 0.9f; fx.flanger.feedback = 0.9f; fx.phaser.feedback = 0.9f;
            fx.distortion.drive = 50.0f; }},
    };

    for (const EffectCase& c : cases) {
        EffectsChain fx;
        fx.setSampleRate(int(sampleRate));
        c.configure(fx);

        std::vector<float> buf(frames);
        for (int i = 0; i < frames; ++i) {
            buf[i] = fx.process(makeInput(i), float(i) / sampleRate);
        }

        BufferStats st = analyze(buf);
        check(st.nonFiniteCount == 0,
              std::string(c.label) + " produced " + std::to_string(st.nonFiniteCount) +
              " non-finite samples");
        check(st.outOfRangeCount == 0,
              std::string(c.label) + " ran away (peak " + std::to_string(st.peak) + ")");
    }
}

// ============================================================================
// 4. Polyphony overflow and voice stealing
// ============================================================================
static void testPolyphonyOverflow() {
    beginTest("Polyphony overflow and voice stealing");

    const float sampleRate = 44100.0f;
    Synthesizer synth;
    synth.setSampleRate(sampleRate);

    // Throw four times the available polyphony at it
    const int noteCount = Synthesizer::MAX_VOICES * 4;
    for (int i = 0; i < noteCount; ++i) {
        synth.noteOn(36 + i, 1.0f, float(i) * 0.001f, 0.0f, 0.0f, 2.0f, OscillatorType::Supersaw);
    }

    std::vector<float> polyBuf(static_cast<size_t>(sampleRate));
    for (int i = 0; i < int(polyBuf.size()); ++i) polyBuf[i] = synth.process(float(i) / sampleRate);

    BufferStats st = analyze(polyBuf);
    check(st.nonFiniteCount == 0, "overflowing polyphony produced non-finite samples");
    check(st.outOfRangeCount == 0,
          "overflowing polyphony exceeded headroom (peak " + std::to_string(st.peak) + ")");
    check(!st.allZero, "overflowing polyphony produced silence");

    // Releasing notes that were stolen (and notes never played) must not crash
    for (int i = 0; i < noteCount; ++i) synth.noteOff(36 + i, 1.0f);
    synth.noteOff(200, 1.0f);
    synth.allNotesOff();
    check(true, "releasing stolen and unknown notes did not crash");
}

// ============================================================================
// 5. Sequencer timing edge cases
// ============================================================================
static void testSequencerEdgeCases() {
    beginTest("Sequencer timing and arrangement edge cases");

    const float sampleRate = 44100.0f;
    const uint32_t blockSize = 512;
    std::vector<float> left(blockSize), right(blockSize);

    auto renderBlocks = [&](Sequencer& seq, int blocks, const std::string& label) {
        BufferStats worst;
        for (int b = 0; b < blocks; ++b) {
            seq.process(left.data(), right.data(), blockSize);
            BufferStats ls = analyze(left);
            BufferStats rs = analyze(right);
            worst.nonFiniteCount += ls.nonFiniteCount + rs.nonFiniteCount;
            worst.outOfRangeCount += ls.outOfRangeCount + rs.outOfRangeCount;
            worst.peak = std::max({worst.peak, ls.peak, rs.peak});
        }
        check(worst.nonFiniteCount == 0, label + " produced non-finite output");
        check(worst.outOfRangeCount == 0,
              label + " exceeded headroom (peak " + std::to_string(worst.peak) + ")");
        return worst;
    };

    // --- A project with no patterns at all -----------------------------
    {
        Project p;
        p.patterns.clear();
        p.arrangement.clear();
        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(sampleRate);
        seq.setProject(&p);
        seq.play();
        renderBlocks(seq, 20, "empty project");
    }

    // --- An empty pattern referenced by a clip --------------------------
    {
        Project p;
        p.patterns.clear();
        p.patterns.push_back(Pattern{});
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(sampleRate);
        seq.setProject(&p);
        seq.play();
        renderBlocks(seq, 20, "empty pattern");
    }

    // --- Clips with out-of-range and negative values ---------------------
    {
        Project p;
        p.patterns.clear();
        Pattern pat;
        pat.notes.push_back(Note{});
        p.patterns.push_back(pat);

        p.arrangement.clear();
        p.arrangement.push_back(Clip{ 99, 0,   0.0f,  4.0f, 0}); // pattern index past end
        p.arrangement.push_back(Clip{ -1, 0,   0.0f,  4.0f, 0}); // negative pattern index
        p.arrangement.push_back(Clip{  0, 0, -16.0f,  4.0f, 0}); // starts before zero
        p.arrangement.push_back(Clip{  0, 0,   0.0f,  0.0f, 0}); // zero length
        p.arrangement.push_back(Clip{  0, 0,   0.0f, -4.0f, 0}); // negative length
        p.arrangement.push_back(Clip{  0, 99,  0.0f,  4.0f, 0}); // channel past end
        p.arrangement.push_back(Clip{  0, -1,  0.0f,  4.0f, 0}); // negative channel

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(sampleRate);
        seq.setProject(&p);
        seq.play();
        renderBlocks(seq, 40, "malformed clips");
    }

    // --- BPM extremes ----------------------------------------------------
    {
        const float bpms[] = { 1.0f, 20.0f, 300.0f, 999.0f };
        for (float bpm : bpms) {
            Project p;
            p.bpm = bpm;
            Pattern pat;
            for (int i = 0; i < 16; ++i) {
                Note n;
                n.pitch = 48 + i;
                n.startTime = float(i) * 0.25f;
                n.duration = 0.25f;
                pat.notes.push_back(n);
            }
            p.patterns.clear();
            p.patterns.push_back(pat);
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
            seq.setSampleRate(sampleRate);
            seq.setProject(&p);
            seq.play();
            renderBlocks(seq, 20, "BPM " + std::to_string(int(bpm)));
        }
    }

    // --- Degenerate loop ranges ------------------------------------------
    {
        struct LoopCase { float start, end; const char* label; };
        const LoopCase loops[] = {
            { 0.0f,  0.0f, "zero-length loop"      },
            { 8.0f,  4.0f, "reversed loop"         },
            {-4.0f,  4.0f, "loop starting negative"},
            { 0.0f, 1e9f,  "absurdly long loop"    },
        };
        for (const LoopCase& lc : loops) {
            Project p;
            Pattern pat;
            Note n; n.pitch = 60; n.startTime = 0.0f; n.duration = 1.0f;
            pat.notes.push_back(n);
            p.patterns.clear();
            p.patterns.push_back(pat);
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
            seq.setSampleRate(sampleRate);
            seq.setProject(&p);
            seq.setLoop(true, lc.start, lc.end);
            seq.play();
            renderBlocks(seq, 30, lc.label);
        }
    }

    // --- Notes with degenerate timing -------------------------------------
    {
        Project p;
        Pattern pat;
        auto addNote = [&](float start, float dur, int pitch, float vel) {
            Note n; n.startTime = start; n.duration = dur; n.pitch = pitch; n.velocity = vel;
            pat.notes.push_back(n);
        };
        addNote( 0.0f,  0.0f,  60, 1.0f);   // zero duration
        addNote( 0.0f, -1.0f,  60, 1.0f);   // negative duration
        addNote(-4.0f,  1.0f,  60, 1.0f);   // starts before the pattern
        addNote( 0.0f,  1.0f, 200, 1.0f);   // pitch above MIDI range
        addNote( 0.0f,  1.0f,  -5, 1.0f);   // negative pitch
        addNote( 0.0f,  1.0f,  60, 5.0f);   // velocity above 1.0
        addNote( 0.0f,  1.0f,  60, -1.0f);  // negative velocity
        addNote( 0.0f, 1e6f,   60, 1.0f);   // absurdly long
        p.patterns.clear();
        p.patterns.push_back(pat);
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 8.0f, 0});

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(sampleRate);
        seq.setProject(&p);
        seq.play();
        renderBlocks(seq, 40, "degenerate notes");
    }

    // --- Transport hammering ----------------------------------------------
    {
        Project p;
        Pattern pat;
        Note n; n.pitch = 60; n.duration = 4.0f;
        pat.notes.push_back(n);
        p.patterns.clear();
        p.patterns.push_back(pat);
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(sampleRate);
        seq.setProject(&p);

        for (int i = 0; i < 50; ++i) {
            seq.play();
            seq.process(left.data(), right.data(), blockSize);
            seq.pause();
            seq.process(left.data(), right.data(), blockSize);
            seq.stop();
            seq.setPosition(float(i) * 0.37f - 5.0f); // includes negative positions
            seq.process(left.data(), right.data(), blockSize);
        }
        BufferStats st = analyze(left);
        check(st.nonFiniteCount == 0, "transport hammering produced non-finite output");
    }

    // --- A null project must not crash the audio callback ------------------
    {
        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(sampleRate);
        seq.play();
        seq.process(left.data(), right.data(), blockSize);
        BufferStats st = analyze(left);
        check(st.nonFiniteCount == 0, "sequencer with no project produced non-finite output");
        check(st.allZero, "sequencer with no project should output silence");
    }
}

// ============================================================================
// 6. Save / load round-trip fidelity
// ============================================================================

// Build a project that touches every instrument and every note-effect field,
// so a missing entry in the save/load tables shows up as a difference.
static Project makeKitchenSinkProject() {
    Project p;
    p.name = "RoundTrip";
    p.bpm = 137.5f;
    p.beatsPerMeasure = 7;
    p.masterVolume = 0.63f;
    p.songLength = 123.0f;
    p.swing = 0.42f;
    p.swingGrid = 0.25f;
    p.humanize = true;
    p.humanizeAmount = 0.031f;
    p.humanizeVelocity = 0.17f;

    p.masterEQEnabled = true;
    p.masterEQLowGain = 3.5f;
    p.masterEQMidGain = -2.25f;
    p.masterEQHighGain = 1.75f;
    p.masterCompressorEnabled = true;
    p.masterCompThreshold = -18.5f;
    p.masterCompRatio = 3.5f;
    p.masterLimiterEnabled = true;
    p.masterLimiterCeiling = -0.5f;

    // Give every channel a distinct, non-default effect configuration
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        ChannelConfig& c = p.channels[ch];
        c.volume = 0.5f + 0.05f * ch;
        c.pan = -0.7f + 0.2f * ch;
        c.filterEnabled = true;
        c.filterCutoff = 500.0f + 300.0f * ch;
        c.filterResonance = 0.1f * ch;
        c.filterEnvEnabled = true;
        c.filterEnvAmount = 0.1f * ch;
        c.delayEnabled = (ch % 2 == 0);
        c.delayTime = 0.125f * (ch + 1);
        c.reverbEnabled = (ch % 3 == 0);
        c.reverbMix = 0.05f * ch;
    }

    // One pattern holding a note per oscillator type, each with every
    // per-note effect field set to a distinctive value.
    Pattern pat;
    pat.name = "Everything";
    for (int i = 0; i < OSC_COUNT; ++i) {
        Note n;
        n.pitch = 24 + (i % 84);
        n.velocity = 0.25f + 0.005f * i;
        n.startTime = 0.25f * i;
        n.duration = 0.5f;
        n.oscillatorType = static_cast<OscillatorType>(i);
        n.fadeIn = 0.01f * (i % 5);
        n.fadeOut = 0.02f * (i % 4);
        n.arpeggio = i % 256;
        n.vibrato = 0.1f * (i % 7);
        n.vibratoSpeed = 4.0f + (i % 5);
        n.slide = -3.0f + 0.5f * (i % 9);
        n.dutyCycle = static_cast<DutyCycle>(i % 4);
        n.useDutyCycle = (i % 2 == 0);
        n.sweepDirection = static_cast<SweepDirection>(i % 3);
        n.sweepSpeed = 0.5f + 0.25f * (i % 4);
        n.sweepAmount = 6.0f + (i % 12);
        n.echoRepeats = i % 5;
        n.echoDelay = 0.125f * (1 + i % 3);
        n.echoDecay = 0.3f + 0.1f * (i % 5);
        n.retriggerCount = i % 4;
        n.retriggerSpeed = 0.0625f * (1 + i % 3);
        n.noteCut = 0.05f * (i % 6);
        n.noteDelay = 0.03f * (i % 5);
        n.tremolo = 0.1f * (i % 8);
        n.tremoloSpeed = 3.0f + (i % 6);
        pat.notes.push_back(n);
    }
    p.patterns.clear();
    p.patterns.push_back(pat);

    p.arrangement.clear();
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        p.arrangement.push_back(Clip{0, ch, 4.0f * ch, 4.0f, 0xFF00FF00u});
    }

    return p;
}

static std::string readWholeFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void testSaveLoadRoundTrip() {
    beginTest("Project save/load round-trip");

    Project original = makeKitchenSinkProject();

    const std::string pathA = testPath("test_roundtrip_a.ctp");
    const std::string pathB = testPath("test_roundtrip_b.ctp");

    check(saveProject(original, pathA), "first save succeeded");

    Project reloaded;
    check(loadProject(reloaded, pathA), "load succeeded");

    check(saveProject(reloaded, pathB), "second save succeeded");

    std::string a = readWholeFile(pathA);
    std::string b = readWholeFile(pathB);
    check(!a.empty(), "saved file is not empty");

    // A save -> load -> save cycle must be a fixed point. If it is not, some
    // field is being dropped or defaulted on the way through.
    if (a != b) {
        // Report the first differing line to make the failure actionable
        std::istringstream sa(a), sb(b);
        std::string la, lb;
        int line = 1;
        std::string detail = "files differ";
        while (std::getline(sa, la) && std::getline(sb, lb)) {
            if (la != lb) {
                detail = "line " + std::to_string(line) + ": saved '" + la +
                         "' but reloaded as '" + lb + "'";
                break;
            }
            ++line;
        }
        check(false, "save/load is not a fixed point - " + detail);
    } else {
        check(true, "save/load/save is byte-identical");
    }

    // Spot-check the fields most likely to be silently dropped
    check(reloaded.patterns.size() == original.patterns.size(),
          "pattern count survived the round trip");

    // ---- Everything that used to be silently dropped on save -----------
    //
    // The v1 format stored seven fields per note and nothing else: no channel
    // settings, no effects, no arrangement, no master bus, no groove. Saving
    // a finished song kept the notes and threw away the mix. These checks are
    // the guard against that regressing.

    auto nearlyEqual = [](float a, float b) {
        return std::fabs(a - b) <= 1e-4f * std::max(1.0f, std::fabs(a));
    };

    check(nearlyEqual(reloaded.bpm, original.bpm),
          "BPM survived (" + std::to_string(original.bpm) + " -> " +
          std::to_string(reloaded.bpm) + ")");
    check(reloaded.beatsPerMeasure == original.beatsPerMeasure,
          "time signature survived");
    check(nearlyEqual(reloaded.songLength, original.songLength),
          "song length survived");
    check(reloaded.name == original.name, "project name survived");

    // Groove
    check(nearlyEqual(reloaded.swing, original.swing), "swing amount survived");
    check(nearlyEqual(reloaded.swingGrid, original.swingGrid), "swing grid survived");
    check(reloaded.humanize == original.humanize, "humanize flag survived");
    check(nearlyEqual(reloaded.humanizeAmount, original.humanizeAmount),
          "humanize amount survived");

    // Master bus
    check(reloaded.masterEQEnabled == original.masterEQEnabled,
          "master EQ enable survived");
    check(nearlyEqual(reloaded.masterEQLowGain, original.masterEQLowGain),
          "master EQ low gain survived");
    check(reloaded.masterCompressorEnabled == original.masterCompressorEnabled,
          "master compressor enable survived");
    check(nearlyEqual(reloaded.masterCompThreshold, original.masterCompThreshold),
          "master compressor threshold survived");
    check(nearlyEqual(reloaded.masterLimiterCeiling, original.masterLimiterCeiling),
          "master limiter ceiling survived");

    // Channels - the entire mix
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        const ChannelConfig& want = original.channels[ch];
        const ChannelConfig& got = reloaded.channels[ch];
        const std::string at = "channel " + std::to_string(ch) + " ";

        check(got.name == want.name, at + "name survived");
        check(nearlyEqual(got.volume, want.volume), at + "volume survived");
        check(nearlyEqual(got.pan, want.pan), at + "pan survived");
        check(got.oscillator.type == want.oscillator.type, at + "oscillator survived");
        check(got.filterEnabled == want.filterEnabled, at + "filter enable survived");
        check(nearlyEqual(got.filterCutoff, want.filterCutoff), at + "filter cutoff survived");
        check(nearlyEqual(got.filterResonance, want.filterResonance),
              at + "filter resonance survived");
        check(got.filterEnvEnabled == want.filterEnvEnabled,
              at + "filter envelope enable survived");
        check(nearlyEqual(got.filterEnvAmount, want.filterEnvAmount),
              at + "filter envelope amount survived");
        check(got.delayEnabled == want.delayEnabled, at + "delay enable survived");
        check(nearlyEqual(got.delayTime, want.delayTime), at + "delay time survived");
        check(got.reverbEnabled == want.reverbEnabled, at + "reverb enable survived");
        check(nearlyEqual(got.reverbMix, want.reverbMix), at + "reverb mix survived");
    }

    // Arrangement - the song structure
    check(reloaded.arrangement.size() == original.arrangement.size(),
          "clip count survived (" + std::to_string(original.arrangement.size()) +
          " -> " + std::to_string(reloaded.arrangement.size()) + ")");
    for (size_t i = 0; i < std::min(reloaded.arrangement.size(), original.arrangement.size()); ++i) {
        const Clip& want = original.arrangement[i];
        const Clip& got = reloaded.arrangement[i];
        const std::string at = "clip " + std::to_string(i) + " ";
        check(got.patternIndex == want.patternIndex, at + "pattern index survived");
        check(got.channelIndex == want.channelIndex, at + "channel index survived");
        check(nearlyEqual(got.startBeat, want.startBeat), at + "start beat survived");
        check(nearlyEqual(got.lengthBeats, want.lengthBeats), at + "length survived");
        check(got.color == want.color, at + "colour survived");
    }

    // Per-note tracker effects
    if (!reloaded.patterns.empty() && !original.patterns.empty()) {
        const auto& wantNotes = original.patterns[0].notes;
        const auto& gotNotes = reloaded.patterns[0].notes;
        const size_t n = std::min(wantNotes.size(), gotNotes.size());

        int mismatches = 0;
        std::string firstMismatch;
        auto compare = [&](bool ok, const char* field, size_t i) {
            if (!ok) {
                ++mismatches;
                if (firstMismatch.empty()) {
                    firstMismatch = std::string(field) + " on note " + std::to_string(i);
                }
            }
        };

        for (size_t i = 0; i < n; ++i) {
            const Note& w = wantNotes[i];
            const Note& g = gotNotes[i];
            compare(g.pitch == w.pitch, "pitch", i);
            compare(nearlyEqual(g.velocity, w.velocity), "velocity", i);
            compare(nearlyEqual(g.startTime, w.startTime), "startTime", i);
            compare(nearlyEqual(g.duration, w.duration), "duration", i);
            compare(nearlyEqual(g.fadeIn, w.fadeIn), "fadeIn", i);
            compare(nearlyEqual(g.fadeOut, w.fadeOut), "fadeOut", i);
            compare(g.arpeggio == w.arpeggio, "arpeggio", i);
            compare(nearlyEqual(g.vibrato, w.vibrato), "vibrato", i);
            compare(nearlyEqual(g.vibratoSpeed, w.vibratoSpeed), "vibratoSpeed", i);
            compare(nearlyEqual(g.slide, w.slide), "slide", i);
            compare(g.dutyCycle == w.dutyCycle, "dutyCycle", i);
            compare(g.useDutyCycle == w.useDutyCycle, "useDutyCycle", i);
            compare(g.sweepDirection == w.sweepDirection, "sweepDirection", i);
            compare(nearlyEqual(g.sweepSpeed, w.sweepSpeed), "sweepSpeed", i);
            compare(nearlyEqual(g.sweepAmount, w.sweepAmount), "sweepAmount", i);
            compare(g.echoRepeats == w.echoRepeats, "echoRepeats", i);
            compare(nearlyEqual(g.echoDelay, w.echoDelay), "echoDelay", i);
            compare(nearlyEqual(g.echoDecay, w.echoDecay), "echoDecay", i);
            compare(g.retriggerCount == w.retriggerCount, "retriggerCount", i);
            compare(nearlyEqual(g.retriggerSpeed, w.retriggerSpeed), "retriggerSpeed", i);
            compare(nearlyEqual(g.noteCut, w.noteCut), "noteCut", i);
            compare(nearlyEqual(g.noteDelay, w.noteDelay), "noteDelay", i);
            compare(nearlyEqual(g.tremolo, w.tremolo), "tremolo", i);
            compare(nearlyEqual(g.tremoloSpeed, w.tremoloSpeed), "tremoloSpeed", i);
            compare(g.sampleID == w.sampleID, "sampleID", i);
        }

        check(mismatches == 0,
              std::to_string(mismatches) + " note effect field(s) lost on save/load" +
              (firstMismatch.empty() ? "" : " (first: " + firstMismatch + ")"));
    }
    if (!reloaded.patterns.empty() && !original.patterns.empty()) {
        check(reloaded.patterns[0].notes.size() == original.patterns[0].notes.size(),
              "note count survived the round trip");

        // Every oscillator type must come back as itself, not as Pulse
        size_t n = std::min(reloaded.patterns[0].notes.size(), original.patterns[0].notes.size());
        int downgraded = 0;
        std::string firstBad;
        for (size_t i = 0; i < n; ++i) {
            OscillatorType want = original.patterns[0].notes[i].oscillatorType;
            OscillatorType got = reloaded.patterns[0].notes[i].oscillatorType;
            if (want != got) {
                ++downgraded;
                if (firstBad.empty()) {
                    firstBad = std::string(oscillatorTypeToString(want)) + " came back as " +
                               oscillatorTypeToString(got);
                }
            }
        }
        check(downgraded == 0,
              std::to_string(downgraded) + " oscillator(s) changed type on load" +
              (firstBad.empty() ? "" : " (first: " + firstBad + ")"));
    }

    std::remove(pathA.c_str());
    std::remove(pathB.c_str());
}

// A v1 file has no CHANNEL, MASTER or CLIP lines and only seven fields per
// note. Those files exist on people's disks, so the v2 reader has to keep
// opening them: notes intact, everything absent left at its default.
static void testLoadsVersion1Files() {
    beginTest("Version 1 project files still load");

    const std::string path = testPath("test_v1.ctp");
    {
        std::ofstream f(path);
        f << "CHIPTUNE_PROJECT v1\n"
          << "NAME Old Song\n"
          << "BPM 140\n"
          << "BEATS_PER_MEASURE 3\n"
          << "MASTER_VOLUME 0.6\n"
          << "SONG_LENGTH 32\n"
          << "\n"
          << "PATTERN \"Lead\" 16\n"
          << "NOTE 60 0.0000 1.0000 1.0000 Pulse 0.0000 0.0000\n"
          << "NOTE 64 1.0000 0.5000 0.8000 Sawtooth 0.0100 0.0200\n"
          << "NOTE 67 2.0000 0.5000 0.9000 SynthBass 0.0000 0.0000\n"
          << "END_PATTERN\n"
          << "\n"
          << "END_PROJECT\n";
    }

    Project p;
    check(loadProject(p, path), "a v1 file loads");

    check(p.name == "Old Song", "v1 project name read (got '" + p.name + "')");
    check(std::fabs(p.bpm - 140.0f) < 0.01f, "v1 BPM read");
    check(p.beatsPerMeasure == 3, "v1 time signature read");
    check(std::fabs(p.masterVolume - 0.6f) < 0.01f, "v1 master volume read");
    check(std::fabs(p.songLength - 32.0f) < 0.01f, "v1 song length read");

    check(p.patterns.size() == 1, "v1 pattern read");
    if (!p.patterns.empty()) {
        check(p.patterns[0].name == "Lead", "v1 pattern name read");
        check(p.patterns[0].notes.size() == 3,
              "all three v1 notes read (got " +
              std::to_string(p.patterns[0].notes.size()) + ")");
        if (p.patterns[0].notes.size() == 3) {
            check(p.patterns[0].notes[0].pitch == 60, "v1 note pitch read");
            check(p.patterns[0].notes[1].oscillatorType == OscillatorType::Sawtooth,
                  "v1 note oscillator read");
            check(std::fabs(p.patterns[0].notes[1].fadeIn - 0.01f) < 0.001f,
                  "v1 note fade-in read");
            check(p.patterns[0].notes[2].oscillatorType == OscillatorType::SynthBass,
                  "v1 synth oscillator read");
        }
    }

    // Fields v1 never stored must be left at their defaults, not garbage
    const Project defaults;
    check(p.channels[0].volume == defaults.channels[0].volume,
          "v1 load leaves channel volume at its default");
    check(p.arrangement.empty(), "v1 load leaves the arrangement empty");

    // And re-saving a loaded v1 file must produce a valid v2 file
    const std::string upgraded = testPath("test_v1_upgraded.ctp");
    check(saveProject(p, upgraded), "a loaded v1 project saves as v2");
    Project reloaded;
    check(loadProject(reloaded, upgraded), "the upgraded file loads back");
    if (!reloaded.patterns.empty()) {
        check(reloaded.patterns[0].notes.size() == 3,
              "notes survive the v1 to v2 upgrade");
    }

    std::remove(path.c_str());
    std::remove(upgraded.c_str());
}

static void testLoadRejectsGarbage() {
    beginTest("Loader survives malformed files");

    struct Bad { const char* label; const char* content; };
    const Bad bad[] = {
        {"empty file",           ""},
        {"wrong header",         "NOT_A_CHIPTUNE_PROJECT\n"},
        {"truncated mid-note",   "CHIPTUNE_PROJECT v1\nNAME x\nBPM 120\nPATTERN 0 p\nNOTE 60\n"},
        {"absurd numbers",       "CHIPTUNE_PROJECT v1\nBPM 1e30\nSONG_LENGTH -1e30\nBEATS_PER_MEASURE 999999\n"},
        {"negative counts",      "CHIPTUNE_PROJECT v1\nBPM 120\nPATTERN -5 bad\n"},
        {"unknown oscillator",   "CHIPTUNE_PROJECT v1\nBPM 120\nPATTERN 0 p\nNOTE 60 1.0 0.0 1.0 NotARealOscillator\nEND_PATTERN\n"},
        {"binary junk",          "CHIPTUNE_PROJECT v1\n\x01\x02\x03\xff\xfe garbage \x00 more\n"},
    };

    for (const Bad& b : bad) {
        const std::string path = testPath("test_bad.ctp");
        {
            std::ofstream f(path, std::ios::binary);
            f.write(b.content, std::strlen(b.content));
        }
        Project p;
        // The contract is simply: do not crash, and do not leave the project
        // in a state that breaks the audio thread.
        loadProject(p, path);

        check(std::isfinite(p.bpm) && p.bpm > 0.0f,
              std::string(b.label) + " left BPM usable (got " + std::to_string(p.bpm) + ")");
        check(std::isfinite(p.songLength) && p.songLength >= 0.0f,
              std::string(b.label) + " left song length usable (got " + std::to_string(p.songLength) + ")");
        check(p.beatsPerMeasure > 0 && p.beatsPerMeasure <= 64,
              std::string(b.label) + " left beats-per-measure usable (got " +
              std::to_string(p.beatsPerMeasure) + ")");

        // And the resulting project must be renderable
        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.play();
        std::vector<float> l(256), r(256);
        seq.process(l.data(), r.data(), 256);
        BufferStats st = analyze(l);
        check(st.nonFiniteCount == 0,
              std::string(b.label) + " produced non-finite audio after loading");

        std::remove(path.c_str());
    }

    // A file that does not exist at all
    Project p;
    bool ok = loadProject(p, "definitely_not_a_real_file_12345.ctp");
    check(!ok, "loading a missing file reports failure");
}

// ============================================================================
// 7. MIDI export
// ============================================================================
static void testMidiExport() {
    beginTest("MIDI export");

    Project p = makeKitchenSinkProject();
    const std::string pathA = testPath("test_export_a.mid");
    const std::string pathB = testPath("test_export_b.mid");

    check(exportProjectToMIDI(p, pathA), "first MIDI export succeeded");
    check(exportProjectToMIDI(p, pathB), "second MIDI export succeeded");

    std::string a = readWholeFile(pathA);
    std::string b = readWholeFile(pathB);

    check(a.size() > 22, "exported MIDI is larger than a bare header");
    check(a.compare(0, 4, "MThd") == 0, "exported MIDI starts with an MThd chunk");

    // Two exports of the same project must be identical. They were not, once:
    // a function-local static suppressed program changes on the second run.
    check(a == b, "two exports of the same project are identical (" +
                  std::to_string(a.size()) + " vs " + std::to_string(b.size()) + " bytes)");

    // Both must actually contain program-change events (status nibble 0xC)
    auto hasProgramChange = [](const std::string& data) {
        for (unsigned char c : data) {
            if ((c & 0xF0) == 0xC0) return true;
        }
        return false;
    };
    check(hasProgramChange(a), "first export contains program changes");
    check(hasProgramChange(b), "second export contains program changes");

    // Exporting an empty project must not crash or produce a broken file
    Project empty;
    empty.patterns.clear();
    empty.arrangement.clear();
    check(exportProjectToMIDI(empty, testPath("test_export_empty.mid")),
          "exporting an empty project succeeded");

    std::remove(pathA.c_str());
    std::remove(pathB.c_str());
    std::remove(testPath("test_export_empty.mid").c_str());
}

// ============================================================================
// 8. WAV export / offline render
// ============================================================================
static void testWavExport() {
    beginTest("WAV export");

    Project p;
    Pattern pat;
    for (int i = 0; i < 8; ++i) {
        Note n;
        n.pitch = 60 + i;
        n.startTime = float(i) * 0.5f;
        n.duration = 0.5f;
        n.oscillatorType = OscillatorType::Pulse;
        pat.notes.push_back(n);
    }
    p.patterns.clear();
    p.patterns.push_back(pat);
    p.arrangement.clear();
    p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

    auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&p);

    const std::string path = testPath("test_render.wav");
    check(exportWav(p, seq, path, 4.0f), "WAV export succeeded");

    std::string data = readWholeFile(path);
    check(data.size() > 44, "WAV file is larger than a bare header");
    if (data.size() >= 12) {
        check(data.compare(0, 4, "RIFF") == 0, "WAV starts with RIFF");
        check(data.compare(8, 4, "WAVE") == 0, "WAV declares the WAVE format");
    }

    // Zero-length and negative-length renders must not hang or crash
    check(exportWav(p, seq, testPath("test_render_zero.wav"), 0.0f) || true,
          "zero-length render returned without hanging");
    check(exportWav(p, seq, testPath("test_render_neg.wav"), -5.0f) || true,
          "negative-length render returned without hanging");

    std::remove(path.c_str());
    std::remove(testPath("test_render_zero.wav").c_str());
    std::remove(testPath("test_render_neg.wav").c_str());
}

// ============================================================================
// 9. Channel config actually reaches the synth
// ============================================================================
static void testChannelConfigReachesSynth() {
    beginTest("Channel config reaches the synthesizer");

    // This is the regression test for the class of bug where a UI control is
    // wired to ChannelConfig but nothing copies it into the synth. Enabling a
    // dramatic effect must audibly change the rendered output.
    auto renderChannel = [](bool withEffect, void (*configure)(ChannelConfig&)) {
        Project p;
        Pattern pat;
        Note n;
        n.pitch = 60; n.startTime = 0.0f; n.duration = 2.0f;
        n.oscillatorType = OscillatorType::Sawtooth;
        pat.notes.push_back(n);
        p.patterns.clear();
        p.patterns.push_back(pat);
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        if (withEffect) configure(p.channels[0]);

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.play();

        std::vector<float> out;
        std::vector<float> l(512), r(512);
        for (int b = 0; b < 40; ++b) {
            seq.process(l.data(), r.data(), 512);
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    };

    struct ConfigCase {
        const char* label;
        void (*configure)(ChannelConfig&);
    };

    const ConfigCase cases[] = {
        {"bitcrusher", [](ChannelConfig& c) {
            c.bitcrusherEnabled = true; c.bitDepth = 2; c.sampleRateDiv = 32; }},
        {"distortion", [](ChannelConfig& c) {
            c.distortionEnabled = true; c.distortionDrive = 50.0f; c.distortionMix = 1.0f; }},
        {"lowpass filter", [](ChannelConfig& c) {
            c.filterEnabled = true; c.filterCutoff = 200.0f; c.filterResonance = 0.2f; }},
        {"EQ", [](ChannelConfig& c) {
            c.eqEnabled = true; c.eqLow = -12.0f; c.eqMid = -12.0f; c.eqHigh = -12.0f; }},
        {"compressor", [](ChannelConfig& c) {
            c.compressorEnabled = true; c.compThreshold = -40.0f;
            c.compRatio = 20.0f; c.compGain = 0.0f; }},
        {"formant filter", [](ChannelConfig& c) {
            c.formantEnabled = true; c.formantResonance = 0.9f; }},
        {"filter envelope", [](ChannelConfig& c) {
            c.filterEnvEnabled = true; c.filterEnvAmount = 1.0f;
            c.filterEnvAttack = 0.0f; c.filterEnvDecay = 0.05f; }},
        {"reverb", [](ChannelConfig& c) {
            c.reverbEnabled = true; c.reverbMix = 1.0f; c.reverbRoomSize = 0.9f; }},
        {"delay", [](ChannelConfig& c) {
            c.delayEnabled = true; c.delayMix = 1.0f;
            c.delayTime = 0.1f; c.delayFeedback = 0.7f; }},
        {"flanger", [](ChannelConfig& c) {
            c.flangerEnabled = true; c.flangerMix = 1.0f; c.flangerFeedback = 0.8f; }},
        {"chorus", [](ChannelConfig& c) {
            c.chorusEnabled = true; c.chorusMix = 1.0f; c.chorusDepth = 1.0f; }},
        {"phaser", [](ChannelConfig& c) {
            c.phaserEnabled = true; c.phaserDepth = 1.0f; c.phaserFeedback = 0.9f; }},
        {"tremolo", [](ChannelConfig& c) {
            c.tremoloEnabled = true; c.tremoloDepth = 1.0f; c.tremoloRate = 20.0f; }},
        {"stereo widener", [](ChannelConfig& c) {
            c.stereoWidenerEnabled = true; c.stereoWidenerWidth = 2.0f;
            c.stereoWidenerMix = 1.0f; }},
        {"tape saturation", [](ChannelConfig& c) {
            c.tapeSaturationEnabled = true; c.tapeDrive = 1.0f; c.tapeMix = 1.0f; }},
        {"sidechain", [](ChannelConfig& c) {
            c.sidechainEnabled = true; c.sidechainAmount = 1.0f;
            // A sidechain with no source channel is a no-op by design, so the
            // test has to name one for this to be a meaningful check.
            c.sidechainSource = 0; }},
    };

    std::vector<float> dry = renderChannel(false, cases[0].configure);
    BufferStats dryStats = analyze(dry);
    check(!dryStats.allZero, "baseline render is not silent");

    for (const ConfigCase& c : cases) {
        std::vector<float> wet = renderChannel(true, c.configure);
        BufferStats wetStats = analyze(wet);

        check(wetStats.nonFiniteCount == 0,
              std::string(c.label) + " produced non-finite audio");

        // Measure how different the two renders are
        size_t n = std::min(dry.size(), wet.size());
        double diff = 0.0;
        for (size_t i = 0; i < n; ++i) diff += std::fabs(double(wet[i]) - double(dry[i]));
        diff /= double(n ? n : 1);

        check(diff > 1e-5,
              std::string(c.label) + " had no audible effect (mean delta " +
              std::to_string(diff) + ") - is it wired from ChannelConfig to the synth?");
    }
}

// ============================================================================
// 10. Master bus
// ============================================================================
static void testMasterBus() {
    beginTest("Master bus limiter and loudness");

    Project p;
    p.masterLimiterEnabled = true;
    p.masterLimiterCeiling = -0.3f;
    p.masterVolume = 1.0f;

    // Stack many loud notes on every channel to slam the master bus
    Pattern pat;
    for (int i = 0; i < 64; ++i) {
        Note n;
        n.pitch = 30 + (i % 60);
        n.startTime = 0.0f;
        n.duration = 4.0f;
        n.velocity = 1.0f;
        n.oscillatorType = OscillatorType::Supersaw;
        pat.notes.push_back(n);
    }
    p.patterns.clear();
    p.patterns.push_back(pat);
    p.arrangement.clear();
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        p.arrangement.push_back(Clip{0, ch, 0.0f, 4.0f, 0});
        p.channels[ch].volume = 1.0f;
    }

    auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&p);
    seq.play();

    float peak = 0.0f;
    int nonFinite = 0;
    std::vector<float> l(512), r(512);
    for (int b = 0; b < 100; ++b) {
        seq.process(l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i) {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { ++nonFinite; continue; }
            peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
        }
    }

    check(nonFinite == 0, "master bus produced non-finite output under heavy load");
    // A brick-wall limiter set to -0.3 dB should hold output at or below 0 dBFS.
    // Allow a little overshoot for the attack, but not gross clipping.
    check(peak <= 1.05f,
          "limiter held the output at or below full scale (peak " + std::to_string(peak) + ")");
    check(peak > 0.05f, "master bus is not silent under heavy load");
}

// ============================================================================
// 11. Silence in, silence out
// ============================================================================
static void testSilenceIsSilent() {
    beginTest("Silence in, silence out");

    Project p;
    p.patterns.clear();
    p.arrangement.clear();
    // Turn on every master effect against an empty project
    p.masterEQEnabled = true;
    p.masterCompressorEnabled = true;
    p.masterLimiterEnabled = true;

    auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&p);
    seq.play();

    std::vector<float> l(512), r(512);
    float peak = 0.0f;
    int nonFinite = 0;
    for (int b = 0; b < 50; ++b) {
        seq.process(l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i) {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { ++nonFinite; continue; }
            peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
        }
    }

    check(nonFinite == 0, "idle master chain produced non-finite output");
    check(peak < 1e-4f,
          "an idle project is silent (peak " + std::to_string(peak) + ") - "
          "a noise floor here means an effect is self-oscillating");
}

// ============================================================================
// 12. Oscillator name tables stay in sync with the enum
// ============================================================================
static void testOscillatorNameTables() {
    beginTest("Oscillator name tables cover every enum value");

    int missingSave = 0, missingLoad = 0;
    std::string firstMissingSave, firstMissingLoad;

    for (int i = 0; i < OSC_COUNT; ++i) {
        OscillatorType t = static_cast<OscillatorType>(i);
        std::string name = oscillatorTypeToString(t);

        // Anything that is not Pulse but serialises as "Pulse" is missing
        // from the save table.
        if (t != OscillatorType::Pulse && name == "Pulse") {
            ++missingSave;
            if (firstMissingSave.empty()) firstMissingSave = "index " + std::to_string(i);
        }

        // And the load table must map that name back to the same value.
        OscillatorType back = stringToOscillatorType(name);
        if (back != t && t != OscillatorType::Pulse) {
            ++missingLoad;
            if (firstMissingLoad.empty()) firstMissingLoad = name;
        }
    }

    check(missingSave == 0,
          std::to_string(missingSave) + " oscillator(s) missing from the save table" +
          (firstMissingSave.empty() ? "" : " (first: " + firstMissingSave + ")"));
    check(missingLoad == 0,
          std::to_string(missingLoad) + " oscillator(s) missing from the load table" +
          (firstMissingLoad.empty() ? "" : " (first: " + firstMissingLoad + ")"));
}

// ============================================================================
// 13. Instrument macros
// ============================================================================
static void testInstrumentMacros() {
    beginTest("Instrument macros");

    // ---- Sequence semantics, independent of audio ----------------------
    {
        Macro m;
        m.enabled = true;
        m.steps = {15, 10, 5};

        check(m.valueAt(0) == 15 && m.valueAt(1) == 10 && m.valueAt(2) == 5,
              "macro reads back its steps");
        check(m.valueAt(3) == 5 && m.valueAt(99) == 5,
              "a non-looping macro holds its last value");
        check(m.isFinished(3), "a non-looping macro reports finishing");

        m.loopStart = 1;
        check(m.valueAt(3) == 10 && m.valueAt(4) == 5 && m.valueAt(5) == 10,
              "a looping macro cycles from its loop point");
        check(!m.isFinished(99), "a looping macro never finishes");

        // Degenerate cases must not read out of bounds
        Macro empty;
        empty.enabled = true;
        check(empty.valueAt(0) == 0 && empty.valueAt(-5) == 0 && empty.valueAt(1000) == 0,
              "an empty macro reads as zero at any position");
        check(!empty.isActive(), "an empty macro is not active");

        Macro badLoop;
        badLoop.enabled = true;
        badLoop.steps = {1, 2};
        badLoop.loopStart = 99;  // past the end
        check(std::isfinite(float(badLoop.valueAt(50))),
              "an out-of-range loop point does not read out of bounds");
    }

    // ---- Fixed vs relative arpeggio steps ------------------------------
    {
        ArpeggioMacro arp;
        arp.enabled = true;
        arp.steps = {0, 4, 7};
        arp.fixed = {0, 1, 0};
        check(!arp.isFixedAt(0) && arp.isFixedAt(1) && !arp.isFixedAt(2),
              "arpeggio fixed flags read back per step");

        ArpeggioMacro noFlags;
        noFlags.enabled = true;
        noFlags.steps = {0, 4, 7};
        check(!noFlags.isFixedAt(0) && !noFlags.isFixedAt(2),
              "an arpeggio with no flags is entirely relative");
    }

    // ---- Macros audibly change the rendered note -----------------------
    const float sampleRate = 44100.0f;
    const int frames = int(sampleRate * 0.5f);

    auto renderWithMacros = [&](const InstrumentMacros& macros, bool quantize) {
        ChannelConfig config;
        config.oscillator.type = OscillatorType::Pulse;
        config.macros = macros;
        config.quantizeVolume4Bit = quantize;

        Synthesizer synth;
        synth.setSampleRate(sampleRate);
        synth.setChannelConfig(config);
        synth.noteOn(60, 1.0f, 0.0f, 0.0f, 0.0f, 0.45f, OscillatorType::Pulse);

        std::vector<float> buf(frames);
        for (int i = 0; i < frames; ++i) buf[i] = synth.process(float(i) / sampleRate);
        return buf;
    };

    const InstrumentMacros none;
    std::vector<float> plain = renderWithMacros(none, false);
    BufferStats plainStats = analyze(plain);
    check(!plainStats.allZero, "a note with no macros still sounds");

    auto meanDelta = [](const std::vector<float>& a, const std::vector<float>& b) {
        const size_t n = std::min(a.size(), b.size());
        double d = 0.0;
        for (size_t i = 0; i < n; ++i) d += std::fabs(double(a[i]) - double(b[i]));
        return n ? d / double(n) : 0.0;
    };

    struct PresetCase { const char* label; InstrumentMacros macros; };
    std::vector<PresetCase> cases;
    for (const MacroPreset& preset : macroPresets()) {
        cases.push_back({preset.name, preset.macros});
    }

    for (const PresetCase& c : cases) {
        std::vector<float> withMacro = renderWithMacros(c.macros, false);
        BufferStats st = analyze(withMacro);

        check(st.nonFiniteCount == 0,
              std::string(c.label) + " macro produced non-finite audio");
        check(st.outOfRangeCount == 0,
              std::string(c.label) + " macro exceeded headroom (peak " +
              std::to_string(st.peak) + ")");
        check(!st.allZero, std::string(c.label) + " macro produced silence");
        check(meanDelta(plain, withMacro) > 1e-5,
              std::string(c.label) + " macro had no audible effect");
    }

    // The volume macro should govern the amplitude envelope. A pluck decays
    // to nothing, so its second half must be far quieter than its first.
    {
        std::vector<float> pluck = renderWithMacros(makePluck(), false);
        const size_t half = pluck.size() / 2;
        std::vector<float> firstHalf(pluck.begin(), pluck.begin() + half);
        std::vector<float> secondHalf(pluck.begin() + half, pluck.end());
        BufferStats a = analyze(firstHalf);
        BufferStats b = analyze(secondHalf);
        check(b.rms < a.rms,
              "the pluck volume macro decays (first half rms " + std::to_string(a.rms) +
              ", second " + std::to_string(b.rms) + ")");
    }

    // 4-bit quantisation must change the waveform without breaking it
    {
        std::vector<float> quantized = renderWithMacros(none, true);
        BufferStats st = analyze(quantized);
        check(st.nonFiniteCount == 0, "4-bit quantisation produced non-finite audio");
        check(!st.allZero, "4-bit quantisation produced silence");
    }

    // ---- Macros survive save and load ----------------------------------
    {
        Project p;
        p.channels[0].macros = makeMajorChordArp();
        p.channels[0].macros.arpeggio.fixed = {0, 1, 0};
        p.channels[0].macros.rateHz = 45.0f;
        p.channels[1].macros = makeLaserZap();
        p.channels[2].macros = makeSustainedLead();
        p.channels[3].quantizeVolume4Bit = true;

        const std::string path = testPath("test_macros.ctp");
        check(saveProject(p, path), "a project with macros saves");

        Project reloaded;
        check(loadProject(reloaded, path), "a project with macros loads");

        const InstrumentMacros& want = p.channels[0].macros;
        const InstrumentMacros& got = reloaded.channels[0].macros;

        check(got.arpeggio.enabled == want.arpeggio.enabled,
              "arpeggio macro enable survived");
        check(got.arpeggio.steps == want.arpeggio.steps,
              "arpeggio macro steps survived");
        check(got.arpeggio.loopStart == want.arpeggio.loopStart,
              "arpeggio macro loop point survived");
        check(got.arpeggio.fixed == want.arpeggio.fixed,
              "arpeggio fixed flags survived");
        check(std::fabs(got.rateHz - want.rateHz) < 0.01f,
              "macro rate survived (" + std::to_string(want.rateHz) + " -> " +
              std::to_string(got.rateHz) + ")");
        check(got.volume.steps == want.volume.steps, "volume macro steps survived");

        check(reloaded.channels[1].macros.pitch.steps == p.channels[1].macros.pitch.steps,
              "pitch macro steps survived");
        check(reloaded.channels[2].macros.volume.releaseStep ==
              p.channels[2].macros.volume.releaseStep,
              "volume macro release point survived");
        check(reloaded.channels[2].macros.duty.steps == p.channels[2].macros.duty.steps,
              "duty macro steps survived");
        check(reloaded.channels[3].quantizeVolume4Bit,
              "4-bit quantisation flag survived");

        std::remove(path.c_str());
    }

    // ---- A malformed macro must not reach the audio thread -------------
    {
        Project p;
        Macro& vol = p.channels[0].macros.volume;
        vol.enabled = true;
        vol.steps = {999, -999, 7};      // outside the 0..15 range
        vol.loopStart = 50;              // past the end
        vol.releaseStep = -80;
        p.channels[0].macros.rateHz = -5.0f;

        clampProjectToValidRanges(p);

        check(vol.steps[0] == 15 && vol.steps[1] == 0,
              "out-of-range volume macro steps are clamped to 0..15");
        check(vol.loopStart == -1, "an out-of-range loop point is cleared");
        check(vol.releaseStep == -1, "an out-of-range release point is cleared");
        check(p.channels[0].macros.rateHz > 0.0f, "a non-positive macro rate is repaired");

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.play();
        std::vector<float> l(512), r(512);
        for (int b = 0; b < 20; ++b) seq.process(l.data(), r.data(), 512);
        BufferStats st = analyze(l);
        check(st.nonFiniteCount == 0, "a repaired macro renders finite audio");
    }
}

// ============================================================================
// 14. Undo / redo history
// ============================================================================
static void testUndoRedo() {
    beginTest("Undo and redo history");

    // The old history snapshotted one pattern's notes, so editing a macro,
    // a channel effect or the arrangement was simply not undoable. These
    // tests exist mostly to hold the line on that: a snapshot is the whole
    // project now, and the round trip has to prove it.

    auto projectWith = [](int noteCount, float bpm) {
        Project p;
        p.bpm = bpm;
        Pattern pattern;
        for (int i = 0; i < noteCount; ++i) {
            Note n;
            n.pitch = 60 + i;
            n.startTime = float(i) * 0.25f;
            pattern.notes.push_back(n);
        }
        p.patterns.clear();
        p.patterns.push_back(pattern);
        return p;
    };

    // ---- Nothing to undo -----------------------------------------------
    {
        UndoHistory history;
        check(!history.canUndo(), "a fresh history has nothing to undo");
        check(!history.canRedo(), "a fresh history has nothing to redo");

        Project current = projectWith(3, 120.0f);
        check(!history.undo(current), "undo on an empty stack reports failure");
        check(!history.redo(current), "redo on an empty stack reports failure");
        check(current.patterns[0].notes.size() == 3,
              "a failed undo leaves the project untouched");
    }

    // ---- Basic undo then redo -------------------------------------------
    {
        UndoHistory history;
        Project current = projectWith(2, 120.0f);
        history.saveState(current, "Draw Note");
        check(history.canUndo(), "saving a state makes undo available");

        current = projectWith(5, 120.0f);
        check(history.undo(current), "undo succeeds");
        check(current.patterns[0].notes.size() == 2,
              "undo restores the saved state (expected 2 notes, got " +
              std::to_string(current.patterns[0].notes.size()) + ")");
        check(history.canRedo(), "undo makes redo available");

        check(history.redo(current), "redo succeeds");
        check(current.patterns[0].notes.size() == 5,
              "redo restores the state undo replaced (expected 5, got " +
              std::to_string(current.patterns[0].notes.size()) + ")");
    }

    // ---- Undo covers the whole project, not just notes -------------------
    //
    // This is the entire reason the history was rewritten. Every one of
    // these would have been silently lost by the old pattern-only snapshot.
    {
        UndoHistory history;
        Project current = makeKitchenSinkProject();

        const float originalCutoff = current.channels[0].filterCutoff;
        const size_t originalClips = current.arrangement.size();
        const size_t originalPatterns = current.patterns.size();
        const float originalBpm = current.bpm;
        const std::string originalName = current.channels[1].name;

        history.saveState(current, "Everything");

        // Wreck the things the old undo could not see.
        current.channels[0].filterCutoff = 137.0f;
        current.channels[0].filterEnabled = !current.channels[0].filterEnabled;
        current.channels[1].name = "clobbered";
        current.arrangement.clear();
        current.patterns.clear();
        current.patterns.push_back(Pattern());
        current.bpm = 71.0f;

        check(history.undo(current), "undo of a full project succeeds");

        check(std::fabs(current.channels[0].filterCutoff - originalCutoff) < 0.5f,
              "undo restores channel effect settings - the old history lost these");
        check(current.channels[1].name == originalName,
              "undo restores channel names");
        check(current.arrangement.size() == originalClips,
              "undo restores the arrangement - deleting a clip was unrecoverable before");
        check(current.patterns.size() == originalPatterns,
              "undo restores every pattern, not only the selected one");
        check(std::fabs(current.bpm - originalBpm) < 0.01f,
              "undo restores the tempo");
    }

    // ---- Labels drive the menu text ---------------------------------------
    {
        UndoHistory history;
        Project current = projectWith(2, 120.0f);
        history.saveState(current, "Draw Note");
        check(std::string(history.undoLabel()) == "Draw Note",
              "the history remembers what the action was, so the menu can name it");
    }

    // ---- An identical state is not recorded twice ------------------------
    //
    // Several gestures call saveState on the click that begins a drag and
    // again as it resolves. Recording both would spend an undo step that
    // appears to do nothing when the user presses Ctrl+Z.
    {
        UndoHistory history;
        Project current = projectWith(4, 120.0f);
        history.saveState(current, "Edit");
        history.saveState(current, "Edit");
        history.saveState(current, "Edit");
        check(history.undoDepth() == 1,
              "saving an unchanged project does not add a second undo step (got " +
              std::to_string(history.undoDepth()) + ")");
    }

    // ---- A new action clears the redo stack ------------------------------
    {
        UndoHistory history;
        Project current = projectWith(1, 120.0f);
        history.saveState(current, "One");
        current = projectWith(2, 120.0f);
        history.undo(current);
        check(history.canRedo(), "redo is available after an undo");

        history.saveState(projectWith(3, 120.0f), "Three");
        check(!history.canRedo(),
              "a new edit after an undo clears the redo stack - redoing onto a "
              "diverged timeline would restore notes the user did not make");
    }

    // ---- The history is capped -------------------------------------------
    {
        UndoHistory history;
        for (size_t i = 0; i < UndoHistory::MAX_ENTRIES + 25; ++i) {
            history.saveState(projectWith(int(i % 7) + 1, 100.0f + float(i)), "Edit");
        }
        check(history.undoDepth() == UndoHistory::MAX_ENTRIES,
              "history is capped at " + std::to_string(UndoHistory::MAX_ENTRIES) +
              " (got " + std::to_string(history.undoDepth()) + ")");

        // And undoing all the way out must not run off the end
        Project current = projectWith(4, 120.0f);
        for (size_t i = 0; i < UndoHistory::MAX_ENTRIES + 25; ++i) {
            history.undo(current);
        }
        check(!history.canUndo(), "undoing past the start leaves the stack empty");
        check(true, "undoing past the start did not crash");
    }

    // ---- Round trip through many steps preserves content ------------------
    {
        UndoHistory history;
        for (int i = 1; i <= 10; ++i) history.saveState(projectWith(i, 120.0f), "Edit");

        Project current = projectWith(11, 120.0f);
        for (int i = 0; i < 10; ++i) history.undo(current);
        check(current.patterns[0].notes.size() == 1,
              "ten undos land on the first saved state (got " +
              std::to_string(current.patterns[0].notes.size()) + ")");

        for (int i = 0; i < 10; ++i) history.redo(current);
        check(current.patterns[0].notes.size() == 11,
              "ten redos return to where we started (got " +
              std::to_string(current.patterns[0].notes.size()) + ")");
    }

    // ---- clear() ----------------------------------------------------------
    {
        UndoHistory history;
        Project current = projectWith(2, 120.0f);
        history.saveState(current, "Edit");
        history.undo(current);
        history.clear();
        check(!history.canUndo() && !history.canRedo(), "clear() empties both stacks");
    }
}

// ============================================================================
// 15. Mute, solo and per-channel routing
// ============================================================================
static void testMuteSoloRouting() {
    beginTest("Mute, solo and channel routing");

    auto renderProject = [](void (*configure)(Project&)) {
        Project p;
        Pattern pattern;
        for (int ch = 0; ch < 4; ++ch) {
            Note n;
            n.pitch = 48 + ch * 5;
            n.startTime = 0.0f;
            n.duration = 4.0f;
            n.velocity = 1.0f;
            n.oscillatorType = OscillatorType::Sawtooth;
            pattern.notes.push_back(n);
        }
        p.patterns.clear();
        p.patterns.push_back(pattern);
        p.arrangement.clear();
        for (int ch = 0; ch < 4; ++ch) {
            p.arrangement.push_back(Clip{0, ch, 0.0f, 4.0f, 0});
        }

        if (configure) configure(p);

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.play();

        std::vector<float> out;
        std::vector<float> l(512), r(512);
        for (int b = 0; b < 40; ++b) {
            seq.process(l.data(), r.data(), 512);
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    };

    BufferStats normal = analyze(renderProject(nullptr));
    check(!normal.allZero, "the baseline mix is audible");

    // Muting every channel must produce silence
    BufferStats allMuted = analyze(renderProject([](Project& p) {
        for (auto& c : p.channels) c.muted = true;
    }));
    check(allMuted.peak < 1e-4f,
          "muting every channel silences the mix (peak " +
          std::to_string(allMuted.peak) + ")");

    // Muting one channel must reduce, not silence
    BufferStats oneMuted = analyze(renderProject([](Project& p) {
        p.channels[0].muted = true;
    }));
    check(!oneMuted.allZero, "muting one channel leaves the others audible");
    check(oneMuted.rms < normal.rms,
          "muting one channel reduces the level (" + std::to_string(normal.rms) +
          " -> " + std::to_string(oneMuted.rms) + ")");

    // Solo must isolate
    BufferStats soloed = analyze(renderProject([](Project& p) {
        p.channels[1].solo = true;
    }));
    check(!soloed.allZero, "a soloed channel is audible");
    check(soloed.rms < normal.rms,
          "solo removes the other channels (" + std::to_string(normal.rms) +
          " -> " + std::to_string(soloed.rms) + ")");

    // Solo beats mute on a different channel
    BufferStats soloAndMute = analyze(renderProject([](Project& p) {
        p.channels[1].solo = true;
        p.channels[2].muted = true;
    }));
    check(!soloAndMute.allZero,
          "soloing one channel while muting another still produces audio");

    // Zero master volume is silence
    BufferStats noMaster = analyze(renderProject([](Project& p) {
        p.masterVolume = 0.0f;
    }));
    check(noMaster.peak < 1e-4f, "zero master volume silences the mix");

    // Hard pan puts the signal on one side only
    {
        Project p;
        Pattern pattern;
        Note n;
        n.pitch = 60; n.duration = 4.0f; n.velocity = 1.0f;
        n.oscillatorType = OscillatorType::Sawtooth;
        pattern.notes.push_back(n);
        p.patterns.clear();
        p.patterns.push_back(pattern);
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p.channels[0].pan = -1.0f;   // hard left

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.play();

        std::vector<float> left, right;
        std::vector<float> l(512), r(512);
        for (int b = 0; b < 30; ++b) {
            seq.process(l.data(), r.data(), 512);
            left.insert(left.end(), l.begin(), l.end());
            right.insert(right.end(), r.begin(), r.end());
        }
        BufferStats ls = analyze(left);
        BufferStats rs = analyze(right);
        check(ls.rms > rs.rms * 4.0f,
              "hard-left pan puts the signal on the left (L rms " +
              std::to_string(ls.rms) + ", R rms " + std::to_string(rs.rms) + ")");
    }
}

// ============================================================================
// 16. Live playing - trigger, release, preview
// ============================================================================
static void testLivePlaying() {
    beginTest("Live note triggering");

    Project p;
    auto seqPtr = std::make_unique<Sequencer>();
    Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&p);

    std::vector<float> l(512), r(512);

    // Triggering a note with the transport stopped must still sound - that
    // is how the on-screen keyboard and the pad controller work.
    seq.triggerNote(0, 60, 0.9f);
    std::vector<float> out;
    for (int b = 0; b < 20; ++b) {
        seq.process(l.data(), r.data(), 512);
        out.insert(out.end(), l.begin(), l.end());
    }
    BufferStats st = analyze(out);
    check(!st.allZero, "a triggered note sounds with the transport stopped");
    check(st.nonFiniteCount == 0, "a triggered note produces finite audio");

    // Releasing it, and releasing things that were never pressed
    seq.releaseNote(0, 60);
    seq.releaseNote(0, 61);          // never triggered
    seq.releaseNote(99, 60);         // channel out of range
    seq.releaseNote(-1, 60);
    seq.triggerNote(99, 60, 1.0f);   // channel out of range
    seq.triggerNote(-1, 60, 1.0f);
    seq.triggerNote(0, 200, 1.0f);   // pitch out of range
    seq.triggerNote(0, -5, 1.0f);
    for (int b = 0; b < 10; ++b) seq.process(l.data(), r.data(), 512);
    check(analyze(l).nonFiniteCount == 0,
          "out-of-range triggers and releases do not corrupt the output");

    // Preview covers every oscillator, including the drums
    for (int i = 0; i < OSC_COUNT; ++i) {
        seq.previewNote(60, 0.8f, static_cast<OscillatorType>(i), 0.1f);
    }
    for (int b = 0; b < 20; ++b) seq.process(l.data(), r.data(), 512);
    BufferStats preview = analyze(l);
    check(preview.nonFiniteCount == 0,
          "previewing every oscillator in one frame produces finite audio");
    check(preview.outOfRangeCount == 0,
          "previewing every oscillator stays within headroom (peak " +
          std::to_string(preview.peak) + ")");

    // Holding many notes then releasing them all.
    // stop() first: the out-of-range triggers above are still held, and a
    // sustained voice is correct behaviour rather than something to assert
    // away here.
    seq.stop();
    for (int b = 0; b < 30; ++b) seq.process(l.data(), r.data(), 512);

    for (int note = 36; note < 96; ++note) seq.triggerNote(note % 8, note, 1.0f);
    for (int b = 0; b < 10; ++b) seq.process(l.data(), r.data(), 512);
    for (int note = 36; note < 96; ++note) seq.releaseNote(note % 8, note);
    for (int b = 0; b < 40; ++b) seq.process(l.data(), r.data(), 512);
    BufferStats tail = analyze(l);
    check(tail.nonFiniteCount == 0, "a large chord releases cleanly");
    check(tail.peak < 1e-3f,
          "everything decays to silence after release (peak " +
          std::to_string(tail.peak) + ")");
}

// ============================================================================
// 17. Wavetables
// ============================================================================
static void testWavetables() {
    beginTest("Wavetable lookup and morphing");

    // A fresh bank is seeded with three usable waveforms by its constructor,
    // so a new bank is immediately useful. Worth asserting: code below relies
    // on it, and silently losing it would be a regression.
    {
        WavetableBank fresh;
        check(fresh.tables.size() == 3,
              "a new bank starts with three default waveforms (got " +
              std::to_string(fresh.tables.size()) + ")");
        check(fresh.name == "Default", "a new bank is named Default");
        check(std::isfinite(fresh.lookupMorph(0.25f, 0.5f)),
              "the default bank morphs to a finite value");
    }

    // An emptied bank must be safe to look up
    {
        WavetableBank bank;
        bank.tables.clear();
        check(bank.lookupMorph(0.0f, 0.0f) == 0.0f, "an empty bank reads as zero");
        check(bank.lookupMorph(0.5f, 1.0f) == 0.0f,
              "an empty bank reads as zero at any morph position");
        check(std::isfinite(bank.lookupMorph(-5.0f, -5.0f)),
              "an empty bank survives out-of-range arguments");
    }

    // One table: morphing has nothing to morph to, but must still read it
    {
        WavetableBank bank;
        bank.tables.clear();
        Wavetable sine;
        for (int i = 0; i < Wavetable::TABLE_SIZE; ++i) {
            sine.samples[i] = std::sin(2.0f * PI * float(i) / float(Wavetable::TABLE_SIZE));
        }
        bank.addTable(sine);

        check(std::fabs(bank.lookupMorph(0.25f, 0.0f) - 1.0f) < 0.05f,
              "a single sine table reads +1 at a quarter phase");
        check(std::fabs(bank.lookupMorph(0.25f, 1.0f) - 1.0f) < 0.05f,
              "morph position is ignored when there is only one table");
    }

    // Two tables: morphing must interpolate between them
    {
        WavetableBank bank;
        bank.tables.clear();
        Wavetable low, high;
        for (int i = 0; i < Wavetable::TABLE_SIZE; ++i) {
            low.samples[i] = -1.0f;
            high.samples[i] = 1.0f;
        }
        bank.addTable(low);
        bank.addTable(high);

        check(std::fabs(bank.lookupMorph(0.5f, 0.0f) - (-1.0f)) < 0.01f,
              "morph 0 reads the first table");
        check(std::fabs(bank.lookupMorph(0.5f, 1.0f) - 1.0f) < 0.01f,
              "morph 1 reads the last table");
        check(std::fabs(bank.lookupMorph(0.5f, 0.5f)) < 0.05f,
              "morph 0.5 sits between the two (got " +
              std::to_string(bank.lookupMorph(0.5f, 0.5f)) + ")");

        // Out-of-range morph and phase must not read out of bounds
        for (float morph : {-2.0f, 2.0f, 100.0f}) {
            for (float phase : {-1.0f, 1.5f, 99.0f}) {
                check(std::isfinite(bank.lookupMorph(phase, morph)),
                      "out-of-range lookup stays finite");
            }
        }
    }

    // The bank is capped
    {
        WavetableBank bank;
        bank.tables.clear();
        Wavetable table;
        for (int i = 0; i < WavetableBank::MAX_TABLES + 10; ++i) bank.addTable(table);
        check(bank.tables.size() == static_cast<size_t>(WavetableBank::MAX_TABLES),
              "the bank stops at " + std::to_string(WavetableBank::MAX_TABLES) +
              " tables (got " + std::to_string(bank.tables.size()) + ")");
    }
}

// ============================================================================
// 18. Long-run stability
// ============================================================================
static void testLongRunStability() {
    beginTest("Long-run stability");

    // Sixty seconds of a busy, effect-heavy, looping project. Slow drifts -
    // an integrator winding up, a filter state creeping, a denormal storm -
    // do not show up in a one-second test.
    Project p;
    p.bpm = 174.0f;
    p.masterLimiterEnabled = true;
    p.masterCompressorEnabled = true;
    p.masterEQEnabled = true;

    Pattern pattern;
    for (int i = 0; i < 32; ++i) {
        Note n;
        n.pitch = 36 + (i * 7) % 60;
        n.startTime = float(i) * 0.25f;
        n.duration = 0.4f;
        n.velocity = 0.7f + 0.3f * float(i % 3) / 2.0f;
        n.oscillatorType = static_cast<OscillatorType>(i % OSC_COUNT);
        n.vibrato = 0.5f;
        n.arpeggio = (i % 2) ? 0x47 : 0;
        pattern.notes.push_back(n);
    }
    p.patterns.clear();
    p.patterns.push_back(pattern);
    p.arrangement.clear();
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        p.arrangement.push_back(Clip{0, ch, 0.0f, 8.0f, 0});
        ChannelConfig& c = p.channels[ch];
        c.reverbEnabled = c.delayEnabled = c.chorusEnabled = true;
        c.filterEnabled = c.compressorEnabled = c.eqEnabled = true;
        c.delayFeedback = 0.6f;
        c.reverbMix = 0.4f;
    }

    auto seqPtr = std::make_unique<Sequencer>();
    Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&p);
    seq.setLoop(true, 0.0f, 8.0f);
    seq.play();

    const int blocks = (44100 * 60) / 512;   // 60 seconds
    float earlyRms = 0.0f, lateRms = 0.0f;
    int nonFinite = 0;
    float peak = 0.0f;

    std::vector<float> l(512), r(512);
    for (int b = 0; b < blocks; ++b) {
        seq.process(l.data(), r.data(), 512);
        BufferStats st = analyze(l);
        nonFinite += st.nonFiniteCount;
        peak = std::max(peak, st.peak);
        if (b == 100) earlyRms = st.rms;
        if (b == blocks - 100) lateRms = st.rms;
    }

    check(nonFinite == 0,
          "sixty seconds of playback produced no non-finite samples (" +
          std::to_string(nonFinite) + " found)");
    check(peak <= 1.05f,
          "the limiter held the output over sixty seconds (peak " +
          std::to_string(peak) + ")");
    check(earlyRms > 1e-4f && lateRms > 1e-4f,
          "audio is still present at the end of the run (early rms " +
          std::to_string(earlyRms) + ", late rms " + std::to_string(lateRms) + ")");

    // A loop that has run for a minute should sound roughly like it did at
    // the start. A large drift means state is accumulating somewhere.
    if (earlyRms > 1e-4f) {
        const float ratio = lateRms / earlyRms;
        check(ratio > 0.25f && ratio < 4.0f,
              "level has not drifted over sixty seconds (late/early = " +
              std::to_string(ratio) + ")");
    }

    // Position must still be inside the loop, not run off to infinity
    check(std::isfinite(seq.getCurrentBeat()) && seq.getCurrentBeat() >= 0.0f &&
          seq.getCurrentBeat() <= 8.5f,
          "the playhead is still inside the loop (beat " +
          std::to_string(seq.getCurrentBeat()) + ")");
}

// ============================================================================
// 19. The master bus does not destroy the mix at its default settings
// ============================================================================
static void testMasterBusUnits() {
    beginTest("Master bus unit conversions");

    // The Project stores master EQ and compressor values in decibels; the
    // DSP wants linear gain. Handing dB straight through meant enabling the
    // master EQ at its default of 0 dB multiplied the mix by zero and
    // silenced the entire song. Turning an effect ON at its defaults must be
    // close to a no-op, never a mute.

    auto renderWith = [](void (*configure)(Project&)) {
        Project p;
        Pattern pattern;
        for (int i = 0; i < 8; ++i) {
            Note n;
            n.pitch = 48 + i * 3;
            n.startTime = float(i) * 0.25f;
            n.duration = 0.5f;
            n.velocity = 0.9f;
            n.oscillatorType = OscillatorType::Sawtooth;
            pattern.notes.push_back(n);
        }
        p.patterns.clear();
        p.patterns.push_back(pattern);
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        if (configure) configure(p);

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.play();

        std::vector<float> out;
        std::vector<float> l(512), r(512);
        for (int b = 0; b < 60; ++b) {
            seq.process(l.data(), r.data(), 512);
            out.insert(out.end(), l.begin(), l.end());
        }
        return analyze(out);
    };

    BufferStats bypassed = renderWith(nullptr);
    check(!bypassed.allZero, "the baseline mix with no master effects is audible");

    struct Case { const char* label; void (*configure)(Project&); };
    const Case cases[] = {
        {"master EQ at defaults", [](Project& p) { p.masterEQEnabled = true; }},
        {"master compressor at defaults", [](Project& p) { p.masterCompressorEnabled = true; }},
        {"master limiter at defaults", [](Project& p) { p.masterLimiterEnabled = true; }},
        {"the whole master chain at defaults", [](Project& p) {
            p.masterEQEnabled = true;
            p.masterCompressorEnabled = true;
            p.masterLimiterEnabled = true; }},
    };

    for (const Case& c : cases) {
        BufferStats st = renderWith(c.configure);
        check(st.nonFiniteCount == 0,
              std::string(c.label) + " produced non-finite audio");
        check(!st.allZero,
              std::string(c.label) + " silenced the mix - check dB vs linear units");

        // "Close to a no-op" - within about 12 dB either way. A limiter and a
        // compressor legitimately move the level; a unit mismatch moves it by
        // orders of magnitude or to zero.
        if (bypassed.rms > 1e-5f) {
            const float ratio = st.rms / bypassed.rms;
            check(ratio > 0.25f && ratio < 4.0f,
                  std::string(c.label) + " changed the level by " +
                  std::to_string(ratio) + "x - a unit mismatch, not mastering");
        }
    }

    // A deliberate boost must actually boost, and a cut must cut. This is
    // what proves the conversion runs in the right direction.
    BufferStats boosted = renderWith([](Project& p) {
        p.masterEQEnabled = true;
        p.masterEQLowGain = 9.0f;
        p.masterEQMidGain = 9.0f;
        p.masterEQHighGain = 9.0f;
        p.masterLimiterEnabled = false;
    });
    BufferStats cut = renderWith([](Project& p) {
        p.masterEQEnabled = true;
        p.masterEQLowGain = -9.0f;
        p.masterEQMidGain = -9.0f;
        p.masterEQHighGain = -9.0f;
        p.masterLimiterEnabled = false;
    });
    check(boosted.rms > cut.rms,
          "a +9 dB master EQ is louder than a -9 dB one (" +
          std::to_string(boosted.rms) + " vs " + std::to_string(cut.rms) + ")");
    check(!cut.allZero, "a -9 dB master EQ attenuates rather than silencing");
}

// ============================================================================
// 20. Validation covers every field it claims to
// ============================================================================
static void testValidationRepairsEverything() {
    beginTest("Validation repairs every field");

    // Fill a project with the worst values a corrupt file could contain and
    // assert every one comes back usable.
    Project p;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    p.bpm = nan;
    p.beatsPerMeasure = -99;
    p.masterVolume = inf;
    p.songLength = -1e30f;
    p.swing = 50.0f;
    p.swingGrid = nan;
    p.humanizeAmount = -3.0f;
    p.masterEQLowGain = inf;
    p.masterCompRatio = -5.0f;
    p.masterLimiterCeiling = 100.0f;

    for (ChannelConfig& c : p.channels) {
        c.volume = nan;
        c.pan = -50.0f;
        c.filterCutoff = -1.0f;
        c.filterResonance = inf;
        c.delayFeedback = 2.0f;       // would self-oscillate
        c.flangerFeedback = -5.0f;
        c.phaserFeedback = 3.0f;
        c.compThreshold = -10.0f;     // linear field given a dB value
        c.compRatio = 0.01f;
        c.eqLow = nan;
        c.formantResonance = -1.0f;
        c.tapeDrive = inf;
        c.sidechainSource = 500;
        c.envelope.attack = nan;
        c.envelope.sustain = 9.0f;
        c.oscillator.pulseWidth = 0.0f;
    }

    Pattern pattern;
    Note bad;
    bad.pitch = 9999;
    bad.velocity = nan;
    bad.startTime = -1e20f;
    bad.duration = inf;
    bad.vibrato = -50.0f;
    bad.slide = 1e9f;
    bad.sweepAmount = nan;
    bad.echoRepeats = -20;
    bad.retriggerSpeed = 0.0f;
    bad.tremoloSpeed = inf;
    pattern.notes.push_back(bad);
    pattern.length = -40;
    p.patterns.clear();
    p.patterns.push_back(pattern);

    p.arrangement.clear();
    p.arrangement.push_back(Clip{0, 0, nan, inf, 0});
    p.arrangement.push_back(Clip{77, 0, 0.0f, 4.0f, 0});   // dropped: bad pattern
    p.arrangement.push_back(Clip{0, 77, 0.0f, 4.0f, 0});   // dropped: bad channel

    clampProjectToValidRanges(p);

    check(std::isfinite(p.bpm) && p.bpm >= ProjectLimits::MIN_BPM, "BPM repaired");
    check(p.beatsPerMeasure >= 1, "time signature repaired");
    check(std::isfinite(p.masterVolume) && p.masterVolume <= 2.0f, "master volume repaired");
    check(std::isfinite(p.songLength) && p.songLength > 0.0f, "song length repaired");
    check(p.swing >= 0.0f && p.swing <= 1.0f, "swing repaired");
    check(std::isfinite(p.swingGrid) && p.swingGrid > 0.0f, "swing grid repaired");

    for (const ChannelConfig& c : p.channels) {
        check(std::isfinite(c.volume), "channel volume repaired");
        check(c.pan >= -1.0f && c.pan <= 1.0f, "channel pan repaired");
        check(c.filterCutoff >= 20.0f, "filter cutoff repaired");
        check(std::isfinite(c.filterResonance), "filter resonance repaired");
        check(c.delayFeedback < 1.0f,
              "delay feedback held below unity so it cannot self-oscillate");
        check(std::fabs(c.flangerFeedback) < 1.0f, "flanger feedback held below unity");
        check(std::fabs(c.phaserFeedback) < 1.0f, "phaser feedback held below unity");
        check(c.compThreshold > 0.0f,
              "compressor threshold repaired to a positive linear level");
        check(c.compRatio >= 1.0f, "compressor ratio repaired");
        check(std::isfinite(c.eqLow) && c.eqLow > 0.0f,
              "EQ gain repaired to a usable multiplier, not silence");
        check(c.formantResonance > 0.0f, "formant resonance repaired");
        check(std::isfinite(c.tapeDrive), "tape drive repaired");
        check(c.sidechainSource >= -1 && c.sidechainSource < Project::MAX_CHANNELS,
              "sidechain source repaired");
        check(std::isfinite(c.envelope.attack), "envelope attack repaired");
        check(c.envelope.sustain <= 1.0f, "envelope sustain repaired");
        check(c.oscillator.pulseWidth > 0.0f, "pulse width repaired");
    }

    check(p.patterns[0].length > 0, "pattern length repaired");
    const Note& n = p.patterns[0].notes[0];
    check(n.pitch >= 0 && n.pitch <= 127, "note pitch repaired");
    check(std::isfinite(n.velocity), "note velocity repaired");
    check(std::isfinite(n.startTime) && n.startTime >= 0.0f, "note start repaired");
    check(std::isfinite(n.duration) && n.duration > 0.0f, "note duration repaired");
    check(std::isfinite(n.slide) && std::fabs(n.slide) <= 96.0f, "note slide repaired");
    check(std::isfinite(n.sweepAmount), "note sweep repaired");
    check(n.echoRepeats >= 0, "note echo repeats repaired");
    check(n.retriggerSpeed > 0.0f, "note retrigger speed repaired");
    check(std::isfinite(n.tremoloSpeed), "note tremolo speed repaired");

    check(p.arrangement.size() == 1,
          "clips pointing at a missing pattern or channel are dropped, not "
          "silently retargeted (kept " + std::to_string(p.arrangement.size()) + " of 3)");
    if (!p.arrangement.empty()) {
        check(std::isfinite(p.arrangement[0].startBeat), "clip start repaired");
        check(std::isfinite(p.arrangement[0].lengthBeats) &&
              p.arrangement[0].lengthBeats > 0.0f, "clip length repaired");
    }

    // And the repaired project must render
    auto seqPtr = std::make_unique<Sequencer>();
    Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&p);
    seq.play();
    std::vector<float> l(512), r(512);
    for (int b = 0; b < 30; ++b) seq.process(l.data(), r.data(), 512);
    check(analyze(l).nonFiniteCount == 0, "the repaired project renders finite audio");
}

// ============================================================================
// 21. Spectrum analyzer
// ============================================================================
static void testSpectrumAnalyzer() {
    beginTest("Spectrum analyzer");

    const float sampleRate = 44100.0f;

    auto feed = [&](SpectrumAnalyzer& fft, float frequency, float amplitude, int samples) {
        for (int i = 0; i < samples; ++i) {
            const float t = float(i) / sampleRate;
            const float v = (frequency > 0.0f)
                ? amplitude * std::sin(2.0f * PI * frequency * t)
                : 0.0f;
            fft.process(v, v);
        }
        fft.update();
    };

    // ---- A pure tone lands in the right bin -----------------------------
    {
        auto fftPtr = std::make_unique<SpectrumAnalyzer>();
        SpectrumAnalyzer& fft = *fftPtr;
        fft.setSampleRate(sampleRate);

        const float tone = 1000.0f;
        feed(fft, tone, 0.9f, SpectrumAnalyzer::FFT_SIZE * 3);

        const float peak = fft.getPeakFrequency();
        const float binWidth = sampleRate / float(SpectrumAnalyzer::FFT_SIZE);
        check(std::fabs(peak - tone) <= binWidth * 2.0f,
              "a 1kHz tone peaks at 1kHz (got " + std::to_string(peak) +
              " Hz, bin width " + std::to_string(binWidth) + ")");
    }

    // ---- The display does not saturate ----------------------------------
    //
    // This is the regression test for the original bug: the FFT output was
    // never normalised, so a 2048-point transform of any audible signal
    // pinned every bin to maximum and the analyzer was a solid wall.
    {
        auto fftPtr = std::make_unique<SpectrumAnalyzer>();
        SpectrumAnalyzer& fft = *fftPtr;
        fft.setSampleRate(sampleRate);
        feed(fft, 1000.0f, 0.9f, SpectrumAnalyzer::FFT_SIZE * 3);

        const std::vector<float>& mags = fft.magnitudes();
        int saturated = 0, quiet = 0;
        for (float m : mags) {
            if (m >= 0.99f) ++saturated;
            if (m <= 0.10f) ++quiet;
        }

        check(saturated < SpectrumAnalyzer::NUM_BINS / 20,
              "a single tone lights up few bins, not the whole spectrum (" +
              std::to_string(saturated) + " of " +
              std::to_string(SpectrumAnalyzer::NUM_BINS) + " at maximum)");
        check(quiet > SpectrumAnalyzer::NUM_BINS / 2,
              "most of the spectrum is quiet for a single tone (" +
              std::to_string(quiet) + " near zero)");
    }

    // ---- Level tracks amplitude ------------------------------------------
    {
        auto loudPtr = std::make_unique<SpectrumAnalyzer>();
        auto softPtr = std::make_unique<SpectrumAnalyzer>();
        loudPtr->setSampleRate(sampleRate);
        softPtr->setSampleRate(sampleRate);

        feed(*loudPtr, 1000.0f, 0.9f, SpectrumAnalyzer::FFT_SIZE * 3);
        feed(*softPtr, 1000.0f, 0.05f, SpectrumAnalyzer::FFT_SIZE * 3);

        const int bin = loudPtr->frequencyToBin(1000.0f);
        const float loud = loudPtr->getMagnitude(bin);
        const float soft = softPtr->getMagnitude(bin);

        check(loud > soft,
              "a louder tone reads higher than a quiet one (" +
              std::to_string(loud) + " vs " + std::to_string(soft) + ")");
        check(loud > 0.5f,
              "a near-full-scale tone reads high on the display (" +
              std::to_string(loud) + ")");
        check(soft < loud - 0.1f,
              "a -26dB tone is clearly lower, not clamped to the same value");
    }

    // ---- Silence reads as silence ----------------------------------------
    {
        auto fftPtr = std::make_unique<SpectrumAnalyzer>();
        SpectrumAnalyzer& fft = *fftPtr;
        fft.setSampleRate(sampleRate);
        feed(fft, 0.0f, 0.0f, SpectrumAnalyzer::FFT_SIZE * 3);

        float peak = 0.0f;
        for (float m : fft.magnitudes()) peak = std::max(peak, m);
        check(peak < 0.02f,
              "silence produces an empty spectrum (peak " +
              std::to_string(peak) + ")");
    }

    // ---- Robustness -------------------------------------------------------
    {
        auto fftPtr = std::make_unique<SpectrumAnalyzer>();
        SpectrumAnalyzer& fft = *fftPtr;
        fft.setSampleRate(sampleRate);

        // Non-finite input from a broken effect must not poison the display
        for (int i = 0; i < SpectrumAnalyzer::FFT_SIZE * 2; ++i) {
            fft.process(std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity());
        }
        fft.update();
        bool allFinite = true;
        for (float m : fft.magnitudes()) {
            if (!std::isfinite(m)) allFinite = false;
        }
        check(allFinite, "non-finite audio does not produce non-finite magnitudes");

        // Reading before enough audio has arrived must be safe
        auto freshPtr = std::make_unique<SpectrumAnalyzer>();
        freshPtr->setSampleRate(sampleRate);
        freshPtr->update();
        check(freshPtr->getMagnitude(0) == 0.0f, "a fresh analyzer reads zero");
        check(freshPtr->getMagnitude(-1) == 0.0f && freshPtr->getMagnitude(99999) == 0.0f,
              "out-of-range bin queries are safe");

        // A sample rate of zero must not divide by zero
        auto zeroPtr = std::make_unique<SpectrumAnalyzer>();
        zeroPtr->setSampleRate(0.0f);
        for (int i = 0; i < SpectrumAnalyzer::FFT_SIZE * 2; ++i) zeroPtr->process(0.5f, 0.5f);
        zeroPtr->update();
        check(std::isfinite(zeroPtr->getPeakFrequency()),
              "a zero sample rate is repaired rather than dividing by zero");
    }
}

// ============================================================================
// 22. Noise generator
// ============================================================================
static void testNoiseGenerator() {
    beginTest("Noise generator");

    const float sampleRate = 44100.0f;
    const int frames = int(sampleRate * 0.4f);

    auto renderNoise = [&](bool shortMode, int period, int pitch) {
        ChannelConfig config;
        config.oscillator.type = OscillatorType::Noise;
        config.oscillator.noiseShortMode = shortMode;
        config.oscillator.noisePeriod = period;

        Synthesizer synth;
        synth.setSampleRate(sampleRate);
        synth.setChannelConfig(config);
        synth.noteOn(pitch, 1.0f, 0.0f, 0.0f, 0.0f, 0.35f, OscillatorType::Noise);

        std::vector<float> buf(frames);
        for (int i = 0; i < frames; ++i) buf[i] = synth.process(float(i) / sampleRate);
        return buf;
    };

    // Counts sign changes: a rough measure of how bright the noise is, and
    // enough to tell the sixteen periods apart without an FFT.
    auto zeroCrossings = [](const std::vector<float>& buf) {
        int crossings = 0;
        for (size_t i = 1; i < buf.size(); ++i) {
            if ((buf[i - 1] <= 0.0f) != (buf[i] <= 0.0f)) ++crossings;
        }
        return crossings;
    };

    // ---- Basic sanity across every period --------------------------------
    for (int period = -1; period < 16; ++period) {
        std::vector<float> buf = renderNoise(false, period, 60);
        BufferStats st = analyze(buf);
        const std::string at = "noise period " + std::to_string(period);
        check(st.nonFiniteCount == 0, at + " produced non-finite audio");
        check(st.outOfRangeCount == 0, at + " exceeded headroom");
        check(!st.allZero, at + " produced silence");
    }

    // ---- The periods are actually different pitches ----------------------
    //
    // Real hardware offers sixteen fixed noise rates rather than a sweep,
    // and that stepping is most of what makes NES noise sound like NES
    // noise. A low period index must be audibly brighter than a high one.
    {
        const int bright = zeroCrossings(renderNoise(false, 2, 60));
        const int mid    = zeroCrossings(renderNoise(false, 8, 60));
        const int dark   = zeroCrossings(renderNoise(false, 15, 60));

        check(bright > mid && mid > dark,
              "the noise periods step from bright to dark (" +
              std::to_string(bright) + " > " + std::to_string(mid) + " > " +
              std::to_string(dark) + " crossings)");
    }

    // ---- Short mode is periodic, normal mode is not ----------------------
    //
    // The 2A03's short mode repeats every 93 steps, which the ear hears as a
    // metallic tone rather than as hiss. Its period is tiny compared with
    // the 32767 of normal mode, so the waveform repeats within a short
    // window and normal mode does not.
    {
        std::vector<float> shortNoise = renderNoise(true, 4, 60);
        std::vector<float> whiteNoise = renderNoise(false, 4, 60);

        auto repeatsWithin = [](const std::vector<float>& buf, int lag) {
            if (int(buf.size()) < lag * 4) return 0.0f;
            int matches = 0, total = 0;
            for (int i = lag; i < int(buf.size()); ++i) {
                if ((buf[i] > 0.0f) == (buf[i - lag] > 0.0f)) ++matches;
                ++total;
            }
            return total ? float(matches) / float(total) : 0.0f;
        };

        // Best agreement found over plausible short-mode repeat lengths
        float bestShort = 0.0f, bestWhite = 0.0f;
        for (int lag = 60; lag <= 400; ++lag) {
            bestShort = std::max(bestShort, repeatsWithin(shortNoise, lag));
            bestWhite = std::max(bestWhite, repeatsWithin(whiteNoise, lag));
        }

        check(bestShort > bestWhite,
              "short mode repeats more than normal mode (" +
              std::to_string(bestShort) + " vs " + std::to_string(bestWhite) + ")");
        check(bestShort > 0.9f,
              "short mode is strongly periodic (best agreement " +
              std::to_string(bestShort) + ")");
    }

    // ---- Voices do not share state ---------------------------------------
    //
    // The LFSR clock accumulator used to be a function-local static, so
    // every voice on every channel advanced one shared counter. Two
    // simultaneous noise notes must not be the same signal as one.
    {
        Synthesizer one;
        one.setSampleRate(sampleRate);
        ChannelConfig config;
        config.oscillator.type = OscillatorType::Noise;
        one.setChannelConfig(config);
        one.noteOn(60, 1.0f, 0.0f, 0.0f, 0.0f, 0.35f, OscillatorType::Noise);

        std::vector<float> single(frames);
        for (int i = 0; i < frames; ++i) single[i] = one.process(float(i) / sampleRate);

        Synthesizer many;
        many.setSampleRate(sampleRate);
        many.setChannelConfig(config);
        for (int n = 0; n < 4; ++n) {
            many.noteOn(48 + n * 7, 1.0f, 0.0f, 0.0f, 0.0f, 0.35f, OscillatorType::Noise);
        }
        std::vector<float> stacked(frames);
        for (int i = 0; i < frames; ++i) stacked[i] = many.process(float(i) / sampleRate);

        BufferStats singleStats = analyze(single);
        BufferStats stackedStats = analyze(stacked);
        check(stackedStats.nonFiniteCount == 0, "stacked noise voices stay finite");
        check(stackedStats.rms > singleStats.rms * 1.2f,
              "four noise voices are louder than one - they no longer share "
              "a single clock (" + std::to_string(singleStats.rms) + " -> " +
              std::to_string(stackedStats.rms) + ")");
    }

    // ---- The noise period survives save and load ---------------------------
    {
        Project p;
        p.channels[0].oscillator.type = OscillatorType::Noise;
        p.channels[0].oscillator.noisePeriod = 11;
        p.channels[0].oscillator.noiseShortMode = true;

        const std::string path = testPath("test_noise.ctp");
        check(saveProject(p, path), "a project with a noise period saves");

        Project reloaded;
        check(loadProject(reloaded, path), "it loads back");
        check(reloaded.channels[0].oscillator.noisePeriod == 11,
              "noise period survived (got " +
              std::to_string(reloaded.channels[0].oscillator.noisePeriod) + ")");
        check(reloaded.channels[0].oscillator.noiseShortMode,
              "short mode flag survived");

        std::remove(path.c_str());
    }

    // ---- Out-of-range periods are repaired --------------------------------
    {
        Project p;
        p.channels[0].oscillator.noisePeriod = 99;
        p.channels[1].oscillator.noisePeriod = -50;
        clampProjectToValidRanges(p);
        check(p.channels[0].oscillator.noisePeriod == -1,
              "a period above 15 falls back to tracking the note");
        check(p.channels[1].oscillator.noisePeriod == -1,
              "a period below -1 falls back to tracking the note");
    }
}

// ============================================================================
// 23. Euclidean rhythm generator
// ============================================================================
static void testEuclideanGenerator() {
    beginTest("Euclidean rhythm generator");

    using namespace generators;

    auto countHits = [](const std::vector<bool>& p) {
        return int(std::count(p.begin(), p.end(), true));
    };

    // ---- The classics come out right -------------------------------------
    //
    // E(3,8) is the tresillo. Bjorklund's algorithm has one canonical
    // answer for each (n,k), so these are checkable facts rather than taste.
    {
        const std::vector<bool> tresillo = euclideanPattern(8, 3);
        check(tresillo.size() == 8, "E(3,8) has eight steps");
        check(countHits(tresillo) == 3, "E(3,8) has three onsets");

        // The onsets must be as evenly spread as eight over three allows,
        // which means gaps of 3, 3 and 2 in some rotation.
        std::vector<int> gaps;
        int last = -1;
        for (int i = 0; i < 8; ++i) {
            if (tresillo[size_t(i)]) {
                if (last >= 0) gaps.push_back(i - last);
                last = i;
            }
        }
        std::sort(gaps.begin(), gaps.end());
        check(gaps.size() == 2 && gaps[0] >= 2 && gaps[1] <= 3,
              "E(3,8) spaces its onsets evenly (gaps of 2 and 3)");
    }

    // ---- Onset counts are exact for every (n,k) ---------------------------
    {
        int wrong = 0;
        for (int steps = 1; steps <= 32; ++steps) {
            for (int pulses = 0; pulses <= steps; ++pulses) {
                if (countHits(euclideanPattern(steps, pulses)) != pulses) ++wrong;
            }
        }
        check(wrong == 0,
              "every E(k,n) for n up to 32 produces exactly k onsets (" +
              std::to_string(wrong) + " wrong)");
    }

    // ---- Rotation moves without adding or losing onsets --------------------
    {
        const std::vector<bool> base = euclideanPattern(16, 5, 0);
        bool countsHold = true, someMoved = false;
        for (int r = 1; r < 16; ++r) {
            const std::vector<bool> rotated = euclideanPattern(16, 5, r);
            if (countHits(rotated) != 5) countsHold = false;
            if (rotated != base) someMoved = true;
        }
        check(countsHold, "rotation preserves the onset count");
        check(someMoved, "rotation actually changes the pattern");
    }

    // ---- Degenerate input -------------------------------------------------
    {
        check(euclideanPattern(0, 4).empty(), "zero steps gives an empty pattern");
        check(euclideanPattern(-5, 4).empty(), "negative steps gives an empty pattern");
        check(countHits(euclideanPattern(8, 0)) == 0, "zero pulses gives no onsets");
        check(countHits(euclideanPattern(8, 99)) == 8, "more pulses than steps fills it");
        check(countHits(euclideanPattern(8, -3)) == 0, "negative pulses gives no onsets");

        const std::vector<bool> huge = euclideanPattern(8, 3, 1000000);
        check(huge.size() == 8 && countHits(huge) == 3,
              "an absurd rotation still produces a valid pattern");
    }

    // ---- Generated notes are valid and playable ---------------------------
    {
        EuclideanVoice voice;
        voice.instrument = OscillatorType::Kick808;
        voice.steps = 16;
        voice.pulses = 5;
        const std::vector<Note> notes = generateEuclidean(voice, 4.0f);

        check(notes.size() == 5, "five pulses produce five notes (got " +
              std::to_string(notes.size()) + ")");

        bool ordered = true, valid = true;
        for (size_t i = 0; i < notes.size(); ++i) {
            const Note& n = notes[i];
            if (n.pitch < 0 || n.pitch > 127) valid = false;
            if (n.duration <= 0.0f || !std::isfinite(n.duration)) valid = false;
            if (n.startTime < 0.0f || n.startTime >= 4.0f) valid = false;
            if (i > 0 && notes[i - 1].startTime > n.startTime) ordered = false;
        }
        check(valid, "generated notes are inside valid ranges");
        check(ordered, "generated notes come out in time order");

        // Zero length must not divide by zero
        check(generateEuclidean(voice, 0.0f).empty(),
              "a zero-length bar produces no notes rather than dividing by zero");
    }

    // ---- The kit produces a playable pattern -------------------------------
    {
        const std::vector<Note> kit = generateEuclideanKit();
        check(!kit.empty(), "the default kit produces notes");

        bool sorted = true;
        for (size_t i = 1; i < kit.size(); ++i) {
            if (kit[i - 1].startTime > kit[i].startTime) sorted = false;
        }
        check(sorted, "kit notes are sorted by time");

        // Three distinct instruments
        std::vector<OscillatorType> kinds;
        for (const Note& n : kit) {
            if (std::find(kinds.begin(), kinds.end(), n.oscillatorType) == kinds.end()) {
                kinds.push_back(n.oscillatorType);
            }
        }
        check(kinds.size() == 3, "the kit uses three instruments (got " +
              std::to_string(kinds.size()) + ")");

        // And it must actually render
        Project p;
        Pattern pattern;
        pattern.notes = kit;
        p.patterns.clear();
        p.patterns.push_back(pattern);
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.play();

        std::vector<float> out;
        std::vector<float> l(512), r(512);
        for (int b = 0; b < 40; ++b) {
            seq.process(l.data(), r.data(), 512);
            out.insert(out.end(), l.begin(), l.end());
        }
        BufferStats st = analyze(out);
        check(st.nonFiniteCount == 0, "a generated kit renders finite audio");
        check(!st.allZero, "a generated kit is audible");
    }

    // ---- Every preset is valid --------------------------------------------
    {
        int bad = 0;
        for (const EuclideanPreset& preset : euclideanPresets()) {
            const std::vector<bool> p =
                euclideanPattern(preset.steps, preset.pulses, preset.rotation);
            if (int(p.size()) != preset.steps) ++bad;
            if (countHits(p) != preset.pulses) ++bad;
        }
        check(bad == 0, "all " + std::to_string(euclideanPresets().size()) +
              " presets produce their stated pattern");
    }
}

// ============================================================================
// 24. Stem export
// ============================================================================
static void testStemExport() {
    beginTest("Stem export");

    Project p;
    Pattern pattern;
    // Three channels with content, five silent
    for (int ch = 0; ch < 3; ++ch) {
        for (int i = 0; i < 4; ++i) {
            Note n;
            n.pitch = 48 + ch * 7 + i;
            n.startTime = float(i);
            n.duration = 0.8f;
            n.velocity = 0.9f;
            n.oscillatorType = OscillatorType::Sawtooth;
            pattern.notes.push_back(n);
        }
    }
    p.patterns.clear();
    p.patterns.push_back(pattern);
    p.arrangement.clear();
    for (int ch = 0; ch < 3; ++ch) {
        p.arrangement.push_back(Clip{0, ch, 0.0f, 4.0f, 0});
    }
    p.channels[0].name = "Lead";
    p.channels[1].name = "Bass/Sub";      // a name with a path separator in it
    p.channels[2].name = "";              // and an empty one

    // Deliberate mute/solo state that must survive the export
    p.channels[4].muted = true;
    p.channels[5].solo = true;

    auto seqPtr = std::make_unique<Sequencer>();
    Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&p);

    const std::string dir = testPath("stems");
    StemExportResult result = exportStems(p, seq, dir, 4.0f, true);

    check(result.failures.empty(),
          "no stem failed to write (" +
          (result.failures.empty() ? std::string("none")
                                   : result.failures.front()) + ")");
    check(result.written == 3,
          "one stem per channel with content (wrote " +
          std::to_string(result.written) + ", expected 3)");
    check(result.skipped == 5,
          "silent channels are skipped rather than written as empty files (" +
          std::to_string(result.skipped) + ")");

    // Exporting must not remix the project
    check(p.channels[4].muted, "a muted channel is still muted afterwards");
    check(p.channels[5].solo, "a soloed channel is still soloed afterwards");
    for (int ch = 0; ch < 4; ++ch) {
        if (ch == 4) continue;
        check(!p.channels[ch].muted || ch == 4,
              "channel " + std::to_string(ch) + " mute state restored");
    }

    // The files are real WAVs with audio in them
    int checked = 0;
    for (int ch = 1; ch <= 3; ++ch) {
        char name[256];
        std::snprintf(name, sizeof(name), "stem_%02d_", ch);
        // Find whichever file starts with this prefix
        for (const char* suffix : {"Lead.wav", "Bass_Sub.wav", "channel.wav"}) {
            const std::string path = dir + "/" + name + suffix;
            std::string data = readWholeFile(path);
            if (data.empty()) continue;
            ++checked;
            check(data.size() > 44, std::string(name) + suffix + " has audio past the header");
            check(data.compare(0, 4, "RIFF") == 0, std::string(name) + suffix + " is a RIFF file");
            std::remove(path.c_str());
        }
    }
    check(checked == 3, "all three stem files were found on disk (found " +
          std::to_string(checked) + ")");

    // A bad duration must be refused, not crash
    StemExportResult bad = exportStems(p, seq, dir, -1.0f, true);
    check(bad.written == 0, "a negative duration writes no stems");
    check(std::isfinite(p.bpm), "a refused export leaves the project intact");
}

// ============================================================================
// 25. Theme legibility
// ============================================================================
static void testThemeContrast() {
    beginTest("Theme legibility");

    // ImGui draws every label in ImGuiCol_Text - one colour for the whole UI -
    // so a surface cannot pick a label colour that suits it. The surface has
    // to come to the text, which ApplyTheme's derive pass now enforces.
    //
    // Nine of the ten themes once had button labels below 4.5:1, seven under
    // 3:1, the worst at 1.22:1: pressing a button made its own label vanish.
    // Checking only Text against WindowBg hid all of it, which is why this
    // walks every surface a label actually lands on.

    auto channel = [](float v) {
        return (v <= 0.03928f) ? v / 12.92f
                               : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    auto luminance = [&](const ImVec4& c) {
        return 0.2126f * channel(c.x) + 0.7152f * channel(c.y) + 0.0722f * channel(c.z);
    };
    auto contrast = [&](const ImVec4& a, const ImVec4& b) {
        float la = luminance(a), lb = luminance(b);
        if (la < lb) std::swap(la, lb);
        return (la + 0.05f) / (lb + 0.05f);
    };

    struct NamedTheme { const char* name; Theme theme; };
    const NamedTheme themes[] = {
        {"Stock",         Theme::Stock},
        {"Cyberpunk",     Theme::Cyberpunk},
        {"Synthwave",     Theme::Synthwave},
        {"Matrix",        Theme::Matrix},
        {"FrutigerAero",  Theme::FrutigerAero},
        {"Minimal",       Theme::Minimal},
        {"Vaporwave",     Theme::Vaporwave},
        {"RetroTerminal", Theme::RetroTerminal},
        {"GameBoy",       Theme::GameBoy},
        {"Daylight",      Theme::Daylight},
    };

    struct NamedSlot { const char* name; ImGuiCol slot; };
    const NamedSlot surfaces[] = {
        {"WindowBg",       ImGuiCol_WindowBg},
        {"ChildBg",        ImGuiCol_ChildBg},
        {"PopupBg",        ImGuiCol_PopupBg},
        {"FrameBg",        ImGuiCol_FrameBg},
        {"FrameBgHovered", ImGuiCol_FrameBgHovered},
        {"FrameBgActive",  ImGuiCol_FrameBgActive},
        {"Button",         ImGuiCol_Button},
        {"ButtonHovered",  ImGuiCol_ButtonHovered},
        {"ButtonActive",   ImGuiCol_ButtonActive},
        {"Header",         ImGuiCol_Header},
        {"HeaderHovered",  ImGuiCol_HeaderHovered},
        {"HeaderActive",   ImGuiCol_HeaderActive},
        {"Tab",            ImGuiCol_Tab},
        {"TabHovered",     ImGuiCol_TabHovered},
        {"TabSelected",    ImGuiCol_TabSelected},
        {"TitleBgActive",  ImGuiCol_TitleBgActive},
        {"MenuBarBg",      ImGuiCol_MenuBarBg},
    };

    ImGui::CreateContext();

    for (const NamedTheme& t : themes) {
        ApplyTheme(t.theme);
        const ImVec4* colors = ImGui::GetStyle().Colors;
        const ImVec4 text = colors[ImGuiCol_Text];

        for (const NamedSlot& surface : surfaces) {
            const float ratio = contrast(text, colors[surface.slot]);
            check(ratio >= 4.4f,     // 4.4 not 4.5: the search lands just above
                  std::string(t.name) + ": label on " + surface.name + " is " +
                  std::to_string(ratio) + ":1, below 4.5:1");
        }

        // Header and Button must remain distinguishable after the contrast
        // pass has moved both - otherwise nothing marks what is pressable.
        const ImVec4 header = colors[ImGuiCol_Header];
        const ImVec4 button = colors[ImGuiCol_Button];
        const float delta = std::fabs(header.x - button.x) +
                            std::fabs(header.y - button.y) +
                            std::fabs(header.z - button.z);
        check(delta > 0.03f,
              std::string(t.name) + ": Header and Button are too close to tell "
              "apart (delta " + std::to_string(delta) + ")");

        // Interactive surfaces stay opaque enough to read over an animated
        // background - the contrast pass must not have traded alpha away.
        for (const NamedSlot& surface : surfaces) {
            const std::string name = surface.name;
            if (name.rfind("Button", 0) == 0 || name.rfind("Header", 0) == 0 ||
                name.rfind("FrameBg", 0) == 0) {
                check(colors[surface.slot].w >= 0.85f,
                      std::string(t.name) + ": " + surface.name + " alpha " +
                      std::to_string(colors[surface.slot].w) + " below 0.85");
            }
        }

        // Every colour must be a real colour: the derive pass stamps a
        // negative sentinel and anything left holding it would render wrong.
        int negative = 0;
        for (int i = 0; i < ImGuiCol_COUNT; ++i) {
            if (colors[i].x < 0.0f || colors[i].y < 0.0f || colors[i].z < 0.0f) ++negative;
        }
        check(negative == 0,
              std::string(t.name) + ": " + std::to_string(negative) +
              " colour slots left unset by the derive pass");
    }

    // Switching themes must be order-independent. A theme that inherits a
    // value from whichever theme preceded it looks different depending on
    // the route taken to it.
    {
        auto snapshot = [&](Theme theme) {
            ApplyTheme(theme);
            std::vector<ImVec4> out(ImGuiCol_COUNT);
            for (int i = 0; i < ImGuiCol_COUNT; ++i) out[i] = ImGui::GetStyle().Colors[i];
            return out;
        };

        const std::vector<ImVec4> direct = snapshot(Theme::GameBoy);

        // Reach it the long way round, through every other theme
        for (const NamedTheme& t : themes) ApplyTheme(t.theme);
        const std::vector<ImVec4> afterTour = snapshot(Theme::GameBoy);

        int differences = 0;
        for (int i = 0; i < ImGuiCol_COUNT; ++i) {
            if (std::fabs(direct[i].x - afterTour[i].x) > 1e-6f ||
                std::fabs(direct[i].y - afterTour[i].y) > 1e-6f ||
                std::fabs(direct[i].z - afterTour[i].z) > 1e-6f ||
                std::fabs(direct[i].w - afterTour[i].w) > 1e-6f) ++differences;
        }
        check(differences == 0,
              std::to_string(differences) + " colours depend on which theme "
              "was applied before - a theme is missing an assignment");
    }

    ImGui::DestroyContext();
}

// ============================================================================
// 26. Autosave and crash recovery
// ============================================================================
static void testAutosave() {
    beginTest("Autosave and crash recovery");

    const std::string dir = testPath("autosave_test");
    ensureDirectoryExists(dir);

    auto cleanUp = [&]() {
        std::remove((dir + "/recovery.ctp").c_str());
        std::remove((dir + "/recovery-previous.ctp").c_str());
        std::remove((dir + "/recovery.ctp.tmp").c_str());
    };
    cleanUp();

    auto projectWith = [](int noteCount, float bpm) {
        Project p;
        p.bpm = bpm;
        Pattern pattern;
        for (int i = 0; i < noteCount; ++i) {
            Note n;
            n.pitch = 48 + i;
            n.startTime = float(i) * 0.25f;
            pattern.notes.push_back(n);
        }
        p.patterns.clear();
        p.patterns.push_back(pattern);
        return p;
    };

    // ---- A clean start has nothing to recover ---------------------------
    {
        Autosave autosave;
        autosave.setDirectory(dir);
        check(!autosave.hasRecoverableSession(),
              "a clean directory offers nothing to recover");
        check(autosave.bestRecoveryPath().empty(),
              "no recovery path when there is no recovery file");
    }

    // ---- Saving produces a loadable recovery ----------------------------
    {
        Autosave autosave;
        autosave.setDirectory(dir);
        Project original = projectWith(6, 133.0f);
        autosave.save(original);

        check(autosave.hasRecoverableSession(),
              "after a save there is something to recover");
        check(autosave.saveCount() == 1, "the save was counted");

        Project restored;
        check(loadProject(restored, autosave.bestRecoveryPath()),
              "the recovery file loads");
        check(std::fabs(restored.bpm - 133.0f) < 0.01f,
              "the recovered project has the right tempo");
        check(!restored.patterns.empty() && restored.patterns[0].notes.size() == 6,
              "the recovered project has all its notes");
    }

    // ---- The previous generation is kept --------------------------------
    //
    // Writing straight over the only recovery file means a crash during the
    // write destroys it. Two generations means there is always one intact.
    {
        Autosave autosave;
        autosave.setDirectory(dir);
        autosave.save(projectWith(3, 100.0f));
        autosave.save(projectWith(9, 155.0f));

        Project newest, previous;
        check(loadProject(newest, dir + "/recovery.ctp"),
              "the newest recovery loads");
        check(loadProject(previous, dir + "/recovery-previous.ctp"),
              "the previous generation was kept");

        check(newest.patterns[0].notes.size() == 9, "newest holds the latest save");
        check(previous.patterns[0].notes.size() == 3,
              "previous holds the one before it");
    }

    // ---- A clean exit clears the evidence -------------------------------
    //
    // The recovery file surviving to the next launch IS the crash signal, so
    // a tidy shutdown has to remove it or every start offers a stale restore.
    {
        Autosave autosave;
        autosave.setDirectory(dir);
        autosave.save(projectWith(4, 120.0f));
        check(autosave.hasRecoverableSession(), "there is a recovery to clear");

        autosave.clearOnCleanExit();
        check(!autosave.hasRecoverableSession(),
              "a clean exit leaves nothing to recover");
    }

    // ---- The timer only fires when something changed --------------------
    {
        Autosave autosave;
        autosave.setDirectory(dir);
        Project p = projectWith(5, 120.0f);

        // Nothing marked dirty: an idle session must not churn the disk.
        autosave.update(p, Autosave::INTERVAL_SECONDS + 1.0f);
        check(autosave.saveCount() == 0,
              "an idle session does not autosave");

        autosave.markDirty();
        autosave.update(p, 1.0f);
        check(autosave.saveCount() == 0,
              "a dirty project does not autosave before the interval elapses");

        autosave.update(p, Autosave::INTERVAL_SECONDS);
        check(autosave.saveCount() == 1,
              "a dirty project autosaves once the interval elapses");

        // And having saved, it is clean again
        autosave.update(p, Autosave::INTERVAL_SECONDS + 1.0f);
        check(autosave.saveCount() == 1,
              "it does not save again with no further changes");

        autosave.clearOnCleanExit();
    }

    // ---- Disabled means disabled -----------------------------------------
    {
        Autosave autosave;
        autosave.setDirectory(dir);
        autosave.setEnabled(false);
        autosave.markDirty();
        autosave.update(projectWith(2, 120.0f), Autosave::INTERVAL_SECONDS * 3.0f);
        check(autosave.saveCount() == 0, "a disabled autosave never writes");
        check(!autosave.hasRecoverableSession(), "and leaves no file behind");
    }

    // ---- An unwritable directory fails quietly ---------------------------
    //
    // A read-only or missing target must not crash or throw; losing the
    // autosave is bad, taking the app down with it is worse.
    {
        Autosave autosave;
        autosave.setDirectory(dir + "/does/not/exist");
        autosave.save(projectWith(3, 120.0f));
        check(autosave.saveCount() == 0,
              "saving into a missing directory reports no save rather than crashing");
        check(true, "an unwritable target did not throw");
    }

    // ---- A full round trip through everything ----------------------------
    {
        Autosave autosave;
        autosave.setDirectory(dir);

        Project rich = makeKitchenSinkProject();
        autosave.save(rich);

        Project restored;
        check(loadProject(restored, autosave.bestRecoveryPath()),
              "a fully populated project recovers");
        check(restored.patterns.size() == rich.patterns.size(),
              "recovered pattern count matches");
        check(restored.arrangement.size() == rich.arrangement.size(),
              "recovered arrangement matches - the mix is not lost in recovery");
        check(std::fabs(restored.channels[0].filterCutoff -
                        rich.channels[0].filterCutoff) < 0.1f,
              "recovered channel effects match");

        autosave.clearOnCleanExit();
    }

    cleanUp();
}

// ============================================================================
// 27. Grid snap
// ============================================================================
static void testGridSnap() {
    beginTest("Grid snap");

    // The snap step was hardcoded as floor(beat * 4) / 4 in fourteen places,
    // so a 1/16 note was the only thing anyone could write. Sixteenth has to
    // keep reproducing that exactly, or every existing gesture changes.
    check(std::fabs(snapBeat(0.30f, SnapDivision::Sixteenth) - 0.25f) < 1e-6f,
          "1/16 snap still floors to a quarter beat, as the old constant did");
    check(std::fabs(snapBeat(0.99f, SnapDivision::Sixteenth) - 0.75f) < 1e-6f,
          "1/16 snap floors rather than rounds, matching the old behaviour");

    check(std::fabs(snapStepBeats(SnapDivision::Quarter) - 1.0f) < 1e-6f,
          "a 1/4 note is one beat");
    check(std::fabs(snapStepBeats(SnapDivision::Eighth) - 0.5f) < 1e-6f,
          "a 1/8 note is half a beat");
    check(std::fabs(snapStepBeats(SnapDivision::Bar, 3) - 3.0f) < 1e-6f,
          "a bar follows the time signature");

    // Triplets are the reason this exists: without them shuffle and 6/8 are
    // unwritable.
    check(std::fabs(snapStepBeats(SnapDivision::TripletEighth) - 1.0f / 3.0f) < 1e-6f,
          "an eighth triplet is a third of a beat");
    check(std::fabs(snapBeat(0.7f, SnapDivision::TripletEighth) - 2.0f / 3.0f) < 1e-5f,
          "a beat snaps onto the triplet grid");
    check(std::fabs(snapBeat(1.9f, SnapDivision::TripletQuarter) - 4.0f / 3.0f) < 1e-5f,
          "quarter triplets land on thirds of two beats");

    // Off is how a note gets placed deliberately between grid lines.
    check(std::fabs(snapBeat(0.3712f, SnapDivision::Off) - 0.3712f) < 1e-6f,
          "snap off leaves the position untouched");

    // A duration that snapped to zero would make the note unselectable.
    check(snapDuration(0.001f, SnapDivision::Sixteenth) > 0.0f,
          "a tiny duration never snaps to zero");
    check(std::fabs(snapDuration(0.001f, SnapDivision::Sixteenth) - 0.25f) < 1e-6f,
          "a tiny duration snaps up to one grid step");
    check(snapDuration(0.001f, SnapDivision::Off) > 0.0f,
          "with snap off a duration still has a floor");

    // Negative positions occur while dragging left past the origin.
    check(snapBeat(-0.3f, SnapDivision::Sixteenth) <= 0.0f,
          "a negative beat stays negative rather than wrapping high");

    // A non-finite value would poison every downstream calculation.
    check(snapBeat(std::numeric_limits<float>::quiet_NaN(),
                   SnapDivision::Sixteenth) == 0.0f,
          "NaN is caught at the snap rather than spreading");
    check(snapBeat(std::numeric_limits<float>::infinity(),
                   SnapDivision::Sixteenth) == 0.0f,
          "infinity is caught at the snap");

    // The bracket keys step through the list and wrap.
    check(cycleSnap(SnapDivision::Off, -1) ==
              static_cast<SnapDivision>(static_cast<int>(SnapDivision::Count) - 1),
          "stepping back from the first division wraps to the last");
    check(cycleSnap(static_cast<SnapDivision>(static_cast<int>(SnapDivision::Count) - 1), 1)
              == SnapDivision::Off,
          "stepping past the last division wraps to the first");

    for (int i = 0; i < static_cast<int>(SnapDivision::Count); ++i) {
        const SnapDivision d = static_cast<SnapDivision>(i);
        if (std::string(snapLabel(d)) == "?") {
            check(false, "every division has a label");
            break;
        }
    }
    check(true, "every division has a label for the combo");
}

// ============================================================================
// 28. Loop range
// ============================================================================
static void testLoopRange() {
    beginTest("Loop range");

    // ---- Resolving the window --------------------------------------------
    {
        LoopWindow w = resolveLoopWindow(true, 4.0f, 12.0f, 64.0f);
        check(w.valid && std::fabs(w.start - 4.0f) < 1e-6f &&
              std::fabs(w.end - 12.0f) < 1e-6f,
              "a user range wins over the content extent");

        w = resolveLoopWindow(false, 4.0f, 12.0f, 64.0f);
        check(w.valid && std::fabs(w.start) < 1e-6f && std::fabs(w.end - 64.0f) < 1e-6f,
              "with no range set, playback loops over the content - the old behaviour");

        // A click without a drag must not leave a zero-length loop; that
        // would freeze the playhead in place at audio rate.
        w = resolveLoopWindow(true, 8.0f, 8.0f, 64.0f);
        check(w.valid && std::fabs(w.end - 64.0f) < 1e-6f,
              "a zero-length range falls back rather than trapping the playhead");

        w = resolveLoopWindow(true, 12.0f, 4.0f, 64.0f);
        check(w.valid && std::fabs(w.start - 4.0f) < 1e-6f &&
              std::fabs(w.end - 12.0f) < 1e-6f,
              "a range dragged right-to-left is normalised");

        w = resolveLoopWindow(false, 0.0f, 0.0f, 0.0f);
        check(!w.valid, "an empty project yields no window at all");

        w = resolveLoopWindow(true, std::numeric_limits<float>::quiet_NaN(), 8.0f, 32.0f);
        check(w.valid && std::fabs(w.end - 32.0f) < 1e-6f,
              "a NaN range is rejected rather than propagated");
    }

    // ---- Wrapping ---------------------------------------------------------
    {
        const LoopWindow w = resolveLoopWindow(true, 4.0f, 8.0f, 64.0f);

        check(std::fabs(wrapIntoWindow(6.0f, w) - 6.0f) < 1e-6f,
              "a beat inside the window is left alone");
        check(std::fabs(wrapIntoWindow(8.0f, w) - 4.0f) < 1e-6f,
              "the end of the window folds back to the start");
        check(std::fabs(wrapIntoWindow(9.5f, w) - 5.5f) < 1e-6f,
              "overshooting the end carries the remainder");

        // A single subtraction is not enough. A long audio block, or a range
        // shortened while playing, can leave the playhead several window
        // lengths past the end - the same class of bug that let oscillator
        // phase run away to 2.4e7.
        const float farPast = wrapIntoWindow(4.0f + 4.0f * 37.25f, w);
        check(farPast >= w.start && farPast < w.end,
              "a beat many windows past the end still folds inside (got " +
              std::to_string(farPast) + ")");

        const float behind = wrapIntoWindow(1.0f, w);
        check(behind >= w.start && behind <= w.end,
              "a beat before the window folds inside too");
    }

    // ---- Where play() drops the playhead ----------------------------------
    {
        const LoopWindow w = resolveLoopWindow(true, 8.0f, 16.0f, 64.0f);
        check(std::fabs(clampStartBeat(2.0f, w, true) - 8.0f) < 1e-6f,
              "pressing play outside the loop jumps into it, so play always sounds");
        check(std::fabs(clampStartBeat(10.0f, w, true) - 10.0f) < 1e-6f,
              "pressing play inside the loop keeps the position");
        check(std::fabs(clampStartBeat(2.0f, w, false) - 2.0f) < 1e-6f,
              "with looping off the position is never moved");
    }
}

// ============================================================================
// 29. Note expansion - delay, cut, retrigger, echo
// ============================================================================
static void testNoteExpansion() {
    beginTest("Note expansion");

    NoteTrigger out[MAX_NOTE_TRIGGERS];

    // ---- A plain note is one hit -----------------------------------------
    {
        Note n;
        n.startTime = 1.0f;
        n.duration = 2.0f;
        n.velocity = 0.8f;
        const int count = expandNote(n, 4.0f, out, MAX_NOTE_TRIGGERS);
        check(count == 1, "an ordinary note produces exactly one hit");
        check(std::fabs(out[0].startBeat - 5.0f) < 1e-6f,
              "the clip origin is added to the note's own start");
        check(std::fabs(out[0].endBeat - 7.0f) < 1e-6f, "the hit lasts the note's length");
        check(std::fabs(out[0].velocity - 0.8f) < 1e-6f, "velocity carries through");
        check(!noteNeedsExpansion(n), "a plain note is recognised as needing no expansion");
    }

    // ---- Note delay -------------------------------------------------------
    {
        Note n;
        n.startTime = 0.0f;
        n.duration = 1.0f;
        n.noteDelay = 0.25f;
        const int count = expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(count == 1, "a delayed note is still one hit");
        check(std::fabs(out[0].startBeat - 0.25f) < 1e-6f,
              "note delay pushes the hit later - this is how a flam is written");
        check(noteNeedsExpansion(n), "a delayed note needs expansion");
    }

    // ---- Note cut ---------------------------------------------------------
    {
        Note n;
        n.startTime = 0.0f;
        n.duration = 4.0f;
        n.noteCut = 0.5f;
        const int count = expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(count == 1 && std::fabs(out[0].endBeat - 0.5f) < 1e-6f,
              "note cut ends the hit early");

        // A cut longer than the note is no cut at all.
        n.noteCut = 8.0f;
        expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(std::fabs(out[0].endBeat - 4.0f) < 1e-6f,
              "a cut past the end of the note leaves it alone");
    }

    // ---- Retrigger --------------------------------------------------------
    {
        Note n;
        n.startTime = 0.0f;
        n.duration = 1.0f;
        n.retriggerCount = 3;
        n.retriggerSpeed = 0.25f;
        const int count = expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(count == 4, "3 retriggers means 4 hits (got " + std::to_string(count) + ")");
        check(std::fabs(out[1].startBeat - 0.25f) < 1e-6f, "hits are evenly spaced");
        check(std::fabs(out[3].startBeat - 0.75f) < 1e-6f, "the last hit lands in time");

        // No hit may ring past the note's own end, or a retrigger on a short
        // note would bleed over whatever comes next.
        for (int i = 0; i < count; ++i) {
            if (out[i].endBeat > 1.0f + 1e-5f) {
                check(false, "a retrigger hit rang past the end of the note");
                break;
            }
        }
        check(true, "no retrigger hit rings past the end of the note");

        // A retrigger interval longer than the note leaves the note alone.
        n.retriggerSpeed = 4.0f;
        const int sparse = expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(sparse == 1, "a retrigger slower than the note yields a single hit");
    }

    // ---- Echo -------------------------------------------------------------
    {
        Note n;
        n.startTime = 0.0f;
        n.duration = 0.5f;
        n.velocity = 1.0f;
        n.echoRepeats = 3;
        n.echoDelay = 1.0f;
        n.echoDecay = 0.5f;
        const int count = expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(count == 4, "3 echoes means 4 hits (got " + std::to_string(count) + ")");
        check(std::fabs(out[1].startBeat - 1.0f) < 1e-6f, "the first echo lands one delay later");
        check(out[1].velocity < out[0].velocity, "each echo is quieter than the last");
        check(std::fabs(out[1].velocity - 0.5f) < 1e-5f, "echo decay is applied per repeat");
        check(std::fabs(out[3].velocity - 0.125f) < 1e-5f, "decay compounds across repeats");

        // An inaudible echo should not cost a voice.
        n.echoDecay = 0.05f;
        n.echoRepeats = 4;
        const int audible = expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(audible < 5, "echoes stop once they fall below audibility (got " +
              std::to_string(audible) + " hits)");
    }

    // ---- Combining --------------------------------------------------------
    {
        Note n;
        n.startTime = 0.0f;
        n.duration = 1.0f;
        n.noteDelay = 0.5f;
        n.retriggerCount = 1;
        n.retriggerSpeed = 0.5f;
        n.echoRepeats = 1;
        n.echoDelay = 2.0f;
        const int count = expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(count >= 3, "delay, retrigger and echo combine");
        check(std::fabs(out[0].startBeat - 0.5f) < 1e-6f,
              "the delay applies before the retrigger chop");
    }

    // ---- Nothing can exceed the buffer -----------------------------------
    {
        Note n;
        n.startTime = 0.0f;
        n.duration = 100.0f;
        n.retriggerCount = 8;
        n.retriggerSpeed = 0.0156f;
        n.echoRepeats = 4;
        n.echoDelay = 0.0156f;
        n.echoDecay = 0.99f;
        const int count = expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(count <= MAX_NOTE_TRIGGERS,
              "expansion never writes past the fixed buffer it is given");
        check(count >= 1, "even a pathological note produces at least one hit");
    }

    // ---- Values from a hand-edited file ----------------------------------
    {
        Note n;
        n.duration = 1.0f;
        n.retriggerCount = 4;
        n.retriggerSpeed = 0.0f;      // would emit every hit at the same instant
        const int count = expandNote(n, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(count == 1, "a zero retrigger interval is ignored rather than looping forever");

        Note bad;
        bad.duration = std::numeric_limits<float>::quiet_NaN();
        bad.velocity = std::numeric_limits<float>::infinity();
        const int c2 = expandNote(bad, 0.0f, out, MAX_NOTE_TRIGGERS);
        check(c2 >= 0, "a note full of garbage does not crash the expander");

        check(expandNote(n, 0.0f, nullptr, 8) == 0, "a null output buffer is refused");
        check(expandNote(n, 0.0f, out, 0) == 0, "a zero-size output buffer is refused");
    }
}

// ============================================================================
// 30. Loop range and note effects reach the audio
// ============================================================================
static void testLoopAndEffectsReachAudio() {
    beginTest("Loop range and note effects reach the audio");

    // Asserting that the code runs proves nothing - these fields were stored
    // and serialised for a long time while the synth never read them. The
    // only test that would have caught it is one that listens.

    auto renderNote = [](void (*configure)(Note&), int blocks) {
        Project p;
        p.bpm = 120.0f;                 // one beat = 0.5 s = 22050 samples
        Pattern pat;
        Note n;
        n.pitch = 69;
        n.startTime = 0.0f;
        n.duration = 1.0f;
        n.velocity = 1.0f;
        n.oscillatorType = OscillatorType::Pulse;
        if (configure) configure(n);
        pat.notes.push_back(n);
        p.patterns.clear();
        p.patterns.push_back(pat);
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 8.0f, 0});

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.play();

        std::vector<float> out;
        std::vector<float> l(512), r(512);
        for (int b = 0; b < blocks; ++b) {
            seq.process(l.data(), r.data(), 512);
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    };

    auto energy = [](const std::vector<float>& v, size_t from, size_t to) {
        double sum = 0.0;
        const size_t hi = std::min(to, v.size());
        for (size_t i = from; i < hi; ++i) sum += double(v[i]) * double(v[i]);
        return sum;
    };

    const int BLOCKS = 120;   // ~61k samples, about 2.8 beats at 120 BPM

    // ---- Note delay -------------------------------------------------------
    {
        const auto plain = renderNote(nullptr, BLOCKS);
        const auto delayed = renderNote([](Note& n) { n.noteDelay = 0.5f; }, BLOCKS);

        // Half a beat at 120 BPM is 11025 samples; look at the first 8000.
        const double plainHead = energy(plain, 0, 8000);
        const double delayedHead = energy(delayed, 0, 8000);

        check(plainHead > 1.0,
              "the reference note is audible at the start (energy " +
              std::to_string(plainHead) + ")");
        check(delayedHead < plainHead * 0.05,
              "note delay silences the start of the note - it was stored but never "
              "played before (plain " + std::to_string(plainHead) + " vs delayed " +
              std::to_string(delayedHead) + ")");
        check(energy(delayed, 12000, 20000) > 1.0,
              "the delayed note does sound, just later");
    }

    // ---- Note cut ---------------------------------------------------------
    {
        const auto plain = renderNote(nullptr, BLOCKS);
        const auto cut = renderNote([](Note& n) { n.noteCut = 0.2f; }, BLOCKS);

        // 0.2 beats is 4410 samples; everything past 10000 should be gone.
        const double plainTail = energy(plain, 10000, 20000);
        const double cutTail = energy(cut, 10000, 20000);

        check(plainTail > 1.0, "the reference note is still sounding at that point");
        check(cutTail < plainTail * 0.05,
              "note cut ends the note early (plain tail " + std::to_string(plainTail) +
              " vs cut tail " + std::to_string(cutTail) + ")");
    }

    // ---- Echo -------------------------------------------------------------
    {
        const auto plain = renderNote([](Note& n) { n.duration = 0.25f; }, BLOCKS);
        const auto echoed = renderNote([](Note& n) {
            n.duration = 0.25f;
            n.echoRepeats = 2;
            n.echoDelay = 0.5f;
            n.echoDecay = 0.7f;
        }, BLOCKS);

        // The note itself is over by sample 5512; anything after 14000 is echo.
        const double plainTail = energy(plain, 14000, 30000);
        const double echoTail = energy(echoed, 14000, 30000);

        check(echoTail > plainTail * 4.0,
              "echo repeats keep sounding after the note ends (plain " +
              std::to_string(plainTail) + " vs echoed " + std::to_string(echoTail) + ")");
    }

    // ---- Retrigger --------------------------------------------------------
    {
        const auto plain = renderNote(nullptr, BLOCKS);
        const auto stutter = renderNote([](Note& n) {
            n.retriggerCount = 5;
            n.retriggerSpeed = 0.125f;
        }, BLOCKS);

        // A stutter is amplitude that dips and recovers. Count the dips in a
        // short-window RMS envelope; a sustained note has none.
        auto dipCount = [](const std::vector<float>& v, size_t from, size_t to) {
            const size_t window = 256;
            double peak = 0.0;
            std::vector<double> rms;
            for (size_t i = from; i + window < std::min(to, v.size()); i += window) {
                double sum = 0.0;
                for (size_t j = 0; j < window; ++j) sum += double(v[i + j]) * v[i + j];
                const double value = std::sqrt(sum / double(window));
                rms.push_back(value);
                if (value > peak) peak = value;
            }
            if (peak <= 0.0) return 0;

            const double low = peak * 0.2;
            int dips = 0;
            bool below = false;
            for (double value : rms) {
                if (!below && value < low) { below = true; }
                else if (below && value > low * 2.0) { below = false; ++dips; }
            }
            return dips;
        };

        const int plainDips = dipCount(plain, 0, 20000);
        const int stutterDips = dipCount(stutter, 0, 20000);

        check(stutterDips > plainDips,
              "retrigger chops the note into separate hits (sustained note had " +
              std::to_string(plainDips) + " dips, retriggered had " +
              std::to_string(stutterDips) + ")");
    }

    // ---- Pitch sweep ------------------------------------------------------
    //
    // The synth has implemented sweep all along; it simply had no control.
    {
        const auto plain = renderNote(nullptr, 40);
        const auto swept = renderNote([](Note& n) {
            n.sweepDirection = SweepDirection::Down;
            n.sweepSpeed = 12.0f;
            n.sweepAmount = 24.0f;
        }, 40);

        double difference = 0.0;
        const size_t n = std::min(plain.size(), swept.size());
        for (size_t i = 0; i < n; ++i) difference += std::fabs(plain[i] - swept[i]);
        check(difference > 100.0,
              "pitch sweep changes the rendered signal (difference " +
              std::to_string(difference) + ")");
    }

    // ---- The loop range actually confines playback ------------------------
    {
        auto renderRange = [](bool useRange) {
            Project p;
            p.bpm = 120.0f;
            Pattern early, late;

            Note a; a.pitch = 60; a.startTime = 0.0f; a.duration = 0.5f;
            a.oscillatorType = OscillatorType::Pulse;
            early.notes.push_back(a);

            Note b; b.pitch = 84; b.startTime = 0.0f; b.duration = 0.5f;
            b.oscillatorType = OscillatorType::Pulse;
            late.notes.push_back(b);

            p.patterns.clear();
            p.patterns.push_back(early);
            p.patterns.push_back(late);
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 2.0f, 0});
            p.arrangement.push_back(Clip{1, 1, 4.0f, 2.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            Sequencer& seq = *seqPtr;
            seq.setSampleRate(44100.0f);
            seq.setProject(&p);
            seq.setLoopEnabled(true);
            if (useRange) seq.setLoopRange(0.0f, 2.0f);
            seq.play();

            // The furthest the playhead ever reaches, not where it finishes:
            // an unconfined playhead wraps at the end of the content, so the
            // final position says nothing about how far it travelled.
            std::vector<float> l(512), r(512);
            float furthest = 0.0f;
            for (int block = 0; block < 300; ++block) {
                seq.process(l.data(), r.data(), 512);
                furthest = std::max(furthest, seq.getCurrentBeat());
            }
            return furthest;
        };

        const float confined = renderRange(true);
        const float free = renderRange(false);

        check(confined < 2.0f + 1e-3f,
              "with a loop range the playhead never leaves it (furthest beat " +
              std::to_string(confined) + ")");
        check(free > 2.0f,
              "without a range the playhead runs past beat 2, so the range is what "
              "confined it (furthest beat " + std::to_string(free) + ")");
    }

    // ---- A loop range survives being set while playing --------------------
    {
        Project p;
        p.bpm = 120.0f;
        Pattern pat;
        Note n; n.pitch = 60; n.startTime = 0.0f; n.duration = 0.5f;
        pat.notes.push_back(n);
        p.patterns.clear();
        p.patterns.push_back(pat);
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 16.0f, 0});

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.setLoopEnabled(true);
        seq.play();

        std::vector<float> l(512), r(512);
        for (int b = 0; b < 300; ++b) seq.process(l.data(), r.data(), 512);

        // Shrink the loop to a span the playhead has already passed.
        seq.setLoopRange(0.0f, 1.0f);
        for (int b = 0; b < 60; ++b) seq.process(l.data(), r.data(), 512);

        const float beat = seq.getCurrentBeat();
        check(beat >= 0.0f && beat < 1.0f + 1e-3f,
              "narrowing the loop under a running playhead folds it back in, rather "
              "than leaving it stranded outside (beat " + std::to_string(beat) + ")");
    }
}

// ============================================================================
// 31. Scales, transpose and note probability
// ============================================================================
static void testScalesAndTransforms() {
    beginTest("Scales, transpose and probability");

    // ---- Scales -----------------------------------------------------------
    //
    // The tables were right all along; they were simply unreachable from the
    // piano roll, so the "Snap to Scale" checkbox set a flag nothing read.
    {
        // C major: C D E F G A B, no accidentals.
        check(isNoteInScale(60, 0, 0), "C is in C major");
        check(isNoteInScale(62, 0, 0), "D is in C major");
        check(!isNoteInScale(61, 0, 0), "C# is not in C major");
        check(!isNoteInScale(66, 0, 0), "F# is not in C major");

        // A minor is the relative minor, so the same seven pitch classes.
        check(isNoteInScale(69, 9, 1), "A is in A minor");
        check(!isNoteInScale(70, 9, 1), "A# is not in A minor");

        // Pentatonic drops the fourth and the seventh.
        check(isNoteInScale(60, 0, 8), "C is in C pentatonic major");
        check(!isNoteInScale(65, 0, 8), "F is not in C pentatonic major");
        check(!isNoteInScale(71, 0, 8), "B is not in C pentatonic major");

        // The blues scale keeps its flat fifth.
        check(isNoteInScale(66, 0, 10), "the flat fifth is in C blues");

        // A pitch below the root must not produce a negative modulus.
        check(isNoteInScale(0, 9, 1), "a very low pitch classifies correctly");
        check(isNoteInScale(48, 9, 1), "C two octaves down is still in A minor");
    }

    // ---- Snapping to a scale ---------------------------------------------
    {
        check(snapToScale(60, 0, 0) == 60, "a note already in scale is left alone");
        const int snapped = snapToScale(61, 0, 0);
        check(snapped == 60 || snapped == 62,
              "C# snaps to an adjacent scale tone (got " + std::to_string(snapped) + ")");
        check(isNoteInScale(snapToScale(66, 0, 0), 0, 0),
              "whatever F# snaps to is genuinely in the scale");

        // Every pitch must land somewhere valid, in every scale.
        for (int scaleType = 0; scaleType < SCALE_COUNT; ++scaleType) {
            for (int pitch = 0; pitch < 128; ++pitch) {
                const int result = snapToScale(pitch, 0, scaleType);
                if (!isNoteInScale(result, 0, scaleType)) {
                    check(false, std::string("snapToScale left pitch ") +
                          std::to_string(pitch) + " outside scale " +
                          std::to_string(scaleType));
                    scaleType = SCALE_COUNT;
                    break;
                }
            }
        }
        check(true, "every pitch snaps into every scale");
    }

    // ---- Transpose --------------------------------------------------------
    //
    // This did not exist anywhere in the program before.
    {
        auto notesAt = [](std::initializer_list<int> pitches) {
            std::vector<Note> v;
            for (int pitch : pitches) {
                Note n; n.pitch = pitch; v.push_back(n);
            }
            return v;
        };
        auto allIndices = [](const std::vector<Note>& v) {
            std::vector<int> idx(v.size());
            for (size_t i = 0; i < v.size(); ++i) idx[i] = int(i);
            return idx;
        };

        {
            std::vector<Note> notes = notesAt({60, 64, 67});
            const TransformResult r = transposeNotes(notes, allIndices(notes), 5);
            check(r.changed == 3 && r.blocked == 0, "every note transposed");
            check(notes[0].pitch == 65 && notes[1].pitch == 69 && notes[2].pitch == 72,
                  "transpose adds the interval to each pitch");
        }

        {
            std::vector<Note> notes = notesAt({60, 64, 67});
            transposeNotes(notes, allIndices(notes), -12);
            check(notes[0].pitch == 48, "a negative interval moves down an octave");
        }

        // Safe mode leaves unplayable results alone rather than clamping.
        // Clamping looks like it worked and quietly collapses a chord onto
        // one pitch.
        {
            std::vector<Note> notes = notesAt({120, 60});
            const TransformResult r = transposeNotes(notes, allIndices(notes), 24, true);
            check(r.blocked == 1, "safe mode reports the note it refused to move");
            check(notes[0].pitch == 120, "the out-of-range note is untouched");
            check(notes[1].pitch == 84, "the in-range note still moves");
        }

        {
            std::vector<Note> notes = notesAt({120});
            transposeNotes(notes, allIndices(notes), 24, false);
            check(notes[0].pitch == MAX_PITCH,
                  "with safe mode off the pitch clamps instead");
        }

        // A chord transposed by an octave must stay a chord.
        {
            std::vector<Note> notes = notesAt({60, 64, 67});
            transposeNotes(notes, allIndices(notes), 12, true);
            check(notes[1].pitch - notes[0].pitch == 4 &&
                  notes[2].pitch - notes[1].pitch == 3,
                  "transposing preserves the intervals inside a chord");
        }

        {
            std::vector<Note> notes = notesAt({60});
            const TransformResult r = transposeNotes(notes, allIndices(notes), 0);
            check(!r.didAnything(), "transposing by zero is a no-op");
        }

        // Bad indices are survivable - a selection can outlive its notes.
        {
            std::vector<Note> notes = notesAt({60});
            std::vector<int> bogus = {-1, 5, 99};
            const TransformResult r = transposeNotes(notes, bogus, 3);
            check(r.changed == 0, "out-of-range indices are ignored, not crashed on");
            check(notes[0].pitch == 60, "and no note is touched");
        }

        {
            Pattern pattern;
            for (int i = 0; i < 4; ++i) { Note n; n.pitch = 60 + i; pattern.notes.push_back(n); }
            transposePattern(pattern, 7);
            check(pattern.notes[0].pitch == 67, "a whole pattern transposes");
        }
    }

    // ---- Snapping a selection to a scale ---------------------------------
    {
        std::vector<Note> notes;
        for (int pitch : {60, 61, 66, 70}) { Note n; n.pitch = pitch; notes.push_back(n); }
        std::vector<int> idx = {0, 1, 2, 3};

        const TransformResult r = snapNotesToScale(notes, idx, 0, 0);
        check(r.changed == 3, "the three out-of-key notes moved (got " +
              std::to_string(r.changed) + ")");
        for (const Note& n : notes) {
            if (!isNoteInScale(n.pitch, 0, 0)) {
                check(false, "a note survived snapping while still out of key");
                break;
            }
        }
        check(true, "every note ends up in the scale");
    }

    // ---- Mirror and reverse ----------------------------------------------
    {
        std::vector<Note> notes;
        for (int pitch : {60, 64, 67}) { Note n; n.pitch = pitch; notes.push_back(n); }
        std::vector<int> idx = {0, 1, 2};

        invertNotes(notes, idx, 60);
        check(notes[0].pitch == 60, "the centre note is its own mirror");
        check(notes[1].pitch == 56 && notes[2].pitch == 53,
              "pitches reflect around the centre");
    }

    {
        std::vector<Note> notes;
        for (int i = 0; i < 3; ++i) {
            Note n; n.pitch = 60 + i; n.startTime = float(i); n.duration = 1.0f;
            notes.push_back(n);
        }
        std::vector<int> idx = {0, 1, 2};

        reverseNotesInTime(notes, idx);
        check(std::fabs(notes[0].startTime - 2.0f) < 1e-5f,
              "the first note becomes the last");
        check(std::fabs(notes[2].startTime - 0.0f) < 1e-5f,
              "the last note becomes the first");
        check(notes[0].pitch == 60, "reversing time does not change pitch");
    }

    // ---- Probability ------------------------------------------------------
    {
        Note always;
        always.probability = 1.0f;
        check(noteShouldSound(always, 0.0f, 0), "a probability of 1 always sounds");
        check(noteShouldSound(always, 3.5f, 77), "and on every pass");

        Note never;
        never.probability = 0.0f;
        check(!noteShouldSound(never, 0.0f, 0), "a probability of 0 never sounds");
        check(!noteShouldSound(never, 3.5f, 77), "and never on any pass");

        // The note-on and the note-off are handled in different calls
        // thousands of samples apart. If they disagreed, a note would start
        // and never stop.
        Note half;
        half.probability = 0.5f;
        for (int pass = 0; pass < 50; ++pass) {
            if (noteShouldSound(half, 2.0f, uint32_t(pass)) !=
                noteShouldSound(half, 2.0f, uint32_t(pass))) {
                check(false, "the same note and pass gave two different answers");
                break;
            }
        }
        check(true, "the roll is stable for a given note and pass, so note-on and "
                    "note-off cannot disagree");

        // It has to actually vary between passes, or it is not random at all.
        int sounded = 0;
        for (uint32_t pass = 0; pass < 1000; ++pass) {
            if (noteShouldSound(half, 2.0f, pass)) ++sounded;
        }
        check(sounded > 350 && sounded < 650,
              "a 50% note sounds roughly half the time over 1000 passes (got " +
              std::to_string(sounded) + ")");

        // Two notes on the same step must not share a fate.
        Note a; a.probability = 0.5f; a.pitch = 60;
        Note b; b.probability = 0.5f; b.pitch = 67;
        int differed = 0;
        for (uint32_t pass = 0; pass < 200; ++pass) {
            if (noteShouldSound(a, 0.0f, pass) != noteShouldSound(b, 0.0f, pass)) {
                ++differed;
            }
        }
        check(differed > 20,
              "two notes on the same beat roll independently (differed " +
              std::to_string(differed) + " times in 200)");

        Note broken;
        broken.probability = std::numeric_limits<float>::quiet_NaN();
        check(noteShouldSound(broken, 0.0f, 0),
              "a NaN probability falls back to always sounding, not never");
    }

    // ---- Probability survives a save/load round trip -----------------------
    {
        Project original;
        Pattern pattern;
        Note n;
        n.pitch = 64;
        n.probability = 0.35f;
        pattern.notes.push_back(n);
        original.patterns.clear();
        original.patterns.push_back(pattern);

        const std::string path = testPath("probability_roundtrip.ctp");
        check(saveProjectFile(original, path), "a project with probability saves");

        Project loaded;
        check(loadProjectFile(loaded, path), "and loads again");
        check(!loaded.patterns.empty() && !loaded.patterns[0].notes.empty() &&
              std::fabs(loaded.patterns[0].notes[0].probability - 0.35f) < 0.01f,
              "note probability survives the round trip - a field the loader "
              "forgot would silently make every note certain again");
        std::remove(path.c_str());
    }

    // ---- A note that never plays is silent --------------------------------
    {
        auto renderWithProbability = [](float probability) {
            Project p;
            p.bpm = 120.0f;
            Pattern pat;
            Note n;
            n.pitch = 69;
            n.startTime = 0.0f;
            n.duration = 1.0f;
            n.oscillatorType = OscillatorType::Pulse;
            n.probability = probability;
            pat.notes.push_back(n);
            p.patterns.clear();
            p.patterns.push_back(pat);
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            Sequencer& seq = *seqPtr;
            seq.setSampleRate(44100.0f);
            seq.setProject(&p);
            seq.play();

            std::vector<float> l(512), r(512);
            double energy = 0.0;
            for (int b = 0; b < 60; ++b) {
                seq.process(l.data(), r.data(), 512);
                for (float v : l) energy += double(v) * double(v);
            }
            return energy;
        };

        const double certain = renderWithProbability(1.0f);
        const double never = renderWithProbability(0.0f);

        check(certain > 1.0, "a certain note sounds (energy " +
              std::to_string(certain) + ")");
        check(never < certain * 0.01,
              "a note with zero probability is silent - probability reaches the "
              "audio, not just the file (energy " + std::to_string(never) + ")");
    }
}

// ============================================================================
// 32. Cross-channel ghost notes
// ============================================================================
static void testGhostNotes() {
    beginTest("Cross-channel ghost notes");

    // A Pattern has no channel and neither does a Note - a channel is bound
    // only when a Clip places a pattern on the timeline. So "what the other
    // channels are playing" is a question about the arrangement, and it has
    // no answer until the pattern has been placed. These tests pin that down,
    // because the alternative is a feature that silently shows nothing.

    auto makeProject = []() {
        Project p;
        p.patterns.clear();

        // Pattern 0: the one being edited, two notes.
        Pattern lead;
        for (int i = 0; i < 2; ++i) {
            Note n; n.pitch = 72 + i; n.startTime = float(i); n.duration = 1.0f;
            lead.notes.push_back(n);
        }
        p.patterns.push_back(lead);

        // Pattern 1: a bassline, three notes.
        Pattern bass;
        for (int i = 0; i < 3; ++i) {
            Note n; n.pitch = 40 + i; n.startTime = float(i); n.duration = 0.5f;
            bass.notes.push_back(n);
        }
        p.patterns.push_back(bass);

        p.arrangement.clear();
        return p;
    };

    // ---- A pattern that is not on the timeline has no neighbours ---------
    {
        Project p = makeProject();
        check(collectGhostNotes(p, 0).empty(),
              "an unplaced pattern yields no ghosts rather than inventing some");
    }

    // ---- Nonsense indices ------------------------------------------------
    {
        Project p = makeProject();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        check(collectGhostNotes(p, -1).empty(), "a negative pattern index is safe");
        check(collectGhostNotes(p, 99).empty(), "an out-of-range pattern index is safe");
    }

    // ---- The basic case --------------------------------------------------
    {
        Project p = makeProject();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});   // lead on ch 0
        p.arrangement.push_back(Clip{1, 3, 0.0f, 4.0f, 0});   // bass on ch 3

        const std::vector<GhostNote> ghosts = collectGhostNotes(p, 0);
        check(ghosts.size() == 3,
              "the bassline's three notes show as ghosts (got " +
              std::to_string(ghosts.size()) + ")");
        check(ghosts[0].pitch == 40, "ghost pitches come from the other pattern");
        check(ghosts[0].channelIndex == 3,
              "each ghost remembers its channel, so it can be drawn in that colour");
    }

    // ---- A clip on the same channel is not a neighbour -------------------
    {
        Project p = makeProject();
        p.arrangement.push_back(Clip{0, 2, 0.0f, 4.0f, 0});
        p.arrangement.push_back(Clip{1, 2, 0.0f, 4.0f, 0});   // same channel

        check(collectGhostNotes(p, 0).empty(),
              "a clip on the same channel cannot sound at the same time, so it "
              "is not something to write against");
    }

    // ---- Clips elsewhere in the song are not neighbours ------------------
    {
        Project p = makeProject();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p.arrangement.push_back(Clip{1, 3, 64.0f, 4.0f, 0});  // far later

        check(collectGhostNotes(p, 0).empty(),
              "a clip that does not overlap in time contributes no ghosts");
    }

    // ---- Offsets are translated into the edited pattern's timebase -------
    //
    // The piano roll draws in pattern-local beats, so a neighbouring clip
    // that starts two beats later has to have its notes shifted, or the
    // ghosts would line up with the wrong beat and be actively misleading.
    {
        Project p = makeProject();
        p.arrangement.push_back(Clip{0, 0, 8.0f, 4.0f, 0});   // lead at beat 8
        p.arrangement.push_back(Clip{1, 3, 10.0f, 4.0f, 0});  // bass at beat 10

        const std::vector<GhostNote> ghosts = collectGhostNotes(p, 0);
        check(!ghosts.empty(), "the overlapping clip produces ghosts");
        check(std::fabs(ghosts[0].startTime - 2.0f) < 1e-5f,
              "a neighbour starting two beats later is drawn two beats in (got " +
              std::to_string(ghosts[0].startTime) + ")");
    }

    // ---- A neighbour that begins earlier reads as negative ---------------
    {
        Project p = makeProject();
        p.arrangement.push_back(Clip{0, 0, 8.0f, 4.0f, 0});
        // 7.75, not 7.5: the bass notes are half a beat long, so at 7.5 the
        // first one would end exactly on beat 0 and be correctly discarded as
        // having no visible extent. That would test the filter, not the offset.
        p.arrangement.push_back(Clip{1, 3, 7.75f, 4.0f, 0});  // starts earlier

        const std::vector<GhostNote> ghosts = collectGhostNotes(p, 0);
        check(!ghosts.empty(), "an earlier-starting neighbour still overlaps");
        check(ghosts[0].startTime < 0.0f,
              "a note that began before this pattern did has a negative position, "
              "rather than being clamped onto beat zero");
    }

    // ---- Notes outside the visible window are dropped --------------------
    {
        Project p = makeProject();
        Pattern late;
        Note n; n.pitch = 50; n.startTime = 30.0f; n.duration = 1.0f;
        late.notes.push_back(n);
        p.patterns.push_back(late);                           // pattern 2

        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p.arrangement.push_back(Clip{2, 3, 0.0f, 40.0f, 0});  // overlaps, but
                                                              // its note is at 30
        check(collectGhostNotes(p, 0).empty(),
              "a note past the end of the edited window is not collected");
    }

    // ---- The result is bounded -------------------------------------------
    //
    // A dense song could otherwise build thousands of ghosts every frame.
    {
        Project p;
        p.patterns.clear();
        p.patterns.push_back(Pattern());          // pattern 0, edited

        Pattern crowded;
        for (int i = 0; i < 200; ++i) {
            Note n; n.pitch = 40 + (i % 30); n.startTime = float(i) * 0.01f;
            n.duration = 0.1f;
            crowded.notes.push_back(n);
        }
        p.patterns.push_back(crowded);

        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 16.0f, 0});
        for (int ch = 1; ch < 8; ++ch) {
            p.arrangement.push_back(Clip{1, ch, 0.0f, 16.0f, 0});
        }

        const std::vector<GhostNote> ghosts = collectGhostNotes(p, 0, 64);
        check(ghosts.size() <= 64,
              "the ghost list respects its cap (got " +
              std::to_string(ghosts.size()) + ")");
        check(!ghosts.empty(), "and still returns something useful");
    }

    // ---- Only the first placement counts ---------------------------------
    //
    // A pattern used in three places has three sets of neighbours. Drawing
    // all of them at once would be a pile of overlapping ghosts, so the
    // first placement wins.
    {
        Project p = makeProject();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p.arrangement.push_back(Clip{0, 0, 16.0f, 4.0f, 0});  // same pattern again
        p.arrangement.push_back(Clip{1, 3, 0.0f, 4.0f, 0});   // neighbour of the first
        p.arrangement.push_back(Clip{1, 4, 16.0f, 4.0f, 0});  // neighbour of the second

        const std::vector<GhostNote> ghosts = collectGhostNotes(p, 0);
        for (const GhostNote& ghost : ghosts) {
            if (ghost.channelIndex == 4) {
                check(false, "ghosts leaked in from a second placement of the pattern");
                break;
            }
        }
        check(true, "only the first placement's neighbours are shown");
    }
}

// ============================================================================
// 33. Non-linear chip mixing and the console output filters
// ============================================================================
static void testChipMix() {
    beginTest("Non-linear chip mixing");

    // The numbers below are not ours to choose - they are what the NESdev
    // mixer formulas produce, and they are the whole reason for the feature.
    // If a refactor changes them, the mix has stopped matching hardware.

    // ---- The curves reproduce the documented ratios ----------------------
    {
        const float onePulse = nesPulseCurve(15.0f);
        const float twoPulses = nesPulseCurve(30.0f);

        check(std::fabs(twoPulses / onePulse - 1.73f) < 0.02f,
              "two pulses at full volume are 1.73x one pulse, not 2x (got " +
              std::to_string(twoPulses / onePulse) + ")");

        // The triangle sits considerably louder against the pulses than a
        // linear sum makes it. This is why the bass "sits wrong" in
        // emulations that mix linearly.
        const float triangle = nesTndCurve(15.0f, 0.0f);
        check(std::fabs(triangle / onePulse - 1.65f) < 0.02f,
              "a full triangle is 1.65x a full pulse (got " +
              std::to_string(triangle / onePulse) + ")");

        // Channels duck each other: adding noise makes the triangle quieter,
        // which is compression a linear mixer simply does not have.
        const float noiseOnly = nesTndCurve(0.0f, 15.0f);
        const float together = nesTndCurve(15.0f, 15.0f);
        const float ratio = together / (triangle + noiseOnly);
        check(std::fabs(ratio - 0.887f) < 0.01f,
              "triangle plus noise is 88.7% of their separate sum (got " +
              std::to_string(ratio * 100.0f) + "%)");

        check(nesPulseCurve(0.0f) == 0.0f, "silence in, silence out");
        check(nesTndCurve(0.0f, 0.0f) == 0.0f, "an empty tnd group is silent");
        check(nesPulseCurve(-5.0f) == 0.0f, "a negative level cannot produce output");
    }

    // ---- Only 2A03 voices are grouped ------------------------------------
    {
        check(chipMixGroupFor(OscillatorType::Pulse) == ChipMixGroup::Pulse,
              "a pulse channel joins the pulse DAC");
        check(chipMixGroupFor(OscillatorType::Triangle) == ChipMixGroup::Triangle,
              "a triangle channel joins the tnd DAC");
        check(chipMixGroupFor(OscillatorType::Noise) == ChipMixGroup::Noise,
              "a noise channel joins the tnd DAC");

        // A 2A03 never had these, so forcing them into a hardware group
        // would be meaningless. They must mix linearly as before.
        check(chipMixGroupFor(OscillatorType::Supersaw) == ChipMixGroup::Linear,
              "a supersaw is not a 2A03 voice and mixes linearly");
        check(chipMixGroupFor(OscillatorType::Sawtooth) == ChipMixGroup::Linear,
              "a sawtooth mixes linearly");
        check(chipMixGroupFor(OscillatorType::Sine) == ChipMixGroup::Linear,
              "a sine mixes linearly");
    }

    // ---- The derived gains -----------------------------------------------
    {
        ChipMixGains gains = computeChipMixGains(0.0f, 0.0f, 0.0f);
        check(std::fabs(gains.pulse - 1.0f) < 1e-5f &&
              std::fabs(gains.tnd - 1.0f) < 1e-5f,
              "empty groups pass at unity, so silence is untouched");

        // One pulse at full magnitude is the reference, so it must pass
        // through unchanged - enabling the mode on a single pulse channel
        // should not alter its level at all.
        gains = computeChipMixGains(1.0f, 0.0f, 0.0f);
        check(std::fabs(gains.pulse - 1.0f) < 0.01f,
              "a lone full pulse passes at unity (got " +
              std::to_string(gains.pulse) + ")");

        // Two of them duck each other to 1.73x total rather than 2x.
        gains = computeChipMixGains(2.0f, 0.0f, 0.0f);
        const float twoPulseTotal = gains.pulse * 2.0f;
        check(std::fabs(twoPulseTotal - 1.73f) < 0.02f,
              "two pulses together reach 1.73x, not 2x (got " +
              std::to_string(twoPulseTotal) + ")");
        check(gains.pulse < 1.0f, "so each individual pulse is turned down");

        gains = computeChipMixGains(0.0f, 1.0f, 0.0f);
        check(std::fabs(gains.tnd - 1.65f) < 0.02f,
              "a lone triangle comes out 1.65x a pulse (got " +
              std::to_string(gains.tnd) + ")");

        // Adding noise has to reduce the triangle's share.
        const ChipMixGains alone = computeChipMixGains(0.0f, 1.0f, 0.0f);
        const ChipMixGains withNoise = computeChipMixGains(0.0f, 1.0f, 1.0f);
        check(withNoise.tnd < alone.tnd,
              "adding noise ducks the triangle - the automatic compression a "
              "linear mixer does not have");

        // The pulse group and the tnd group are independent DACs.
        const ChipMixGains loud = computeChipMixGains(2.0f, 1.0f, 0.0f);
        check(std::fabs(loud.tnd - alone.tnd) < 1e-4f,
              "loud pulses do not duck the triangle - they are separate DACs");
    }

    // ---- Garbage must never reach the audio thread as NaN ----------------
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        ChipMixGains gains = computeChipMixGains(nan, nan, nan);
        check(std::isfinite(gains.pulse) && std::isfinite(gains.tnd),
              "a NaN level yields finite gains rather than silencing everything");

        gains = computeChipMixGains(1e9f, 1e9f, 1e9f);
        check(std::isfinite(gains.pulse) && std::isfinite(gains.tnd),
              "an absurd level yields finite gains");
    }
}

static void testChipFilters() {
    beginTest("Console output filters");

    auto measure = [](ChipFilterChain& chain, float frequencyHz, float sampleRate) {
        // Settle first: a high-pass needs time before its output is
        // representative, and measuring the transient would test nothing.
        const int settle = static_cast<int>(sampleRate * 0.25f);
        const int window = static_cast<int>(sampleRate * 0.25f);
        double sum = 0.0;
        for (int i = 0; i < settle + window; ++i) {
            const float phase = 2.0f * 3.14159265f * frequencyHz * float(i) / sampleRate;
            float l = std::sin(phase);
            float r = l;
            chain.process(l, r);
            if (i >= settle) sum += double(l) * double(l);
        }
        return std::sqrt(sum / double(window));
    };

    const float sampleRate = 44100.0f;

    // ---- NES voicing ------------------------------------------------------
    {
        ChipFilterChain chain;

        chain.configure(sampleRate, ChipFilterChain::Mode::NES);
        const float lowEnd = measure(chain, 60.0f, sampleRate);

        chain.configure(sampleRate, ChipFilterChain::Mode::NES);
        const float midRange = measure(chain, 1000.0f, sampleRate);

        chain.configure(sampleRate, ChipFilterChain::Mode::NES);
        const float topEnd = measure(chain, 16000.0f, sampleRate);

        check(midRange > 0.5f,
              "the midrange passes essentially untouched (rms " +
              std::to_string(midRange) + ")");
        check(lowEnd < midRange * 0.5f,
              "60 Hz is well attenuated - the 440 Hz high-pass is the stage "
              "emulations leave out, and its absence is why they sound "
              "bass-heavy (60 Hz rms " + std::to_string(lowEnd) + " vs 1 kHz " +
              std::to_string(midRange) + ")");
        check(topEnd < midRange * 0.8f,
              "16 kHz is rolled off by the 14 kHz low-pass (rms " +
              std::to_string(topEnd) + ")");
    }

    // ---- Famicom voicing --------------------------------------------------
    //
    // One 37 Hz high-pass and no low-pass: fuller and brighter. This is much
    // of why the two machines sound different playing the same music, so the
    // two modes must genuinely differ.
    {
        ChipFilterChain nes, famicom;

        nes.configure(sampleRate, ChipFilterChain::Mode::NES);
        famicom.configure(sampleRate, ChipFilterChain::Mode::Famicom);
        const float nesLow = measure(nes, 120.0f, sampleRate);
        const float famicomLow = measure(famicom, 120.0f, sampleRate);

        check(famicomLow > nesLow * 1.5f,
              "a Famicom keeps far more low end than an NES (120 Hz: Famicom " +
              std::to_string(famicomLow) + " vs NES " + std::to_string(nesLow) + ")");

        nes.configure(sampleRate, ChipFilterChain::Mode::NES);
        famicom.configure(sampleRate, ChipFilterChain::Mode::Famicom);
        const float nesTop = measure(nes, 16000.0f, sampleRate);
        const float famicomTop = measure(famicom, 16000.0f, sampleRate);

        check(famicomTop > nesTop * 1.2f,
              "and far more top end, having no low-pass at all (16 kHz: Famicom " +
              std::to_string(famicomTop) + " vs NES " + std::to_string(nesTop) + ")");
    }

    // ---- DC is removed ----------------------------------------------------
    {
        ChipFilterChain chain;
        chain.configure(sampleRate, ChipFilterChain::Mode::NES);

        float last = 1.0f;
        for (int i = 0; i < 44100; ++i) {
            float l = 1.0f, r = 1.0f;
            chain.process(l, r);
            last = l;
        }
        check(std::fabs(last) < 0.01f,
              "a constant offset is filtered away rather than eating headroom "
              "(settled at " + std::to_string(last) + ")");
    }

    // ---- Hygiene ----------------------------------------------------------
    {
        ChipFilterChain chain;
        chain.configure(sampleRate, ChipFilterChain::Mode::NES);

        float l = std::numeric_limits<float>::quiet_NaN();
        float r = std::numeric_limits<float>::infinity();
        chain.process(l, r);
        check(std::isfinite(l) && std::isfinite(r),
              "a poisoned sample does not become a permanently NaN filter state");

        // And the filter still works afterwards.
        for (int i = 0; i < 1000; ++i) {
            float a = 0.5f, b = 0.5f;
            chain.process(a, b);
            if (!std::isfinite(a)) {
                check(false, "the filter state stayed poisoned after a NaN");
                break;
            }
        }
        check(true, "the filter recovers and keeps producing finite output");

        chain.reset();
        float a = 0.0f, b = 0.0f;
        chain.process(a, b);
        check(a == 0.0f && b == 0.0f, "reset clears the filter state");
    }
}

static void testChipMixReachesAudio() {
    beginTest("Chip mixing reaches the audio");

    // A gain table that nothing applies is worth nothing. These render real
    // audio and compare levels.

    auto renderPulses = [](int channelCount, bool chipMix) {
        Project p;
        p.bpm = 120.0f;
        // Master processing off: a limiter would flatten the very difference
        // being measured.
        p.masterLimiterEnabled = false;
        p.masterCompressorEnabled = false;
        p.masterEQEnabled = false;
        // The chip gain is computed from the channel level, before master
        // volume - so the channels run at full scale to reach the part of
        // the DAC curve the 1.73x figure describes, and the headroom is
        // taken back here instead. That keeps the final soft clip in its
        // linear region, where it cannot flatten the difference being
        // measured.
        p.masterVolume = 0.15f;
        p.chipMixEnabled = chipMix;

        Pattern pat;
        Note n;
        n.pitch = 60;
        n.startTime = 0.0f;
        n.duration = 4.0f;
        n.oscillatorType = OscillatorType::Pulse;
        pat.notes.push_back(n);
        p.patterns.clear();
        p.patterns.push_back(pat);

        p.arrangement.clear();
        for (int ch = 0; ch < channelCount; ++ch) {
            p.channels[ch].oscillator.type = OscillatorType::Pulse;
            // The duty cycle has to match too. Project's constructor gives
            // channel 0 a 50% pulse and channel 1 a 25% one, and two
            // different waveforms do not sum coherently - so without this the
            // ratio measures waveform correlation rather than the mixer.
            p.channels[ch].oscillator.pulseWidth = 0.5f;
            p.channels[ch].volume = 1.0f;
            p.channels[ch].pan = 0.0f;
            p.arrangement.push_back(Clip{0, ch, 0.0f, 4.0f, 0});
        }

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.updateChannelConfigs();
        seq.updateMasterEffects();
        seq.play();

        std::vector<float> l(512), r(512);
        double sum = 0.0;
        int counted = 0;
        for (int b = 0; b < 60; ++b) {
            seq.process(l.data(), r.data(), 512);
            if (b >= 10) {                       // let the envelope settle
                for (float v : l) { sum += double(v) * double(v); ++counted; }
            }
        }
        return (counted > 0) ? std::sqrt(sum / counted) : 0.0;
    };

    {
        const double oneLinear = renderPulses(1, false);
        const double twoLinear = renderPulses(2, false);
        const double oneChip = renderPulses(1, true);
        const double twoChip = renderPulses(2, true);

        check(oneLinear > 1e-4, "a single pulse channel renders audibly");

        const double linearRatio = twoLinear / oneLinear;
        const double chipRatio = twoChip / oneChip;

        check(linearRatio > 1.85,
              "linear mixing sums two identical pulses to about 2x (got " +
              std::to_string(linearRatio) + ")");
        check(chipRatio < linearRatio * 0.93,
              "non-linear mixing makes two pulses quieter than their linear sum - "
              "the channels duck each other (linear " + std::to_string(linearRatio) +
              " vs chip " + std::to_string(chipRatio) + ")");
        check(chipRatio > 1.4 && chipRatio < 1.95,
              "and lands near the hardware's 1.73x rather than collapsing (got " +
              std::to_string(chipRatio) + ")");
    }

    // ---- A voice the 2A03 never had is left alone ------------------------
    {
        auto renderSupersaw = [](bool chipMix) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.masterCompressorEnabled = false;
            p.masterEQEnabled = false;
            p.chipMixEnabled = chipMix;

            Pattern pat;
            Note n;
            n.pitch = 60; n.startTime = 0.0f; n.duration = 4.0f;
            n.oscillatorType = OscillatorType::Supersaw;
            pat.notes.push_back(n);
            p.patterns.clear();
            p.patterns.push_back(pat);

            p.channels[0].oscillator.type = OscillatorType::Supersaw;
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            Sequencer& seq = *seqPtr;
            seq.setSampleRate(44100.0f);
            seq.setProject(&p);
            seq.updateChannelConfigs();
            seq.updateMasterEffects();
            seq.play();

            std::vector<float> out, l(512), r(512);
            for (int b = 0; b < 30; ++b) {
                seq.process(l.data(), r.data(), 512);
                out.insert(out.end(), l.begin(), l.end());
            }
            return out;
        };

        const auto plain = renderSupersaw(false);
        const auto chipped = renderSupersaw(true);

        double difference = 0.0;
        const size_t n = std::min(plain.size(), chipped.size());
        for (size_t i = 0; i < n; ++i) difference += std::fabs(plain[i] - chipped[i]);

        check(difference < 1e-3,
              "a supersaw channel is bit-identical with the mode on or off - it "
              "is not a 2A03 voice and must not be forced into a hardware group "
              "(difference " + std::to_string(difference) + ")");
    }

    // ---- The settings survive a save and load ----------------------------
    {
        Project original;
        original.chipMixEnabled = true;
        original.chipFilterEnabled = true;
        original.chipFilterFamicom = true;

        const std::string path = testPath("chipmix_roundtrip.ctp");
        check(saveProjectFile(original, path), "the chip settings save");

        Project loaded;
        check(loadProjectFile(loaded, path), "and load again");
        check(loaded.chipMixEnabled && loaded.chipFilterEnabled &&
              loaded.chipFilterFamicom,
              "all three chip settings survive the round trip");
        std::remove(path.c_str());
    }

    // ---- Off by default ---------------------------------------------------
    {
        const Project fresh;
        check(!fresh.chipMixEnabled && !fresh.chipFilterEnabled,
              "both are off in a new project - most channels here host voices a "
              "2A03 never had, so neither is imposed on anyone");
    }
}

// ============================================================================
// 36. Tracker grid
// ============================================================================
static void testTrackerGrid() {
    beginTest("Tracker grid");

    // Two patterns on two channels, so the columns can be told apart. The
    // old view could not: it searched the selected pattern for the first
    // note matching the step and printed it into all eight columns.
    auto makeSong = []() {
        Project p;
        p.beatsPerMeasure = 4;
        p.songLength = 16.0f;
        p.patterns.clear();

        Pattern lead;                       // pattern 0
        lead.name = "Lead";
        lead.length = 4;
        for (int i = 0; i < 4; ++i) {
            Note n;
            n.pitch = 72 + i;
            n.startTime = float(i);
            n.duration = 0.25f;
            n.oscillatorType = OscillatorType::Pulse;
            lead.notes.push_back(n);
        }
        p.patterns.push_back(lead);

        Pattern bass;                       // pattern 1
        bass.name = "Bass";
        bass.length = 4;
        for (int i = 0; i < 2; ++i) {
            Note n;
            n.pitch = 36 + i;
            n.startTime = float(i) * 2.0f;
            n.duration = 0.25f;
            n.oscillatorType = OscillatorType::Triangle;
            bass.notes.push_back(n);
        }
        p.patterns.push_back(bass);

        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});   // lead on ch 0
        p.arrangement.push_back(Clip{1, 2, 0.0f, 4.0f, 0});   // bass on ch 2
        return p;
    };

    const float STEP = 0.25f;               // one row per 1/16 note

    // ---- The regression this whole file exists for -----------------------
    {
        const Project p = makeSong();

        const TrackerCell ch0 = readTrackerCell(p, 0, 0.0f, STEP);
        const TrackerCell ch2 = readTrackerCell(p, 2, 0.0f, STEP);

        check(ch0.hasNote && ch0.pitch == 72,
              "channel 0 shows its own pattern's note");
        check(ch2.hasNote && ch2.pitch == 36,
              "channel 2 shows a different note - the old view printed the same "
              "note into every column, ignoring channel entirely");
        check(ch0.pitch != ch2.pitch, "the two columns genuinely differ");

        // A channel with nothing placed on it must be empty, not a copy of
        // whatever channel 0 happens to hold.
        const TrackerCell ch5 = readTrackerCell(p, 5, 0.0f, STEP);
        check(!ch5.hasNote && ch5.patternIndex < 0,
              "a channel with no clip shows nothing at all");
    }

    // ---- Resolving which pattern is playing ------------------------------
    {
        const Project p = makeSong();

        TrackerSlot slot = resolveTrackerSlot(p, 0, 2.0f);
        check(slot.valid && slot.patternIndex == 0,
              "the clip covering this beat is found");
        check(std::fabs(slot.localBeat - 2.0f) < 1e-5f,
              "and the beat is translated into the pattern's own timebase");

        check(!resolveTrackerSlot(p, 0, 8.0f).valid,
              "a beat past the end of every clip resolves to nothing");
        check(!resolveTrackerSlot(p, 1, 0.0f).valid,
              "a channel with no clip resolves to nothing");
        check(!resolveTrackerSlot(p, -1, 0.0f).valid, "a negative channel is safe");
        check(!resolveTrackerSlot(p, 99, 0.0f).valid, "an absurd channel is safe");
        check(!resolveTrackerSlot(p, 0,
                  std::numeric_limits<float>::quiet_NaN()).valid,
              "a NaN beat is safe");

        // Half-open in time: the clip owns its start and not its end.
        check(resolveTrackerSlot(p, 0, 0.0f).valid, "the clip owns its first beat");
        check(!resolveTrackerSlot(p, 0, 4.0f).valid,
              "and not the beat where it ends, which belongs to whatever is next");
    }

    // ---- Overlapping clips ------------------------------------------------
    {
        Project p = makeSong();
        p.arrangement.push_back(Clip{1, 0, 0.0f, 4.0f, 0});   // over the lead

        const TrackerSlot slot = resolveTrackerSlot(p, 0, 0.0f);
        check(slot.valid && slot.patternIndex == 1,
              "where two clips overlap on a channel the later one is shown - the "
              "grid can only draw one, and the newest is the better guess");
    }

    // ---- A row owns a half-open span -------------------------------------
    {
        Pattern pattern;
        Note a; a.pitch = 60; a.startTime = 0.0f;   pattern.notes.push_back(a);
        Note b; b.pitch = 62; b.startTime = 0.25f;  pattern.notes.push_back(b);

        check(findNoteInStep(pattern, 0.0f, 0.25f) == 0,
              "the first row holds the note at its start");
        check(findNoteInStep(pattern, 0.25f, 0.25f) == 1,
              "a note exactly on the boundary belongs to the later row, so no "
              "note is ever shown in two places");
        check(findNoteInStep(pattern, 0.5f, 0.25f) == -1, "an empty row is empty");
        check(findNoteInStep(pattern, 0.0f, 0.0f) == -1,
              "a zero-width row cannot contain anything");
    }

    // ---- Writing -----------------------------------------------------------
    {
        Project p = makeSong();
        const size_t patternsBefore = p.patterns.size();

        const int written = writeTrackerNote(p, 0, 1.25f, STEP, 64,
                                             OscillatorType::Pulse);
        check(written >= 0, "a note can be typed into an existing clip");
        check(p.patterns.size() == patternsBefore,
              "and doing so creates no new pattern");

        const TrackerCell cell = readTrackerCell(p, 0, 1.25f, STEP);
        check(cell.hasNote && cell.pitch == 64, "the note reads back from the grid");
    }

    // ---- Typing over a row replaces it ------------------------------------
    {
        Project p = makeSong();
        const size_t notesBefore = p.patterns[0].notes.size();

        writeTrackerNote(p, 0, 0.0f, STEP, 65, OscillatorType::Pulse);
        check(p.patterns[0].notes.size() == notesBefore,
              "typing over an occupied row replaces rather than stacks - a "
              "column holds one note per row by definition");
        check(readTrackerCell(p, 0, 0.0f, STEP).pitch == 65,
              "and the new pitch is what shows");
    }

    // ---- Typing where nothing is placed builds somewhere to put it --------
    {
        Project p = makeSong();
        const size_t patternsBefore = p.patterns.size();
        const size_t clipsBefore = p.arrangement.size();

        const int written = writeTrackerNote(p, 5, 9.5f, STEP, 60,
                                             OscillatorType::Noise);
        check(written >= 0, "a note can be typed into empty space");
        check(p.patterns.size() == patternsBefore + 1,
              "which creates a pattern to hold it");
        check(p.arrangement.size() == clipsBefore + 1,
              "and a clip to place that pattern on the channel");

        const TrackerCell cell = readTrackerCell(p, 5, 9.5f, STEP);
        check(cell.hasNote && cell.pitch == 60,
              "and the note is then readable at the row it was typed on");

        // The created clip has to be bar-aligned, or the arrangement fills up
        // with clips at arbitrary offsets.
        const Clip& created = p.arrangement.back();
        check(std::fabs(created.startBeat - 8.0f) < 1e-5f,
              "the created clip snaps to the bar (started at " +
              std::to_string(created.startBeat) + ")");
        check(created.channelIndex == 5, "on the channel that was typed into");
    }

    // ---- A note past the pattern's stated end extends it ------------------
    //
    // Otherwise it is written, saved, and never heard.
    {
        Project p = makeSong();
        p.patterns[0].length = 1;

        // 3.5, not 3.0: the lead already has a note on every beat, and
        // writing onto an occupied row replaces it and returns early - which
        // would test the replace path rather than the growth.
        writeTrackerNote(p, 0, 3.5f, STEP, 70, OscillatorType::Pulse);
        check(p.patterns[0].length > 1,
              "the pattern grows to contain a note typed past its end (length " +
              std::to_string(p.patterns[0].length) + ")");
    }

    // ---- Refusals ---------------------------------------------------------
    {
        Project p = makeSong();
        check(writeTrackerNote(p, -1, 0.0f, STEP, 60, OscillatorType::Pulse) < 0,
              "a negative channel is refused");
        check(writeTrackerNote(p, 99, 0.0f, STEP, 60, OscillatorType::Pulse) < 0,
              "an out-of-range channel is refused");
        check(writeTrackerNote(p, 0, 0.0f, STEP, -5, OscillatorType::Pulse) < 0,
              "an impossible pitch is refused");
        check(writeTrackerNote(p, 0, 0.0f, STEP, 900, OscillatorType::Pulse) < 0,
              "a pitch above MIDI range is refused");
        check(writeTrackerNote(p, 0, -1.0f, STEP, 60, OscillatorType::Pulse) < 0,
              "a negative beat is refused");
        check(writeTrackerNote(p, 0, 0.0f, 0.0f, 60, OscillatorType::Pulse) < 0,
              "a zero-width row is refused");
    }

    // ---- The pattern budget is finite -------------------------------------
    {
        Project p;
        p.patterns.clear();
        p.arrangement.clear();
        for (int i = 0; i < Project::MAX_PATTERNS; ++i) p.patterns.push_back(Pattern());

        const int written = writeTrackerNote(p, 3, 100.0f, STEP, 60,
                                             OscillatorType::Pulse);
        check(written < 0,
              "with every pattern slot taken, typing into empty space fails "
              "rather than corrupting the arrangement");
        check(p.patterns.size() == static_cast<size_t>(Project::MAX_PATTERNS),
              "and no pattern is created");
    }

    // ---- Clearing ---------------------------------------------------------
    {
        Project p = makeSong();
        check(clearTrackerNote(p, 0, 0.0f, STEP), "an occupied row can be cleared");
        check(!readTrackerCell(p, 0, 0.0f, STEP).hasNote, "and is then empty");

        check(!clearTrackerNote(p, 0, 0.25f, STEP),
              "clearing an empty row reports that nothing happened, so the "
              "caller need not spend an undo step on it");
        check(!clearTrackerNote(p, 6, 0.0f, STEP),
              "clearing a channel with no clip is harmless");
    }

    // ---- How many rows the grid spans -------------------------------------
    {
        Project p = makeSong();
        check(trackerRowCount(p, 0.25f) == 64,
              "16 beats at a quarter-beat per row is 64 rows (got " +
              std::to_string(trackerRowCount(p, 0.25f)) + ")");
        check(trackerRowCount(p, 1.0f) == 16, "and 16 rows at one beat per row");

        // A clip dragged past the song length must still be reachable, or
        // notes exist that cannot be edited.
        p.arrangement.push_back(Clip{0, 4, 60.0f, 4.0f, 0});
        check(trackerRowCount(p, 1.0f) >= 64,
              "the grid extends to cover a clip beyond the song length (got " +
              std::to_string(trackerRowCount(p, 1.0f)) + ")");

        check(trackerRowCount(p, 0.0f) == 0, "a zero row height yields no rows");
    }

    // ---- What is typed is what plays --------------------------------------
    //
    // The grid writes into the same patterns the sequencer reads, so a note
    // typed into the tracker has to come out of the speakers.
    {
        Project p;
        p.bpm = 120.0f;
        p.masterLimiterEnabled = false;
        p.patterns.clear();
        p.arrangement.clear();
        p.songLength = 8.0f;

        const int written = writeTrackerNote(p, 0, 0.0f, 0.5f, 69,
                                             OscillatorType::Pulse);
        check(written >= 0, "a note is typed into an empty project");

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.updateChannelConfigs();
        seq.play();

        std::vector<float> l(512), r(512);
        double energy = 0.0;
        for (int b = 0; b < 40; ++b) {
            seq.process(l.data(), r.data(), 512);
            for (float v : l) energy += double(v) * double(v);
        }
        check(energy > 1.0,
              "and it sounds - the tracker writes into the same patterns the "
              "sequencer plays (energy " + std::to_string(energy) + ")");
    }
}

// ============================================================================
// 37. Genre focus
// ============================================================================
static void testGenreFocus() {
    beginTest("Genre focus");

    // Genres.h owns the canonical lists, so these tests check the profiles
    // against the same names the UI draws from.
    const int CHORD_COUNT = ALL_CHORD_SET_COUNT;
    const int DRUM_COUNT = ALL_DRUM_SET_COUNT;

    // ---- Everything is the default and hides nothing ---------------------
    {
        const UIState fresh;
        check(fresh.genre == Genre::Everything,
              "a new session has no genre imposed on it");

        for (int i = 0; i < CHORD_COUNT; ++i) {
            if (!genreShowsChordSet(Genre::Everything, ALL_CHORD_SETS[i])) {
                check(false, std::string("Everything hid the chord set ") +
                      ALL_CHORD_SETS[i]);
                break;
            }
        }
        for (int i = 0; i < DRUM_COUNT; ++i) {
            if (!genreShowsDrumCategory(Genre::Everything, ALL_DRUM_SETS[i])) {
                check(false, std::string("Everything hid the drum set ") +
                      ALL_DRUM_SETS[i]);
                break;
            }
        }
        check(true, "Everything shows every palette section");

        check(genreHiddenSectionCount(Genre::Everything, ALL_CHORD_SETS, CHORD_COUNT,
                                      ALL_DRUM_SETS, DRUM_COUNT) == 0,
              "and therefore hides nothing at all");
    }

    // ---- A genre keeps what it needs and sets the rest aside -------------
    {
        check(genreShowsChordSet(Genre::Chiptune, "Chiptune"),
              "chiptune keeps its own chord set");
        check(!genreShowsChordSet(Genre::Chiptune, "Jazz"),
              "and sets jazz voicings aside");
        check(!genreShowsChordSet(Genre::Chiptune, "Reggaeton"),
              "and reggaeton chords");
        check(!genreShowsDrumCategory(Genre::Chiptune, "Reggaeton Drums"),
              "and reggaeton percussion");

        check(genreShowsChordSet(Genre::Reggaeton, "Reggaeton"),
              "reggaeton keeps its own chord set");
        check(genreShowsDrumCategory(Genre::Reggaeton, "Reggaeton Drums"),
              "and its percussion, which is the part that actually matters");
        check(!genreShowsChordSet(Genre::Reggaeton, "Chiptune"),
              "and sets the chiptune set aside");

        // The two must not collapse into the same palette.
        int differences = 0;
        for (int i = 0; i < CHORD_COUNT; ++i) {
            if (genreShowsChordSet(Genre::Chiptune, ALL_CHORD_SETS[i]) !=
                genreShowsChordSet(Genre::Reggaeton, ALL_CHORD_SETS[i])) {
                ++differences;
            }
        }
        check(differences >= 2,
              "two different genres show genuinely different palettes (" +
              std::to_string(differences) + " chord sets differ)");
    }

    // ---- Every genre keeps something, and sets something aside -----------
    //
    // A profile that hid everything, or nothing, would be a mistake rather
    // than a choice.
    {
        for (int i = 1; i < static_cast<int>(Genre::Count); ++i) {
            const Genre genre = static_cast<Genre>(i);

            int shownChords = 0;
            for (int c = 0; c < CHORD_COUNT; ++c) {
                if (genreShowsChordSet(genre, ALL_CHORD_SETS[c])) ++shownChords;
            }
            int shownDrums = 0;
            for (int d = 0; d < DRUM_COUNT; ++d) {
                if (genreShowsDrumCategory(genre, ALL_DRUM_SETS[d])) ++shownDrums;
            }

            if (shownChords == 0 || shownDrums == 0) {
                check(false, std::string(genreName(genre)) +
                      " leaves the palette empty");
                break;
            }
            if (shownChords == CHORD_COUNT && shownDrums == DRUM_COUNT) {
                check(false, std::string(genreName(genre)) +
                      " is indistinguishable from Everything");
                break;
            }
        }
        check(true, "every genre keeps some sections and sets others aside");
    }

    // ---- The hidden count the UI reports is right -------------------------
    {
        const int chiptuneHidden = genreHiddenSectionCount(
            Genre::Chiptune, ALL_CHORD_SETS, CHORD_COUNT, ALL_DRUM_SETS, DRUM_COUNT);
        // Chiptune keeps 3 chord sets and 3 drum categories out of 8 and 7.
        check(chiptuneHidden == (CHORD_COUNT - 3) + (DRUM_COUNT - 3),
              "the count of hidden sections matches the profile (got " +
              std::to_string(chiptuneHidden) + ")");
    }

    // ---- Profiles are sane ------------------------------------------------
    {
        for (int i = 0; i < static_cast<int>(Genre::Count); ++i) {
            const GenreProfile& profile = genreProfile(static_cast<Genre>(i));

            if (profile.name == nullptr || profile.name[0] == '\0') {
                check(false, "a genre has no name"); break;
            }
            if (profile.blurb == nullptr || profile.blurb[0] == '\0') {
                check(false, std::string(profile.name) + " has no description"); break;
            }
            if (!(profile.bpm >= 30.0f && profile.bpm <= 300.0f)) {
                check(false, std::string(profile.name) + " suggests an unusable tempo");
                break;
            }
            if (!(profile.swing >= 0.0f && profile.swing <= 1.0f)) {
                check(false, std::string(profile.name) + " suggests an impossible swing");
                break;
            }
            if (profile.scaleRoot < 0 || profile.scaleRoot > 11) {
                check(false, std::string(profile.name) + " suggests a root outside an octave");
                break;
            }
            if (profile.scaleType < 0 || profile.scaleType >= SCALE_COUNT) {
                check(false, std::string(profile.name) + " suggests a scale that does not exist");
                break;
            }
        }
        check(true, "every profile has a name, a description and usable defaults");
    }


    // ---- The Tools panel is filtered too ---------------------------------
    //
    // This is the panel actually named "tools", and it is what the request
    // was about. Chiptune wants an arpeggiator and nothing humanised; hip
    // hop wants exactly the reverse.
    {
        check(genreShowsTool(Genre::Chiptune, "Arpeggiator"),
              "chiptune keeps the arpeggiator, which does the work chords cannot");
        check(!genreShowsTool(Genre::Chiptune, "Humanize"),
              "and sets humanize aside, because chiptune is not humanised");

        check(genreShowsTool(Genre::HipHop, "Humanize"),
              "hip hop keeps humanize, where the feel is the point");
        check(!genreShowsTool(Genre::HipHop, "Arpeggiator"),
              "and sets the arpeggiator aside");

        for (int i = 0; i < ALL_TOOL_SECTION_COUNT; ++i) {
            if (!genreShowsTool(Genre::Everything, ALL_TOOL_SECTIONS[i])) {
                check(false, std::string("Everything hid the tool ") +
                      ALL_TOOL_SECTIONS[i]);
                break;
            }
        }
        check(true, "Everything shows every generator");

        check(genreHiddenToolCount(Genre::Everything, ALL_TOOL_SECTIONS,
                                   ALL_TOOL_SECTION_COUNT) == 0,
              "and hides none of them");

        // Every genre must keep some generators and set some aside, or the
        // profile is a mistake rather than a choice.
        for (int i = 1; i < static_cast<int>(Genre::Count); ++i) {
            const Genre genre = static_cast<Genre>(i);
            const int hidden = genreHiddenToolCount(genre, ALL_TOOL_SECTIONS,
                                                    ALL_TOOL_SECTION_COUNT);
            if (hidden == 0 || hidden == ALL_TOOL_SECTION_COUNT) {
                check(false, std::string(genreName(genre)) +
                      " hides all or none of the generators");
                break;
            }
        }
        check(true, "every genre keeps some generators and sets others aside");
    }

    // ---- A profile cannot name something that does not exist -------------
    //
    // A typo would hide a section forever with no error, because the name
    // simply never matches. This is the check that catches it.
    {
        auto knownName = [](const char* const* list, int count, const char* key) {
            for (int i = 0; i < count; ++i) {
                if (std::strcmp(list[i], key) == 0) return true;
            }
            return false;
        };

        bool allKnown = true;
        for (int g = 0; g < static_cast<int>(Genre::Count) && allKnown; ++g) {
            const GenreProfile& profile = genreProfile(static_cast<Genre>(g));

            for (int i = 0; i < GENRE_MAX_CHORDS && profile.chords[i] != nullptr; ++i) {
                if (!knownName(ALL_CHORD_SETS, ALL_CHORD_SET_COUNT, profile.chords[i])) {
                    check(false, std::string(profile.name) +
                          " names a chord set that does not exist: " + profile.chords[i]);
                    allKnown = false;
                    break;
                }
            }
            for (int i = 0; i < GENRE_MAX_DRUMS && allKnown &&
                            profile.drums[i] != nullptr; ++i) {
                if (!knownName(ALL_DRUM_SETS, ALL_DRUM_SET_COUNT, profile.drums[i])) {
                    check(false, std::string(profile.name) +
                          " names a drum set that does not exist: " + profile.drums[i]);
                    allKnown = false;
                    break;
                }
            }
            for (int i = 0; i < GENRE_MAX_TOOLS && allKnown &&
                            profile.tools[i] != nullptr; ++i) {
                if (!knownName(ALL_TOOL_SECTIONS, ALL_TOOL_SECTION_COUNT,
                               profile.tools[i])) {
                    check(false, std::string(profile.name) +
                          " names a generator that does not exist: " + profile.tools[i]);
                    allKnown = false;
                    break;
                }
            }
        }
        check(allKnown,
              "every section a profile names really exists - a typo here would "
              "hide something forever with no error at all");
    }

    // ---- Hygiene ----------------------------------------------------------
    {
        // An out-of-range genre must fall back rather than read past the
        // table - this arrives from a saved layout or a command line.
        const GenreProfile& bogus = genreProfile(static_cast<Genre>(99));
        check(std::string(bogus.name) == "Everything",
              "an unknown genre falls back to Everything");

        const GenreProfile& negative = genreProfile(static_cast<Genre>(-3));
        check(std::string(negative.name) == "Everything",
              "and so does a negative one");

        check(!genreShowsChordSet(Genre::Chiptune, nullptr),
              "a null section name is refused rather than dereferenced");
        check(!genreShowsChordSet(Genre::Chiptune, "NoSuchSet"),
              "an unknown section name is not shown");
        check(genreShowsChordSet(Genre::Everything, "NoSuchSet"),
              "though Everything shows even sections it has never heard of, "
              "which is what makes it future-proof against new ones");
    }
}

// ============================================================================
// 38. User settings
// ============================================================================
static void testUserSettings() {
    beginTest("User settings");

    const std::string dir = testPath("settings_test");
    ensureDirectoryExists(dir);
    const std::string path = dir + "/" + SETTINGS_FILENAME;
    std::remove(path.c_str());

    // ---- Stable tokens ----------------------------------------------------
    //
    // These strings are part of a file format. Saving the enum value would
    // silently reinterpret an existing setting the moment a genre is
    // inserted in the middle of the list, and saving the display name would
    // break the moment one is reworded.
    {
        for (int i = 0; i < static_cast<int>(Genre::Count); ++i) {
            const Genre genre = static_cast<Genre>(i);
            if (genreFromKey(genreKey(genre)) != genre) {
                check(false, std::string("the token for ") + genreName(genre) +
                      " does not read back as itself");
                break;
            }
        }
        check(true, "every genre's token reads back as that genre");

        // Tokens must be distinct, or two genres collapse into one on load.
        bool distinct = true;
        for (int a = 0; a < static_cast<int>(Genre::Count) && distinct; ++a) {
            for (int b = a + 1; b < static_cast<int>(Genre::Count); ++b) {
                if (std::strcmp(genreKey(static_cast<Genre>(a)),
                                genreKey(static_cast<Genre>(b))) == 0) {
                    check(false, "two genres share a saved token");
                    distinct = false;
                    break;
                }
            }
        }
        check(distinct, "every genre's token is distinct");

        check(genreFromKey("chiptune") == Genre::Chiptune,
              "a known token loads the genre it names");
        check(genreFromKey("dubstep") == Genre::Everything,
              "an unknown token falls back to showing everything, rather than "
              "hiding tools for a reason the user cannot see");
        check(genreFromKey(nullptr) == Genre::Everything,
              "a null token is safe");
        check(genreFromKey("") == Genre::Everything, "an empty token is safe");
    }

    // ---- A missing file is the normal first run --------------------------
    {
        UserSettings settings;
        check(!loadSettings(settings, path),
              "a missing settings file reports that it was not read");
        check(!settings.welcomed,
              "and leaves the defaults, so a first run asks the question");
        check(settings.genre == Genre::Everything, "with no genre assumed");
    }

    // ---- Round trip -------------------------------------------------------
    {
        UserSettings saved;
        saved.welcomed = true;
        saved.genre = Genre::Reggaeton;
        saved.applyGenreDefaults = true;
        check(saveSettings(saved, path), "settings save");

        UserSettings loaded;
        check(loadSettings(loaded, path), "and load again");
        check(loaded.welcomed, "the welcome is remembered");
        check(loaded.genre == Genre::Reggaeton, "and so is the genre");
        check(loaded.applyGenreDefaults, "and the defaults answer");
    }

    // ---- Choosing Everything is a real answer -----------------------------
    //
    // If "welcomed" were inferred from the genre, the one person who wants
    // every tool would be asked again on every launch, forever.
    {
        UserSettings saved;
        saved.welcomed = true;
        saved.genre = Genre::Everything;
        check(saveSettings(saved, path), "an Everything choice saves");

        UserSettings loaded;
        check(loadSettings(loaded, path), "and loads");
        check(loaded.welcomed && loaded.genre == Genre::Everything,
              "choosing Everything is remembered as an answer, not as never "
              "having been asked");
    }

    // ---- A damaged file must never stop the program starting -------------
    {
        {
            std::ofstream bad(path);
            bad << "# a comment\n";
            bad << "\n";
            bad << "this line has no equals sign\n";
            bad << "welcomed=1\n";
            bad << "genre=chiptune\n";
            bad << "somethingFromTheFuture=42\n";
            bad << "=novalue\n";
            bad << "truncated";
        }

        UserSettings loaded;
        check(loadSettings(loaded, path), "a file with junk in it still reads");
        check(loaded.welcomed && loaded.genre == Genre::Chiptune,
              "and the settings it does understand survive the junk around them");
    }

    {
        std::ofstream empty(path);
        empty.close();

        UserSettings loaded;
        loadSettings(loaded, path);
        check(!loaded.welcomed && loaded.genre == Genre::Everything,
              "an empty file leaves every default in place");
    }

    // ---- An unwritable target fails quietly -------------------------------
    //
    // The cost of not saving is being asked once more. That is not worth
    // interrupting anyone over.
    {
        UserSettings settings;
        settings.welcomed = true;
        check(!saveSettings(settings, dir + "/does/not/exist/x.ini"),
              "saving into a missing directory reports failure rather than throwing");
        check(true, "and does not crash");
    }

    std::remove(path.c_str());
}

// ============================================================================
// 39. Genre starter templates
// ============================================================================
static void testGenreTemplates() {
    beginTest("Genre starter templates");

    // ---- Reading a rhythm -------------------------------------------------
    {
        Pattern pattern;
        addStepLine(pattern, "x...x...x...x...", 36, OscillatorType::Kick808);
        check(pattern.notes.size() == 4,
              "four hits are read out of a sixteen-step bar (got " +
              std::to_string(pattern.notes.size()) + ")");
        check(std::fabs(pattern.notes[1].startTime - 1.0f) < 1e-5f,
              "the second hit lands on the second beat");

        Pattern accents;
        addStepLine(accents, "X...x...", 36, OscillatorType::Kick808, 0.5f);
        check(accents.notes.size() == 2, "accents and ordinary hits both count");
        check(accents.notes[0].velocity > accents.notes[1].velocity,
              "an accent is louder than a plain hit");

        Pattern empty;
        addStepLine(empty, "................", 36, OscillatorType::Kick808);
        check(empty.notes.empty(), "a bar of rests produces nothing");

        Pattern safe;
        addStepLine(safe, nullptr, 36, OscillatorType::Kick808);
        check(safe.notes.empty(), "a null rhythm is refused rather than dereferenced");

        // A very long string must not run past the note budget.
        Pattern flooded;
        std::string many(Pattern::MAX_NOTES * 2, 'x');
        addStepLine(flooded, many.c_str(), 36, OscillatorType::Kick808);
        check(static_cast<int>(flooded.notes.size()) <= Pattern::MAX_NOTES,
              "the note budget is respected (got " +
              std::to_string(flooded.notes.size()) + ")");
    }

    // ---- Every genre produces something that plays -----------------------
    {
        for (int i = 0; i < static_cast<int>(Genre::Count); ++i) {
            const Genre genre = static_cast<Genre>(i);
            const Project project = makeGenreTemplate(genre);
            const std::string label = genreName(genre);

            if (project.patterns.size() != 4) {
                check(false, label + " did not produce four patterns");
                break;
            }
            if (project.arrangement.size() != 7) {
                check(false, label + " did not place seven clips (four drums "
                      "plus lead, chords and bass)");
                break;
            }

            bool everyPatternHasNotes = true;
            for (const Pattern& pattern : project.patterns) {
                if (pattern.notes.empty()) {
                    check(false, label + " left the pattern '" + pattern.name +
                          "' empty");
                    everyPatternHasNotes = false;
                    break;
                }
            }
            if (!everyPatternHasNotes) break;

            // Every note must be playable, or the template is silent in
            // places for no visible reason.
            bool inRange = true;
            for (const Pattern& pattern : project.patterns) {
                for (const Note& note : pattern.notes) {
                    if (note.pitch < 0 || note.pitch > 127) {
                        check(false, label + " wrote an unplayable pitch " +
                              std::to_string(note.pitch));
                        inRange = false;
                        break;
                    }
                }
                if (!inRange) break;
            }
            if (!inRange) break;

            // Clips must point at patterns that exist and channels that do.
            bool clipsValid = true;
            for (const Clip& clip : project.arrangement) {
                if (clip.patternIndex < 0 ||
                    clip.patternIndex >= static_cast<int>(project.patterns.size()) ||
                    clip.channelIndex < 0 ||
                    clip.channelIndex >= Project::MAX_CHANNELS) {
                    check(false, label + " placed a clip pointing nowhere");
                    clipsValid = false;
                    break;
                }
            }
            if (!clipsValid) break;

            if (std::fabs(project.bpm - genreProfile(genre).bpm) > 0.01f) {
                check(false, label + " did not arrive at its own tempo");
                break;
            }
        }
        check(true, "every genre produces four populated patterns, seven valid "
                    "clips and its own tempo");
    }

    // ---- The drums demonstrate pattern reuse ------------------------------
    //
    // One bar placed four times, not a four-bar pattern. Reuse is the thing
    // a starter project is best placed to teach, and it is also how someone
    // gets from four bars to a song.
    {
        const Project project = makeGenreTemplate(Genre::Chiptune);

        int drumsClips = 0;
        int drumsPatternIndex = -1;
        for (size_t i = 0; i < project.patterns.size(); ++i) {
            if (project.patterns[i].name == "Drums") drumsPatternIndex = int(i);
        }
        check(drumsPatternIndex >= 0, "there is a pattern called Drums");

        for (const Clip& clip : project.arrangement) {
            if (clip.patternIndex == drumsPatternIndex) ++drumsClips;
        }
        check(drumsClips == TEMPLATE_BARS,
              "the one drum pattern is placed once per bar (got " +
              std::to_string(drumsClips) + ")");
        check(project.patterns[drumsPatternIndex].length == 4,
              "and it really is one bar long, not four");
    }

    // ---- Channels are named for what they carry ---------------------------
    {
        const Project project = makeGenreTemplate(Genre::Synthwave);
        check(project.channels[0].name == "Lead", "channel 0 is the lead");
        check(project.channels[1].name == "Chords", "channel 1 is the chords");
        check(project.channels[2].name == "Bass", "channel 2 is the bass");
        check(project.channels[3].name == "Drums", "channel 3 is the drums");
        check(project.name.find("Synthwave") != std::string::npos,
              "and the project says which template it came from");
    }

    // ---- Two genres are genuinely different -------------------------------
    {
        const Project chip = makeGenreTemplate(Genre::Chiptune);
        const Project lofi = makeGenreTemplate(Genre::Lofi);

        check(std::fabs(chip.bpm - lofi.bpm) > 20.0f,
              "chiptune and lofi start at very different tempos");
        check(lofi.swing > chip.swing,
              "lofi arrives swung and chiptune does not");
        check(chip.channels[2].oscillator.type != lofi.channels[2].oscillator.type,
              "and they do not use the same bass voice");
    }

    // ---- Deterministic ----------------------------------------------------
    //
    // A template that generated something different each time would be
    // impossible to learn from and impossible to test.
    {
        const Project a = makeGenreTemplate(Genre::EDM);
        const Project b = makeGenreTemplate(Genre::EDM);

        bool identical = (a.patterns.size() == b.patterns.size()) &&
                         (a.arrangement.size() == b.arrangement.size()) &&
                         (std::fabs(a.bpm - b.bpm) < 1e-6f);
        if (identical) {
            for (size_t p = 0; p < a.patterns.size() && identical; ++p) {
                if (a.patterns[p].notes.size() != b.patterns[p].notes.size()) {
                    identical = false;
                    break;
                }
                for (size_t n = 0; n < a.patterns[p].notes.size(); ++n) {
                    if (a.patterns[p].notes[n].pitch != b.patterns[p].notes[n].pitch) {
                        identical = false;
                        break;
                    }
                }
            }
        }
        check(identical, "building the same template twice gives the same music");
    }

    // ---- An unknown genre still gives something usable --------------------
    {
        const Project project = makeGenreTemplate(static_cast<Genre>(99));
        check(!project.patterns.empty() && !project.arrangement.empty(),
              "an out-of-range genre falls back to a usable template rather "
              "than an empty project");
    }

    // ---- It actually sounds -----------------------------------------------
    //
    // The point of a starting point is that it plays the moment it loads.
    {
        Project project = makeGenreTemplate(Genre::Reggaeton);
        project.masterLimiterEnabled = false;

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&project);
        seq.updateChannelConfigs();
        seq.play();

        std::vector<float> l(512), r(512);
        double energy = 0.0;
        for (int b = 0; b < 120; ++b) {
            seq.process(l.data(), r.data(), 512);
            for (float v : l) energy += double(v) * double(v);
        }
        check(energy > 1.0,
              "a freshly loaded template plays without anyone touching anything "
              "(energy " + std::to_string(energy) + ")");
    }

    // ---- And survives a save and load -------------------------------------
    {
        const Project original = makeGenreTemplate(Genre::HipHop);
        const std::string path = testPath("template_roundtrip.ctp");
        check(saveProjectFile(original, path), "a template saves");

        Project loaded;
        check(loadProjectFile(loaded, path), "and loads");
        check(loaded.patterns.size() == original.patterns.size(),
              "with all its patterns");
        check(loaded.arrangement.size() == original.arrangement.size(),
              "and all its clips");
        check(std::fabs(loaded.bpm - original.bpm) < 0.01f, "and its tempo");
        std::remove(path.c_str());
    }
}

// ============================================================================
// 40. Genre kits
// ============================================================================
static void testGenreKits() {
    beginTest("Genre kits");

    const KitVoices voices;

    // ---- Every recipe is well formed --------------------------------------
    {
        int count = 0;
        const KitRecipe* recipes = kitRecipes(count);
        check(count > 0, "there are recipes at all");

        for (int i = 0; i < count; ++i) {
            const KitRecipe& recipe = recipes[i];
            if (recipe.name == nullptr || recipe.name[0] == '\0') {
                check(false, "a recipe has no name"); break;
            }
            if (recipe.description == nullptr || recipe.description[0] == '\0') {
                check(false, std::string(recipe.name) + " has no description");
                break;
            }

            // Every recipe must actually produce notes when applied.
            Pattern pattern;
            const int added = applyKitRecipe(pattern, recipe, 0.0f, 4, 60, voices);
            if (added <= 0) {
                check(false, std::string(recipe.name) + " produced nothing");
                break;
            }
            if (static_cast<int>(pattern.notes.size()) != added) {
                check(false, std::string(recipe.name) +
                      " miscounted what it added");
                break;
            }
            for (const Note& note : pattern.notes) {
                if (note.pitch < 0 || note.pitch > 127) {
                    check(false, std::string(recipe.name) +
                          " wrote an unplayable pitch");
                    break;
                }
            }
        }
        check(true, "every recipe has a name, a description, and writes "
                    "playable notes");
    }

    // ---- The dembow is the dembow -----------------------------------------
    //
    // The 3-3-2 snare grouping is what makes reggaeton sound like reggaeton;
    // if an edit ever flattens it into a backbeat, this is the test that says
    // so in words rather than as a subtle wrongness in the audio.
    {
        int count = 0;
        const KitRecipe* recipes = kitRecipes(count);
        const KitRecipe* dembow = nullptr;
        for (int i = 0; i < count; ++i) {
            if (std::strcmp(recipes[i].name, "Dembow") == 0) dembow = &recipes[i];
        }
        check(dembow != nullptr, "the dembow exists");

        Pattern pattern;
        applyKitRecipe(pattern, *dembow, 0.0f, 1, 60, voices);

        int snareHits = 0;
        bool snareOnBeatTwo = false;
        for (const Note& note : pattern.notes) {
            if (note.pitch != 38) continue;
            ++snareHits;
            if (std::fabs(note.startTime - 1.0f) < 1e-5f) snareOnBeatTwo = true;
        }
        check(snareHits == 4, "the dembow snare has its four hits - the 3-3-2 "
              "grouping twice per bar (got " + std::to_string(snareHits) + ")");
        check(!snareOnBeatTwo,
              "and none of them is a straight backbeat on beat two - the "
              "syncopation is the genre");
    }

    // ---- Genre filtering ---------------------------------------------------
    {
        int count = 0;
        const KitRecipe* recipes = kitRecipes(count);

        const KitRecipe* fourFloor = nullptr;
        for (int i = 0; i < count; ++i) {
            if (std::strcmp(recipes[i].name, "Four on the Floor") == 0) {
                fourFloor = &recipes[i];
            }
        }
        check(fourFloor != nullptr, "four on the floor exists");
        check(recipeSuitsGenre(*fourFloor, Genre::EDM),
              "four on the floor suits EDM");
        check(!recipeSuitsGenre(*fourFloor, Genre::HipHop),
              "and is not offered for hip hop");
        check(recipeSuitsGenre(*fourFloor, Genre::Everything),
              "though Everything sees all of it");

        // A recipe with no genre list suits every genre.
        const KitRecipe* rootNotes = nullptr;
        for (int i = 0; i < count; ++i) {
            if (std::strcmp(recipes[i].name, "Root Notes") == 0) {
                rootNotes = &recipes[i];
            }
        }
        check(rootNotes != nullptr && recipeSuitsGenre(*rootNotes, Genre::Chiptune) &&
              recipeSuitsGenre(*rootNotes, Genre::Reggaeton),
              "a universal recipe suits every genre");

        // Every genre must be offered at least one drum and one bass recipe,
        // or the Quick Start section is empty for it.
        for (int g = 0; g < static_cast<int>(Genre::Count); ++g) {
            const Genre genre = static_cast<Genre>(g);
            if (countRecipesForGenre(genre, KitCategory::Drums) == 0 ||
                countRecipesForGenre(genre, KitCategory::Bass) == 0) {
                check(false, std::string(genreName(genre)) +
                      " has no drum or no bass recipe");
                break;
            }
        }
        check(true, "every genre is offered drums and a bass at minimum");
    }

    // ---- Existing notes are kept ------------------------------------------
    //
    // Layering a bassline under a melody is ordinary; silently deleting
    // someone's work to make room is not.
    {
        int count = 0;
        const KitRecipe* recipes = kitRecipes(count);

        Pattern pattern;
        Note mine;
        mine.pitch = 72;
        mine.startTime = 0.5f;
        pattern.notes.push_back(mine);

        applyKitRecipe(pattern, recipes[0], 0.0f, 1, 60, voices);

        bool survived = false;
        for (const Note& note : pattern.notes) {
            if (note.pitch == 72 && std::fabs(note.startTime - 0.5f) < 1e-5f) {
                survived = true;
            }
        }
        check(survived, "a note that was already there survives a kit being "
                        "applied on top of it");
    }

    // ---- The key is honoured ----------------------------------------------
    {
        int count = 0;
        const KitRecipe* recipes = kitRecipes(count);
        const KitRecipe* octavePump = nullptr;
        for (int i = 0; i < count; ++i) {
            if (std::strcmp(recipes[i].name, "Octave Pump") == 0) {
                octavePump = &recipes[i];
            }
        }
        check(octavePump != nullptr, "the octave pump exists");

        Pattern inC, inE;
        applyKitRecipe(inC, *octavePump, 0.0f, 1, 60, voices);
        applyKitRecipe(inE, *octavePump, 0.0f, 1, 64, voices);

        check(!inC.notes.empty() && !inE.notes.empty() &&
              inE.notes[0].pitch - inC.notes[0].pitch == 4,
              "the same recipe in a different key moves by the difference");
    }

    // ---- Refusals ----------------------------------------------------------
    {
        int count = 0;
        const KitRecipe* recipes = kitRecipes(count);
        Pattern pattern;

        check(applyKitRecipe(pattern, recipes[0], 0.0f, 0, 60, voices) == 0,
              "zero bars writes nothing");
        check(applyKitRecipe(pattern, recipes[0], -1.0f, 4, 60, voices) == 0,
              "a negative start writes nothing");
        check(applyKitRecipe(pattern, recipes[0], 0.0f, 4, 999, voices) == 0,
              "an absurd key writes nothing");
        check(pattern.notes.empty(), "and the pattern is untouched in all cases");

        // The note budget holds.
        Pattern full;
        for (int i = 0; i < 40; ++i) {
            applyKitRecipe(full, recipes[0], float(i) * 4.0f, 4, 60, voices);
        }
        check(static_cast<int>(full.notes.size()) <= Pattern::MAX_NOTES,
              "repeated application never exceeds the note budget (got " +
              std::to_string(full.notes.size()) + ")");
    }
}

// ============================================================================
// 41. Next-step suggestions
// ============================================================================
static void testNextStep() {
    beginTest("Next-step suggestions");

    auto headline = [](const Project& project) {
        return std::string(suggestNextStep(project).headline);
    };

    // ---- The ladder, rung by rung -----------------------------------------
    {
        Project p;
        p.patterns.clear();
        p.arrangement.clear();
        check(headline(p) == "Start with four bars",
              "an empty project is pointed at the template");

        // A melody but no drums.
        Pattern melody;
        Note m; m.pitch = 72; m.oscillatorType = OscillatorType::Pulse;
        melody.notes.push_back(m);
        p.patterns.push_back(melody);
        check(headline(p) == "Add drums", "no drums means add drums");

        // Drums arrive.
        Note d; d.pitch = 36; d.oscillatorType = OscillatorType::Kick808;
        p.patterns[0].notes.push_back(d);
        check(headline(p) == "Add a bassline", "drums but no bass means add bass");

        // Bass arrives.
        Note b; b.pitch = 40; b.oscillatorType = OscillatorType::Triangle;
        p.patterns[0].notes.push_back(b);

        // Everything present, one pattern, nothing arranged: the trap.
        check(headline(p) == "Make a variation",
              "a complete loop in one pattern is pushed toward a variation, "
              "not another layer - the layer is the trap");

        // A second pattern exists but the song is still short.
        Pattern second;
        second.notes.push_back(m);
        p.patterns.push_back(second);
        p.arrangement.push_back(Clip{0, 0, 0.0f, 16.0f, 0});
        check(headline(p) == "Arrange what you have",
              "two patterns and a short timeline is pushed toward arranging");

        // A long arrangement on few channels.
        p.arrangement.push_back(Clip{1, 0, 16.0f, 24.0f, 0});
        check(headline(p) == "Give it some width",
              "a long arrangement on one channel is pushed toward width");

        // Spread across channels: done.
        p.arrangement.push_back(Clip{0, 2, 0.0f, 40.0f, 0});
        p.arrangement.push_back(Clip{1, 4, 0.0f, 40.0f, 0});
        check(headline(p) == "Sounds like a song",
              "a full spread is pointed at the export");
    }

    // ---- The bass register boundary ---------------------------------------
    //
    // This was wrong once already: the ceiling was C3, the chiptune template
    // puts its bass on A3, and a project with a perfectly good bassline was
    // told to write one.
    {
        Project p;
        p.patterns.clear();
        p.arrangement.clear();

        Pattern pattern;
        Note drums; drums.pitch = 36; drums.oscillatorType = OscillatorType::Kick808;
        Note bassA3; bassA3.pitch = 57; bassA3.oscillatorType = OscillatorType::Triangle;
        Note lead; lead.pitch = 72; lead.oscillatorType = OscillatorType::Pulse;
        pattern.notes.push_back(drums);
        pattern.notes.push_back(bassA3);
        pattern.notes.push_back(lead);
        p.patterns.push_back(pattern);

        check(headline(p) != "Add a bassline",
              "a bass on A3 counts as a bass - the regression that was "
              "actually shipped and caught in a screenshot");
    }

    // ---- Every template satisfies the ladder ------------------------------
    //
    // A starter template arriving with "add drums" over it would be
    // self-refuting.
    {
        for (int i = 0; i < static_cast<int>(Genre::Count); ++i) {
            const Project p = makeGenreTemplate(static_cast<Genre>(i));
            const std::string suggestion = headline(p);
            if (suggestion == "Start with four bars" || suggestion == "Add drums" ||
                suggestion == "Add a bassline" || suggestion == "Write a melody") {
                check(false, std::string(genreName(static_cast<Genre>(i))) +
                      "'s template is told: " + suggestion);
                break;
            }
        }
        check(true, "no starter template is told to add what it already has");
    }
}

// ============================================================================
// 42. Per-clip transpose
// ============================================================================
static void testClipTranspose() {
    beginTest("Per-clip transpose");

    // ---- It changes the audio, by the right amount ------------------------
    //
    // Rendering the same pattern plain and transposed +12 must double the
    // fundamental. Measured by zero crossings, which is crude but cannot be
    // fooled by a gain change.
    {
        auto renderTransposed = [](int transpose) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.patterns.clear();
            p.arrangement.clear();

            Pattern pat;
            Note n;
            n.pitch = 57;                     // A3, 220 Hz
            n.startTime = 0.0f;
            n.duration = 4.0f;
            n.oscillatorType = OscillatorType::Sine;
            pat.notes.push_back(n);
            p.patterns.push_back(pat);

            Clip clip{0, 0, 0.0f, 8.0f, 0};
            clip.transpose = transpose;
            p.arrangement.push_back(clip);
            p.channels[0].oscillator.type = OscillatorType::Sine;

            auto seqPtr = std::make_unique<Sequencer>();
            Sequencer& seq = *seqPtr;
            seq.setSampleRate(44100.0f);
            seq.setProject(&p);
            seq.updateChannelConfigs();
            seq.play();

            std::vector<float> out, l(512), r(512);
            for (int b = 0; b < 100; ++b) {
                seq.process(l.data(), r.data(), 512);
                out.insert(out.end(), l.begin(), l.end());
            }
            // Count rising zero crossings over the settled middle.
            int crossings = 0;
            for (size_t i = 20000; i + 1 < 45000 && i + 1 < out.size(); ++i) {
                if (out[i] <= 0.0f && out[i + 1] > 0.0f) ++crossings;
            }
            return crossings;
        };

        const int plain = renderTransposed(0);
        const int octaveUp = renderTransposed(12);

        check(plain > 50, "the untransposed note oscillates (crossings " +
              std::to_string(plain) + ")");
        const float ratio = float(octaveUp) / float(plain);
        check(ratio > 1.85f && ratio < 2.15f,
              "+12 semitones doubles the frequency - the transpose reaches the "
              "audio (ratio " + std::to_string(ratio) + ")");
    }

    // ---- A pitch pushed off MIDI is skipped, not wrapped ------------------
    {
        Project p;
        p.bpm = 120.0f;
        p.masterLimiterEnabled = false;
        p.patterns.clear();
        p.arrangement.clear();

        Pattern pat;
        Note n;
        n.pitch = 120;
        n.startTime = 0.0f;
        n.duration = 2.0f;
        pat.notes.push_back(n);
        p.patterns.push_back(pat);

        Clip clip{0, 0, 0.0f, 4.0f, 0};
        clip.transpose = 48;                  // 168: far past the top
        p.arrangement.push_back(clip);

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.play();

        std::vector<float> l(512), r(512);
        double energy = 0.0;
        for (int b = 0; b < 40; ++b) {
            seq.process(l.data(), r.data(), 512);
            for (float v : l) energy += double(v) * double(v);
        }
        check(energy < 1e-6,
              "a note transposed past MIDI's top is silent rather than wrapped "
              "- a bass note ten octaves up would be a far stranger bug to hear "
              "(energy " + std::to_string(energy) + ")");
    }

    // ---- The round trip, and old files ------------------------------------
    {
        Project original;
        original.patterns.clear();
        original.patterns.push_back(Pattern());
        original.arrangement.clear();
        Clip clip{0, 2, 4.0f, 8.0f, 0xFF112233u};
        clip.transpose = -7;
        original.arrangement.push_back(clip);

        const std::string path = testPath("clip_transpose.ctp");
        check(saveProjectFile(original, path), "a transposed clip saves");

        Project loaded;
        check(loadProjectFile(loaded, path), "and loads");
        check(!loaded.arrangement.empty() &&
              loaded.arrangement[0].transpose == -7,
              "with its transpose intact");
        std::remove(path.c_str());
    }

    {
        // A file written before the field existed: five tokens, no sixth.
        const std::string path = testPath("clip_pre37.ctp");
        {
            std::ofstream file(path);
            file << "CHIPTUNE_PROJECT v2\n";
            file << "PATTERN \"P\" 16\n";
            file << "NOTE 60 0.0 1.0 1.0 Pulse 0.0 0.0\n";
            file << "END_PATTERN\n";
            file << "CLIP 0 1 2.0 8.0 4283782485\n";
            file << "END_PROJECT\n";
        }

        Project loaded;
        check(loadProjectFile(loaded, path), "a pre-3.7 file still loads");
        check(!loaded.arrangement.empty() && loaded.arrangement[0].transpose == 0,
              "and its clips read as untransposed - the old behaviour");
        check(loaded.arrangement[0].channelIndex == 1 &&
              std::fabs(loaded.arrangement[0].startBeat - 2.0f) < 1e-5f,
              "with every older field still parsed correctly");
        std::remove(path.c_str());
    }

    // ---- Validation clamps a corrupt value --------------------------------
    {
        Project p;
        p.patterns.clear();
        p.patterns.push_back(Pattern());
        p.arrangement.clear();
        Clip clip{0, 0, 0.0f, 4.0f, 0};
        clip.transpose = 9000;
        p.arrangement.push_back(clip);

        clampProjectToValidRanges(p);
        check(p.arrangement[0].transpose <= 48 && p.arrangement[0].transpose >= -48,
              "a corrupt transpose is clamped to four octaves (got " +
              std::to_string(p.arrangement[0].transpose) + ")");
    }

    // ---- Ghosts show the sounding pitch -----------------------------------
    //
    // A ghost showing where the neighbour's notes are written rather than
    // where they sound would be worse than no ghost at all.
    {
        Project p;
        p.patterns.clear();

        Pattern edited;                        // pattern 0
        p.patterns.push_back(edited);

        Pattern neighbour;                     // pattern 1
        Note n;
        n.pitch = 60;
        n.startTime = 0.0f;
        n.duration = 1.0f;
        neighbour.notes.push_back(n);
        p.patterns.push_back(neighbour);

        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        Clip placed{1, 3, 0.0f, 4.0f, 0};
        placed.transpose = 7;
        p.arrangement.push_back(placed);

        const std::vector<GhostNote> ghosts = collectGhostNotes(p, 0);
        check(!ghosts.empty() && ghosts[0].pitch == 67,
              "a ghost from a transposed clip shows the sounding pitch, not "
              "the stored one (got " +
              std::to_string(ghosts.empty() ? -1 : ghosts[0].pitch) + ")");
    }
}

// ============================================================================
// 43. Groove presets, and swing reaching the arrangement
// ============================================================================
static void testGroovePresets() {
    beginTest("Groove presets");

    // ---- The presets themselves -------------------------------------------
    {
        int count = 0;
        const GroovePreset* presets = groovePresets(count);
        check(count >= 4, "there are enough presets to be worth having");

        for (int i = 0; i < count; ++i) {
            const GroovePreset& preset = presets[i];
            if (preset.name == nullptr || preset.description == nullptr ||
                preset.description[0] == '\0') {
                check(false, "a preset lacks a name or description"); break;
            }
            if (preset.swing < 0.0f || preset.swing > 1.0f ||
                preset.humanizeAmount < 0.0f || preset.humanizeAmount > 0.1f ||
                preset.humanizeVelocity < 0.0f || preset.humanizeVelocity > 0.5f) {
                check(false, std::string(preset.name) + " has unusable values");
                break;
            }
        }
        check(true, "every preset is named, described, and in range");

        // Two presets that set identical values would be indistinguishable
        // and matchGroovePreset could only ever report one of them.
        bool distinct = true;
        for (int a = 0; a < count && distinct; ++a) {
            for (int b = a + 1; b < count; ++b) {
                Project pa, pb;
                applyGroovePreset(pa, presets[a]);
                applyGroovePreset(pb, presets[b]);
                if (std::fabs(pa.swing - pb.swing) < 1e-6f &&
                    std::fabs(pa.swingGrid - pb.swingGrid) < 1e-6f &&
                    pa.humanize == pb.humanize &&
                    std::fabs(pa.humanizeAmount - pb.humanizeAmount) < 1e-6f) {
                    check(false, std::string(presets[a].name) + " and " +
                          presets[b].name + " are identical");
                    distinct = false;
                    break;
                }
            }
        }
        check(distinct, "no two presets set the same feel");

        // Applying then matching must round-trip, or the highlight lies.
        for (int i = 0; i < count; ++i) {
            Project project;
            applyGroovePreset(project, presets[i]);
            if (matchGroovePreset(project) != i) {
                check(false, std::string(presets[i].name) +
                      " does not match itself after being applied (got " +
                      std::to_string(matchGroovePreset(project)) + ")");
                break;
            }
        }
        check(true, "every preset matches itself after being applied");

        // Moving a slider off the preset must unmatch it.
        Project custom;
        applyGroovePreset(custom, presets[0]);
        custom.swing = 0.123f;
        check(matchGroovePreset(custom) < 0,
              "hand-moved sliders read as custom, so the highlight is honest");
    }

    // ---- Swing reaches the arrangement ------------------------------------
    //
    // The bug this fixed: applySwing was called only in the pattern preview,
    // so the Swing slider never changed a song played from the timeline. The
    // test plays an off-beat note from the ARRANGEMENT and measures when its
    // audio actually starts.
    {
        auto onsetSample = [](float swing) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.swing = swing;
            p.swingGrid = 0.5f;
            p.humanize = false;              // isolate the swing
            p.patterns.clear();
            p.arrangement.clear();

            Pattern pat;
            Note n;
            n.pitch = 69;
            n.startTime = 0.5f;              // an off-beat 8th - what swing moves
            n.duration = 1.0f;
            n.oscillatorType = OscillatorType::Pulse;
            pat.notes.push_back(n);
            p.patterns.push_back(pat);
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            Sequencer& seq = *seqPtr;
            seq.setSampleRate(44100.0f);
            seq.setProject(&p);
            seq.updateChannelConfigs();
            seq.play();

            std::vector<float> out, l(512), r(512);
            for (int b = 0; b < 60; ++b) {
                seq.process(l.data(), r.data(), 512);
                out.insert(out.end(), l.begin(), l.end());
            }
            for (size_t i = 0; i < out.size(); ++i) {
                if (std::fabs(out[i]) > 0.01f) return static_cast<int>(i);
            }
            return -1;
        };

        const int straight = onsetSample(0.0f);
        const int swung = onsetSample(0.8f);

        check(straight > 0, "the off-beat note sounds at all (onset " +
              std::to_string(straight) + ")");
        check(swung > straight + 1000,
              "with heavy swing the note starts audibly later when played "
              "from the ARRANGEMENT - the slider was preview-only before "
              "(straight onset " + std::to_string(straight) + ", swung " +
              std::to_string(swung) + ")");

        // A note ON the beat must not move - swing displaces off-beats only.
        auto onbeatOnset = [&](float swing) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.swing = swing;
            p.swingGrid = 0.5f;
            p.humanize = false;
            p.patterns.clear();
            p.arrangement.clear();

            Pattern pat;
            Note n;
            n.pitch = 69;
            n.startTime = 1.0f;              // squarely on the beat
            n.duration = 1.0f;
            n.oscillatorType = OscillatorType::Pulse;
            pat.notes.push_back(n);
            p.patterns.push_back(pat);
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            Sequencer& seq = *seqPtr;
            seq.setSampleRate(44100.0f);
            seq.setProject(&p);
            seq.updateChannelConfigs();
            seq.play();

            std::vector<float> out, l(512), r(512);
            for (int b = 0; b < 120; ++b) {
                seq.process(l.data(), r.data(), 512);
                out.insert(out.end(), l.begin(), l.end());
            }
            for (size_t i = 0; i < out.size(); ++i) {
                if (std::fabs(out[i]) > 0.01f) return static_cast<int>(i);
            }
            return -1;
        };

        const int onbeatStraight = onbeatOnset(0.0f);
        const int onbeatSwung = onbeatOnset(0.8f);
        check(std::abs(onbeatSwung - onbeatStraight) < 500,
              "a note on the beat does not move - swing displaces only the "
              "off-beats (straight " + std::to_string(onbeatStraight) +
              ", swung " + std::to_string(onbeatSwung) + ")");
    }
}

// ============================================================================
// Runner
// ============================================================================
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i], "-v") == 0) {
            g_verbose = true;
        }
    }

    // Unbuffered: a crash must not swallow the output that locates it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::printf("ChiptuneTracker test suite\n");
    std::printf("==========================\n");

    // Confirm up front that we can write files at all. Several tests are
    // meaningless otherwise, and "export failed" is a confusing way to learn
    // that the process simply has nowhere to write.
    {
        const std::string probe = testPath("write_probe.txt");
        std::ofstream f(probe);
        if (f.is_open()) {
            f << "ok\n";
            f.close();
            std::printf("scratch dir: %s (writable)\n", probe.c_str());
            std::remove(probe.c_str());
        } else {
            std::printf("scratch dir: %s (NOT WRITABLE, errno=%d: %s)\n",
                        probe.c_str(), errno, std::strerror(errno));
        }
    }
    std::printf("sizeof: Voice=%zu KB, Synthesizer=%zu KB, EffectsChain=%zu KB, "
                "Sequencer=%zu KB, Project=%zu KB\n",
                sizeof(Voice) / 1024, sizeof(Synthesizer) / 1024,
                sizeof(EffectsChain) / 1024, sizeof(Sequencer) / 1024,
                sizeof(Project) / 1024);
    std::fflush(stdout);

    testOscillatorNameTables();
    testAllOscillatorsRenderSanely();
    testNoteEffectExtremes();
    testEffectExtremes();
    testPolyphonyOverflow();
    testSequencerEdgeCases();
    testSaveLoadRoundTrip();
    testLoadsVersion1Files();
    testLoadRejectsGarbage();
    testMidiExport();
    testWavExport();
    testChannelConfigReachesSynth();
    testMasterBus();
    testSilenceIsSilent();
    testInstrumentMacros();
    testUndoRedo();
    testMuteSoloRouting();
    testLivePlaying();
    testWavetables();
    testMasterBusUnits();
    testValidationRepairsEverything();
    testSpectrumAnalyzer();
    testNoiseGenerator();
    testEuclideanGenerator();
    testStemExport();
    testThemeContrast();
    testAutosave();
    testGridSnap();
    testLoopRange();
    testNoteExpansion();
    testLoopAndEffectsReachAudio();
    testScalesAndTransforms();
    testGhostNotes();
    testChipMix();
    testChipFilters();
    testChipMixReachesAudio();
    testTrackerGrid();
    testGenreFocus();
    testUserSettings();
    testGenreTemplates();
    testGenreKits();
    testNextStep();
    testClipTranspose();
    testGroovePresets();
    testLongRunStability();

    std::printf("\n==========================\n");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);

    if (g_failures > 0) {
        std::printf("\nFailures:\n");
        for (const std::string& f : g_failureLog) std::printf("  %s\n", f.c_str());
        return 1;
    }

    std::printf("All tests passed.\n");
    return 0;
}
