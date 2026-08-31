#pragma once

/*
 * ChiptuneTracker - Reverb algorithms
 *
 * The tracker had one reverb: eight parallel combs into four allpasses, the
 * Schroeder-Moorer arrangement every plugin started life as. It is a
 * perfectly good general room and it is the only thing on offer, which means
 * every reverb in every project is the same reverb.
 *
 * These are the other five, plus the original kept exactly as it was.
 *
 * ONE SLOT, NOT SIX. They are algorithms on the existing Reverb effect
 * rather than six new effects in the rack. Nobody puts six reverbs on one
 * channel; a channel has *a* reverb and you choose what kind. It also
 * matters for memory - a tank is around 100 KB of delay lines, and six of
 * them on each of 32 channels plus four buses would be twenty megabytes of
 * mostly-silent buffers.
 *
 * ROOM IS UNCHANGED, BIT FOR BIT. It is the default, every existing project
 * uses it, and there is a test asserting the rack still matches a frozen
 * copy of the pre-rack chain. A "better" room that sounded different would
 * quietly alter every project ever saved.
 *
 * The five new ones share one tank, configured differently, because that is
 * what they are: a plate and a hall differ in diffusion and decay shape, not
 * in kind. Hardware units did the same thing.
 *
 * Buffers are sized in prepare(), which runs on the UI thread. Nothing here
 * allocates while playing.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace ChiptuneTracker {

enum class ReverbAlgorithm : uint8_t {
    Room = 0,   // the original, unchanged
    Plate,
    Spring,
    Hall,
    Shimmer,
    Reverse,
    Gated,
    Count
};

inline const char* reverbAlgorithmName(ReverbAlgorithm algorithm) {
    switch (algorithm) {
        case ReverbAlgorithm::Plate:   return "Plate";
        case ReverbAlgorithm::Spring:  return "Spring";
        case ReverbAlgorithm::Hall:    return "Hall";
        case ReverbAlgorithm::Shimmer: return "Shimmer";
        case ReverbAlgorithm::Reverse: return "Reverse";
        case ReverbAlgorithm::Gated:   return "Gated";
        case ReverbAlgorithm::Room:
        default:                       return "Room";
    }
}

inline const char* reverbAlgorithmBlurb(ReverbAlgorithm algorithm) {
    switch (algorithm) {
        case ReverbAlgorithm::Plate:
            return "Dense and bright from the first millisecond. A sheet of "
                   "steel has no room modes, so there are no early "
                   "reflections - it is the classic vocal and snare reverb.";
        case ReverbAlgorithm::Spring:
            return "Dispersive: high frequencies arrive before low ones, "
                   "which is the boing. Guitar amps and dub.";
        case ReverbAlgorithm::Hall:
            return "A feedback delay network. Slow to build, long to decay, "
                   "and the most natural-sounding of these on sustained "
                   "material.";
        case ReverbAlgorithm::Shimmer:
            return "An octave-up pitch shift inside the feedback path, so "
                   "the tail rises as it decays. Ambient and post-rock.";
        case ReverbAlgorithm::Reverse:
            return "The tail arrives before the note. Swells into a hit.";
        case ReverbAlgorithm::Gated:
            return "A dense burst cut off abruptly rather than allowed to "
                   "decay. The eighties snare, and a synthwave staple.";
        case ReverbAlgorithm::Room:
        default:
            return "The original general-purpose room. Unchanged, and the "
                   "default, so existing projects sound as they always did.";
    }
}

// ============================================================================
// Primitives
// ============================================================================
namespace verb {

// A delay line with a fractional read, sized once and never reallocated.
class Line {
public:
    void prepare(int maxSamples) {
        m_buffer.assign(static_cast<size_t>(std::max(1, maxSamples)), 0.0f);
        m_write = 0;
    }

    void clear() { std::fill(m_buffer.begin(), m_buffer.end(), 0.0f); m_write = 0; }
    int size() const { return static_cast<int>(m_buffer.size()); }

    void write(float value) {
        if (m_buffer.empty()) return;
        m_buffer[static_cast<size_t>(m_write)] = value;
        m_write = (m_write + 1) % static_cast<int>(m_buffer.size());
    }

    float readAt(int delay) const {
        if (m_buffer.empty()) return 0.0f;
        const int size = static_cast<int>(m_buffer.size());
        int index = m_write - 1 - std::clamp(delay, 0, size - 1);
        while (index < 0) index += size;
        return m_buffer[static_cast<size_t>(index)];
    }

    // Fractional, for the pitch shifter in the shimmer feedback.
    float readAt(float delay) const {
        if (m_buffer.empty()) return 0.0f;
        const int whole = static_cast<int>(delay);
        const float fraction = delay - static_cast<float>(whole);
        const float a = readAt(whole);
        const float b = readAt(whole + 1);
        return a + (b - a) * fraction;
    }

private:
    std::vector<float> m_buffer;
    int m_write = 0;
};

/*
 * A Schroeder allpass: flat magnitude, and it smears phase.
 *
 * This is the whole basis of diffusion. It scatters an impulse into a dense
 * cloud without colouring it, which is what turns a handful of echoes into
 * something that sounds like a room rather than a flutter.
 */
