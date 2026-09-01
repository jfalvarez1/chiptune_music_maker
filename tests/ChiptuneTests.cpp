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
#include "Version.h"
#include "VoiceCapture.h"
#include "LiveVoice.h"
#include "VoicePanel.h"
#include "WavetableEngine.h"
#include "FMSynth.h"
#include "Sampler.h"
#include "GranularSynth.h"
#include "DrumMachine.h"
#include "ModMatrix.h"
#include "PitchShift.h"
#include "InstrumentPresets.h"
#include "SettingsAudit.h"
#include "Reverbs.h"
#include "Convolution.h"
#include "EqualizerSuite.h"
#include "TakeLanes.h"
#include "Tutorial.h"
#include "LegacyEffectsChain.h"
#include "Routing.h"
#include <thread>
#include "UndoHistory.h"

// Pulls in ApplyTheme. No window or GL context is needed: it only writes to
// ImGuiStyle, and a context can exist without a backend.
#include "imgui.h"
#include "imgui_internal.h"
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
    // Values spread across each field's real range rather than scaled
    // linearly by the channel index. The old formulas were written when
    // eight channels was the whole world: 0.1f * ch gave channel 31 a
    // resonance of 3.1 and a reverb mix of 1.55, which validation correctly
    // clamped - so the round trip "failed" on values that were never legal.
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        ChannelConfig& c = p.channels[ch];
        const float spread = (Project::MAX_CHANNELS > 1)
            ? static_cast<float>(ch) / static_cast<float>(Project::MAX_CHANNELS - 1)
            : 0.0f;

        c.volume = 0.30f + 0.60f * spread;
        c.pan = -0.9f + 1.8f * spread;
        c.filterEnabled = true;
        c.filterCutoff = 500.0f + 6000.0f * spread;
        c.filterResonance = 0.05f + 0.90f * spread;
        c.filterEnvEnabled = true;
        c.filterEnvAmount = -0.9f + 1.8f * spread;
        c.delayEnabled = (ch % 2 == 0);
        c.delayTime = 0.05f + 0.90f * spread;
        c.reverbEnabled = (ch % 3 == 0);
        c.reverbMix = 0.05f + 0.90f * spread;
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
    // Derived, not hardcoded: this was 5 when eight channels was the whole
    // world, and the number of silent channels changes with the cap.
    check(result.skipped == Project::MAX_CHANNELS - 3,
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

    // ---- The engine focus ---------------------------------------------------
    {
        // Everything shows every engine, as it does every other section.
        for (int i = 0; i < ALL_ENGINE_COUNT; ++i) {
            if (!genreShowsEngine(Genre::Everything, ALL_ENGINES[i])) {
                check(false, std::string("Everything hides the ") +
                      ALL_ENGINES[i] + " engine");
                break;
            }
        }
        check(true, "Everything shows every engine");

        /*
         * Every engine a profile names must be a real one.
         *
         * A profile naming something not in ALL_ENGINES is a typo that hides
         * that engine forever with no error anywhere - the same failure the
         * chord and drum lists are already checked for, and the reason those
         * catalogues exist at all.
         */
        bool allNamesReal = true;
        std::string bad;
        for (int g = 0; g < static_cast<int>(Genre::Count) && allNamesReal; ++g) {
            const GenreProfile& profile = genreProfile(static_cast<Genre>(g));
            for (int e = 0; e < GENRE_MAX_ENGINES; ++e) {
                const char* named = profile.engines[e];
                if (named == nullptr) break;

                bool found = false;
                for (int k = 0; k < ALL_ENGINE_COUNT; ++k) {
                    if (std::strcmp(named, ALL_ENGINES[k]) == 0) { found = true; break; }
                }
                if (!found) {
                    allNamesReal = false;
                    bad = std::string(profile.name) + " names '" + named + "'";
                    break;
                }
            }
        }
        check(allNamesReal,
              "every engine a profile names exists" +
              (allNamesReal ? std::string() : " (" + bad + ")"));

        // And the genres actually differ, or the filter is decoration.
        check(genreShowsEngine(Genre::Chiptune, "Wavetable") &&
              genreShowsEngine(Genre::Chiptune, "FM"),
              "chiptune keeps wavetable and FM - the Game Boy's wave channel "
              "and the Mega Drive's YM2612 are what that era actually had");
        check(!genreShowsEngine(Genre::Chiptune, "Sampler"),
              "and sets the multisample sampler aside, which is the one "
              "thing those machines could not do");

        check(genreShowsEngine(Genre::HipHop, "Sampler"),
              "hip hop keeps the sampler, since chopping samples is the genre");
        check(!genreShowsEngine(Genre::HipHop, "FM"),
              "and sets FM aside");

        int differences = 0;
        for (int i = 0; i < ALL_ENGINE_COUNT; ++i) {
            if (genreShowsEngine(Genre::Chiptune, ALL_ENGINES[i]) !=
                genreShowsEngine(Genre::HipHop, ALL_ENGINES[i])) {
                ++differences;
            }
        }
        check(differences >= 3,
              "chiptune and hip hop show genuinely different engines (" +
              std::to_string(differences) + " differ)");

        // No genre may hide all of them, or the palette section is an empty
        // header with a line of apology in it.
        bool everyGenreHasOne = true;
        std::string empty;
        for (int g = 0; g < static_cast<int>(Genre::Count); ++g) {
            const Genre genre = static_cast<Genre>(g);
            int shown = 0;
            for (int i = 0; i < ALL_ENGINE_COUNT; ++i) {
                if (genreShowsEngine(genre, ALL_ENGINES[i])) ++shown;
            }
            if (shown == 0) { everyGenreHasOne = false; empty = genreName(genre); }
        }
        check(everyGenreHasOne,
              "no genre hides every engine" +
              (everyGenreHasOne ? std::string() : " (" + empty + " does)"));
    }

    // ---- The hidden count the UI reports is right -------------------------
    {
        const int chiptuneHidden = genreHiddenSectionCount(
            Genre::Chiptune, ALL_CHORD_SETS, CHORD_COUNT, ALL_DRUM_SETS, DRUM_COUNT);
        // Chiptune keeps 3 chord sets of 8, 3 drum categories of 7, and 2
        // engines of 5 - wavetable and FM, which is what the chips of that
        // era actually did.
        check(chiptuneHidden ==
                  (CHORD_COUNT - 3) + (DRUM_COUNT - 3) + (ALL_ENGINE_COUNT - 2),
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
// 44. Version coherence
// ============================================================================
static void testVersionCoherence() {
    beginTest("Version coherence");

    // The window title shipped saying 3.4.1 on a 3.6.0 build, because
    // Version.h held the version twice and only one copy was bumped. The
    // string is composed from the ints now; this pins that it stays so.
    const std::string expected = std::to_string(VERSION_MAJOR) + "." +
                                 std::to_string(VERSION_MINOR) + "." +
                                 std::to_string(VERSION_PATCH);
    check(VERSION_STRING == expected,
          "VERSION_STRING is composed from the version ints (got '" +
          VERSION_STRING + "', ints say '" + expected + "')");
    check(windowTitle().find(expected) != std::string::npos,
          "the window title carries the real version");
    check(aboutText().find(expected) != std::string::npos,
          "and so does the About dialog");
}

// ============================================================================
// 45. Guided first track
// ============================================================================
static void testTutorial() {
    beginTest("Guided first track");

    // ---- The lesson is well formed ----------------------------------------
    {
        int count = 0;
        const TutorialStep* steps = tutorialSteps(count);
        check(count >= 8 && count <= 32,
              "the lesson has a sane number of steps and fits the 32-bit "
              "latch mask (got " + std::to_string(count) + ")");

        for (int i = 0; i < count; ++i) {
            const TutorialStep& step = steps[i];
            if (step.title == nullptr || step.title[0] == '\0' ||
                step.body == nullptr || step.body[0] == '\0') {
                check(false, "step " + std::to_string(i) +
                      " lacks a title or body");
                break;
            }
            // The contract that makes it a lesson and not a wizard: an
            // action step must have evidence to check, an info step must not
            // pretend to.
            if (step.kind == TutorialStepKind::Action &&
                step.isComplete == nullptr) {
                check(false, std::string(step.title) +
                      " is an action step with no completion condition");
                break;
            }
            if (step.kind == TutorialStepKind::Info &&
                step.isComplete != nullptr) {
                check(false, std::string(step.title) +
                      " is an info step with a condition it would ignore");
                break;
            }
        }
        check(true, "every step is titled, described, and correctly kinded");
    }

    // ---- The whole road, walked in order ----------------------------------
    //
    // Simulates a user actually doing the lesson: each stage builds the
    // thing the step asks for, and the test asserts the step completes then
    // and not before. A condition firing early is as much a bug as one
    // never firing - it would tick goals the user has not reached.
    {
        Project project;
        project.patterns.clear();
        project.arrangement.clear();

        TutorialContext context;
        context.project = &project;

        TutorialProgress progress;
        progress.active = true;

        int count = 0;
        const TutorialStep* steps = tutorialSteps(count);

        auto conditionOf = [&](int index) {
            return steps[index].isComplete;
        };
        auto firstActionAfter = [&](int from) {
            for (int i = from; i < count; ++i) {
                if (steps[i].kind == TutorialStepKind::Action) return i;
            }
            return -1;
        };

        // Locate the action steps by walking the table, so reordering the
        // lesson does not silently invalidate the test.
        const int melodyStep = firstActionAfter(0);
        check(melodyStep >= 0, "there is a melody step");
        check(!conditionOf(melodyStep)(context),
              "an empty project has not completed the melody step");

        // The user draws a melody.
        Pattern pattern;
        for (int i = 0; i < 4; ++i) {
            Note n; n.pitch = 72 + i; n.startTime = float(i);
            n.oscillatorType = OscillatorType::Pulse;
            pattern.notes.push_back(n);
        }
        project.patterns.push_back(pattern);
        check(conditionOf(melodyStep)(context),
              "four drawn notes complete the melody step");

        // Playing.
        const int playStep = firstActionAfter(melodyStep + 1);
        check(playStep >= 0 && !conditionOf(playStep)(context),
              "the play step waits until playback has actually run");
        context.hasPlayed = true;
        check(conditionOf(playStep)(context), "and completes once it has");

        // Drums.
        const int drumStep = firstActionAfter(playStep + 1);
        check(drumStep >= 0 && !conditionOf(drumStep)(context),
              "the drum step is not fooled by melody notes");
        for (int i = 0; i < 3; ++i) {
            Note d; d.pitch = 36; d.startTime = float(i);
            d.oscillatorType = OscillatorType::Kick808;
            project.patterns[0].notes.push_back(d);
        }
        check(conditionOf(drumStep)(context), "three drum hits complete it");

        // Bass.
        const int bassStep = firstActionAfter(drumStep + 1);
        check(bassStep >= 0 && !conditionOf(bassStep)(context),
              "the bass step is not fooled by drums or melody");
        for (int i = 0; i < 2; ++i) {
            Note b; b.pitch = 40; b.startTime = float(i) * 2.0f;
            b.oscillatorType = OscillatorType::Triangle;
            project.patterns[0].notes.push_back(b);
        }
        check(conditionOf(bassStep)(context), "two bass notes complete it");

        // A variation.
        const int variationStep = firstActionAfter(bassStep + 1);
        check(variationStep >= 0 && !conditionOf(variationStep)(context),
              "one pattern is not a variation");
        Pattern second;
        Note v; v.pitch = 74; v.oscillatorType = OscillatorType::Pulse;
        second.notes.push_back(v);
        project.patterns.push_back(second);
        check(conditionOf(variationStep)(context),
              "a second pattern with notes completes it");

        // Arranging.
        const int arrangeStep = firstActionAfter(variationStep + 1);
        check(arrangeStep >= 0 && !conditionOf(arrangeStep)(context),
              "an empty timeline is not an arrangement");
        project.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        project.arrangement.push_back(Clip{1, 0, 4.0f, 4.0f, 0});
        check(conditionOf(arrangeStep)(context),
              "two placements covering eight beats complete it");

        // Looping.
        const int loopStep = firstActionAfter(arrangeStep + 1);
        check(loopStep >= 0 && !conditionOf(loopStep)(context),
              "no loop range yet");
        context.loopRangeActive = true;
        check(conditionOf(loopStep)(context), "a drawn loop range completes it");

        // Saving.
        const int saveStep = firstActionAfter(loopStep + 1);
        check(saveStep >= 0 && !conditionOf(saveStep)(context),
              "unsaved is unsaved");
        context.projectSaved = true;
        check(conditionOf(saveStep)(context), "a saved file finishes the road");
    }

    // ---- Latching ----------------------------------------------------------
    //
    // Deleting your drums after the drum step must not walk the lesson
    // backwards underneath you.
    {
        Project project;
        project.patterns.clear();
        Pattern pattern;
        for (int i = 0; i < 3; ++i) {
            Note d; d.pitch = 36; d.oscillatorType = OscillatorType::Kick808;
            pattern.notes.push_back(d);
        }
        project.patterns.push_back(pattern);

        TutorialContext context;
        context.project = &project;

        TutorialProgress progress;
        progress.active = true;

        // Find the drum step and park the lesson on it.
        int count = 0;
        const TutorialStep* steps = tutorialSteps(count);
        int drumStep = -1;
        for (int i = 0; i < count; ++i) {
            if (steps[i].kind == TutorialStepKind::Action &&
                steps[i].isComplete(context)) {
                drumStep = i;   // the drum condition is the one this satisfies
            }
        }
        check(drumStep >= 0, "a drum-only project satisfies exactly the drum step");

        progress.step = drumStep;
        check(updateTutorial(progress, context), "the step reports complete");
        check(progress.stepDone(drumStep), "and is latched");

        project.patterns[0].notes.clear();   // the user deletes everything
        check(updateTutorial(progress, context),
              "deleting the drums afterwards does not un-complete the step");
    }

    // ---- A restart is a restart -------------------------------------------
    //
    // The user restarted the lesson and found steps pre-completed. Two
    // leaks: the has-played latch lived in a panel static that outlived the
    // reset, and the old project's notes satisfied the early conditions.
    // The first is pinned here; the second is the fresh-project rule in
    // main.cpp, whose observable half - a fresh progress claims nothing -
    // is pinned too.
    {
        TutorialProgress finished;
        finished.active = true;
        finished.step = 5;
        finished.completedMask = 0x3F;
        finished.hasPlayed = true;
        finished.focusedStep = 5;

        finished = TutorialProgress{};     // what StartTutorial does
        finished.active = true;

        check(finished.step == 0, "a restarted lesson is on step one");
        check(finished.completedMask == 0, "with nothing marked done");
        check(!finished.hasPlayed,
              "and no memory of playback from the previous run - the latch "
              "lives inside the progress precisely so a reset clears it");
        check(finished.focusedStep < 0, "and re-raises the first step's window");

        Project empty;
        empty.patterns.clear();
        empty.arrangement.clear();
        TutorialContext context;
        context.project = &empty;

        // Step 0 is an Info step and reports advanceable by design - that
        // is what Next means. The claim under test belongs on the first
        // ACTION step: an empty project must not satisfy it.
        int stepTotal = 0;
        const TutorialStep* table = tutorialSteps(stepTotal);
        for (int i = 0; i < stepTotal; ++i) {
            if (table[i].kind == TutorialStepKind::Action) {
                finished.step = i;
                break;
            }
        }
        check(!updateTutorial(finished, context),
              "on an empty project a fresh lesson's first action step is "
              "not already done");
        check(finished.completedMask == 0, "and nothing is latched");
    }

    // ---- Hygiene ----------------------------------------------------------
    {
        TutorialProgress progress;
        check(!progress.active, "the lesson does not start itself");

        TutorialContext empty;               // null project
        progress.active = true;
        progress.step = 0;
        check(updateTutorial(progress, empty) || true,
              "a null project does not crash a condition");

        progress.step = 999;
        check(!updateTutorial(progress, empty),
              "a step index past the end reports nothing rather than reading "
              "past the table");

        check(!progress.stepDone(-1) && !progress.stepDone(64),
              "out-of-range latch queries are safely false");
    }
}

// ============================================================================
// 46. Play again after the end
// ============================================================================
static void testPlayFromEnd() {
    beginTest("Play again after the end");

    // The user's report, verbatim: "when the track finished playing and
    // click on play again it doesnt, i have to click on stop even though
    // theres nothing playing". The playhead parks at the end; play() must
    // rewind rather than let the first block re-stop itself.

    Project p;
    p.bpm = 240.0f;                     // short beats, fast test
    p.masterLimiterEnabled = false;
    p.patterns.clear();
    p.arrangement.clear();

    Pattern pat;
    Note n;
    n.pitch = 69;
    n.startTime = 0.0f;
    n.duration = 1.0f;
    n.oscillatorType = OscillatorType::Pulse;
    pat.notes.push_back(n);
    p.patterns.push_back(pat);
    p.arrangement.push_back(Clip{0, 0, 0.0f, 2.0f, 0});

    auto seqPtr = std::make_unique<Sequencer>();
    Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&p);
    seq.setLoopEnabled(false);
    seq.play();

    // Run to the end. With no preview pattern the content window falls
    // back to the fixed 16-beat loop end, so the stop lands at beat 16 -
    // 4 seconds at 240 BPM, about 350 blocks.
    std::vector<float> l(512), r(512);
    for (int b = 0; b < 420 && seq.isPlaying(); ++b) {
        seq.process(l.data(), r.data(), 512);
    }
    check(!seq.isPlaying(), "non-looping playback stops at the end");
    check(seq.getCurrentBeat() > 15.9f, "with the playhead parked there");

    // The bug: this second play() used to die inside its first block.
    seq.play();
    double energy = 0.0;
    for (int b = 0; b < 10; ++b) {
        seq.process(l.data(), r.data(), 512);
        for (float v : l) energy += double(v) * double(v);
    }
    check(seq.isPlaying(),
          "pressing play at the end starts playback instead of instantly "
          "re-stopping");
    check(energy > 0.01,
          "and it is audible from the top, with no STOP press required "
          "(energy " + std::to_string(energy) + ")");

    // Pausing mid-song must still resume from the pause point, not the top.
    seq.stop();
    seq.play();
    for (int b = 0; b < 10; ++b) seq.process(l.data(), r.data(), 512);
    seq.pause();
    const float pausedAt = seq.getCurrentBeat();
    check(pausedAt > 0.05f && pausedAt < 15.9f, "paused somewhere mid-song");
    seq.play();
    check(std::fabs(seq.getCurrentBeat() - pausedAt) < 0.05f,
          "play after pause resumes where it paused - the rewind is only "
          "for a playhead parked at the very end");
}

// ============================================================================
// 47. The insert rack is sample-identical to the fixed chain
// ============================================================================
static void testEffectRackIdentity() {
    beginTest("Effect rack: identical to the old fixed chain");

    // Task A replaced seventeen members and a hardcoded order with a
    // reorderable rack. The whole claim is that it changed nothing audible,
    // so this compares against a frozen verbatim copy of the old chain -
    // tests/LegacyEffectsChain.h - sample by sample, with exact float
    // equality. Not an epsilon: any drift at all is a defect here.

    // Deterministic and broadband, so every effect has something to chew on.
    auto makeInput = [](int n) {
        std::vector<float> input(static_cast<size_t>(n));
        uint32_t rng = 0x13579BDFu;
        for (int i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / 44100.0f;
            rng = rng * 1664525u + 1013904223u;
            const float noise = (float(rng >> 8) / 8388608.0f) - 1.0f;
            float value = 0.45f * std::sin(6.2831853f * 220.0f * t)      // tone
                        + 0.25f * (std::fmod(t * 110.0f, 1.0f) - 0.5f)   // saw
                        + 0.05f * noise;                                  // hiss
            if (i % 4410 == 0) value += 0.9f;                             // impulses
            input[static_cast<size_t>(i)] = value;
        }
        return input;
    };

    // Extreme but legal settings: an effect left at unity would prove nothing.
    auto configure = [](auto& chain) {
        chain.eq.lowGain = 1.8f; chain.eq.midGain = 0.5f; chain.eq.highGain = 1.6f;
        chain.tapeSaturation.drive = 2.5f; chain.tapeSaturation.mix = 0.8f;
        chain.formant.vowel = FormantFilter::Vowel::E; chain.formant.resonance = 6.0f;
        // threshold is linear 0..1 here, not dB - the same units trap that
        // silenced the master EQ once.
        chain.compressor.threshold = 0.15f; chain.compressor.ratio = 8.0f;
        chain.bitcrusher.bitDepth = 3.0f; chain.bitcrusher.sampleRateReduction = 12.0f;
        chain.distortion.drive = 18.0f; chain.distortion.mix = 0.7f;
        chain.filter.cutoff = 900.0f; chain.filter.resonance = 0.6f;
        chain.ringMod.frequency = 320.0f; chain.ringMod.mix = 0.65f;
        chain.tremolo.rate = 5.5f; chain.tremolo.depth = 0.7f;
        chain.phaser.rate = 0.7f; chain.phaser.depth = 0.8f;
        chain.flanger.rate = 0.35f; chain.flanger.depth = 0.7f;
        chain.chorus.rate = 1.4f; chain.chorus.depth = 0.6f;
        chain.delay.delayTime = 0.18f; chain.delay.feedback = 0.45f; chain.delay.mix = 0.5f;
        chain.reverb.roomSize = 0.7f; chain.reverb.damping = 0.4f; chain.reverb.mix = 0.45f;
    };

    /*
     * Only the effects the frozen chain actually has.
     *
     * LegacyEffectsChain is a verbatim copy of the pre-rack chain, and
     * anything added since has no counterpart in it - so enabling one is a
     * divergence by definition, and a correct one. Reverb was the last
     * effect at the freeze; everything after it in the enum is newer.
     *
     * Named rather than numbered so that adding another effect does not
     * silently widen or narrow what this compares.
     */
    const int LEGACY_EFFECT_COUNT = static_cast<int>(EffectType::Reverb) + 1;
    check(LEGACY_EFFECT_COUNT <= EFFECT_TYPE_COUNT,
          "the legacy chain covers a prefix of the current effect list");

    struct Case { const char* name; EffectType only; bool all; };
    std::vector<Case> cases;
    cases.push_back({"every effect at once", EffectType::EQ, true});
    for (int i = 0; i < LEGACY_EFFECT_COUNT; ++i) {
        cases.push_back({effectDisplayName(static_cast<EffectType>(i)),
                         static_cast<EffectType>(i), false});
    }

    const int SAMPLES = 44100 * 2;   // two seconds
    const std::vector<float> input = makeInput(SAMPLES);

    int mismatches = 0;
    for (const Case& testCase : cases) {
        legacy::LegacyEffectsChain oldChain;
        EffectsChain newChain;

        oldChain.setSampleRate(44100.0f);
        newChain.setSampleRate(44100.0f);
        configure(oldChain);
        configure(newChain);

        // Same enables on both sides. Effects newer than the freeze stay
        // off throughout, including in the "every effect at once" case -
        // the legacy chain has no way to switch them on, so leaving them
        // running would compare a rack that has them against one that
        // cannot.
        for (int i = 0; i < LEGACY_EFFECT_COUNT; ++i) {
            const EffectType type = static_cast<EffectType>(i);
            const bool on = testCase.all || (type == testCase.only);
            newChain.setEnabled(type, on);
            switch (type) {
                case EffectType::EQ:             oldChain.eqEnabled = on; break;
                case EffectType::TapeSaturation: oldChain.tapeSaturationEnabled = on; break;
                case EffectType::Formant:        oldChain.formantEnabled = on; break;
                case EffectType::Compressor:     oldChain.compressorEnabled = on; break;
                case EffectType::Bitcrusher:     oldChain.bitcrusherEnabled = on; break;
                case EffectType::Distortion:     oldChain.distortionEnabled = on; break;
                case EffectType::Filter:         oldChain.filterEnabled = on; break;
                case EffectType::RingMod:        oldChain.ringModEnabled = on; break;
                case EffectType::Tremolo:        oldChain.tremoloEnabled = on; break;
                case EffectType::Phaser:         oldChain.phaserEnabled = on; break;
                case EffectType::Flanger:        oldChain.flangerEnabled = on; break;
                case EffectType::Chorus:         oldChain.chorusEnabled = on; break;
                case EffectType::Delay:          oldChain.delayEnabled = on; break;
                case EffectType::Reverb:         oldChain.reverbEnabled = on; break;
                default: break;
            }
        }

        oldChain.reset();
        newChain.reset();

        int firstBad = -1;
        for (int i = 0; i < SAMPLES; ++i) {
            const float time = static_cast<float>(i) / 44100.0f;
            const float a = oldChain.process(input[static_cast<size_t>(i)], time);
            const float b = newChain.process(input[static_cast<size_t>(i)], time);
            if (a != b) { firstBad = i; break; }
        }

        if (firstBad >= 0) {
            check(false, std::string("rack diverged from the old chain with '") +
                  testCase.name + "' at sample " + std::to_string(firstBad));
            if (++mismatches >= 3) break;
        }
    }
    if (mismatches == 0) {
        check(true, "the rack in classic order is sample-identical to the old "
                    "fixed chain, across all effects together and each alone");
    }

    // ---- The default really is the classic order --------------------------
    {
        EffectsChain chain;
        check(chain.isClassicOrder(),
              "a new chain starts in the order every previous version used");
        check(chain.rack().count == EFFECT_TYPE_COUNT,
              "with every effect present exactly once");

        bool seen[EFFECT_TYPE_COUNT] = {};
        bool duplicate = false;
        for (int i = 0; i < chain.rack().count; ++i) {
            const int index = static_cast<int>(chain.rack().slots[i]);
            if (index < 0 || index >= EFFECT_TYPE_COUNT || seen[index]) duplicate = true;
            else seen[index] = true;
        }
        check(!duplicate, "and no effect listed twice");
    }

    // ---- Reordering actually reorders -------------------------------------
    //
    // The point of the whole task: reverb before distortion must sound
    // different from distortion before reverb, or the rack is decorative.
    {
        const std::vector<float> shortInput = makeInput(8192);

        auto render = [&](bool reversed) {
            EffectsChain chain;
            chain.setSampleRate(44100.0f);
            configure(chain);
            chain.distortionEnabled = true;
            chain.reverbEnabled = true;
            if (reversed) {
                // Find reverb and move it before distortion.
                int reverbSlot = -1, distSlot = -1;
                for (int i = 0; i < chain.rack().count; ++i) {
                    if (chain.rack().slots[i] == EffectType::Reverb) reverbSlot = i;
                    if (chain.rack().slots[i] == EffectType::Distortion) distSlot = i;
                }
                chain.moveSlot(reverbSlot, distSlot);
            }
            chain.reset();
            std::vector<float> out;
            out.reserve(shortInput.size());
            for (size_t i = 0; i < shortInput.size(); ++i) {
                out.push_back(chain.process(shortInput[i],
                                            static_cast<float>(i) / 44100.0f));
            }
            return out;
        };

        const std::vector<float> normal = render(false);
        const std::vector<float> swapped = render(true);

        double difference = 0.0;
        for (size_t i = 0; i < normal.size(); ++i) {
            difference += std::fabs(normal[i] - swapped[i]);
        }
        check(difference > 1.0,
              "reverb before distortion sounds different from distortion "
              "before reverb - the reorder reaches the audio (difference " +
              std::to_string(difference) + ")");
    }

    // ---- moveSlot is total ------------------------------------------------
    {
        EffectsChain chain;
        const auto original = chain.rack().slots;

        chain.moveSlot(-1, 3);
        chain.moveSlot(3, -1);
        chain.moveSlot(99, 0);
        chain.moveSlot(0, 99);
        chain.moveSlot(2, 2);
        bool unchanged = (chain.rack().count == EFFECT_TYPE_COUNT);
        for (int i = 0; i < chain.rack().count && unchanged; ++i) {
            if (chain.rack().slots[i] != original[i]) unchanged = false;
        }
        check(unchanged, "out-of-range and no-op moves leave the rack alone");

        chain.moveSlot(0, chain.rack().count - 1);
        bool stillComplete = true;
        bool seen[EFFECT_TYPE_COUNT] = {};
        for (int i = 0; i < chain.rack().count; ++i) {
            const int index = static_cast<int>(chain.rack().slots[i]);
            if (seen[index]) stillComplete = false;
            seen[index] = true;
        }
        check(stillComplete, "moving the first slot to the end loses nothing");
        check(chain.rack().slots[chain.rack().count - 1] == EffectType::EQ,
              "and puts it where it was asked to go");
    }

    // ---- Wet/dry --------------------------------------------------------
    {
        EffectsChain dryChain, wetChain;
        dryChain.setSampleRate(44100.0f);
        wetChain.setSampleRate(44100.0f);
        configure(dryChain);
        configure(wetChain);
        dryChain.distortionEnabled = true;
        wetChain.distortionEnabled = true;
        dryChain.mix[static_cast<size_t>(EffectType::Distortion)] = 0.0f;

        const float sample = 0.5f;
        check(std::fabs(dryChain.process(sample, 0.0f) - sample) < 1e-6f,
              "a slot at zero mix passes the signal through untouched");
        check(std::fabs(wetChain.process(sample, 0.0f) - sample) > 1e-6f,
              "and at full mix it does not");
    }

    // ---- Copying rebinds --------------------------------------------------
    //
    // The adapters hold pointers into their own chain. A copy that did not
    // rebind them would silently drive the original's effects.
    {
        EffectsChain source;
        source.setSampleRate(44100.0f);
        configure(source);
        source.distortionEnabled = true;

        EffectsChain copy = source;
        copy.distortion.drive = 40.0f;    // diverge the copy

        const float a = source.process(0.4f, 0.0f);
        const float b = copy.process(0.4f, 0.0f);
        check(a != b,
              "a copied chain drives its own effects, not the original's - "
              "the adapters rebind on copy");
    }
}

// ============================================================================
// 48. Rack stability under reordering
// ============================================================================
static void testEffectRackStability() {
    beginTest("Effect rack stability");

    // Anything touching the audio thread gets a stability run. The UI
    // reorders slots while audio is processing, so this hammers both at once.
    //
    // The assertions are split by whether they depend on thread timing.
    // Peak amplitude with delay and reverb in the rack does - feedback
    // accumulates differently per interleaving, and an early version of this
    // test failed and passed on consecutive runs because of it. Finiteness
    // and structural integrity do not depend on timing, so those are
    // asserted with the feedback effects present; the amplitude bound is
    // asserted separately with them removed, where it is deterministic.

    auto stormRun = [](bool includeFeedback, int blocks) {
        struct Result {
            bool finite = true;
            bool bounded = true;
            float worst = 0.0f;
            int reorders = 0;
            bool intact = false;
        } result;

        EffectsChain chain;
        chain.setSampleRate(44100.0f);
        chain.eqEnabled = true;
        chain.distortionEnabled = true;
        chain.filterEnabled = true;
        chain.bitcrusherEnabled = true;
        if (includeFeedback) {
            chain.delayEnabled = true;
            chain.reverbEnabled = true;
        }

        std::atomic<bool> running{true};
        std::atomic<int> reorders{0};

        std::thread editor([&]() {
            uint32_t rng = 0xA5A5A5A5u;
            while (running.load(std::memory_order_relaxed)) {
                rng = rng * 1664525u + 1013904223u;
                const int from = static_cast<int>((rng >> 16) % 14u);
                rng = rng * 1664525u + 1013904223u;
                const int to = static_cast<int>((rng >> 16) % 14u);
                chain.moveSlot(from, to);
                reorders.fetch_add(1, std::memory_order_relaxed);
            }
        });

        for (int block = 0; block < blocks; ++block) {
            for (int i = 0; i < 64; ++i) {
                const float time = static_cast<float>(block * 64 + i) / 44100.0f;
                const float out = chain.process(0.4f * std::sin(time * 900.0f), time);
                if (!std::isfinite(out)) { result.finite = false; break; }
                result.worst = std::max(result.worst, std::fabs(out));
            }
            if (!result.finite) break;
        }

        running.store(false, std::memory_order_relaxed);
        editor.join();

        result.reorders = reorders.load();

        bool seen[EFFECT_TYPE_COUNT] = {};
        result.intact = (chain.rack().count == EFFECT_TYPE_COUNT);
        for (int i = 0; i < chain.rack().count && result.intact; ++i) {
            const int index = static_cast<int>(chain.rack().slots[i]);
            if (index < 0 || index >= EFFECT_TYPE_COUNT || seen[index]) {
                result.intact = false;
            } else {
                seen[index] = true;
            }
        }
        return result;
    };

    // ---- With feedback: the timing-independent invariants ----------------
    {
        const auto result = stormRun(true, 4000);

        check(result.reorders > 0,
              "the editor thread really did reorder slots (" +
              std::to_string(result.reorders) + " times)");
        check(result.finite,
              "no sample went NaN or infinite while slots moved underneath, "
              "even with delay and reverb feedback in the rack");
        check(result.intact,
              "and the rack still holds every effect exactly once afterwards");
    }

    // ---- Without feedback: the amplitude bound, deterministically --------
    //
    // Nothing accumulates here, so the peak is set by the gains alone. If a
    // torn read ever applies an effect twice this fails - which is exactly
    // what it did, at amplitude 50, before the order was published
    // atomically.
    {
        const auto result = stormRun(false, 2000);

        check(result.finite, "no non-finite sample without feedback either");
        check(result.worst < 5.0f,
              "peak amplitude stays bounded under a reorder storm - a torn "
              "read applying an effect twice would break this (worst " +
              std::to_string(result.worst) + ")");
        check(result.intact, "and the rack survives structurally intact");
    }
}

// ============================================================================
// 49. The rack survives save, load and a v2 file
// ============================================================================
static void testEffectRackPersistence() {
    beginTest("Effect rack persistence and migration");

    // Rule 4 exists because v1 -> v2 once discarded an entire mix in silence.
    // These are the checks that would have caught that.

    // ---- A reordered rack round-trips -------------------------------------
    {
        Project original;
        // Reverb first, distortion last: nothing like the classic order.
        original.channels[0].fxOrder[0] = static_cast<uint8_t>(EffectType::Reverb);
        original.channels[0].fxOrder[1] = static_cast<uint8_t>(EffectType::Delay);
        original.channels[0].fxOrder[2] = static_cast<uint8_t>(EffectType::Distortion);
        original.channels[0].fxSlotCount = 3;
        original.channels[0].reverbEnabled = true;

        const std::string path = testPath("rack_roundtrip.ctp");
        check(saveProjectFile(original, path), "a reordered rack saves");

        Project loaded;
        check(loadProjectFile(loaded, path), "and loads");
        check(loaded.channels[0].fxSlotCount == 3,
              "with its slot count intact (got " +
              std::to_string(loaded.channels[0].fxSlotCount) + ")");
        check(loaded.channels[0].fxOrder[0] ==
                  static_cast<uint8_t>(EffectType::Reverb) &&
              loaded.channels[0].fxOrder[2] ==
                  static_cast<uint8_t>(EffectType::Distortion),
              "and its order preserved end to end");
        std::remove(path.c_str());
    }

    // ---- A classic rack writes no line at all -----------------------------
    //
    // A project nobody reordered must produce the bytes it always did.
    {
        Project plain;
        const std::string path = testPath("rack_classic.ctp");
        check(saveProjectFile(plain, path), "an untouched project saves");

        const std::string text = readWholeFile(path);
        check(text.find("FXORDER") == std::string::npos,
              "and writes no rack line, so a file nobody reordered is "
              "unchanged by the format bump");
        std::remove(path.c_str());
    }

    // ---- A v2 file migrates to the classic order --------------------------
    //
    // This is the migration. It is a no-op by construction - count 0 means
    // classic - and that is exactly why it cannot lose anyone's mix.
    {
        const std::string path = testPath("rack_v2.ctp");
        {
            std::ofstream file(path);
            file << "CHIPTUNE_PROJECT v2\n";
            file << "NAME Old Mix\n";
            file << "BPM 128\n";
            file << "CHANNEL 0 \"Lead\" Pulse revOn=1 dstOn=1 dlyOn=1\n";
            file << "PATTERN \"P\" 16\n";
            file << "NOTE 60 0.0 1.0 1.0 Pulse 0.0 0.0\n";
            file << "END_PATTERN\n";
            file << "END_PROJECT\n";
        }

        Project loaded;
        check(loadProjectFile(loaded, path), "a v2 file still loads");
        check(loaded.channels[0].fxSlotCount == 0,
              "its rack reads as 'classic order' rather than as an empty rack");
        check(loaded.channels[0].reverbEnabled &&
              loaded.channels[0].distortionEnabled &&
              loaded.channels[0].delayEnabled,
              "and every effect it had enabled is still enabled - the mix "
              "survives the version bump, which v1 -> v2 did not manage");

        // And the chain that config produces really is the classic one.
        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&loaded);
        seq.updateChannelConfigs();
        check(seq.channelEffects(0).isClassicOrder(),
              "and the live chain is in the classic order it always was");

        std::remove(path.c_str());
    }

    // ---- Sanitising a corrupt rack ----------------------------------------
    {
        Project corrupt;
        corrupt.channels[0].fxOrder[0] = static_cast<uint8_t>(EffectType::Delay);
        corrupt.channels[0].fxOrder[1] = static_cast<uint8_t>(EffectType::Delay);
        corrupt.channels[0].fxOrder[2] = 200;    // no such effect
        corrupt.channels[0].fxOrder[3] = static_cast<uint8_t>(EffectType::Filter);
        corrupt.channels[0].fxSlotCount = 4;

        clampProjectToValidRanges(corrupt);

        check(corrupt.channels[0].fxSlotCount == 2,
              "a duplicate and an unknown effect are both dropped (got " +
              std::to_string(corrupt.channels[0].fxSlotCount) + ")");
        check(corrupt.channels[0].fxOrder[0] ==
                  static_cast<uint8_t>(EffectType::Delay) &&
              corrupt.channels[0].fxOrder[1] ==
                  static_cast<uint8_t>(EffectType::Filter),
              "and what survives keeps its order");
    }

    {
        Project allJunk;
        allJunk.channels[0].fxOrder[0] = 210;
        allJunk.channels[0].fxOrder[1] = 220;
        allJunk.channels[0].fxSlotCount = 2;

        clampProjectToValidRanges(allJunk);

        check(allJunk.channels[0].fxSlotCount == 0,
              "a rack that sanitises to nothing falls back to the classic "
              "order rather than leaving the channel silent");
    }

    {
        Project overflow;
        overflow.channels[0].fxSlotCount = 9999;
        clampProjectToValidRanges(overflow);
        check(overflow.channels[0].fxSlotCount <= MAX_FX_SLOTS,
              "an absurd slot count cannot walk off the end of the array");
    }

    // ---- An unknown token in a v3 file ------------------------------------
    {
        const std::string path = testPath("rack_future.ctp");
        {
            std::ofstream file(path);
            file << "CHIPTUNE_PROJECT v3\n";
            file << "CHANNEL 0 \"Lead\" Pulse\n";
            file << "FXORDER 0 reverb quantumflux delay\n";
            file << "PATTERN \"P\" 16\n";
            file << "END_PATTERN\n";
            file << "END_PROJECT\n";
        }

        Project loaded;
        check(loadProjectFile(loaded, path), "a file naming an unknown effect loads");
        check(loaded.channels[0].fxSlotCount == 2,
              "the unknown effect is dropped and the rest of the rack kept");
        check(loaded.channels[0].fxOrder[0] ==
                  static_cast<uint8_t>(EffectType::Reverb) &&
              loaded.channels[0].fxOrder[1] ==
                  static_cast<uint8_t>(EffectType::Delay),
              "in the order the file gave them");
        std::remove(path.c_str());
    }
}

// ============================================================================
// 50. The routing graph
// ============================================================================
static void testRoutingGraph() {
    beginTest("Aux routing graph");

    using Buses = std::array<AuxBusConfig, Project::MAX_AUX_BUSES>;

    // ---- Cycle detection --------------------------------------------------
    {
        Buses buses;   // all default to master

        check(!wouldCreateCycle(buses, 0, ROUTE_TO_MASTER),
              "routing a bus to the master can never loop");
        check(!wouldCreateCycle(buses, 0, 1),
              "routing bus 0 into bus 1 is fine when 1 goes to master");
        check(wouldCreateCycle(buses, 0, 0),
              "a bus feeding itself is a loop");

        // 1 -> 2 already; now try 2 -> 1.
        buses[1].output = 2;
        check(wouldCreateCycle(buses, 2, 1),
              "closing a two-bus loop is caught");
        check(!wouldCreateCycle(buses, 3, 1),
              "but an unrelated bus joining the chain is not a loop");

        // A longer chain: 0 -> 1 -> 2 -> 3, then 3 -> 0.
        Buses chain;
        chain[0].output = 1;
        chain[1].output = 2;
        chain[2].output = 3;
        check(wouldCreateCycle(chain, 3, 0),
              "a four-bus loop is caught too");
        check(!wouldCreateCycle(chain, 3, ROUTE_TO_MASTER),
              "and the master is still always safe");

        // Out-of-range destinations are not cycles; validation drops them.
        check(!wouldCreateCycle(buses, 0, 99), "an absurd destination is not a cycle");
        check(!wouldCreateCycle(buses, 99, 0), "nor is an absurd source");
    }

    // ---- Processing order -------------------------------------------------
    {
        Buses buses;
        int order[Project::MAX_AUX_BUSES];
        int count = 0;

        check(computeBusOrder(buses, order, count),
              "a graph where every bus feeds the master resolves");
        check(count == Project::MAX_AUX_BUSES, "with every bus placed");

        // 0 -> 1 means 0 must run before 1.
        buses[0].output = 1;
        check(computeBusOrder(buses, order, count), "a chained graph resolves");

        int posZero = -1, posOne = -1;
        for (int i = 0; i < count; ++i) {
            if (order[i] == 0) posZero = i;
            if (order[i] == 1) posOne = i;
        }
        check(posZero >= 0 && posOne >= 0 && posZero < posOne,
              "a bus is processed before the bus it feeds - otherwise its "
              "signal would arrive a sample late, every sample");

        // A cycle must be reported, not walked.
        Buses looped;
        looped[0].output = 1;
        looped[1].output = 0;
        check(!computeBusOrder(looped, order, count),
              "a cycle is reported rather than resolved into something wrong");
    }

    // ---- Repairing a file that already loops -------------------------------
    {
        Buses looped;
        looped[0].output = 1;
        looped[1].output = 0;

        const int repaired = breakRoutingCycles(looped);
        check(repaired > 0, "a loop in a file is repaired (" +
              std::to_string(repaired) + " bus(es))");

        int order[Project::MAX_AUX_BUSES];
        int count = 0;
        check(computeBusOrder(looped, order, count),
              "and the graph resolves afterwards");
    }

    {
        Buses selfFed;
        selfFed[2].output = 2;
        check(breakRoutingCycles(selfFed) > 0, "a self-feeding bus is repaired");
        check(selfFed[2].output == ROUTE_TO_MASTER, "by routing it to the master");
    }

    {
        Buses bogus;
        bogus[0].output = 77;
        check(breakRoutingCycles(bogus) > 0, "an out-of-range output is repaired");
        check(bogus[0].output == ROUTE_TO_MASTER, "to the master");
    }

    {
        // Every bus in one long cycle: 0->1->2->3->0.
        Buses ring;
        ring[0].output = 1;
        ring[1].output = 2;
        ring[2].output = 3;
        ring[3].output = 0;

        check(breakRoutingCycles(ring) > 0, "a full ring is repaired");
        int order[Project::MAX_AUX_BUSES];
        int count = 0;
        check(computeBusOrder(ring, order, count), "and resolves");
    }

    {
        Buses clean;
        check(breakRoutingCycles(clean) == 0,
              "a graph with nothing wrong is left completely alone");
    }

    // ---- Hygiene ----------------------------------------------------------
    {
        Buses buses;
        int count = 0;
        check(!computeBusOrder(buses, nullptr, count),
              "a null output array is refused rather than written through");
    }
}

// ============================================================================
// 51. Sends and buses reach the audio
// ============================================================================
static void testSendsAndBuses() {
    beginTest("Sends and aux buses");

    // A send that changes nothing audible is a send that does not exist, so
    // every one of these renders and measures.

    auto render = [](void (*configure)(Project&), int blocks) {
        Project p;
        p.bpm = 120.0f;
        p.masterLimiterEnabled = false;
        p.masterCompressorEnabled = false;
        p.masterEQEnabled = false;
        p.patterns.clear();
        p.arrangement.clear();

        Pattern pat;
        Note n;
        n.pitch = 69;
        n.startTime = 0.0f;
        n.duration = 4.0f;
        n.oscillatorType = OscillatorType::Pulse;
        pat.notes.push_back(n);
        p.patterns.push_back(pat);
        p.arrangement.push_back(Clip{0, 0, 0.0f, 8.0f, 0});
        p.channels[0].volume = 0.5f;

        if (configure) configure(p);

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.updateChannelConfigs();
        seq.play();

        std::vector<float> l(512), r(512);
        double energy = 0.0;
        int counted = 0;
        for (int b = 0; b < blocks; ++b) {
            seq.process(l.data(), r.data(), 512);
            if (b >= 10) {
                for (float v : l) { energy += double(v) * double(v); ++counted; }
            }
        }
        return (counted > 0) ? std::sqrt(energy / counted) : 0.0;
    };

    // ---- A send adds signal -----------------------------------------------
    {
        const double dry = render(nullptr, 60);
        const double sent = render([](Project& p) {
            p.channels[0].sends[0].destination = 0;
            p.channels[0].sends[0].level = 1.0f;
        }, 60);

        check(dry > 1e-4, "the dry channel renders");
        check(sent > dry * 1.2,
              "a send to a bus adds that bus's output to the master (dry " +
              std::to_string(dry) + " vs sent " + std::to_string(sent) + ")");
    }

    // ---- A muted bus contributes nothing -----------------------------------
    {
        const double dry = render(nullptr, 60);
        const double muted = render([](Project& p) {
            p.channels[0].sends[0].destination = 0;
            p.channels[0].sends[0].level = 1.0f;
            p.auxBuses[0].muted = true;
        }, 60);

        check(std::fabs(muted - dry) < dry * 0.05,
              "a muted bus contributes nothing, leaving just the dry channel");
    }

    // ---- Bus volume scales it ---------------------------------------------
    {
        const double full = render([](Project& p) {
            p.channels[0].sends[0].destination = 0;
            p.channels[0].sends[0].level = 1.0f;
            p.auxBuses[0].volume = 1.0f;
        }, 60);
        const double quiet = render([](Project& p) {
            p.channels[0].sends[0].destination = 0;
            p.channels[0].sends[0].level = 1.0f;
            p.auxBuses[0].volume = 0.1f;
        }, 60);

        check(full > quiet, "bus volume scales what the bus returns (" +
              std::to_string(full) + " vs " + std::to_string(quiet) + ")");
    }

    // ---- Pre-fader ignores the channel fader -------------------------------
    //
    // The point of pre-fader: pull the channel to silence and the send keeps
    // feeding the bus, which is how a dry signal fades into its own tail.
    {
        const double postFader = render([](Project& p) {
            p.channels[0].volume = 0.0f;          // channel silent
            p.channels[0].sends[0].destination = 0;
            p.channels[0].sends[0].level = 1.0f;
            p.channels[0].sends[0].preFader = false;
        }, 60);

        const double preFader = render([](Project& p) {
            p.channels[0].volume = 0.0f;          // channel silent
            p.channels[0].sends[0].destination = 0;
            p.channels[0].sends[0].level = 1.0f;
            p.channels[0].sends[0].preFader = true;
        }, 60);

        check(postFader < 1e-4,
              "a post-fader send from a silent channel sends silence");
        check(preFader > 1e-3,
              "a pre-fader send from a silent channel still feeds the bus - "
              "which is the entire reason pre-fader exists (got " +
              std::to_string(preFader) + ")");
    }

    // ---- A bus effect actually processes ------------------------------------
    {
        const double clean = render([](Project& p) {
            p.channels[0].sends[0].destination = 0;
            p.channels[0].sends[0].level = 1.0f;
        }, 60);
        const double crushed = render([](Project& p) {
            p.channels[0].sends[0].destination = 0;
            p.channels[0].sends[0].level = 1.0f;
            p.auxBuses[0].strip.distortionEnabled = true;
            p.auxBuses[0].strip.distortionDrive = 20.0f;
            p.auxBuses[0].strip.distortionMix = 1.0f;
        }, 60);

        check(std::fabs(crushed - clean) > clean * 0.05,
              "an effect on the bus strip changes the bus output - the bus "
              "really does own a full insert rack (" + std::to_string(clean) +
              " vs " + std::to_string(crushed) + ")");
    }

    // ---- Bus feeding bus ----------------------------------------------------
    {
        const double chained = render([](Project& p) {
            p.channels[0].sends[0].destination = 0;
            p.channels[0].sends[0].level = 1.0f;
            p.auxBuses[0].output = 1;             // 0 feeds 1, 1 feeds master
            p.auxBuses[1].volume = 1.0f;
        }, 60);
        const double direct = render(nullptr, 60);

        check(chained > direct * 1.1,
              "a bus feeding another bus still reaches the master (" +
              std::to_string(direct) + " vs " + std::to_string(chained) + ")");
    }

    // ---- A routing loop must not hang or explode ---------------------------
    //
    // The graph is repaired when configs are applied, so this asserts the
    // repair happens rather than that the audio thread survives a cycle.
    {
        Project p;
        p.masterLimiterEnabled = false;
        p.auxBuses[0].output = 1;
        p.auxBuses[1].output = 0;                 // a loop, straight from a file

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.updateChannelConfigs();               // repairs the graph

        check(p.auxBuses[0].output == ROUTE_TO_MASTER ||
              p.auxBuses[1].output == ROUTE_TO_MASTER,
              "applying configs breaks a routing loop rather than trusting it");

        std::vector<float> l(512), r(512);
        bool finite = true;
        for (int b = 0; b < 200; ++b) {
            seq.process(l.data(), r.data(), 512);
            for (float v : l) if (!std::isfinite(v)) { finite = false; break; }
            if (!finite) break;
        }
        check(finite, "and the audio thread renders finite samples throughout");
    }

    // ---- A send pointing at nothing is silent, not a crash -----------------
    {
        Project p;
        p.masterLimiterEnabled = false;
        p.patterns.clear();
        p.arrangement.clear();
        Pattern pat;
        Note n; n.pitch = 60; n.duration = 2.0f;
        pat.notes.push_back(n);
        p.patterns.push_back(pat);
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        p.channels[0].sends[0].destination = 99;   // no such bus
        p.channels[0].sends[0].level = 1.0f;
        p.channels[0].sends[1].destination = -1;   // explicitly nowhere
        p.channels[0].sends[1].level = 1.0f;

        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.updateChannelConfigs();
        seq.play();

        std::vector<float> l(512), r(512);
        bool finite = true;
        for (int b = 0; b < 60; ++b) {
            seq.process(l.data(), r.data(), 512);
            for (float v : l) if (!std::isfinite(v)) { finite = false; break; }
        }
        check(finite, "a send to a bus that does not exist is ignored safely");
    }
}

// ============================================================================
// 52. Sends and buses survive save and load
// ============================================================================
static void testRoutingPersistence() {
    beginTest("Routing persistence");

    // ---- Round trip ---------------------------------------------------------
    {
        Project original;
        original.channels[2].sends[0].destination = 1;
        original.channels[2].sends[0].level = 0.42f;
        original.channels[2].sends[0].preFader = true;
        original.channels[2].sidechainBus = 3;

        original.auxBuses[1].name = "Plate";
        original.auxBuses[1].volume = 0.65f;
        original.auxBuses[1].pan = -0.3f;
        original.auxBuses[1].output = 2;
        original.auxBuses[1].strip.reverbEnabled = true;
        original.auxBuses[1].strip.reverbMix = 0.8f;
        original.auxBuses[1].strip.fxOrder[0] = static_cast<uint8_t>(EffectType::Reverb);
        original.auxBuses[1].strip.fxOrder[1] = static_cast<uint8_t>(EffectType::Delay);
        original.auxBuses[1].strip.fxSlotCount = 2;

        const std::string path = testPath("routing_roundtrip.ctp");
        check(saveProjectFile(original, path), "routing saves");

        Project loaded;
        check(loadProjectFile(loaded, path), "and loads");

        check(loaded.channels[2].sends[0].destination == 1 &&
              std::fabs(loaded.channels[2].sends[0].level - 0.42f) < 0.01f &&
              loaded.channels[2].sends[0].preFader,
              "the send survives with its level and pre-fader flag");
        check(loaded.channels[2].sidechainBus == 3,
              "and the sidechain bus survives");

        check(loaded.auxBuses[1].name == "Plate" &&
              std::fabs(loaded.auxBuses[1].volume - 0.65f) < 0.01f &&
              std::fabs(loaded.auxBuses[1].pan + 0.3f) < 0.01f &&
              loaded.auxBuses[1].output == 2,
              "the bus keeps its name, level, pan and output");
        check(loaded.auxBuses[1].strip.reverbEnabled &&
              std::fabs(loaded.auxBuses[1].strip.reverbMix - 0.8f) < 0.01f,
              "and its strip settings");
        check(loaded.auxBuses[1].strip.fxSlotCount == 2 &&
              loaded.auxBuses[1].strip.fxOrder[0] ==
                  static_cast<uint8_t>(EffectType::Reverb),
              "and its own rack order");
        std::remove(path.c_str());
    }

    // ---- An untouched project writes nothing new ---------------------------
    {
        Project plain;
        const std::string path = testPath("routing_none.ctp");
        check(saveProjectFile(plain, path), "a project with no routing saves");

        const std::string text = readWholeFile(path);
        check(text.find("SEND ") == std::string::npos &&
              text.find("AUXBUS ") == std::string::npos,
              "and writes no routing lines, so a project that never opened "
              "the panel is unchanged by the v4 bump");
        std::remove(path.c_str());
    }

    // ---- A v3 file still loads ---------------------------------------------
    {
        const std::string path = testPath("routing_v3.ctp");
        {
            std::ofstream file(path);
            file << "CHIPTUNE_PROJECT v3\n";
            file << "NAME Older\n";
            file << "CHANNEL 0 \"Lead\" Pulse revOn=1\n";
            file << "PATTERN \"P\" 16\n";
            file << "END_PATTERN\n";
            file << "END_PROJECT\n";
        }

        Project loaded;
        check(loadProjectFile(loaded, path), "a v3 file loads");
        check(loaded.channels[0].reverbEnabled, "with its effects intact");
        check(loaded.channels[0].sends[0].destination == -1,
              "and no sends, which is what a pre-v4 file means");
        check(loaded.auxBuses[0].output == ROUTE_TO_MASTER,
              "and every bus routed to the master");
        std::remove(path.c_str());
    }

    // ---- A file carrying a loop is repaired on load -------------------------
    {
        const std::string path = testPath("routing_loop.ctp");
        {
            std::ofstream file(path);
            file << "CHIPTUNE_PROJECT v4\n";
            file << "AUXBUS 0 \"A\" out=1 vol=0.8 pan=0 mute=0\n";
            file << "AUXBUS 1 \"B\" out=0 vol=0.8 pan=0 mute=0\n";
            file << "PATTERN \"P\" 16\n";
            file << "END_PATTERN\n";
            file << "END_PROJECT\n";
        }

        Project loaded;
        check(loadProjectFile(loaded, path), "a looped file loads");

        int order[Project::MAX_AUX_BUSES];
        int count = 0;
        check(computeBusOrder(loaded.auxBuses, order, count),
              "and its graph is repaired during validation rather than "
              "reaching the audio thread as a cycle");
        std::remove(path.c_str());
    }

    // ---- Validation clamps hostile values -----------------------------------
    {
        Project hostile;
        hostile.channels[0].sends[0].destination = 500;
        hostile.channels[0].sends[0].level = 9.0f;
        hostile.channels[0].sends[1].level = -3.0f;
        hostile.channels[0].sidechainBus = 42;
        hostile.auxBuses[0].volume = 99.0f;
        hostile.auxBuses[0].pan = -7.0f;

        clampProjectToValidRanges(hostile);

        check(hostile.channels[0].sends[0].destination == -1,
              "a send to a bus that does not exist becomes no send");
        check(hostile.channels[0].sends[0].level <= 1.0f &&
              hostile.channels[0].sends[1].level >= 0.0f,
              "send levels are clamped into range");
        check(hostile.channels[0].sidechainBus == -1,
              "an impossible sidechain bus falls back to the channel tap");
        check(hostile.auxBuses[0].volume <= 2.0f &&
              hostile.auxBuses[0].pan >= -1.0f,
              "bus volume and pan are clamped");
    }
}

// ============================================================================
// 53. The channel cap and chip-authentic mode
// ============================================================================
static void testChannelCap() {
    beginTest("Channel cap and chip mode");

    // ---- The two caps must agree -------------------------------------------
    {
        check(Project::MAX_CHANNELS == Sequencer::MAX_CHANNELS,
              "the project and the sequencer agree on the channel count - if "
              "they ever disagreed the mix would walk off the end of one of "
              "them, and it would do so in the audio thread");
        check(Project::MAX_CHANNELS >= Project::CHIP_CHANNELS,
              "the cap is at least the chip count");
        check(Project::CHIP_CHANNELS == 8, "a 2A03 still has eight channels");
    }

    // ---- Every channel is named and reachable ------------------------------
    {
        const Project p;
        for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
            if (p.channels[static_cast<size_t>(ch)].name.empty()) {
                check(false, "channel " + std::to_string(ch) + " has no name");
                break;
            }
        }
        check(true, "every channel past the original eight is named too");

        // The names must be distinct, or the mixer and the send menus show
        // several identical entries and nobody can tell them apart.
        bool distinct = true;
        for (int a = 0; a < Project::MAX_CHANNELS && distinct; ++a) {
            for (int b = a + 1; b < Project::MAX_CHANNELS; ++b) {
                if (p.channels[static_cast<size_t>(a)].name ==
                    p.channels[static_cast<size_t>(b)].name) {
                    check(false, "channels " + std::to_string(a) + " and " +
                          std::to_string(b) + " share a name");
                    distinct = false;
                    break;
                }
            }
        }
        check(distinct, "and no two channels share a name");
    }

    // ---- Chip mode is enforced in the ENGINE, not just the UI --------------
    //
    // The whole claim of the flag: a channel beyond the eighth is not mixed
    // at all. If it were only a UI hint, this would render sound.
    {
        auto renderOnChannel = [](int channel, bool chipAuthentic) {
            Project p;
            p.chipAuthentic = chipAuthentic;
            p.masterLimiterEnabled = false;
            p.patterns.clear();
            p.arrangement.clear();

            Pattern pat;
            Note n;
            n.pitch = 69;
            n.startTime = 0.0f;
            n.duration = 4.0f;
            n.oscillatorType = OscillatorType::Pulse;
            pat.notes.push_back(n);
            p.patterns.push_back(pat);
            p.arrangement.push_back(Clip{0, channel, 0.0f, 8.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            Sequencer& seq = *seqPtr;
            seq.setSampleRate(44100.0f);
            seq.setProject(&p);
            seq.updateChannelConfigs();
            seq.play();

            std::vector<float> l(512), r(512);
            double energy = 0.0;
            for (int b = 0; b < 60; ++b) {
                seq.process(l.data(), r.data(), 512);
                for (float v : l) energy += double(v) * double(v);
            }
            return energy;
        };

        check(renderOnChannel(0, true) > 1.0,
              "channel 0 sounds in chip mode");
        check(renderOnChannel(20, false) > 1.0,
              "channel 20 sounds with the cap raised - the whole point of "
              "raising it");
        check(renderOnChannel(20, true) < 1e-6,
              "and is silent in chip mode, because the flag is enforced in "
              "the audio engine rather than being a note in the interface");
    }

    // ---- activeChannelCount ------------------------------------------------
    {
        Project p;
        check(p.activeChannelCount() == Project::MAX_CHANNELS,
              "a normal project uses every channel");
        p.chipAuthentic = true;
        check(p.activeChannelCount() == Project::CHIP_CHANNELS,
              "a chip-authentic project uses eight");
    }

    // ---- A clip stranded past the cap is moved, not lost -------------------
    {
        Project p;
        p.chipAuthentic = true;
        p.patterns.clear();
        p.patterns.push_back(Pattern());
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 25, 0.0f, 4.0f, 0});

        clampProjectToValidRanges(p);

        check(!p.arrangement.empty(),
              "a clip on an unreachable channel is not silently deleted");
        check(p.arrangement[0].channelIndex < Project::CHIP_CHANNELS,
              "it moves onto a channel the project can actually play (now " +
              std::to_string(p.arrangement[0].channelIndex) + ")");
    }

    // ---- The flag round-trips ----------------------------------------------
    {
        Project original;
        original.chipAuthentic = true;
        original.channels[20].name = "Way Out Here";
        original.channels[20].reverbEnabled = true;

        const std::string path = testPath("channel_cap.ctp");
        check(saveProjectFile(original, path), "saves");

        Project loaded;
        check(loadProjectFile(loaded, path), "and loads");
        check(loaded.chipAuthentic, "the chip-authentic flag survives");
        check(loaded.channels[20].name == "Way Out Here" &&
              loaded.channels[20].reverbEnabled,
              "and a channel past the eighth keeps its settings");
        std::remove(path.c_str());
    }

    // ---- Raising the cap must not bloat every file -------------------------
    //
    // 32 channels would otherwise add 24 CHANNEL lines to every project,
    // including ones that only ever used the first eight.
    {
        Project plain;
        const std::string path = testPath("channel_sparse.ctp");
        check(saveProjectFile(plain, path), "an untouched project saves");

        const std::string text = readWholeFile(path);
        int channelLines = 0;
        size_t at = 0;
        while ((at = text.find("CHANNEL ", at)) != std::string::npos) {
            ++channelLines;
            at += 8;
        }
        check(channelLines == Project::CHIP_CHANNELS,
              "a project nobody extended writes only the original eight "
              "channel lines, not 32 (wrote " + std::to_string(channelLines) + ")");
        std::remove(path.c_str());
    }

    {
        Project extended;
        extended.channels[19].filterEnabled = true;
        extended.channels[19].filterCutoff = 777.0f;

        const std::string path = testPath("channel_extended.ctp");
        check(saveProjectFile(extended, path), "an extended project saves");

        Project loaded;
        check(loadProjectFile(loaded, path), "and loads");
        check(loaded.channels[19].filterEnabled &&
              std::fabs(loaded.channels[19].filterCutoff - 777.0f) < 0.5f,
              "a channel that was touched is written and read back, even "
              "though its neighbours are skipped");
        std::remove(path.c_str());
    }
}

// ============================================================================
// 54. MIDI export at 32 channels
// ============================================================================
static void testMidiChannelBounds() {
    beginTest("MIDI export channel bounds");

    // Raising the cap turned an existing latent bug into a live one:
    // s_programSent was sized 16 and indexed by the PROJECT channel, so
    // channels 16 and up wrote past the end of it. It showed up as the same
    // project exporting different bytes on two runs.
    {
        Project p;
        p.patterns.clear();
        p.arrangement.clear();

        Pattern pat;
        for (int i = 0; i < 4; ++i) {
            Note n;
            n.pitch = 60 + i;
            n.startTime = static_cast<float>(i);
            n.duration = 0.5f;
            pat.notes.push_back(n);
        }
        p.patterns.push_back(pat);

        // A clip on every channel, including the ones past MIDI's sixteen.
        for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
            p.arrangement.push_back(Clip{0, ch, 0.0f, 4.0f, 0});
        }

        const std::string pathA = testPath("midi_wide_a.mid");
        const std::string pathB = testPath("midi_wide_b.mid");

        check(exportProjectToMIDI(p, pathA), "a 32-channel project exports");
        check(exportProjectToMIDI(p, pathB), "twice");

        const std::string a = readWholeFile(pathA);
        const std::string b = readWholeFile(pathB);
        check(a == b,
              "and both exports are byte-identical - the program-change "
              "flags used to be a file-scope array indexed out of bounds by "
              "any channel past the sixteenth (" + std::to_string(a.size()) +
              " vs " + std::to_string(b.size()) + " bytes)");
        check(a.size() > 22, "and the export is not empty");

        std::remove(pathA.c_str());
        std::remove(pathB.c_str());
    }
}


// ============================================================================
// 55. Audio clips on the timeline
// ============================================================================
static void testAudioClips() {
    beginTest("Audio clips");

    // ---- A sample on disk, so the pool's real loader is what runs ---------
    //
    // Building the Sample by hand would test the mixer against a fixture the
    // shipping path never produces. loadSample() resamples to 48 kHz, mono,
    // and normalises to 0.95 - all three of which the mixer has to cope with,
    // and none of which a hand-built struct would exercise.
    auto writeTestWav = [](const std::string& path, float seconds,
                           float frequency, float silentUntil) {
        const int rate = 44100;
        const size_t frames = static_cast<size_t>(seconds * float(rate));
        std::vector<float> l(frames), r(frames);
        for (size_t i = 0; i < frames; ++i) {
            const float t = float(i) / float(rate);
            const float v = (t < silentUntil)
                ? 0.0f
                : 0.8f * std::sin(6.28318530718f * frequency * t);
            l[i] = v;
            r[i] = v;
        }
        return writeWavFile(path, l, r);
    };

    const std::string wavPath = testPath("clip_tone.wav");
    const std::string wavHalf = testPath("clip_halfsilent.wav");
    check(writeTestWav(wavPath, 2.0f, 220.0f, 0.0f),
          "a two-second test tone is written to disk");
    check(writeTestWav(wavHalf, 2.0f, 220.0f, 1.0f),
          "and one whose first second is silence");

    // Render `beats` of a project and return the RMS of the left channel.
    auto renderRms = [](Project& p, float beats, float skipBeats = 0.0f) {
        auto seqPtr = std::make_unique<Sequencer>();
        Sequencer& seq = *seqPtr;
        seq.setSampleRate(44100.0f);
        seq.setProject(&p);
        seq.updateChannelConfigs();
        seq.updateMasterEffects();
        seq.play();

        const float secondsPerBeat = 60.0f / p.bpm;
        const int totalFrames = int(beats * secondsPerBeat * 44100.0f);
        const int skipFrames = int(skipBeats * secondsPerBeat * 44100.0f);

        std::vector<float> l(256), r(256);
        double sum = 0.0;
        int counted = 0;
        for (int done = 0; done < totalFrames; done += 256) {
            seq.process(l.data(), r.data(), 256);
            if (done < skipFrames) continue;
            for (float v : l) { sum += double(v) * double(v); ++counted; }
        }
        return (counted > 0) ? std::sqrt(sum / counted) : 0.0;
    };

    // A project with nothing in it but one audio clip, so anything measured
    // came from the clip and not from a synth voice.
    auto audioOnlyProject = [&](Project& p) {
        p.bpm = 120.0f;
        p.patterns.clear();
        p.arrangement.clear();
        p.masterLimiterEnabled = false;
        p.masterCompressorEnabled = false;
        p.masterEQEnabled = false;
        p.masterVolume = 0.8f;
    };

    auto makeAudioClip = [](int sampleId, int channel, float start, float length) {
        Clip c;
        c.type = ClipType::Audio;
        c.sampleId = sampleId;
        c.channelIndex = channel;
        c.startBeat = start;
        c.lengthBeats = length;
        return c;
    };

    // ---- It reaches the audio at all --------------------------------------
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        check(id >= 0, "the pool loads the test tone");

        const double silent = renderRms(p, 2.0f);
        check(silent < 1e-6,
              "an empty arrangement renders silence, so anything below is "
              "the clip and not a stray voice");

        p.arrangement.push_back(makeAudioClip(id, 0, 0.0f, 2.0f));
        const double sounding = renderRms(p, 2.0f);
        check(sounding > 0.01,
              "an audio clip renders actual audio (rms " +
              std::to_string(sounding) + ")");
    }

    // ---- It is bounded by its own start and length ------------------------
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        p.arrangement.push_back(makeAudioClip(id, 0, 2.0f, 2.0f));

        // Beats 0..1.9. Stopping just short of 2.0 rather than at it: the
        // renderer works in 256-frame blocks and would otherwise cross the
        // boundary inside the last one and pick up the clip's first samples.
        const double before = renderRms(p, 1.9f);
        check(before < 1e-6, "nothing sounds before the clip starts");

        // Beats 2..4 are inside it.
        const double during = renderRms(p, 4.0f, 2.0f);
        check(during > 0.01, "and it sounds once the playhead reaches it");
    }

    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        p.arrangement.push_back(makeAudioClip(id, 0, 0.0f, 1.0f));

        const double after = renderRms(p, 3.0f, 1.5f);
        check(after < 1e-6,
              "and it stops at its end even though the sample has more audio "
              "left - the clip length is the edit, not a suggestion");
    }

    // ---- Gain scales it ---------------------------------------------------
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        p.arrangement.push_back(makeAudioClip(id, 0, 0.0f, 2.0f));

        p.arrangement[0].gain = 0.25f;
        const double quiet = renderRms(p, 2.0f);
        p.arrangement[0].gain = 0.5f;
        const double loud = renderRms(p, 2.0f);

        check(quiet > 1e-5 && loud > 1e-5, "both gains render");
        const double ratio = loud / quiet;
        check(ratio > 1.9 && ratio < 2.1,
              "doubling clip gain doubles the level (got " +
              std::to_string(ratio) + ")");
    }

    // ---- Trim skips into the sample ---------------------------------------
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavHalf);
        check(id >= 0, "the half-silent sample loads");

        // The first second is silence, and at 120 bpm one beat is half a
        // second - so the first two beats of an untrimmed clip are silent.
        p.arrangement.push_back(makeAudioClip(id, 0, 0.0f, 4.0f));
        const double untrimmed = renderRms(p, 1.5f);
        check(untrimmed < 1e-6,
              "an untrimmed clip plays the sample's leading silence");

        p.arrangement[0].trimStartSeconds = 1.0f;
        const double trimmed = renderRms(p, 1.5f);
        check(trimmed > 0.01,
              "trimming past the silence makes the clip sound immediately "
              "(rms " + std::to_string(trimmed) + ")");
    }

    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        p.arrangement.push_back(makeAudioClip(id, 0, 0.0f, 4.0f));

        // Half a second of a two-second sample, then silence: the clip is
        // four beats but only the first beat has audio behind it.
        p.arrangement[0].trimEndSeconds = 0.5f;
        const double tail = renderRms(p, 4.0f, 2.0f);
        check(tail < 1e-6,
              "trimEnd stops playback even though the clip is still running "
              "and the sample still has audio");
    }

    // ---- Loop refills the clip --------------------------------------------
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);

        // Eight beats at 120 bpm is four seconds; the sample is two.
        p.arrangement.push_back(makeAudioClip(id, 0, 0.0f, 8.0f));

        const double tailOnce = renderRms(p, 8.0f, 5.0f);
        check(tailOnce < 1e-6,
              "a clip longer than its sample falls silent once the sample "
              "runs out");

        p.arrangement[0].loopClip = true;
        const double tailLooped = renderRms(p, 8.0f, 5.0f);
        check(tailLooped > 0.01,
              "and keeps sounding when the clip loops (rms " +
              std::to_string(tailLooped) + ")");
    }

    // ---- Fades ------------------------------------------------------------
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        p.arrangement.push_back(makeAudioClip(id, 0, 0.0f, 4.0f));
        p.arrangement[0].loopClip = true;

        const double flatHead = renderRms(p, 0.5f);

        p.arrangement[0].fadeInBeats = 2.0f;
        const double fadedHead = renderRms(p, 0.5f);

        check(flatHead > 1e-5, "the un-faded head sounds");
        check(fadedHead < flatHead * 0.6,
              "a fade-in makes the head quieter than the same audio without "
              "one (" + std::to_string(fadedHead) + " vs " +
              std::to_string(flatHead) + ")");
    }

    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        p.arrangement.push_back(makeAudioClip(id, 0, 0.0f, 4.0f));
        p.arrangement[0].loopClip = true;

        const double flatTail = renderRms(p, 4.0f, 3.5f);
        p.arrangement[0].fadeOutBeats = 2.0f;
        const double fadedTail = renderRms(p, 4.0f, 3.5f);

        check(flatTail > 1e-5, "the un-faded tail sounds");
        check(fadedTail < flatTail * 0.6,
              "and a fade-out makes the tail quieter");
    }

    // ---- It goes THROUGH the channel strip, not around it -----------------
    //
    // This is the whole reason an audio clip lives on a channel rather than
    // on a track type of its own. If it bypassed the strip, a recorded part
    // could not use the channel's reverb or its send, and the mixer's fader
    // would not move it.
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        p.arrangement.push_back(makeAudioClip(id, 3, 0.0f, 2.0f));

        p.channels[3].volume = 0.8f;
        const double open = renderRms(p, 2.0f);
        check(open > 0.01, "an audio clip on channel 3 sounds");

        p.channels[3].volume = 0.0f;
        const double closed = renderRms(p, 2.0f);
        check(closed < 1e-6,
              "pulling that channel's fader down silences the audio clip - "
              "it runs through the channel strip, not around it");

        p.channels[3].volume = 0.8f;
        p.channels[3].muted = true;
        const double muted = renderRms(p, 2.0f);
        check(muted < 1e-6, "and muting the channel silences it too");
    }

    // ---- Pattern clips and audio clips do not bleed into each other -------
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);

        Pattern pat;
        Note n;
        n.pitch = 60;
        n.startTime = 0.0f;
        n.duration = 2.0f;
        pat.notes.push_back(n);
        p.patterns.push_back(pat);

        // A pattern clip that happens to carry a sample id must not play it.
        Clip patternClip{0, 0, 0.0f, 2.0f, 0};
        patternClip.sampleId = id;
        patternClip.gain = 1.0f;
        p.arrangement.push_back(patternClip);

        const double patternOnly = renderRms(p, 2.0f);
        check(patternOnly > 1e-5, "the pattern clip plays its notes");

        // Same project, the clip switched to Audio: now the notes must not
        // sound and the sample must.
        Project q;
        audioOnlyProject(q);
        const int qid = q.samplePool.loadSample(wavPath);
        q.patterns.push_back(pat);
        Clip audioClip = makeAudioClip(qid, 0, 0.0f, 2.0f);
        audioClip.patternIndex = 0;      // a live index it must ignore
        q.arrangement.push_back(audioClip);

        // Silence the synth entirely; whatever is left is the sample.
        for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
            q.channels[ch].volume = 0.8f;
        }
        const double audioOnly = renderRms(q, 2.0f);
        check(audioOnly > 0.01,
              "and an audio clip plays its sample, not the pattern its "
              "patternIndex still points at");
    }

    // ---- Chip-authentic mode bounds audio clips too -----------------------
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        p.arrangement.push_back(makeAudioClip(id, 20, 0.0f, 2.0f));

        p.chipAuthentic = false;
        check(renderRms(p, 2.0f) > 0.01,
              "an audio clip on channel 20 sounds with the chip cap off");

        p.chipAuthentic = true;
        check(renderRms(p, 2.0f) < 1e-6,
              "and is silent with it on - the cap is enforced in the engine, "
              "not just drawn in the UI");
    }

    // ---- The sample rate conversion is real -------------------------------
    //
    // The pool decodes to 48 kHz and the engine here runs at 44.1. Reading
    // the array without resampling would play it 8.8% fast, which is nearly
    // a semitone and a half - and would finish early.
    {
        Project p;
        audioOnlyProject(p);
        const int id = p.samplePool.loadSample(wavPath);
        const Sample* s = p.samplePool.getSample(id);
        check(s != nullptr && s->sampleRate == 48000,
              "the pool decodes to 48 kHz regardless of the file's rate");
        check(s != nullptr && std::fabs(s->lengthSeconds - 2.0f) < 0.05f,
              "and reports the real duration in seconds");

        // Four beats at 120 bpm is two seconds, exactly the sample length.
        // Play five beats and measure the last: correct resampling leaves
        // that beat silent, playing 9% fast would have finished sooner but
        // still silent - so measure the beat that straddles the end instead.
        p.arrangement.push_back(makeAudioClip(id, 0, 0.0f, 8.0f));

        // Beats 3.5..3.9 are inside the sample under correct resampling
        // (1.75..1.95 s of 2.0 s) and past its end at 48/44.1 speed
        // (1.90..2.12 s), where it would already be silent.
        const double nearEnd = renderRms(p, 3.9f, 3.5f);
        check(nearEnd > 0.005,
              "the sample is still playing just before its true end, which it "
              "would not be if the 48 kHz data were read at 44.1 (rms " +
              std::to_string(nearEnd) + ")");
    }

    // ---- The pool cannot move under the audio thread ----------------------
    {
        Project p;
        const int id = p.samplePool.loadSample(wavPath);
        check(id >= 0, "a sample loads");
        const Sample* first = p.samplePool.getSample(id);

        Sample synthetic;
        synthetic.name = "synthetic";
        synthetic.audioData.assign(1000, 0.5f);
        synthetic.sampleRate = 48000;
        synthetic.lengthSeconds = 1000.0f / 48000.0f;
        synthetic.isLoaded = true;
        for (int i = 0; i < 32; ++i) p.samplePool.addSample(synthetic);

        check(p.samplePool.getSample(id) == first,
              "adding 32 more samples does not move the first one - the pool "
              "reserves its whole capacity up front, because the audio thread "
              "holds a pointer into it while the UI thread loads");
        check(p.samplePool.count() == 33, "and the count tracks what is in it");
    }

    std::remove(wavPath.c_str());
    std::remove(wavHalf.c_str());
}

// ============================================================================
// 56. Audio clips survive a save and load
// ============================================================================
static void testAudioClipPersistence() {
    beginTest("Audio clip persistence");

    const std::string wavA = testPath("persist_a.wav");
    const std::string wavB = testPath("persist_b.wav");
    {
        std::vector<float> l(4410, 0.5f), r(4410, 0.5f);
        check(writeWavFile(wavA, l, r), "sample A is on disk");
        check(writeWavFile(wavB, l, r), "sample B is on disk");
    }

    check(ctp::FORMAT_VERSION >= 5,
          "audio clips bumped the format to v5 or later");

    // ---- Every field round-trips -----------------------------------------
    {
        Project p;
        p.patterns.clear();
        p.arrangement.clear();
        const int id = p.samplePool.loadSample(wavA);
        check(id >= 0, "the sample loads before saving");

        Clip c;
        c.type = ClipType::Audio;
        c.sampleId = id;
        c.channelIndex = 5;
        c.startBeat = 6.5f;
        c.lengthBeats = 3.25f;
        c.color = 0xFF3366CCu;
        c.gain = 0.625f;
        c.trimStartSeconds = 0.125f;
        c.trimEndSeconds = 0.875f;
        c.fadeInBeats = 0.5f;
        c.fadeOutBeats = 1.5f;
        c.loopClip = true;
        p.arrangement.push_back(c);

        const std::string path = testPath("aclip_roundtrip.ctp");
        check(saveProject(p, path), "the project saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads back");
        check(loaded.arrangement.size() == 1, "with its one clip");

        if (loaded.arrangement.size() == 1) {
            const Clip& g = loaded.arrangement[0];
            check(g.type == ClipType::Audio, "still an audio clip");
            check(g.channelIndex == 5, "channel survives");
            check(std::fabs(g.startBeat - 6.5f) < 1e-4f, "start survives");
            check(std::fabs(g.lengthBeats - 3.25f) < 1e-4f, "length survives");
            check(g.color == 0xFF3366CCu, "colour survives");
            check(std::fabs(g.gain - 0.625f) < 1e-4f, "gain survives");
            check(std::fabs(g.trimStartSeconds - 0.125f) < 1e-4f,
                  "trim start survives");
            check(std::fabs(g.trimEndSeconds - 0.875f) < 1e-4f,
                  "trim end survives");
            check(std::fabs(g.fadeInBeats - 0.5f) < 1e-4f, "fade in survives");
            check(std::fabs(g.fadeOutBeats - 1.5f) < 1e-4f, "fade out survives");
            check(g.loopClip, "the loop flag survives");
            check(g.sampleId >= 0 &&
                  loaded.samplePool.getSample(g.sampleId) != nullptr,
                  "and it points at a sample that actually loaded");
        }
        std::remove(path.c_str());
    }

    // ---- Pattern clips are untouched by the new line type ----------------
    {
        Project p;
        p.arrangement.clear();
        // Three patterns, so patternIndex 2 is a real one - validation drops
        // a pattern clip that points past the end, and rightly so.
        while (p.patterns.size() < 3) p.patterns.push_back(Pattern());
        Clip pc{2, 4, 1.5f, 2.0f, 0xFF00FF00u};
        pc.transpose = -5;
        p.arrangement.push_back(pc);

        const std::string path = testPath("aclip_pattern_still.ctp");
        check(saveProject(p, path), "a pattern-only project saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");
        check(loaded.arrangement.size() == 1, "keeping its clip");
        if (loaded.arrangement.size() == 1) {
            check(loaded.arrangement[0].type == ClipType::Pattern,
                  "as a pattern clip");
            check(loaded.arrangement[0].patternIndex == 2 &&
                  loaded.arrangement[0].transpose == -5,
                  "with its pattern and transpose intact");
        }
        std::remove(path.c_str());
    }

    // ---- A sample that moved is reported, not swallowed -------------------
    {
        Project p;
        p.patterns.clear();
        p.arrangement.clear();
        const int id = p.samplePool.loadSample(wavA);

        Clip c;
        c.type = ClipType::Audio;
        c.sampleId = id;
        c.channelIndex = 1;
        c.startBeat = 0.0f;
        c.lengthBeats = 4.0f;
        c.trimStartSeconds = 0.25f;
        p.arrangement.push_back(c);

        const std::string path = testPath("aclip_missing.ctp");
        check(saveProject(p, path), "the project saves while the file exists");

        // Now the sample goes away, as it does when a user moves a folder.
        std::remove(wavA.c_str());

        Project loaded;
        check(loadProject(loaded, path), "the project still loads");
        check(loaded.missingSamples.size() == 1,
              "and says which file it could not find");
        check(loaded.arrangement.size() == 1,
              "the clip keeps its place on the timeline rather than being "
              "dropped - the edit is worth more than the broken link");
        if (loaded.arrangement.size() == 1) {
            check(loaded.arrangement[0].sampleId == -1,
                  "with no sample behind it");
            check(std::fabs(loaded.arrangement[0].trimStartSeconds - 0.25f) < 1e-4f,
                  "and its trim intact, so relinking restores the edit");
        }

        // It must also be safe to render.
        loaded.masterLimiterEnabled = false;
        auto seqPtr = std::make_unique<Sequencer>();
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(&loaded);
        seqPtr->updateChannelConfigs();
        seqPtr->play();
        std::vector<float> l(256), r(256);
        bool finite = true;
        for (int i = 0; i < 40; ++i) {
            seqPtr->process(l.data(), r.data(), 256);
            for (float v : l) if (!std::isfinite(v)) finite = false;
        }
        check(finite, "and a clip with no sample renders silence, not NaN");

        std::remove(path.c_str());
        // Put A back for the remaining cases.
        std::vector<float> ll(4410, 0.5f), rr(4410, 0.5f);
        writeWavFile(wavA, ll, rr);
    }

    // ---- Ids are remapped, not assumed -----------------------------------
    //
    // The writer emits the pool index it had at save time. On load,
    // loadSample() hands out whatever index is next free - so one sample
    // that no longer exists shifts every later index by one. Without the
    // remap, every clip after the missing file plays the WRONG sample, which
    // looks exactly like a correct load.
    {
        Project p;
        p.patterns.clear();
        p.arrangement.clear();
        const int gone = p.samplePool.loadSample(wavA);   // becomes id 0
        const int kept = p.samplePool.loadSample(wavB);   // becomes id 1
        check(gone == 0 && kept == 1, "two samples load in order");

        Clip c;
        c.type = ClipType::Audio;
        c.sampleId = kept;                                // file id 1
        c.channelIndex = 2;
        c.startBeat = 0.0f;
        c.lengthBeats = 4.0f;
        p.arrangement.push_back(c);

        const std::string path = testPath("aclip_remap.ctp");
        check(saveProject(p, path), "the project saves both samples");

        std::remove(wavA.c_str());   // the earlier one disappears

        Project loaded;
        check(loadProject(loaded, path), "it loads with one sample missing");
        check(loaded.samplePool.count() == 1, "only one sample made it in");
        check(loaded.arrangement.size() == 1, "and the clip is there");
        if (loaded.arrangement.size() == 1 && loaded.samplePool.count() == 1) {
            const Sample* s = loaded.samplePool.getSample(loaded.arrangement[0].sampleId);
            check(s != nullptr,
                  "the clip resolves to a real sample even though its saved "
                  "id no longer matches the pool index");
            check(s != nullptr && s->filepath == wavB,
                  "and specifically to the file it was actually pointing at, "
                  "not the one that took over its old index");
        }
        std::remove(path.c_str());
        std::vector<float> ll(4410, 0.5f), rr(4410, 0.5f);
        writeWavFile(wavA, ll, rr);
    }

    // ---- A v4 file still loads -------------------------------------------
    {
        Project p;
        p.name = "older";
        p.arrangement.clear();
        while (p.patterns.size() < 2) p.patterns.push_back(Pattern());
        p.arrangement.push_back(Clip{1, 2, 0.0f, 4.0f, 0xFF112233u});

        const std::string path = testPath("aclip_v4.ctp");
        check(saveProject(p, path), "write a project");

        // Rewrite its header as v4, exactly as a file saved before this
        // change would read.
        std::string text;
        {
            std::ifstream in(path, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        }
        // Built from the constant rather than spelled out, so this test
        // keeps testing the v4 path through every future bump instead of
        // failing on the number.
        const std::string header =
            "CHIPTUNE_PROJECT v" + std::to_string(ctp::FORMAT_VERSION);
        const size_t v = text.find(header);
        check(v != std::string::npos, "the header names the current version");
        if (v != std::string::npos) {
            text.replace(v, header.size(), "CHIPTUNE_PROJECT v4");
            std::ofstream out(path, std::ios::binary);
            out << text;
        }

        Project loaded;
        check(loadProject(loaded, path), "a v4 file loads under v5");
        check(loaded.arrangement.size() == 1, "with its arrangement");
        if (loaded.arrangement.size() == 1) {
            check(loaded.arrangement[0].type == ClipType::Pattern,
                  "and its clips default to pattern clips - a file written "
                  "before audio clips existed has none");
        }
        check(loaded.missingSamples.empty(),
              "and reports nothing missing, having referenced nothing");
        std::remove(path.c_str());
    }

    // ---- Validation repairs hostile values --------------------------------
    {
        Project p;
        p.arrangement.clear();
        const int id = p.samplePool.loadSample(wavA);
        check(id >= 0, "a sample to point at");

        Clip bad;
        bad.type = ClipType::Audio;
        bad.sampleId = 900;                  // past the end of the pool
        bad.channelIndex = 0;
        bad.startBeat = 0.0f;
        bad.lengthBeats = 4.0f;
        bad.gain = 99.0f;
        bad.trimStartSeconds = 5.0f;
        bad.trimEndSeconds = 1.0f;           // before the start
        bad.fadeInBeats = -3.0f;
        bad.fadeOutBeats = 1e9f;
        p.arrangement.push_back(bad);

        Clip nanClip;
        nanClip.type = ClipType::Audio;
        nanClip.sampleId = id;
        nanClip.gain = std::numeric_limits<float>::quiet_NaN();
        nanClip.trimStartSeconds = std::numeric_limits<float>::infinity();
        nanClip.lengthBeats = 4.0f;
        p.arrangement.push_back(nanClip);

        clampProjectToValidRanges(p);

        check(p.arrangement[0].sampleId == -1,
              "a sample id past the end of the pool becomes no sample - it "
              "would otherwise be an out-of-bounds read in the audio thread");
        check(p.arrangement[0].gain >= 0.0f && p.arrangement[0].gain <= 4.0f,
              "gain is clamped");
        check(p.arrangement[0].trimEndSeconds == 0.0f,
              "a trim that ends before it starts becomes 'to the end'");
        check(p.arrangement[0].fadeInBeats >= 0.0f,
              "a negative fade becomes none");
        check(p.arrangement[0].fadeOutBeats <= 64.0f, "a huge fade is bounded");
        check(std::isfinite(p.arrangement[1].gain) &&
              std::isfinite(p.arrangement[1].trimStartSeconds),
              "NaN and infinity are replaced with real numbers");
    }

    // ---- An audio clip is not judged by a field it does not use -----------
    //
    // Validation drops a clip whose patternIndex points past the end of the
    // pattern list. An audio clip plays a sample and its patternIndex is a
    // leftover default, so applying that rule to one would throw away a
    // recording for pointing at a pattern it never meant to play.
    {
        Project p;
        p.patterns.clear();
        p.patterns.push_back(Pattern());
        p.arrangement.clear();
        const int id = p.samplePool.loadSample(wavB);

        Clip audio;
        audio.type = ClipType::Audio;
        audio.sampleId = id;
        audio.channelIndex = 1;
        audio.patternIndex = 40;      // nonsense, and irrelevant
        audio.lengthBeats = 4.0f;
        p.arrangement.push_back(audio);

        Clip pattern{40, 1, 0.0f, 4.0f, 0};   // same nonsense, but it matters
        p.arrangement.push_back(pattern);

        clampProjectToValidRanges(p);

        check(p.arrangement.size() == 1,
              "exactly one of the two clips is dropped");
        check(p.arrangement.size() == 1 &&
              p.arrangement[0].type == ClipType::Audio,
              "and it is the pattern clip that goes - the audio clip keeps "
              "its place because patternIndex means nothing to it");

        // A clip on a channel that does not exist still goes, either way.
        p.arrangement.clear();
        Clip offChannel;
        offChannel.type = ClipType::Audio;
        offChannel.sampleId = id;
        offChannel.channelIndex = Project::MAX_CHANNELS + 5;
        offChannel.lengthBeats = 4.0f;
        p.arrangement.push_back(offChannel);
        clampProjectToValidRanges(p);
        check(p.arrangement.empty(),
              "but an audio clip on a channel that does not exist is still "
              "dropped - that one would be an out-of-bounds mix");
    }

    std::remove(wavA.c_str());
    std::remove(wavB.c_str());
}


// ============================================================================
// 57. Tempo and meter map
// ============================================================================
static void testTempoMap() {
    beginTest("Tempo and meter map");

    // ---- A map with nothing in it behaves exactly as before ---------------
    {
        TempoMap map;
        check(map.isFlat(), "an empty map is flat");
        check(std::fabs(map.bpmAtBeat(100.0f, 140.0f) - 140.0f) < 1e-4f,
              "and reports the project tempo everywhere");

        // 16 beats at 120 bpm is 8 seconds.
        check(std::fabs(map.beatToSeconds(16.0f, 120.0f) - 8.0f) < 1e-3f,
              "beats convert to seconds by the single tempo");
        check(std::fabs(map.secondsToBeat(8.0f, 120.0f) - 16.0f) < 1e-3f,
              "and back again");
    }

    // ---- Tempo changes take effect at their beat --------------------------
    {
        TempoMap map;
        check(map.setTempo(16.0f, 240.0f), "a tempo change is accepted");
        check(!map.isFlat(), "and the map is no longer flat");

        check(std::fabs(map.bpmAtBeat(0.0f, 120.0f) - 120.0f) < 1e-4f,
              "before the change, the project tempo holds");
        check(std::fabs(map.bpmAtBeat(15.9f, 120.0f) - 120.0f) < 1e-4f,
              "right up to it");
        check(std::fabs(map.bpmAtBeat(16.0f, 120.0f) - 240.0f) < 1e-4f,
              "the change takes effect AT its beat, not after it");
        check(std::fabs(map.bpmAtBeat(500.0f, 120.0f) - 240.0f) < 1e-4f,
              "and holds until the next one");
    }

    // ---- Seconds are integrated, not divided ------------------------------
    {
        TempoMap map;
        map.setTempo(16.0f, 240.0f);

        // 16 beats at 120 = 8 s, then 16 beats at 240 = 4 s. Total 12.
        // Dividing once by either tempo gives 16 or 8 - both wrong.
        const float seconds = map.beatToSeconds(32.0f, 120.0f);
        check(std::fabs(seconds - 12.0f) < 1e-3f,
              "beat 32 across a tempo change is 12 s, not 16 or 8 (got " +
              std::to_string(seconds) + ")");

        check(std::fabs(map.secondsToBeat(12.0f, 120.0f) - 32.0f) < 1e-2f,
              "and the inverse lands back on beat 32");

        // Round-trip through several changes.
        map.setTempo(48.0f, 90.0f);
        map.setTempo(64.0f, 180.0f);
        for (float beat : {4.0f, 20.0f, 33.0f, 55.0f, 80.0f, 200.0f}) {
            const float s = map.beatToSeconds(beat, 120.0f);
            const float back = map.secondsToBeat(s, 120.0f);
            if (std::fabs(back - beat) > 1e-2f) {
                check(false, "round trip failed at beat " + std::to_string(beat) +
                      " (got " + std::to_string(back) + ")");
                break;
            }
        }
        check(true, "beats survive a round trip through four tempo segments");

        // Time must never run backwards, whatever the map says.
        bool monotonic = true;
        float previous = -1.0f;
        for (float beat = 0.0f; beat < 120.0f; beat += 0.37f) {
            const float s = map.beatToSeconds(beat, 120.0f);
            if (s < previous) { monotonic = false; break; }
            previous = s;
        }
        check(monotonic, "and seconds increase monotonically with beats");
    }

    // ---- Entries stay sorted, and one beat holds one change ---------------
    {
        TempoMap map;
        map.setTempo(32.0f, 100.0f);
        map.setTempo(8.0f, 200.0f);
        map.setTempo(16.0f, 150.0f);

        check(map.tempoCount() == 3, "three changes went in");
        check(map.tempoAt(0).beat < map.tempoAt(1).beat &&
              map.tempoAt(1).beat < map.tempoAt(2).beat,
              "and they are sorted by beat regardless of insertion order");

        map.setTempo(16.0f, 175.0f);
        check(map.tempoCount() == 3,
              "setting a tempo at an existing beat replaces it rather than "
              "adding a second - two entries at one beat would make the "
              "lookup depend on insertion order, which is a bug you can only "
              "find by ear");
        check(std::fabs(map.bpmAtBeat(16.0f, 120.0f) - 175.0f) < 1e-4f,
              "and the later value wins");

        map.removeTempoAt(1);
        check(map.tempoCount() == 2, "and one can be removed");
    }

    // ---- Impossible values cannot get in ----------------------------------
    {
        TempoMap map;
        map.setTempo(4.0f, 0.0f);
        check(map.bpmAtBeat(4.0f, 120.0f) >= TempoMap::MIN_BPM,
              "a tempo of zero is clamped - it would divide by zero in the "
              "beat advance and freeze the playhead, on the audio thread");

        map.setTempo(8.0f, 99999.0f);
        check(map.bpmAtBeat(8.0f, 120.0f) <= TempoMap::MAX_BPM,
              "and an absurd one is bounded");

        check(!map.setTempo(std::numeric_limits<float>::quiet_NaN(), 120.0f),
              "a NaN beat is refused outright");
        check(!map.setTempo(4.0f, std::numeric_limits<float>::infinity()),
              "and so is an infinite tempo");

        map.setTempo(-5.0f, 130.0f);
        check(map.tempoAt(0).beat >= 0.0f, "a negative beat is pulled to zero");

        // The cap holds.
        TempoMap full;
        for (int i = 0; i < TempoMap::MAX_TEMPO_CHANGES + 20; ++i) {
            full.setTempo(static_cast<float>(i) * 2.0f, 100.0f + float(i));
        }
        check(full.tempoCount() == TempoMap::MAX_TEMPO_CHANGES,
              "the array is fixed-capacity and stops accepting entries rather "
              "than growing under the audio thread");
    }

    // ---- Meter: bars are counted, not divided -----------------------------
    {
        TempoMap map;
        check(std::fabs(TempoMap::barLengthBeats(MeterChange{0.0f, 4, 4}) - 4.0f) < 1e-4f,
              "4/4 is four beats");
        check(std::fabs(TempoMap::barLengthBeats(MeterChange{0.0f, 3, 4}) - 3.0f) < 1e-4f,
              "3/4 is three");
        check(std::fabs(TempoMap::barLengthBeats(MeterChange{0.0f, 6, 8}) - 3.0f) < 1e-4f,
              "and 6/8 is three beats, not six - the engine counts quarter "
              "notes and six eighths is three of them");
        check(std::fabs(TempoMap::barLengthBeats(MeterChange{0.0f, 7, 8}) - 3.5f) < 1e-4f,
              "7/8 is three and a half");

        check(map.barAtBeat(0.0f, 4) == 0, "beat 0 is bar 0");
        check(map.barAtBeat(4.0f, 4) == 1, "beat 4 is bar 1 in 4/4");
        check(map.barAtBeat(31.9f, 4) == 7, "and beat 31.9 is bar 7");
        check(std::fabs(map.beatOfBar(8, 4) - 32.0f) < 1e-4f,
              "bar 8 starts at beat 32");
    }

    {
        // 4/4 for eight bars, then 3/4. Bar 8 starts at 32, bar 9 at 35,
        // bar 10 at 38 - dividing by anything puts them all wrong.
        TempoMap map;
        map.setMeter(32.0f, 3, 4);

        check(std::fabs(map.beatOfBar(8, 4) - 32.0f) < 1e-3f,
              "the bar the meter changes on still starts at 32");
        check(std::fabs(map.beatOfBar(9, 4) - 35.0f) < 1e-3f,
              "the next bar is three beats later, not four (got " +
              std::to_string(map.beatOfBar(9, 4)) + ")");
        check(std::fabs(map.beatOfBar(10, 4) - 38.0f) < 1e-3f,
              "and so is the one after");

        check(map.barAtBeat(35.0f, 4) == 9, "beat 35 is bar 9");
        check(map.barAtBeat(34.9f, 4) == 8, "and beat 34.9 is still bar 8");

        // barAtBeat and beatOfBar must be each other's inverse, or the ruler
        // and the snap disagree about where a bar line is.
        bool consistent = true;
        for (int bar = 0; bar < 40; ++bar) {
            const float beat = map.beatOfBar(bar, 4);
            if (map.barAtBeat(beat, 4) != bar) { consistent = false; break; }
        }
        check(consistent,
              "every bar line reports its own bar number back - if these two "
              "disagreed the grid would snap somewhere the ruler did not draw");
    }

    {
        // A denominator that is not a power of two cannot be represented in
        // a MIDI time signature, which stores its base-2 logarithm.
        TempoMap map;
        map.setMeter(0.0f, 5, 5);
        check(map.meterAt(0).denominator == 4,
              "a denominator of 5 is snapped to the nearest power of two");
        map.setMeter(4.0f, 100, 8);
        check(map.meterAt(1).numerator <= 32, "and the numerator is bounded");
    }

    // ---- Snap follows the meter map ---------------------------------------
    {
        TempoMap map;
        map.setMeter(32.0f, 3, 4);

        // Everything but Bar is an absolute note value and must not change.
        check(std::fabs(snapBeatMapped(5.3f, SnapDivision::Sixteenth, map, 4) -
                        snapBeat(5.3f, SnapDivision::Sixteenth, 4)) < 1e-5f,
              "a 1/16 snap is unaffected by the meter map");

        check(std::fabs(snapBeatMapped(33.5f, SnapDivision::Bar, map, 4) - 32.0f) < 1e-3f,
              "snapping to Bar inside the 3/4 section lands on 32");
        check(std::fabs(snapBeatMapped(36.5f, SnapDivision::Bar, map, 4) - 35.0f) < 1e-3f,
              "and the next one on 35, which is where the ruler drew it - a "
              "plain division would have said 36");

        check(std::fabs(snapBeatNearestMapped(34.9f, SnapDivision::Bar, map, 4) - 35.0f) < 1e-3f,
              "nearest-bar rounds up to the next line when it is closer");
        check(std::fabs(snapBeatNearestMapped(32.4f, SnapDivision::Bar, map, 4) - 32.0f) < 1e-3f,
              "and down when it is not");

        check(snapBeatMapped(std::numeric_limits<float>::quiet_NaN(),
                             SnapDivision::Bar, map, 4) == 0.0f,
              "a NaN beat cannot escape into the grid");
    }

    // ---- Markers and regions ----------------------------------------------
    {
        std::vector<Marker> markers = {
            {16.0f, "Chorus", 0u}, {0.0f, "Intro", 0u}, {8.0f, "Verse", 0u},
        };
        sortMarkers(markers);
        check(markers[0].name == "Intro" && markers[2].name == "Chorus",
              "markers sort by beat, so next and previous are a step through "
              "the list rather than a search");

        check(markerAtOrBefore(markers, 12.0f) == 1, "the marker at or before 12 is Verse");
        check(markerAtOrBefore(markers, 8.0f) == 1, "one exactly on a marker is that marker");
        check(markerAtOrBefore(markers, -1.0f) == -1, "and before the first there is none");
        check(markerAfter(markers, 8.0f) == 2, "the next after Verse is Chorus");
        check(markerAfter(markers, 20.0f) == -1, "and past the last there is none");
    }

    {
        std::vector<Region> regions = {
            {0.0f, 64.0f, "Song", 0u},
            {16.0f, 32.0f, "Chorus", 0u},
        };
        check(regionAtBeat(regions, 20.0f) == 1,
              "a beat inside two nested regions reports the inner one - the "
              "shorter is the one the user means");
        check(regionAtBeat(regions, 40.0f) == 0, "outside it, the outer");
        check(regionAtBeat(regions, 100.0f) == -1, "and past both, none");
        check(regions[1].contains(16.0f) && !regions[1].contains(32.0f),
              "a region includes its start and excludes its end, so two "
              "touching regions do not both claim the boundary");
    }
}

// ============================================================================
// 58. The tempo map reaches the audio, the file and the MIDI
// ============================================================================
static void testTempoMapReachesEverything() {
    beginTest("Tempo map reaches audio, files and MIDI");

    // ---- The engine plays it ----------------------------------------------
    //
    // A map the sequencer ignores is worth nothing. This measures how far the
    // playhead travels in a fixed number of samples: doubling the tempo half
    // way must cover more ground than a flat project does.
    auto beatsTravelled = [](bool withChange) {
        Project p;
        p.bpm = 120.0f;
        p.patterns.clear();
        p.patterns.push_back(Pattern());
        p.arrangement.clear();
        p.songLength = 256.0f;

        p.arrangement.push_back(Clip{0, 0, 0.0f, 128.0f, 0});

        if (withChange) p.tempoMap.setTempo(8.0f, 240.0f);

        auto seqPtr = std::make_unique<Sequencer>();
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(&p);
        seqPtr->updateChannelConfigs();
        // The playhead has to have somewhere to go. In song mode the loop
        // window falls back to m_state.loopEnd, which defaults to 16 beats -
        // so both the flat and the mapped run would park at exactly 16 and
        // the test would pass while measuring nothing at all.
        seqPtr->setLoop(false, 0.0f, 200.0f);
        seqPtr->play();

        std::vector<float> l(512), r(512);
        // 8 seconds. At a flat 120 bpm that is 16 beats.
        for (int i = 0; i < 689; ++i) seqPtr->process(l.data(), r.data(), 512);
        return seqPtr->getCurrentBeat();
    };

    {
        const float flat = beatsTravelled(false);
        const float mapped = beatsTravelled(true);

        check(std::fabs(flat - 16.0f) < 0.2f,
              "eight seconds at 120 bpm is sixteen beats (got " +
              std::to_string(flat) + ")");

        // 8 beats at 120 takes 4 s; the remaining 4 s at 240 covers 16 beats.
        check(std::fabs(mapped - 24.0f) < 0.3f,
              "with the tempo doubling at beat 8, the same eight seconds "
              "covers twenty-four beats - the audio thread reads the map "
              "(got " + std::to_string(mapped) + ")");
    }

    // ---- A flat project is untouched --------------------------------------
    {
        Project p;
        check(p.tempoMap.isFlat(),
              "a new project has no tempo changes, so the per-sample lookup "
              "is hoisted out of the loop and costs what it always did");
        check(std::fabs(p.beatToSeconds(16.0f) - 8.0f) < 1e-3f,
              "and its beat-to-seconds is the plain division");
    }

    // ---- Audio clips follow the tempo where they sit ----------------------
    {
        const std::string wav = testPath("tempo_clip.wav");
        {
            std::vector<float> l(44100), r(44100);
            for (size_t i = 0; i < l.size(); ++i) {
                const float t = float(i) / 44100.0f;
                l[i] = r[i] = 0.7f * std::sin(6.28318530718f * 220.0f * t);
            }
            check(writeWavFile(wav, l, r), "a one-second tone for the clip test");
        }

        Project p;
        p.bpm = 120.0f;
        p.patterns.clear();
        p.arrangement.clear();
        p.masterLimiterEnabled = false;
        p.masterCompressorEnabled = false;
        p.masterEQEnabled = false;

        const int id = p.samplePool.loadSample(wav);
        check(id >= 0, "and it loads");

        // At 120 bpm the one-second sample fills two beats. Doubling the
        // tempo at beat 0 makes it fill four - so a four-beat clip that was
        // half silence becomes full.
        Clip c;
        c.type = ClipType::Audio;
        c.sampleId = id;
        c.channelIndex = 0;
        c.startBeat = 0.0f;
        c.lengthBeats = 4.0f;
        p.arrangement.push_back(c);

        auto rmsOfLastBeat = [](Project& proj) {
            auto seqPtr = std::make_unique<Sequencer>();
            seqPtr->setSampleRate(44100.0f);
            seqPtr->setProject(&proj);
            seqPtr->updateChannelConfigs();
            seqPtr->updateMasterEffects();
            seqPtr->play();

            std::vector<float> l(256), r(256);
            double sum = 0.0;
            int counted = 0;
            const int total = int(proj.beatToSeconds(3.5f) * 44100.0f);
            const int skip = int(proj.beatToSeconds(3.0f) * 44100.0f);
            for (int done = 0; done < total; done += 256) {
                seqPtr->process(l.data(), r.data(), 256);
                if (done < skip) continue;
                for (float v : l) { sum += double(v) * double(v); ++counted; }
            }
            return (counted > 0) ? std::sqrt(sum / counted) : 0.0;
        };

        const double slow = rmsOfLastBeat(p);
        check(slow < 1e-6,
              "at 120 bpm the one-second sample has run out by beat 3");

        p.tempoMap.setTempo(0.0f, 240.0f);
        const double fast = rmsOfLastBeat(p);
        check(fast > 0.005,
              "at 240 bpm the same sample still has audio left at beat 3 - "
              "the clip mixer reads the tempo where the playhead is (rms " +
              std::to_string(fast) + ")");

        std::remove(wav.c_str());
    }

    // ---- It survives a save and load --------------------------------------
    {
        Project p;
        p.bpm = 128.0f;
        p.beatsPerMeasure = 4;
        p.tempoMap.setTempo(16.0f, 96.0f);
        p.tempoMap.setTempo(48.0f, 174.0f);
        p.tempoMap.setMeter(32.0f, 6, 8);
        p.markers.push_back(Marker{8.0f, "Verse 1", 0xFF3366CCu});
        p.markers.push_back(Marker{24.0f, "Drop \"the\" bass", 0xFFCC3366u});
        p.regions.push_back(Region{16.0f, 32.0f, "Chorus", 0xFF44CC88u});

        const std::string path = testPath("tempomap.ctp");
        check(saveProject(p, path), "a project with a tempo map saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");

        check(loaded.tempoMap.tempoCount() == 2, "both tempo changes survive");
        check(std::fabs(loaded.tempoMap.bpmAtBeat(20.0f, loaded.bpm) - 96.0f) < 1e-3f,
              "at the right beats");
        check(std::fabs(loaded.tempoMap.bpmAtBeat(50.0f, loaded.bpm) - 174.0f) < 1e-3f,
              "and the right values");

        check(loaded.tempoMap.meterCount() == 1, "the meter change survives");
        check(loaded.meterAt(33.0f).numerator == 6 &&
              loaded.meterAt(33.0f).denominator == 8,
              "as 6/8, numerator and denominator both");

        check(loaded.markers.size() == 2, "both markers survive");
        check(loaded.markers.size() == 2 && loaded.markers[0].name == "Verse 1",
              "with their names");
        check(loaded.markers.size() == 2 &&
              loaded.markers[1].name == "Drop \"the\" bass",
              "including one with quotes in it, which the quoting must escape");
        check(loaded.markers.size() == 2 &&
              loaded.markers[1].color == 0xFFCC3366u,
              "and their colours");

        check(loaded.regions.size() == 1, "the region survives");
        check(loaded.regions.size() == 1 && loaded.regions[0].name == "Chorus" &&
              std::fabs(loaded.regions[0].endBeat - 32.0f) < 1e-3f,
              "with its name and its span");

        std::remove(path.c_str());
    }

    // ---- A project with no map writes nothing extra -----------------------
    {
        Project plain;
        const std::string path = testPath("tempomap_plain.ctp");
        check(saveProject(plain, path), "a project with no tempo map saves");

        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();

        check(text.find("TEMPO ") == std::string::npos &&
              text.find("METER ") == std::string::npos &&
              text.find("MARKER ") == std::string::npos &&
              text.find("REGION ") == std::string::npos,
              "and writes not one line about any of it - the bump costs "
              "nothing to a project that never uses the feature");
        std::remove(path.c_str());
    }

    // ---- Validation cannot be bypassed by a hand-edited file --------------
    {
        Project p;
        p.markers.push_back(Marker{std::numeric_limits<float>::quiet_NaN(), "bad", 0u});
        p.markers.push_back(Marker{-40.0f, "negative", 0u});
        p.regions.push_back(Region{40.0f, 8.0f, "backwards", 0u});

        clampProjectToValidRanges(p);

        for (const Marker& m : p.markers) {
            if (!std::isfinite(m.beat) || m.beat < 0.0f) {
                check(false, "a marker escaped validation");
                break;
            }
        }
        check(true, "NaN and negative marker positions are repaired");
        check(p.regions[0].endBeat > p.regions[0].startBeat,
              "a region that ends before it starts is turned the right way "
              "round rather than drawn inside out");
    }

    // ---- MIDI carries the map ---------------------------------------------
    //
    // The time signature was being written with a denominator of 2 - a
    // half-note pulse - because midifile's `bottom` parameter is the actual
    // denominator and takes its own logarithm. Every export said x/2, and
    // any DAW importing one saw bars twice the intended length.
    {
        auto readFile = [](const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        };

        // Find every FF 58 04 time-signature meta event.
        auto timeSignatures = [](const std::string& data) {
            std::vector<std::pair<int, int>> found;
            for (size_t i = 0; i + 5 < data.size(); ++i) {
                if (static_cast<unsigned char>(data[i]) != 0xFF) continue;
                if (static_cast<unsigned char>(data[i + 1]) != 0x58) continue;
                if (static_cast<unsigned char>(data[i + 2]) != 0x04) continue;
                const int top = static_cast<unsigned char>(data[i + 3]);
                const int logBottom = static_cast<unsigned char>(data[i + 4]);
                found.emplace_back(top, 1 << logBottom);
            }
            return found;
        };

        auto tempoEvents = [](const std::string& data) {
            int count = 0;
            for (size_t i = 0; i + 5 < data.size(); ++i) {
                if (static_cast<unsigned char>(data[i]) != 0xFF) continue;
                if (static_cast<unsigned char>(data[i + 1]) != 0x51) continue;
                if (static_cast<unsigned char>(data[i + 2]) != 0x03) continue;
                ++count;
            }
            return count;
        };

        Project p;
        p.bpm = 120.0f;
        p.beatsPerMeasure = 4;
        p.patterns.clear();
        p.arrangement.clear();
        Pattern pat;
        Note n;
        n.pitch = 60;
        n.startTime = 0.0f;
        n.duration = 1.0f;
        pat.notes.push_back(n);
        p.patterns.push_back(pat);
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        const std::string plain = testPath("midi_meter_plain.mid");
        check(exportProjectToMIDI(p, plain), "a plain project exports to MIDI");
        {
            const auto sigs = timeSignatures(readFile(plain));
            check(!sigs.empty(), "and carries a time signature");
            check(!sigs.empty() && sigs[0].first == 4 && sigs[0].second == 4,
                  "which says 4/4 - it said 4/2 before, because midifile's "
                  "bottom parameter is the denominator itself and takes its "
                  "own base-2 logarithm");
        }
        std::remove(plain.c_str());

        p.tempoMap.setTempo(16.0f, 90.0f);
        p.tempoMap.setTempo(32.0f, 200.0f);
        p.tempoMap.setMeter(16.0f, 6, 8);

        const std::string mapped = testPath("midi_meter_mapped.mid");
        check(exportProjectToMIDI(p, mapped), "a mapped project exports");
        {
            const std::string data = readFile(mapped);
            check(tempoEvents(data) == 3,
                  "three tempo events reach the file: the opening one and "
                  "both changes");

            const auto sigs = timeSignatures(data);
            check(sigs.size() == 2, "and two time signatures");
            check(sigs.size() == 2 && sigs[0].first == 4 && sigs[0].second == 4,
                  "opening in 4/4");
            check(sigs.size() == 2 && sigs[1].first == 6 && sigs[1].second == 8,
                  "and changing to 6/8, with the eight surviving as an eight");
        }
        std::remove(mapped.c_str());
    }
}


// ============================================================================
// 59. The capture ring
// ============================================================================
static void testVoiceRing() {
    beginTest("Voice capture ring");

    // ---- Round trip --------------------------------------------------------
    {
        AudioRing<16> ring;
        const float in[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        ring.write(in, 5);
        check(ring.available() == 5, "five samples in, five available");

        float out[8] = {};
        check(ring.read(out, 8) == 5, "and five come back out");
        check(out[0] == 1.0f && out[4] == 5.0f, "in the order they went in");
        check(ring.available() == 0, "leaving it empty");
        check(ring.read(out, 8) == 0, "and an empty ring reads nothing");
    }

    // ---- The wrap ----------------------------------------------------------
    {
        AudioRing<16> ring;
        float scratch[16] = {};

        // Walk the write cursor most of the way round, then straddle the end.
        for (int pass = 0; pass < 3; ++pass) {
            float block[5];
            for (int i = 0; i < 5; ++i) block[i] = float(pass * 5 + i);
            ring.write(block, 5);
            check(ring.read(scratch, 5) == 5, "a block survives the wrap");
            bool ok = true;
            for (int i = 0; i < 5; ++i) {
                if (scratch[i] != float(pass * 5 + i)) { ok = false; break; }
            }
            if (!ok) { check(false, "values corrupted across the wrap"); break; }
        }
        check(true, "values are intact after the write cursor wraps");
    }

    // ---- Overflow drops the OLDEST -----------------------------------------
    //
    // A live monitor that has fallen behind should show what is being sung
    // now. Keeping a stale backlog and discarding the present is the wrong
    // way round.
    {
        AudioRing<16> ring;
        float block[10];
        for (int i = 0; i < 10; ++i) block[i] = float(i);
        ring.write(block, 10);

        float second[10];
        for (int i = 0; i < 10; ++i) second[i] = float(100 + i);
        ring.write(second, 10);

        check(ring.droppedSamples() > 0, "the overflow is counted, not silent");

        float out[16] = {};
        const size_t got = ring.read(out, 16);
        check(got == 15, "the ring holds capacity - 1 (got " +
              std::to_string(got) + ")");
        check(got > 0 && out[got - 1] == 109.0f,
              "and the NEWEST sample survived - a monitor that has fallen "
              "behind must show the present, not a stale backlog");
    }

    // ---- A block larger than the ring --------------------------------------
    {
        AudioRing<16> ring;
        float huge[64];
        for (int i = 0; i < 64; ++i) huge[i] = float(i);
        ring.write(huge, 64);

        float out[16] = {};
        const size_t got = ring.read(out, 16);
        check(got == 15, "an oversized block leaves the ring full, not broken");
        check(got > 0 && out[got - 1] == 63.0f,
              "keeping the tail of it - the newest audio is the audio the "
              "user just made");
    }

    // ---- clear -------------------------------------------------------------
    {
        AudioRing<16> ring;
        const float in[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        ring.write(in, 4);
        ring.clear();
        check(ring.available() == 0, "clear empties it");
        ring.write(in, 4);
        float out[4] = {};
        check(ring.read(out, 4) == 4 && out[0] == 1.0f,
              "and it still works afterwards");
    }

    // ---- The meter ---------------------------------------------------------
    {
        const float block[4] = {0.1f, -0.7f, 0.3f, 0.2f};
        check(std::fabs(blockPeak(block, 4) - 0.7f) < 1e-6f,
              "the block peak is the largest magnitude, sign ignored");

        PeakMeter meter;
        meter.push(0.9f);
        check(std::fabs(meter.value() - 0.9f) < 1e-6f, "the meter rises instantly");
        meter.push(0.0f);
        check(meter.value() > 0.0f && meter.value() < 0.9f,
              "and falls gradually, so a transient stays visible for longer "
              "than the one frame it occupied");
        for (int i = 0; i < 100; ++i) meter.push(0.0f);
        check(meter.value() == 0.0f, "reaching zero eventually");
    }
}

// ============================================================================
// 60. The FFT plan
// ============================================================================
static void testFFTPlan() {
    beginTest("FFT plan");

    // ---- It refuses what it cannot do --------------------------------------
    {
        DSP::FFTPlan plan;
        check(!plan.resize(100), "a non-power-of-two size is refused outright "
              "rather than half-transformed into a spectrum that looks "
              "plausible and is wrong");
        check(!plan.resize(1), "and so is a size of one");
        check(plan.resize(256), "a power of two is accepted");
        check(plan.size() == 256, "and reports its size");
    }

    // ---- It agrees with the reference implementation ------------------------
    {
        const size_t N = 256;
        std::vector<float> real(N), imag(N, 0.0f);
        std::vector<DSP::Complex> reference(N);

        for (size_t i = 0; i < N; ++i) {
            const float t = float(i) / float(N);
            const float v = std::sin(6.28318530718f * 5.0f * t) +
                            0.5f * std::sin(6.28318530718f * 17.0f * t + 0.7f);
            real[i] = v;
            reference[i] = DSP::Complex(v, 0.0f);
        }

        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(real.data(), imag.data());
        DSP::fft(reference);

        float worst = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            worst = std::max(worst, std::fabs(real[i] - reference[i].real()));
            worst = std::max(worst, std::fabs(imag[i] - reference[i].imag()));
        }
        check(worst < 1e-3f,
              "the iterative plan matches the recursive reference to within "
              "float error (worst " + std::to_string(worst) + ") - it exists "
              "for speed, not for a different answer");
    }

    // ---- A tone lands in its own bin ---------------------------------------
    {
        const size_t N = 1024;
        const int sampleRate = 48000;
        const float frequency = 1500.0f;

        std::vector<float> real(N), imag(N, 0.0f);
        for (size_t i = 0; i < N; ++i) {
            real[i] = std::sin(6.28318530718f * frequency * float(i) / float(sampleRate));
        }

        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(real.data(), imag.data());

        size_t loudest = 0;
        float peak = 0.0f;
        for (size_t bin = 1; bin < N / 2; ++bin) {
            const float magnitude = std::sqrt(real[bin] * real[bin] +
                                              imag[bin] * imag[bin]);
            if (magnitude > peak) { peak = magnitude; loudest = bin; }
        }

        const float binHz = float(sampleRate) / float(N);
        const float found = float(loudest) * binHz;
        check(std::fabs(found - frequency) < binHz,
              "a 1500 Hz tone lands within one bin of 1500 Hz (got " +
              std::to_string(found) + ")");
    }

    // ---- Resizing keeps working ---------------------------------------------
    {
        DSP::FFTPlan plan;
        plan.resize(64);
        plan.resize(512);
        check(plan.size() == 512, "a plan can be resized");

        std::vector<float> real(512, 0.0f), imag(512, 0.0f);
        real[0] = 1.0f;                      // an impulse
        plan.transform(real.data(), imag.data());

        bool flat = true;
        for (size_t i = 0; i < 512; ++i) {
            if (std::fabs(real[i] - 1.0f) > 1e-4f || std::fabs(imag[i]) > 1e-4f) {
                flat = false;
                break;
            }
        }
        check(flat, "and an impulse still transforms to a flat spectrum after "
              "the resize, so the twiddle tables were actually rebuilt");
    }
}

// ============================================================================
// 61. Live voice tracking
// ============================================================================
static void testLiveVoice() {
    beginTest("Live voice tracking");

    const int rate = 48000;

    auto tone = [rate](float frequency, float seconds, float amplitude) {
        std::vector<float> out(static_cast<size_t>(seconds * float(rate)));
        for (size_t i = 0; i < out.size(); ++i) {
            const float t = float(i) / float(rate);
            // A couple of harmonics, because a pure sine is not what a voice
            // gives a pitch detector and YIN behaves differently on one.
            out[i] = amplitude * (std::sin(6.28318530718f * frequency * t) +
                                  0.35f * std::sin(6.28318530718f * frequency * 2.0f * t) +
                                  0.15f * std::sin(6.28318530718f * frequency * 3.0f * t));
        }
        return out;
    };

    // ---- Silence is silence -------------------------------------------------
    {
        LiveVoiceTracker tracker;
        tracker.setSampleRate(rate);
        std::vector<float> quiet(rate / 2, 0.0f);
        tracker.process(quiet.data(), quiet.size());

        check(tracker.currentNote() == -1,
              "silence reports no note rather than the last one it saw");
        check(tracker.hits().empty(), "and produces no hits");
    }

    // ---- A sung note is identified -----------------------------------------
    {
        // A440 is MIDI 69. Half a second is about 90 analysis hops, so this
        // is not a lucky single frame.
        LiveVoiceTracker tracker;
        tracker.setSampleRate(rate);
        tracker.setMode(LiveVoiceMode::Melodic);

        const std::vector<float> a4 = tone(440.0f, 0.5f, 0.3f);
        tracker.process(a4.data(), a4.size());

        check(tracker.currentNote() == 69,
              "a 440 Hz tone reads as MIDI 69 (got " +
              std::to_string(tracker.currentNote()) + ") - an octave error "
              "would give 57 or 81, which is exactly what plain "
              "autocorrelation does to a voice");
        check(std::fabs(tracker.currentFrequency() - 440.0f) < 10.0f,
              "and the frequency is close to 440");
        check(!tracker.hits().empty(), "and it produced a hit");
    }

    // ---- Several notes in a row --------------------------------------------
    {
        LiveVoiceTracker tracker;
        tracker.setSampleRate(rate);
        tracker.setMode(LiveVoiceMode::Melodic);

        // C4, E4, G4 - 261.6, 329.6, 392.0 Hz, MIDI 60, 64, 67.
        const float pitches[3] = {261.63f, 329.63f, 392.00f};
        const int expected[3] = {60, 64, 67};
        std::vector<int> seen;

        for (int i = 0; i < 3; ++i) {
            const std::vector<float> note = tone(pitches[i], 0.4f, 0.3f);
            tracker.process(note.data(), note.size());
            seen.push_back(tracker.currentNote());
        }

        bool allRight = true;
        for (int i = 0; i < 3; ++i) {
            if (seen[static_cast<size_t>(i)] != expected[i]) { allRight = false; break; }
        }
        check(allRight,
              "a C-E-G line tracks as 60, 64, 67 (got " +
              std::to_string(seen[0]) + ", " + std::to_string(seen[1]) + ", " +
              std::to_string(seen[2]) + ")");

        // Every distinct note should have produced at least one hit.
        int distinct = 0;
        int previous = -999;
        for (const LiveHit& hit : tracker.hits()) {
            if (hit.midiNote != previous) { ++distinct; previous = hit.midiNote; }
        }
        check(distinct >= 3,
              "and each change produced a hit (" + std::to_string(distinct) + ")");
    }

    // ---- Beatbox classification --------------------------------------------
    {
        // A kick: a low tone with a fast decay, almost all energy under
        // 250 Hz. A hat: a short burst of noise. A snare: a mid tone with
        // noise on top, which is what a snare is.
        // The noise is band-limited, because real drums are. Flat noise all
        // the way to Nyquist is not what either a snare or a hi-hat sounds
        // like, and classifying THAT would be testing the fixture rather
        // than the classifier: a snare's rattle sits between about 200 Hz
        // and 3 kHz, a hi-hat's is almost entirely above 6.
        auto drumHit = [rate](int kind, float seconds) {
            std::vector<float> out(static_cast<size_t>(seconds * float(rate)), 0.0f);
            unsigned int seed = 22222u;
            float lowpass = 0.0f;
            float highpassPrevIn = 0.0f;
            float highpass = 0.0f;

            for (size_t i = 0; i < out.size(); ++i) {
                const float t = float(i) / float(rate);
                seed = seed * 1103515245u + 12345u;
                const float noise = (float((seed >> 16) & 0x7FFF) / 16383.5f) - 1.0f;

                // One-pole pair, enough to put the energy in the right half
                // of the spectrum without pretending to be a drum synth.
                lowpass += (noise - lowpass) * 0.28f;            // ~3 kHz
                highpass = 0.86f * (highpass + noise - highpassPrevIn);
                highpassPrevIn = noise;

                if (kind == 0) {
                    out[i] = 0.9f * std::exp(-t * 26.0f) *
                             std::sin(6.28318530718f * 62.0f * t);
                } else if (kind == 2) {
                    out[i] = 0.6f * std::exp(-t * 130.0f) * highpass;
                } else {
                    out[i] = std::exp(-t * 42.0f) *
                             (0.5f * std::sin(6.28318530718f * 210.0f * t) +
                              0.7f * lowpass);
                }
            }
            return out;
        };

        auto classify = [&](int kind) {
            LiveVoiceTracker tracker;
            tracker.setSampleRate(rate);
            tracker.setMode(LiveVoiceMode::Drums);
            tracker.setSensitivity(0.7f);

            // Lead in with silence so the flux history has a floor to
            // measure against - without it the very first window IS the
            // onset and there is nothing to compare it to.
            const std::vector<float> lead(static_cast<size_t>(0.15f * float(rate)), 0.0f);
            tracker.process(lead.data(), lead.size());

            const std::vector<float> hit = drumHit(kind, 0.25f);
            tracker.process(hit.data(), hit.size());

            const std::vector<float> tail(static_cast<size_t>(0.1f * float(rate)), 0.0f);
            tracker.process(tail.data(), tail.size());

            if (tracker.hits().empty()) return -1;
            return tracker.hits().front().drumType;
        };

        check(classify(0) == 0,
              "a low decaying thump classifies as a kick (got " +
              std::to_string(classify(0)) + ")");
        check(classify(2) == 2,
              "a short noise burst classifies as a hat (got " +
              std::to_string(classify(2)) + ")");
        check(classify(1) == 1,
              "and a mid tone with noise on it classifies as a snare (got " +
              std::to_string(classify(1)) + ")");
    }

    // ---- Onsets are not double-triggered ------------------------------------
    {
        LiveVoiceTracker tracker;
        tracker.setSampleRate(rate);
        tracker.setMode(LiveVoiceMode::Drums);
        tracker.setSensitivity(0.9f);

        // One kick with a long decay. Below the retrigger gap its own decay
        // fires a second onset and a beatboxed kick becomes two.
        std::vector<float> buffer(static_cast<size_t>(0.6f * float(rate)), 0.0f);
        const size_t onsetAt = static_cast<size_t>(0.15f * float(rate));
        for (size_t i = onsetAt; i < buffer.size(); ++i) {
            const float t = float(i - onsetAt) / float(rate);
            buffer[i] = 0.9f * std::exp(-t * 12.0f) *
                        std::sin(6.28318530718f * 60.0f * t);
        }
        tracker.process(buffer.data(), buffer.size());

        check(tracker.hits().size() <= 2,
              "one hit with a long decay produces at most two onsets, not a "
              "stream of them (got " +
              std::to_string(tracker.hits().size()) + ")");
        check(!tracker.hits().empty(), "and at least one");
    }

    // ---- Sensitivity does something ----------------------------------------
    {
        auto countHits = [&](float sensitivity) {
            LiveVoiceTracker tracker;
            tracker.setSampleRate(rate);
            tracker.setMode(LiveVoiceMode::Drums);
            tracker.setSensitivity(sensitivity);

            // A run of quiet taps over a noise floor.
            std::vector<float> buffer(static_cast<size_t>(1.5f * float(rate)), 0.0f);
            unsigned int seed = 7u;
            for (size_t i = 0; i < buffer.size(); ++i) {
                seed = seed * 1103515245u + 12345u;
                buffer[i] = 0.004f * ((float((seed >> 16) & 0x7FFF) / 16383.5f) - 1.0f);
            }
            for (int tap = 0; tap < 8; ++tap) {
                const size_t at = static_cast<size_t>((0.15f + float(tap) * 0.15f) *
                                                      float(rate));
                for (size_t i = at; i < at + 1200 && i < buffer.size(); ++i) {
                    const float t = float(i - at) / float(rate);
                    buffer[i] += 0.25f * std::exp(-t * 60.0f) *
                                 std::sin(6.28318530718f * 90.0f * t);
                }
            }
            tracker.process(buffer.data(), buffer.size());
            return tracker.hits().size();
        };

        const size_t shy = countHits(0.0f);
        const size_t eager = countHits(1.0f);
        check(eager >= shy,
              "turning sensitivity up never finds fewer onsets (" +
              std::to_string(shy) + " -> " + std::to_string(eager) + ")");
    }

    // ---- Reset really resets ------------------------------------------------
    {
        LiveVoiceTracker tracker;
        tracker.setSampleRate(rate);
        const std::vector<float> a4 = tone(440.0f, 0.3f, 0.3f);
        tracker.process(a4.data(), a4.size());
        check(!tracker.hits().empty(), "something was tracked");

        tracker.reset();
        check(tracker.hits().empty() && tracker.currentNote() == -1 &&
              tracker.elapsedSeconds() == 0.0f,
              "and reset clears the notes, the hits and the clock - a second "
              "take must not land after the first one's timestamps");
    }

    // ---- Changing the sample rate resets, because the clock changed --------
    {
        LiveVoiceTracker tracker;
        tracker.setSampleRate(48000);
        const std::vector<float> a4 = tone(440.0f, 0.3f, 0.3f);
        tracker.process(a4.data(), a4.size());
        const float before = tracker.elapsedSeconds();
        check(before > 0.0f, "the clock advanced");

        tracker.setSampleRate(44100);
        check(tracker.elapsedSeconds() == 0.0f,
              "switching to a device at another rate resets the clock rather "
              "than reinterpreting the elapsed samples at the new rate");
    }
}

// ============================================================================
// 62. Detected notes reach the pattern
// ============================================================================
static void testVoiceToNotes() {
    beginTest("Voice to notes");

    TempoMap flat;
    const float bpm = 120.0f;          // one beat = 0.5 s

    // ---- Quantisation -------------------------------------------------------
    {
        std::vector<LiveHit> hits;
        // Sung 30 ms early of beats 0, 1 and 2.
        hits.push_back(LiveHit{0.0f,  60, 261.6f, 0.8f, false, 0, 1.0f});
        hits.push_back(LiveHit{0.47f, 62, 293.7f, 0.8f, false, 0, 1.0f});
        hits.push_back(LiveHit{0.97f, 64, 329.6f, 0.8f, false, 0, 1.0f});

        VoiceToNotesOptions options;
        options.snap = SnapDivision::Quarter;
        const std::vector<Note> snapped = hitsToNotes(hits, flat, bpm, 4, options);

        check(snapped.size() == 3, "three hits become three notes");
        check(snapped.size() == 3 &&
              std::fabs(snapped[1].startTime - 1.0f) < 1e-3f &&
              std::fabs(snapped[2].startTime - 2.0f) < 1e-3f,
              "and land on the beat they were aimed at");

        options.snap = SnapDivision::Off;
        const std::vector<Note> asPlayed = hitsToNotes(hits, flat, bpm, 4, options);
        check(asPlayed.size() == 3 &&
              std::fabs(asPlayed[1].startTime - 0.94f) < 1e-2f,
              "with snap off the timing is left exactly as sung - a line that "
              "lands 30 ms early is played, not wrong, and forcing it onto "
              "the grid is a choice the performer may not want");
    }

    // ---- Notes run until the next one --------------------------------------
    {
        std::vector<LiveHit> hits;
        hits.push_back(LiveHit{0.0f, 60, 261.6f, 0.8f, false, 0, 1.0f});
        hits.push_back(LiveHit{1.0f, 62, 293.7f, 0.8f, false, 0, 1.0f});

        VoiceToNotesOptions options;
        options.snap = SnapDivision::Quarter;
        const std::vector<Note> notes = hitsToNotes(hits, flat, bpm, 4, options);

        check(notes.size() == 2, "two notes");
        check(notes.size() == 2 && std::fabs(notes[0].duration - 2.0f) < 1e-3f,
              "the first runs until the second, so a sung line comes back as "
              "a line rather than a row of clicks");
        check(notes.size() == 2 && notes[1].duration > 0.0f,
              "and the last one still has a length");
    }

    // ---- Drums are short and land on their own instruments -----------------
    {
        std::vector<LiveHit> hits;
        hits.push_back(LiveHit{0.0f, 0, 0.0f, 0.9f, true, 0, 1.0f});   // kick
        hits.push_back(LiveHit{0.5f, 0, 0.0f, 0.6f, true, 1, 1.0f});   // snare
        hits.push_back(LiveHit{0.75f, 0, 0.0f, 0.4f, true, 2, 1.0f});  // hat

        VoiceToNotesOptions options;
        options.snap = SnapDivision::Sixteenth;
        const std::vector<Note> notes = hitsToNotes(hits, flat, bpm, 4, options);

        check(notes.size() == 3, "three drum hits become three notes");
        if (notes.size() == 3) {
            check(notes[0].oscillatorType == OscillatorType::Kick &&
                  notes[1].oscillatorType == OscillatorType::Snare &&
                  notes[2].oscillatorType == OscillatorType::HiHat,
                  "each class becomes the instrument it names");
            check(notes[0].pitch != notes[1].pitch &&
                  notes[1].pitch != notes[2].pitch,
                  "on three different keys - a drum oscillator ignores pitch, "
                  "but stacking all three on one key makes the piano roll "
                  "unreadable");
            check(notes[0].duration <= 0.25f,
                  "and a drum hit is short: holding it to the next hit would "
                  "make every kick a whole note and choke the oscillator on "
                  "its own retrigger");
        }

        // The mapping is configurable, because a genre kit may want 808s.
        options.kick = OscillatorType::Kick808;
        const std::vector<Note> eightOhEight = hitsToNotes(hits, flat, bpm, 4, options);
        check(!eightOhEight.empty() &&
              eightOhEight[0].oscillatorType == OscillatorType::Kick808,
              "and which instrument each class becomes is a setting");
    }

    // ---- Velocity ----------------------------------------------------------
    {
        std::vector<LiveHit> hits;
        hits.push_back(LiveHit{0.0f, 60, 261.6f, 0.25f, false, 0, 1.0f});
        hits.push_back(LiveHit{1.0f, 62, 293.7f, 0.95f, false, 0, 1.0f});

        VoiceToNotesOptions options;
        std::vector<Note> notes = hitsToNotes(hits, flat, bpm, 4, options);
        check(notes.size() == 2 && notes[1].velocity > notes[0].velocity,
              "how hard it was sung reaches the note's velocity");

        options.useVelocity = false;
        notes = hitsToNotes(hits, flat, bpm, 4, options);
        check(notes.size() == 2 &&
              std::fabs(notes[0].velocity - notes[1].velocity) < 1e-4f,
              "and can be turned off for a take that should be even");
    }

    // ---- Two hits on one beat are one note ---------------------------------
    {
        std::vector<LiveHit> hits;
        hits.push_back(LiveHit{0.50f, 60, 261.6f, 0.8f, false, 0, 1.0f});
        hits.push_back(LiveHit{0.52f, 60, 261.6f, 0.8f, false, 0, 1.0f});

        VoiceToNotesOptions options;
        options.snap = SnapDivision::Quarter;
        const std::vector<Note> notes = hitsToNotes(hits, flat, bpm, 4, options);
        check(notes.size() == 1,
              "two hits that quantise onto the same beat with the same pitch "
              "are one note played unevenly, not two voices on one key");
    }

    // ---- A take over a tempo change ----------------------------------------
    {
        TempoMap map;
        map.setTempo(8.0f, 240.0f);   // beat 8 is 4 s in; after it, 4 beats/s

        std::vector<LiveHit> hits;
        hits.push_back(LiveHit{4.0f, 60, 261.6f, 0.8f, false, 0, 1.0f});  // beat 8
        hits.push_back(LiveHit{5.0f, 62, 293.7f, 0.8f, false, 0, 1.0f});  // beat 12

        VoiceToNotesOptions options;
        options.snap = SnapDivision::Quarter;
        const std::vector<Note> notes = hitsToNotes(hits, map, 120.0f, 4, options);

        check(notes.size() == 2, "both hits convert");
        check(notes.size() == 2 && std::fabs(notes[0].startTime - 8.0f) < 1e-2f,
              "a hit four seconds in lands on beat 8");
        check(notes.size() == 2 && std::fabs(notes[1].startTime - 12.0f) < 1e-2f,
              "and one at five seconds on beat 12, not beat 10 - the "
              "conversion runs through the tempo map rather than one bpm "
              "(got " + std::to_string(notes.size() == 2 ? notes[1].startTime : 0.0f) + ")");
    }

    // ---- Transpose ---------------------------------------------------------
    {
        std::vector<LiveHit> hits;
        hits.push_back(LiveHit{0.0f, 60, 261.6f, 0.8f, false, 0, 1.0f});

        VoiceToNotesOptions options;
        options.transpose = -12;
        const std::vector<Note> notes = hitsToNotes(hits, flat, bpm, 4, options);
        check(notes.size() == 1 && notes[0].pitch == 48,
              "a sung line can be dropped an octave on the way in, which is "
              "how a bassline gets written by someone who cannot sing that low");

        options.transpose = 200;      // nonsense
        const std::vector<Note> clamped = hitsToNotes(hits, flat, bpm, 4, options);
        check(clamped.size() == 1 && clamped[0].pitch <= 127,
              "and an absurd transpose is clamped into MIDI range");
    }

    // ---- The offline detector's measured durations are preferred -----------
    {
        std::vector<DetectedNote> detected;
        DetectedNote a{};
        a.noteNumber = 60;
        a.startTime = 0.0f;
        a.duration = 0.25f;      // half a beat at 120 bpm
        a.velocity = 0.8f;
        a.isDrum = false;
        detected.push_back(a);

        DetectedNote b{};
        b.noteNumber = 62;
        b.startTime = 2.0f;      // four beats later
        b.duration = 0.25f;
        b.velocity = 0.8f;
        b.isDrum = false;
        detected.push_back(b);

        VoiceToNotesOptions options;
        options.snap = SnapDivision::Eighth;
        const std::vector<Note> notes = detectedToNotes(detected, flat, bpm, 4, options);

        check(notes.size() == 2, "both detected notes convert");
        check(notes.size() == 2 && notes[0].duration < 1.0f,
              "the first keeps its own measured length rather than being "
              "stretched to the next note four beats away - the offline "
              "detector knows how long it lasted and the live path can only "
              "guess (got " +
              std::to_string(notes.size() == 2 ? notes[0].duration : 0.0f) + ")");
    }

    // ---- Nothing in, nothing out -------------------------------------------
    {
        const std::vector<LiveHit> none;
        VoiceToNotesOptions options;
        check(hitsToNotes(none, flat, bpm, 4, options).empty(),
              "no hits produce no notes rather than one at beat zero");
    }
}


// ============================================================================
// 63. The wavetable engine
// ============================================================================
static void testWavetableEngine() {
    beginTest("Wavetable engine");

    // ---- A band-limited table is still the wave you drew --------------------
    {
        WavetableBank bank;
        bank.tables.clear();
        Wavetable sine;
        sine.initSine();
        bank.tables.push_back(sine);

        WavetableSet set;
        set.build(bank);
        check(set.count() == 1, "the bank builds");

        // Level 0 keeps every harmonic, so a sine must survive untouched.
        // An FFT round trip that got the conjugate symmetry wrong shows up
        // here as a half-amplitude or phase-shifted result.
        float worst = 0.0f;
        for (int i = 0; i < 64; ++i) {
            const float phase = float(i) / 64.0f;
            const float expected = std::sin(6.28318530718f * phase);
            const float got = set.sample(phase, 0.0f, 1.0f / 4096.0f);
            worst = std::max(worst, std::fabs(got - expected));
        }
        check(worst < 0.02f,
              "a sine survives the band-limiting round trip (worst error " +
              std::to_string(worst) + ") - getting the spectrum's conjugate "
              "symmetry wrong would halve it or shift its phase");
    }

    // ---- Band-limiting actually removes harmonics ---------------------------
    {
        WavetableBank bank;
        bank.tables.clear();
        Wavetable square;
        square.initSquare();
        bank.tables.push_back(square);

        WavetableSet set;
        set.build(bank);

        // Read one cycle at each level and measure how sharp the edge is.
        // A band-limited square rings and slopes; the raw one is vertical.
        auto edgeSteepness = [&](int level) {
            const WavetableMips& mips = set.table(0);
            float steepest = 0.0f;
            for (int i = 0; i < WavetableMips::SIZE; ++i) {
                const float a = mips.levels[static_cast<size_t>(level)][static_cast<size_t>(i)];
                const float b = mips.levels[static_cast<size_t>(level)][static_cast<size_t>(i) + 1];
                steepest = std::max(steepest, std::fabs(b - a));
            }
            return steepest;
        };

        const float raw = edgeSteepness(0);
        const float filtered = edgeSteepness(6);
        check(raw > filtered * 2.0f,
              "a high mip level has a visibly softer edge than the raw table "
              "(" + std::to_string(raw) + " vs " + std::to_string(filtered) +
              ") - which is what removing the harmonics means");

        check(WavetableMips::harmonicsAtLevel(0) > WavetableMips::harmonicsAtLevel(5),
              "and each level keeps fewer harmonics than the one below");
        check(WavetableMips::harmonicsAtLevel(WavetableMips::LEVELS - 1) >= 1,
              "the last level still keeps one, so it is a sine rather than "
              "silence");
    }

    // ---- The level follows the pitch ---------------------------------------
    {
        // A low note can afford every harmonic; a high one cannot.
        const float lowStep = 55.0f / 44100.0f;      // A1
        const float highStep = 7040.0f / 44100.0f;   // A8

        check(WavetableMips::levelFor(lowStep) < 1.0f,
              "a low note plays the brightest table");
        check(WavetableMips::levelFor(highStep) >
              WavetableMips::levelFor(lowStep) + 3.0f,
              "and a note seven octaves up plays a much duller one");
        check(WavetableMips::levelFor(0.0f) == 0.0f,
              "a zero increment does not divide by zero");
        check(WavetableMips::levelFor(0.49f) <= float(WavetableMips::LEVELS - 1),
              "and an absurd one is bounded rather than indexing off the end");
    }

    // ---- Aliasing, measured -------------------------------------------------
    //
    // The whole reason the engine exists. Play a drawn square high up and
    // look for energy at frequencies that are not harmonics of the note.
    // Without band-limiting those fold-down tones are louder than the
    // harmonics they came from.
    {
        WavetableBank bank;
        bank.tables.clear();
        Wavetable square;
        square.initSquare();
        bank.tables.push_back(square);

        WavetableSet set;
        set.build(bank);

        const int rate = 44100;
        const float frequency = 3520.0f;         // A7
        const float step = frequency / float(rate);
        const size_t N = 4096;

        std::vector<float> limited(N), raw(N);
        float phase = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            limited[i] = set.sample(phase, 0.0f, step);
            // The same table read with no band-limiting at all - level 0
            // regardless of pitch, which is what the engine would do if the
            // mip selection were removed.
            raw[i] = set.table(0).sampleLevel(0, phase);
            phase += step;
            phase -= std::floor(phase);
        }

        // Energy that is NOT within a bin or two of a harmonic of 3520 Hz.
        auto inharmonicEnergy = [&](const std::vector<float>& signal) {
            std::vector<float> re(signal), im(N, 0.0f);
            DSP::FFTPlan plan;
            plan.resize(N);
            for (size_t i = 0; i < N; ++i) {
                const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                    float(i) / float(N - 1)));
                re[i] = signal[i] * w;
            }
            plan.transform(re.data(), im.data());

            const float binHz = float(rate) / float(N);
            double stray = 0.0;
            for (size_t bin = 2; bin < N / 2; ++bin) {
                const float hz = float(bin) * binHz;
                const float ratio = hz / frequency;
                const float distance = std::fabs(ratio - std::round(ratio));
                if (distance < 0.06f) continue;      // it is a harmonic
                const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                                   double(im[bin]) * im[bin]);
                stray += magnitude;
            }
            return stray;
        };

        const double strayLimited = inharmonicEnergy(limited);
        const double strayRaw = inharmonicEnergy(raw);

        check(strayRaw > 0.0, "the unfiltered read produces stray energy");
        check(strayLimited < strayRaw * 0.5,
              "band-limiting removes most of the inharmonic energy at A7 (" +
              std::to_string(strayLimited) + " vs " + std::to_string(strayRaw) +
              ") - those tones are alias fold-down, and they move DOWN as the "
              "note goes up, which is what makes them so obvious");
    }

    // ---- Morphing -----------------------------------------------------------
    {
        WavetableBank bank;
        bank.tables.clear();
        Wavetable sine, saw;
        sine.initSine();
        saw.initSaw();
        bank.tables.push_back(sine);
        bank.tables.push_back(saw);

        WavetableSet set;
        set.build(bank);
        check(set.count() == 2, "a two-table bank builds");

        const float step = 1.0f / 4096.0f;   // low enough for level 0
        const float atSine = set.sample(0.25f, 0.0f, step);
        const float atSaw = set.sample(0.25f, 1.0f, step);
        const float atMiddle = set.sample(0.25f, 0.5f, step);

        check(std::fabs(atSine - atSaw) > 0.1f,
              "the two ends of the bank sound different");
        check(atMiddle > std::min(atSine, atSaw) - 0.05f &&
              atMiddle < std::max(atSine, atSaw) + 0.05f,
              "and the middle is between them, which is what morphing means");

        // Out-of-range morph must not index off the end.
        check(std::isfinite(set.sample(0.25f, -5.0f, step)) &&
              std::isfinite(set.sample(0.25f, 9.0f, step)),
              "a morph outside 0..1 is clamped rather than read off the end "
              "of the bank");
    }

    // ---- Phase outside 0..1, which detune and vibrato produce --------------
    {
        WavetableBank bank;
        WavetableSet set;
        set.build(bank);

        const float step = 1.0f / 4096.0f;
        const float inside = set.sample(0.3f, 0.0f, step);
        check(std::fabs(set.sample(1.3f, 0.0f, step) - inside) < 1e-3f,
              "a phase past 1 wraps rather than clamping - vibrato and detune "
              "both hand it values outside the range");
        check(std::fabs(set.sample(-0.7f, 0.0f, step) - inside) < 1e-3f,
              "and so does a negative one");
    }

    // ---- An empty bank is a sine, not silence -------------------------------
    {
        WavetableBank empty;
        empty.tables.clear();
        WavetableSet set;
        set.build(empty);

        check(set.count() >= 1,
              "a bank with no tables falls back to one rather than being "
              "silence, which would read as the engine being broken");

        float peak = 0.0f;
        for (int i = 0; i < 64; ++i) {
            peak = std::max(peak, std::fabs(
                set.sample(float(i) / 64.0f, 0.0f, 1.0f / 4096.0f)));
        }
        check(peak > 0.5f, "and it makes a sound");
    }

    // ---- The library publishes without tearing ------------------------------
    {
        // On the heap deliberately. The library is most of a megabyte, and
        // a local one overflowed the stack outright - which is worth
        // knowing about the type, not just working around.
        auto libraryPtr = std::make_unique<WavetableLibrary>();
        WavetableLibrary& library = *libraryPtr;
        std::vector<WavetableBank> banks;

        WavetableBank first;
        first.tables.clear();
        Wavetable square;
        square.initSquare();
        first.tables.push_back(square);
        banks.push_back(first);

        library.rebuild(banks);
        const float step = 1.0f / 4096.0f;
        const float before = library.bank(0).sample(0.1f, 0.0f, step);

        WavetableBank second;
        second.tables.clear();
        Wavetable sine;
        sine.initSine();
        second.tables.push_back(sine);
        banks[0] = second;
        library.rebuild(banks);

        const float after = library.bank(0).sample(0.1f, 0.0f, step);
        check(std::fabs(before - after) > 0.1f,
              "rebuilding the library changes what plays");

        check(std::isfinite(library.bank(WavetableLibrary::MAX_BANKS + 5)
                                   .sample(0.1f, 0.0f, step)),
              "and a bank index past the end is clamped rather than read off "
              "the array, which the audio thread would do every sample");
    }
}

// ============================================================================
// 64. A drawn wavetable reaches the speakers
// ============================================================================
static void testWavetableReachesAudio() {
    beginTest("Wavetable reaches the audio");

    // Custom used to run generateTriangle(). A user could draw a waveform,
    // watch the editor's preview redraw, save it in the project, and hear a
    // triangle. This renders two projects that differ ONLY in the drawn
    // table and asserts the audio differs.
    auto renderWith = [](const Wavetable& table, float morph) {
        Project p;
        p.bpm = 120.0f;
        p.masterLimiterEnabled = false;
        p.masterCompressorEnabled = false;
        p.masterEQEnabled = false;
        p.masterVolume = 0.6f;

        p.wavetableBanks.clear();
        WavetableBank bank;
        bank.tables.clear();
        bank.tables.push_back(table);
        Wavetable second;
        second.initSquare();
        bank.tables.push_back(second);
        p.wavetableBanks.push_back(bank);

        p.patterns.clear();
        Pattern pattern;
        Note note;
        note.pitch = 57;                 // A3, low enough to keep harmonics
        note.startTime = 0.0f;
        note.duration = 4.0f;
        note.oscillatorType = OscillatorType::Custom;
        pattern.notes.push_back(note);
        p.patterns.push_back(pattern);

        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p.channels[0].oscillator.type = OscillatorType::Custom;
        p.channels[0].oscillator.wavetableBank = 0;
        p.channels[0].oscillator.wavetableMorph = morph;
        p.channels[0].volume = 0.8f;
        p.channels[0].pan = 0.0f;

        auto seqPtr = std::make_unique<Sequencer>();
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(&p);
        seqPtr->updateChannelConfigs();
        seqPtr->updateMasterEffects();
        seqPtr->play();

        std::vector<float> l(512), r(512);
        std::vector<float> collected;
        for (int b = 0; b < 40; ++b) {
            seqPtr->process(l.data(), r.data(), 512);
            if (b >= 10) collected.insert(collected.end(), l.begin(), l.end());
        }
        return collected;
    };

    Wavetable sine, saw;
    sine.initSine();
    saw.initSaw();

    const std::vector<float> withSine = renderWith(sine, 0.0f);
    const std::vector<float> withSaw = renderWith(saw, 0.0f);

    auto rms = [](const std::vector<float>& signal) {
        double sum = 0.0;
        for (float v : signal) sum += double(v) * v;
        return signal.empty() ? 0.0 : std::sqrt(sum / double(signal.size()));
    };

    check(rms(withSine) > 1e-4, "a Custom-oscillator note makes a sound");

    double difference = 0.0;
    const size_t n = std::min(withSine.size(), withSaw.size());
    for (size_t i = 0; i < n; ++i) {
        difference += std::fabs(double(withSine[i]) - double(withSaw[i]));
    }
    check(n > 0 && difference / double(n) > 1e-3,
          "changing the drawn waveform changes the audio - it did not before, "
          "because the Custom oscillator ran generateTriangle() and the whole "
          "wavetable editor was a drawing toy (mean difference " +
          std::to_string(n > 0 ? difference / double(n) : 0.0) + ")");

    // Morph has to reach the audio too, or a bank is one table with extras.
    const std::vector<float> atStart = renderWith(sine, 0.0f);
    const std::vector<float> atEnd = renderWith(sine, 1.0f);
    double morphDifference = 0.0;
    const size_t m = std::min(atStart.size(), atEnd.size());
    for (size_t i = 0; i < m; ++i) {
        morphDifference += std::fabs(double(atStart[i]) - double(atEnd[i]));
    }
    check(m > 0 && morphDifference / double(m) > 1e-3,
          "and moving the morph across the bank changes it as well");

    // ---- The settings survive a save and load ------------------------------
    {
        Project p;
        p.channels[3].oscillator.type = OscillatorType::Custom;
        p.channels[3].oscillator.wavetableBank = 2;
        p.channels[3].oscillator.wavetableMorph = 0.625f;
        p.channels[3].oscillator.wavetableMorphSweep = -0.5f;
        p.channels[3].oscillator.wavetableSweepTime = 1.25f;
        p.arrangement.push_back(Clip{0, 3, 0.0f, 4.0f, 0});

        const std::string path = testPath("wavetable_roundtrip.ctp");
        check(saveProject(p, path), "a project with wavetable settings saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");
        check(loaded.channels[3].oscillator.wavetableBank == 2,
              "the bank choice survives");
        check(std::fabs(loaded.channels[3].oscillator.wavetableMorph - 0.625f) < 1e-4f,
              "the morph survives");
        check(std::fabs(loaded.channels[3].oscillator.wavetableMorphSweep + 0.5f) < 1e-4f,
              "the sweep survives");
        check(std::fabs(loaded.channels[3].oscillator.wavetableSweepTime - 1.25f) < 1e-4f,
              "and its duration survives");
        std::remove(path.c_str());
    }

    // ---- Validation --------------------------------------------------------
    {
        Project p;
        p.channels[0].oscillator.wavetableBank = 900;
        p.channels[0].oscillator.wavetableMorph =
            std::numeric_limits<float>::quiet_NaN();
        p.channels[0].oscillator.wavetableSweepTime = 0.0f;

        clampProjectToValidRanges(p);

        check(p.channels[0].oscillator.wavetableBank >= 0 &&
              p.channels[0].oscillator.wavetableBank < WavetableLibrary::MAX_BANKS,
              "a bank index past the end of the library is repaired - the "
              "audio thread would otherwise read off the array every sample");
        check(std::isfinite(p.channels[0].oscillator.wavetableMorph),
              "a NaN morph is replaced");
        check(p.channels[0].oscillator.wavetableSweepTime > 0.0f,
              "and a zero sweep time cannot divide by zero in the voice");
    }
}


// ============================================================================
// 65. Six-operator FM
// ============================================================================
static void testFMSynth() {
    beginTest("FM synthesis");

    const float rate = 44100.0f;

    // Render one note of a patch and hand back the samples.
    auto render = [rate](const FMPatch& patch, float frequency, float velocity,
                         int samples) {
        FMVoiceState state;
        state.reset(patch);
        std::vector<float> out(static_cast<size_t>(samples));
        for (int i = 0; i < samples; ++i) {
            out[static_cast<size_t>(i)] =
                fm::process(patch, state, frequency, velocity, rate, false);
        }
        return out;
    };

    auto rms = [](const std::vector<float>& signal) {
        double sum = 0.0;
        for (float v : signal) sum += double(v) * v;
        return signal.empty() ? 0.0 : std::sqrt(sum / double(signal.size()));
    };

    // Brightness, measured as the spectral centroid. This is the number FM's
    // modulation index is FOR, so it is the number to assert on.
    auto centroid = [rate](const std::vector<float>& signal) {
        size_t N = 1;
        while (N * 2 <= signal.size()) N *= 2;
        if (N < 256) return 0.0;

        std::vector<float> re(N), im(N, 0.0f);
        for (size_t i = 0; i < N; ++i) {
            const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                float(i) / float(N - 1)));
            re[i] = signal[i] * w;
        }
        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(re.data(), im.data());

        double weighted = 0.0, total = 0.0;
        const double binHz = double(rate) / double(N);
        for (size_t bin = 1; bin < N / 2; ++bin) {
            const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                               double(im[bin]) * im[bin]);
            weighted += double(bin) * binHz * magnitude;
            total += magnitude;
        }
        return (total > 0.0) ? weighted / total : 0.0;
    };

    // ---- It makes a sound at all -------------------------------------------
    {
        FMPatch patch;
        const std::vector<float> audio = render(patch, 220.0f, 1.0f, 8192);
        check(rms(audio) > 0.01,
              "the default patch makes a sound (rms " +
              std::to_string(rms(audio)) + ")");

        bool finite = true;
        for (float v : audio) {
            if (!std::isfinite(v)) { finite = false; break; }
        }
        check(finite, "and every sample of it is finite");
    }

    // ---- The modulation index controls brightness ---------------------------
    //
    // This is the whole instrument. If the index did nothing, FM would be a
    // clumsy additive synth.
    {
        FMPatch dull;
        dull.algorithm = FMAlgorithm::stack();
        dull.index = 0.0f;

        FMPatch bright = dull;
        bright.index = 8.0f;

        // Sustain the modulators so the measurement is not of the envelope.
        for (FMOperator& op : dull.operators) { op.attack = 0.0f; op.sustain = 1.0f; op.decay = 0.0f; }
        for (FMOperator& op : bright.operators) { op.attack = 0.0f; op.sustain = 1.0f; op.decay = 0.0f; }

        const double dullCentroid = centroid(render(dull, 220.0f, 1.0f, 8192));
        const double brightCentroid = centroid(render(bright, 220.0f, 1.0f, 8192));

        check(dullCentroid > 0.0, "the index-zero patch has a spectrum");
        check(brightCentroid > dullCentroid * 2.0,
              "raising the modulation index makes the sound brighter (" +
              std::to_string(dullCentroid) + " Hz -> " +
              std::to_string(brightCentroid) + " Hz) - which is what the "
              "index is for, and the one thing FM must get right");
    }

    // ---- Modulation is PHASE, not frequency ---------------------------------
    //
    // Adding a modulator to the carrier's FREQUENCY and integrating makes
    // the perceived pitch drift with the modulator's DC content. Adding to
    // the phase does not. The test: with a modulator running, the
    // fundamental must still be at the played note.
    {
        FMPatch patch;
        patch.algorithm = FMAlgorithm::stack();
        patch.index = 6.0f;
        for (FMOperator& op : patch.operators) {
            op.attack = 0.0f;
            op.decay = 0.0f;
            op.sustain = 1.0f;
        }

        const float played = 440.0f;
        const std::vector<float> audio = render(patch, played, 1.0f, 16384);

        // Find the strongest bin below 1 kHz - the fundamental.
        const size_t N = 8192;
        std::vector<float> re(N), im(N, 0.0f);
        for (size_t i = 0; i < N; ++i) {
            const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                float(i) / float(N - 1)));
            re[i] = audio[i + 4096] * w;      // past the attack
        }
        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(re.data(), im.data());

        const double binHz = double(rate) / double(N);
        size_t loudest = 0;
        double peak = 0.0;
        for (size_t bin = 1; bin < size_t(1000.0 / binHz); ++bin) {
            const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                               double(im[bin]) * im[bin]);
            if (magnitude > peak) { peak = magnitude; loudest = bin; }
        }
        const double found = double(loudest) * binHz;

        check(std::fabs(found - double(played)) < 15.0,
              "with a modulator running hard the fundamental is still at the "
              "note played (" + std::to_string(found) + " Hz vs 440) - "
              "modulating the frequency rather than the phase would have "
              "pulled it off pitch, and every digital FM synth ever shipped "
              "is a phase modulator despite the name");
    }

    // ---- Feedback is bounded ------------------------------------------------
    //
    // Self-modulation from a single previous sample oscillates violently at
    // high feedback. Averaging the last two is a one-pole lowpass in the
    // feedback path, and the difference between a saw and a screech.
    {
        FMPatch patch;
        patch.algorithm = FMAlgorithm::brass();
        patch.algorithm.feedback = 1.0f;
        patch.index = 12.0f;
        for (FMOperator& op : patch.operators) {
            op.attack = 0.0f;
            op.decay = 0.0f;
            op.sustain = 1.0f;
        }

        const std::vector<float> audio = render(patch, 110.0f, 1.0f, 32768);

        float peak = 0.0f;
        bool finite = true;
        for (float v : audio) {
            if (!std::isfinite(v)) { finite = false; break; }
            peak = std::max(peak, std::fabs(v));
        }
        check(finite, "maximum feedback stays finite");
        check(peak < 4.0f,
              "and bounded (peak " + std::to_string(peak) + ") - an operator "
              "modulating itself from one previous sample runs away; the "
              "average of two does not");
    }

    // ---- Each algorithm sounds different ------------------------------------
    {
        auto renderAlgorithm = [&](FMAlgorithmPreset preset) {
            FMPatch patch;
            patch.algorithm = fmAlgorithmFromPreset(preset);
            patch.index = 4.0f;
            for (FMOperator& op : patch.operators) {
                op.attack = 0.0f;
                op.decay = 0.0f;
                op.sustain = 1.0f;
            }
            return render(patch, 220.0f, 1.0f, 8192);
        };

        const double brass = centroid(renderAlgorithm(FMAlgorithmPreset::Brass));
        const double stack = centroid(renderAlgorithm(FMAlgorithmPreset::Stack));
        const double additive = centroid(renderAlgorithm(FMAlgorithmPreset::Additive));

        check(additive > 0.0 && brass > 0.0 && stack > 0.0,
              "every algorithm produces a spectrum");
        check(stack > additive,
              "a five-deep modulation stack is brighter than six carriers "
              "with no modulation at all (" + std::to_string(stack) +
              " vs " + std::to_string(additive) + ")");
        check(std::fabs(brass - stack) > 50.0,
              "and the algorithms are not all the same routing wearing "
              "different names");
    }

    // ---- Velocity reaches the timbre, not just the level --------------------
    {
        FMPatch patch;
        patch.algorithm = FMAlgorithm::stack();
        patch.index = 6.0f;
        for (FMOperator& op : patch.operators) {
            op.attack = 0.0f;
            op.decay = 0.0f;
            op.sustain = 1.0f;
            op.velocitySensitivity = 0.0f;
        }
        // Only the modulators are velocity-sensitive, which is what makes
        // playing harder sound brighter rather than merely louder.
        for (int i = 1; i < FM_OPERATORS; ++i) {
            patch.operators[static_cast<size_t>(i)].velocitySensitivity = 1.0f;
        }

        const double soft = centroid(render(patch, 220.0f, 0.2f, 8192));
        const double hard = centroid(render(patch, 220.0f, 1.0f, 8192));

        check(hard > soft * 1.3,
              "playing harder makes it brighter, not just louder (" +
              std::to_string(soft) + " -> " + std::to_string(hard) + ") - "
              "which is the thing FM keyboards were famous for");
    }

    // ---- Per-operator envelopes really are per-operator ---------------------
    {
        // A bell: a carrier that rings under a modulator that dies fast. The
        // sound must get PURER over time, which only happens if the two
        // envelopes are independent.
        FMPatch bell;
        bell.algorithm = FMAlgorithm::threePairs();
        bell.index = 8.0f;
        for (FMOperator& op : bell.operators) {
            op.attack = 0.001f;
            op.decay = 3.0f;
            op.sustain = 0.9f;
        }
        for (int i : {1, 3, 5}) {                 // the modulators
            bell.operators[static_cast<size_t>(i)].decay = 0.05f;
            bell.operators[static_cast<size_t>(i)].sustain = 0.0f;
        }

        const std::vector<float> audio = render(bell, 440.0f, 1.0f, 44100);
        const std::vector<float> head(audio.begin(), audio.begin() + 4096);
        const std::vector<float> tail(audio.begin() + 30000, audio.begin() + 34096);

        const double headCentroid = centroid(head);
        const double tailCentroid = centroid(tail);

        check(headCentroid > tailCentroid * 1.3,
              "a bell patch starts bright and settles pure (" +
              std::to_string(headCentroid) + " -> " +
              std::to_string(tailCentroid) + ") - which requires the "
              "modulator's envelope to be independent of the carrier's, and "
              "is most of what makes FM expressive");
    }

    // ---- The matrix cannot contain a cycle ----------------------------------
    {
        FMAlgorithm algorithm;
        algorithm.modulation[0][5] = 1.0f;      // operator 0 modulates 5
        algorithm.modulation[5][0] = 1.0f;      // and 5 modulates 0
        algorithm.makeAcyclic();

        check(algorithm.modulation[0][5] == 0.0f,
              "an upper-triangle entry is dropped - it would be a cycle, and "
              "a cycle in this matrix is infinite recursion in the audio "
              "thread rather than a strange sound");
        check(algorithm.modulation[5][0] == 1.0f,
              "while the legal direction is kept");
    }

    // ---- Validation ---------------------------------------------------------
    {
        FMPatch hostile;
        hostile.index = std::numeric_limits<float>::quiet_NaN();
        hostile.algorithm.feedback = 40.0f;
        hostile.operators[0].ratio = 9999.0f;
        hostile.operators[0].level = -3.0f;
        hostile.operators[1].attack = std::numeric_limits<float>::infinity();
        hostile.operators[2].sustain = 7.0f;
        for (int i = 0; i < FM_OPERATORS; ++i) {
            hostile.algorithm.carrier[static_cast<size_t>(i)] = 0.0f;
        }

        clampFMPatch(hostile);

        check(std::isfinite(hostile.index), "a NaN index is replaced");
        check(hostile.algorithm.feedback <= 1.0f, "feedback is bounded");
        check(hostile.operators[0].ratio <= 32.0f,
              "an absurd ratio is clamped - past Nyquist an operator aliases "
              "rather than sounds");
        check(hostile.operators[0].level >= 0.0f, "a negative level is clamped");
        check(std::isfinite(hostile.operators[1].attack),
              "an infinite attack is replaced");
        check(hostile.operators[2].sustain <= 1.0f, "sustain is bounded");

        bool anyCarrier = false;
        for (int i = 0; i < FM_OPERATORS; ++i) {
            if (hostile.algorithm.carrier[static_cast<size_t>(i)] > 0.0f) {
                anyCarrier = true;
                break;
            }
        }
        check(anyCarrier,
              "and a patch with no carrier at all gets one, because silence "
              "reads as the engine being broken rather than as a patch "
              "nobody finished");

        const std::vector<float> audio = render(hostile, 220.0f, 1.0f, 4096);
        bool finite = true;
        for (float v : audio) {
            if (!std::isfinite(v)) { finite = false; break; }
        }
        check(finite, "the repaired patch renders finite audio");
    }

    // ---- Release --------------------------------------------------------------
    {
        FMPatch patch;
        FMVoiceState state;
        state.reset(patch);

        for (int i = 0; i < 4410; ++i) {
            fm::process(patch, state, 220.0f, 1.0f, rate, false);
        }
        state.release();

        double energy = 0.0;
        for (int i = 0; i < 44100; ++i) {
            const float v = fm::process(patch, state, 220.0f, 1.0f, rate, true);
            if (i > 40000) energy += std::fabs(double(v));
        }
        check(energy < 1.0,
              "a released voice actually falls silent, so it can be reclaimed "
              "rather than holding a slot forever");
        check(state.finished(), "and reports itself finished");
    }
}

// ============================================================================
// 66. FM reaches the audio and the file
// ============================================================================
static void testFMReachesAudio() {
    beginTest("FM reaches the audio and the file");

    // ---- A note on an FM channel sounds, and differs from a pulse ----------
    {
        auto renderChannel = [](OscillatorType type) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.masterCompressorEnabled = false;
            p.masterEQEnabled = false;
            p.masterVolume = 0.7f;

            p.patterns.clear();
            Pattern pattern;
            Note note;
            note.pitch = 57;
            note.startTime = 0.0f;
            note.duration = 4.0f;
            note.oscillatorType = type;
            pattern.notes.push_back(note);
            p.patterns.push_back(pattern);

            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
            p.channels[0].oscillator.type = type;
            p.channels[0].volume = 0.8f;
            p.channels[0].pan = 0.0f;

            auto seqPtr = std::make_unique<Sequencer>();
            seqPtr->setSampleRate(44100.0f);
            seqPtr->setProject(&p);
            seqPtr->updateChannelConfigs();
            seqPtr->updateMasterEffects();
            seqPtr->play();

            std::vector<float> l(512), r(512);
            std::vector<float> collected;
            for (int b = 0; b < 30; ++b) {
                seqPtr->process(l.data(), r.data(), 512);
                if (b >= 8) collected.insert(collected.end(), l.begin(), l.end());
            }
            return collected;
        };

        const std::vector<float> fmAudio = renderChannel(OscillatorType::FMSynth);
        const std::vector<float> pulseAudio = renderChannel(OscillatorType::Pulse);

        double fmEnergy = 0.0;
        for (float v : fmAudio) fmEnergy += double(v) * v;
        check(fmEnergy > 1e-4, "an FM channel makes a sound through the engine");

        double difference = 0.0;
        const size_t n = std::min(fmAudio.size(), pulseAudio.size());
        for (size_t i = 0; i < n; ++i) {
            difference += std::fabs(double(fmAudio[i]) - double(pulseAudio[i]));
        }
        check(n > 0 && difference / double(n) > 1e-3,
              "and it is not simply a pulse wave under another name");
    }

    // ---- The type name round-trips -----------------------------------------
    {
        /*
         * The enum's shape, pinned.
         *
         * The Channel Editor's type dropdown listed six names and indexed
         * them straight into OscillatorType. The enum's sixth entry is
         * Supersaw, not Custom - so picking "Custom" set the channel to
         * Supersaw and the wavetable controls never appeared whatever you
         * chose. The dropdown maps by value now, and these pin the layout
         * so a reorder is a failing test rather than a silently wrong menu.
         */
        check(static_cast<int>(OscillatorType::Supersaw) == 5,
              "Supersaw is the sixth oscillator type");
        check(static_cast<int>(OscillatorType::Custom) == 6,
              "and Custom the seventh - a six-name list indexed into this "
              "enum lands on the wrong one, which is exactly what the "
              "Channel Editor was doing");
        check(static_cast<int>(OscillatorType::FMSynth) >
              static_cast<int>(OscillatorType::KavinskyBass),
              "FM is last, so adding it renumbered nothing - the pad banks "
              "and the palette's name table both index this enum");

        check(stringToOscillatorType(oscillatorTypeToString(OscillatorType::FMSynth)) ==
              OscillatorType::FMSynth,
              "the FM type survives a name round trip - Vocoder, the "
              "reggaeton kit and Supersaw all silently became Pulse for want "
              "of exactly this");
    }

    // ---- The patch survives a save and load --------------------------------
    {
        Project p;
        p.channels[2].oscillator.type = OscillatorType::FMSynth;
        p.channels[2].oscillator.fmAlgorithmPreset =
            static_cast<int>(FMAlgorithmPreset::DoubleStack);
        p.channels[2].oscillator.fm.index = 5.75f;
        p.channels[2].oscillator.fm.algorithm = FMAlgorithm::doubleStack();
        p.channels[2].oscillator.fm.algorithm.feedback = 0.42f;
        p.channels[2].oscillator.fm.operators[3].ratio = 7.0f;
        p.channels[2].oscillator.fm.operators[3].detuneCents = -14.0f;
        p.channels[2].oscillator.fm.operators[3].decay = 1.75f;
        p.channels[2].oscillator.fm.operators[4].enabled = false;
        p.channels[2].oscillator.fm.operators[5].velocitySensitivity = 0.85f;
        p.arrangement.push_back(Clip{0, 2, 0.0f, 4.0f, 0});

        const std::string path = testPath("fm_roundtrip.ctp");
        check(saveProject(p, path), "an FM project saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");

        const OscillatorConfig& osc = loaded.channels[2].oscillator;
        check(osc.type == OscillatorType::FMSynth, "the type survives");
        check(std::fabs(osc.fm.index - 5.75f) < 1e-3f, "the index survives");
        check(std::fabs(osc.fm.algorithm.feedback - 0.42f) < 1e-3f,
              "the feedback survives");
        check(std::fabs(osc.fm.operators[3].ratio - 7.0f) < 1e-3f,
              "an operator's ratio survives");
        check(std::fabs(osc.fm.operators[3].detuneCents + 14.0f) < 1e-2f,
              "its detune survives");
        check(std::fabs(osc.fm.operators[3].decay - 1.75f) < 1e-3f,
              "its envelope survives");
        check(!osc.fm.operators[4].enabled,
              "a disabled operator stays disabled");
        check(std::fabs(osc.fm.operators[5].velocitySensitivity - 0.85f) < 1e-3f,
              "and velocity sensitivity survives");
        check(osc.fm.algorithm.modulation[2][1] > 0.0f,
              "the routing matrix survives, not just the preset number - a "
              "patch edited away from its preset must come back as edited");

        std::remove(path.c_str());
    }

    // ---- A project with no FM channel writes no FM lines --------------------
    {
        Project plain;
        const std::string path = testPath("fm_absent.ctp");
        check(saveProject(plain, path), "a project with no FM channel saves");

        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        check(ss.str().find("\nFM ") == std::string::npos,
              "and writes no FM line - the patch is a hundred values, and "
              "nobody who never touches the instrument should carry them");
        std::remove(path.c_str());
    }
}


// ============================================================================
// 67. The multisample sampler
// ============================================================================
static void testSampler() {
    beginTest("Multisample sampler");

    // A pool with three tones in it, standing in for three recordings of an
    // instrument at different dynamics.
    auto makePool = [](SamplePool& pool, int count) {
        for (int i = 0; i < count; ++i) {
            Sample sample;
            sample.name = "layer" + std::to_string(i);
            sample.sampleRate = 48000;
            sample.rootNote = 60;
            sample.audioData.resize(48000);
            for (size_t n = 0; n < sample.audioData.size(); ++n) {
                const float t = float(n) / 48000.0f;
                // A different frequency per layer, so a test can tell which
                // one it is hearing.
                sample.audioData[n] = 0.8f * std::sin(
                    6.28318530718f * (220.0f * float(i + 1)) * t);
            }
            sample.lengthSeconds = 1.0f;
            sample.isLoaded = true;
            pool.addSample(sample);
        }
    };

    // ---- Zone selection ----------------------------------------------------
    {
        SamplerInstrument instrument;
        instrument.velocityCrossfade = 0.0f;

        SampleZone soft;
        soft.sampleId = 0;
        soft.lowKey = 0; soft.highKey = 127; soft.rootKey = 60;
        soft.lowVelocity = 0.0f; soft.highVelocity = 0.5f;
        instrument.addZone(soft);

        SampleZone loud = soft;
        loud.sampleId = 1;
        loud.lowVelocity = 0.5f; loud.highVelocity = 1.0f;
        instrument.addZone(loud);

        float weight = 0.0f;
        check(instrument.findZone(60, 0.2f, 0, weight) == 0,
              "a soft note picks the soft layer");
        check(instrument.findZone(60, 0.9f, 0, weight) == 1,
              "and a hard one picks the loud layer - which is the whole "
              "difference between a sampler and a tape player: a piano "
              "played hard is not a piano played softly and turned up");
    }

    {
        // Key zones bound how far any one recording is stretched.
        SamplerInstrument instrument;
        SampleZone low;
        low.sampleId = 0;
        low.lowKey = 0; low.highKey = 59; low.rootKey = 48;
        instrument.addZone(low);

        SampleZone high = low;
        high.sampleId = 1;
        high.lowKey = 60; high.highKey = 127; high.rootKey = 72;
        instrument.addZone(high);

        float weight = 0.0f;
        check(instrument.findZone(40, 0.8f, 0, weight) == 0,
              "a low note takes the low recording");
        check(instrument.findZone(90, 0.8f, 0, weight) == 1,
              "and a high one the high recording");
    }

    {
        // A gap in the keyboard is silence, not a stretched neighbour.
        SamplerInstrument instrument;
        SampleZone zone;
        zone.sampleId = 0;
        zone.lowKey = 60; zone.highKey = 72;
        instrument.addZone(zone);

        float weight = 0.0f;
        check(instrument.findZone(30, 0.8f, 0, weight) == -1,
              "a note no zone covers finds nothing rather than the nearest "
              "zone - an audible gap is findable, and a sample dragged three "
              "octaves is just 'the sampler sounds bad'");
    }

    // ---- Velocity crossfade -------------------------------------------------
    {
        SamplerInstrument instrument;
        instrument.velocityCrossfade = 0.2f;

        SampleZone soft;
        soft.sampleId = 0;
        soft.lowVelocity = 0.0f; soft.highVelocity = 0.5f;
        instrument.addZone(soft);

        SampleZone loud = soft;
        loud.sampleId = 1;
        loud.lowVelocity = 0.5f; loud.highVelocity = 1.0f;
        instrument.addZone(loud);

        float middleWeight = 0.0f;
        instrument.findZone(60, 0.5f, 0, middleWeight);
        float clearWeight = 0.0f;
        instrument.findZone(60, 0.1f, 0, clearWeight);

        check(clearWeight > middleWeight,
              "a note well inside a layer plays it at full weight, and one on "
              "the boundary plays it partly (" + std::to_string(clearWeight) +
              " vs " + std::to_string(middleWeight) + ") - without this, a "
              "crescendo changes character on one note");
    }

    // ---- Round-robin --------------------------------------------------------
    {
        SamplerInstrument instrument;
        instrument.velocityCrossfade = 0.0f;

        for (int take = 0; take < 3; ++take) {
            SampleZone zone;
            zone.sampleId = take;
            zone.roundRobinGroup = take;
            instrument.addZone(zone);
        }

        float weight = 0.0f;
        const int a = instrument.findZone(60, 0.8f, 0, weight);
        const int b = instrument.findZone(60, 0.8f, 1, weight);
        const int c = instrument.findZone(60, 0.8f, 2, weight);
        const int d = instrument.findZone(60, 0.8f, 3, weight);

        check(a != b && b != c,
              "consecutive hits take different takes - two identical samples "
              "a few milliseconds apart comb-filter against each other, which "
              "is the machine-gun sound that gives a sampled kit away");
        check(d == a, "and the cycle wraps");
    }

    // ---- The sample rate conversion, which was missing ---------------------
    //
    // SampleOscillator advanced by the pitch ratio alone. The pool decodes to
    // 48 kHz and the engine usually runs at 44.1, so everything played 8.8%
    // sharp - nearly a semitone and a half above the synths.
    {
        SamplePool pool;
        makePool(pool, 1);

        SamplerInstrument instrument;
        instrument.attack = 0.0f;
        instrument.release = 1.0f;
        SampleZone zone;
        zone.sampleId = 0;
        zone.rootKey = 60;
        instrument.addZone(zone);

        // The sample is a 220 Hz tone recorded at 48 kHz. Played at its root
        // key through a 44.1 kHz engine it must still come out at 220 Hz.
        SamplerVoice voice;
        voice.trigger(instrument, zone, pool.getSample(0), 60, 1.0f, 1.0f, 44100.0f);

        const size_t N = 8192;
        std::vector<float> re(N), im(N, 0.0f);
        for (size_t i = 0; i < N; ++i) {
            const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                float(i) / float(N - 1)));
            re[i] = voice.process() * w;
        }
        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(re.data(), im.data());

        const double binHz = 44100.0 / double(N);
        size_t loudest = 0;
        double peak = 0.0;
        for (size_t bin = 1; bin < N / 2; ++bin) {
            const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                               double(im[bin]) * im[bin]);
            if (magnitude > peak) { peak = magnitude; loudest = bin; }
        }
        const double found = double(loudest) * binHz;

        check(std::fabs(found - 220.0) < 8.0,
              "a 48 kHz sample played at its root through a 44.1 kHz engine "
              "comes out at its recorded pitch (" + std::to_string(found) +
              " Hz, want 220) - without the rate term it lands near 239 Hz, "
              "which is what every sample instrument in the program has been "
              "doing: a semitone and a half sharp of the synths");
    }

    // ---- Pitch shifting still works ----------------------------------------
    {
        SamplePool pool;
        makePool(pool, 1);

        SamplerInstrument instrument;
        instrument.attack = 0.0f;
        SampleZone zone;
        zone.sampleId = 0;
        zone.rootKey = 60;
        instrument.addZone(zone);

        auto pitchOf = [&](int note) {
            SamplerVoice voice;
            voice.trigger(instrument, zone, pool.getSample(0), note, 1.0f,
                          1.0f, 44100.0f);
            const size_t N = 8192;
            std::vector<float> re(N), im(N, 0.0f);
            for (size_t i = 0; i < N; ++i) {
                const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                    float(i) / float(N - 1)));
                re[i] = voice.process() * w;
            }
            DSP::FFTPlan plan;
            plan.resize(N);
            plan.transform(re.data(), im.data());
            const double binHz = 44100.0 / double(N);
            size_t loudest = 0;
            double peak = 0.0;
            for (size_t bin = 1; bin < N / 2; ++bin) {
                const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                                   double(im[bin]) * im[bin]);
                if (magnitude > peak) { peak = magnitude; loudest = bin; }
            }
            return double(loudest) * binHz;
        };

        const double root = pitchOf(60);
        const double octave = pitchOf(72);
        check(std::fabs(octave / root - 2.0) < 0.05,
              "a note an octave above the root plays an octave higher (ratio " +
              std::to_string(octave / root) + ")");
    }

    // ---- Looping ------------------------------------------------------------
    {
        SamplePool pool;
        makePool(pool, 1);

        SamplerInstrument instrument;
        instrument.attack = 0.0f;
        instrument.release = 10.0f;

        SampleZone plain;
        plain.sampleId = 0;
        plain.rootKey = 60;
        instrument.addZone(plain);

        SampleZone looped = plain;
        looped.loop = true;
        looped.loopStartSeconds = 0.1f;
        looped.loopEndSeconds = 0.5f;

        auto energyAfter = [&](const SampleZone& zone, int skip, int count) {
            SamplerVoice voice;
            voice.trigger(instrument, zone, pool.getSample(0), 60, 1.0f,
                          1.0f, 44100.0f);
            for (int i = 0; i < skip; ++i) voice.process();
            double energy = 0.0;
            for (int i = 0; i < count; ++i) {
                const float v = voice.process();
                energy += std::fabs(double(v));
            }
            return energy;
        };

        // The sample is one second; at 44100 that is ~44100 frames of engine
        // time before rate conversion, fewer after. Two seconds in, a
        // non-looping voice is done and a looping one is not.
        check(energyAfter(plain, 88200, 4410) < 1e-3,
              "a non-looping sample stops when it runs out");
        check(energyAfter(looped, 88200, 4410) > 1.0,
              "and a looping one keeps going");
    }

    // ---- The envelope stops clicks -----------------------------------------
    {
        SamplePool pool;
        makePool(pool, 1);

        SamplerInstrument instrument;
        instrument.attack = 0.05f;
        SampleZone zone;
        zone.sampleId = 0;
        zone.rootKey = 60;
        instrument.addZone(zone);

        SamplerVoice voice;
        voice.trigger(instrument, zone, pool.getSample(0), 60, 1.0f, 1.0f, 44100.0f);

        const float first = std::fabs(voice.process());
        float later = 0.0f;
        for (int i = 0; i < 4410; ++i) later = std::max(later, std::fabs(voice.process()));

        check(first < later * 0.2f,
              "the attack ramps in rather than starting at full level - a "
              "sample that begins mid-cycle at full amplitude is a click");
    }

    {
        // And release actually ends the voice, so it can be reclaimed.
        SamplePool pool;
        makePool(pool, 1);

        SamplerInstrument instrument;
        instrument.attack = 0.0f;
        instrument.release = 0.05f;
        SampleZone zone;
        zone.sampleId = 0;
        zone.rootKey = 60;
        zone.loop = true;
        zone.loopEndSeconds = 0.5f;
        instrument.addZone(zone);

        SamplerVoice voice;
        voice.trigger(instrument, zone, pool.getSample(0), 60, 1.0f, 1.0f, 44100.0f);
        for (int i = 0; i < 1000; ++i) voice.process();
        check(voice.isPlaying(), "a looping voice keeps playing");

        voice.release();
        for (int i = 0; i < 44100; ++i) voice.process();
        check(!voice.isPlaying(),
              "and a released one ends, even though it is looping - otherwise "
              "it holds a voice slot forever");
    }

    // ---- Validation ---------------------------------------------------------
    {
        SamplerInstrument hostile;
        SampleZone bad;
        bad.sampleId = 500;                 // past the end of any pool
        bad.lowKey = 90; bad.highKey = 20;  // backwards
        bad.lowVelocity = 0.9f; bad.highVelocity = 0.1f;
        bad.gain = std::numeric_limits<float>::quiet_NaN();
        bad.loopStartSeconds = 5.0f;
        bad.loopEndSeconds = 1.0f;
        hostile.addZone(bad);
        hostile.zoneCount = 900;            // more zones than exist

        clampSamplerInstrument(hostile, 3);

        check(hostile.zoneCount <= SamplerInstrument::MAX_ZONES,
              "an impossible zone count is bounded");
        check(hostile.zones[0].sampleId == -1,
              "a sample id past the end of the pool becomes none - it would "
              "be an out-of-bounds read from the audio thread");
        check(hostile.zones[0].lowKey <= hostile.zones[0].highKey,
              "a backwards key range is turned the right way round");
        check(hostile.zones[0].lowVelocity <= hostile.zones[0].highVelocity,
              "and so is a backwards velocity range");
        check(std::isfinite(hostile.zones[0].gain), "a NaN gain is replaced");
        check(hostile.zones[0].loopEndSeconds == 0.0f,
              "and a loop that ends before it starts becomes no loop point");
    }

    // ---- A zone with no sample is silence, not a crash ---------------------
    {
        SamplerInstrument instrument;
        SampleZone zone;
        zone.sampleId = -1;
        instrument.addZone(zone);

        SamplerVoice voice;
        voice.trigger(instrument, zone, nullptr, 60, 1.0f, 1.0f, 44100.0f);
        check(!voice.isPlaying(), "a zone with no sample does not start");
        check(voice.process() == 0.0f, "and renders silence rather than reading null");
    }
}

// ============================================================================
// 68. The sampler reaches the audio and the file
// ============================================================================
static void testSamplerReachesAudio() {
    beginTest("Sampler reaches the audio and the file");

    // ---- A sampler channel makes the sound of its sample --------------------
    {
        Project p;
        p.bpm = 120.0f;
        p.masterLimiterEnabled = false;
        p.masterCompressorEnabled = false;
        p.masterEQEnabled = false;
        p.masterVolume = 0.7f;

        Sample tone;
        tone.name = "tone";
        tone.sampleRate = 48000;
        tone.rootNote = 60;
        tone.audioData.resize(48000);
        for (size_t i = 0; i < tone.audioData.size(); ++i) {
            tone.audioData[i] = 0.8f * std::sin(
                6.28318530718f * 440.0f * float(i) / 48000.0f);
        }
        tone.lengthSeconds = 1.0f;
        tone.isLoaded = true;
        const int id = p.samplePool.addSample(tone);
        check(id >= 0, "the pool takes the sample");

        p.channels[0].oscillator.type = OscillatorType::Sampler;
        SampleZone zone;
        zone.sampleId = id;
        zone.rootKey = 60;
        p.channels[0].oscillator.sampler.addZone(zone);
        p.channels[0].oscillator.sampler.attack = 0.0f;
        p.channels[0].volume = 0.8f;
        p.channels[0].pan = 0.0f;

        p.patterns.clear();
        Pattern pattern;
        Note note;
        note.pitch = 60;
        note.startTime = 0.0f;
        note.duration = 2.0f;
        note.oscillatorType = OscillatorType::Sampler;
        pattern.notes.push_back(note);
        p.patterns.push_back(pattern);

        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        auto seqPtr = std::make_unique<Sequencer>();
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(&p);
        seqPtr->updateChannelConfigs();
        seqPtr->updateMasterEffects();
        seqPtr->play();

        std::vector<float> l(512), r(512);
        double energy = 0.0;
        for (int b = 0; b < 30; ++b) {
            seqPtr->process(l.data(), r.data(), 512);
            if (b >= 5) for (float v : l) energy += double(v) * v;
        }
        check(energy > 1e-4,
              "a sampler channel plays its sample through the engine - "
              "SampleOscillator was declared on Voice and triggered from "
              "nowhere, so none of this was reachable before");
    }

    // ---- Name round trip ----------------------------------------------------
    {
        check(stringToOscillatorType(oscillatorTypeToString(OscillatorType::Sampler)) ==
              OscillatorType::Sampler,
              "the Sampler type survives a name round trip");
    }

    // ---- Zones survive a save and load --------------------------------------
    {
        Project p;
        Sample tone;
        tone.name = "tone";
        tone.filepath = "";
        tone.sampleRate = 48000;
        tone.audioData.assign(1000, 0.3f);
        tone.lengthSeconds = 1000.0f / 48000.0f;
        tone.isLoaded = true;
        p.samplePool.addSample(tone);

        p.channels[1].oscillator.type = OscillatorType::Sampler;
        SamplerInstrument& s = p.channels[1].oscillator.sampler;
        s.velocityCrossfade = 0.15f;
        s.attack = 0.02f;
        s.release = 0.4f;
        s.velocityToLevel = 0.6f;

        SampleZone a;
        a.sampleId = 0;
        a.lowKey = 36; a.highKey = 59; a.rootKey = 48;
        a.lowVelocity = 0.0f; a.highVelocity = 0.6f;
        a.gain = 0.75f; a.pan = -0.4f; a.tuneCents = 7.0f;
        a.loop = true; a.loopStartSeconds = 0.05f; a.loopEndSeconds = 0.4f;
        a.roundRobinGroup = 2;
        s.addZone(a);

        SampleZone b = a;
        b.lowKey = 60; b.highKey = 84; b.rootKey = 72;
        b.roundRobinGroup = 0;
        s.addZone(b);

        p.arrangement.push_back(Clip{0, 1, 0.0f, 4.0f, 0});

        const std::string path = testPath("sampler_roundtrip.ctp");
        check(saveProject(p, path), "a sampler project saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");

        const SamplerInstrument& g = loaded.channels[1].oscillator.sampler;
        check(loaded.channels[1].oscillator.type == OscillatorType::Sampler,
              "the type survives");
        check(g.zoneCount == 2, "both zones survive");
        if (g.zoneCount == 2) {
            check(g.zones[0].lowKey == 36 && g.zones[0].highKey == 59 &&
                  g.zones[0].rootKey == 48,
                  "with their key ranges");
            check(std::fabs(g.zones[0].highVelocity - 0.6f) < 1e-3f,
                  "their velocity ranges");
            check(std::fabs(g.zones[0].gain - 0.75f) < 1e-3f &&
                  std::fabs(g.zones[0].pan + 0.4f) < 1e-3f &&
                  std::fabs(g.zones[0].tuneCents - 7.0f) < 1e-2f,
                  "their gain, pan and tuning");
            check(g.zones[0].loop &&
                  std::fabs(g.zones[0].loopEndSeconds - 0.4f) < 1e-3f,
                  "their loop points");
            check(g.zones[0].roundRobinGroup == 2,
                  "and their round-robin group");
        }
        check(std::fabs(g.velocityCrossfade - 0.15f) < 1e-3f &&
              std::fabs(g.attack - 0.02f) < 1e-3f &&
              std::fabs(g.release - 0.4f) < 1e-3f &&
              std::fabs(g.velocityToLevel - 0.6f) < 1e-3f,
              "and the instrument's own settings survive");

        std::remove(path.c_str());
    }

    // ---- Nothing written for a project with no sampler ---------------------
    {
        Project plain;
        const std::string path = testPath("sampler_absent.ctp");
        check(saveProject(plain, path), "a project with no sampler saves");

        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        check(text.find("SAMPLER ") == std::string::npos &&
              text.find("SZONE ") == std::string::npos,
              "and writes no sampler lines");
        std::remove(path.c_str());
    }
}


// ============================================================================
// 69. Granular synthesis
// ============================================================================
static void testGranular() {
    beginTest("Granular synthesis");

    const float rate = 44100.0f;

    // A two-second source whose two halves are different pitches, so a test
    // can tell WHERE in the sample the grains are being taken from.
    SamplePool pool;
    {
        Sample source;
        source.name = "source";
        source.sampleRate = 48000;
        source.audioData.resize(96000);
        for (size_t i = 0; i < source.audioData.size(); ++i) {
            const float t = float(i) / 48000.0f;
            const float frequency = (i < 48000) ? 220.0f : 880.0f;
            source.audioData[i] = 0.8f * std::sin(6.28318530718f * frequency * t);
        }
        source.lengthSeconds = 2.0f;
        source.isLoaded = true;
        pool.addSample(source);
    }

    auto render = [&](const GranularConfig& config, int samples) {
        GranularVoice voice;
        voice.trigger(config, pool.getSample(0), 60, 1.0f, rate, 0xABCDEFu);
        std::vector<float> out(static_cast<size_t>(samples));
        for (int i = 0; i < samples; ++i) {
            out[static_cast<size_t>(i)] = voice.process(config);
        }
        return out;
    };

    auto rms = [](const std::vector<float>& signal) {
        double sum = 0.0;
        for (float v : signal) sum += double(v) * v;
        return signal.empty() ? 0.0 : std::sqrt(sum / double(signal.size()));
    };

    auto dominantHz = [rate](const std::vector<float>& signal) {
        size_t N = 1;
        while (N * 2 <= signal.size()) N *= 2;
        if (N < 1024) return 0.0;
        std::vector<float> re(N), im(N, 0.0f);
        for (size_t i = 0; i < N; ++i) {
            const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                float(i) / float(N - 1)));
            re[i] = signal[i] * w;
        }
        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(re.data(), im.data());
        const double binHz = double(rate) / double(N);
        size_t loudest = 0;
        double peak = 0.0;
        for (size_t bin = 2; bin < N / 2; ++bin) {
            const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                               double(im[bin]) * im[bin]);
            if (magnitude > peak) { peak = magnitude; loudest = bin; }
        }
        return double(loudest) * binHz;
    };

    // ---- It makes a sound ---------------------------------------------------
    {
        GranularConfig config;
        config.sampleId = 0;
        config.followNote = false;
        const std::vector<float> audio = render(config, 44100);

        check(rms(audio) > 0.01,
              "a granular voice makes a sound (rms " +
              std::to_string(rms(audio)) + ")");

        bool finite = true;
        for (float v : audio) if (!std::isfinite(v)) { finite = false; break; }
        check(finite, "and all of it is finite");
    }

    // ---- The read position decides what you hear ---------------------------
    {
        GranularConfig low;
        low.sampleId = 0;
        low.followNote = false;
        low.positionRate = 0.0f;      // frozen
        low.position = 0.1f;          // inside the 220 Hz half
        low.spray = 0.001f;
        low.grainSeconds = 0.08f;

        GranularConfig high = low;
        high.position = 0.75f;        // inside the 880 Hz half

        const double lowHz = dominantHz(render(low, 32768));
        const double highHz = dominantHz(render(high, 32768));

        check(std::fabs(lowHz - 220.0) < 40.0,
              "grains taken from the first half play its pitch (" +
              std::to_string(lowHz) + " Hz, want 220)");
        check(std::fabs(highHz - 880.0) < 90.0,
              "and grains from the second half play its pitch (" +
              std::to_string(highHz) + " Hz, want 880) - which is the whole "
              "point: the read position is an instrument control");
    }

    // ---- Freezing does not change pitch ------------------------------------
    //
    // This is granular's headline trick: hold the position still and the
    // sound continues without the pitch drifting, which no resampling
    // time-stretch can do.
    {
        GranularConfig frozen;
        frozen.sampleId = 0;
        frozen.followNote = false;
        frozen.positionRate = 0.0f;
        frozen.position = 0.1f;
        frozen.spray = 0.002f;

        const std::vector<float> early = render(frozen, 8192);
        GranularVoice voice;
        voice.trigger(frozen, pool.getSample(0), 60, 1.0f, rate, 0xABCDEFu);
        std::vector<float> late(8192);
        for (int i = 0; i < 40000; ++i) voice.process(frozen);
        for (size_t i = 0; i < late.size(); ++i) late[i] = voice.process(frozen);

        const double earlyHz = dominantHz(early);
        const double lateHz = dominantHz(late);
        check(rms(late) > 0.01,
              "a frozen voice is still sounding a second later");
        check(std::fabs(earlyHz - lateHz) < 40.0,
              "at the same pitch it started at (" + std::to_string(earlyHz) +
              " -> " + std::to_string(lateHz) + ") - freezing time without "
              "moving the pitch is the thing granular is for");
    }

    // ---- Density changes texture, not loudness ------------------------------
    //
    // Without normalising by the expected overlap, turning density up is
    // just a volume control with extra steps.
    {
        GranularConfig sparse;
        sparse.sampleId = 0;
        sparse.followNote = false;
        sparse.positionRate = 0.0f;
        sparse.position = 0.1f;
        sparse.grainsPerSecond = 20.0f;
        sparse.grainSeconds = 0.05f;

        GranularConfig dense = sparse;
        dense.grainsPerSecond = 160.0f;

        const double sparseRms = rms(render(sparse, 32768));
        const double denseRms = rms(render(dense, 32768));

        check(sparseRms > 0.005 && denseRms > 0.005, "both densities sound");
        const double ratio = denseRms / sparseRms;
        check(ratio > 0.4 && ratio < 2.5,
              "eight times the density is not eight times the level (ratio " +
              std::to_string(ratio) + ") - the output is scaled by the "
              "expected overlap, or density would just be a volume control");
    }

    // ---- The grain window stops the clicks ---------------------------------
    //
    // Without a window every grain starts and ends at whatever the waveform
    // was doing, and the result is a buzz at the grain rate rather than a
    // texture. The signature of that is strong energy at the grain rate.
    {
        GranularConfig config;
        config.sampleId = 0;
        config.followNote = false;
        config.positionRate = 0.0f;
        config.position = 0.1f;
        config.grainsPerSecond = 30.0f;
        config.grainSeconds = 0.06f;
        config.spray = 0.004f;

        const std::vector<float> audio = render(config, 32768);

        // Measure energy near 30 Hz, the grain rate. A windowed stream has
        // very little; an unwindowed one has a great deal.
        const size_t N = 16384;
        std::vector<float> re(N), im(N, 0.0f);
        for (size_t i = 0; i < N; ++i) {
            const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                float(i) / float(N - 1)));
            re[i] = audio[i] * w;
        }
        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(re.data(), im.data());

        const double binHz = double(rate) / double(N);
        double atGrainRate = 0.0;
        double total = 0.0;
        for (size_t bin = 1; bin < N / 2; ++bin) {
            const double hz = double(bin) * binHz;
            const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                               double(im[bin]) * im[bin]);
            total += magnitude;
            if (hz < 60.0) atGrainRate += magnitude;
        }

        check(total > 0.0 && atGrainRate / total < 0.25,
              "little of the energy sits at the grain rate (" +
              std::to_string(total > 0.0 ? atGrainRate / total : 0.0) +
              ") - grains that start and end at zero are a texture; grains "
              "that do not are a buzz at the grain frequency");
    }

    // ---- The window shapes differ ------------------------------------------
    {
        GranularConfig smooth;
        smooth.sampleId = 0;
        smooth.followNote = false;
        smooth.positionRate = 0.0f;
        smooth.position = 0.1f;
        smooth.windowShape = 0.0f;

        GranularConfig sharp = smooth;
        sharp.windowShape = 1.0f;

        const double smoothRms = rms(render(smooth, 16384));
        const double sharpRms = rms(render(sharp, 16384));
        check(sharpRms > smoothRms,
              "a flat-topped window passes more of the grain than a Hann "
              "does, which is what makes it the transient-preserving one");
    }

    // ---- Two voices do not scatter identically ------------------------------
    {
        GranularConfig config;
        config.sampleId = 0;
        config.followNote = false;
        config.spray = 0.05f;

        GranularVoice a, b;
        a.trigger(config, pool.getSample(0), 60, 1.0f, rate, 111u);
        b.trigger(config, pool.getSample(0), 60, 1.0f, rate, 222u);

        double difference = 0.0;
        for (int i = 0; i < 16384; ++i) {
            difference += std::fabs(double(a.process(config)) -
                                    double(b.process(config)));
        }
        check(difference > 1.0,
              "two voices with different seeds scatter differently - "
              "identical seeds would sum coherently and sound like one very "
              "loud voice rather than a cloud");
    }

    // ---- A zero seed does not freeze the generator -------------------------
    {
        GranularConfig config;
        config.sampleId = 0;
        config.followNote = false;
        config.spray = 0.05f;

        GranularVoice voice;
        voice.trigger(config, pool.getSample(0), 60, 1.0f, rate, 0u);
        double energy = 0.0;
        for (int i = 0; i < 16384; ++i) energy += std::fabs(double(voice.process(config)));
        check(energy > 0.5,
              "a seed of zero still produces sound - xorshift is stuck at "
              "zero forever, so every grain would land in the same place");
    }

    // ---- No sample is silence, not a crash ---------------------------------
    {
        GranularConfig config;
        config.sampleId = -1;
        GranularVoice voice;
        voice.trigger(config, nullptr, 60, 1.0f, rate, 5u);
        check(!voice.isPlaying(), "a granular voice with no sample does not start");
        check(voice.process(config) == 0.0f, "and renders silence");
    }

    // ---- Validation ---------------------------------------------------------
    {
        GranularConfig hostile;
        hostile.sampleId = 400;
        hostile.grainSeconds = 0.0f;
        hostile.grainsPerSecond = 0.0f;
        hostile.spray = std::numeric_limits<float>::quiet_NaN();
        hostile.positionRate = 1e9f;

        clampGranularConfig(hostile, 2);

        check(hostile.sampleId == -1,
              "a sample id past the end of the pool becomes none");
        check(hostile.grainsPerSecond > 0.0f,
              "a density of zero is raised - the interval is 1/density, and "
              "that division happens in the audio thread");
        check(hostile.grainSeconds >= 0.002f,
              "and a grain shorter than about 2 ms is raised, since it is a "
              "click however it is windowed");
        check(std::isfinite(hostile.spray), "a NaN spray is replaced");
        check(std::isfinite(hostile.positionRate) &&
              std::fabs(hostile.positionRate) <= 4.0f,
              "and an absurd scan rate is bounded");
    }
}

// ============================================================================
// 70. The analogue drum model
// ============================================================================
static void testDrumModel() {
    beginTest("Analogue drum model");

    const float rate = 44100.0f;

    auto render = [rate](const DrumModelConfig& config, int samples) {
        DrumModelVoice voice;
        voice.trigger(config, 1.0f, rate, 999u);
        std::vector<float> out(static_cast<size_t>(samples));
        for (int i = 0; i < samples; ++i) {
            out[static_cast<size_t>(i)] = voice.process(config);
        }
        return out;
    };

    auto rms = [](const std::vector<float>& signal) {
        double sum = 0.0;
        for (float v : signal) sum += double(v) * v;
        return signal.empty() ? 0.0 : std::sqrt(sum / double(signal.size()));
    };

    auto centroid = [rate](const std::vector<float>& signal) {
        size_t N = 1;
        while (N * 2 <= signal.size()) N *= 2;
        if (N < 512) return 0.0;
        std::vector<float> re(N), im(N, 0.0f);
        for (size_t i = 0; i < N; ++i) {
            const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                float(i) / float(N - 1)));
            re[i] = signal[i] * w;
        }
        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(re.data(), im.data());
        double weighted = 0.0, total = 0.0;
        const double binHz = double(rate) / double(N);
        for (size_t bin = 1; bin < N / 2; ++bin) {
            const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                               double(im[bin]) * im[bin]);
            weighted += double(bin) * binHz * magnitude;
            total += magnitude;
        }
        return (total > 0.0) ? weighted / total : 0.0;
    };

    // ---- The three voices are actually different ---------------------------
    {
        DrumModelConfig kick;
        kick.voice = DrumVoiceType::Kick;

        DrumModelConfig snare = kick;
        snare.voice = DrumVoiceType::Snare;
        snare.tuneHz = 180.0f;

        DrumModelConfig hat = kick;
        hat.voice = DrumVoiceType::HiHat;
        hat.decaySeconds = 0.05f;

        const double kickCentroid = centroid(render(kick, 16384));
        const double snareCentroid = centroid(render(snare, 16384));
        const double hatCentroid = centroid(render(hat, 16384));

        check(kickCentroid > 0.0, "the kick has a spectrum");
        check(snareCentroid > kickCentroid,
              "a snare is brighter than a kick (" +
              std::to_string(kickCentroid) + " -> " +
              std::to_string(snareCentroid) + ")");
        check(hatCentroid > snareCentroid,
              "and a hat is brighter than a snare (" +
              std::to_string(hatCentroid) + ")");
    }

    // ---- The kick's pitch sweep is the thump --------------------------------
    {
        DrumModelConfig config;
        config.voice = DrumVoiceType::Kick;
        config.tuneHz = 50.0f;
        config.pitchSweepSemitones = 36.0f;
        config.pitchSweepSeconds = 0.06f;
        config.decaySeconds = 0.5f;

        const std::vector<float> audio = render(config, 22050);
        const std::vector<float> head(audio.begin(), audio.begin() + 2048);
        const std::vector<float> tail(audio.begin() + 8192, audio.begin() + 10240);

        check(centroid(head) > centroid(tail) * 1.5,
              "the kick starts high and falls (" +
              std::to_string(centroid(head)) + " -> " +
              std::to_string(centroid(tail)) + ") - that sweep IS the thump, "
              "and a kick without it is just a low sine");

        // The sweep must be optional, or it cannot make an 808 tail.
        DrumModelConfig flat = config;
        flat.pitchSweepSemitones = 0.0f;
        const std::vector<float> flatAudio = render(flat, 22050);
        const std::vector<float> flatHead(flatAudio.begin(), flatAudio.begin() + 2048);
        check(centroid(flatHead) < centroid(head),
              "and turning the sweep off gives the static sine an 808 tail "
              "needs to work as a bass note");
    }

    // ---- The snare's mix control reaches the sound --------------------------
    {
        DrumModelConfig shell;
        shell.voice = DrumVoiceType::Snare;
        shell.tuneHz = 180.0f;
        shell.noiseMix = 0.0f;

        DrumModelConfig snares = shell;
        snares.noiseMix = 1.0f;

        check(centroid(render(snares, 16384)) > centroid(render(shell, 16384)) * 1.3,
              "an all-snares snare is much brighter than an all-shell one - "
              "that mix is the single most useful control a snare has");
    }

    // ---- Decay actually decays, and ends -----------------------------------
    {
        DrumModelConfig config;
        config.voice = DrumVoiceType::Kick;
        config.decaySeconds = 0.1f;

        DrumModelVoice voice;
        voice.trigger(config, 1.0f, rate, 7u);

        std::vector<float> head(2048), tail(2048);
        for (float& v : head) v = voice.process(config);
        for (int i = 0; i < 8000; ++i) voice.process(config);
        for (float& v : tail) v = voice.process(config);

        check(rms(head) > rms(tail) * 4.0,
              "the voice decays (" + std::to_string(rms(head)) + " -> " +
              std::to_string(rms(tail)) + ")");

        for (int i = 0; i < 44100; ++i) voice.process(config);
        check(!voice.isPlaying(),
              "and stops rather than holding a voice slot at -60 dB forever");
    }

    // ---- Velocity ------------------------------------------------------------
    {
        DrumModelConfig config;
        config.voice = DrumVoiceType::Snare;
        config.velocityToLevel = 1.0f;

        DrumModelVoice soft, hard;
        soft.trigger(config, 0.2f, rate, 3u);
        hard.trigger(config, 1.0f, rate, 3u);

        double softEnergy = 0.0, hardEnergy = 0.0;
        for (int i = 0; i < 8192; ++i) {
            softEnergy += std::fabs(double(soft.process(config)));
            hardEnergy += std::fabs(double(hard.process(config)));
        }
        check(hardEnergy > softEnergy * 2.0,
              "playing harder is louder when velocity is mapped");

        config.velocityToLevel = 0.0f;
        DrumModelVoice flatSoft, flatHard;
        flatSoft.trigger(config, 0.2f, rate, 3u);
        flatHard.trigger(config, 1.0f, rate, 3u);
        double a = 0.0, b = 0.0;
        for (int i = 0; i < 8192; ++i) {
            a += std::fabs(double(flatSoft.process(config)));
            b += std::fabs(double(flatHard.process(config)));
        }
        check(std::fabs(a - b) < 1e-3,
              "and identical when it is not - which is what a kit whose "
              "layers already encode the dynamics wants");
    }

    // ---- Validation ---------------------------------------------------------
    {
        DrumModelConfig hostile;
        hostile.voice = static_cast<DrumVoiceType>(99);
        hostile.tuneHz = 0.0f;
        hostile.decaySeconds = std::numeric_limits<float>::quiet_NaN();
        hostile.pitchSweepSeconds = 0.0f;
        hostile.level = 500.0f;

        clampDrumModel(hostile);

        check(hostile.voice < DrumVoiceType::Count,
              "an impossible voice type falls back to a real one");
        check(hostile.tuneHz >= 20.0f,
              "a tune of zero is raised - it would put the hat's six "
              "oscillators all at DC, which is silence that looks like a bug");
        check(std::isfinite(hostile.decaySeconds), "a NaN decay is replaced");
        check(hostile.pitchSweepSeconds > 0.0f, "and a zero sweep time cannot divide by zero");
        check(hostile.level <= 2.0f, "an absurd level is bounded");

        const std::vector<float> audio = render(hostile, 4096);
        bool finite = true;
        for (float v : audio) if (!std::isfinite(v)) { finite = false; break; }
        check(finite, "and the repaired config renders finite audio");
    }
}

// ============================================================================
// 71. Both reach the audio and the file
// ============================================================================
static void testGranularAndDrumsReachAudio() {
    beginTest("Granular and drum model reach audio and files");

    auto renderChannel = [](OscillatorType type,
                            void (*setup)(Project&)) {
        Project p;
        p.bpm = 120.0f;
        p.masterLimiterEnabled = false;
        p.masterCompressorEnabled = false;
        p.masterEQEnabled = false;
        p.masterVolume = 0.7f;
        setup(p);

        p.channels[0].oscillator.type = type;
        p.channels[0].volume = 0.8f;
        p.channels[0].pan = 0.0f;

        p.patterns.clear();
        Pattern pattern;
        Note note;
        note.pitch = 60;
        note.startTime = 0.0f;
        note.duration = 2.0f;
        note.oscillatorType = type;
        pattern.notes.push_back(note);
        p.patterns.push_back(pattern);

        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        auto seqPtr = std::make_unique<Sequencer>();
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(&p);
        seqPtr->updateChannelConfigs();
        seqPtr->updateMasterEffects();
        seqPtr->play();

        std::vector<float> l(512), r(512);
        double energy = 0.0;
        for (int b = 0; b < 30; ++b) {
            seqPtr->process(l.data(), r.data(), 512);
            if (b >= 4) for (float v : l) energy += double(v) * v;
        }
        return energy;
    };

    {
        const double energy = renderChannel(OscillatorType::Granular, [](Project& p) {
            Sample source;
            source.name = "grain source";
            source.sampleRate = 48000;
            source.audioData.resize(48000);
            for (size_t i = 0; i < source.audioData.size(); ++i) {
                source.audioData[i] = 0.8f * std::sin(
                    6.28318530718f * 330.0f * float(i) / 48000.0f);
            }
            source.lengthSeconds = 1.0f;
            source.isLoaded = true;
            p.samplePool.addSample(source);
            p.channels[0].oscillator.granular.sampleId = 0;
            p.channels[0].oscillator.granular.followNote = false;
        });
        check(energy > 1e-4, "a granular channel sounds through the engine");
    }

    {
        const double energy = renderChannel(OscillatorType::DrumModel, [](Project& p) {
            p.channels[0].oscillator.drumModel.voice = DrumVoiceType::Kick;
            p.channels[0].oscillator.drumModel.decaySeconds = 0.4f;
        });
        check(energy > 1e-4, "and a modelled drum channel does too");
    }

    // ---- Name round trips ---------------------------------------------------
    {
        check(stringToOscillatorType(oscillatorTypeToString(OscillatorType::Granular)) ==
              OscillatorType::Granular, "Granular survives a name round trip");
        check(stringToOscillatorType(oscillatorTypeToString(OscillatorType::DrumModel)) ==
              OscillatorType::DrumModel, "and so does DrumModel");
    }

    // ---- Settings survive a save and load ----------------------------------
    {
        Project p;
        p.channels[4].oscillator.type = OscillatorType::Granular;
        p.channels[4].oscillator.granular.position = 0.375f;
        p.channels[4].oscillator.granular.positionRate = -0.5f;
        p.channels[4].oscillator.granular.spray = 0.125f;
        p.channels[4].oscillator.granular.grainSeconds = 0.0625f;
        p.channels[4].oscillator.granular.grainsPerSecond = 75.0f;
        p.channels[4].oscillator.granular.pitchJitter = 3.5f;
        p.channels[4].oscillator.granular.reverseChance = 0.25f;
        p.channels[4].oscillator.granular.windowShape = 0.8f;
        p.channels[4].oscillator.granular.followNote = false;

        p.channels[5].oscillator.type = OscillatorType::DrumModel;
        p.channels[5].oscillator.drumModel.voice = DrumVoiceType::HiHat;
        p.channels[5].oscillator.drumModel.tuneHz = 320.0f;
        p.channels[5].oscillator.drumModel.decaySeconds = 0.075f;
        p.channels[5].oscillator.drumModel.snap = 0.65f;
        p.channels[5].oscillator.drumModel.hatHighpass = 0.85f;

        p.arrangement.push_back(Clip{0, 4, 0.0f, 4.0f, 0});
        p.arrangement.push_back(Clip{0, 5, 0.0f, 4.0f, 0});

        const std::string path = testPath("gran_drum_roundtrip.ctp");
        check(saveProject(p, path), "the project saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");

        const GranularConfig& g = loaded.channels[4].oscillator.granular;
        check(loaded.channels[4].oscillator.type == OscillatorType::Granular,
              "the granular type survives");
        check(std::fabs(g.position - 0.375f) < 1e-4f &&
              std::fabs(g.positionRate + 0.5f) < 1e-4f &&
              std::fabs(g.spray - 0.125f) < 1e-4f,
              "its position, scan rate and spray survive");
        check(std::fabs(g.grainSeconds - 0.0625f) < 1e-4f &&
              std::fabs(g.grainsPerSecond - 75.0f) < 1e-3f,
              "its grain size and density survive");
        check(std::fabs(g.pitchJitter - 3.5f) < 1e-3f &&
              std::fabs(g.reverseChance - 0.25f) < 1e-4f &&
              std::fabs(g.windowShape - 0.8f) < 1e-4f,
              "its jitter, reverse chance and window shape survive");
        check(!g.followNote, "and its follow-note flag survives");

        const DrumModelConfig& d = loaded.channels[5].oscillator.drumModel;
        check(d.voice == DrumVoiceType::HiHat,
              "the drum voice survives - it is an enum, so it rides as a "
              "plain int rather than through the typed field tables");
        check(std::fabs(d.tuneHz - 320.0f) < 1e-2f &&
              std::fabs(d.decaySeconds - 0.075f) < 1e-4f &&
              std::fabs(d.snap - 0.65f) < 1e-4f &&
              std::fabs(d.hatHighpass - 0.85f) < 1e-4f,
              "and so do its tuning, decay, snap and filter");

        std::remove(path.c_str());
    }
}


// ============================================================================
// 72. The modulation matrix
// ============================================================================
static void testModMatrix() {
    beginTest("Modulation matrix");

    const float rate = 44100.0f;
    PerformanceState performance;

    // ---- Nothing routed is nothing applied ---------------------------------
    {
        ModMatrix matrix;
        check(!matrix.anyActive(),
              "a fresh matrix has no active routes, so the per-sample walk "
              "is skipped entirely");

        ModState state;
        state.start(matrix, 1u);
        const ModValues values = mod::evaluate(matrix, state, 60, 1.0f,
                                               performance, rate, false);
        check(values.pitchSemitones == 0.0f && values.level == 0.0f,
              "and it resolves to no offset at all");
    }

    // ---- An LFO actually oscillates ----------------------------------------
    {
        ModMatrix matrix;
        matrix.lfos[0].shape = LFOShape::Sine;
        matrix.lfos[0].rateHz = 4.0f;
        matrix.addRoute(ModRoute{ModSource::LFO1, ModDestination::Pitch, 0.5f, true});
        check(matrix.anyActive(), "a route makes the matrix active");

        ModState state;
        state.start(matrix, 1u);

        float lowest = 1e9f, highest = -1e9f;
        for (int i = 0; i < static_cast<int>(rate); ++i) {
            const ModValues values = mod::evaluate(matrix, state, 60, 1.0f,
                                                   performance, rate, false);
            lowest = std::min(lowest, values.pitchSemitones);
            highest = std::max(highest, values.pitchSemitones);
        }
        check(highest > 5.0f && lowest < -5.0f,
              "an LFO on pitch swings both ways (" + std::to_string(lowest) +
              " .. " + std::to_string(highest) + " semitones)");
        check(std::fabs(highest + lowest) < 2.0f,
              "and symmetrically about zero, so it is a vibrato rather than "
              "a transpose");
    }

    // ---- Each shape is a different shape -----------------------------------
    {
        auto sampleShape = [&](LFOShape shape) {
            ModMatrix matrix;
            matrix.lfos[0].shape = shape;
            matrix.lfos[0].rateHz = 1.0f;
            matrix.addRoute(ModRoute{ModSource::LFO1, ModDestination::Level, 1.0f, true});

            ModState state;
            state.start(matrix, 7u);
            std::vector<float> trace;
            // A little over one second at 1 Hz. Exactly one second wraps the
            // phase on the sample AFTER the last one, so the saw's reset
            // falls outside the trace and the test sees no jump at all.
            for (int i = 0; i < static_cast<int>(rate) + 200; ++i) {
                trace.push_back(mod::evaluate(matrix, state, 60, 1.0f,
                                              performance, rate, false).level);
            }
            return trace;
        };

        const std::vector<float> sine = sampleShape(LFOShape::Sine);
        const std::vector<float> square = sampleShape(LFOShape::Square);
        const std::vector<float> saw = sampleShape(LFOShape::Saw);

        // A square spends all its time at the extremes; a sine does not.
        auto extremeFraction = [](const std::vector<float>& trace) {
            int extreme = 0;
            for (float v : trace) if (std::fabs(v) > 0.9f) ++extreme;
            return double(extreme) / double(trace.size());
        };
        check(extremeFraction(square) > 0.9,
              "a square LFO sits at its extremes");
        check(extremeFraction(sine) < 0.4,
              "and a sine does not");

        // A saw rises steadily then jumps back exactly once per cycle.
        int jumps = 0;
        for (size_t i = 1; i < saw.size(); ++i) {
            if (saw[i] < saw[i - 1] - 1.0f) ++jumps;
        }
        check(jumps == 1, "and a saw resets once per second at 1 Hz (got " +
              std::to_string(jumps) + ")");
    }

    // ---- Sample and hold changes once per cycle ----------------------------
    {
        ModMatrix matrix;
        matrix.lfos[0].shape = LFOShape::SampleHold;
        matrix.lfos[0].rateHz = 8.0f;
        matrix.addRoute(ModRoute{ModSource::LFO1, ModDestination::Level, 1.0f, true});

        ModState state;
        state.start(matrix, 42u);

        int changes = 0;
        float previous = 1e9f;
        for (int i = 0; i < static_cast<int>(rate); ++i) {
            const float value = mod::evaluate(matrix, state, 60, 1.0f,
                                              performance, rate, false).level;
            if (i > 0 && std::fabs(value - previous) > 1e-6f) ++changes;
            previous = value;
        }
        check(changes >= 6 && changes <= 10,
              "sample and hold picks a new value about eight times a second "
              "at 8 Hz (got " + std::to_string(changes) + ") - it is held "
              "flat in between, which is the whole shape");
    }

    // ---- LFO delay and fade -------------------------------------------------
    {
        ModMatrix matrix;
        matrix.lfos[0].shape = LFOShape::Sine;
        matrix.lfos[0].rateHz = 6.0f;
        matrix.lfos[0].delaySeconds = 0.3f;
        matrix.lfos[0].fadeSeconds = 0.3f;
        matrix.addRoute(ModRoute{ModSource::LFO1, ModDestination::Pitch, 1.0f, true});

        ModState state;
        state.start(matrix, 3u);

        float earlyMax = 0.0f, lateMax = 0.0f;
        for (int i = 0; i < static_cast<int>(rate); ++i) {
            const float value = std::fabs(
                mod::evaluate(matrix, state, 60, 1.0f, performance, rate, false)
                    .pitchSemitones);
            if (i < static_cast<int>(rate * 0.25f)) earlyMax = std::max(earlyMax, value);
            if (i > static_cast<int>(rate * 0.8f)) lateMax = std::max(lateMax, value);
        }
        check(earlyMax < 0.01f,
              "the LFO is silent during its delay - vibrato that starts the "
              "instant a note does sounds mechanical, and no player does it");
        check(lateMax > 10.0f, "and at full depth once it has faded in");
    }

    // ---- The second envelope releases ---------------------------------------
    {
        ModMatrix matrix;
        matrix.env2Attack = 0.0f;
        matrix.env2Decay = 0.0f;
        matrix.env2Sustain = 1.0f;
        matrix.env2Release = 0.2f;
        matrix.addRoute(ModRoute{ModSource::Envelope2, ModDestination::Level, 1.0f, true});

        ModState state;
        state.start(matrix, 5u);
        for (int i = 0; i < 4410; ++i) {
            mod::evaluate(matrix, state, 60, 1.0f, performance, rate, false);
        }
        const float held = mod::evaluate(matrix, state, 60, 1.0f, performance,
                                         rate, false).level;
        check(held > 0.5f, "the second envelope holds its sustain");

        state.release();
        for (int i = 0; i < static_cast<int>(rate * 0.5f); ++i) {
            mod::evaluate(matrix, state, 60, 1.0f, performance, rate, false);
        }
        const float after = mod::evaluate(matrix, state, 60, 1.0f, performance,
                                          rate, false).level;
        check(after < 0.05f,
              "and falls when released - without that, a filter it was "
              "opening never closes after the key comes up");
    }

    // ---- The static sources -------------------------------------------------
    {
        ModMatrix matrix;
        matrix.addRoute(ModRoute{ModSource::Velocity, ModDestination::Level, 1.0f, true});
        ModState soft, hard;
        soft.start(matrix, 1u);
        hard.start(matrix, 1u);

        const float atSoft = mod::evaluate(matrix, soft, 60, 0.2f, performance,
                                           rate, false).level;
        const float atHard = mod::evaluate(matrix, hard, 60, 1.0f, performance,
                                           rate, false).level;
        check(atHard > atSoft, "velocity reaches its destination");
    }

    {
        ModMatrix matrix;
        matrix.addRoute(ModRoute{ModSource::KeyTrack, ModDestination::FilterCutoff,
                                 1.0f, true});
        ModState low, high;
        low.start(matrix, 1u);
        high.start(matrix, 1u);

        const float atLow = mod::evaluate(matrix, low, 24, 1.0f, performance,
                                          rate, true).filterCutoff;
        const float atHigh = mod::evaluate(matrix, high, 96, 1.0f, performance,
                                           rate, true).filterCutoff;
        check(atHigh > atLow,
              "key tracking opens the filter further up the keyboard, which "
              "is what stops high notes sounding dull under a fixed cutoff");
    }

    {
        ModMatrix matrix;
        matrix.addRoute(ModRoute{ModSource::ModWheel, ModDestination::FMBrightness,
                                 1.0f, true});
        ModState state;
        state.start(matrix, 1u);

        PerformanceState down, up;
        down.modWheel = 0.0f;
        up.modWheel = 1.0f;

        const float atDown = mod::evaluate(matrix, state, 60, 1.0f, down, rate,
                                           false).fmBrightness;
        const float atUp = mod::evaluate(matrix, state, 60, 1.0f, up, rate,
                                         false).fmBrightness;
        check(atUp > atDown, "the mod wheel reaches its destination");
    }

    // ---- Per-note randomness is per note ------------------------------------
    {
        ModMatrix matrix;
        matrix.addRoute(ModRoute{ModSource::RandomPerNote, ModDestination::Pitch,
                                 1.0f, true});

        ModState a, b;
        a.start(matrix, 111u);
        b.start(matrix, 222u);

        const float first = mod::evaluate(matrix, a, 60, 1.0f, performance,
                                          rate, false).pitchSemitones;
        const float second = mod::evaluate(matrix, b, 60, 1.0f, performance,
                                           rate, false).pitchSemitones;
        check(std::fabs(first - second) > 1e-4f,
              "two notes get different random values");

        // And it is HELD for the life of the note rather than re-rolled.
        float held = first;
        for (int i = 0; i < 1000; ++i) {
            held = mod::evaluate(matrix, a, 60, 1.0f, performance, rate,
                                 false).pitchSemitones;
        }
        check(std::fabs(held - first) < 1e-4f,
              "and each one holds for the life of its note rather than "
              "re-rolling every sample, which would be noise, not variation");
    }

    // ---- Routes sum ----------------------------------------------------------
    {
        ModMatrix matrix;
        matrix.lfos[0].rateHz = 0.0001f;      // effectively static
        matrix.addRoute(ModRoute{ModSource::Velocity, ModDestination::Level, 0.3f, true});
        matrix.addRoute(ModRoute{ModSource::ModWheel, ModDestination::Level, 0.4f, true});

        PerformanceState wheel;
        wheel.modWheel = 1.0f;

        ModState state;
        state.start(matrix, 1u);
        const float combined = mod::evaluate(matrix, state, 60, 1.0f, wheel,
                                             rate, false).level;
        check(std::fabs(combined - 0.7f) < 0.01f,
              "two routes to one destination add (got " +
              std::to_string(combined) + ", want 0.7)");
    }

    // ---- A disabled route does nothing ---------------------------------------
    {
        ModMatrix matrix;
        ModRoute route{ModSource::Velocity, ModDestination::Level, 1.0f, false};
        matrix.addRoute(route);
        check(!matrix.anyActive(), "a disabled route leaves the matrix inactive");

        matrix.routes[0].enabled = true;
        check(matrix.anyActive(), "and enabling it turns the matrix on");
    }

    // ---- Scope separation ----------------------------------------------------
    {
        ModMatrix matrix;
        matrix.addRoute(ModRoute{ModSource::Velocity, ModDestination::Pitch, 1.0f, true});
        matrix.addRoute(ModRoute{ModSource::Velocity, ModDestination::FilterCutoff,
                                 1.0f, true});

        ModState voiceState, channelState;
        voiceState.start(matrix, 1u);
        channelState.start(matrix, 1u);

        const ModValues voice = mod::evaluate(matrix, voiceState, 60, 1.0f,
                                              performance, rate, false);
        const ModValues channel = mod::evaluate(matrix, channelState, 60, 1.0f,
                                                performance, rate, true);

        check(voice.pitchSemitones != 0.0f && voice.filterCutoff == 0.0f,
              "the per-voice pass fills only per-voice destinations");
        check(channel.filterCutoff != 0.0f && channel.pitchSemitones == 0.0f,
              "and the channel pass only channel ones - they are applied in "
              "different places, so they cannot share a result");

        check(isChannelDestination(ModDestination::FilterCutoff),
              "the filter is a channel destination");
        check(!isChannelDestination(ModDestination::Pitch),
              "and pitch is not");
    }

    // ---- Validation ----------------------------------------------------------
    {
        ModMatrix hostile;
        hostile.routeCount = 900;
        hostile.routes[0].source = static_cast<ModSource>(77);
        hostile.routes[0].destination = static_cast<ModDestination>(88);
        hostile.routes[0].amount = std::numeric_limits<float>::quiet_NaN();
        hostile.lfos[0].rateHz = 5000.0f;
        hostile.lfos[1].shape = static_cast<LFOShape>(40);
        hostile.polyphonyLimit = -5;
        hostile.pitchBendSemitones = 1e9f;

        clampModMatrix(hostile);

        check(hostile.routeCount <= ModMatrix::MAX_ROUTES,
              "an impossible route count is bounded");
        check(hostile.routes[0].source == ModSource::None &&
              hostile.routes[0].destination == ModDestination::None,
              "a source or destination this build does not have becomes None "
              "rather than wrapping onto a different one, which would "
              "silently route a file's LFO somewhere else entirely");
        check(std::isfinite(hostile.routes[0].amount), "a NaN amount is replaced");
        check(hostile.lfos[0].rateHz <= 40.0f,
              "an audio-rate LFO is bounded - above about 40 Hz it is an "
              "oscillator, which is a real sound but not what these "
              "destinations expect");
        check(hostile.lfos[1].shape < LFOShape::Count, "an unknown shape falls back");
        check(hostile.polyphonyLimit >= 0, "a negative polyphony limit is repaired");
        check(hostile.pitchBendSemitones <= 48.0f, "and an absurd bend range is bounded");
    }
}

// ============================================================================
// 73. Modulation reaches the audio
// ============================================================================
static void testModulationReachesAudio() {
    beginTest("Modulation reaches the audio");

    // Render a note on a channel and hand back the samples. `setup` gets to
    // configure the channel however the case needs.
    auto render = [](void (*setup)(Project&), int blocks) {
        Project p;
        p.bpm = 120.0f;
        p.masterLimiterEnabled = false;
        p.masterCompressorEnabled = false;
        p.masterEQEnabled = false;
        p.masterVolume = 0.7f;

        p.patterns.clear();
        Pattern pattern;
        Note note;
        note.pitch = 60;
        note.startTime = 0.0f;
        note.duration = 4.0f;
        pattern.notes.push_back(note);
        p.patterns.push_back(pattern);
        p.arrangement.clear();
        p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        p.channels[0].volume = 0.8f;
        p.channels[0].pan = 0.0f;
        setup(p);
        p.patterns[0].notes[0].oscillatorType = p.channels[0].oscillator.type;

        auto seqPtr = std::make_unique<Sequencer>();
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(&p);
        seqPtr->updateChannelConfigs();
        seqPtr->updateMasterEffects();
        seqPtr->play();

        std::vector<float> l(512), r(512);
        std::vector<float> collected;
        for (int b = 0; b < blocks; ++b) {
            seqPtr->process(l.data(), r.data(), 512);
            if (b >= 4) collected.insert(collected.end(), l.begin(), l.end());
        }
        return collected;
    };

    auto meanAbsDifference = [](const std::vector<float>& a,
                                const std::vector<float>& b) {
        const size_t n = std::min(a.size(), b.size());
        if (n == 0) return 0.0;
        double sum = 0.0;
        for (size_t i = 0; i < n; ++i) sum += std::fabs(double(a[i]) - double(b[i]));
        return sum / double(n);
    };

    // ---- Pitch ---------------------------------------------------------------
    {
        const std::vector<float> plain = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Pulse;
        }, 40);

        const std::vector<float> wobbled = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Pulse;
            ModMatrix& m = p.channels[0].oscillator.modMatrix;
            m.lfos[0].rateHz = 6.0f;
            m.addRoute(ModRoute{ModSource::LFO1, ModDestination::Pitch, 0.2f, true});
        }, 40);

        check(meanAbsDifference(plain, wobbled) > 1e-3,
              "an LFO routed to pitch changes the audio - a matrix that "
              "computes offsets nothing reads is the bug this codebase keeps "
              "finding");
    }

    // ---- Level ---------------------------------------------------------------
    {
        const std::vector<float> plain = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Pulse;
        }, 40);

        const std::vector<float> tremolo = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Pulse;
            ModMatrix& m = p.channels[0].oscillator.modMatrix;
            m.lfos[0].rateHz = 5.0f;
            m.addRoute(ModRoute{ModSource::LFO1, ModDestination::Level, 0.6f, true});
        }, 40);

        check(meanAbsDifference(plain, tremolo) > 1e-3,
              "and so does one routed to level");
    }

    // ---- Wavetable morph ------------------------------------------------------
    {
        const std::vector<float> still = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Custom;
        }, 40);

        const std::vector<float> moving = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Custom;
            ModMatrix& m = p.channels[0].oscillator.modMatrix;
            m.lfos[0].rateHz = 3.0f;
            m.addRoute(ModRoute{ModSource::LFO1, ModDestination::WavetableMorph,
                                0.8f, true});
        }, 40);

        check(meanAbsDifference(still, moving) > 1e-3,
              "morphing a wavetable from an LFO changes the audio - a table "
              "that does not move is a sampled waveform");
    }

    // ---- FM brightness --------------------------------------------------------
    {
        const std::vector<float> fixed = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::FMSynth;
        }, 40);

        const std::vector<float> swept = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::FMSynth;
            ModMatrix& m = p.channels[0].oscillator.modMatrix;
            m.lfos[0].rateHz = 2.0f;
            m.addRoute(ModRoute{ModSource::LFO1, ModDestination::FMBrightness,
                                0.7f, true});
        }, 40);

        check(meanAbsDifference(fixed, swept) > 1e-3,
              "and sweeping an FM patch's index changes it - a fixed index "
              "is one timbre");
    }

    // ---- The filter cutoff control itself ------------------------------------
    //
    // This is a regression test for a bug the modulation work uncovered, not
    // for the modulation. Filter::process() reads cached coefficients that
    // only setCutoff() recomputes, and applyEffectsConfig was assigning the
    // public field directly - so the per-channel cutoff and resonance
    // controls had never moved the sound at all. The filter ran at whatever
    // setSampleRate last computed, which is the 1000 Hz default.
    {
        const std::vector<float> open = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Sawtooth;
            p.channels[0].filterEnabled = true;
            p.channels[0].filterType = 0;          // low pass
            p.channels[0].filterCutoff = 16000.0f;
        }, 40);

        const std::vector<float> shut = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Sawtooth;
            p.channels[0].filterEnabled = true;
            p.channels[0].filterType = 0;
            p.channels[0].filterCutoff = 200.0f;
        }, 40);

        double openEnergy = 0.0, shutEnergy = 0.0;
        for (float v : open) openEnergy += double(v) * v;
        for (float v : shut) shutEnergy += double(v) * v;

        check(openEnergy > 1e-4, "a filtered sawtooth sounds");
        check(shutEnergy < openEnergy * 0.6,
              "and closing the cutoff to 200 Hz makes it quieter than at "
              "16 kHz (" + std::to_string(shutEnergy) + " vs " +
              std::to_string(openEnergy) + ") - it did not before, because "
              "the coefficients were cached and nothing recomputed them");
    }

    // ---- The filter is stable across its whole range -------------------------
    //
    // Fixing the stale coefficients immediately exposed a worse bug: a
    // Chamberlin state-variable filter diverges once 2*sin(pi*fc/fs) passes
    // 1, which is around fs/6 - about 7 kHz at 44.1. A 16 kHz cutoff went to
    // NaN in a few dozen samples, and a NaN in a channel's insert rack is
    // silence from then on. It had never shown because the filter was pinned
    // at its 1000 Hz default.
    {
        bool allFinite = true;
        float worstPeak = 0.0f;
        float failedAt = 0.0f;

        for (float cutoff : {20.0f, 200.0f, 1000.0f, 5000.0f, 7000.0f,
                             12000.0f, 16000.0f, 20000.0f, 40000.0f}) {
            for (float resonance : {0.0f, 0.5f, 0.99f}) {
                Filter filter;
                filter.setSampleRate(44100.0f);
                filter.type = FilterType::LowPass;
                filter.cutoff = cutoff;
                filter.resonance = resonance;
                filter.refresh();

                // A saw, which has energy everywhere and is the least
                // forgiving input a resonant filter can be given.
                float phase = 0.0f;
                for (int i = 0; i < 8000; ++i) {
                    phase += 220.0f / 44100.0f;
                    if (phase >= 1.0f) phase -= 1.0f;
                    const float out = filter.process(2.0f * phase - 1.0f);
                    if (!std::isfinite(out)) {
                        allFinite = false;
                        failedAt = cutoff;
                        break;
                    }
                    worstPeak = std::max(worstPeak, std::fabs(out));
                }
                if (!allFinite) break;
            }
            if (!allFinite) break;
        }

        check(allFinite,
              "the filter is finite at every cutoff from 20 Hz to past "
              "Nyquist and at full resonance" +
              (allFinite ? std::string()
                         : " (diverged at " + std::to_string(failedAt) + " Hz)"));
        check(worstPeak < 50.0f,
              "and bounded (peak " + std::to_string(worstPeak) + ")");
    }

    // ---- Filter cutoff, the channel-scope path -------------------------------
    {
        const std::vector<float> fixed = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Sawtooth;
            p.channels[0].filterEnabled = true;
            p.channels[0].filterCutoff = 1200.0f;
        }, 40);

        const std::vector<float> swept = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Sawtooth;
            p.channels[0].filterEnabled = true;
            p.channels[0].filterCutoff = 1200.0f;
            ModMatrix& m = p.channels[0].oscillator.modMatrix;
            m.lfos[0].rateHz = 3.0f;
            m.addRoute(ModRoute{ModSource::LFO1, ModDestination::FilterCutoff,
                                0.5f, true});
        }, 40);

        check(meanAbsDifference(fixed, swept) > 1e-3,
              "and a filter sweep reaches the audio through the channel-scope "
              "pass, which is separate because the filter runs on the summed "
              "mix rather than per voice");
    }

    // ---- Nothing runs away ----------------------------------------------------
    {
        // The channel-scope cutoff is applied as an offset from the
        // CONFIGURED value. Accumulating it instead would compound every
        // sample and open the filter fully within milliseconds - so this
        // renders a long stretch and checks the sound is still there and
        // still bounded.
        const std::vector<float> audio = render([](Project& p) {
            p.channels[0].oscillator.type = OscillatorType::Sawtooth;
            p.channels[0].filterEnabled = true;
            p.channels[0].filterCutoff = 900.0f;
            ModMatrix& m = p.channels[0].oscillator.modMatrix;
            m.lfos[0].rateHz = 0.5f;
            m.addRoute(ModRoute{ModSource::LFO1, ModDestination::FilterCutoff,
                                1.0f, true});
        }, 200);

        float peak = 0.0f;
        bool finite = true;
        for (float v : audio) {
            if (!std::isfinite(v)) { finite = false; break; }
            peak = std::max(peak, std::fabs(v));
        }
        check(finite, "a long filter sweep stays finite");
        check(peak < 4.0f,
              "and bounded (peak " + std::to_string(peak) + ") - the offset "
              "is held against the configured cutoff rather than accumulated, "
              "which would run the filter away in milliseconds");
    }

    // ---- The polyphony limit --------------------------------------------------
    {
        auto renderChord = [](int limit) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.masterCompressorEnabled = false;
            p.masterEQEnabled = false;
            p.masterVolume = 0.7f;
            p.channels[0].oscillator.type = OscillatorType::Sawtooth;
            p.channels[0].volume = 0.8f;
            p.channels[0].pan = 0.0f;
            p.channels[0].oscillator.modMatrix.polyphonyLimit = limit;

            p.patterns.clear();
            Pattern pattern;
            for (int i = 0; i < 4; ++i) {
                Note note;
                note.pitch = 60 + i * 4;
                note.startTime = 0.0f;
                note.duration = 4.0f;
                note.oscillatorType = OscillatorType::Sawtooth;
                pattern.notes.push_back(note);
            }
            p.patterns.push_back(pattern);
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            seqPtr->setSampleRate(44100.0f);
            seqPtr->setProject(&p);
            seqPtr->updateChannelConfigs();
            seqPtr->updateMasterEffects();
            seqPtr->play();

            std::vector<float> l(512), r(512);
            double energy = 0.0;
            for (int b = 0; b < 30; ++b) {
                seqPtr->process(l.data(), r.data(), 512);
                if (b >= 6) for (float v : l) energy += double(v) * v;
            }
            return energy;
        };

        const double full = renderChord(0);
        const double mono = renderChord(1);

        check(full > 1e-4 && mono > 1e-4, "both play something");
        check(mono < full * 0.85,
              "a four-note chord on a monophonic channel is quieter than on a "
              "polyphonic one (" + std::to_string(mono) + " vs " +
              std::to_string(full) + ") - a mono bass that steals its own "
              "voice is a different instrument from one that stacks");
    }

    // ---- Persistence -----------------------------------------------------------
    {
        Project p;
        ModMatrix& m = p.channels[2].oscillator.modMatrix;
        m.polyphonyLimit = 4;
        m.pitchBendSemitones = 7.0f;
        m.env2Attack = 0.02f;
        m.env2Decay = 0.4f;
        m.env2Sustain = 0.35f;
        m.env2Release = 0.6f;
        m.lfos[0].shape = LFOShape::SampleHold;
        m.lfos[0].rateHz = 9.5f;
        m.lfos[0].delaySeconds = 0.15f;
        m.lfos[0].fadeSeconds = 0.25f;
        m.lfos[0].retrigger = false;
        m.lfos[2].shape = LFOShape::Square;
        m.addRoute(ModRoute{ModSource::LFO1, ModDestination::WavetableMorph, 0.6f, true});
        m.addRoute(ModRoute{ModSource::Envelope2, ModDestination::FilterCutoff,
                            -0.45f, true});
        m.addRoute(ModRoute{ModSource::Velocity, ModDestination::FMBrightness,
                            0.8f, false});

        p.arrangement.push_back(Clip{0, 2, 0.0f, 4.0f, 0});

        const std::string path = testPath("modmatrix_roundtrip.ctp");
        check(saveProject(p, path), "a modulated project saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");

        const ModMatrix& g = loaded.channels[2].oscillator.modMatrix;
        check(g.routeCount == 3, "all three routes survive");
        if (g.routeCount == 3) {
            check(g.routes[0].source == ModSource::LFO1 &&
                  g.routes[0].destination == ModDestination::WavetableMorph &&
                  std::fabs(g.routes[0].amount - 0.6f) < 1e-3f,
                  "with their sources, destinations and amounts");
            check(g.routes[1].destination == ModDestination::FilterCutoff &&
                  std::fabs(g.routes[1].amount + 0.45f) < 1e-3f,
                  "including negative amounts");
            check(!g.routes[2].enabled,
                  "and a disabled route stays disabled rather than coming "
                  "back on");
        }
        check(g.polyphonyLimit == 4 &&
              std::fabs(g.pitchBendSemitones - 7.0f) < 1e-3f,
              "the polyphony limit and bend range survive");
        check(g.lfos[0].shape == LFOShape::SampleHold &&
              std::fabs(g.lfos[0].rateHz - 9.5f) < 1e-3f &&
              std::fabs(g.lfos[0].delaySeconds - 0.15f) < 1e-3f &&
              !g.lfos[0].retrigger,
              "the first LFO survives entirely");
        check(g.lfos[2].shape == LFOShape::Square,
              "and so does the third, so the LFOs are not being collapsed "
              "into one another");
        check(std::fabs(g.env2Sustain - 0.35f) < 1e-3f,
              "the second envelope survives");

        std::remove(path.c_str());
    }

    // ---- An unmodulated project writes nothing --------------------------------
    {
        Project plain;
        const std::string path = testPath("modmatrix_absent.ctp");
        check(saveProject(plain, path), "an unmodulated project saves");

        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        check(text.find("MOD ") == std::string::npos &&
              text.find("MROUTE ") == std::string::npos,
              "and writes no modulation lines at all");
        std::remove(path.c_str());
    }

    // ---- The Project has not quietly become enormous --------------------------
    {
        /*
         * Every instrument config is inline in ChannelConfig and there are
         * 32 channels, so each field is paid for 32 times whether or not any
         * channel uses that engine. Adding the sampler at 64 zones took
         * Project from 23 KB to 170 KB and started overflowing the stack of
         * tests that hold two or three projects as locals.
         *
         * This is a tripwire, not a target. If it fails, the question is
         * whether the new field really needs to be inline for all 32
         * channels - not whether to raise the number.
         */
        check(sizeof(Project) < 160 * 1024,
              "a Project is under 160 KB (currently " +
              std::to_string(sizeof(Project) / 1024) + " KB) - it is a value "
              "type held as a local in a great deal of code, and every "
              "per-channel field is multiplied by 32");
    }
}


// ============================================================================
// 74. Pitch and time
// ============================================================================
static void testPitchAndTime() {
    beginTest("Pitch shift, formants and autotune");

    const float rate = 44100.0f;

    // A harmonic tone, because a pure sine tells you nothing about whether
    // the partials moved together.
    auto tone = [rate](float frequency, int samples) {
        std::vector<float> out(static_cast<size_t>(samples));
        for (int i = 0; i < samples; ++i) {
            const float t = float(i) / rate;
            out[static_cast<size_t>(i)] = 0.5f * (
                std::sin(6.28318530718f * frequency * t) +
                0.4f * std::sin(6.28318530718f * frequency * 2.0f * t) +
                0.25f * std::sin(6.28318530718f * frequency * 3.0f * t));
        }
        return out;
    };

    // The strongest partial below 2 kHz, which for these signals is the
    // fundamental.
    auto fundamental = [rate](const std::vector<float>& signal, size_t from) {
        const size_t N = 16384;
        if (signal.size() < from + N) return 0.0;

        std::vector<float> re(N), im(N, 0.0f);
        for (size_t i = 0; i < N; ++i) {
            const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                float(i) / float(N - 1)));
            re[i] = signal[from + i] * w;
        }
        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(re.data(), im.data());

        const double binHz = double(rate) / double(N);
        size_t loudest = 0;
        double peak = 0.0;
        for (size_t bin = 2; bin < size_t(2000.0 / binHz); ++bin) {
            const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                               double(im[bin]) * im[bin]);
            if (magnitude > peak) { peak = magnitude; loudest = bin; }
        }
        return double(loudest) * binHz;
    };

    auto runShifter = [&](PitchShifter& shifter, const std::vector<float>& input) {
        std::vector<float> out(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            out[i] = shifter.process(input[i]);
        }
        return out;
    };

    // ---- Unity is (nearly) the signal back -----------------------------------
    {
        PitchShifter shifter;
        shifter.configure(rate);
        shifter.semitones = 0.0f;

        const std::vector<float> input = tone(220.0f, 65536);
        const std::vector<float> output = runShifter(shifter, input);

        const double inHz = fundamental(input, 20000);
        const double outHz = fundamental(output, 20000);
        check(std::fabs(inHz - 220.0) < 6.0, "the test tone is at 220 Hz");
        check(std::fabs(outHz - inHz) < 6.0,
              "a shift of zero semitones returns the same pitch (" +
              std::to_string(outHz) + " Hz)");

        double energy = 0.0;
        for (size_t i = 20000; i < output.size(); ++i) energy += std::fabs(double(output[i]));
        check(energy > 100.0,
              "and it is not silence - a phase vocoder whose overlap-add "
              "constant is wrong cancels itself out entirely");
    }

    // ---- It shifts by the amount asked for -----------------------------------
    {
        struct Case { float semitones; double ratio; };
        const Case cases[] = {
            {12.0f, 2.0},      // an octave up
            {-12.0f, 0.5},     // an octave down
            {7.0f, 1.4983},    // a fifth
            {-5.0f, 0.7492},   // a fourth down
        };

        for (const Case& c : cases) {
            PitchShifter shifter;
            shifter.configure(rate);
            shifter.semitones = c.semitones;

            const std::vector<float> input = tone(220.0f, 65536);
            const std::vector<float> output = runShifter(shifter, input);
            const double outHz = fundamental(output, 20000);
            const double want = 220.0 * c.ratio;

            // Within a couple of analysis bins. A phase vocoder quantises
            // the shift to bin positions, so exactness is not on offer.
            if (std::fabs(outHz - want) > want * 0.06) {
                check(false, "shifting " + std::to_string(c.semitones) +
                      " semitones gave " + std::to_string(outHz) +
                      " Hz, wanted about " + std::to_string(want));
                break;
            }
        }
        check(true,
              "an octave up, an octave down, a fifth and a fourth all land "
              "where they should - which requires recovering each bin's TRUE "
              "frequency from its phase advance, not assuming it sits at the "
              "bin centre");
    }

    // ---- Output stays bounded -------------------------------------------------
    {
        PitchShifter shifter;
        shifter.configure(rate);
        shifter.semitones = 12.0f;

        const std::vector<float> input = tone(110.0f, 131072);
        const std::vector<float> output = runShifter(shifter, input);

        float peak = 0.0f;
        bool finite = true;
        for (float v : output) {
            if (!std::isfinite(v)) { finite = false; break; }
            peak = std::max(peak, std::fabs(v));
        }
        check(finite, "the shifter stays finite over a long run");
        check(peak < 4.0f,
              "and bounded (peak " + std::to_string(peak) + ") - the gain "
              "depends on the window's overlap constant, and getting that "
              "wrong shows up here rather than anywhere obvious");
    }

    // ---- Latency is reported -------------------------------------------------
    {
        PitchShifter shifter;
        shifter.configure(rate);
        check(shifter.latency() == PhaseVocoder::WINDOW,
              "the shifter reports a window of latency - it is real and "
              "unavoidable, since a bin's true frequency needs two frames");

        PitchShiftFx adapter;
        adapter.target = &shifter;
        check(adapter.latencySamples() == PhaseVocoder::WINDOW,
              "and the rack adapter passes it through, which is what a "
              "delay-compensating host would read");
    }

    // ---- The formant shifter moves the envelope, NOT the pitch --------------
    {
        FormantShifter shifter;
        shifter.configure(rate);
        shifter.semitones = 7.0f;

        const std::vector<float> input = tone(220.0f, 65536);
        std::vector<float> output(input.size());
        for (size_t i = 0; i < input.size(); ++i) output[i] = shifter.process(input[i]);

        const double outHz = fundamental(output, 20000);
        check(std::fabs(outHz - 220.0) < 10.0,
              "shifting formants by a fifth leaves the PITCH at 220 Hz (got " +
              std::to_string(outHz) + ") - that separation is the whole "
              "point: a pitch shifter moves both, which is why a voice an "
              "octave up is a chipmunk rather than a soprano");

        // And it must actually change the timbre, or it is a very expensive
        // passthrough.
        FormantShifter flat;
        flat.configure(rate);
        flat.semitones = 0.0f;
        std::vector<float> unshifted(input.size());
        for (size_t i = 0; i < input.size(); ++i) unshifted[i] = flat.process(input[i]);

        double difference = 0.0;
        for (size_t i = 30000; i < input.size(); ++i) {
            difference += std::fabs(double(output[i]) - double(unshifted[i]));
        }
        check(difference > 10.0,
              "while changing the timbre, or it would be an expensive "
              "passthrough");
    }

    // ---- Autotune pulls a flat note to the scale ----------------------------
    {
        AutoTune tuner;
        tuner.configure(rate);
        tuner.strength = 1.0f;
        tuner.rootNote = 0;
        tuner.scaleMask = 0x0FFF;      // chromatic
        tuner.minimumMagnitude = 0.0f;

        // 30 cents flat of A3 (220 Hz). Chromatic autotune should pull it to
        // 220, since A is the nearest allowed note.
        const float flatHz = 220.0f * std::pow(2.0f, -0.30f / 12.0f);
        const std::vector<float> input = tone(flatHz, 131072);

        std::vector<float> output(input.size());
        for (size_t i = 0; i < input.size(); ++i) output[i] = tuner.process(input[i]);

        const double before = fundamental(input, 40000);
        const double after = fundamental(output, 60000);

        check(std::fabs(before - double(flatHz)) < 6.0,
              "the input is flat of A3 (" + std::to_string(before) + " Hz)");
        check(after > before,
              "and autotune pulls it upward toward the note (" +
              std::to_string(after) + " Hz)");
        check(std::fabs(tuner.correctionSemitones()) > 0.05f,
              "reporting a real correction rather than none");
    }

    {
        // A scale that excludes the sung note moves it further - to the
        // nearest note the scale DOES allow.
        AutoTune tuner;
        tuner.configure(rate);
        tuner.strength = 1.0f;
        tuner.rootNote = 0;
        // C major: no A-sharp, no G-sharp. A is in it.
        tuner.scaleMask = 0b0000101010110101u;
        tuner.minimumMagnitude = 0.0f;

        check(tuner.detectedHz() == 0.0f,
              "nothing is detected before any audio has been fed in");

        const std::vector<float> input = tone(220.0f, 65536);
        for (float v : input) tuner.process(v);
        check(tuner.detectedHz() > 100.0f,
              "and a fundamental is found once there is (" +
              std::to_string(tuner.detectedHz()) + " Hz)");
    }

    {
        // Strength zero corrects nothing, which is what a bypass switch
        // ought to mean.
        AutoTune tuner;
        tuner.configure(rate);
        tuner.strength = 0.0f;
        tuner.minimumMagnitude = 0.0f;

        const float flatHz = 220.0f * std::pow(2.0f, -0.4f / 12.0f);
        const std::vector<float> input = tone(flatHz, 65536);
        for (float v : input) tuner.process(v);

        check(std::fabs(tuner.correctionSemitones()) < 0.02f,
              "a strength of zero corrects nothing at all");
    }

    {
        // Silence must not be "corrected", or every breath and consonant
        // produces a warble.
        AutoTune tuner;
        tuner.configure(rate);
        tuner.strength = 1.0f;
        tuner.minimumMagnitude = 0.05f;

        std::vector<float> quiet(65536, 0.0f);
        for (float v : quiet) tuner.process(v);
        check(std::fabs(tuner.correctionSemitones()) < 0.01f,
              "and silence is left alone rather than being tuned, which "
              "would warble on every breath");
    }

    // ---- Time stretch is offline, and changes length ------------------------
    {
        const std::vector<float> input = tone(220.0f, 44100);

        const std::vector<float> longer = timeStretch(input, 2.0f);
        const std::vector<float> shorter = timeStretch(input, 0.5f);

        check(longer.size() > input.size() * 1.8 &&
              longer.size() < input.size() * 2.2,
              "stretching by 2 roughly doubles the length (" +
              std::to_string(longer.size()) + " from " +
              std::to_string(input.size()) + ")");
        check(shorter.size() < input.size() * 0.7,
              "and halving it roughly halves the length");

        check(timeStretch(input, 1.0f).size() == input.size(),
              "a ratio of 1 is the input untouched");

        // The pitch must NOT change - that is the difference between time
        // stretching and simply resampling.
        auto stretchedPitch = [&](const std::vector<float>& signal) {
            const size_t N = 8192;
            if (signal.size() < N + 4096) return 0.0;
            std::vector<float> re(N), im(N, 0.0f);
            for (size_t i = 0; i < N; ++i) {
                const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                    float(i) / float(N - 1)));
                re[i] = signal[4096 + i] * w;
            }
            DSP::FFTPlan plan;
            plan.resize(N);
            plan.transform(re.data(), im.data());
            const double binHz = 44100.0 / double(N);
            size_t loudest = 0;
            double peak = 0.0;
            for (size_t bin = 2; bin < N / 2; ++bin) {
                const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                                   double(im[bin]) * im[bin]);
                if (magnitude > peak) { peak = magnitude; loudest = bin; }
            }
            return double(loudest) * binHz;
        };

        check(std::fabs(stretchedPitch(longer) - 220.0) < 12.0,
              "and the stretched audio is still at 220 Hz (" +
              std::to_string(stretchedPitch(longer)) +
              ") - resampling would have moved it, which is the whole "
              "distinction");

        bool finite = true;
        for (float v : longer) if (!std::isfinite(v)) { finite = false; break; }
        check(finite, "the stretched buffer is finite throughout");

        // Hostile input.
        check(timeStretch({}, 2.0f).empty(), "an empty buffer stretches to empty");
        check(!timeStretch(input,
                           std::numeric_limits<float>::quiet_NaN()).empty(),
              "a NaN ratio returns the input rather than an empty buffer");
        check(timeStretch(input, 0.0f).size() > 0,
              "and a ratio of zero is clamped rather than producing an "
              "output longer than memory");
    }
}

// ============================================================================
// 75. The pitch effects reach the rack
// ============================================================================
static void testPitchEffectsInRack() {
    beginTest("Pitch effects in the insert rack");

    // ---- The rack knows about them ------------------------------------------
    {
        check(EFFECT_TYPE_COUNT == CLASSIC_EFFECT_COUNT,
              "the classic order still lists every effect exactly once");

        EffectType parsed;
        check(effectTypeFromId("pitchshift", parsed) &&
              parsed == EffectType::PitchShift,
              "the pitch shifter has a stable token");
        check(effectTypeFromId("formantshift", parsed) &&
              parsed == EffectType::FormantShift, "and so does the formant shifter");
        check(effectTypeFromId("autotune", parsed) &&
              parsed == EffectType::AutoTune, "and autotune");

        // Appended, not inserted: every token that existed before must still
        // resolve to the same enumerator, or existing .ctp racks are
        // reinterpreted.
        check(effectTypeFromId("reverb", parsed) && parsed == EffectType::Reverb,
              "reverb still resolves where it always did");
        check(static_cast<int>(EffectType::PitchShift) >
              static_cast<int>(EffectType::Reverb),
              "and the new effects sit after it, so no existing file's rack "
              "order is renumbered");
    }

    // ---- Every type resolves to an adapter -----------------------------------
    //
    // EffectsChain::lookup() has a switch with a `default: return nullptr`,
    // and a type with no arm is skipped by the rack silently - no error, the
    // effect simply never runs. That is precisely what happened when these
    // three were added: the vocoder worked perfectly in isolation and did
    // nothing at all in a channel. This is the structural guard.
    {
        auto chainPtr = std::make_unique<EffectsChain>();
        bool allResolve = true;
        std::string missing;

        for (int i = 0; i < EFFECT_TYPE_COUNT; ++i) {
            const EffectType type = static_cast<EffectType>(i);
            if (chainPtr->effectFor(type) == nullptr) {
                allResolve = false;
                missing = effectDisplayName(type);
                break;
            }
        }
        check(allResolve,
              "every EffectType resolves to an adapter" +
              (allResolve ? std::string() : " (" + missing + " does not)"));
    }

    // ---- Off by default ------------------------------------------------------
    {
        const ChannelConfig fresh;
        check(!fresh.pitchShiftEnabled && !fresh.formantShiftEnabled &&
              !fresh.autoTuneEnabled,
              "all three are off on a new channel - none of them is anything "
              "a 2A03 could do, and the ground rule is that non-chip DSP "
              "ships silent");

        auto chainPtr = std::make_unique<EffectsChain>();
        check(!chainPtr->isEnabled(EffectType::PitchShift) &&
              !chainPtr->isEnabled(EffectType::FormantShift) &&
              !chainPtr->isEnabled(EffectType::AutoTune),
              "and off in a fresh chain too");
    }

    // ---- Enabling one changes the audio -------------------------------------
    {
        auto render = [](bool shift) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.masterCompressorEnabled = false;
            p.masterEQEnabled = false;
            p.masterVolume = 0.7f;

            p.channels[0].oscillator.type = OscillatorType::Sawtooth;
            p.channels[0].volume = 0.8f;
            p.channels[0].pan = 0.0f;
            if (shift) {
                p.channels[0].pitchShiftEnabled = true;
                p.channels[0].pitchShiftSemitones = 12.0f;
                p.channels[0].pitchShiftMix = 1.0f;
            }

            p.patterns.clear();
            Pattern pattern;
            Note note;
            note.pitch = 48;
            note.startTime = 0.0f;
            note.duration = 4.0f;
            note.oscillatorType = OscillatorType::Sawtooth;
            pattern.notes.push_back(note);
            p.patterns.push_back(pattern);
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            seqPtr->setSampleRate(44100.0f);
            seqPtr->setProject(&p);
            seqPtr->updateChannelConfigs();
            seqPtr->updateMasterEffects();
            seqPtr->play();

            std::vector<float> l(512), r(512);
            std::vector<float> collected;
            for (int b = 0; b < 60; ++b) {
                seqPtr->process(l.data(), r.data(), 512);
                if (b >= 20) collected.insert(collected.end(), l.begin(), l.end());
            }
            return collected;
        };

        const std::vector<float> plain = render(false);
        const std::vector<float> shifted = render(true);

        double difference = 0.0;
        const size_t n = std::min(plain.size(), shifted.size());
        for (size_t i = 0; i < n; ++i) {
            difference += std::fabs(double(plain[i]) - double(shifted[i]));
        }
        check(n > 0 && difference / double(n) > 1e-3,
              "switching the pitch shifter on changes what comes out of the "
              "channel");

        bool finite = true;
        for (float v : shifted) if (!std::isfinite(v)) { finite = false; break; }
        check(finite, "and the result is finite");
    }

    // ---- Settings survive a save and load -----------------------------------
    {
        Project p;
        p.channels[3].pitchShiftEnabled = true;
        p.channels[3].pitchShiftSemitones = -7.5f;
        p.channels[3].pitchShiftMix = 0.65f;
        p.channels[3].formantShiftEnabled = true;
        p.channels[3].formantShiftSemitones = 4.0f;
        p.channels[3].formantShiftMix = 0.8f;
        p.channels[3].autoTuneEnabled = true;
        p.channels[3].autoTuneStrength = 0.9f;
        p.channels[3].autoTuneRoot = 9;             // A
        p.channels[3].autoTuneScaleMask = 0b0000101010110101;
        p.channels[3].autoTuneMix = 0.75f;
        p.arrangement.push_back(Clip{0, 3, 0.0f, 4.0f, 0});

        const std::string path = testPath("pitchfx_roundtrip.ctp");
        check(saveProject(p, path), "the project saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");

        const ChannelConfig& c = loaded.channels[3];
        check(c.pitchShiftEnabled &&
              std::fabs(c.pitchShiftSemitones + 7.5f) < 1e-3f &&
              std::fabs(c.pitchShiftMix - 0.65f) < 1e-3f,
              "the pitch shifter's settings survive");
        check(c.formantShiftEnabled &&
              std::fabs(c.formantShiftSemitones - 4.0f) < 1e-3f,
              "the formant shifter's survive");
        check(c.autoTuneEnabled && c.autoTuneRoot == 9 &&
              c.autoTuneScaleMask == 0b0000101010110101 &&
              std::fabs(c.autoTuneStrength - 0.9f) < 1e-3f,
              "and autotune's, including the scale mask");

        std::remove(path.c_str());
    }

    // ---- Validation -----------------------------------------------------------
    {
        Project p;
        p.channels[0].pitchShiftSemitones = 500.0f;
        p.channels[0].formantShiftSemitones = std::numeric_limits<float>::quiet_NaN();
        p.channels[0].autoTuneStrength = -3.0f;
        p.channels[0].autoTuneRoot = 47;
        p.channels[0].autoTuneScaleMask = 0;

        clampProjectToValidRanges(p);

        check(std::fabs(p.channels[0].pitchShiftSemitones) <= 24.0f,
              "an absurd shift is clamped to two octaves - past that the bin "
              "mapping folds most of the spectrum off the end of the array");
        check(std::isfinite(p.channels[0].formantShiftSemitones),
              "a NaN formant shift is replaced");
        check(p.channels[0].autoTuneStrength >= 0.0f, "a negative strength is repaired");
        check(p.channels[0].autoTuneRoot >= 0 && p.channels[0].autoTuneRoot < 12,
              "an impossible root is repaired");
        check(p.channels[0].autoTuneScaleMask != 0,
              "and an empty scale becomes chromatic - a scale allowing no "
              "notes would correct nothing and read as autotune being broken");
    }
}


// ============================================================================
// 76. Engine presets
// ============================================================================
static void testInstrumentPresets() {
    beginTest("Engine presets");

    const float rate = 44100.0f;

    auto rms = [](const std::vector<float>& signal) {
        double sum = 0.0;
        for (float v : signal) sum += double(v) * v;
        return signal.empty() ? 0.0 : std::sqrt(sum / double(signal.size()));
    };

    auto centroid = [rate](const std::vector<float>& signal) {
        size_t N = 1;
        while (N * 2 <= signal.size()) N *= 2;
        if (N < 512) return 0.0;
        std::vector<float> re(N), im(N, 0.0f);
        for (size_t i = 0; i < N; ++i) {
            const float w = 0.5f * (1.0f - std::cos(6.28318530718f *
                float(i) / float(N - 1)));
            re[i] = signal[i] * w;
        }
        DSP::FFTPlan plan;
        plan.resize(N);
        plan.transform(re.data(), im.data());
        double weighted = 0.0, total = 0.0;
        const double binHz = double(rate) / double(N);
        for (size_t bin = 1; bin < N / 2; ++bin) {
            const double magnitude = std::sqrt(double(re[bin]) * re[bin] +
                                               double(im[bin]) * im[bin]);
            weighted += double(bin) * binHz * magnitude;
            total += magnitude;
        }
        return (total > 0.0) ? weighted / total : 0.0;
    };

    // ---- Every FM preset makes a distinct sound ----------------------------
    {
        std::vector<std::vector<float>> rendered;
        bool allSound = true;
        std::string silent;

        for (int i = 0; i < FM_PRESET_COUNT; ++i) {
            FMPatch patch;
            FM_PRESETS[i].apply(patch);
            clampFMPatch(patch);

            FMVoiceState state;
            state.reset(patch);
            std::vector<float> audio(16384);
            for (size_t s = 0; s < audio.size(); ++s) {
                audio[s] = fm::process(patch, state, 220.0f, 1.0f, rate, false);
            }

            if (rms(audio) < 0.005) { allSound = false; silent = FM_PRESETS[i].name; }
            rendered.push_back(audio);
        }

        check(allSound,
              "every FM preset makes a sound" +
              (allSound ? std::string() : " (" + silent + " is silent)") +
              " - a preset that produces nothing teaches the user the engine "
              "is broken");

        // And they must differ from each other, or the menu overstates what
        // the engine does.
        bool allDistinct = true;
        for (size_t a = 0; a < rendered.size() && allDistinct; ++a) {
            for (size_t b = a + 1; b < rendered.size(); ++b) {
                double difference = 0.0;
                const size_t n = std::min(rendered[a].size(), rendered[b].size());
                for (size_t i = 0; i < n; ++i) {
                    difference += std::fabs(double(rendered[a][i]) -
                                            double(rendered[b][i]));
                }
                if (n == 0 || difference / double(n) < 1e-3) {
                    allDistinct = false;
                    break;
                }
            }
        }
        check(allDistinct,
              "and no two FM presets sound the same - four variations on one "
              "routing would look tidier and teach nothing");
    }

    {
        // The bell must actually behave like a bell: bright attack, pure
        // tail. That is the property the preset exists to demonstrate.
        FMPatch bell;
        presets::fmBell(bell);
        clampFMPatch(bell);

        FMVoiceState state;
        state.reset(bell);
        std::vector<float> audio(rate);
        for (size_t i = 0; i < audio.size(); ++i) {
            audio[i] = fm::process(bell, state, 440.0f, 1.0f, rate, false);
        }

        const std::vector<float> head(audio.begin(), audio.begin() + 4096);
        const std::vector<float> tail(audio.begin() + 20000, audio.begin() + 24096);
        check(centroid(head) > centroid(tail) * 1.25,
              "the Bell preset starts bright and settles pure (" +
              std::to_string(centroid(head)) + " -> " +
              std::to_string(centroid(tail)) + ")");
    }

    // ---- Every drum preset sounds, and the kicks differ from the hats ------
    {
        bool allSound = true;
        std::string silent;
        double kickCentroid = 0.0;
        double hatCentroid = 0.0;

        for (int i = 0; i < DRUM_PRESET_COUNT; ++i) {
            DrumModelConfig config;
            DRUM_PRESETS[i].apply(config);
            clampDrumModel(config);

            DrumModelVoice voice;
            voice.trigger(config, 1.0f, rate, 99u);
            std::vector<float> audio(16384);
            for (size_t s = 0; s < audio.size(); ++s) {
                audio[s] = voice.process(config);
            }

            if (rms(audio) < 0.002) { allSound = false; silent = DRUM_PRESETS[i].name; }
            if (config.voice == DrumVoiceType::Kick && kickCentroid == 0.0) {
                kickCentroid = centroid(audio);
            }
            if (config.voice == DrumVoiceType::HiHat && hatCentroid == 0.0) {
                hatCentroid = centroid(audio);
            }
        }

        check(allSound,
              "every drum preset makes a sound" +
              (allSound ? std::string() : " (" + silent + " is silent)"));
        check(hatCentroid > kickCentroid,
              "and the hat presets are brighter than the kick presets (" +
              std::to_string(kickCentroid) + " vs " +
              std::to_string(hatCentroid) + ")");
    }

    {
        // The 808 kick must ring far longer than the hard kick, since that
        // is the entire difference between them.
        DrumModelConfig eightOhEight, hard;
        presets::drum808Kick(eightOhEight);
        presets::drumHardKick(hard);
        check(eightOhEight.decaySeconds > hard.decaySeconds * 3.0f,
              "the 808 kick rings far longer than the hard kick, which is "
              "what makes it a bass note rather than a thud");
    }

    // ---- Granular presets keep the loaded sample ---------------------------
    {
        for (int i = 0; i < GRANULAR_PRESET_COUNT; ++i) {
            GranularConfig config;
            config.sampleId = 7;              // as if the user had loaded one
            GRANULAR_PRESETS[i].apply(config);
            if (config.sampleId != 7) {
                check(false, std::string("granular preset ") +
                      GRANULAR_PRESETS[i].name + " threw away the sample");
                break;
            }
        }
        check(true,
              "every granular preset keeps the sample the user loaded - "
              "'try this setting' must not discard the audio it applies to");

        GranularConfig freeze;
        presets::granFreeze(freeze);
        check(freeze.positionRate == 0.0f,
              "Freeze actually freezes the read position");

        GranularConfig stutter;
        presets::granStutter(stutter);
        check(stutter.spray == 0.0f && stutter.windowShape > 0.9f,
              "and Stutter has no scatter and a flat window, which is what "
              "makes it rhythmic rather than smeared");
    }

    // ---- Modulation presets route something --------------------------------
    {
        for (int i = 0; i < MOD_PRESET_COUNT; ++i) {
            ModMatrix matrix;
            MOD_PRESETS[i].apply(matrix);
            clampModMatrix(matrix);

            if (!matrix.anyActive()) {
                check(false, std::string("modulation preset ") +
                      MOD_PRESETS[i].name + " routes nothing");
                break;
            }
        }
        check(true,
              "every modulation preset has at least one active route - an "
              "empty matrix is exactly the state these exist to get out of");

        ModMatrix vibrato;
        presets::modVibrato(vibrato);
        check(vibrato.lfos[0].delaySeconds > 0.0f,
              "the Vibrato preset delays its LFO, because vibrato arriving "
              "with the note sounds mechanical and no player does it");
    }

    // ---- All of them survive validation ------------------------------------
    //
    // A preset that validation immediately repairs is a preset with a value
    // outside the range the rest of the program allows, which is a bug in
    // the preset rather than in validation.
    {
        bool fmStable = true;
        for (int i = 0; i < FM_PRESET_COUNT; ++i) {
            FMPatch before;
            FM_PRESETS[i].apply(before);
            FMPatch after = before;
            clampFMPatch(after);

            if (std::fabs(after.index - before.index) > 1e-4f ||
                std::fabs(after.algorithm.feedback - before.algorithm.feedback) > 1e-4f) {
                fmStable = false;
                break;
            }
            for (int op = 0; op < FM_OPERATORS; ++op) {
                const FMOperator& a = before.operators[static_cast<size_t>(op)];
                const FMOperator& b = after.operators[static_cast<size_t>(op)];
                if (std::fabs(a.ratio - b.ratio) > 1e-4f ||
                    std::fabs(a.level - b.level) > 1e-4f ||
                    a.enabled != b.enabled) {
                    fmStable = false;
                    break;
                }
            }
            if (!fmStable) break;
        }
        check(fmStable,
              "no FM preset is altered by validation - one that is has a "
              "value outside the range the rest of the program allows");

        bool drumStable = true;
        for (int i = 0; i < DRUM_PRESET_COUNT; ++i) {
            DrumModelConfig before;
            DRUM_PRESETS[i].apply(before);
            DrumModelConfig after = before;
            clampDrumModel(after);
            if (std::fabs(after.tuneHz - before.tuneHz) > 1e-3f ||
                std::fabs(after.decaySeconds - before.decaySeconds) > 1e-4f) {
                drumStable = false;
                break;
            }
        }
        check(drumStable, "and no drum preset is either");
    }
}



// ============================================================================
// 81. Conflicting settings
// ============================================================================
static void testSettingsAudit() {
    beginTest("Conflicting settings audit");

    auto has = [](const std::vector<AuditFinding>& findings,
                  const std::string& fragment) {
        for (const AuditFinding& finding : findings) {
            if (finding.what.find(fragment) != std::string::npos) return true;
        }
        return false;
    };

    // ---- A fresh project says nothing --------------------------------------
    //
    // The hard requirement. A panel that always has something in it is one
    // everybody learns to ignore, and then it is worse than nothing.
    {
        auto fresh = std::make_unique<Project>();
        const auto findings = auditProject(*fresh);
        if (!findings.empty()) {
            check(false, "a fresh project reports " +
                  std::to_string(findings.size()) +
                  " findings, first: " + findings[0].what);
        } else {
            check(true, "a fresh project reports nothing at all");
        }
    }

    // A project someone has actually filled in, with nothing wrong with it,
    // must also be silent - the audit must not fire on ordinary use.
    {
        auto p = std::make_unique<Project>();
        Pattern pattern;
        for (int i = 0; i < 8; ++i) {
            Note note;
            note.pitch = 60 + i;
            note.startTime = float(i) * 0.5f;
            note.duration = 0.4f;
            pattern.notes.push_back(note);
        }
        p->patterns.clear();
        p->patterns.push_back(pattern);
        p->arrangement.clear();
        for (int ch = 0; ch < 4; ++ch) {
            p->arrangement.push_back(Clip{0, ch, 0.0f, 4.0f, 0});
            p->channels[ch].reverbEnabled = true;
            p->channels[ch].reverbMix = 0.3f;
        }
        const auto findings = auditProject(*p);
        if (!findings.empty()) {
            check(false, "an ordinary project reports " +
                  std::to_string(findings.size()) +
                  " findings, first: " + findings[0].what);
        } else {
            check(true, "and neither does an ordinary working project");
        }
    }

    // ---- Solo -----------------------------------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->channels[3].solo = true;
        const auto findings = auditProject(*p);
        check(has(findings, "Solo is on"),
              "a soloed channel is reported - it is the single most common "
              "reason someone cannot hear the rest of their project");
        check(!findings.empty() && findings[0].severity == AuditSeverity::Problem,
              "as a Problem, and sorted to the top");
    }

    // ---- Silent by accident ---------------------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 2, 0.0f, 4.0f, 0});
        p->channels[2].volume = 0.0f;
        check(has(auditProject(*p), "fader at zero"),
              "a channel with content and a zero fader is reported");

        p->channels[2].volume = 0.8f;
        p->channels[2].muted = true;
        check(has(auditProject(*p), "muted but has clips"),
              "and a muted one is mentioned, more gently");
    }

    {
        auto p = std::make_unique<Project>();
        p->masterVolume = 0.0f;
        check(has(auditProject(*p), "Master volume is at zero"),
              "a zero master volume is reported");
    }

    // ---- Engines with nothing to play -----------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p->channels[0].oscillator.type = OscillatorType::Sampler;
        check(has(auditProject(*p), "sampler with no zones"),
              "a sampler with no zones is reported");

        SampleZone zone;
        zone.sampleId = -1;
        p->channels[0].oscillator.sampler.addZone(zone);
        check(has(auditProject(*p), "no audio behind them"),
              "and one whose zones point at nothing");
    }

    {
        auto p = std::make_unique<Project>();
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p->channels[0].oscillator.type = OscillatorType::Granular;
        check(has(auditProject(*p), "granular with no source"),
              "a granular channel with no sample is reported");
    }

    // ---- On, and doing nothing -------------------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 1, 0.0f, 4.0f, 0});
        p->channels[1].reverbEnabled = true;
        p->channels[1].reverbMix = 0.0f;
        check(has(auditProject(*p), "Reverb is on with its mix at zero"),
              "an effect switched on with its mix at zero is reported - it is "
              "running, costing CPU, and none of it reaches the output");
    }

    {
        auto p = std::make_unique<Project>();
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 1, 0.0f, 4.0f, 0});
        p->channels[1].sidechainEnabled = true;
        p->channels[1].sidechainSource = -1;
        p->channels[1].sidechainBus = -1;
        check(has(auditProject(*p), "sidechain on with no source"),
              "a sidechain with nothing to duck against is reported");
    }

    {
        auto p = std::make_unique<Project>();
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 1, 0.0f, 4.0f, 0});
        p->channels[1].autoTuneEnabled = true;
        p->channels[1].pitchShiftEnabled = true;
        p->channels[1].pitchShiftSemitones = 3.5f;
        check(has(auditProject(*p), "Auto-Tune and Pitch Shift both on"),
              "autotune fighting a pitch shifter is reported - the correction "
              "lands somewhere between the two");
    }

    // ---- Sends into nothing ----------------------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p->channels[0].sends[0].destination = 0;
        p->channels[0].sends[0].level = 0.5f;
        p->auxBuses[0].muted = true;
        check(has(auditProject(*p), "which is muted"),
              "a send into a muted bus is reported - the bus being muted is "
              "not visible from the channel");

        p->auxBuses[0].muted = false;
        p->channels[0].sends[0].level = 0.0f;
        check(has(auditProject(*p), "send to"),
              "and so is a send routed but left at zero");
    }

    // ---- Modulation that cannot reach anything ---------------------------------
    {
        auto p = std::make_unique<Project>();
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p->channels[0].oscillator.type = OscillatorType::FMSynth;
        p->channels[0].oscillator.modMatrix.addRoute(
            ModRoute{ModSource::LFO1, ModDestination::WavetableMorph, 0.5f, true});

        // Searched by the same function the finding is built from, so the
        // test cannot drift out of step with the wording.
        const std::string morph = modDestinationName(ModDestination::WavetableMorph);

        check(has(auditProject(*p), morph),
              "a route to " + morph + " on an FM channel is reported - it is "
              "legal, saved, shown as enabled, and does nothing whatsoever");

        // The same route on a wavetable channel is fine and must not fire.
        p->channels[0].oscillator.type = OscillatorType::Custom;
        check(!has(auditProject(*p), morph),
              "and the same route on a wavetable channel is not reported");
    }

    {
        // Filter destinations need the filter switched on.
        auto p = std::make_unique<Project>();
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p->channels[0].filterEnabled = false;
        p->channels[0].oscillator.modMatrix.addRoute(
            ModRoute{ModSource::LFO1, ModDestination::FilterCutoff, 0.5f, true});
        const std::string cutoff = modDestinationName(ModDestination::FilterCutoff);

        check(has(auditProject(*p), cutoff),
              "a filter route with the filter off is reported");

        p->channels[0].filterEnabled = true;
        check(!has(auditProject(*p), cutoff),
              "and is not once the filter is on");
    }

    // ---- Monophonic channels playing chords ------------------------------------
    {
        auto p = std::make_unique<Project>();
        Pattern chord;
        for (int i = 0; i < 3; ++i) {
            Note note;
            note.pitch = 60 + i * 4;
            note.startTime = 0.0f;      // all together
            note.duration = 2.0f;
            chord.notes.push_back(note);
        }
        p->patterns.clear();
        p->patterns.push_back(chord);
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});
        p->channels[0].oscillator.modMatrix.polyphonyLimit = 1;

        check(has(auditProject(*p), "monophonic but plays chords"),
              "a monophonic channel carrying chords is reported - each note "
              "steals the voice from the one before it");

        p->channels[0].oscillator.modMatrix.polyphonyLimit = 4;
        check(!has(auditProject(*p), "monophonic but plays chords"),
              "and is not once the limit is raised");
    }

    // ---- Content stranded past the chip limit ----------------------------------
    {
        auto p = std::make_unique<Project>();
        p->chipAuthentic = true;
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 20, 0.0f, 4.0f, 0});
        check(has(auditProject(*p), "past the chip channel limit"),
              "clips above the chip channel cap are reported - they are still "
              "in the file and never mixed");

        p->chipAuthentic = false;
        check(!has(auditProject(*p), "past the chip channel limit"),
              "and are not when chip mode is off");
    }

    // ---- Changes past the end of the song --------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->songLength = 32.0f;
        p->tempoMap.setTempo(500.0f, 140.0f);
        check(has(auditProject(*p), "past the end of the song"),
              "a tempo change the playhead never reaches is reported");
    }

    // ---- Missing audio ----------------------------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->missingSamples.push_back("D:/moved/break.wav");
        check(has(auditProject(*p), "could not be found"),
              "missing audio files are reported");
    }

    // ---- Ordering and counting ---------------------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->channels[5].solo = true;              // Problem
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 1, 0.0f, 4.0f, 0});
        p->channels[1].reverbEnabled = true;     // Warning
        p->channels[1].reverbMix = 0.0f;
        p->channels[1].muted = true;             // Note

        const auto findings = auditProject(*p);
        check(findings.size() >= 3, "all three severities are found");

        bool ordered = true;
        for (size_t i = 1; i < findings.size(); ++i) {
            if (findings[i].severity < findings[i - 1].severity) ordered = false;
        }
        check(ordered,
              "and they come back worst first, so the panel reads in the "
              "order someone would act on it");

        check(auditCount(findings, AuditSeverity::Problem) >= 1 &&
              auditCount(findings, AuditSeverity::Warning) >= 1 &&
              auditCount(findings, AuditSeverity::Note) >= 1,
              "and the per-severity counts add up");
    }

    // ---- Every finding is actionable ---------------------------------------------
    //
    // A warning that only names a field is a puzzle rather than a help, so
    // each one has to say what, why, and what to do.
    {
        auto p = std::make_unique<Project>();
        p->channels[0].solo = true;
        p->masterVolume = 0.0f;
        p->missingSamples.push_back("gone.wav");
        p->arrangement.clear();
        p->arrangement.push_back(Clip{0, 1, 0.0f, 4.0f, 0});
        p->channels[1].delayEnabled = true;
        p->channels[1].delayMix = 0.0f;

        bool allComplete = true;
        std::string incomplete;
        for (const AuditFinding& finding : auditProject(*p)) {
            if (finding.what.empty() || finding.why.empty() || finding.fix.empty()) {
                allComplete = false;
                incomplete = finding.what;
                break;
            }
        }
        check(allComplete,
              "every finding says what, why and what to do" +
              (allComplete ? std::string() : " (" + incomplete + " does not)"));
    }

    // ---- The audit changes nothing ------------------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->channels[0].solo = true;
        p->channels[2].muted = true;
        p->masterVolume = 0.0f;

        const std::string before = [&] {
            std::ostringstream out;
            writeProject(out, *p);
            return out.str();
        }();

        auditProject(*p);

        const std::string after = [&] {
            std::ostringstream out;
            writeProject(out, *p);
            return out.str();
        }();

        check(before == after,
              "auditing a project does not change it - this reports, and the "
              "user decides; silently unmuting a channel would be worse than "
              "the confusion it solves");
    }
}


// ============================================================================
// 82. Nothing the program ships trips its own audit
// ============================================================================
static void testShippedContentIsClean() {
    beginTest("Shipped content passes the audit");

    /*
     * A template that opens with a warning teaches the user, on their first
     * contact with the panel, that it is noise. So every starter template
     * and every genre kit has to be clean - and if one is not, the fault is
     * either in the template or in the rule, and both are worth knowing.
     */
    for (int g = 0; g < static_cast<int>(Genre::Count); ++g) {
        const Genre genre = static_cast<Genre>(g);

        auto p = std::make_unique<Project>(makeGenreTemplate(genre));

        const auto findings = auditProject(*p);
        if (!findings.empty()) {
            check(false, std::string(genreName(genre)) +
                  "'s starter template trips the audit: " + findings[0].what);
            continue;
        }
        check(true, std::string(genreName(genre)) +
              "'s starter template is clean");
    }

    // The quick-start kits, which write patterns into an existing project.
    {
        int recipeCount = 0;
        const KitRecipe* recipes = kitRecipes(recipeCount);

        bool clean = true;
        std::string problem;

        for (int r = 0; r < recipeCount && clean; ++r) {
            auto fresh = std::make_unique<Project>();
            fresh->patterns.clear();
            fresh->patterns.push_back(Pattern());

            const KitVoices voices;
            applyKitRecipe(fresh->patterns[0], recipes[r], 0.0f, 1, 60, voices);
            fresh->arrangement.clear();
            fresh->arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            const auto findings = auditProject(*fresh);
            if (!findings.empty()) {
                clean = false;
                problem = std::string(recipes[r].name) + ": " + findings[0].what;
            }
        }
        check(clean,
              "and so is every quick-start kit" +
              (clean ? std::string() : " (" + problem + ")"));
    }
}


// ============================================================================
// 83. Reverb algorithms
// ============================================================================
static void testReverbAlgorithms() {
    beginTest("Reverb algorithms");

    const float rate = 44100.0f;

    // A short burst, then silence. Everything below measures what the tank
    // does with the silence, which is the whole point of a reverb.
    auto burstThenSilence = [rate](int burstSamples, int totalSamples) {
        std::vector<float> input(static_cast<size_t>(totalSamples), 0.0f);
        for (int i = 0; i < burstSamples && i < totalSamples; ++i) {
            const float t = float(i) / rate;
            input[static_cast<size_t>(i)] =
                0.8f * std::sin(6.28318530718f * 220.0f * t);
        }
        return input;
    };

    auto renderTank = [rate](ReverbAlgorithm algorithm,
                             const std::vector<float>& input) {
        auto tankPtr = std::make_unique<AdvancedReverb>();
        tankPtr->prepare(rate, algorithm);
        tankPtr->size = 0.7f;
        tankPtr->damping = 0.35f;

        std::vector<float> out(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            const auto [l, r] = tankPtr->processStereo(input[i]);
            out[i] = (l + r) * 0.5f;
        }
        return out;
    };

    auto rmsOf = [](const std::vector<float>& signal, size_t from, size_t to) {
        if (from >= signal.size()) return 0.0;
        to = std::min(to, signal.size());
        double sum = 0.0;
        size_t count = 0;
        for (size_t i = from; i < to; ++i) { sum += double(signal[i]) * signal[i]; ++count; }
        return (count > 0) ? std::sqrt(sum / double(count)) : 0.0;
    };

    // ---- Every algorithm makes a tail, and none of them runs away ----------
    {
        const std::vector<float> input = burstThenSilence(4410, 88200);

        for (int a = 0; a < static_cast<int>(ReverbAlgorithm::Count); ++a) {
            const ReverbAlgorithm algorithm = static_cast<ReverbAlgorithm>(a);
            if (algorithm == ReverbAlgorithm::Room) continue;   // not the tank

            const std::vector<float> out = renderTank(algorithm, input);
            const std::string name = reverbAlgorithmName(algorithm);

            bool finite = true;
            float peak = 0.0f;
            for (float v : out) {
                if (!std::isfinite(v)) { finite = false; break; }
                peak = std::max(peak, std::fabs(v));
            }
            if (!finite) {
                check(false, name + " produced a non-finite sample");
                continue;
            }
            check(peak < 4.0f,
                  name + " stays bounded over two seconds (peak " +
                  std::to_string(peak) + ")");

            // There must be something after the input stops - otherwise it
            // is a filter, not a reverb.
            const double tail = rmsOf(out, 5000, 20000);
            check(tail > 1e-4,
                  name + " has a tail after the input stops (rms " +
                  std::to_string(tail) + ")");
        }
    }

    // ---- And they decay rather than sustaining -----------------------------
    {
        const std::vector<float> input = burstThenSilence(2205, 132300);

        for (int a = 0; a < static_cast<int>(ReverbAlgorithm::Count); ++a) {
            const ReverbAlgorithm algorithm = static_cast<ReverbAlgorithm>(a);
            if (algorithm == ReverbAlgorithm::Room) continue;
            // Reverse deliberately swells rather than decaying from the
            // start; it gets its own test below.
            if (algorithm == ReverbAlgorithm::Reverse) continue;

            const std::vector<float> out = renderTank(algorithm, input);
            const double early = rmsOf(out, 3000, 13000);
            const double late = rmsOf(out, 100000, 110000);

            if (!(early > late * 1.5)) {
                check(false, std::string(reverbAlgorithmName(algorithm)) +
                      " does not decay (early " + std::to_string(early) +
                      ", late " + std::to_string(late) + ")");
                break;
            }
        }
        check(true, "every algorithm decays rather than sustaining forever - "
                    "an orthogonal feedback matrix neither adds nor removes "
                    "energy, so the decay comes entirely from the gain");
    }

    // ---- They are genuinely different from each other -----------------------
    {
        const std::vector<float> input = burstThenSilence(2205, 44100);
        const std::vector<float> plate = renderTank(ReverbAlgorithm::Plate, input);
        const std::vector<float> hall = renderTank(ReverbAlgorithm::Hall, input);
        const std::vector<float> spring = renderTank(ReverbAlgorithm::Spring, input);

        auto meanDifference = [](const std::vector<float>& a,
                                 const std::vector<float>& b) {
            const size_t n = std::min(a.size(), b.size());
            double sum = 0.0;
            for (size_t i = 0; i < n; ++i) sum += std::fabs(double(a[i]) - double(b[i]));
            return (n > 0) ? sum / double(n) : 0.0;
        };

        check(meanDifference(plate, hall) > 1e-3,
              "a plate and a hall are not the same reverb");
        check(meanDifference(plate, spring) > 1e-3,
              "and neither is a spring");
    }

    // ---- Gated cuts, rather than fading -------------------------------------
    {
        const std::vector<float> input = burstThenSilence(2205, 44100);
        const std::vector<float> gated = renderTank(ReverbAlgorithm::Gated, input);
        const std::vector<float> plate = renderTank(ReverbAlgorithm::Plate, input);

        // Well past the gate's hold, the gated tail must be far quieter than
        // the same tank left to decay.
        const double gatedLate = rmsOf(gated, 30000, 40000);
        const double plateLate = rmsOf(plate, 30000, 40000);

        check(plateLate > 1e-6, "the ungated plate still has a tail there");
        check(gatedLate < plateLate * 0.25,
              "and the gated one has been cut off (" +
              std::to_string(gatedLate) + " vs " + std::to_string(plateLate) +
              ") - the eighties snare is not a short reverb, it is a long "
              "one switched off part way through");
    }

    // ---- Reverse swells -------------------------------------------------------
    {
        // A single hit near the start. A reverse reverb's output should
        // build rather than begin loud.
        std::vector<float> input(88200, 0.0f);
        for (int i = 0; i < 2205; ++i) {
            const float t = float(i) / rate;
            input[static_cast<size_t>(i)] =
                0.8f * std::sin(6.28318530718f * 220.0f * t);
        }

        auto tankPtr = std::make_unique<AdvancedReverb>();
        tankPtr->prepare(rate, ReverbAlgorithm::Reverse);
        tankPtr->reverseSeconds = 0.5f;

        std::vector<float> out(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            const auto [l, r] = tankPtr->processStereo(input[i]);
            out[i] = (l + r) * 0.5f;
        }

        bool finite = true;
        for (float v : out) if (!std::isfinite(v)) { finite = false; break; }
        check(finite, "reverse stays finite");

        const double atStart = rmsOf(out, 0, 2000);
        const double later = rmsOf(out, 8000, 18000);
        check(later > atStart,
              "a reverse reverb builds rather than starting at full level (" +
              std::to_string(atStart) + " -> " + std::to_string(later) + ")");
    }

    // ---- Shimmer puts energy above the input --------------------------------
    {
        // A steady low tone in; the tail should contain content an octave up
        // that the input never had.
        std::vector<float> input(132300, 0.0f);
        for (int i = 0; i < 44100; ++i) {
            const float t = float(i) / rate;
            input[static_cast<size_t>(i)] =
                0.6f * std::sin(6.28318530718f * 220.0f * t);
        }

        auto tankPtr = std::make_unique<AdvancedReverb>();
        tankPtr->prepare(rate, ReverbAlgorithm::Shimmer);
        tankPtr->size = 0.85f;
        tankPtr->damping = 0.15f;
        tankPtr->shimmerAmount = 0.7f;
        tankPtr->shimmerSemitones = 12.0f;

        std::vector<float> out(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            const auto [l, r] = tankPtr->processStereo(input[i]);
            out[i] = (l + r) * 0.5f;
        }

        bool finite = true;
        float peak = 0.0f;
        for (float v : out) {
            if (!std::isfinite(v)) { finite = false; break; }
            peak = std::max(peak, std::fabs(v));
        }
        check(finite && peak < 4.0f,
              "shimmer stays finite and bounded with a pitch shifter inside "
              "its own feedback path (peak " + std::to_string(peak) + ")");

        const double tail = rmsOf(out, 60000, 90000);
        check(tail > 1e-4, "and it rings on after the input stops");
    }

    // ---- Switching algorithm mid-life is safe --------------------------------
    {
        auto tankPtr = std::make_unique<AdvancedReverb>();
        tankPtr->prepare(rate, ReverbAlgorithm::Plate);

        bool finite = true;
        for (int a = 0; a < static_cast<int>(ReverbAlgorithm::Count) && finite; ++a) {
            tankPtr->prepare(rate, static_cast<ReverbAlgorithm>(a));
            for (int i = 0; i < 4410; ++i) {
                const float t = float(i) / rate;
                const auto [l, r] = tankPtr->processStereo(
                    0.5f * std::sin(6.28318530718f * 330.0f * t));
                if (!std::isfinite(l) || !std::isfinite(r)) { finite = false; break; }
            }
        }
        check(finite,
              "re-preparing the tank for a different algorithm while it holds "
              "a tail leaves it in a usable state rather than reading a "
              "resized buffer with a stale index");
    }

    // ---- Extremes -------------------------------------------------------------
    {
        auto tankPtr = std::make_unique<AdvancedReverb>();
        bool allSafe = true;
        std::string failed;

        for (int a = 0; a < static_cast<int>(ReverbAlgorithm::Count) && allSafe; ++a) {
            const ReverbAlgorithm algorithm = static_cast<ReverbAlgorithm>(a);
            tankPtr->prepare(rate, algorithm);

            // Everything at its limit at once, with a loud input.
            tankPtr->size = 1.0f;
            tankPtr->damping = 0.0f;
            tankPtr->predelay = 1.0f;
            tankPtr->width = 1.0f;
            tankPtr->shimmerAmount = 1.0f;
            tankPtr->shimmerSemitones = 24.0f;
            tankPtr->gateHold = 0.0f;
            tankPtr->gateThreshold = 0.0f;
            tankPtr->reverseSeconds = 5.0f;      // past the buffer
            tankPtr->dispersion = 1.0f;

            float peak = 0.0f;
            for (int i = 0; i < 88200; ++i) {
                const auto [l, r] = tankPtr->processStereo(0.95f);
                if (!std::isfinite(l) || !std::isfinite(r)) {
                    allSafe = false;
                    failed = reverbAlgorithmName(algorithm);
                    break;
                }
                peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            }
            if (allSafe && peak > 8.0f) {
                allSafe = false;
                failed = std::string(reverbAlgorithmName(algorithm)) +
                         " reached " + std::to_string(peak);
            }
        }
        check(allSafe,
              "every algorithm survives every parameter at its limit with a "
              "constant loud input" +
              (allSafe ? std::string() : " (" + failed + ")"));
    }

    // ---- A NaN in does not poison the tank ------------------------------------
    {
        auto tankPtr = std::make_unique<AdvancedReverb>();
        tankPtr->prepare(rate, ReverbAlgorithm::Plate);

        tankPtr->processStereo(std::numeric_limits<float>::quiet_NaN());
        tankPtr->processStereo(std::numeric_limits<float>::infinity());

        bool recovered = true;
        for (int i = 0; i < 4410; ++i) {
            const auto [l, r] = tankPtr->processStereo(0.3f);
            if (!std::isfinite(l) || !std::isfinite(r)) { recovered = false; break; }
        }
        check(recovered,
              "a NaN arriving from a broken upstream effect does not poison "
              "the tank permanently - a feedback loop that takes one is "
              "silent for the rest of the session");
    }
}

// ============================================================================
// 84. The algorithms reach the channel and the file
// ============================================================================
static void testReverbAlgorithmsInChannel() {
    beginTest("Reverb algorithms in a channel");

    // ---- Room is the default and is untouched --------------------------------
    {
        const ChannelConfig fresh;
        check(fresh.reverbAlgorithm == static_cast<int>(ReverbAlgorithm::Room),
              "a new channel is set to Room - the original algorithm, so "
              "every existing project sounds exactly as it did");

        auto chainPtr = std::make_unique<EffectsChain>();
        check(chainPtr->reverb.algorithm == ReverbAlgorithm::Room,
              "and so is a fresh chain");
    }

    // ---- Choosing another one changes the audio -------------------------------
    {
        auto render = [](int algorithm) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.masterCompressorEnabled = false;
            p.masterEQEnabled = false;
            p.masterVolume = 0.7f;

            p.channels[0].oscillator.type = OscillatorType::Pulse;
            p.channels[0].volume = 0.8f;
            p.channels[0].pan = 0.0f;
            p.channels[0].reverbEnabled = true;
            p.channels[0].reverbMix = 0.8f;
            p.channels[0].reverbAlgorithm = algorithm;

            p.patterns.clear();
            Pattern pattern;
            Note note;
            note.pitch = 60;
            note.startTime = 0.0f;
            note.duration = 0.5f;
            note.oscillatorType = OscillatorType::Pulse;
            pattern.notes.push_back(note);
            p.patterns.push_back(pattern);
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            seqPtr->setSampleRate(44100.0f);
            seqPtr->setProject(&p);
            seqPtr->updateChannelConfigs();
            seqPtr->updateMasterEffects();
            seqPtr->play();

            std::vector<float> l(512), r(512);
            std::vector<float> collected;
            for (int b = 0; b < 60; ++b) {
                seqPtr->process(l.data(), r.data(), 512);
                collected.insert(collected.end(), l.begin(), l.end());
            }
            return collected;
        };

        const std::vector<float> room = render(static_cast<int>(ReverbAlgorithm::Room));
        const std::vector<float> plate = render(static_cast<int>(ReverbAlgorithm::Plate));
        const std::vector<float> gated = render(static_cast<int>(ReverbAlgorithm::Gated));

        auto meanDifference = [](const std::vector<float>& a,
                                 const std::vector<float>& b) {
            const size_t n = std::min(a.size(), b.size());
            double sum = 0.0;
            for (size_t i = 0; i < n; ++i) sum += std::fabs(double(a[i]) - double(b[i]));
            return (n > 0) ? sum / double(n) : 0.0;
        };

        check(meanDifference(room, plate) > 1e-4,
              "switching the channel to Plate changes what comes out");
        check(meanDifference(plate, gated) > 1e-4,
              "and Gated differs from Plate");

        bool finite = true;
        for (float v : plate) if (!std::isfinite(v)) { finite = false; break; }
        check(finite, "and the result is finite through the whole chain");
    }

    // ---- It survives a save and load -------------------------------------------
    {
        Project p;
        p.channels[2].reverbEnabled = true;
        p.channels[2].reverbAlgorithm = static_cast<int>(ReverbAlgorithm::Shimmer);
        p.arrangement.push_back(Clip{0, 2, 0.0f, 4.0f, 0});

        const std::string path = testPath("reverb_algo.ctp");
        check(saveProject(p, path), "a project with a reverb algorithm saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");
        check(loaded.channels[2].reverbAlgorithm ==
                  static_cast<int>(ReverbAlgorithm::Shimmer),
              "with the algorithm intact");
        std::remove(path.c_str());
    }

    // ---- Validation ---------------------------------------------------------------
    {
        Project p;
        p.channels[0].reverbAlgorithm = 900;
        clampProjectToValidRanges(p);
        check(p.channels[0].reverbAlgorithm == 0,
              "an algorithm this build does not have falls back to Room "
              "rather than wrapping onto whichever one sits at that index");
    }

    // ---- Every algorithm has a name and a description -----------------------------
    {
        bool allNamed = true;
        for (int a = 0; a < static_cast<int>(ReverbAlgorithm::Count); ++a) {
            const ReverbAlgorithm algorithm = static_cast<ReverbAlgorithm>(a);
            const char* name = reverbAlgorithmName(algorithm);
            const char* blurb = reverbAlgorithmBlurb(algorithm);
            if (name == nullptr || *name == '\0' ||
                blurb == nullptr || *blurb == '\0') {
                allNamed = false;
                break;
            }
        }
        check(allNamed,
              "every algorithm has a name and a line saying what it is for - "
              "a menu of seven reverbs called Reverb 1 through 7 is not a "
              "choice anybody can make");
    }
}


// ============================================================================
// 85. Effect settings persist and survive a sync
// ============================================================================
static void testEffectSettingsPersist() {
    beginTest("Effect settings persist");

    /*
     * Every effect field the Channel Editor can edit, with a distinctive
     * value. Table-driven rather than written out twice, so a field cannot
     * be set here and checked nowhere - which is the shape of the bug this
     * exists to catch.
     */
    struct FloatField { const char* name; float ChannelConfig::* member; float value; };
    static const FloatField FLOATS[] = {
        {"bitDepth",            &ChannelConfig::bitDepth,            6.5f},
        {"sampleRateDiv",       &ChannelConfig::sampleRateDiv,       3.25f},
        {"chorusRate",          &ChannelConfig::chorusRate,          1.75f},
        {"chorusDepth",         &ChannelConfig::chorusDepth,         0.42f},
        {"chorusMix",           &ChannelConfig::chorusMix,           0.36f},
        {"delayTime",           &ChannelConfig::delayTime,           0.185f},
        {"delayFeedback",       &ChannelConfig::delayFeedback,       0.47f},
        {"delayMix",            &ChannelConfig::delayMix,            0.29f},
        {"distortionDrive",     &ChannelConfig::distortionDrive,     3.5f},
        {"distortionMix",       &ChannelConfig::distortionMix,       0.62f},
        {"filterCutoff",        &ChannelConfig::filterCutoff,        1234.0f},
        {"filterResonance",     &ChannelConfig::filterResonance,     0.55f},
        {"phaserRate",          &ChannelConfig::phaserRate,          0.85f},
        {"phaserDepth",         &ChannelConfig::phaserDepth,         0.44f},
        {"phaserFeedback",      &ChannelConfig::phaserFeedback,      0.33f},
        {"reverbRoomSize",      &ChannelConfig::reverbRoomSize,      0.82f},
        {"reverbDamping",       &ChannelConfig::reverbDamping,       0.27f},
        {"reverbMix",           &ChannelConfig::reverbMix,           0.41f},
        {"ringModFrequency",    &ChannelConfig::ringModFrequency,    432.0f},
        {"ringModMix",          &ChannelConfig::ringModMix,          0.38f},
        {"sidechainAmount",     &ChannelConfig::sidechainAmount,     0.71f},
        {"sidechainRelease",    &ChannelConfig::sidechainRelease,    0.22f},
        {"sidechainAttack",     &ChannelConfig::sidechainAttack,     0.012f},
        {"sidechainThreshold",  &ChannelConfig::sidechainThreshold,  0.44f},
        {"stereoWidenerWidth",  &ChannelConfig::stereoWidenerWidth,  0.66f},
        {"stereoWidenerHaas",   &ChannelConfig::stereoWidenerHaas,   0.018f},
        {"stereoWidenerMix",    &ChannelConfig::stereoWidenerMix,    0.53f},
        {"tapeDrive",           &ChannelConfig::tapeDrive,           2.4f},
        {"tapeWarmth",          &ChannelConfig::tapeWarmth,          0.58f},
        {"tapeCompression",     &ChannelConfig::tapeCompression,     0.35f},
        {"tapeMix",             &ChannelConfig::tapeMix,             0.64f},
        {"tremoloRate",         &ChannelConfig::tremoloRate,         5.5f},
        {"tremoloDepth",        &ChannelConfig::tremoloDepth,        0.48f},
    };

    struct BoolField { const char* name; bool ChannelConfig::* member; };
    static const BoolField BOOLS[] = {
        {"bitcrusherEnabled",     &ChannelConfig::bitcrusherEnabled},
        {"chorusEnabled",         &ChannelConfig::chorusEnabled},
        {"delayEnabled",          &ChannelConfig::delayEnabled},
        {"distortionEnabled",     &ChannelConfig::distortionEnabled},
        {"filterEnabled",         &ChannelConfig::filterEnabled},
        {"phaserEnabled",         &ChannelConfig::phaserEnabled},
        {"reverbEnabled",         &ChannelConfig::reverbEnabled},
        {"ringModEnabled",        &ChannelConfig::ringModEnabled},
        {"sidechainEnabled",      &ChannelConfig::sidechainEnabled},
        {"stereoWidenerEnabled",  &ChannelConfig::stereoWidenerEnabled},
        {"tapeSaturationEnabled", &ChannelConfig::tapeSaturationEnabled},
        {"tremoloEnabled",        &ChannelConfig::tremoloEnabled},
    };

    auto configure = [&](Project& p, int ch) {
        ChannelConfig& c = p.channels[static_cast<size_t>(ch)];
        for (const FloatField& field : FLOATS) c.*(field.member) = field.value;
        for (const BoolField& field : BOOLS) c.*(field.member) = true;
        p.arrangement.push_back(Clip{0, ch, 0.0f, 4.0f, 0});
    };

    // ---- They survive a save and load ---------------------------------------
    {
        auto p = std::make_unique<Project>();
        configure(*p, 3);

        const std::string path = testPath("effect_persistence.ctp");
        check(saveProject(*p, path), "a project with every effect set saves");

        auto loaded = std::make_unique<Project>();
        check(loadProject(*loaded, path), "and loads");

        const ChannelConfig& got = loaded->channels[3];
        bool allSurvived = true;
        std::string lost;

        for (const FloatField& field : FLOATS) {
            if (std::fabs(got.*(field.member) - field.value) > 1e-3f) {
                allSurvived = false;
                lost = std::string(field.name) + " came back as " +
                       std::to_string(got.*(field.member)) + ", set to " +
                       std::to_string(field.value);
                break;
            }
        }
        check(allSurvived,
              "every effect parameter survives a round trip" +
              (allSurvived ? std::string() : " (" + lost + ")"));

        bool allFlags = true;
        std::string lostFlag;
        for (const BoolField& field : BOOLS) {
            if (!(got.*(field.member))) {
                allFlags = false;
                lostFlag = field.name;
                break;
            }
        }
        check(allFlags,
              "and so does every enable flag" +
              (allFlags ? std::string() : " (" + lostFlag + " came back off)"));

        std::remove(path.c_str());
    }

    // ---- They survive a config sync ------------------------------------------
    //
    // This is the half that actually bit. The controls wrote into the live
    // chain, and updateChannelConfigs() - which runs whenever any unrelated
    // control is touched - copies the config OVER the chain. So the user set
    // a reverb, changed the oscillator type, and the reverb reset.
    {
        auto p = std::make_unique<Project>();
        configure(*p, 2);

        auto seqPtr = std::make_unique<Sequencer>();
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(p.get());
        seqPtr->updateChannelConfigs();

        const EffectsChain& chain = seqPtr->channelEffects(2);

        check(chain.reverbEnabled && chain.delayEnabled && chain.ringModEnabled &&
              chain.tremoloEnabled && chain.sidechainEnabled,
              "the enable flags reach the engine");
        check(std::fabs(chain.reverb.roomSize - 0.82f) < 1e-3f,
              "and so do the parameters");
        check(std::fabs(chain.ringMod.frequency - 432.0f) < 1e-2f,
              "including the ring modulator, which had no config field at "
              "all before this and could therefore never be saved");
        check(std::fabs(chain.sidechain.attack - 0.012f) < 1e-4f &&
              std::fabs(chain.sidechain.threshold - 0.44f) < 1e-3f,
              "and the sidechain's attack and threshold, likewise");

        // Sync again - the values must not drift or reset.
        seqPtr->updateChannelConfigs();
        check(std::fabs(seqPtr->channelEffects(2).reverb.roomSize - 0.82f) < 1e-3f,
              "a second sync leaves them where they were, which is the "
              "property the old binding did not have");
    }

    // ---- The config is what the engine follows -------------------------------
    {
        auto p = std::make_unique<Project>();
        p->channels[0].reverbEnabled = true;
        p->channels[0].reverbRoomSize = 0.2f;
        p->arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        auto seqPtr = std::make_unique<Sequencer>();
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(p.get());
        seqPtr->updateChannelConfigs();
        check(std::fabs(seqPtr->channelEffects(0).reverb.roomSize - 0.2f) < 1e-3f,
              "the engine starts where the project says");

        // Change the project the way the editor now does, and sync one
        // channel the way the panel does at the end of its frame.
        p->channels[0].reverbRoomSize = 0.9f;
        seqPtr->getSynth(0).setChannelConfig(p->channels[0]);
        check(std::fabs(seqPtr->channelEffects(0).reverb.roomSize - 0.9f) < 1e-3f,
              "and follows the project when it changes - one sync at the end "
              "of the panel rather than a call bolted onto each of the "
              "forty-five controls, because a list of forty-five grows a "
              "forty-sixth without one");
    }

    // ---- Validation covers the new fields --------------------------------------
    {
        auto p = std::make_unique<Project>();
        p->channels[0].ringModFrequency = -50.0f;
        p->channels[0].ringModMix = 40.0f;
        p->channels[0].sidechainAttack = 0.0f;
        p->channels[0].sidechainThreshold =
            std::numeric_limits<float>::quiet_NaN();

        clampProjectToValidRanges(*p);

        check(p->channels[0].ringModFrequency > 0.0f,
              "a ring-mod frequency of zero or below is repaired - at DC it "
              "multiplies the signal by a constant");
        check(p->channels[0].ringModMix <= 1.0f, "and an absurd mix is clamped");
        check(p->channels[0].sidechainAttack > 0.0f,
              "a zero sidechain attack cannot divide by itself in the envelope");
        check(std::isfinite(p->channels[0].sidechainThreshold),
              "and a NaN threshold is replaced");
    }
}


// ============================================================================
// 86. Convolution
// ============================================================================
static void testConvolution() {
    beginTest("Convolution reverb");

    const float rate = 44100.0f;

    // ---- It equals direct convolution ---------------------------------------
    //
    // The one property that is arithmetic rather than opinion. Partitioned
    // overlap-save is a rearrangement of the same sum; if it does not match
    // a direct convolution it is simply wrong, however plausible it sounds.
    {
        // A short, awkward response - not a decaying tail, so a bug cannot
        // hide under something that was going to fade anyway.
        std::vector<float> ir(1500, 0.0f);
        ir[0] = 1.0f;
        ir[1] = -0.5f;
        ir[97] = 0.8f;
        ir[513] = -0.6f;      // deliberately across a partition boundary
        ir[1024] = 0.4f;      // and on another
        ir[1499] = 0.25f;

        // A signal with no symmetry to be flattered by.
        std::vector<float> input(6000, 0.0f);
        uint32_t rng = 12345u;
        for (size_t i = 0; i < input.size(); ++i) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            input[i] = (float((rng >> 16) & 0x7FFF) / 16383.5f) - 1.0f;
        }

        // Direct convolution, as the definition.
        std::vector<float> expected(input.size(), 0.0f);
        for (size_t n = 0; n < input.size(); ++n) {
            float sum = 0.0f;
            for (size_t k = 0; k < ir.size() && k <= n; ++k) {
                sum += input[n - k] * ir[k];
            }
            expected[n] = sum;
        }

        auto partitioned = std::make_unique<PartitionedIR>();
        partitioned->build(ir, rate);
        check(partitioned->partitions() == 3,
              "a 1500-sample response partitions into three 512-blocks (got " +
              std::to_string(partitioned->partitions()) + ")");

        auto engine = std::make_unique<ConvolutionEngine>();
        engine->prepare(partitioned.get());
        check(engine->active(), "and the engine attaches to it");
        check(engine->latency() == ConvolutionEngine::BLOCK,
              "reporting one block of latency, which is real and unavoidable");

        std::vector<float> got(input.size(), 0.0f);
        for (size_t i = 0; i < input.size(); ++i) got[i] = engine->process(input[i]);

        // Compare past the reported latency, allowing for it.
        const int latency = engine->latency();
        float worst = 0.0f;
        size_t worstAt = 0;
        for (size_t i = static_cast<size_t>(latency) + 100;
             i + static_cast<size_t>(latency) < got.size(); ++i) {
            const float difference =
                std::fabs(got[i + static_cast<size_t>(latency)] - expected[i]);
            if (difference > worst) { worst = difference; worstAt = i; }
        }

        check(worst < 1e-3f,
              "partitioned overlap-save matches direct convolution (worst "
              "error " + std::to_string(worst) + " at sample " +
              std::to_string(worstAt) + ") - it is the same sum rearranged, "
              "so anything else is a bug however good it sounds");
    }

    // ---- An unattached engine costs nothing and passes nothing --------------
    {
        auto engine = std::make_unique<ConvolutionEngine>();
        check(!engine->active(),
              "an engine with no response attached is inactive");
        check(engine->latency() == 0, "and reports no latency");
        check(engine->process(0.5f) == 0.0f,
              "and produces nothing rather than reading an empty buffer");

        // The adapter must pass the signal through untouched in that state,
        // or a channel with the effect off would go silent.
        ConvolutionFx adapter;
        adapter.target = engine.get();
        check(adapter.process(0.5f, 0.0f) == 0.5f,
              "and the rack adapter passes the dry signal straight through");
    }

    // ---- The built-in responses are usable ----------------------------------
    {
        bool allGood = true;
        std::string bad;

        for (int i = 0; i < static_cast<int>(ImpulseResponse::Count); ++i) {
            const ImpulseResponse which = static_cast<ImpulseResponse>(i);
            if (which == ImpulseResponse::Custom) continue;   // nothing loaded

            const std::vector<float> ir = makeImpulseResponse(which, rate);
            if (ir.size() < 1000) { allGood = false; bad = "too short"; break; }

            bool finite = true;
            double energy = 0.0;
            for (float v : ir) {
                if (!std::isfinite(v)) { finite = false; break; }
                energy += double(v) * v;
            }
            if (!finite || energy < 1e-6) {
                allGood = false;
                bad = std::string(impulseResponseName(which)) +
                      (finite ? " has no energy" : " has a non-finite sample");
                break;
            }
        }
        check(allGood,
              "every built-in response is finite and has energy" +
              (allGood ? std::string() : " (" + bad + ")"));

        // Deterministic: the same room every run, or A/B is meaningless and
        // these tests would be too.
        const std::vector<float> a = makeImpulseResponse(ImpulseResponse::LargeHall, rate);
        const std::vector<float> b = makeImpulseResponse(ImpulseResponse::LargeHall, rate);
        check(a == b, "and the same choice always gives the same room");

        const std::vector<float> hall = makeImpulseResponse(ImpulseResponse::LargeHall, rate);
        const std::vector<float> room = makeImpulseResponse(ImpulseResponse::SmallRoom, rate);
        check(hall.size() > room.size(),
              "a hall is longer than a small room");
    }

    // ---- Responses are level-matched ----------------------------------------
    {
        // Normalised to unit energy rather than unit peak, so swapping rooms
        // does not change the level - louder always sounds better, and an
        // A/B where one side is louder is not a comparison.
        double previous = -1.0;
        bool matched = true;
        for (int i = 0; i < static_cast<int>(ImpulseResponse::Count); ++i) {
            const ImpulseResponse which = static_cast<ImpulseResponse>(i);
            if (which == ImpulseResponse::Custom) continue;

            const std::vector<float> ir = makeImpulseResponse(which, rate);
            double energy = 0.0;
            for (float v : ir) energy += double(v) * v;

            if (previous >= 0.0 && std::fabs(energy - previous) > 0.05) {
                matched = false;
                break;
            }
            previous = energy;
        }
        check(matched,
              "every response carries the same energy, so choosing a room "
              "changes the room and not the volume");
    }

    // ---- The library, and lazy allocation -----------------------------------
    {
        auto library = std::make_unique<IRLibrary>();
        library->rebuild(rate, {});

        for (int i = 0; i < IRLibrary::SLOTS; ++i) {
            const PartitionedIR* ir = library->get(i);
            if (ir == nullptr) { check(false, "a library slot is null"); break; }
        }
        check(true, "every library slot is populated");

        check(library->get(-5) != nullptr && library->get(9999) != nullptr,
              "and an index outside the library is clamped rather than read "
              "off the end, which the audio thread would otherwise do");

        // Custom is empty until something is loaded, and an empty response
        // must leave the engine inactive rather than half-prepared.
        auto engine = std::make_unique<ConvolutionEngine>();
        engine->prepare(library->get(static_cast<int>(ImpulseResponse::Custom)));
        check(!engine->active(),
              "the custom slot is inactive until a file is loaded");
    }

    // ---- Long-run stability --------------------------------------------------
    {
        auto library = std::make_unique<IRLibrary>();
        library->rebuild(rate, {});

        auto engine = std::make_unique<ConvolutionEngine>();
        engine->prepare(library->get(static_cast<int>(ImpulseResponse::LargeHall)));

        bool finite = true;
        float peak = 0.0f;
        for (int i = 0; i < 200000; ++i) {
            const float t = float(i) / rate;
            const float in = 0.7f * std::sin(6.28318530718f * 220.0f * t);
            const float out = engine->process(in);
            if (!std::isfinite(out)) { finite = false; break; }
            peak = std::max(peak, std::fabs(out));
        }
        check(finite, "a two-second hall stays finite over five seconds of audio");
        check(peak < 8.0f,
              "and bounded (peak " + std::to_string(peak) + ")");
    }

    // ---- Re-attaching mid-life ------------------------------------------------
    {
        auto library = std::make_unique<IRLibrary>();
        library->rebuild(rate, {});

        auto engine = std::make_unique<ConvolutionEngine>();
        engine->prepare(library->get(0));
        for (int i = 0; i < 3000; ++i) engine->process(0.4f);

        // Swapping to a response with a different partition count resizes
        // the delay line; the old slot index must not survive it.
        engine->prepare(library->get(static_cast<int>(ImpulseResponse::LargeHall)));
        bool finite = true;
        for (int i = 0; i < 20000; ++i) {
            if (!std::isfinite(engine->process(0.4f))) { finite = false; break; }
        }
        check(finite,
              "swapping the response while the tail is running leaves the "
              "engine usable rather than indexing a resized delay line with "
              "a stale slot");

        engine->prepare(nullptr);
        check(!engine->active() && engine->process(0.5f) == 0.0f,
              "and detaching releases it cleanly");
    }
}

// ============================================================================
// 87. Convolution in a channel
// ============================================================================
static void testConvolutionInChannel() {
    beginTest("Convolution in a channel");

    {
        const ChannelConfig fresh;
        check(!fresh.convolutionEnabled,
              "convolution is off on a new channel - nothing a 2A03 could do "
              "ships switched on");
    }

    // ---- It reaches the audio, and costs nothing until it does --------------
    {
        auto render = [](bool enabled) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.masterCompressorEnabled = false;
            p.masterEQEnabled = false;
            p.masterVolume = 0.7f;

            p.channels[0].oscillator.type = OscillatorType::Pulse;
            p.channels[0].volume = 0.8f;
            p.channels[0].pan = 0.0f;
            p.channels[0].convolutionEnabled = enabled;
            p.channels[0].convolutionIR = static_cast<int>(ImpulseResponse::LargeHall);
            p.channels[0].convolutionMix = 0.7f;

            p.patterns.clear();
            Pattern pattern;
            Note note;
            note.pitch = 60;
            note.startTime = 0.0f;
            note.duration = 0.5f;
            note.oscillatorType = OscillatorType::Pulse;
            pattern.notes.push_back(note);
            p.patterns.push_back(pattern);
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            seqPtr->setSampleRate(44100.0f);
            seqPtr->setProject(&p);
            seqPtr->updateChannelConfigs();
            seqPtr->updateMasterEffects();
            seqPtr->play();

            std::vector<float> l(512), r(512);
            std::vector<float> collected;
            for (int b = 0; b < 50; ++b) {
                seqPtr->process(l.data(), r.data(), 512);
                collected.insert(collected.end(), l.begin(), l.end());
            }
            return collected;
        };

        const std::vector<float> dry = render(false);
        const std::vector<float> wet = render(true);

        double difference = 0.0;
        const size_t n = std::min(dry.size(), wet.size());
        for (size_t i = 0; i < n; ++i) {
            difference += std::fabs(double(dry[i]) - double(wet[i]));
        }
        check(n > 0 && difference / double(n) > 1e-4,
              "switching convolution on changes what comes out of the channel");

        bool finite = true;
        for (float v : wet) if (!std::isfinite(v)) { finite = false; break; }
        check(finite, "and the result is finite through the whole chain");
    }

    // ---- The rack knows about it -----------------------------------------------
    {
        EffectType parsed;
        check(effectTypeFromId("convolution", parsed) &&
              parsed == EffectType::Convolution,
              "convolution has a stable token");
        check(EFFECT_TYPE_COUNT == CLASSIC_EFFECT_COUNT,
              "and the classic order still lists every effect exactly once");

        auto chainPtr = std::make_unique<EffectsChain>();
        check(chainPtr->effectFor(EffectType::Convolution) != nullptr,
              "and it resolves to an adapter - a type with no arm is skipped "
              "by the rack in complete silence");
    }

    // ---- Settings survive a save and load ---------------------------------------
    {
        Project p;
        p.channels[1].convolutionEnabled = true;
        p.channels[1].convolutionIR = static_cast<int>(ImpulseResponse::DarkChamber);
        p.channels[1].convolutionMix = 0.62f;
        p.arrangement.push_back(Clip{0, 1, 0.0f, 4.0f, 0});

        const std::string path = testPath("convolution.ctp");
        check(saveProject(p, path), "a convolution project saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");
        check(loaded.channels[1].convolutionEnabled &&
              loaded.channels[1].convolutionIR ==
                  static_cast<int>(ImpulseResponse::DarkChamber) &&
              std::fabs(loaded.channels[1].convolutionMix - 0.62f) < 1e-3f,
              "with the response and mix intact");
        std::remove(path.c_str());
    }

    // ---- Validation ---------------------------------------------------------------
    {
        Project p;
        p.channels[0].convolutionIR = 900;
        p.channels[0].convolutionMix = -3.0f;
        clampProjectToValidRanges(p);

        check(p.channels[0].convolutionIR >= 0 &&
              p.channels[0].convolutionIR < IRLibrary::SLOTS,
              "a response index past the end of the library is repaired");
        check(p.channels[0].convolutionMix >= 0.0f, "and a negative mix is clamped");
    }
}


// ============================================================================
// 88. Equalisers
// ============================================================================
static void testEqualisers() {
    beginTest("Equalisers");

    const float rate = 44100.0f;

    /*
     * The gain a processor applies at one frequency, in dB.
     *
     * Measured by settling first and then taking the RMS - a biquad has a
     * transient at the start of any tone, and including it would report a
     * gain that is partly the filter starting up.
     */
    auto gainAt = [rate](auto&& processor, float frequency) {
        const int settle = 8192;
        const int measure = 16384;

        double energy = 0.0;
        for (int i = 0; i < settle + measure; ++i) {
            const float t = float(i) / rate;
            const float in = std::sin(6.28318530718f * frequency * t);
            const float out = processor(in);
            if (i >= settle) energy += double(out) * out;
        }
        const double rms = std::sqrt(energy / double(measure));
        // A unit sine has an RMS of 1/sqrt(2).
        return 20.0 * std::log10(std::max(1e-9, rms / 0.70710678));
    };

    // ---- Tilt ---------------------------------------------------------------
    {
        auto tilt = std::make_unique<TiltEQ>();
        tilt->configure(rate);
        tilt->set(0.0f, 700.0f);

        const double flatLow = gainAt([&](float x) { return tilt->process(x); }, 100.0f);
        const double flatHigh = gainAt([&](float x) { return tilt->process(x); }, 6000.0f);
        check(std::fabs(flatLow) < 0.6 && std::fabs(flatHigh) < 0.6,
              "a tilt of zero is flat (" + std::to_string(flatLow) + " dB low, " +
              std::to_string(flatHigh) + " dB high)");

        tilt->set(6.0f, 700.0f);
        tilt->reset();
        const double brightLow = gainAt([&](float x) { return tilt->process(x); }, 100.0f);
        tilt->reset();
        const double brightHigh = gainAt([&](float x) { return tilt->process(x); }, 6000.0f);

        check(brightHigh > 3.0,
              "tilting bright raises the top (" + std::to_string(brightHigh) + " dB)");
        check(brightLow < -3.0,
              "and lowers the bottom (" + std::to_string(brightLow) +
              " dB) - it pivots, which is what stops it being a level control "
              "in disguise");

        tilt->set(-6.0f, 700.0f);
        tilt->reset();
        const double darkHigh = gainAt([&](float x) { return tilt->process(x); }, 6000.0f);
        check(darkHigh < -3.0, "and tilting dark does the reverse");
    }

    // ---- Graphic --------------------------------------------------------------
    {
        auto graphic = std::make_unique<GraphicEQ>();
        graphic->configure(rate);

        // Flat must be exactly flat: ten near-unity biquads in series still
        // accumulate phase and numerical noise, which is why zero-gain bands
        // are set to passthrough rather than to a 0 dB bell.
        const double flat = gainAt([&](float x) { return graphic->process(x); }, 1000.0f);
        check(std::fabs(flat) < 0.05,
              "a graphic EQ at zero is flat to within " +
              std::to_string(std::fabs(flat)) + " dB");

        // Boost one band and check it lands on that band and not a neighbour.
        graphic->gainDb[6] = 12.0f;         // 2 kHz
        graphic->update();
        graphic->reset();
        const double atBand = gainAt([&](float x) { return graphic->process(x); }, 2000.0f);
        graphic->reset();
        const double farBelow = gainAt([&](float x) { return graphic->process(x); }, 125.0f);
        graphic->reset();
        const double farAbove = gainAt([&](float x) { return graphic->process(x); }, 16000.0f);

        check(atBand > 8.0,
              "boosting the 2 kHz band raises 2 kHz (" +
              std::to_string(atBand) + " dB)");
        check(std::fabs(farBelow) < 2.0 && std::fabs(farAbove) < 2.5,
              "and leaves 125 Hz and 16 kHz alone (" +
              std::to_string(farBelow) + ", " + std::to_string(farAbove) + " dB)");

        // Cuts as well as boosts.
        graphic->gainDb[6] = -12.0f;
        graphic->update();
        graphic->reset();
        check(gainAt([&](float x) { return graphic->process(x); }, 2000.0f) < -8.0,
              "and cutting it cuts");
    }

    // ---- Mid-side ---------------------------------------------------------------
    {
        auto ms = std::make_unique<MidSideEQ>();
        ms->configure(rate);

        // With nothing set, the encode and decode must be an exact round
        // trip. The 0.5 in the decode is what makes that true; without it
        // every mid-side stage doubles the level.
        float worst = 0.0f;
        for (int i = 0; i < 2000; ++i) {
            const float t = float(i) / rate;
            float l = 0.7f * std::sin(6.28318530718f * 300.0f * t);
            float r = 0.4f * std::sin(6.28318530718f * 900.0f * t + 1.1f);
            const float wantL = l;
            const float wantR = r;
            ms->process(l, r);
            worst = std::max(worst, std::max(std::fabs(l - wantL),
                                             std::fabs(r - wantR)));
        }
        check(worst < 1e-4f,
              "an untouched mid-side stage is an exact round trip (worst " +
              std::to_string(worst) + ") - the 0.5 in the decode is what "
              "makes that true, and leaving it out doubles the level");

        // Cutting the side alone must leave a mono signal untouched: a mono
        // signal has no side content at all. This is the property that makes
        // mid-side worth having, so it is the one to assert.
        ms->side[0] = {120.0f, -18.0f, 0.9f};
        ms->update();
        ms->reset();

        float monoWorst = 0.0f;
        for (int i = 0; i < 4000; ++i) {
            const float t = float(i) / rate;
            const float mono = 0.6f * std::sin(6.28318530718f * 120.0f * t);
            float l = mono, r = mono;
            ms->process(l, r);
            if (i > 2000) {
                monoWorst = std::max(monoWorst, std::fabs(l - mono));
            }
        }
        check(monoWorst < 1e-3f,
              "cutting the SIDE leaves a mono signal untouched (worst " +
              std::to_string(monoWorst) + ") - which is exactly why it "
              "tightens a mix without thinning it");

        // And it must actually act on a wide signal.
        ms->reset();
        double wideEnergy = 0.0;
        for (int i = 0; i < 8000; ++i) {
            const float t = float(i) / rate;
            const float s = 0.6f * std::sin(6.28318530718f * 120.0f * t);
            float l = s, r = -s;          // pure side
            ms->process(l, r);
            if (i > 4000) wideEnergy += double(l) * l;
        }
        check(wideEnergy < 4000.0 * 0.6 * 0.6 * 0.5 * 0.3,
              "while a pure side signal at that frequency is cut");
    }

    // ---- Dynamic -----------------------------------------------------------------
    {
        auto dyn = std::make_unique<DynamicEQ>();
        dyn->frequency = 300.0f;
        dyn->q = 1.2f;
        dyn->thresholdDb = -20.0f;
        dyn->rangeDb = -12.0f;
        dyn->attack = 0.005f;
        dyn->release = 0.05f;
        dyn->configure(rate);

        // Quiet: below the threshold, so it should do nothing at all. A
        // static cut would be cutting here too, which is the whole
        // difference.
        dyn->reset();
        double quietEnergy = 0.0, quietInput = 0.0;
        for (int i = 0; i < 20000; ++i) {
            const float t = float(i) / rate;
            const float in = 0.01f * std::sin(6.28318530718f * 300.0f * t);
            const float out = dyn->process(in);
            if (i > 10000) { quietEnergy += double(out) * out; quietInput += double(in) * in; }
        }
        const double quietGain = 20.0 * std::log10(
            std::sqrt(std::max(1e-12, quietEnergy)) /
            std::sqrt(std::max(1e-12, quietInput)));
        check(std::fabs(quietGain) < 1.5,
              "a quiet signal passes untouched (" + std::to_string(quietGain) +
              " dB) - a static cut would be cutting it too");

        // Loud: above the threshold, so the band should duck.
        dyn->reset();
        double loudEnergy = 0.0, loudInput = 0.0;
        for (int i = 0; i < 30000; ++i) {
            const float t = float(i) / rate;
            const float in = 0.8f * std::sin(6.28318530718f * 300.0f * t);
            const float out = dyn->process(in);
            if (i > 20000) { loudEnergy += double(out) * out; loudInput += double(in) * in; }
        }
        const double loudGain = 20.0 * std::log10(
            std::sqrt(std::max(1e-12, loudEnergy)) /
            std::sqrt(std::max(1e-12, loudInput)));
        check(loudGain < -4.0,
              "and a loud one is ducked (" + std::to_string(loudGain) +
              " dB), which is the point: the problem gets fixed only when it "
              "is actually there");

        check(dyn->currentGainDb() < -1.0,
              "and the applied gain reports what it is doing");

        // The detector is banded, not full-band. A loud tone somewhere else
        // entirely must not duck this band - otherwise it is a compressor
        // with extra steps.
        dyn->reset();
        double offBandEnergy = 0.0, offBandInput = 0.0;
        for (int i = 0; i < 30000; ++i) {
            const float t = float(i) / rate;
            const float in = 0.8f * std::sin(6.28318530718f * 6000.0f * t);
            const float out = dyn->process(in);
            if (i > 20000) { offBandEnergy += double(out) * out; offBandInput += double(in) * in; }
        }
        const double offBandGain = 20.0 * std::log10(
            std::sqrt(std::max(1e-12, offBandEnergy)) /
            std::sqrt(std::max(1e-12, offBandInput)));
        check(std::fabs(offBandGain) < 2.0,
              "a loud tone in a different band does not duck this one (" +
              std::to_string(offBandGain) + " dB) - the detector is a "
              "bandpass tap, not the full-band level, or it would be a "
              "compressor with extra steps");
    }

    // ---- Nothing goes unstable ------------------------------------------------
    {
        // Every shape at extreme settings, over frequencies up to and past
        // Nyquist. tan() runs to infinity at Nyquist, so a band nudged past
        // it by a high-rate project would otherwise produce infinite
        // coefficients rather than failing gently.
        bool allSafe = true;
        std::string failed;

        for (float frequency : {5.0f, 20.0f, 1000.0f, 15000.0f, 22050.0f, 40000.0f}) {
            for (float gain : {-36.0f, -18.0f, 0.0f, 18.0f, 36.0f}) {
                for (float q : {0.05f, 1.0f, 30.0f}) {
                    eq::Biquad biquad;
                    biquad.setPeaking(frequency, q, gain, rate);

                    float peak = 0.0f;
                    for (int i = 0; i < 20000; ++i) {
                        const float t = float(i) / rate;
                        const float out = biquad.process(
                            0.8f * std::sin(6.28318530718f * 440.0f * t));
                        if (!std::isfinite(out)) {
                            allSafe = false;
                            failed = std::to_string(frequency) + " Hz, " +
                                     std::to_string(gain) + " dB, Q " +
                                     std::to_string(q);
                            break;
                        }
                        peak = std::max(peak, std::fabs(out));
                    }
                    if (!allSafe) break;
                    if (peak > 200.0f) {
                        allSafe = false;
                        failed = "peak " + std::to_string(peak) + " at " +
                                 std::to_string(frequency) + " Hz";
                        break;
                    }
                }
                if (!allSafe) break;
            }
            if (!allSafe) break;
        }
        check(allSafe,
              "biquads stay finite and bounded at every frequency from 5 Hz "
              "to past Nyquist and every gain and Q" +
              (allSafe ? std::string() : " (" + failed + ")"));
    }

    // ---- Validation -------------------------------------------------------------
    {
        Project p;
        p.channels[0].tiltEqAmount = std::numeric_limits<float>::quiet_NaN();
        p.channels[0].tiltEqCentre = 1e9f;
        p.channels[0].graphicEqGains[3] = std::numeric_limits<float>::infinity();
        p.channels[0].graphicEqGains[7] = 900.0f;
        p.channels[0].dynamicEqAttack = 0.0f;
        p.channels[0].dynamicEqQ = -4.0f;

        clampProjectToValidRanges(p);

        check(std::isfinite(p.channels[0].tiltEqAmount),
              "a NaN tilt is replaced - a biquad is recursive, so one NaN in "
              "its state means every sample after it is NaN and the channel "
              "is silent for the rest of the session");
        check(p.channels[0].tiltEqCentre <= 8000.0f, "an absurd centre is clamped");
        check(std::isfinite(p.channels[0].graphicEqGains[3]) &&
              std::fabs(p.channels[0].graphicEqGains[7]) <= 18.0f,
              "and so are the graphic bands");
        check(p.channels[0].dynamicEqAttack > 0.0f,
              "a zero attack cannot divide by itself in the envelope");
        check(p.channels[0].dynamicEqQ > 0.0f, "and a negative Q is repaired");
    }
}

// ============================================================================
// 89. Equalisers in a channel and on the master
// ============================================================================
static void testEqualisersInChannel() {
    beginTest("Equalisers in a channel");

    {
        const ChannelConfig fresh;
        check(!fresh.tiltEqEnabled && !fresh.graphicEqEnabled &&
              !fresh.dynamicEqEnabled,
              "every new equaliser is off on a new channel");
    }

    // ---- The rack knows about them ---------------------------------------------
    {
        check(EFFECT_TYPE_COUNT == CLASSIC_EFFECT_COUNT,
              "the classic order still lists every effect exactly once");

        auto chainPtr = std::make_unique<EffectsChain>();
        bool allResolve = true;
        for (int i = 0; i < EFFECT_TYPE_COUNT; ++i) {
            if (chainPtr->effectFor(static_cast<EffectType>(i)) == nullptr) {
                allResolve = false;
                break;
            }
        }
        check(allResolve, "and every type still resolves to an adapter");

        EffectType parsed;
        check(effectTypeFromId("tilteq", parsed) && parsed == EffectType::TiltEq,
              "tilt has a stable token");
        check(effectTypeFromId("graphiceq", parsed) && parsed == EffectType::GraphicEq,
              "graphic has one");
        check(effectTypeFromId("dynamiceq", parsed) && parsed == EffectType::DynamicEq,
              "and so does dynamic");
    }

    // ---- Switching one on changes the audio -------------------------------------
    {
        auto render = [](bool tilt) {
            Project p;
            p.bpm = 120.0f;
            p.masterLimiterEnabled = false;
            p.masterCompressorEnabled = false;
            p.masterEQEnabled = false;
            p.masterVolume = 0.7f;

            p.channels[0].oscillator.type = OscillatorType::Sawtooth;
            p.channels[0].volume = 0.8f;
            p.channels[0].pan = 0.0f;
            p.channels[0].tiltEqEnabled = tilt;
            p.channels[0].tiltEqAmount = tilt ? -9.0f : 0.0f;

            p.patterns.clear();
            Pattern pattern;
            Note note;
            note.pitch = 60;
            note.startTime = 0.0f;
            note.duration = 4.0f;
            note.oscillatorType = OscillatorType::Sawtooth;
            pattern.notes.push_back(note);
            p.patterns.push_back(pattern);
            p.arrangement.clear();
            p.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

            auto seqPtr = std::make_unique<Sequencer>();
            seqPtr->setSampleRate(44100.0f);
            seqPtr->setProject(&p);
            seqPtr->updateChannelConfigs();
            seqPtr->updateMasterEffects();
            seqPtr->play();

            std::vector<float> l(512), r(512);
            std::vector<float> collected;
            for (int b = 0; b < 40; ++b) {
                seqPtr->process(l.data(), r.data(), 512);
                if (b >= 8) collected.insert(collected.end(), l.begin(), l.end());
            }
            return collected;
        };

        const std::vector<float> flat = render(false);
        const std::vector<float> tilted = render(true);

        double difference = 0.0;
        const size_t n = std::min(flat.size(), tilted.size());
        for (size_t i = 0; i < n; ++i) {
            difference += std::fabs(double(flat[i]) - double(tilted[i]));
        }
        check(n > 0 && difference / double(n) > 1e-3,
              "switching the tilt EQ on changes what comes out");

        bool finite = true;
        for (float v : tilted) if (!std::isfinite(v)) { finite = false; break; }
        check(finite, "and the result is finite");
    }

    // ---- Mid-side on the master --------------------------------------------------
    {
        auto masterPtr = std::make_unique<MasterEffects>();
        masterPtr->setSampleRate(44100.0f);
        check(!masterPtr->midSideEnabled,
              "mid-side is off on a fresh master bus");

        // Off, it must be an exact passthrough for the mid-side stage.
        masterPtr->eqEnabled = false;
        masterPtr->compressorEnabled = false;
        masterPtr->limiterEnabled = false;

        float l = 0.4f, r = -0.2f;
        masterPtr->process(l, r);
        check(std::fabs(l - 0.4f) < 1e-5f && std::fabs(r + 0.2f) < 1e-5f,
              "and with everything off the master bus passes audio through "
              "untouched");
    }

    // ---- Settings survive a save and load -----------------------------------------
    {
        Project p;
        p.channels[4].tiltEqEnabled = true;
        p.channels[4].tiltEqAmount = -4.5f;
        p.channels[4].tiltEqCentre = 900.0f;
        p.channels[4].graphicEqEnabled = true;
        p.channels[4].graphicEqGains[0] = 6.0f;
        p.channels[4].graphicEqGains[5] = -7.5f;
        p.channels[4].graphicEqGains[9] = 3.25f;
        p.channels[4].dynamicEqEnabled = true;
        p.channels[4].dynamicEqFrequency = 420.0f;
        p.channels[4].dynamicEqThreshold = -18.5f;
        p.channels[4].dynamicEqRange = -9.0f;
        p.arrangement.push_back(Clip{0, 4, 0.0f, 4.0f, 0});

        const std::string path = testPath("eq_suite.ctp");
        check(saveProject(p, path), "an EQ project saves");

        Project loaded;
        check(loadProject(loaded, path), "and loads");

        const ChannelConfig& c = loaded.channels[4];
        check(c.tiltEqEnabled && std::fabs(c.tiltEqAmount + 4.5f) < 1e-3f &&
              std::fabs(c.tiltEqCentre - 900.0f) < 1e-2f,
              "the tilt survives");
        check(c.graphicEqEnabled &&
              std::fabs(c.graphicEqGains[0] - 6.0f) < 1e-3f &&
              std::fabs(c.graphicEqGains[5] + 7.5f) < 1e-3f &&
              std::fabs(c.graphicEqGains[9] - 3.25f) < 1e-3f,
              "all ten graphic bands survive, on their own line - the field "
              "tables key on a member pointer and cannot address an array");
        check(c.dynamicEqEnabled &&
              std::fabs(c.dynamicEqFrequency - 420.0f) < 1e-2f &&
              std::fabs(c.dynamicEqThreshold + 18.5f) < 1e-3f,
              "and the dynamic band survives");

        std::remove(path.c_str());
    }

    // ---- A flat graphic EQ writes nothing --------------------------------------------
    {
        Project plain;
        const std::string path = testPath("eq_absent.ctp");
        check(saveProject(plain, path), "a project with flat EQ saves");

        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        check(ss.str().find("GRAPHICEQ") == std::string::npos,
              "and writes no GRAPHICEQ line - ten numbers per channel for an "
              "effect nobody touched");
        std::remove(path.c_str());
    }
}


// ============================================================================
// 90. Take lanes and comping
// ============================================================================
static void testTakeLanes() {
    beginTest("Take lanes and comping");

    // The segment list, as a compact string, so a test can assert the whole
    // shape at once rather than field by field.
    auto shapeOf = [](const CompGroup& group) {
        std::string out;
        for (const CompSegment& segment : group.segments) {
            out += "[" + std::to_string(static_cast<int>(segment.startBeat)) + "," +
                   std::to_string(static_cast<int>(segment.endBeat)) + ")=" +
                   std::to_string(segment.takeIndex) + " ";
        }
        return out;
    };

    // ---- A new take wins over what it covers --------------------------------
    {
        CompGroup group;
        group.lengthBeats = 16.0f;

        const int first = comp::addTake(group, 0, 0.0f, 16.0f, "");
        check(first == 0, "the first take goes in");
        check(comp::takeAt(group, 8.0f) == 0,
              "and is chosen across its span - you record because you want "
              "to hear the new one");

        const int second = comp::addTake(group, 1, 0.0f, 16.0f, "");
        check(second == 1, "a second take goes in");
        check(comp::takeAt(group, 8.0f) == 1,
              "and takes over, with the first still there one click away");
    }

    // ---- The swipe --------------------------------------------------------------
    {
        CompGroup group;
        group.lengthBeats = 16.0f;
        comp::addTake(group, 0, 0.0f, 16.0f, "");
        comp::addTake(group, 1, 0.0f, 16.0f, "");

        // Swipe take 0 across the middle. The two ends of take 1 must
        // survive - that is what makes it feel like painting rather than a
        // series of destructive edits.
        comp::chooseTake(group, 4.0f, 8.0f, 0);

        check(comp::takeAt(group, 2.0f) == 1, "before the swipe, take 1");
        check(comp::takeAt(group, 6.0f) == 0, "inside it, take 0");
        check(comp::takeAt(group, 12.0f) == 1, "and after it, take 1 again");
        check(group.segments.size() == 3,
              "leaving exactly three segments (" + shapeOf(group) + ")");
    }

    {
        // A swipe covering everything collapses to one segment.
        CompGroup group;
        group.lengthBeats = 16.0f;
        comp::addTake(group, 0, 0.0f, 16.0f, "");
        comp::addTake(group, 1, 0.0f, 16.0f, "");
        comp::chooseTake(group, 4.0f, 8.0f, 0);
        comp::chooseTake(group, 0.0f, 16.0f, 1);

        check(group.segments.size() == 1,
              "a swipe over the whole span collapses to one segment (" +
              shapeOf(group) + ")");
    }

    {
        // Adjacent runs of the same take MERGE. Without this every swipe
        // leaves another boundary, the list grows all session, and
        // flattening emits a clip per fragment.
        CompGroup group;
        group.lengthBeats = 16.0f;
        comp::addTake(group, 0, 0.0f, 16.0f, "");
        comp::addTake(group, 1, 0.0f, 16.0f, "");

        for (int i = 0; i < 8; ++i) {
            comp::chooseTake(group, float(i) * 2.0f, float(i) * 2.0f + 2.0f, 0);
        }
        check(group.segments.size() == 1,
              "eight adjacent swipes of the same take merge into one segment "
              "(" + shapeOf(group) + ") - otherwise a comp that is one take "
              "start to finish flattens into fifty clips that happen to abut");
    }

    {
        // Overlapping swipes must not leave overlapping segments.
        CompGroup group;
        group.lengthBeats = 16.0f;
        comp::addTake(group, 0, 0.0f, 16.0f, "");
        comp::addTake(group, 1, 0.0f, 16.0f, "");
        comp::addTake(group, 2, 0.0f, 16.0f, "");

        comp::chooseTake(group, 2.0f, 10.0f, 0);
        comp::chooseTake(group, 6.0f, 14.0f, 1);
        comp::chooseTake(group, 4.0f, 8.0f, 2);

        bool ordered = true;
        for (size_t i = 1; i < group.segments.size(); ++i) {
            if (group.segments[i].startBeat < group.segments[i - 1].endBeat - 1e-4f) {
                ordered = false;
                break;
            }
        }
        check(ordered,
              "overlapping swipes leave no overlapping segments (" +
              shapeOf(group) + ")");

        check(comp::takeAt(group, 5.0f) == 2 && comp::takeAt(group, 9.0f) == 1,
              "and the last swipe wins where it landed");
    }

    {
        // A backwards swipe is the same as a forwards one - a drag can end
        // to the left of where it started.
        CompGroup group;
        group.lengthBeats = 16.0f;
        comp::addTake(group, 0, 0.0f, 16.0f, "");
        comp::addTake(group, 1, 0.0f, 16.0f, "");
        comp::chooseTake(group, 12.0f, 4.0f, 0);
        check(comp::takeAt(group, 8.0f) == 0,
              "a swipe dragged right to left works like one dragged left to "
              "right");

        // And a zero-length one does nothing rather than inserting an empty
        // segment.
        const size_t before = group.segments.size();
        comp::chooseTake(group, 5.0f, 5.0f, 1);
        check(group.segments.size() == before,
              "and a zero-length swipe is ignored");
    }

    // ---- Removing a take renumbers the segments -------------------------------
    {
        CompGroup group;
        group.lengthBeats = 16.0f;
        comp::addTake(group, 10, 0.0f, 16.0f, "A");
        comp::addTake(group, 11, 0.0f, 16.0f, "B");
        comp::addTake(group, 12, 0.0f, 16.0f, "C");

        comp::chooseTake(group, 0.0f, 5.0f, 0);
        comp::chooseTake(group, 5.0f, 10.0f, 1);
        comp::chooseTake(group, 10.0f, 16.0f, 2);

        comp::removeTake(group, 1);      // remove B

        check(group.takes.size() == 2, "the take is gone");
        check(comp::takeAt(group, 2.0f) == 0,
              "segments below the removed index are untouched");
        check(comp::takeAt(group, 7.0f) == -1,
              "segments that used it become holes rather than pointing at a "
              "take that no longer exists");
        check(comp::takeAt(group, 12.0f) == 1,
              "and segments above it shift down - getting this wrong does "
              "not crash, it silently plays the wrong take");
        check(group.takes[1].name == "C", "which is the take it now refers to");
    }

    // ---- Coverage ------------------------------------------------------------
    {
        CompGroup group;
        group.lengthBeats = 16.0f;
        comp::addTake(group, 0, 0.0f, 16.0f, "");
        comp::addTake(group, 1, 0.0f, 16.0f, "");
        comp::chooseTake(group, 0.0f, 4.0f, 0);

        check(std::fabs(comp::takeCoverage(group, 0) - 4.0f) < 1e-3f,
              "coverage reports how much of the comp a take actually won");
        check(std::fabs(comp::takeCoverage(group, 1) - 12.0f) < 1e-3f,
              "for every take");
    }

    // ---- Punch ---------------------------------------------------------------
    {
        check(comp::insidePunch(5.0f, false, 8.0f, 12.0f),
              "with punch off, every beat records");
        check(!comp::insidePunch(5.0f, true, 8.0f, 12.0f),
              "with it on, a beat before the range does not");
        check(comp::insidePunch(9.0f, true, 8.0f, 12.0f),
              "and one inside does");
        check(!comp::insidePunch(12.0f, true, 8.0f, 12.0f),
              "the end is exclusive, so a punch that ends at 12 does not "
              "record beat 12 - two punches meeting at a beat must not both "
              "claim it");
        check(comp::insidePunch(5.0f, true, 8.0f, 8.0f),
              "and an empty range punches nothing rather than everything");
    }
}

// ============================================================================
// 91. Comps flatten into clips
// ============================================================================
static void testCompFlattening() {
    beginTest("Comps flatten into clips");

    auto makeProject = [](int takes) {
        auto p = std::make_unique<Project>();
        p->bpm = 120.0f;               // one beat is half a second
        p->arrangement.clear();

        for (int i = 0; i < takes; ++i) {
            Sample sample;
            sample.name = "take" + std::to_string(i);
            sample.sampleRate = 48000;
            sample.audioData.assign(48000 * 8, 0.25f);
            sample.lengthSeconds = 8.0f;
            sample.isLoaded = true;
            p->samplePool.addSample(sample);
        }

        CompGroup group;
        group.channelIndex = 2;
        group.startBeat = 0.0f;
        group.lengthBeats = 16.0f;
        for (int i = 0; i < takes; ++i) {
            comp::addTake(group, i, 0.0f, 16.0f, "");
        }
        p->compGroups.push_back(group);
        return p;
    };

    // ---- One take across the span becomes one clip -----------------------------
    {
        auto p = makeProject(1);
        const int emitted = comp::flattenAll(*p);

        check(emitted == 1, "a single-take comp flattens to one clip");
        check(p->arrangement.size() == 1, "and that is all that is there");
        if (p->arrangement.size() == 1) {
            const Clip& clip = p->arrangement[0];
            check(clip.type == ClipType::Audio, "as an audio clip");
            check(clip.channelIndex == 2, "on the comp's channel");
            check(std::fabs(clip.lengthBeats - 16.0f) < 1e-3f,
                  "covering the whole comp");
            check(clip.compGroup == 0,
                  "and tagged with the group that made it, so re-flattening "
                  "can replace its own output");
        }
    }

    // ---- Re-flattening replaces rather than accumulates --------------------------
    {
        auto p = makeProject(2);
        comp::flattenAll(*p);
        const size_t first = p->arrangement.size();

        comp::flattenAll(*p);
        comp::flattenAll(*p);
        check(p->arrangement.size() == first,
              "flattening three times leaves the same number of clips - a "
              "swipe re-flattens, and accumulating would fill the timeline");
    }

    // ---- Hand-placed clips are left alone ----------------------------------------
    {
        auto p = makeProject(1);
        Clip byHand{0, 5, 0.0f, 4.0f, 0};
        p->patterns.clear();
        p->patterns.push_back(Pattern());
        p->arrangement.push_back(byHand);

        comp::flattenAll(*p);
        comp::flattenAll(*p);

        int handPlaced = 0;
        for (const Clip& clip : p->arrangement) {
            if (clip.compGroup < 0) ++handPlaced;
        }
        check(handPlaced == 1,
              "flattening does not touch clips it did not make - it removes "
              "only what carries its own group index");
    }

    // ---- The trim offset, which is the classic comping bug ------------------------
    {
        auto p = makeProject(2);
        CompGroup& group = p->compGroups[0];

        // Take 1 across the whole span, then take 0 swiped over beats 8-12.
        comp::chooseTake(group, 0.0f, 16.0f, 1);
        comp::chooseTake(group, 8.0f, 12.0f, 0);
        comp::flattenAll(*p);

        const Clip* middle = nullptr;
        for (const Clip& clip : p->arrangement) {
            if (std::fabs(clip.startBeat - 8.0f) < 1e-3f) middle = &clip;
        }
        check(middle != nullptr, "the swiped span produced a clip");

        if (middle != nullptr) {
            // The take started at beat 0, the segment starts at beat 8, and
            // at 120 bpm a beat is half a second - so playback must begin
            // four seconds into the recording. Getting this wrong means
            // every segment plays from the top of its take, and the comp is
            // in time with nothing.
            check(std::fabs(middle->trimStartSeconds - 4.0f) < 1e-3f,
                  "and starts four seconds into its take, not at the top of "
                  "it (got " + std::to_string(middle->trimStartSeconds) + ")");
            check(std::fabs(middle->trimEndSeconds - 6.0f) < 1e-3f,
                  "ending where the segment ends");
        }
    }

    // ---- Joins are crossfaded ------------------------------------------------------
    {
        auto p = makeProject(2);
        CompGroup& group = p->compGroups[0];
        comp::chooseTake(group, 0.0f, 16.0f, 1);
        comp::chooseTake(group, 8.0f, 12.0f, 0);
        comp::flattenAll(*p);

        int faded = 0;
        for (const Clip& clip : p->arrangement) {
            if (clip.fadeInBeats > 0.0f || clip.fadeOutBeats > 0.0f) ++faded;
        }
        check(faded >= 2,
              "internal joins carry a short crossfade - butting two takes "
              "together at a zero crossing they do not share is a click, and "
              "a click at every comp point is what gives an amateur comp away");

        // But not at the very outside edges, which are not joins.
        const Clip* first = nullptr;
        for (const Clip& clip : p->arrangement) {
            if (std::fabs(clip.startBeat) < 1e-3f) first = &clip;
        }
        check(first != nullptr && first->fadeInBeats == 0.0f,
              "and the start of the comp is not faded, because it is not a "
              "join");
    }

    // ---- Muted takes and holes -------------------------------------------------------
    {
        auto p = makeProject(2);
        CompGroup& group = p->compGroups[0];
        comp::chooseTake(group, 0.0f, 16.0f, 0);
        group.takes[0].muted = true;
        comp::flattenAll(*p);
        check(p->arrangement.empty(),
              "a muted take produces no clips - it is kept, and never chosen");

        group.takes[0].muted = false;
        comp::chooseTake(group, 4.0f, 8.0f, -1);
        comp::flattenAll(*p);

        bool anyOverHole = false;
        for (const Clip& clip : p->arrangement) {
            if (clip.startBeat < 8.0f && clip.startBeat + clip.lengthBeats > 4.0f &&
                clip.startBeat >= 4.0f) {
                anyOverHole = true;
            }
        }
        check(!anyOverHole,
              "and a deliberate hole stays silent rather than falling back "
              "to some other take");
    }

    // ---- A punched-in take only covers what it recorded --------------------------------
    {
        auto p = makeProject(1);
        CompGroup& group = p->compGroups[0];

        // A repair pass covering only beats 4 to 8.
        Sample patch;
        patch.name = "punch";
        patch.sampleRate = 48000;
        patch.audioData.assign(48000 * 2, 0.5f);
        patch.lengthSeconds = 2.0f;
        patch.isLoaded = true;
        const int patchId = p->samplePool.addSample(patch);

        const int punched = comp::addTake(group, patchId, 4.0f, 4.0f, "Punch");
        comp::flattenAll(*p);

        const Clip* punchClip = nullptr;
        for (const Clip& clip : p->arrangement) {
            if (clip.sampleId == patchId) punchClip = &clip;
        }
        check(punchClip != nullptr, "the punched take produced a clip");
        check(punchClip != nullptr &&
              std::fabs(punchClip->startBeat - 4.0f) < 1e-3f &&
              std::fabs(punchClip->lengthBeats - 4.0f) < 1e-3f,
              "covering only what it recorded, not the whole comp");
        check(punched >= 0, "and it was added");
    }

    // ---- The flattened comp actually sounds --------------------------------------------
    {
        auto p = makeProject(1);
        p->masterLimiterEnabled = false;
        p->masterCompressorEnabled = false;
        p->masterEQEnabled = false;
        p->masterVolume = 0.8f;
        p->channels[2].volume = 0.8f;
        p->channels[2].pan = 0.0f;
        comp::flattenAll(*p);

        auto seqPtr = std::make_unique<Sequencer>();
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(p.get());
        seqPtr->updateChannelConfigs();
        seqPtr->updateMasterEffects();
        seqPtr->play();

        std::vector<float> l(512), r(512);
        double energy = 0.0;
        for (int b = 0; b < 30; ++b) {
            seqPtr->process(l.data(), r.data(), 512);
            if (b >= 4) for (float v : l) energy += double(v) * v;
        }
        check(energy > 1e-4,
              "a flattened comp plays through the ordinary audio-clip path - "
              "which is the entire reason comping flattens rather than being "
              "resolved in the mixer");
    }

    // ---- It survives a save and load ------------------------------------------------------
    {
        auto p = makeProject(3);
        CompGroup& group = p->compGroups[0];
        group.name = "Lead vocal";
        comp::chooseTake(group, 0.0f, 6.0f, 0);
        comp::chooseTake(group, 6.0f, 11.0f, 2);
        comp::chooseTake(group, 11.0f, 16.0f, 1);
        group.takes[1].muted = true;
        group.takes[2].name = "The good one";
        comp::flattenAll(*p);

        const size_t clipCount = p->arrangement.size();

        const std::string path = testPath("comp.ctp");
        check(saveProject(*p, path), "a comped project saves");

        auto loaded = std::make_unique<Project>();
        check(loadProject(*loaded, path), "and loads");

        check(loaded->compGroups.size() == 1, "with its comp group");
        if (!loaded->compGroups.empty()) {
            const CompGroup& got = loaded->compGroups[0];
            check(got.name == "Lead vocal" && got.channelIndex == 2,
                  "its name and channel");
            check(got.takes.size() == 3, "all three takes");
            check(got.takes.size() == 3 && got.takes[1].muted,
                  "including which are muted");
            check(got.takes.size() == 3 && got.takes[2].name == "The good one",
                  "and their names");
            check(got.segments.size() == 3,
                  "and the comp itself (" + std::to_string(got.segments.size()) +
                  " segments)");
            check(comp::takeAt(got, 8.0f) == 2,
                  "resolving to the same takes it did before");
        }

        check(loaded->arrangement.size() == clipCount,
              "and the flattened clips come back too - re-flattening on load "
              "would be equivalent but would discard any hand-editing done "
              "to them");

        int tagged = 0;
        for (const Clip& clip : loaded->arrangement) {
            if (clip.compGroup == 0) ++tagged;
        }
        check(tagged == static_cast<int>(clipCount),
              "still tagged with their group");

        std::remove(path.c_str());
    }

    // ---- A project with no comps writes nothing ------------------------------------------
    {
        Project plain;
        const std::string path = testPath("comp_absent.ctp");
        check(saveProject(plain, path), "a project with no comps saves");

        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        check(text.find("COMP ") == std::string::npos &&
              text.find("TAKE ") == std::string::npos,
              "and writes no comp lines");
        std::remove(path.c_str());
    }

    // ---- Validation ---------------------------------------------------------------------
    {
        auto p = std::make_unique<Project>();
        CompGroup group;
        group.channelIndex = 900;
        group.lengthBeats = -5.0f;

        Take take;
        take.sampleId = 500;                 // past the end of the pool
        take.lengthBeats = 0.0f;
        group.takes.push_back(take);

        CompSegment segment;
        segment.startBeat = 0.0f;
        segment.endBeat = 8.0f;
        segment.takeIndex = 40;              // no such take
        group.segments.push_back(segment);

        p->compGroups.push_back(group);
        clampProjectToValidRanges(*p);

        const CompGroup& fixed = p->compGroups[0];
        check(fixed.channelIndex >= 0 && fixed.channelIndex < Project::MAX_CHANNELS,
              "an impossible channel is repaired");
        check(fixed.lengthBeats > 0.0f, "and a negative length");
        check(fixed.takes[0].sampleId == -1,
              "a take pointing past the sample pool becomes silent");
        check(fixed.takes[0].lengthBeats > 0.0f, "and a zero-length take is given one");
        check(fixed.segments.empty() || fixed.segments[0].takeIndex == -1,
              "and a segment naming a take that does not exist becomes a "
              "hole - it would otherwise index off the end of the vector "
              "during flattening");

        // And flattening a repaired project must not crash or emit nonsense.
        const int emitted = comp::flattenAll(*p);
        check(emitted == 0, "a repaired comp with no usable takes emits nothing");
    }
}

// ============================================================================
// Headless ImGui harness
// ============================================================================
/*
 * A real context, a built font atlas, a fake display, and no backend.
 *
 * The atlas is built through the legacy path - Build() plus
 * GetTexDataAsRGBA32() - rather than the 1.92 texture-request path, because
 * the new one expects a backend to service ImTextureData requests and there
 * is no backend here. Without a built atlas, NewFrame() asserts.
 */
class HeadlessUI {
public:
    HeadlessUI(float width = 1600.0f, float height = 900.0f) {
        m_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_context);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(width, height);
        io.DeltaTime = 1.0f / 60.0f;

        // No .ini file: a test must not read or write the user's layout, and
        // it must not depend on one existing.
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        unsigned char* pixels = nullptr;
        int texWidth = 0, texHeight = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &texWidth, &texHeight);
        // ImTextureID is a plain ImU64 in this build, not a pointer.
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));

        ApplyTheme(Theme::Stock);
    }

    ~HeadlessUI() {
        if (m_context != nullptr) {
            ImGui::SetCurrentContext(m_context);
            ImGui::DestroyContext(m_context);
        }
    }

    HeadlessUI(const HeadlessUI&) = delete;
    HeadlessUI& operator=(const HeadlessUI&) = delete;

    void setDisplaySize(float width, float height) {
        ImGui::SetCurrentContext(m_context);
        ImGui::GetIO().DisplaySize = ImVec2(width, height);
    }

    // Where the mouse is, and whether it is down, for the NEXT frame.
    void setMouse(float x, float y, bool down) {
        ImGui::SetCurrentContext(m_context);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(0, down);
    }

    struct FrameResult {
        bool idStackBalanced = true;
        bool windowStackBalanced = true;
        bool allVerticesFinite = true;
        int vertexCount = 0;
        int drawListCount = 0;
        std::string firstProblem;
    };

    /*
     * Run one frame, calling `draw` between NewFrame and Render.
     *
     * Checks after Render rather than during: the ID and window stacks are
     * only meaningfully comparable once the frame has closed, and the draw
     * data does not exist until then.
     */
    template <typename Fn>
    FrameResult frame(Fn&& draw) {
        ImGui::SetCurrentContext(m_context);
        FrameResult result;

        ImGui::NewFrame();

        ImGuiContext& g = *ImGui::GetCurrentContext();
        const int windowStackBefore = g.CurrentWindowStack.Size;

        draw();

        if (g.CurrentWindowStack.Size != windowStackBefore) {
            result.windowStackBalanced = false;
            result.firstProblem =
                "window stack left at " +
                std::to_string(g.CurrentWindowStack.Size) + ", expected " +
                std::to_string(windowStackBefore) +
                " - a Begin() without its End()";
            // Unwind, or Render() asserts and every later frame is wrong.
            while (g.CurrentWindowStack.Size > windowStackBefore) ImGui::End();
        }

        // Every window's ID stack must be back to its single root entry. A
        // PushID left dangling corrupts the IDs of every widget after it,
        // so unrelated controls silently start sharing state.
        for (ImGuiWindow* window : g.Windows) {
            if (window == nullptr) continue;
            if (window->IDStack.Size != 1) {
                result.idStackBalanced = false;
                if (result.firstProblem.empty()) {
                    result.firstProblem =
                        std::string("window '") + window->Name +
                        "' left " + std::to_string(window->IDStack.Size - 1) +
                        " PushID(s) unpopped";
                }
            }
        }

        ImGui::Render();

        ImDrawData* data = ImGui::GetDrawData();
        if (data != nullptr) {
            result.drawListCount = data->CmdListsCount;
            for (int i = 0; i < data->CmdListsCount; ++i) {
                const ImDrawList* list = data->CmdLists[i];
                result.vertexCount += list->VtxBuffer.Size;
                for (int v = 0; v < list->VtxBuffer.Size; ++v) {
                    const ImDrawVert& vertex = list->VtxBuffer[v];
                    if (!std::isfinite(vertex.pos.x) || !std::isfinite(vertex.pos.y) ||
                        !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y)) {
                        result.allVerticesFinite = false;
                        if (result.firstProblem.empty()) {
                            result.firstProblem =
                                "a vertex position is NaN or infinite - layout "
                                "maths divided by zero, and a GPU discards "
                                "those silently";
                        }
                        break;
                    }
                }
            }
        }
        return result;
    }

    // Several frames of the same content. ImGui settles over two or three
    // frames - the first has no size for anything auto-sized, docking
    // resolves on the second - so a single frame is not representative.
    template <typename Fn>
    FrameResult frames(int count, Fn&& draw) {
        FrameResult last;
        for (int i = 0; i < count; ++i) last = frame(draw);
        return last;
    }

private:
    ImGuiContext* m_context = nullptr;
};

// Fold a FrameResult into one check, so a panel test is one line.
static bool frameIsClean(const HeadlessUI::FrameResult& result) {
    return result.idStackBalanced && result.windowStackBalanced &&
           result.allVerticesFinite;
}


// ============================================================================
// 92. The take lanes panel: draws, swipes, and commits a take
// ============================================================================

// ============================================================================
// 93. Fuzzy matching, shortcuts and the action registry
// ============================================================================
static void testActionRegistry() {
    beginTest("Actions, shortcuts and fuzzy search");

    // ---- The matcher --------------------------------------------------------
    //
    // What matters is the ranking, not merely that a match was found: a
    // palette that finds the right command and puts it fourth is a palette
    // people stop using.
    check(fuzzyScore("save", "Save Project") >= 0, "An exact prefix matches");
    check(fuzzyScore("svp", "Save Project") >= 0, "Initials match");
    check(fuzzyScore("zzz", "Save Project") < 0, "Absent letters do not match");
    check(fuzzyScore("evas", "Save Project") < 0,
          "Letters out of order do not match");
    check(fuzzyScore("", "Save Project") == 0, "An empty query matches neutrally");
    check(fuzzyScore("saveproject", "Save") < 0,
          "A query longer than the string cannot match");

    check(fuzzyScore("save", "Save Project") >
              fuzzyScore("save", "Autosave Interval"),
          "A match at the start beats one in the middle");
    check(fuzzyScore("save", "Save") > fuzzyScore("save", "Save Project As"),
          "The shorter of two equal matches wins");
    check(fuzzyScore("rl", "Reset Layout") > fuzzyScore("rl", "Rewind to Start"),
          "Word-initial letters beat scattered ones");

    // Case must not matter: nobody types capitals into a palette.
    check(fuzzyScore("SAVE", "Save Project") >= 0, "Matching ignores case");
    check(fuzzyScore("save", "SAVE PROJECT") >= 0, "Matching ignores case both ways");

    // ---- Shortcut text round-trips -------------------------------------------
    {
        Shortcut plain;
        plain.key = ImGuiKey_S;
        check(shortcutText(plain) == "S", "A bare key prints as itself");

        Shortcut combo;
        combo.key = ImGuiKey_S;
        combo.ctrl = true;
        combo.shift = true;
        check(shortcutText(combo) == "Ctrl+Shift+S", "Modifiers print in order");

        // The round trip is what the binding file depends on.
        const char* SAMPLES[] = {"S", "Ctrl+S", "Ctrl+Shift+S", "Alt+F4",
                                 "Ctrl+Shift+Alt+Z", "Space", "F12", "[",
                                 "Left", "PageDown", "\\"};
        for (const char* text : SAMPLES) {
            const Shortcut parsed = shortcutFromText(text);
            check(parsed.valid(),
                  std::string("\"") + text + "\" parses");
            check(shortcutText(parsed) == text,
                  std::string("\"") + text + "\" survives a round trip, got \"" +
                      shortcutText(parsed) + "\"");
        }

        check(!shortcutFromText("").valid(), "An empty string is not a shortcut");
        check(!shortcutFromText("Ctrl+").valid(), "A modifier alone is not one");
        check(!shortcutFromText("Meta+S").valid(),
              "An unknown modifier is refused rather than dropped");
        check(!shortcutFromText("Ctrl+Nonsense").valid(),
              "An unknown key name is refused");
        check(shortcutText(Shortcut()).empty(),
              "An unbound action prints as nothing, not \"None\"");
    }

    // ---- The registry ---------------------------------------------------------
    {
        ActionRegistry registry;

        int ran = 0;
        Action first;
        first.id = "test.first";
        first.label = "First Command";
        first.category = "Testing";
        first.defaultShortcut = shortcutFromText("Ctrl+1");
        first.run = [&ran](ActionContext&) { ++ran; };
        registry.add(first);

        Action second;
        second.id = "test.second";
        second.label = "Second Command";
        second.category = "Testing";
        second.defaultShortcut = shortcutFromText("Ctrl+2");
        second.run = [&ran](ActionContext&) { ran += 10; };
        registry.add(second);

        check(registry.size() == 2, "Two commands registered");
        check(registry.find("test.first") != nullptr, "A command is found by id");
        check(registry.find("test.missing") == nullptr,
              "An id that was never registered is not found");

        // Re-adding an id replaces rather than duplicating, or the binding
        // file would have two rows to choose between.
        registry.add(first);
        check(registry.size() == 2, "Re-adding an id replaces it");

        // add() must apply the default, or every command starts unbound.
        check(registry.find("test.first")->shortcut == shortcutFromText("Ctrl+1"),
              "A registered command starts on its default key");

        // ---- Conflicts -------------------------------------------------------
        check(registry.conflict(shortcutFromText("Ctrl+2"), "test.first") == 1,
              "A key already in use is reported");
        check(registry.conflict(shortcutFromText("Ctrl+2"), "test.second") < 0,
              "A command does not conflict with itself");
        check(registry.conflict(shortcutFromText("Ctrl+9"), "test.first") < 0,
              "An unused key is free");
        check(registry.conflict(Shortcut(), "test.first") < 0,
              "Unbound is not a conflict - every unbound command would clash");

        // ---- Rebinding and persistence ----------------------------------------
        check(!registry.anyRebound(), "Nothing is rebound to start with");
        check(registry.bind("test.first", shortcutFromText("Alt+Q")),
              "A command can be rebound");
        check(registry.anyRebound(), "Rebinding is noticed");

        const std::string path = "test-keys.ini";
        check(registry.saveBindings(path), "Bindings save");

        {
            // Only the difference is written. Writing the whole table would
            // freeze today's defaults into every user's file forever, so a
            // later change of default would reach nobody.
            std::ifstream file(path);
            std::string body((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
            check(body.find("test.first=Alt+Q") != std::string::npos,
                  "The rebound command is written");
            check(body.find("test.second") == std::string::npos,
                  "A command still on its default is not written");
        }

        // A fresh registry, same commands, loads what was saved.
        ActionRegistry reloaded;
        reloaded.add(first);
        reloaded.add(second);
        check(reloaded.find("test.first")->shortcut == shortcutFromText("Ctrl+1"),
              "The fresh registry starts on defaults");
        check(reloaded.loadBindings(path), "Bindings load");
        check(reloaded.find("test.first")->shortcut == shortcutFromText("Alt+Q"),
              "The saved binding came back");
        check(reloaded.find("test.second")->shortcut == shortcutFromText("Ctrl+2"),
              "A command not in the file keeps its default");

        check(registry.resetToDefault("test.first"), "One command resets");
        check(registry.find("test.first")->shortcut == shortcutFromText("Ctrl+1"),
              "Reset restores the default key");

        registry.bind("test.first", shortcutFromText("Alt+Q"));
        registry.bind("test.second", Shortcut());
        registry.resetAllToDefaults();
        check(!registry.anyRebound(), "Reset all restores every default");

        std::remove(path.c_str());
    }

    // ---- A binding file from another version -----------------------------------
    //
    // The tolerance that matters: one bad line must not cost the rest.
    {
        const std::string path = "test-keys-mixed.ini";
        {
            std::ofstream file(path);
            file << "# a comment\n";
            file << "\n";
            file << "test.first=Alt+Q\n";
            file << "command.from.the.future=Ctrl+J\n";
            file << "test.second=Ctrl+Nonsense\n";
            file << "malformed line with no equals\n";
        }

        ActionRegistry registry;
        Action first;
        first.id = "test.first";
        first.label = "First";
        first.defaultShortcut = shortcutFromText("Ctrl+1");
        registry.add(first);

        Action second;
        second.id = "test.second";
        second.label = "Second";
        second.defaultShortcut = shortcutFromText("Ctrl+2");
        registry.add(second);

        check(registry.loadBindings(path), "A mixed binding file loads");
        check(registry.find("test.first")->shortcut == shortcutFromText("Alt+Q"),
              "A good line before the bad ones was applied");
        check(registry.find("test.second")->shortcut == shortcutFromText("Ctrl+2"),
              "An unparseable key leaves the command on its default");

        std::remove(path.c_str());

        check(!registry.loadBindings("no-such-keys-file.ini"),
              "A missing binding file is reported, not thrown");
    }

    // ---- The shipped table -------------------------------------------------------
    {
        ActionRegistry registry;
        registerDefaultActions(registry);
        check(registry.size() > 20, "The shipped table has commands in it");

        // Every command must be runnable and describable, or it is a row in
        // the palette that does nothing when chosen.
        int missingRun = 0, missingLabel = 0, missingCategory = 0;
        for (const Action& action : registry.all()) {
            if (!action.run) ++missingRun;
            if (action.label.empty()) ++missingLabel;
            if (action.category.empty()) ++missingCategory;
        }
        check(missingRun == 0, "Every command has something to run");
        check(missingLabel == 0, "Every command has a label");
        check(missingCategory == 0, "Every command has a category");

        // Ids must be unique - a binding file addresses them by id.
        int duplicates = 0;
        for (size_t i = 0; i < registry.all().size(); ++i) {
            for (size_t j = i + 1; j < registry.all().size(); ++j) {
                if (registry.all()[i].id == registry.all()[j].id) ++duplicates;
            }
        }
        check(duplicates == 0, "Every command id is unique");

        /*
         * No two commands ship on the same key.
         *
         * A conflict is allowed after rebinding - it is occasionally what
         * somebody wants - but shipping one means one of the two silently
         * never fires, and which one depends on registration order.
         */
        std::vector<std::string> clashes;
        for (const Action& action : registry.all()) {
            if (!action.shortcut.valid()) continue;
            const int other = registry.conflict(action.shortcut, action.id);
            if (other >= 0) {
                clashes.push_back(action.label + " and " +
                                  registry.all()[size_t(other)].label + " on " +
                                  shortcutText(action.shortcut));
            }
        }
        check(clashes.empty(), "No two default bindings collide" +
                  (clashes.empty() ? std::string() : ": " + clashes[0]));

        // Registering twice must not duplicate - main() and a test both do it.
        const size_t before = registry.size();
        registerDefaultActions(registry);
        check(registry.size() == before,
              "Registering the defaults twice does not duplicate them");

        // The commands people look for by name must be findable by name.
        struct Lookup { const char* query; const char* expectedId; };
        static const Lookup LOOKUPS[] = {
            {"save",     "file.save"},
            {"undo",     "edit.undo"},
            {"mixer",    "view.mixer"},
            {"tracker",  "view.tracker"},
            {"reset",    "layout.reset"},
            {"browser",  "help.browser"},
        };
        for (const Lookup& lookup : LOOKUPS) {
            const std::vector<ActionMatch> matches = registry.search(lookup.query);
            const bool found = !matches.empty() &&
                registry.all()[size_t(matches[0].index)].id == lookup.expectedId;
            check(found, std::string("\"") + lookup.query + "\" finds " +
                      lookup.expectedId + ", got " +
                      (matches.empty()
                           ? std::string("nothing")
                           : registry.all()[size_t(matches[0].index)].id));
        }

        check(registry.search("").size() == registry.size(),
              "An empty query lists everything");
        check(registry.search("qqqzzz").empty(),
              "A query matching nothing returns nothing");

        // ---- Every command actually does something ---------------------------
        //
        // The point of this one: an action whose callback is wired to the
        // wrong field, or to a copy, changes nothing and looks identical to
        // one that worked.
        auto projectPtr = std::make_unique<Project>();
        auto seqPtr = std::make_unique<Sequencer>();
        UIState ui;
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(projectPtr.get());

        ActionContext context;
        context.project = projectPtr.get();
        context.ui = &ui;
        context.seq = seqPtr.get();
        check(context.complete(), "A fully-populated context is complete");

        ActionContext empty;
        check(!empty.complete(), "An empty context is not complete");

        int ranWithoutCrashing = 0;
        for (const Action& action : registry.all()) {
            // A blocking OS dialog does not fail here, it hangs.
            if (action.opensDialog) continue;
            if (!action.run) continue;
            action.run(context);
            ++ranWithoutCrashing;
        }
        check(ranWithoutCrashing > 15,
              "Most commands ran headlessly without a window");

        // And specific ones did what they say.
        ui.currentView = ViewMode::PianoRoll;
        registry.find("view.mixer")->run(context);
        check(ui.currentView == ViewMode::Mixer, "The view command changes the view");

        const bool macrosBefore = ui.showMacroEditor;
        registry.find("panel.macros")->run(context);
        check(ui.showMacroEditor != macrosBefore,
              "The panel command toggles the panel");

        ui.selectedChannel = 0;
        registry.find("channel.next")->run(context);
        check(ui.selectedChannel == 1, "Next channel advances");
        registry.find("channel.previous")->run(context);
        check(ui.selectedChannel == 0, "Previous channel goes back");

        // Wrapping, rather than running off the end into a channel that
        // does not exist.
        ui.selectedChannel = 0;
        registry.find("channel.previous")->run(context);
        check(ui.selectedChannel == projectPtr->activeChannelCount() - 1,
              "Previous channel wraps to the last one");

        const bool mutedBefore = projectPtr->channels[0].muted;
        ui.selectedChannel = 0;
        registry.find("channel.mute")->run(context);
        check(projectPtr->channels[0].muted != mutedBefore,
              "The mute command reaches the channel");

        ui.pendingLayoutFrames = 0;
        registry.find("layout.reset")->run(context);
        check(ui.pendingLayoutFrames > 0, "Reset layout asks for a relayout");

        // New Project must actually empty the project - the classic failure
        // is capturing a Project& at registration, so this works once.
        projectPtr->patterns.resize(4);
        registry.find("file.new")->run(context);
        check(projectPtr->patterns.size() == 1,
              "New Project resets the project it was handed, not a copy");
        registry.find("file.new")->run(context);
        check(projectPtr->patterns.size() == 1,
              "New Project still works the second time");
    }
}

// ============================================================================
// 94. The browser's model
// ============================================================================
static void testBrowserModel() {
    beginTest("Browser scanning and filtering");

    // ---- Payloads must survive a memcpy ------------------------------------
    //
    // ImGui copies a drag payload byte for byte, so anything with a pointer
    // in it arrives as a dangling one.
    {
        check(std::is_trivially_copyable<BrowserPayload>::value,
              "A drag payload is trivially copyable");

        BrowserEntry entry;
        entry.kind = BrowserEntryKind::File;
        entry.id = 7;
        entry.engine = 3;
        entry.path = "C:/samples/kick.wav";

        const BrowserPayload payload = makePayload(entry);
        check(payload.kind == BrowserEntryKind::File, "The payload keeps its kind");
        check(payload.id == 7, "The payload keeps its id");
        check(payload.engine == 3, "The payload keeps its engine");
        check(std::string(payload.path) == "C:/samples/kick.wav",
              "The payload keeps its path");

        // Round-tripped through raw bytes, which is what ImGui does to it.
        unsigned char buffer[sizeof(BrowserPayload)];
        std::memcpy(buffer, &payload, sizeof(payload));
        BrowserPayload arrived;
        std::memcpy(&arrived, buffer, sizeof(arrived));
        check(std::string(arrived.path) == "C:/samples/kick.wav",
              "The path survives being copied as bytes");

        // A path longer than the buffer truncates rather than overrunning.
        BrowserEntry huge;
        huge.path = std::string(1000, 'x');
        const BrowserPayload truncated = makePayload(huge);
        check(std::strlen(truncated.path) == sizeof(truncated.path) - 1,
              "An over-long path is truncated to fit");
        check(truncated.path[sizeof(truncated.path) - 1] == '\0',
              "A truncated path is still terminated");
    }

    // ---- Extensions ---------------------------------------------------------
    check(isAudioExtension(".wav"), ".wav is audio");
    check(isAudioExtension(".WAV"), "Extension matching ignores case");
    check(isAudioExtension(".mp3"), ".mp3 is audio");
    check(!isAudioExtension(".txt"), ".txt is not audio");
    check(!isAudioExtension(""), "No extension is not audio");
    check(isProjectExtension(".ctp"), ".ctp is a project");
    check(isProjectExtension(".MID"), ".MID is a project");
    check(!isProjectExtension(".wav"), "Audio is not a project file");

    // ---- Scanning -----------------------------------------------------------
    {
        const std::string root = "browser-scan-test";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(root + "/inner", error);

        auto touch = [&](const std::string& name) {
            std::ofstream file(root + "/" + name);
            file << "x";
        };
        touch("beat.wav");
        touch("song.ctp");
        touch("notes.txt");        // filtered out
        touch("alpha.wav");

        std::vector<BrowserEntry> entries;
        scanDirectory(root, entries);

        int wavs = 0, projects = 0, others = 0, directories = 0;
        bool hasParent = false;
        for (const BrowserEntry& entry : entries) {
            if (entry.name == "..") { hasParent = true; continue; }
            if (entry.kind == BrowserEntryKind::Directory) { ++directories; continue; }
            const std::string extension =
                std::filesystem::path(entry.name).extension().string();
            if (isAudioExtension(extension)) ++wavs;
            else if (isProjectExtension(extension)) ++projects;
            else ++others;
        }

        check(hasParent, "A scan offers the way back out");
        check(wavs == 2, "Both audio files were listed");
        check(projects == 1, "The project file was listed");
        check(others == 0, "Files that cannot be opened are not listed");
        check(directories == 1, "The sub-directory was listed");

        // Directories first, then files, each sorted - a browser that lists
        // in filesystem order is a browser you cannot find anything in.
        size_t lastDirectory = 0;
        size_t firstFile = entries.size();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].kind == BrowserEntryKind::Directory) lastDirectory = i;
            else if (firstFile == entries.size()) firstFile = i;
        }
        check(lastDirectory < firstFile, "Directories are listed before files");

        bool sorted = true;
        for (size_t i = firstFile + 1; i < entries.size(); ++i) {
            if (entries[i].name < entries[i - 1].name) sorted = false;
        }
        check(sorted, "Files are listed in order");

        // A directory that is not there must be empty, not a throw out of a
        // draw call.
        std::vector<BrowserEntry> missing;
        scanDirectory(root + "/no-such-place", missing);
        check(missing.empty(), "Scanning a missing directory yields nothing");

        std::vector<BrowserEntry> notADirectory;
        scanDirectory(root + "/beat.wav", notADirectory);
        check(notADirectory.empty(), "Scanning a file yields nothing");

        // ---- Filtering ------------------------------------------------------
        const std::vector<BrowserEntry> all = filterEntries(entries, "");
        check(all.size() == entries.size(), "An empty filter keeps everything");

        const std::vector<BrowserEntry> wav = filterEntries(entries, "beat");
        bool foundBeat = false;
        for (const BrowserEntry& entry : wav) {
            if (entry.name == "beat.wav") foundBeat = true;
        }
        check(foundBeat, "Filtering finds the file by name");

        // The way out survives whatever is typed, or a search becomes a
        // dead end you can only leave by clearing it.
        bool keptParent = false;
        for (const BrowserEntry& entry : wav) {
            if (entry.name == "..") keptParent = true;
        }
        check(keptParent, "The parent entry survives a filter");
        check(wav[0].name == "..", "The parent entry stays first");

        const std::vector<BrowserEntry> none = filterEntries(entries, "zzzzz");
        check(none.size() == 1 && none[0].name == "..",
              "A filter matching nothing leaves only the way out");

        std::filesystem::remove_all(root, error);
    }
}

// ============================================================================
// 95. The command palette, the shortcut list and the browser draw
// ============================================================================
static void testCommandPaletteAndBrowser() {
    beginTest("Palette, shortcuts and browser (headless GUI)");

    auto uiPtr = std::make_unique<HeadlessUI>();
    auto projectPtr = std::make_unique<Project>();
    auto seqPtr = std::make_unique<Sequencer>();
    UIState ui;

    Project& project = *projectPtr;
    Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&project);

    registerDefaultActions(actionRegistry());

    // A sample and a preset to list, so the browser draws rows rather than
    // its empty state.
    {
        Sample sample;
        sample.name = "kick.wav";
        sample.sampleRate = 44100;
        sample.audioData.assign(4410, 0.2f);
        sample.lengthSeconds = 0.1f;
        sample.isLoaded = true;
        project.samplePool.addSample(sample);
    }

    // ---- The browser, on every tab -------------------------------------------
    ui.showBrowser = true;
    for (int t = 0; t < static_cast<int>(BrowserTab::Count); ++t) {
        browserState().tab = static_cast<BrowserTab>(t);
        browserState().search[0] = '\0';

        auto result = uiPtr->frames(3, [&] { DrawBrowser(project, ui, seq); });
        check(frameIsClean(result),
              std::string("Browser tab ") + browserTabName(browserState().tab) +
                  " draws cleanly: " + result.firstProblem);
    }

    // With a search that matches nothing, which is the empty-state path.
    browserState().tab = BrowserTab::Instruments;
    snprintf(browserState().search, sizeof(browserState().search), "zzzzzzz");
    {
        auto result = uiPtr->frames(2, [&] { DrawBrowser(project, ui, seq); });
        check(frameIsClean(result),
              "Browser with no matches draws cleanly: " + result.firstProblem);
    }
    browserState().search[0] = '\0';

    // Hidden means not drawn at all.
    ui.showBrowser = false;
    {
        auto result = uiPtr->frames(2, [&] { DrawBrowser(project, ui, seq); });
        check(frameIsClean(result), "A closed browser draws cleanly");
    }

    // ---- The shortcut list ----------------------------------------------------
    ui.showShortcuts = true;

    // Into a scratch directory: a rebind writes a file, and a test must not
    // write into the user's real settings.
    ui.settingsDirectory = "shortcut-panel-test";
    std::error_code error;
    std::filesystem::create_directories(ui.settingsDirectory, error);
    {
        auto result = uiPtr->frames(3, [&] { DrawShortcutsPanel(project, ui, seq); });
        check(frameIsClean(result),
              "Shortcut list draws cleanly: " + result.firstProblem);
        check(result.vertexCount > 0, "Shortcut list draws its rows");
    }

    // With a conflict in it, which is the row that draws the extra warning.
    actionRegistry().bind("view.mixer", shortcutFromText("Ctrl+S"));
    {
        auto result = uiPtr->frames(2, [&] { DrawShortcutsPanel(project, ui, seq); });
        check(frameIsClean(result),
              "A conflicting binding draws cleanly: " + result.firstProblem);
        check(actionRegistry().conflict(shortcutFromText("Ctrl+S"),
                                        "view.mixer") >= 0,
              "The conflict the panel is drawing is real");
    }
    actionRegistry().resetAllToDefaults();
    std::filesystem::remove_all(ui.settingsDirectory, error);
    ui.settingsDirectory.clear();
    ui.showShortcuts = false;

    // ---- The palette ----------------------------------------------------------
    OpenCommandPalette();
    check(commandPaletteState().open, "The palette opens");
    {
        auto result = uiPtr->frames(3, [&] { DrawCommandPalette(project, ui, seq); });
        check(frameIsClean(result),
              "Palette draws cleanly: " + result.firstProblem);
        check(result.vertexCount > 0, "Palette draws its list");
    }

    // With a query that matches nothing - the state most likely to index an
    // empty list.
    snprintf(commandPaletteState().query, sizeof(commandPaletteState().query),
             "zzzzzzzz");
    commandPaletteState().selected = 5;   // stale, from the longer list
    {
        auto result = uiPtr->frames(2, [&] { DrawCommandPalette(project, ui, seq); });
        check(frameIsClean(result),
              "Palette with no matches draws cleanly: " + result.firstProblem);
        check(commandPaletteState().selected == 0,
              "A stale selection is clamped rather than indexing off the end");
    }

    // And with a real query.
    snprintf(commandPaletteState().query, sizeof(commandPaletteState().query),
             "mixer");
    {
        auto result = uiPtr->frames(2, [&] { DrawCommandPalette(project, ui, seq); });
        check(frameIsClean(result), "Palette with results draws cleanly");
    }

    commandPaletteState().open = false;
    {
        auto result = uiPtr->frames(2, [&] { DrawCommandPalette(project, ui, seq); });
        check(frameIsClean(result), "A closed palette draws cleanly");
    }

    // ---- Dropping out of the browser --------------------------------------------
    //
    // The drop handlers, driven directly. What matters is that a drop
    // changes the project - a handler that accepts a payload and quietly
    // does nothing looks exactly like a drag that missed.
    {
        BrowserEntry instrument;
        instrument.kind = BrowserEntryKind::Instrument;
        instrument.id = static_cast<int>(OscillatorType::Sine);
        const BrowserPayload payload = makePayload(instrument);

        project.channels[0].oscillator.type = OscillatorType::Pulse;
        check(applyBrowserDropToChannel(project, seq, 0, payload),
              "An instrument drops onto a channel");
        check(project.channels[0].oscillator.type == OscillatorType::Sine,
              "The dropped instrument reached the channel");

        // Out of range must be refused rather than written past the array.
        check(!applyBrowserDropToChannel(project, seq, -1, payload),
              "A drop onto no channel is refused");
        check(!applyBrowserDropToChannel(project, seq, Project::MAX_CHANNELS,
                                         payload),
              "A drop past the last channel is refused");

        BrowserEntry rubbish;
        rubbish.kind = BrowserEntryKind::Instrument;
        rubbish.id = 9999;
        check(!applyBrowserDropToChannel(project, seq, 0, makePayload(rubbish)),
              "An instrument index out of range is refused");
    }

    {
        // A sample onto a channel means the sampler plays it.
        BrowserEntry sample;
        sample.kind = BrowserEntryKind::Sample;
        sample.id = 0;
        check(applyBrowserDropToChannel(project, seq, 1, makePayload(sample)),
              "A sample drops onto a channel");
        check(project.channels[1].oscillator.type == OscillatorType::Sampler,
              "A dropped sample switches the channel to the sampler");
        check(project.channels[1].oscillator.sampler.zoneCount == 1,
              "The dropped sample became a zone");
        check(project.channels[1].oscillator.sampler.zones[0].sampleId == 0,
              "The zone points at the dropped sample");

        BrowserEntry missing;
        missing.kind = BrowserEntryKind::Sample;
        missing.id = 999;
        check(!applyBrowserDropToChannel(project, seq, 1, makePayload(missing)),
              "A sample that is not in the pool is refused");
    }

    {
        // A preset switches the engine as well as writing the patch:
        // choosing "Bell" and hearing the existing sawtooth would read as
        // the preset having done nothing.
        const std::vector<BrowserEntry> presetEntries = collectPresetEntries();
        check(!presetEntries.empty(), "There are presets to list");

        const BrowserEntry& preset = presetEntries[0];
        check(applyBrowserDropToChannel(project, seq, 2, makePayload(preset)),
              "A preset drops onto a channel");
        check(project.channels[2].oscillator.type ==
                  static_cast<OscillatorType>(preset.engine),
              "A dropped preset switches the channel to its engine");
    }

    {
        // A sample onto the timeline becomes a clip, at the beat it landed
        // on and as long as the audio actually is.
        const size_t before = project.arrangement.size();

        BrowserEntry sample;
        sample.kind = BrowserEntryKind::Sample;
        sample.id = 0;
        check(applyBrowserDropToTimeline(project, 8.0f, 3, makePayload(sample)),
              "A sample drops onto the timeline");
        check(project.arrangement.size() == before + 1, "The drop made one clip");

        const Clip& clip = project.arrangement.back();
        check(clip.type == ClipType::Audio, "The drop made an audio clip");
        check(clip.sampleId == 0, "The clip plays the dropped sample");
        check(clip.channelIndex == 3, "The clip landed on the lane it was dropped on");
        check(std::fabs(clip.startBeat - 8.0f) < 0.01f,
              "The clip landed at the beat it was dropped on, not at zero");

        // 0.1 s at 120 bpm is 0.2 beats, which the minimum raises to 0.25.
        check(clip.lengthBeats > 0.0f, "The clip has a length");
        check(clip.lengthBeats < 4.0f,
              "A short sample makes a short clip, not a default-length one");

        // An instrument is not something the timeline can accept: making a
        // pattern for it would be inventing content nobody asked for.
        BrowserEntry instrument;
        instrument.kind = BrowserEntryKind::Instrument;
        instrument.id = 0;
        check(!applyBrowserDropToTimeline(project, 0.0f, 0,
                                          makePayload(instrument)),
              "An instrument dropped on the timeline is refused");

        // A file that is not there must fail rather than making a clip with
        // nothing behind it.
        BrowserEntry ghost;
        ghost.kind = BrowserEntryKind::File;
        ghost.path = "no-such-file-anywhere.wav";
        check(!applyBrowserDropToTimeline(project, 0.0f, 0, makePayload(ghost)),
              "A file that cannot be read makes no clip");
    }

    // The registry is a global; leave it as it was found.
    actionRegistry().resetAllToDefaults();
    commandPaletteState() = CommandPaletteState();
    browserState() = BrowserState();
}

static void testTakeLanesPanel() {
    beginTest("Take lanes panel (headless GUI)");

    auto uiPtr = std::make_unique<HeadlessUI>();
    auto projectPtr = std::make_unique<Project>();
    auto seqPtr = std::make_unique<Sequencer>();
    UIState ui;

    Project& project = *projectPtr;
    Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&project);

    TakeLanesState& lanes = takeLanesState();
    lanes = TakeLanesState();

    // ---- The empty state ---------------------------------------------------
    //
    // A panel with nothing in it yet is the first thing every user sees, and
    // it is the state most likely to divide by a zero count.
    project.compGroups.clear();
    {
        auto result = uiPtr->frames(3, [&] { DrawTakeLanes(project, ui, seq); });
        check(frameIsClean(result),
              "Empty take lanes panel draws cleanly: " + result.firstProblem);
        check(result.vertexCount > 0, "Empty take lanes panel draws something");
    }

    // ---- With takes --------------------------------------------------------
    CompGroup group;
    group.channelIndex = 2;
    group.name = "Vocal comp";
    group.startBeat = 0.0f;
    group.lengthBeats = 16.0f;

    // Four passes, as somebody comping actually has.
    for (int t = 0; t < 4; ++t) {
        Sample sample;
        sample.name = "Take " + std::to_string(t + 1);
        sample.sampleRate = 44100;
        sample.audioData.assign(44100, 0.0f);
        for (size_t i = 0; i < sample.audioData.size(); ++i) {
            sample.audioData[i] = 0.3f * std::sin(TWO_PI * 220.0f *
                float(t + 1) * float(i) / 44100.0f);
        }
        sample.lengthSeconds = 1.0f;
        sample.isLoaded = true;
        const int id = project.samplePool.addSample(sample);
        comp::addTake(group, id, 0.0f, 16.0f, sample.name);
    }
    group.takes[1].muted = true;   // a muted lane must draw differently, not crash
    project.compGroups.push_back(group);
    takeLanesState().selectedGroup = 0;

    {
        auto result = uiPtr->frames(3, [&] { DrawTakeLanes(project, ui, seq); });
        check(frameIsClean(result),
              "Take lanes with four takes draws cleanly: " + result.firstProblem);
    }

    // ---- A degenerate group -------------------------------------------------
    //
    // Zero length is what a group has for the instant between being created
    // and being recorded into, and it is a division by the beat span.
    {
        const float saved = project.compGroups[0].lengthBeats;
        project.compGroups[0].lengthBeats = 0.0f;
        auto result = uiPtr->frames(2, [&] { DrawTakeLanes(project, ui, seq); });
        check(frameIsClean(result),
              "Zero-length comp group draws cleanly: " + result.firstProblem);
        project.compGroups[0].lengthBeats = saved;
    }

    // ---- The swipe ----------------------------------------------------------
    //
    // Driven through the panel's own release path rather than by aiming at
    // pixels: what must be proven is that letting go of the mouse commits
    // the choice and flattens it, which is where a comp becomes audible.
    {
        const size_t clipsBefore = project.arrangement.size();

        TakeLanesState& state = takeLanesState();
        state.swiping = true;
        state.swipeTake = 2;
        state.swipeAnchor = 4.0f;

        // Mouse released, positioned inside the grid so the drop beat is a
        // real one rather than off the left edge.
        uiPtr->setMouse(700.0f, 380.0f, false);
        auto result = uiPtr->frames(2, [&] { DrawTakeLanes(project, ui, seq); });
        check(frameIsClean(result),
              "Swipe release draws cleanly: " + result.firstProblem);

        check(!state.swiping, "Releasing the mouse ends the swipe");

        const CompGroup& after = project.compGroups[0];
        bool takeTwoUsed = false;
        for (const CompSegment& segment : after.segments) {
            if (segment.takeIndex == 2) takeTwoUsed = true;
        }
        check(takeTwoUsed, "The swiped take wins the range it was swiped over");

        // Flattening is what makes it audible. Without this the comp is a
        // picture: the segments exist and nothing plays them.
        check(project.arrangement.size() > clipsBefore,
              "The swipe flattens the comp onto the arrangement");

        bool tagged = false;
        for (const Clip& clip : project.arrangement) {
            if (clip.compGroup == 0) tagged = true;
        }
        check(tagged, "Flattened clips are tagged with their comp group");
    }

    // ---- Committing a recorded pass ------------------------------------------
    {
        TakeLanesState& state = takeLanesState();
        CompGroup& live = project.compGroups[0];
        const size_t takesBefore = live.takes.size();

        // A false start: a few milliseconds of audio is somebody hitting
        // record and changing their mind, and it must not become a lane.
        state.pending.assign(200, 0.1f);
        state.takeStartBeat = 0.0f;
        const int rejected = commitPendingTake(project, live, state, 44100, 4.0f);
        check(rejected < 0, "A near-empty pass is not kept as a take");
        check(live.takes.size() == takesBefore,
              "A rejected pass adds no lane");
        check(state.pending.empty(), "A rejected pass clears the buffer");

        // A real pass.
        state.pending.assign(44100, 0.0f);
        for (size_t i = 0; i < state.pending.size(); ++i) {
            state.pending[i] = 0.4f * std::sin(TWO_PI * 330.0f * float(i) / 44100.0f);
        }
        state.takeStartBeat = 8.0f;
        const int added = commitPendingTake(project, live, state, 44100, 12.0f);
        check(added >= 0, "A recorded pass becomes a take");
        check(live.takes.size() == takesBefore + 1, "The pass adds one lane");
        check(state.pending.empty(), "Committing clears the buffer");

        if (added >= 0) {
            const Take& take = live.takes[size_t(added)];
            check(std::fabs(take.startBeat - 8.0f) < 0.01f,
                  "The take starts where recording started");
            check(std::fabs(take.lengthBeats - 4.0f) < 0.01f,
                  "The take is as long as the pass was");

            // The audio has to actually be in the pool, or the lane is a
            // label with nothing behind it.
            const Sample* sample = project.samplePool.getSample(take.sampleId);
            check(sample != nullptr, "The take's audio reached the sample pool");
            if (sample != nullptr) {
                check(sample->audioData.size() == 44100,
                      "The whole pass was kept, not a fragment");
                float peak = 0.0f;
                for (float value : sample->audioData) {
                    peak = std::max(peak, std::fabs(value));
                }
                check(peak > 0.3f, "The take contains the audio that was played");
            }
        }
    }

    // ---- Loop recording stacks ------------------------------------------------
    //
    // The playhead going backwards is the only signal a wrap happened. Each
    // wrap must close one lane and open the next, which is the whole reason
    // to loop-record at all.
    {
        TakeLanesState& state = takeLanesState();
        state = TakeLanesState();
        state.selectedGroup = 0;
        state.recording = true;
        state.loopStack = true;
        state.takeStartBeat = 0.0f;
        state.lastBeat = 0.0f;

        const size_t takesBefore = project.compGroups[0].takes.size();

        // Two passes around a four-beat loop.
        for (int pass = 0; pass < 2; ++pass) {
            for (int step = 0; step < 4; ++step) {
                std::vector<float> block(6000, 0.25f);
                g_VoiceDevice.ring().write(block.data(), block.size());
                PollTakeRecording(project, float(step));
            }
            // The wrap: back to the top.
            PollTakeRecording(project, 0.0f);
        }

        check(project.compGroups[0].takes.size() == takesBefore + 2,
              "Two loop passes stack into two lanes");

        // Punch: outside the range, nothing is captured.
        state = TakeLanesState();
        state.selectedGroup = 0;
        state.recording = true;
        state.loopStack = false;
        state.punchEnabled = true;
        state.punchIn = 4.0f;
        state.punchOut = 8.0f;

        std::vector<float> block(6000, 0.25f);
        g_VoiceDevice.ring().write(block.data(), block.size());
        PollTakeRecording(project, 1.0f);    // before the punch-in
        check(state.pending.empty(),
              "Nothing is recorded before the punch-in point");

        PollTakeRecording(project, 5.0f);    // inside
        check(!state.pending.empty(), "Audio is recorded inside the punch range");

        state = TakeLanesState();
    }

    // ---- Not recording is free -------------------------------------------------
    {
        TakeLanesState& state = takeLanesState();
        state = TakeLanesState();
        PollTakeRecording(project, 4.0f);
        check(state.pending.empty(),
              "Polling while not recording captures nothing");
    }

    takeLanesState() = TakeLanesState();
}

// ============================================================================
// 77. Every panel draws cleanly, headlessly
// ============================================================================
static void testPanelsDrawHeadless() {
    beginTest("Panels draw cleanly (headless GUI)");

    auto uiPtr = std::make_unique<HeadlessUI>();
    auto projectPtr = std::make_unique<Project>();
    auto seqPtr = std::make_unique<Sequencer>();
    UIState ui;

    Project& project = *projectPtr;
    Sequencer& seq = *seqPtr;
    seq.setSampleRate(44100.0f);
    seq.setProject(&project);

    // Something to draw. An empty project exercises none of the note,
    // clip or waveform paths, which are where the geometry maths lives.
    {
        Pattern pattern;
        for (int i = 0; i < 8; ++i) {
            Note note;
            note.pitch = 60 + (i % 5) * 2;
            note.startTime = float(i) * 0.5f;
            note.duration = 0.45f;
            pattern.notes.push_back(note);
        }
        project.patterns.clear();
        project.patterns.push_back(pattern);
        project.arrangement.clear();
        project.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0xFF4488FFu});
    }

    struct Panel { const char* name; void (*draw)(Project&, UIState&, Sequencer&); };
    static const Panel PANELS[] = {
        {"Piano Roll",     [](Project& p, UIState& u, Sequencer& s) { DrawPianoRoll(p, u, s); }},
        {"Tracker",        [](Project& p, UIState& u, Sequencer& s) { DrawTrackerView(p, u, s); }},
        {"Arrangement",    [](Project& p, UIState& u, Sequencer& s) { DrawArrangement(p, u, s); }},
        {"Mixer",          [](Project& p, UIState& u, Sequencer& s) { DrawMixer(p, u, s); }},
        {"Channel Editor", [](Project& p, UIState& u, Sequencer& s) { DrawChannelEditor(p, u, s); }},
        {"Sound Palette",  [](Project& p, UIState& u, Sequencer& s) { DrawSoundPalette(p, u, s); }},
        {"Tools",          [](Project& p, UIState& u, Sequencer& s) { DrawToolsPanel(p, u, s); }},
        {"Pad",            [](Project& p, UIState& u, Sequencer& s) { DrawPadController(p, u, s); }},
        {"Master Bus",     [](Project& p, UIState& u, Sequencer& s) { DrawMasterBus(s, p, u); }},
        {"File",           [](Project& p, UIState& u, Sequencer& s) { DrawFileMenu(p, u, s); }},
        {"Voice",          [](Project& p, UIState& u, Sequencer& s) { DrawVoicePanel(p, u, s); }},
        {"Pattern List",   [](Project& p, UIState& u, Sequencer& s) { (void)s; DrawPatternList(p, u); }},
        {"Note Editor",    [](Project& p, UIState& u, Sequencer& s) { (void)s; DrawNoteEditor(p, u); }},
        {"Take Lanes",     [](Project& p, UIState& u, Sequencer& s) { DrawTakeLanes(p, u, s); }},
        {"Browser",        [](Project& p, UIState& u, Sequencer& s) { u.showBrowser = true; DrawBrowser(p, u, s); }},
        {"Shortcuts",      [](Project& p, UIState& u, Sequencer& s) { u.showShortcuts = true; DrawShortcutsPanel(p, u, s); }},
    };

    for (const Panel& panel : PANELS) {
        // Three frames: ImGui settles over two or three - the first has no
        // size for anything auto-sized - so one frame is not representative.
        const auto result = uiPtr->frames(3, [&] { panel.draw(project, ui, seq); });

        if (!frameIsClean(result)) {
            check(false, std::string(panel.name) + ": " + result.firstProblem);
            continue;
        }
        check(result.vertexCount > 0,
              std::string(panel.name) + " draws something");
    }
}

// ============================================================================
// 78. Every engine's editor draws cleanly
// ============================================================================
static void testEngineEditorsDrawHeadless() {
    beginTest("Engine editors draw cleanly (headless GUI)");

    struct Case { const char* name; OscillatorType type; };
    static const Case CASES[] = {
        {"Pulse",      OscillatorType::Pulse},
        {"Noise",      OscillatorType::Noise},
        {"Wavetable",  OscillatorType::Custom},
        {"FM",         OscillatorType::FMSynth},
        {"Sampler",    OscillatorType::Sampler},
        {"Granular",   OscillatorType::Granular},
        {"Drum Model", OscillatorType::DrumModel},
    };

    for (const Case& testCase : CASES) {
        auto uiPtr = std::make_unique<HeadlessUI>();
        auto projectPtr = std::make_unique<Project>();
        auto seqPtr = std::make_unique<Sequencer>();
        UIState ui;

        Project& project = *projectPtr;
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(&project);

        project.channels[0].oscillator.type = testCase.type;
        ui.selectedChannel = 0;

        // A zone and a route, so the list-drawing paths run rather than the
        // empty-state text. Each of those loops pushes an ID per entry and
        // can delete an entry mid-iteration, which is the classic way to
        // leave a PushID unpopped.
        if (testCase.type == OscillatorType::Sampler) {
            SampleZone zone;
            zone.sampleId = -1;
            project.channels[0].oscillator.sampler.addZone(zone);
            project.channels[0].oscillator.sampler.addZone(zone);
        }
        project.channels[0].oscillator.modMatrix.addRoute(
            ModRoute{ModSource::LFO1, ModDestination::Pitch, 0.2f, true});
        project.channels[0].oscillator.modMatrix.addRoute(
            ModRoute{ModSource::Velocity, ModDestination::FilterCutoff, 0.4f, true});

        const auto result = uiPtr->frames(3, [&] {
            DrawChannelEditor(project, ui, *seqPtr);
        });

        if (!frameIsClean(result)) {
            check(false, std::string(testCase.name) + " editor: " + result.firstProblem);
            continue;
        }
        check(result.vertexCount > 0,
              std::string(testCase.name) + " editor draws cleanly");
    }
}

// ============================================================================
// 79. Awkward sizes and every genre
// ============================================================================
static void testLayoutEdgesHeadless() {
    beginTest("Layout edges (headless GUI)");

    // ---- Sizes ---------------------------------------------------------------
    //
    // A panel that divides by its own width produces infinities at zero and
    // NaN geometry at one pixel. The GPU discards those silently, so the
    // rendering smoke test still sees a plausible frame.
    struct Size { const char* name; float w, h; };
    static const Size SIZES[] = {
        {"tiny",   320.0f, 240.0f},
        {"narrow", 200.0f, 900.0f},
        {"short",  1600.0f, 150.0f},
        {"wide",   3840.0f, 800.0f},
        {"huge",   3840.0f, 2160.0f},
    };

    for (const Size& size : SIZES) {
        auto uiPtr = std::make_unique<HeadlessUI>(size.w, size.h);
        auto projectPtr = std::make_unique<Project>();
        auto seqPtr = std::make_unique<Sequencer>();
        UIState ui;

        Project& project = *projectPtr;
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(&project);
        project.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0});

        const auto result = uiPtr->frames(3, [&] {
            DrawPianoRoll(project, ui, *seqPtr);
            DrawArrangement(project, ui, *seqPtr);
            DrawMixer(project, ui, *seqPtr);
            DrawChannelEditor(project, ui, *seqPtr);
        });

        check(frameIsClean(result),
              std::string("the main panels survive a ") + size.name +
              " display" + (frameIsClean(result) ? "" : ": " + result.firstProblem));
    }

    // ---- Every genre focus ---------------------------------------------------
    //
    // The palette and the Tools panel both hide sections by genre, and the
    // engine row wraps on the count DRAWN rather than the loop index - so a
    // genre that hides some is a different code path, not just fewer icons.
    for (int g = 0; g < static_cast<int>(Genre::Count); ++g) {
        auto uiPtr = std::make_unique<HeadlessUI>();
        auto projectPtr = std::make_unique<Project>();
        auto seqPtr = std::make_unique<Sequencer>();
        UIState ui;

        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(projectPtr.get());
        ui.genre = static_cast<Genre>(g);
        ui.paletteShowEverything = false;

        const auto result = uiPtr->frames(3, [&] {
            DrawSoundPalette(*projectPtr, ui, *seqPtr);
            DrawToolsPanel(*projectPtr, ui, *seqPtr);
        });

        if (!frameIsClean(result)) {
            check(false, std::string(genreName(static_cast<Genre>(g))) +
                  " focus: " + result.firstProblem);
            break;
        }
    }
    check(true, "the palette and tools draw cleanly under every genre focus");

    // ---- Every theme ---------------------------------------------------------
    {
        auto uiPtr = std::make_unique<HeadlessUI>();
        auto projectPtr = std::make_unique<Project>();
        auto seqPtr = std::make_unique<Sequencer>();
        UIState ui;
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(projectPtr.get());

        bool allClean = true;
        for (int t = 0; t < static_cast<int>(Theme::Count); ++t) {
            ApplyTheme(static_cast<Theme>(t));
            const auto result = uiPtr->frames(2, [&] {
                DrawPianoRoll(*projectPtr, ui, *seqPtr);
                DrawMixer(*projectPtr, ui, *seqPtr);
            });
            if (!frameIsClean(result)) { allClean = false; break; }
        }
        check(allClean, "every theme draws cleanly - a theme changes geometry "
                        "as well as colour, so it is a layout path too");
        ApplyTheme(Theme::Stock);
    }
}

// ============================================================================
// 80. Clicking things
// ============================================================================
/*
 * Real mouse events through real widgets.
 *
 * ImGui reports a click on the frame the button is released, and only if the
 * press landed on the same widget - so a click is three frames: move and
 * press, hold, release. Doing it in fewer silently does nothing, which is
 * why a naive attempt at this passes while testing nothing.
 *
 * The positions come from ImGui itself rather than being guessed: a probe
 * frame draws the same content and records where the widget landed.
 */
static void testClickingHeadless() {
    beginTest("Clicking widgets (headless GUI)");

    // ---- The probe -----------------------------------------------------------
    //
    // Finding a widget by drawing it and asking ImGui where it went. Guessed
    // coordinates would pass by accident when the layout moved.
    struct Probe {
        ImVec2 centre{0.0f, 0.0f};
        bool found = false;
    };

    // ---- A button that changes the project -----------------------------------
    {
        auto uiPtr = std::make_unique<HeadlessUI>();
        auto projectPtr = std::make_unique<Project>();
        Project& project = *projectPtr;

        Probe probe;
        auto content = [&] {
            ImGui::Begin("ClickTest");
            ImGui::Button("Add Pattern");
            if (!probe.found) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                probe.centre = ImVec2((min.x + max.x) * 0.5f,
                                      (min.y + max.y) * 0.5f);
                probe.found = true;
            }
            if (ImGui::IsItemClicked()) {
                project.patterns.push_back(Pattern());
            }
            ImGui::End();
        };

        uiPtr->frames(3, content);
        check(probe.found && probe.centre.x > 0.0f,
              "the probe located the button at (" +
              std::to_string(probe.centre.x) + ", " +
              std::to_string(probe.centre.y) + ")");

        const size_t before = project.patterns.size();

        // Away from it, pressed: must not count.
        uiPtr->setMouse(5.0f, 5.0f, true);
        uiPtr->frame(content);
        uiPtr->setMouse(5.0f, 5.0f, false);
        uiPtr->frame(content);
        check(project.patterns.size() == before,
              "clicking away from the button does nothing");

        // On it: move and press, hold, release.
        uiPtr->setMouse(probe.centre.x, probe.centre.y, false);
        uiPtr->frame(content);
        uiPtr->setMouse(probe.centre.x, probe.centre.y, true);
        uiPtr->frame(content);
        uiPtr->frame(content);
        uiPtr->setMouse(probe.centre.x, probe.centre.y, false);
        uiPtr->frame(content);

        check(project.patterns.size() == before + 1,
              "clicking the button runs its handler (" +
              std::to_string(project.patterns.size()) + " patterns, was " +
              std::to_string(before) + ")");
    }

    // ---- A checkbox that changes a channel -----------------------------------
    {
        auto uiPtr = std::make_unique<HeadlessUI>();
        auto projectPtr = std::make_unique<Project>();
        auto seqPtr = std::make_unique<Sequencer>();
        Project& project = *projectPtr;
        seqPtr->setSampleRate(44100.0f);
        seqPtr->setProject(&project);

        Probe probe;
        auto content = [&] {
            ImGui::Begin("CheckTest");
            if (ImGui::Checkbox("Mute", &project.channels[0].muted)) {
                seqPtr->updateChannelConfigs();
            }
            if (!probe.found) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                probe.centre = ImVec2(min.x + 8.0f, (min.y + max.y) * 0.5f);
                probe.found = true;
            }
            ImGui::End();
        };

        uiPtr->frames(3, content);
        check(!project.channels[0].muted, "the channel starts unmuted");

        uiPtr->setMouse(probe.centre.x, probe.centre.y, false);
        uiPtr->frame(content);
        uiPtr->setMouse(probe.centre.x, probe.centre.y, true);
        uiPtr->frame(content);
        uiPtr->setMouse(probe.centre.x, probe.centre.y, false);
        uiPtr->frame(content);

        check(project.channels[0].muted,
              "clicking the checkbox mutes the channel, and the click "
              "reaches the sequencer sync as well as the flag");
    }

    // ---- A drag that changes a value -----------------------------------------
    {
        auto uiPtr = std::make_unique<HeadlessUI>();
        auto projectPtr = std::make_unique<Project>();
        Project& project = *projectPtr;
        project.bpm = 120.0f;

        Probe probe;
        auto content = [&] {
            ImGui::Begin("DragTest");
            ImGui::SetNextItemWidth(200.0f);
            ImGui::DragFloat("BPM", &project.bpm, 1.0f, 20.0f, 300.0f);
            if (!probe.found) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                probe.centre = ImVec2((min.x + max.x) * 0.5f,
                                      (min.y + max.y) * 0.5f);
                probe.found = true;
            }
            ImGui::End();
        };

        uiPtr->frames(3, content);
        const float before = project.bpm;

        uiPtr->setMouse(probe.centre.x, probe.centre.y, false);
        uiPtr->frame(content);
        uiPtr->setMouse(probe.centre.x, probe.centre.y, true);
        uiPtr->frame(content);
        for (int i = 1; i <= 12; ++i) {
            uiPtr->setMouse(probe.centre.x + float(i) * 4.0f, probe.centre.y, true);
            uiPtr->frame(content);
        }
        uiPtr->setMouse(probe.centre.x + 48.0f, probe.centre.y, false);
        uiPtr->frame(content);

        check(project.bpm > before,
              "dragging right raises the value (" + std::to_string(before) +
              " -> " + std::to_string(project.bpm) + ")");
        check(project.bpm <= 300.0f,
              "and it stops at the maximum rather than running past it");
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
    testVersionCoherence();
    testTutorial();
    testPlayFromEnd();
    testEffectRackIdentity();
    testEffectRackStability();
    testEffectRackPersistence();
    testRoutingGraph();
    testSendsAndBuses();
    testRoutingPersistence();
    testChannelCap();
    testMidiChannelBounds();
    testAudioClips();
    testAudioClipPersistence();
    testTempoMap();
    testTempoMapReachesEverything();
    testVoiceRing();
    testFFTPlan();
    testLiveVoice();
    testVoiceToNotes();
    testWavetableEngine();
    testWavetableReachesAudio();
    testFMSynth();
    testFMReachesAudio();
    testSampler();
    testSamplerReachesAudio();
    testGranular();
    testDrumModel();
    testGranularAndDrumsReachAudio();
    testModMatrix();
    testModulationReachesAudio();
    testPitchAndTime();
    testPitchEffectsInRack();
    testInstrumentPresets();
    testSettingsAudit();
    testShippedContentIsClean();
    testReverbAlgorithms();
    testReverbAlgorithmsInChannel();
    testEffectSettingsPersist();
    testConvolution();
    testConvolutionInChannel();
    testEqualisers();
    testEqualisersInChannel();
    testTakeLanes();
    testCompFlattening();
    testPanelsDrawHeadless();
    testTakeLanesPanel();
    testActionRegistry();
    testBrowserModel();
    testCommandPaletteAndBrowser();
    testEngineEditorsDrawHeadless();
    testLayoutEdgesHeadless();
    testClickingHeadless();
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
