#pragma once

/*
 * ChiptuneTracker - Modulation matrix
 *
 * Any source to any destination. The tracker has had no modulation routing
 * at all: instrument macros can step a few fixed parameters on a fixed
 * clock, and automation can draw a curve for a channel parameter, but
 * nothing could say "this LFO controls that operator's brightness" or "how
 * hard I play opens the filter".
 *
 * This is the piece that multiplies the value of every engine added in Task
 * F. A wavetable whose morph does not move is a sampled waveform. An FM
 * patch whose index is fixed is one timbre. A granular voice whose position
 * is static is a loop. The engines are worth having because something can
 * move them, and until now nothing could.
 *
 * Two scopes, and the split is forced by where the parameters live.
 *
 * PER-VOICE destinations are the ones each sounding note owns: its pitch,
 * its level, its wavetable morph, its FM brightness. Each voice has its own
 * LFO phases and its own second envelope, so two notes held together do not
 * wobble in lockstep - which is most of what makes a modulated pad sound
 * like an instrument rather than a chorus effect.
 *
 * PER-CHANNEL destinations are the ones that live on the channel strip and
 * are applied once to the summed mix: the filter cutoff and resonance sit in
 * the insert rack, after the voices are added together, so there is no
 * per-voice version of them to modulate. They get their own modulation state
 * advanced once per sample for the whole channel.
 *
 * Destinations that cannot be reached are deliberately absent rather than
 * present and inert. A control that silently does nothing is the bug this
 * codebase keeps finding - the dead stereo widener, the dead sidechain, the
 * wavetable editor that drove nothing.
 *
 * Fixed arrays, no allocation, no locks: this is evaluated per sample per
 * voice on the audio thread.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ChiptuneTracker {

// ============================================================================
// Sources and destinations
// ============================================================================
enum class ModSource : uint8_t {
    None = 0,
    LFO1,
    LFO2,
    LFO3,
    Envelope2,      // a second envelope, free to be any shape
    Velocity,
    KeyTrack,       // how far up the keyboard the note is, -1..1 around C4
    ModWheel,
    PitchBend,
    RandomPerNote,  // one value chosen when the note starts
    Count
};

enum class ModDestination : uint8_t {
    None = 0,

    // ---- Per voice -------------------------------------------------------
    Pitch,          // semitones
    Level,          // multiplier offset
    PulseWidth,
    WavetableMorph,
    FMBrightness,   // the modulation index
    GrainPosition,
    GrainDensity,

    // ---- Per channel -----------------------------------------------------
    //
    // These live in the insert rack, which runs on the summed mix, so they
    // are modulated once for the channel rather than per note.
    FilterCutoff,
    FilterResonance,

    Count
};

// True when a destination is applied to the channel rather than the voice.
inline bool isChannelDestination(ModDestination destination) {
    return destination == ModDestination::FilterCutoff ||
           destination == ModDestination::FilterResonance;
}

inline const char* modSourceName(ModSource source) {
    switch (source) {
        case ModSource::LFO1:          return "LFO 1";
        case ModSource::LFO2:          return "LFO 2";
        case ModSource::LFO3:          return "LFO 3";
        case ModSource::Envelope2:     return "Envelope 2";
        case ModSource::Velocity:      return "Velocity";
        case ModSource::KeyTrack:      return "Key track";
        case ModSource::ModWheel:      return "Mod wheel";
        case ModSource::PitchBend:     return "Pitch bend";
        case ModSource::RandomPerNote: return "Random per note";
        case ModSource::None:
        default:                       return "-";
    }
}

inline const char* modDestinationName(ModDestination destination) {
    switch (destination) {
        case ModDestination::Pitch:           return "Pitch";
        case ModDestination::Level:           return "Level";
        case ModDestination::PulseWidth:      return "Pulse width";
        case ModDestination::WavetableMorph:  return "Wavetable morph";
        case ModDestination::FMBrightness:    return "FM brightness";
        case ModDestination::GrainPosition:   return "Grain position";
        case ModDestination::GrainDensity:    return "Grain density";
        case ModDestination::FilterCutoff:    return "Filter cutoff";
        case ModDestination::FilterResonance: return "Filter resonance";
        case ModDestination::None:
        default:                              return "-";
    }
}

// How far a full-scale source moves each destination. Without this every
// destination would need the amount slider recalibrated by hand: 1.0 of
// pitch is a semitone, but 1.0 of filter cutoff should be most of the range.
inline float modDestinationScale(ModDestination destination) {
    switch (destination) {
        case ModDestination::Pitch:           return 24.0f;    // semitones
        case ModDestination::Level:           return 1.0f;
        case ModDestination::PulseWidth:      return 0.45f;
        case ModDestination::WavetableMorph:  return 1.0f;
        case ModDestination::FMBrightness:    return 8.0f;
        case ModDestination::GrainPosition:   return 1.0f;
        case ModDestination::GrainDensity:    return 150.0f;   // grains/sec
        case ModDestination::FilterCutoff:    return 8000.0f;  // Hz
        case ModDestination::FilterResonance: return 0.9f;
        case ModDestination::None:
        default:                              return 0.0f;
    }
}

// ============================================================================
// LFOs
// ============================================================================
enum class LFOShape : uint8_t { Sine = 0, Triangle, Saw, Square, SampleHold, Count };

inline const char* lfoShapeName(LFOShape shape) {
    switch (shape) {
        case LFOShape::Triangle:   return "Triangle";
        case LFOShape::Saw:        return "Saw";
        case LFOShape::Square:     return "Square";
        case LFOShape::SampleHold: return "Sample & hold";
        case LFOShape::Sine:
        default:                   return "Sine";
    }
}

struct LFOConfig {
    LFOShape shape = LFOShape::Sine;
    float rateHz = 5.0f;

    // Seconds before the LFO reaches full depth. Vibrato that starts the
    // instant a note does sounds mechanical; every wind and string player
    // brings it in after the note has spoken.
    float delaySeconds = 0.0f;
    float fadeSeconds = 0.0f;

    // Restart the phase on every note. Off, the LFOs free-run and notes
    // played together are at different points in the cycle - which is what
    // you want for a pad and not for a rhythmic sync.
    bool retrigger = true;
};

// ============================================================================
// One route
// ============================================================================
struct ModRoute {
    ModSource source = ModSource::None;
    ModDestination destination = ModDestination::None;
    float amount = 0.0f;     // -1..1
    bool enabled = true;

    bool active() const {
        return enabled && source != ModSource::None &&
               destination != ModDestination::None && amount != 0.0f;
    }
};

// ============================================================================
// The matrix
// ============================================================================
struct ModMatrix {
    static constexpr int MAX_ROUTES = 16;
    static constexpr int LFO_COUNT = 3;

    std::array<ModRoute, MAX_ROUTES> routes{};
    int routeCount = 0;

    std::array<LFOConfig, LFO_COUNT> lfos{};

    // The second envelope. Free to be any shape, which is the point: the
    // amplitude envelope has to fit the note, and this one does not.
    float env2Attack = 0.01f;
    float env2Decay = 0.3f;
    float env2Sustain = 0.5f;
    float env2Release = 0.3f;

    /*
     * How many notes this channel may sound at once. 0 means the synth's own
     * limit. A limit is not only about cost: a monophonic bass that steals
     * its own voice is a different instrument from one that stacks, and a
     * granular pad at 32 voices is a wall rather than a texture.
     */
    int polyphonyLimit = 0;

    // Pitch bend range in semitones. Two is the near-universal default and
    // what a controller expects when nobody has said otherwise.
    float pitchBendSemitones = 2.0f;

    bool addRoute(const ModRoute& route) {
        if (routeCount >= MAX_ROUTES) return false;
        routes[static_cast<size_t>(routeCount++)] = route;
        return true;
    }

    bool anyActive() const {
        for (int i = 0; i < routeCount; ++i) {
            if (routes[static_cast<size_t>(i)].active()) return true;
        }
        return false;
    }
};