class Allpass {
public:
    void prepare(int delaySamples) {
        m_delay = std::max(1, delaySamples);
        m_line.prepare(m_delay + 2);
    }
    void clear() { m_line.clear(); }

    float process(float input, float gain) {
        const float delayed = m_line.readAt(m_delay - 1);
        const float v = input + delayed * gain;
        m_line.write(v);
        return delayed - v * gain;
    }

private:
    Line m_line;
    int m_delay = 1;
};

// One-pole lowpass, for the damping in a feedback path. Air and soft
// furnishings absorb treble faster than bass; a tank without this decays
// into a bright ringing that no room does.
class Damper {
public:
    void clear() { m_state = 0.0f; }
    float process(float input, float amount) {
        const float a = std::clamp(amount, 0.0f, 0.99f);
        m_state = input * (1.0f - a) + m_state * a;
        return m_state;
    }
private:
    float m_state = 0.0f;
};

} // namespace verb

// ============================================================================
// The tank
// ============================================================================
/*
 * One structure, configured six ways.
 *
 * Input diffusion (four allpasses) feeds a four-line feedback delay network
 * with a Householder matrix. Householder because it is orthogonal - so the
 * feedback neither grows nor shrinks on its own, and the decay is set purely
 * by the gain - and because it costs one sum and four subtractions rather
 * than the sixteen multiplies a general matrix would.
 *
 * What the algorithms actually change: the delay lengths, how much input
 * diffusion runs, where the damping sits, and what happens in the feedback
 * path.
 */
class AdvancedReverb {
public:
    static constexpr int LINES = 4;
    static constexpr int DIFFUSERS = 4;

    // ---- Parameters -------------------------------------------------------
    float size = 0.7f;        // 0..1
    float damping = 0.4f;     // 0..1
    float predelay = 0.02f;   // seconds
    float width = 1.0f;

    // Shimmer: how far up the feedback is transposed, in semitones, and how
    // much of the shifted signal is fed back.
    float shimmerSemitones = 12.0f;
    float shimmerAmount = 0.5f;

    // Gated: how long the burst is held before it is cut.
    float gateHold = 0.12f;      // seconds
    float gateThreshold = 0.02f;

    // Reverse: how far ahead the swell is built.
    float reverseSeconds = 0.6f;

    // Spring: how strongly high frequencies run ahead of low ones.
    float dispersion = 0.6f;

    void prepare(float sampleRate, ReverbAlgorithm algorithm) {
        m_sampleRate = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
        m_algorithm = algorithm;

        const float ratio = m_sampleRate / 44100.0f;
        auto scaled = [ratio](int samples) {
            return std::max(1, static_cast<int>(static_cast<float>(samples) * ratio));
        };

        /*
         * Mutually prime lengths, so the lines do not reinforce each other at
         * a common period. Shared factors are what produce a metallic ring
         * on the tail instead of a decay.
         */
        static const int PLATE[LINES]   = {1753, 2143, 2521, 2953};
        static const int HALL[LINES]    = {3169, 4013, 4721, 5477};
        static const int SPRING[LINES]  = {  97,  131,  173,  211};

        const int* lengths = PLATE;
        switch (algorithm) {
            case ReverbAlgorithm::Hall:
            case ReverbAlgorithm::Shimmer:
            case ReverbAlgorithm::Reverse: lengths = HALL; break;
            case ReverbAlgorithm::Spring:  lengths = SPRING; break;
            default:                       lengths = PLATE; break;
        }

        for (int i = 0; i < LINES; ++i) {
            m_lineLength[i] = scaled(lengths[i]);
            m_line[i].prepare(m_lineLength[i] + 4);
            m_damper[i].clear();
        }

        // The input diffusers. A plate wants a lot of them - it is dense
        // from the first millisecond because a steel sheet has no room modes
        // and therefore no early reflections to hear through.
        static const int DIFFUSE[DIFFUSERS] = {142, 107, 379, 277};
        for (int i = 0; i < DIFFUSERS; ++i) {
            m_diffuser[i].prepare(scaled(DIFFUSE[i]));
        }

        // A spring's dispersion is a long chain of short allpasses: each one
        // delays low frequencies slightly more than high, and stacked they
        // turn an impulse into the descending chirp everyone recognises.
        for (int i = 0; i < DISPERSERS; ++i) {
            m_disperser[i].prepare(scaled(37 + i * 11));
        }

        m_predelayLine.prepare(std::max(2, scaled(4410)));

        // The reverse buffer is only paid for when it is used - two seconds
        // of audio is not something to allocate on every channel for an
        // algorithm nobody selected.
        if (algorithm == ReverbAlgorithm::Reverse) {
            m_reverseLine.prepare(static_cast<int>(2.0f * m_sampleRate));
        } else {
            m_reverseLine.prepare(1);
        }

        clear();
    }

