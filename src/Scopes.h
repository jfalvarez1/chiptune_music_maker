#pragma once

// ============================================================================
// Per-channel oscilloscopes
//
// A level meter tells you a channel is doing something. It cannot tell you
// WHAT. On a chip channel that difference is most of the work: a duty change
// and a volume change look identical on a meter, a wave with a DC offset
// looks perfectly healthy, a triangle that is being band-limited into a sine
// at high pitches reads as a normal note, and two channels beating against
// each other look like two channels.
//
// Every one of those is obvious the instant you can see the waveform, which
// is why the trackers people actually use for chip work all show one per
// channel. It is a debugging tool that happens to look good.
//
// AUDIO THREAD RULES. Writing is one store into a ring plus one relaxed
// increment - no allocation, no locks, no branches worth naming. The reader
// is the UI thread and may catch a sample mid-write; for a picture refreshed
// sixty times a second that is not worth a lock, and it is stated here
// rather than discovered.
//
// TRIGGERING. Drawing the most recent N samples makes a stable note crawl
// across the display, because the window almost never lands on the same
// point in the cycle twice. A real oscilloscope solves this by starting the
// sweep at a rising edge through zero, and so does this: the picture holds
// still, which is the difference between a scope you can read and a
// decoration.
// ============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace ChiptuneTracker {

/*
 * One channel's ring.
 *
 * 4096 samples is 93 ms at 44.1 kHz. Half of that is the longest window a
 * read will ask for, which is two full cycles of a 22 Hz note - below
 * anything a chip channel plays as a pitch - and the other half is the slack
 * the trigger search needs. 2048 was the first choice and it clamped a
 * two-octaves-below-middle-C bass to one and a third cycles, which is
 * exactly the note whose shape is hardest to read.
 *
 * At 32 channels this is half a megabyte, which buys a picture of every
 * channel at once.
 */
class ScopeBuffer {
public:
    static constexpr int SIZE = 4096;

    // Audio thread. One store and one increment.
    void write(float sample) {
        const uint32_t at = m_write.load(std::memory_order_relaxed);
        m_samples[at & (SIZE - 1)] = sample;
        m_write.store(at + 1, std::memory_order_release);
    }

    void clear() {
        m_samples.fill(0.0f);
        m_write.store(0, std::memory_order_release);
    }

    /*
     * Copy out a triggered window. UI thread.
     *
     * `out` receives `count` samples ending at the newest one, with the
     * start moved back to the most recent rising zero crossing so the
     * picture does not crawl. Returns false when the channel is silent,
     * which the caller draws differently rather than drawing a flat line
     * that looks like a bug.
     */
    bool read(float* out, int count, float silenceFloor = 1e-4f) const {
        if (out == nullptr || count <= 0 || count > SIZE) return false;

        const uint32_t head = m_write.load(std::memory_order_acquire);
        if (head < static_cast<uint32_t>(count)) return false;

        auto at = [&](uint32_t index) {
            return m_samples[index & (SIZE - 1)];
        };

        // Loud enough to be worth drawing?
        float peak = 0.0f;
        for (int i = 0; i < count; ++i) {
            peak = std::max(peak, std::fabs(at(head - static_cast<uint32_t>(count) + static_cast<uint32_t>(i))));
        }
        if (peak < silenceFloor) return false;

        /*
         * Find the trigger.
         *
         * Searching backwards from the newest sample for a rising crossing,
         * but no further than the slack between the ring and the window -
         * a search that ran the whole ring could pick a crossing so old that
         * the window it starts is partly overwritten by the time it is read.
         */
        const int slack = SIZE - count - 1;
        uint32_t start = head - static_cast<uint32_t>(count);
        for (int back = 0; back < slack; ++back) {
            const uint32_t candidate = head - static_cast<uint32_t>(count) - static_cast<uint32_t>(back);
            const float previous = at(candidate - 1);
            const float current = at(candidate);
            if (previous <= 0.0f && current > 0.0f) { start = candidate; break; }
        }

        for (int i = 0; i < count; ++i) {
            out[i] = at(start + static_cast<uint32_t>(i));
        }
        return true;
    }

    // Whether anything has ever been written, so a scope on a channel that
    // has never sounded says "silent" rather than drawing zeros.
    bool started() const { return m_write.load(std::memory_order_acquire) > 0; }

private:
    static_assert((SIZE & (SIZE - 1)) == 0,
                  "the ring masks rather than divides, so the size must be a "
                  "power of two");

    std::array<float, SIZE> m_samples{};
    std::atomic<uint32_t> m_write{0};
};

/*
 * How much of a channel's own picture is worth showing.
 *
 * Fewer samples than the ring holds: one or two cycles of the note being
 * played is what makes the shape readable, and eight cycles is a blur. This
 * picks a window from the pitch so a bass note and a lead both show roughly
 * the same number of cycles.
 */
inline int scopeWindowForPitch(float frequencyHz, float sampleRate,
                               int cycles = 2) {
    if (!(frequencyHz > 1.0f) || !(sampleRate > 1.0f)) return 512;
    const float period = sampleRate / frequencyHz;
    const int wanted = static_cast<int>(period * static_cast<float>(cycles));
    return std::clamp(wanted, 64, ScopeBuffer::SIZE / 2);
}

}  // namespace ChiptuneTracker
