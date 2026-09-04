#pragma once

// ============================================================================
// Wavetable shaping
//
// The wavetable editor could draw a wave by hand and initialise four
// presets. Everything past that - "make this two cycles instead of one",
// "round the corners off", "what would this sound like on a Game Boy" - was
// a matter of redrawing 256 samples with a mouse.
//
// These are the operations that turn drawing into editing. Every one of them
// is arithmetic on a fixed-length array of floats: no allocation, no state,
// nothing that touches the audio thread. They are separated from the UI
// entirely so the arithmetic can be tested, which matters more here than it
// looks - a Scale X that reads one sample past the end of the table wraps
// into silence at the seam and produces a click on every cycle, and that is
// the kind of thing you hear long before you see it.
//
// THE CHIP ONES ARE THE POINT. quantiseLevels and reduceSteps together turn
// any drawn shape into something a real wave channel could hold: the Game
// Boy's is 32 steps of 4 bits, and rounding a smooth wave down to that is
// what makes it sound like a Game Boy rather than like a synthesiser
// pretending. They are exact, not approximate - the output really does only
// contain the values that hardware could store.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ChiptuneTracker {

// ============================================================================
// Reading a table at a fractional position
// ============================================================================
enum class WaveInterp : int {
    None = 0,   // nearest sample: stepped, and the honest choice for chip work
    Linear,
    Cosine,     // linear's corners rounded off, at no extra cost worth naming
    Cubic       // Catmull-Rom; overshoots, which is sometimes what you want
};
inline constexpr int WAVE_INTERP_COUNT = 4;

inline const char* waveInterpName(int mode) {
    switch (mode) {
        case 0: return "None (stepped)";
        case 1: return "Linear";
        case 2: return "Cosine";
        case 3: return "Cubic";
        default: return "Linear";
    }
}

/*
 * Sample a table at a fractional index, wrapping.
 *
 * Wrapping rather than clamping, always. A wavetable is a loop by
 * definition: the sample after the last one is the first one. Clamping at
 * the ends flattens the seam, and a flattened seam is a discontinuity in the
 * slope that every cycle of the oscillator walks over.
 */
