#pragma once

/*
 * ChiptuneTracker - Six-operator FM
 *
 * The tracker fakes FM with two fixed presets, SynthBell and SynthwaveFM,
 * which are hardcoded two-operator arrangements with no editable anything.
 * This is the real thing: six sine operators, a routing algorithm deciding
 * which modulates which, per-operator ratio, level, detune and envelope, and
 * feedback on the first operator.
 *
 * FM is more on-brand for a chip tracker than most of what a modern DAW
 * offers, not less. The YM2612 in the Mega Drive is six-operator FM, the
 * YM2151 in countless arcade boards is eight, and the DX7 that defined the
 * sound of the eighties is six. A chiptune tracker without FM is missing one
 * of the two great families of chip sound.
 *
 * The maths, briefly, because the implementation looks trivial and is easy
 * to get subtly wrong:
 *
 * An operator is a sine oscillator whose phase is offset by its modulators'
 * outputs. output = sin(2*pi*phase + modulation). The modulation index -
 * how much the modulator's output is scaled before being added - is what
 * decides the brightness, and its relationship to the resulting spectrum is
 * the Bessel functions. That is why FM brightness has a character all its
 * own: the harmonics do not simply fade in, they swap places.
 *
 * Two things every implementation has to get right:
 *
 * MODULATION IS PHASE, NOT FREQUENCY. Adding the modulator to the carrier's
 * frequency, then integrating, produces a pitch that drifts with the
 * modulator's DC content. Adding to the phase directly does not. The name
 * "frequency modulation" is historical; every digital FM synth ever shipped
 * is a phase modulator.
 *
 * FEEDBACK NEEDS THE PREVIOUS SAMPLE, AVERAGED. An operator modulating
 * itself with only its last output oscillates violently at high feedback.
 * The classic fix, used by Yamaha, is to average the last two - it is a
 * one-pole lowpass in the feedback path and it turns a screech into the
 * saw-like tone feedback is supposed to give you.
 *
 * Fixed-size, no allocation, no locks: all of this runs per sample per voice
 * on the audio thread.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ChiptuneTracker {

inline constexpr int FM_OPERATORS = 6;

// ============================================================================
// One operator
// ============================================================================
struct FMOperator {
    // Frequency as a ratio of the played note. Integer ratios give harmonic
    // spectra - the reason FM basses and bells sound like they do - and
    // non-integer ones give the inharmonic, metallic timbres.
    float ratio = 1.0f;

    // A fixed frequency in Hz instead of a ratio, for the low rumble under
    // a bass or a fixed formant. -1 means "follow the note".
    float fixedHz = -1.0f;

    float level = 1.0f;         // 0..1, the operator's output gain
    float detuneCents = 0.0f;
    float phaseOffset = 0.0f;   // 0..1

    // Per-operator envelope. FM's expressiveness comes almost entirely from
    // modulators having different envelopes than carriers: a bell is a
    // carrier that rings under a modulator that decays quickly, which is why
    // the attack is metallic and the tail is pure.
    float attack = 0.005f;
    float decay = 0.2f;
    float sustain = 0.7f;
    float release = 0.3f;

    // How much the envelope tracks velocity. A modulator that gets brighter
    // when you play harder is the whole reason FM felt expressive on a
    // keyboard.
    float velocitySensitivity = 0.0f;

    bool enabled = true;
};

// ============================================================================
// The algorithm
// ============================================================================
/*
 * Who modulates whom, and who is heard.
 *
 * A full 6x6 matrix rather than a numbered list of the DX7's 32 algorithms.
 * The matrix is a superset - every one of those 32 is expressible as a
 * matrix - and it takes less code than the table would, while allowing
 * routings Yamaha's ROM did not happen to include.
 *
 * modulation[m][c] is how much operator m modulates operator c. Only the
 * lower triangle is used: operator m may modulate operator c when m > c.
 * That restriction is what makes the whole thing evaluable in one pass from
 * operator 5 down to operator 0, with no feedback loops except the explicit
 * self-feedback on operator 5 - and without it a cycle in the matrix would
 * be an infinite recursion in the audio thread.
 */
struct FMAlgorithm {
    std::array<std::array<float, FM_OPERATORS>, FM_OPERATORS> modulation{};

    // Which operators reach the output, and how loudly.
    std::array<float, FM_OPERATORS> carrier{};

    // Operator 5 modulating itself. 0..1; above about 0.7 it becomes a saw.
    float feedback = 0.0f;

    // ---- The classic shapes ---------------------------------------------

    // Every operator a carrier: six detuned sines, an organ.
    static FMAlgorithm additive() {
        FMAlgorithm algorithm;
        for (int i = 0; i < FM_OPERATORS; ++i) algorithm.carrier[size_t(i)] = 1.0f;
        return algorithm;
    }