    void clear() {
        for (int i = 0; i < LINES; ++i) { m_line[i].clear(); m_damper[i].clear(); }
        for (int i = 0; i < DIFFUSERS; ++i) m_diffuser[i].clear();
        for (int i = 0; i < DISPERSERS; ++i) m_disperser[i].clear();
        m_predelayLine.clear();
        m_reverseLine.clear();
        m_shimmerPhase = 0.0f;
        m_gateEnvelope = 0.0f;
        m_gateHeld = 0.0f;
        m_reverseCursor = 0.0f;
    }

    ReverbAlgorithm algorithm() const { return m_algorithm; }

    /*
     * One sample in, a stereo pair out. Audio thread: no allocation, no
     * locks, and every index bounded by construction.
     */
    std::pair<float, float> processStereo(float input) {
        if (!std::isfinite(input)) input = 0.0f;

        // ---- Pre-delay ----------------------------------------------------
        const int predelaySamples = std::clamp(
            static_cast<int>(predelay * m_sampleRate), 0,
            m_predelayLine.size() - 1);
        const float delayed = m_predelayLine.readAt(predelaySamples);
        m_predelayLine.write(input);

        float source = delayed;

        // ---- Reverse: build the swell before it is heard -------------------
        if (m_algorithm == ReverbAlgorithm::Reverse) {
            source = reverseRead(input);
        }

        // ---- Diffusion -----------------------------------------------------
        float diffused = source;
        if (m_algorithm == ReverbAlgorithm::Spring) {
            // A spring has almost no diffusion and a great deal of
            // dispersion - which is exactly why it sounds like a spring and
            // not like a room.
            const float amount = 0.4f + 0.5f * std::clamp(dispersion, 0.0f, 1.0f);
            for (int i = 0; i < DISPERSERS; ++i) {
                diffused = m_disperser[i].process(diffused, amount);
            }
        } else {
            const int count = (m_algorithm == ReverbAlgorithm::Plate ||
                               m_algorithm == ReverbAlgorithm::Gated)
                ? DIFFUSERS : 2;
            for (int i = 0; i < count; ++i) {
                diffused = m_diffuser[i].process(diffused, 0.625f);
            }
        }

        // ---- The tank -------------------------------------------------------
        std::array<float, LINES> tapped{};
        for (int i = 0; i < LINES; ++i) {
            tapped[static_cast<size_t>(i)] = m_line[i].readAt(m_lineLength[i] - 1);
        }

        /*
         * Householder feedback: y_i = x_i - (2/N) * sum(x).
         *
         * Orthogonal, so it neither adds nor removes energy - the decay is
         * set purely by the gain below, which is what makes the decay time
         * predictable instead of a function of the matrix.
         */
        float sum = 0.0f;
        for (float v : tapped) sum += v;
        const float correction = sum * (2.0f / static_cast<float>(LINES));

        const float decay = decayGain();

        for (int i = 0; i < LINES; ++i) {
            const size_t index = static_cast<size_t>(i);
            float value = tapped[index] - correction;

            value = m_damper[i].process(value, std::clamp(damping, 0.0f, 0.95f));
            value *= decay;

            if (m_algorithm == ReverbAlgorithm::Shimmer && i < 2) {
                value = mixShimmer(value, i);
            }

            m_line[i].write(diffused + value);
        }

        // ---- Out ------------------------------------------------------------
        //
        // Opposite line pairs to each side, so the two channels are
        // decorrelated rather than being the same signal twice.
        float left = tapped[0] + tapped[2] * 0.7f;
        float right = tapped[1] + tapped[3] * 0.7f;

        const float mid = (left + right) * 0.5f;
        const float spread = std::clamp(width, 0.0f, 1.0f);
        left = mid + (left - mid) * spread;
        right = mid + (right - mid) * spread;

        if (m_algorithm == ReverbAlgorithm::Gated) {
            const float gate = gateGain(input);
            left *= gate;
            right *= gate;
        }

        // A tank can ring up transiently on a loud transient; the tail is
        // bounded here rather than allowed to reach the rest of the chain.
        left = std::clamp(left * 0.3f, -4.0f, 4.0f);
        right = std::clamp(right * 0.3f, -4.0f, 4.0f);

        if (!std::isfinite(left)) left = 0.0f;
        if (!std::isfinite(right)) right = 0.0f;
        return {left, right};
    }

private:
    static constexpr int DISPERSERS = 8;

