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
// BUSES TOO. A send is a copy of a channel into a bus, processed there and
// returned to the mix - and if that bus carries a vocoder, the returned copy
// is a window later than the original it is being blended with. Two copies
// of the same sound a few milliseconds apart is a comb filter, which is the
// audible failure people describe as "the parallel bus sounds hollow". So
// the graph is levelled as a whole: every path from a channel to the master,
// direct or through any depth of bus, is made the same length.
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

// ============================================================================
// The routing graph
// ============================================================================
/*
 * What a bus looks like from here.
 *
 * `order` is the topological walk the mixer already computes - a bus always
 * appears after everything that feeds it - which is what makes this a single
 * forward pass rather than a graph traversal.
 *
 * `channelLatency` is what the channels have already been levelled to by
 * computeCompensation above: every channel's signal is that many samples old
 * by the time it reaches a send.
 */
struct BusGraph {
    std::vector<int> latency;          // each bus's own effects-chain latency
    std::vector<int> output;           // -1 for master, otherwise a bus index
    std::vector<char> receivesSend;    // does any channel send into it
    std::vector<int> order;            // topological, sources first
    int channelLatency = 0;
};

/*
 * Where the delays go.
 *
 * Three places, and it has to be three. A bus input cannot simply be delayed
 * as a whole, because the signals arriving there are not all equally late:
 * a channel send arrives at the channel latency, while another bus feeding
 * the same input arrives at whatever that bus cost. Delaying the input
 * delays both, and the one that was already correct becomes wrong.
 *
 * So the channel sends into a bus are delayed together (they are all equally
 * old), each bus's OUTPUT is delayed to match wherever it is going, and the
 * channels' direct path to the master is delayed to match the buses coming
 * back. That is the general rule, and it happens to reduce to nothing at all
 * when no bus has any latency, which is nearly every project.
 */
struct BusCompensation {
    std::vector<int> sendInput;   // on the channel-send sum entering each bus
    std::vector<int> busOutput;   // on each bus's output, before it goes on
    int direct = 0;               // on every channel's direct path to master
    int total = 0;                // how late the whole mix ends up
};

inline void computeBusCompensation(const BusGraph& graph, BusCompensation& out) {
    const size_t count = graph.latency.size();
    out.sendInput.assign(count, 0);
    out.busOutput.assign(count, 0);
    out.direct = 0;
    out.total = std::max(0, graph.channelLatency);

    if (count == 0) return;

    const int channelLatency = std::max(0, graph.channelLatency);

    // How late a signal is when it ARRIVES at each bus, and when it LEAVES.
    std::vector<int> arriving(count, 0);
    std::vector<int> leaving(count, 0);
    std::vector<char> used(count, 0);

    for (size_t i = 0; i < count; ++i) {
        if (i < graph.receivesSend.size() && graph.receivesSend[i] != 0) {
            arriving[i] = channelLatency;
            used[i] = 1;
        }
    }

    /*
     * One forward pass in topological order.
     *
     * A bus's arrival time is final by the time we reach it, because
     * everything that feeds it came earlier in the order. That is the whole
     * reason the mixer computes that order, and it is why this needs no
     * recursion and no visited set.
     */
    int masterArrival = channelLatency;   // the channels' own direct path

    for (size_t slot = 0; slot < graph.order.size(); ++slot) {
        const int bus = graph.order[slot];
        if (bus < 0 || static_cast<size_t>(bus) >= count) continue;
        const size_t b = static_cast<size_t>(bus);

        leaving[b] = arriving[b] + std::max(0, graph.latency[b]);
        if (used[b] == 0) continue;   // nothing reaches it, so it costs nothing

        const int destination =
            (b < graph.output.size()) ? graph.output[b] : -1;

        if (destination >= 0 && static_cast<size_t>(destination) < count &&
            static_cast<size_t>(destination) != b) {
            const size_t d = static_cast<size_t>(destination);
            arriving[d] = std::max(arriving[d], leaving[b]);
            used[d] = 1;
        } else {
            masterArrival = std::max(masterArrival, leaving[b]);
        }
    }

    out.total = masterArrival;
    out.direct = masterArrival - channelLatency;

    for (size_t b = 0; b < count; ++b) {
        if (used[b] == 0) continue;

        // The channel sends wait for whatever else is arriving here.
        out.sendInput[b] = arriving[b] - channelLatency;

        const int destination =
            (b < graph.output.size()) ? graph.output[b] : -1;
        const int target =
            (destination >= 0 && static_cast<size_t>(destination) < count &&
             static_cast<size_t>(destination) != b)
                ? arriving[static_cast<size_t>(destination)]
                : masterArrival;

        out.busOutput[b] = target - leaving[b];
    }

    // Nothing here can legitimately be negative - every delay is a target
    // minus something that reached it earlier - but a malformed graph must
    // clamp rather than index off the end of a delay line.
    const int ceiling = CompensationDelay::MAX_SAMPLES - 1;
    out.direct = std::clamp(out.direct, 0, ceiling);
    for (size_t b = 0; b < count; ++b) {
        out.sendInput[b] = std::clamp(out.sendInput[b], 0, ceiling);
        out.busOutput[b] = std::clamp(out.busOutput[b], 0, ceiling);
    }
}

} // namespace ChiptuneTracker