    // 5 -> 4 -> 3 -> 2 -> 1 -> 0, one carrier. The brightest and most
    // extreme routing, and the one that goes to noise fastest.
    static FMAlgorithm stack() {
        FMAlgorithm algorithm;
        for (int i = FM_OPERATORS - 1; i > 0; --i) {
            algorithm.modulation[size_t(i)][size_t(i - 1)] = 1.0f;
        }
        algorithm.carrier[0] = 1.0f;
        return algorithm;
    }

    // Three pairs, each a modulator over a carrier. The standard electric
    // piano and bell layout.
    static FMAlgorithm threePairs() {
        FMAlgorithm algorithm;
        algorithm.modulation[1][0] = 1.0f;
        algorithm.modulation[3][2] = 1.0f;
        algorithm.modulation[5][4] = 1.0f;
        algorithm.carrier[0] = 1.0f;
        algorithm.carrier[2] = 1.0f;
        algorithm.carrier[4] = 1.0f;
        return algorithm;
    }

    // One modulator into three carriers - the DX7 brass sound, and what
    // SynthwaveFM was imitating.
    static FMAlgorithm brass() {
        FMAlgorithm algorithm;
        algorithm.modulation[5][0] = 1.0f;
        algorithm.modulation[5][1] = 1.0f;
        algorithm.modulation[5][2] = 1.0f;
        algorithm.carrier[0] = 1.0f;
        algorithm.carrier[1] = 0.8f;
        algorithm.carrier[2] = 0.6f;
        algorithm.feedback = 0.3f;
        return algorithm;
    }

    // Two chains of three. Deep enough for metallic timbres, split enough
    // to keep a fundamental.
    static FMAlgorithm doubleStack() {
        FMAlgorithm algorithm;
        algorithm.modulation[2][1] = 1.0f;
        algorithm.modulation[1][0] = 1.0f;
        algorithm.modulation[5][4] = 1.0f;
        algorithm.modulation[4][3] = 1.0f;
        algorithm.carrier[0] = 1.0f;
        algorithm.carrier[3] = 1.0f;
        return algorithm;
    }

    /*
     * A matrix arriving from a file may have entries in the upper triangle,
     * which would be a cycle. They are dropped rather than clamped: there is
     * no sensible smaller value for "operator 0 modulates operator 5", and
     * evaluating it would need an ordering that does not exist.
     */
    void makeAcyclic() {
        for (int m = 0; m < FM_OPERATORS; ++m) {
            for (int c = 0; c < FM_OPERATORS; ++c) {
                if (m <= c) modulation[size_t(m)][size_t(c)] = 0.0f;
            }
        }
    }
};

// Named presets, so the UI is a list rather than a matrix nobody will edit.
enum class FMAlgorithmPreset : uint8_t {
    Brass = 0, ThreePairs, DoubleStack, Stack, Additive, Count
};

inline FMAlgorithm fmAlgorithmFromPreset(FMAlgorithmPreset preset) {
    switch (preset) {
        case FMAlgorithmPreset::ThreePairs:  return FMAlgorithm::threePairs();
        case FMAlgorithmPreset::DoubleStack: return FMAlgorithm::doubleStack();
        case FMAlgorithmPreset::Stack:       return FMAlgorithm::stack();
        case FMAlgorithmPreset::Additive:    return FMAlgorithm::additive();
        case FMAlgorithmPreset::Brass:
        default:                             return FMAlgorithm::brass();
    }
}

inline const char* fmAlgorithmName(FMAlgorithmPreset preset) {
    switch (preset) {
        case FMAlgorithmPreset::ThreePairs:  return "Three Pairs (bells, e-piano)";
        case FMAlgorithmPreset::DoubleStack: return "Double Stack (metallic)";
        case FMAlgorithmPreset::Stack:       return "Stack (extreme)";
        case FMAlgorithmPreset::Additive:    return "Additive (organ)";
        case FMAlgorithmPreset::Brass:
        default:                             return "Brass (one into three)";
    }
}

// ============================================================================
// A patch
// ============================================================================
struct FMPatch {
    std::array<FMOperator, FM_OPERATORS> operators{};
    FMAlgorithm algorithm = FMAlgorithm::brass();

    // How far a modulator's output is scaled before being added to a
    // carrier's phase, in radians. This is the modulation index, and it is
    // the single most important control on the instrument: it is what
    // "brighter" means in FM.
    float index = 2.0f;