// ============================================================================
// Per-voice (or per-channel) modulation state
// ============================================================================
struct ModState {
    std::array<float, ModMatrix::LFO_COUNT> lfoPhase{};
    std::array<float, ModMatrix::LFO_COUNT> holdValue{};   // for sample & hold
    std::array<float, ModMatrix::LFO_COUNT> lastHoldPhase{};

    float time = 0.0f;           // seconds since the note started
    float env2Time = 0.0f;
    bool env2Released = false;
    float randomValue = 0.0f;    // chosen once, when the note starts

    uint32_t rng = 0x6D2B79F5u;

    void start(const ModMatrix& matrix, uint32_t seed) {
        rng = (seed == 0u) ? 0x6D2B79F5u : seed;
        for (int i = 0; i < ModMatrix::LFO_COUNT; ++i) {
            if (matrix.lfos[static_cast<size_t>(i)].retrigger) {
                lfoPhase[static_cast<size_t>(i)] = 0.0f;
            }
            holdValue[static_cast<size_t>(i)] = nextRandom();
            lastHoldPhase[static_cast<size_t>(i)] = 0.0f;
        }
        time = 0.0f;
        env2Time = 0.0f;
        env2Released = false;
        randomValue = nextRandom();
    }

    void release() {
        if (!env2Released) {
            env2Released = true;
            env2Time = 0.0f;
        }
    }