inline float waveSampleAt(const float* table, int size, float position,
                          WaveInterp mode) {
    if (table == nullptr || size <= 0) return 0.0f;
    if (size == 1) return table[0];

    auto at = [&](int index) {
        int wrapped = index % size;
        if (wrapped < 0) wrapped += size;
        return table[wrapped];
    };

    const float floored = std::floor(position);
    const int base = static_cast<int>(floored);
    const float fraction = position - floored;

    switch (mode) {
        case WaveInterp::None:
            return at(base);

        case WaveInterp::Linear:
            return at(base) + (at(base + 1) - at(base)) * fraction;

        case WaveInterp::Cosine: {
            const float smooth =
                (1.0f - std::cos(fraction * 3.14159265358979323846f)) * 0.5f;
            return at(base) + (at(base + 1) - at(base)) * smooth;
        }

        case WaveInterp::Cubic: {
            const float p0 = at(base - 1);
            const float p1 = at(base);
            const float p2 = at(base + 1);
            const float p3 = at(base + 2);
            const float t = fraction;
            const float t2 = t * t;
            const float t3 = t2 * t;
            return 0.5f * ((2.0f * p1) +
                           (-p0 + p2) * t +
                           (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                           (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        }
    }
    return at(base);
}

// ============================================================================
// The tools
// ============================================================================

/*
 * Stretch or squeeze the wave horizontally.
 *
 * A factor of 2 puts two cycles of what was there into the table, which is
 * the same thing as doubling the frequency. A factor of 0.5 takes the first
 * half and spreads it across the whole table.
 *
 * Reads through a copy rather than in place: resampling a buffer while
 * overwriting it feeds the second half of the operation the first half's
 * results, and the wave dissolves into a smear that looks plausible enough
 * not to be noticed.
 */
inline void waveScaleX(float* table, int size, float factor, WaveInterp mode) {
    if (table == nullptr || size <= 1) return;
    if (!std::isfinite(factor) || factor <= 0.0f) return;

    float source[1024];
    if (size > 1024) return;
    for (int i = 0; i < size; ++i) source[i] = table[i];

    for (int i = 0; i < size; ++i) {
        table[i] = waveSampleAt(source, size, static_cast<float>(i) * factor, mode);
    }
}

// Scale the amplitude. Clamped, because a wavetable outside -1..1 is one the
// oscillator clips on every cycle.
inline void waveScaleY(float* table, int size, float factor) {
    if (table == nullptr || size <= 0 || !std::isfinite(factor)) return;
    for (int i = 0; i < size; ++i) {
        table[i] = std::clamp(table[i] * factor, -1.0f, 1.0f);
    }
}

/*
 * Rotate the wave, wrapping.
 *
 * Which sample of a looping wave is "first" is arbitrary, so this changes
 * nothing about the sound of a single table on its own. It changes
 * everything about morphing between two, and about how a wave lines up with
 * a hard-synced one.
 */
inline void waveOffsetX(float* table, int size, int shift) {
    if (table == nullptr || size <= 1) return;

    float source[1024];
    if (size > 1024) return;
    for (int i = 0; i < size; ++i) source[i] = table[i];

    for (int i = 0; i < size; ++i) {
        int from = (i - shift) % size;
        if (from < 0) from += size;
        table[i] = source[from];
    }
}

inline void waveOffsetY(float* table, int size, float offset) {
    if (table == nullptr || size <= 0 || !std::isfinite(offset)) return;
    for (int i = 0; i < size; ++i) {
        table[i] = std::clamp(table[i] + offset, -1.0f, 1.0f);
    }
}

inline void waveInvert(float* table, int size) {
    if (table == nullptr) return;
    for (int i = 0; i < size; ++i) table[i] = -table[i];
}

inline void waveReverse(float* table, int size) {
    if (table == nullptr || size <= 1) return;
    for (int i = 0, j = size - 1; i < j; ++i, --j) std::swap(table[i], table[j]);
}

/*
 * A circular moving average.
 *
 * Circular for the same reason waveSampleAt wraps: the ends of a wavetable
 * are neighbours. Smoothing that treated them as edges would leave a corner
 * exactly where the loop joins, which is the one place a corner is audible
 * on every single cycle.
 */
inline void waveSmooth(float* table, int size, int passes = 1) {
    if (table == nullptr || size <= 2) return;

    float scratch[1024];
    if (size > 1024) return;

    for (int pass = 0; pass < std::clamp(passes, 1, 32); ++pass) {
        for (int i = 0; i < size; ++i) {
            const float previous = table[(i - 1 + size) % size];
            const float next = table[(i + 1) % size];
            scratch[i] = (previous + table[i] * 2.0f + next) * 0.25f;
        }
        for (int i = 0; i < size; ++i) table[i] = scratch[i];
    }
}

/*
 * Remove the DC offset.
 *
 * A hand-drawn wave almost never averages to zero, and one that does not is
 * a thump on every note-on plus a constant pull on anything downstream with
 * memory - a filter, a compressor, the master limiter. It is inaudible as a
 * tone and very audible as everything else misbehaving, which is exactly the
 * kind of fault that is worth one button.
 */
inline void waveRemoveDC(float* table, int size) {
    if (table == nullptr || size <= 0) return;

    double sum = 0.0;
    for (int i = 0; i < size; ++i) sum += table[i];
    const float mean = static_cast<float>(sum / size);
    if (std::fabs(mean) < 1e-9f) return;

    for (int i = 0; i < size; ++i) {
        table[i] = std::clamp(table[i] - mean, -1.0f, 1.0f);
    }
}

/*
 * Round every sample to one of N evenly spaced levels.
 *
 * This is a chip constraint made real rather than an effect that sounds a
 * bit like one. A Game Boy's wave channel stores 4 bits per sample, which is
 * 16 levels, and a wave rounded to 16 levels is one that hardware could
 * actually hold. The steps are placed so that both extremes are reachable
 * and so that zero is a level when the count is odd - a quantiser that
 * cannot represent silence adds a DC offset to every wave it touches.
 */
inline void waveQuantiseLevels(float* table, int size, int levels) {
    if (table == nullptr || size <= 0) return;
    levels = std::clamp(levels, 2, 256);

    const float steps = static_cast<float>(levels - 1);
    for (int i = 0; i < size; ++i) {
        const float unit = (std::clamp(table[i], -1.0f, 1.0f) + 1.0f) * 0.5f;
        const float snapped = std::round(unit * steps) / steps;
        table[i] = snapped * 2.0f - 1.0f;
    }
}

/*
 * Reduce the wave to N steps, each held.
 *
 * The other half of the chip constraint: hardware wave channels are short.
 * The Game Boy's is 32 samples long, the PC Engine's is 32, the Namco 163's
 * varies. Holding each step rather than interpolating between them is the
 * point - the stair edges are where the harmonics that make it sound like
 * hardware come from.
 */
inline void waveReduceSteps(float* table, int size, int steps) {
    if (table == nullptr || size <= 1) return;
    steps = std::clamp(steps, 2, size);

    float source[1024];
    if (size > 1024) return;
    for (int i = 0; i < size; ++i) source[i] = table[i];

    for (int i = 0; i < size; ++i) {
        const int step = i * steps / size;
        // The value of the step is taken from its own start rather than
        // averaged across it: hardware holds the sample it was given, and an
        // average would round off exactly the edges this exists to keep.
        const int from = step * size / steps;
        table[i] = source[std::clamp(from, 0, size - 1)];
    }
}

// Both at once, which is what "make this a Game Boy wave" means.
inline void waveFitToChip(float* table, int size, int steps, int levels) {
    waveReduceSteps(table, size, steps);
    waveQuantiseLevels(table, size, levels);
}

/*
 * Scale so the loudest sample reaches full scale.
 *
 * A table that is already silent is left alone rather than amplified by a
 * division by nearly zero, which would turn rounding noise into a full-scale
 * square wave.
 */
inline void waveNormalise(float* table, int size) {
    if (table == nullptr || size <= 0) return;

    float peak = 0.0f;
    for (int i = 0; i < size; ++i) peak = std::max(peak, std::fabs(table[i]));
    if (peak < 1e-6f) return;

    const float gain = 1.0f / peak;
    for (int i = 0; i < size; ++i) {
        table[i] = std::clamp(table[i] * gain, -1.0f, 1.0f);
    }
}

// ============================================================================
// Shapes - building a wave out of parts instead of drawing it
// ============================================================================
/*
 * Additive synthesis, with the components you would actually reach for.
 *
 * Not a harmonic-amplitude editor with 64 sliders: those are precise and
 * nobody can hear what a change to bin 37 will do. This is a small stack of
 * whole waveforms at chosen harmonics, which is how the sounds people want
 * are actually described - "a sine with a bit of the third harmonic as a
 * square" is a sentence somebody can mean.
 *
 * The exponent is the part that earns its place. Raising a component to a
 * power while keeping its sign bends it toward its extremes or toward zero,
 * which turns a sine into something between a sine and a square without any
 * of the aliasing that clipping one would produce.
 */
enum class WaveShapeKind : int { Sine = 0, Triangle, Saw, Pulse, Noise };
inline constexpr int WAVE_SHAPE_KIND_COUNT = 5;

inline const char* waveShapeKindName(int kind) {
    switch (kind) {
        case 0: return "Sine";
        case 1: return "Triangle";
        case 2: return "Saw";
        case 3: return "Pulse";
        case 4: return "Noise";
        default: return "Sine";
    }
}

struct WaveShapeComponent {
    int   kind = static_cast<int>(WaveShapeKind::Sine);
    float amplitude = 1.0f;    // -1..1; negative inverts the component
    int   harmonic = 1;        // 1..32, how many cycles it fits in the table
    float phase = 0.0f;        // 0..1 turns
    float duty = 0.5f;         // Pulse only
    float exponent = 1.0f;     // 0.2..5, shaping toward the extremes or zero
};

inline constexpr int MAX_WAVE_SHAPES = 6;

struct WaveShapeSpec {
    WaveShapeComponent components[MAX_WAVE_SHAPES];
    int count = 1;
    bool normalise = true;
    // The chip constraints, applied last so the shape is built cleanly and
    // then made to fit rather than built inside a grid.
    int steps = 0;             // 0 = the table's own length
    int levels = 0;            // 0 = full float resolution
};

namespace detail {

// A deterministic value per (harmonic, index). Noise in a wavetable has to
// be the SAME noise every cycle or it is not a wavetable, it is a sample -
// so this is a hash rather than a generator.
inline float shapeNoise(int harmonic, int index) {
    uint32_t h = static_cast<uint32_t>(index) * 2654435761u;
    h ^= static_cast<uint32_t>(harmonic) * 0x9E3779B9u;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return static_cast<float>(h & 0xFFFFu) / 32767.5f - 1.0f;
}

inline float shapeValue(const WaveShapeComponent& component, float turns) {
    // turns is 0..1 through the component's own cycle.
    float phase = turns - std::floor(turns);

    switch (static_cast<WaveShapeKind>(component.kind)) {
        case WaveShapeKind::Sine:
            return std::sin(phase * 6.28318530718f);

        case WaveShapeKind::Triangle:
            return (phase < 0.5f) ? (phase * 4.0f - 1.0f)
                                  : (3.0f - phase * 4.0f);

        case WaveShapeKind::Saw:
            return phase * 2.0f - 1.0f;

        case WaveShapeKind::Pulse: {
            const float duty = std::clamp(component.duty, 0.01f, 0.99f);
            return (phase < duty) ? 1.0f : -1.0f;
        }

        case WaveShapeKind::Noise:
            break;
    }
    return 0.0f;
}

}  // namespace detail

/*
 * Render a spec into a table.
 *
 * Sums the components, then optionally normalises, then applies the chip
 * constraints. That order matters: normalising after quantising would move
 * every sample off the levels it was just snapped to, and the result would
 * be a wave that claims to be 4-bit and is not.
 */
inline void renderWaveShapes(const WaveShapeSpec& spec, float* table, int size) {
    if (table == nullptr || size <= 0) return;

    for (int i = 0; i < size; ++i) table[i] = 0.0f;

    const int count = std::clamp(spec.count, 0, MAX_WAVE_SHAPES);
    for (int c = 0; c < count; ++c) {
        const WaveShapeComponent& component = spec.components[c];

        const float amplitude = std::clamp(component.amplitude, -1.0f, 1.0f);
        if (std::fabs(amplitude) < 1e-6f) continue;

        const int harmonic = std::clamp(component.harmonic, 1, 32);
        const float exponent = std::clamp(component.exponent, 0.2f, 5.0f);
        const float phase = component.phase;

        for (int i = 0; i < size; ++i) {
            const float turns =
                static_cast<float>(i) / static_cast<float>(size) *
                    static_cast<float>(harmonic) + phase;

            float value;
            if (static_cast<WaveShapeKind>(component.kind) == WaveShapeKind::Noise) {
                // Noise ignores phase. A higher harmonic repeats the same
                // short burst more times inside the table rather than
                // producing different noise, which is what makes it a
                // wavetable and not a sample: the cycle has to be identical
                // every time round or the note is not a pitch.
                value = detail::shapeNoise(harmonic, (i * harmonic) % size);
            } else {
                value = detail::shapeValue(component, turns);
            }

            // Shaping keeps the sign and bends the magnitude, so an exponent
            // never turns a symmetric wave into a lopsided one.
            if (exponent != 1.0f) {
                const float magnitude = std::pow(std::fabs(value), exponent);
                value = (value < 0.0f) ? -magnitude : magnitude;
            }

            table[i] += value * amplitude;
        }
    }

    if (spec.normalise) waveNormalise(table, size);

    if (spec.steps > 0 && spec.steps < size) waveReduceSteps(table, size, spec.steps);
    if (spec.levels > 1) waveQuantiseLevels(table, size, spec.levels);

    for (int i = 0; i < size; ++i) {
        if (!std::isfinite(table[i])) table[i] = 0.0f;
        table[i] = std::clamp(table[i], -1.0f, 1.0f);
    }
}

}  // namespace ChiptuneTracker
