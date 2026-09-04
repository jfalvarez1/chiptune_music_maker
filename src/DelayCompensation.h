#pragma once

// ============================================================================
// Delay compensation
//
// Some effects cannot produce their output at the instant they receive their
// input. A convolution reverb has to fill a block before it can transform
// one; a phase vocoder needs a whole analysis window before it knows what
// the pitch is. Both are correct, and both mean the channel they sit on
// comes out LATE.
//
// Late by how much matters. A phase vocoder's window is 1024 samples, which
// at 44.1 kHz is 23 milliseconds - about the gap that makes two drums sound
// like a flam rather than one hit. So putting a pitch shifter on the lead
// and nothing on the drums does not merely process the lead: it pulls the
// lead out of time with the rest of the song, by an amount nothing in the
// interface mentions.
//
// The fix is the one every professional DAW performs and calls delay
// compensation: find the channel that is latest, and delay every other
// channel to match it. The song is then late as a whole, by a fixed and
// inaudible amount, and internally in time - which is what matters, because
// nobody can hear when a song starts but everybody can hear a snare that
// arrives after the kick it was played with.
//
// This was previously reported and not acted on. PluginChain::latencySamples
// existed, the effects knew their own latency, and nothing summed or applied
// any of it.
//
// WHAT THIS DOES NOT COVER, stated plainly rather than left to be
// discovered. Compensation is applied per channel, after that channel's
// effects and plugins. A latency-bearing effect on a BUS is not compensated:
// every channel feeding that bus is late together, so they stay in time with
// each other, but they are late against channels routed elsewhere. Buses
// carry the same effects chain, so this is a real gap and not a theoretical
// one - it is simply the next piece of work rather than this one.
//
// AUDIO THREAD RULES. The delay line is a fixed-capacity buffer allocated
// once. process() does no allocation, takes no locks, and touches no
// filesystem. Changing the delay is a UI-thread operation.
// ============================================================================

#include <algorithm>
#include <array>
#include <vector>

namespace ChiptuneTracker {

/*
 * A delay line of fixed capacity: hold this signal back by N samples.
 *
 * Fixed capacity rather than a resizing buffer, because this is read from
 * the audio callback and an allocation there is a dropout. A delay asked for
 * beyond the capacity is clamped rather than allowed to index out of bounds.
 *
 * Two sizes are in use, aliased below: a large one per channel, and a small
 * one inside individual effects.
 */
template <int CAPACITY>
class FixedDelayLine {
public:
    static constexpr int MAX_SAMPLES = CAPACITY;

    /*
     * Set the delay. UI thread.
     *
     * The buffer is cleared when the amount changes, which costs a click at
     * the moment an effect is switched on. The alternative - reusing
     * whatever happened to be in the line - plays a fragment of older audio
     * at a new offset, which is a worse noise and is much harder to explain.
     */
    void setDelay(int samples) {
        const int clamped = std::clamp(samples, 0, MAX_SAMPLES - 1);
        if (clamped == m_delay) return;
        m_delay = clamped;
        clear();
    }

    int delay() const { return m_delay; }

    void clear() {
        m_buffer.fill(0.0f);
        m_cursor = 0;
    }

    /*
     * One sample in, one sample out, delayed. Audio thread.
     *
     * A zero delay returns the input directly rather than running it
     * through a one-element ring, so a project with no latency anywhere
     * pays nothing at all for this existing.
     */
    float process(float input) {
        if (m_delay <= 0) return input;

        const float out = m_buffer[static_cast<size_t>(m_cursor)];
        m_buffer[static_cast<size_t>(m_cursor)] = input;

        ++m_cursor;
        if (m_cursor >= m_delay) m_cursor = 0;

        return out;
    }

private:
    std::array<float, static_cast<size_t>(CAPACITY)> m_buffer{};
    int m_delay = 0;
    int m_cursor = 0;
};

/*
 * The channel line, 8192 samples: 186 ms at 44.1 kHz, far past any plausible
 * chain. One per channel and per bus.
 */
using CompensationDelay = FixedDelayLine<8192>;

/*
 * The line an individual effect uses to hold its own dry path back.
 *
 * 2048 is comfortably past the largest latency anything here reports - a
 * phase vocoder's 1024-sample window - and small enough that four of them
 * inside every effects chain is under a megabyte across the whole mixer.
 */
using DryPathDelay = FixedDelayLine<2048>;

/*
 * Work out what each channel's compensating delay should be.
 *
 * Given every channel's own latency, the answer for each is "however much
 * less than the worst it is". The channel that is latest gets none, and
 * everything else waits for it.
 *
 * A free function taking and returning plain arrays so it can be tested
 * without a Sequencer, a Project or an audio device - the arithmetic is
 * the part worth pinning, and it is the part that is easy to get subtly
 * backwards.
 */
inline void computeCompensation(const std::vector<int>& latencies,
                                std::vector<int>& delaysOut) {
    delaysOut.assign(latencies.size(), 0);
    if (latencies.empty()) return;

    int worst = 0;
    for (int latency : latencies) {
        worst = std::max(worst, std::max(0, latency));
    }

    for (size_t i = 0; i < latencies.size(); ++i) {
        const int own = std::max(0, latencies[i]);
        delaysOut[i] = std::clamp(worst - own, 0,
                                  CompensationDelay::MAX_SAMPLES - 1);
    }
}

} // namespace ChiptuneTracker