    // Its own generator rather than rand(), which is not guaranteed
    // reentrant and carries hidden state shared across threads.
    float nextRandom() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (static_cast<float>(rng & 0x00FFFFFFu) /
                static_cast<float>(0x00800000u)) - 1.0f;
    }
};

// ============================================================================
// The resolved offsets
// ============================================================================
struct ModValues {
    float pitchSemitones = 0.0f;
    float level = 0.0f;
    float pulseWidth = 0.0f;
    float wavetableMorph = 0.0f;
    float fmBrightness = 0.0f;
    float grainPosition = 0.0f;
    float grainDensity = 0.0f;
    float filterCutoff = 0.0f;
    float filterResonance = 0.0f;
};

// What the performer is doing with the controls, shared by every voice.
struct PerformanceState {
    float modWheel = 0.0f;    // 0..1
    float pitchBend = 0.0f;   // -1..1
};

namespace mod {

inline constexpr float TWO_PI = 6.28318530718f;

inline float lfoValue(const LFOConfig& config, ModState& state, int index) {
    const size_t i = static_cast<size_t>(index);
    const float phase = state.lfoPhase[i];

    float raw = 0.0f;
    switch (config.shape) {
        case LFOShape::Triangle:
            raw = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
            break;
        case LFOShape::Saw:
            raw = 2.0f * phase - 1.0f;
            break;
        case LFOShape::Square:
            raw = (phase < 0.5f) ? 1.0f : -1.0f;
            break;
        case LFOShape::SampleHold:
            // A new random value once per cycle. Detected by the phase
            // wrapping rather than by a counter, so it stays correct when
            // the rate changes mid-note.
            if (phase < state.lastHoldPhase[i]) {
                state.holdValue[i] = state.nextRandom();
            }
            state.lastHoldPhase[i] = phase;
            raw = state.holdValue[i];
            break;
        case LFOShape::Sine:
        default:
            raw = std::sin(TWO_PI * phase);
            break;
    }

    // Delay then fade in. Vibrato arriving the instant a note starts sounds
    // mechanical; players bring it in after the note has spoken.
    float depth = 1.0f;
    if (config.delaySeconds > 0.0f && state.time < config.delaySeconds) {
        depth = 0.0f;
    } else if (config.fadeSeconds > 1e-4f) {
        const float into = state.time - config.delaySeconds;
        depth = std::clamp(into / config.fadeSeconds, 0.0f, 1.0f);
    }
    return raw * depth;
}

inline float envelope2(const ModMatrix& matrix, const ModState& state) {
    if (state.env2Released) {
        if (matrix.env2Release <= 1e-5f) return 0.0f;
        const float through = std::clamp(state.env2Time / matrix.env2Release,
                                         0.0f, 1.0f);
        return matrix.env2Sustain * (1.0f - through);
    }
    if (state.env2Time < matrix.env2Attack) {
        return (matrix.env2Attack > 1e-6f)
            ? state.env2Time / matrix.env2Attack : 1.0f;
    }
    const float intoDecay = state.env2Time - matrix.env2Attack;
    if (matrix.env2Decay > 1e-6f && intoDecay < matrix.env2Decay) {
        return 1.0f + (matrix.env2Sustain - 1.0f) * (intoDecay / matrix.env2Decay);
    }
    return matrix.env2Sustain;
}

inline float sourceValue(const ModMatrix& matrix, ModState& state,
                         ModSource source, int note, float velocity,
                         const PerformanceState& performance) {
    switch (source) {
        case ModSource::LFO1:
            return lfoValue(matrix.lfos[0], state, 0);
        case ModSource::LFO2:
            return lfoValue(matrix.lfos[1], state, 1);
        case ModSource::LFO3:
            return lfoValue(matrix.lfos[2], state, 2);
        case ModSource::Envelope2:
            return envelope2(matrix, state);
        case ModSource::Velocity:
            return std::clamp(velocity, 0.0f, 1.0f);
        case ModSource::KeyTrack:
            // -1 at the bottom of the keyboard, +1 at the top, 0 at C4.
            return std::clamp((static_cast<float>(note) - 60.0f) / 60.0f,
                              -1.0f, 1.0f);
        case ModSource::ModWheel:
            return std::clamp(performance.modWheel, 0.0f, 1.0f);
        case ModSource::PitchBend:
            return std::clamp(performance.pitchBend, -1.0f, 1.0f);
        case ModSource::RandomPerNote:
            return state.randomValue;
        case ModSource::None:
        default:
            return 0.0f;
    }
}

/*
 * Advance the state and resolve every route.
 *
 * `channelScope` picks which half of the destination list is filled in. The
 * two are evaluated separately because they are applied in different places:
 * per-voice offsets go into the voice's own parameters, and per-channel ones
 * into the insert rack once for the summed mix.
 *
 * Audio thread. Fixed arrays, no allocation.
 */
inline ModValues evaluate(const ModMatrix& matrix, ModState& state,
                          int note, float velocity,
                          const PerformanceState& performance,
                          float sampleRate, bool channelScope) {
    ModValues values;
    if (sampleRate <= 0.0f) return values;

    const float step = 1.0f / sampleRate;

    for (int i = 0; i < ModMatrix::LFO_COUNT; ++i) {
        const LFOConfig& config = matrix.lfos[static_cast<size_t>(i)];
        state.lfoPhase[static_cast<size_t>(i)] += config.rateHz * step;
        if (state.lfoPhase[static_cast<size_t>(i)] >= 1.0f) {
            state.lfoPhase[static_cast<size_t>(i)] -=
                std::floor(state.lfoPhase[static_cast<size_t>(i)]);
        }
    }
    state.time += step;
    state.env2Time += step;

    for (int i = 0; i < matrix.routeCount; ++i) {
        const ModRoute& route = matrix.routes[static_cast<size_t>(i)];
        if (!route.active()) continue;
        if (isChannelDestination(route.destination) != channelScope) continue;

        const float value = sourceValue(matrix, state, route.source, note,
                                        velocity, performance);
        const float amount = value * route.amount *
                             modDestinationScale(route.destination);

        switch (route.destination) {
            case ModDestination::Pitch:           values.pitchSemitones += amount; break;
            case ModDestination::Level:           values.level += amount; break;
            case ModDestination::PulseWidth:      values.pulseWidth += amount; break;
            case ModDestination::WavetableMorph:  values.wavetableMorph += amount; break;
            case ModDestination::FMBrightness:    values.fmBrightness += amount; break;
            case ModDestination::GrainPosition:   values.grainPosition += amount; break;
            case ModDestination::GrainDensity:    values.grainDensity += amount; break;
            case ModDestination::FilterCutoff:    values.filterCutoff += amount; break;
            case ModDestination::FilterResonance: values.filterResonance += amount; break;
            case ModDestination::None:
            default: break;
        }
    }
    return values;
}

} // namespace mod