    FMPatch() {
        // A default that sounds like something rather than a sine.
        operators[0].ratio = 1.0f;
        operators[1].ratio = 1.0f;
        operators[2].ratio = 2.0f;
        operators[3].ratio = 3.0f;
        operators[4].ratio = 1.0f;

        // The modulator: a shorter envelope than the carriers, which is what
        // gives FM its characteristic bright attack settling to a purer
        // sustain.
        operators[5].ratio = 2.0f;
        operators[5].attack = 0.001f;
        operators[5].decay = 0.35f;
        operators[5].sustain = 0.25f;
        operators[5].velocitySensitivity = 0.6f;
    }
};

// ============================================================================
// Per-voice state
// ============================================================================
struct FMVoiceState {
    std::array<float, FM_OPERATORS> phase{};

    // Envelope level and elapsed time per operator.
    std::array<float, FM_OPERATORS> envLevel{};
    std::array<float, FM_OPERATORS> envTime{};
    std::array<bool, FM_OPERATORS> released{};

    // The last two outputs of operator 5, for feedback. Two, not one:
    // self-modulation from a single previous sample oscillates violently at
    // high feedback, and averaging the last two is a one-pole lowpass in the
    // feedback path - the fix Yamaha used, and the difference between a saw
    // and a screech.
    float feedback1 = 0.0f;
    float feedback2 = 0.0f;

    void reset(const FMPatch& patch) {
        for (int i = 0; i < FM_OPERATORS; ++i) {
            phase[size_t(i)] = patch.operators[size_t(i)].phaseOffset;
            envLevel[size_t(i)] = 0.0f;
            envTime[size_t(i)] = 0.0f;
            released[size_t(i)] = false;
        }
        feedback1 = 0.0f;
        feedback2 = 0.0f;
    }

    void release() {
        for (int i = 0; i < FM_OPERATORS; ++i) {
            if (!released[size_t(i)]) {
                released[size_t(i)] = true;
                envTime[size_t(i)] = 0.0f;
            }
        }
    }

    bool finished() const {
        for (int i = 0; i < FM_OPERATORS; ++i) {
            if (!released[size_t(i)]) return false;
            if (envLevel[size_t(i)] > 1e-4f) return false;
        }
        return true;
    }
};

// ============================================================================
// The engine
// ============================================================================
namespace fm {

inline constexpr float TWO_PI = 6.28318530718f;

// One operator's ADSR, evaluated at its own elapsed time.
inline float envelopeAt(const FMOperator& op, float time, bool released,
                        float velocity) {
    // Velocity scales the whole envelope, not just the attack: on a
    // modulator that is what makes playing harder sound brighter rather
    // than merely louder, which is the thing FM keyboards were famous for.
    const float scale = 1.0f - op.velocitySensitivity * (1.0f - velocity);

    if (released) {
        if (op.release <= 1e-5f) return 0.0f;
        const float through = std::clamp(time / op.release, 0.0f, 1.0f);
        return op.sustain * (1.0f - through) * scale;
    }
    if (time < op.attack) {
        return (op.attack > 1e-6f) ? (time / op.attack) * scale : scale;
    }
    const float intoDecay = time - op.attack;
    if (intoDecay < op.decay) {
        const float through = (op.decay > 1e-6f) ? intoDecay / op.decay : 1.0f;
        return (1.0f + (op.sustain - 1.0f) * through) * scale;
    }
    return op.sustain * scale;
}

/*
 * One sample of a whole patch.
 *
 * Evaluated from operator 5 down to operator 0, which works precisely
 * because the matrix is lower-triangular: by the time an operator is
 * reached, everything that can modulate it has already produced its output
 * this sample. That ordering is what makes the whole thing a single pass
 * with no recursion and no per-sample sorting.
 *
 * Audio thread: no allocation, no branching on anything that changes shape.
 */
inline float process(const FMPatch& patch, FMVoiceState& state,
                     float baseFrequency, float velocity, float sampleRate,
                     bool released) {
    if (sampleRate <= 0.0f) return 0.0f;

    std::array<float, FM_OPERATORS> output{};
    float mix = 0.0f;

    for (int i = FM_OPERATORS - 1; i >= 0; --i) {
        const size_t index = size_t(i);
        const FMOperator& op = patch.operators[index];
        if (!op.enabled) { output[index] = 0.0f; continue; }

        // How much this operator's phase is displaced by its modulators.
        float modulation = 0.0f;
        for (int m = FM_OPERATORS - 1; m > i; --m) {
            const float amount = patch.algorithm.modulation[size_t(m)][index];
            if (amount != 0.0f) modulation += output[size_t(m)] * amount;
        }
        modulation *= patch.index;

        // Self-feedback on the top operator, from the average of its last
        // two outputs.
        if (i == FM_OPERATORS - 1 && patch.algorithm.feedback > 0.0f) {
            modulation += (state.feedback1 + state.feedback2) * 0.5f *
                          patch.algorithm.feedback * patch.index;
        }

        const float envelope = envelopeAt(op, state.envTime[index],
                                          released, velocity);
        state.envLevel[index] = envelope;

        const float frequency = (op.fixedHz > 0.0f)
            ? op.fixedHz
            : baseFrequency * op.ratio *
              std::pow(2.0f, op.detuneCents / 1200.0f);

        // Phase modulation, not frequency modulation. Adding the modulator
        // to the frequency and integrating makes the pitch drift with the
        // modulator's DC content; adding it to the phase does not.
        const float value = std::sin(TWO_PI * state.phase[index] + modulation);

        output[index] = value * envelope * op.level;

        state.phase[index] += frequency / sampleRate;
        if (state.phase[index] >= 1.0f) {
            state.phase[index] -= std::floor(state.phase[index]);
        }

        const float carrierLevel = patch.algorithm.carrier[index];
        if (carrierLevel != 0.0f) mix += output[index] * carrierLevel;
    }

    state.feedback2 = state.feedback1;
    state.feedback1 = output[FM_OPERATORS - 1];

    // Advance every operator's envelope clock together. One clock per
    // operator rather than one for the voice, because the entire point of
    // per-operator envelopes is that they run at different speeds.
    const float step = 1.0f / sampleRate;
    for (int i = 0; i < FM_OPERATORS; ++i) state.envTime[size_t(i)] += step;

    // Several carriers summing can exceed unity. Divided by the number of
    // ACTIVE carriers rather than by six, or a two-carrier patch would be a
    // third as loud as it should be for no reason the user can see.
    float carriers = 0.0f;
    for (int i = 0; i < FM_OPERATORS; ++i) {
        carriers += std::fabs(patch.algorithm.carrier[size_t(i)]);
    }
    return (carriers > 1.0f) ? mix / carriers : mix;
}

} // namespace fm