    float decayGain() const {
        const float clamped = std::clamp(size, 0.0f, 1.0f);
        switch (m_algorithm) {
            // A gated reverb's tank is cut by the gate, so it can be dense
            // and short without also having to be quiet.
            case ReverbAlgorithm::Gated:   return 0.55f + 0.25f * clamped;
            case ReverbAlgorithm::Spring:  return 0.6f + 0.32f * clamped;
            case ReverbAlgorithm::Hall:
            case ReverbAlgorithm::Shimmer: return 0.7f + 0.28f * clamped;
            default:                       return 0.65f + 0.3f * clamped;
        }
    }

    /*
     * Shimmer: an octave up inside the feedback.
     *
     * Not a phase vocoder - one in a feedback loop is both very expensive
     * and unstable, because its latency is inside the loop. This is the
     * cheap classic: read the line at a moving rate to transpose, with two
     * readers half a cycle apart and crossfaded so the wrap is not a click.
     */
    float mixShimmer(float value, int lineIndex) {
        const float ratio = std::pow(2.0f, std::clamp(shimmerSemitones, 0.0f, 24.0f) / 12.0f);
        const float span = static_cast<float>(m_lineLength[lineIndex] - 2);

        m_shimmerPhase += (ratio - 1.0f);
        if (m_shimmerPhase >= span) m_shimmerPhase -= span;
        if (m_shimmerPhase < 0.0f) m_shimmerPhase += span;

        const float second = std::fmod(m_shimmerPhase + span * 0.5f, span);

        const float a = m_line[lineIndex].readAt(m_shimmerPhase);
        const float b = m_line[lineIndex].readAt(second);

        // Equal-power crossfade across the wrap, so the seam is inaudible.
        const float t = m_shimmerPhase / span;
        const float fade = 0.5f - 0.5f * std::cos(6.28318530718f * t);
        const float shifted = a * (1.0f - fade) + b * fade;

        const float amount = std::clamp(shimmerAmount, 0.0f, 0.9f);
        return value * (1.0f - amount) + shifted * amount;
    }

    /*
     * Gate: hold, then cut.
     *
     * The eighties snare is not a short reverb - it is a long, dense one
     * switched off part way through, which is why the tail stops dead rather
     * than fading. The release is deliberately abrupt for the same reason.
     */
    float gateGain(float input) {
        const float level = std::fabs(input);
        if (level > gateThreshold) {
            m_gateHeld = std::max(0.001f, gateHold);
            m_gateEnvelope = 1.0f;
        } else if (m_gateHeld > 0.0f) {
            m_gateHeld -= 1.0f / m_sampleRate;
        } else {
            // ~4 ms to silence. Any slower and it is a fade, which is the
            // thing a gate exists not to be.
            m_gateEnvelope -= 250.0f / m_sampleRate;
            if (m_gateEnvelope < 0.0f) m_gateEnvelope = 0.0f;
        }
        return m_gateEnvelope;
    }

    /*
     * Reverse: read the recent past backwards.
     *
     * A window of input is written forwards and read backwards, so the
     * energy of a hit arrives before the hit does - the swell. The cursor
     * wraps at the window length, and the window is crossfaded at the wrap
     * because otherwise every cycle is a click.
     */
    float reverseRead(float input) {
        m_reverseLine.write(input);

        const float window = std::clamp(reverseSeconds, 0.05f, 1.9f) * m_sampleRate;
        const float span = std::min(window,
                                    static_cast<float>(m_reverseLine.size() - 2));
        if (span < 2.0f) return input;

        m_reverseCursor += 1.0f;
        if (m_reverseCursor >= span) m_reverseCursor -= span;

        // Reading at (span - cursor) walks backwards through the window.
        const float readPoint = span - m_reverseCursor;
        const float value = m_reverseLine.readAt(readPoint);

        const float t = m_reverseCursor / span;
        const float fade = 0.5f - 0.5f * std::cos(6.28318530718f * t);
        return value * fade;
    }

    float m_sampleRate = 44100.0f;
    ReverbAlgorithm m_algorithm = ReverbAlgorithm::Plate;

    std::array<verb::Line, LINES> m_line;
    std::array<int, LINES> m_lineLength{};
    std::array<verb::Damper, LINES> m_damper;
    std::array<verb::Allpass, DIFFUSERS> m_diffuser;
    std::array<verb::Allpass, DISPERSERS> m_disperser;

    verb::Line m_predelayLine;
    verb::Line m_reverseLine;

    float m_shimmerPhase = 0.0f;
    float m_gateEnvelope = 0.0f;
    float m_gateHeld = 0.0f;
    float m_reverseCursor = 0.0f;
};

} // namespace ChiptuneTracker