// ============================================================================
// Validation
// ============================================================================
inline void clampModMatrix(ModMatrix& matrix) {
    auto sane = [](float value, float lo, float hi, float fallback) {
        if (!std::isfinite(value)) return fallback;
        return std::max(lo, std::min(value, hi));
    };

    matrix.routeCount = std::clamp(matrix.routeCount, 0, ModMatrix::MAX_ROUTES);

    for (int i = 0; i < matrix.routeCount; ++i) {
        ModRoute& route = matrix.routes[static_cast<size_t>(i)];
        // A source or destination this build does not have becomes None
        // rather than wrapping onto a different one, which would silently
        // route a file's LFO to whatever happened to land at that index.
        if (static_cast<int>(route.source) < 0 ||
            route.source >= ModSource::Count) {
            route.source = ModSource::None;
        }
        if (static_cast<int>(route.destination) < 0 ||
            route.destination >= ModDestination::Count) {
            route.destination = ModDestination::None;
        }
        route.amount = sane(route.amount, -1.0f, 1.0f, 0.0f);
    }

    for (LFOConfig& lfo : matrix.lfos) {
        if (static_cast<int>(lfo.shape) < 0 || lfo.shape >= LFOShape::Count) {
            lfo.shape = LFOShape::Sine;
        }
        // Above about 40 Hz an LFO is an audio-rate oscillator, which is a
        // legitimate sound but not what any of these destinations expect.
        lfo.rateHz = sane(lfo.rateHz, 0.01f, 40.0f, 5.0f);
        lfo.delaySeconds = sane(lfo.delaySeconds, 0.0f, 10.0f, 0.0f);
        lfo.fadeSeconds = sane(lfo.fadeSeconds, 0.0f, 10.0f, 0.0f);
    }

    matrix.env2Attack = sane(matrix.env2Attack, 0.0f, 10.0f, 0.01f);
    matrix.env2Decay = sane(matrix.env2Decay, 0.0f, 20.0f, 0.3f);
    matrix.env2Sustain = sane(matrix.env2Sustain, 0.0f, 1.0f, 0.5f);
    matrix.env2Release = sane(matrix.env2Release, 0.0f, 20.0f, 0.3f);
    matrix.polyphonyLimit = std::clamp(matrix.polyphonyLimit, 0, 64);
    matrix.pitchBendSemitones = sane(matrix.pitchBendSemitones, 0.0f, 48.0f, 2.0f);
}

} // namespace ChiptuneTracker