// ============================================================================
// Validation
// ============================================================================
inline void clampFMPatch(FMPatch& patch) {
    auto sane = [](float value, float lo, float hi, float fallback) {
        if (!std::isfinite(value)) return fallback;
        return std::max(lo, std::min(value, hi));
    };

    // 32 is well past useful and stops a hand-edited ratio from putting an
    // operator's frequency past Nyquist, where it aliases rather than sounds.
    patch.index = sane(patch.index, 0.0f, 24.0f, 2.0f);
    patch.algorithm.feedback = sane(patch.algorithm.feedback, 0.0f, 1.0f, 0.0f);
    patch.algorithm.makeAcyclic();

    for (FMOperator& op : patch.operators) {
        op.ratio = sane(op.ratio, 0.0f, 32.0f, 1.0f);
        op.fixedHz = std::isfinite(op.fixedHz)
            ? std::min(op.fixedHz, 20000.0f) : -1.0f;
        op.level = sane(op.level, 0.0f, 1.0f, 1.0f);
        op.detuneCents = sane(op.detuneCents, -1200.0f, 1200.0f, 0.0f);
        op.phaseOffset = sane(op.phaseOffset, 0.0f, 1.0f, 0.0f);
        // An attack of exactly zero is a click, and every envelope stage
        // needs a floor or the voice divides by it.
        op.attack = sane(op.attack, 0.0f, 10.0f, 0.005f);
        op.decay = sane(op.decay, 0.0f, 20.0f, 0.2f);
        op.sustain = sane(op.sustain, 0.0f, 1.0f, 0.7f);
        op.release = sane(op.release, 0.0f, 20.0f, 0.3f);
        op.velocitySensitivity = sane(op.velocitySensitivity, 0.0f, 1.0f, 0.0f);
    }

    for (int i = 0; i < FM_OPERATORS; ++i) {
        patch.algorithm.carrier[size_t(i)] =
            sane(patch.algorithm.carrier[size_t(i)], 0.0f, 2.0f, 0.0f);
        for (int c = 0; c < FM_OPERATORS; ++c) {
            patch.algorithm.modulation[size_t(i)][size_t(c)] =
                sane(patch.algorithm.modulation[size_t(i)][size_t(c)],
                     0.0f, 4.0f, 0.0f);
        }
    }

    // A patch with no carrier at all is silence, which reads as the engine
    // being broken rather than as a patch nobody finished.
    bool anyCarrier = false;
    for (int i = 0; i < FM_OPERATORS; ++i) {
        if (patch.algorithm.carrier[size_t(i)] > 0.0f) { anyCarrier = true; break; }
    }
    if (!anyCarrier) patch.algorithm.carrier[0] = 1.0f;
}

} // namespace ChiptuneTracker
