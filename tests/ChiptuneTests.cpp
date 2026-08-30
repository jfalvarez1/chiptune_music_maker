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

    auto patternWithNotes = [](int count) {
        Pattern p;
        for (int i = 0; i < count; ++i) {
            Note n;
            n.pitch = 60 + i;
            n.startTime = float(i);
            p.notes.push_back(n);
        }
        return p;
    };

    // ---- Nothing to undo -----------------------------------------------
    {
        UndoHistory history;
        check(!history.canUndo(), "a fresh history has nothing to undo");
        check(!history.canRedo(), "a fresh history has nothing to redo");

        Pattern current = patternWithNotes(3);
        PatternSnapshot result = history.undo(current, 0);
        check(result.notes.size() == 3,
              "undo with an empty stack returns the current state unchanged");
        result = history.redo(current, 0);
        check(result.notes.size() == 3,
              "redo with an empty stack returns the current state unchanged");
    }

    // ---- Basic undo then redo -------------------------------------------
    {
        UndoHistory history;
        Pattern first = patternWithNotes(2);
        history.saveState(first, 0);

        Pattern second = patternWithNotes(5);
        check(history.canUndo(), "saving a state makes undo available");

        PatternSnapshot undone = history.undo(second, 0);
        check(undone.notes.size() == 2,
              "undo restores the saved state (expected 2 notes, got " +
              std::to_string(undone.notes.size()) + ")");
        check(history.canRedo(), "undo makes redo available");

        PatternSnapshot redone = history.redo(Pattern(first), 0);
        check(redone.notes.size() == 5,
              "redo restores the state undo replaced (expected 5, got " +
              std::to_string(redone.notes.size()) + ")");
    }

    // ---- A new action clears the redo stack ------------------------------
    {
        UndoHistory history;
        history.saveState(patternWithNotes(1), 0);
        history.undo(patternWithNotes(2), 0);
        check(history.canRedo(), "redo is available after an undo");

        history.saveState(patternWithNotes(3), 0);
        check(!history.canRedo(),
              "a new edit after an undo clears the redo stack - redoing onto a "
              "diverged timeline would restore notes the user did not make");
    }

    // ---- The history is capped -------------------------------------------
    {
        UndoHistory history;
        for (int i = 0; i < UndoHistory::MAX_HISTORY + 25; ++i) {
            history.saveState(patternWithNotes(i % 7 + 1), 0);
        }
        check(history.undoStack.size() == static_cast<size_t>(UndoHistory::MAX_HISTORY),
              "history is capped at " + std::to_string(UndoHistory::MAX_HISTORY) +
              " (got " + std::to_string(history.undoStack.size()) + ")");

        // And undoing all the way out must not run off the end
        Pattern current = patternWithNotes(4);
        for (int i = 0; i < UndoHistory::MAX_HISTORY + 25; ++i) {
            current.notes = history.undo(current, 0).notes;
        }
        check(!history.canUndo(), "undoing past the start leaves the stack empty");
        check(true, "undoing past the start did not crash");
    }

    // ---- Round trip through many steps preserves content ------------------
    {
        UndoHistory history;
        for (int i = 1; i <= 10; ++i) history.saveState(patternWithNotes(i), 0);

        Pattern current = patternWithNotes(11);
        for (int i = 0; i < 10; ++i) current.notes = history.undo(current, 0).notes;
        check(current.notes.size() == 1,
              "ten undos land on the first saved state (got " +
              std::to_string(current.notes.size()) + ")");

        for (int i = 0; i < 10; ++i) current.notes = history.redo(current, 0).notes;
        check(current.notes.size() == 11,
              "ten redos return to where we started (got " +
              std::to_string(current.notes.size()) + ")");
    }

    // ---- clear() ----------------------------------------------------------
    {
        UndoHistory history;
        history.saveState(patternWithNotes(2), 0);
        history.undo(patternWithNotes(3), 0);
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
