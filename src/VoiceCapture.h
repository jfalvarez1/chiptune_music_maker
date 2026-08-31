#pragma once

/*
 * ChiptuneTracker - Microphone capture into a lock-free ring
 *
 * The existing AudioRecorder took a std::mutex and did a vector insert -
 * which allocates - inside its capture callback. Both are forbidden there
 * for the same reason: the callback runs on a thread the OS will not wait
 * for, and a lock it cannot take or an allocator it has to enter is a
 * dropout. It is the same bug that was fixed in 3.3, when the spectrum
 * analyzer was running a whole FFT inside the callback.
 *
 * So the callback here does one thing: copy floats into a fixed ring and
 * update two atomics. Every piece of analysis - onset detection, YIN pitch,
 * drum classification - happens on the UI thread, reading what the ring has.
 *
 * The ring is bulk rather than item-at-a-time. A per-sample push would do
 * two atomic operations per sample, 48000 times a second, to move data that
 * arrives in blocks anyway.
 *
 * Overflow drops the OLDEST audio, not the newest. A live monitor that falls
 * behind should show what is being sung now; holding onto a stale backlog
 * and discarding the present is the wrong way round.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// Bulk single-producer / single-consumer float ring
// ============================================================================
template <size_t Capacity>
class AudioRing {
public:
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two so the wrap is a mask "
                  "rather than a modulo - the write side runs in the audio "
                  "callback and an integer division there is not free.");

    static constexpr size_t CAPACITY = Capacity;

    // Producer: the audio callback. Never blocks, never allocates.
    void write(const float* data, size_t count) {
        const size_t writeIndex = m_write.load(std::memory_order_relaxed);
        const size_t readIndex = m_read.load(std::memory_order_acquire);
        const size_t used = writeIndex - readIndex;
        const size_t free = (Capacity - 1) - used;

        // More audio than the ring can hold at all: keep the tail of it,
        // since the newest audio is the audio the user just made.
        if (count >= Capacity) {
            data += count - (Capacity - 1);
            count = Capacity - 1;
        }

        if (count > free) {
            // Drop the oldest by advancing the read cursor. The consumer may
            // be reading concurrently, which can only make it re-read or skip
            // a block it was about to discard anyway.
            const size_t drop = count - free;
            m_read.store(readIndex + drop, std::memory_order_release);
            m_dropped.fetch_add(drop, std::memory_order_relaxed);
        }

        const size_t start = writeIndex & (Capacity - 1);
        const size_t first = std::min(count, Capacity - start);
        std::copy(data, data + first, m_buffer.data() + start);
        if (count > first) {
            std::copy(data + first, data + count, m_buffer.data());
        }
        m_write.store(writeIndex + count, std::memory_order_release);
    }

    // Consumer: the UI thread. Returns how many samples it actually took.
    size_t read(float* out, size_t count) {
        const size_t readIndex = m_read.load(std::memory_order_relaxed);
        const size_t writeIndex = m_write.load(std::memory_order_acquire);
        const size_t available = writeIndex - readIndex;
        const size_t take = std::min(count, available);
        if (take == 0) return 0;

        const size_t start = readIndex & (Capacity - 1);
        const size_t first = std::min(take, Capacity - start);
        std::copy(m_buffer.data() + start, m_buffer.data() + start + first, out);
        if (take > first) {
            std::copy(m_buffer.data(), m_buffer.data() + (take - first), out + first);
        }
        m_read.store(readIndex + take, std::memory_order_release);
        return take;
    }

    size_t available() const {
        return m_write.load(std::memory_order_acquire) -
               m_read.load(std::memory_order_relaxed);
    }

    void clear() {
        m_read.store(m_write.load(std::memory_order_acquire),
                     std::memory_order_release);
    }

    // How much audio the ring has thrown away because nothing drained it.
    // Surfaced rather than silent: a monitor that has been quietly dropping
    // audio for a minute looks exactly like one that is merely inaccurate.
    uint64_t droppedSamples() const {
        return m_dropped.load(std::memory_order_relaxed);
    }
    void resetDropped() { m_dropped.store(0, std::memory_order_relaxed); }

private:
    // Free-running counters, masked on use. They do not wrap in any
    // plausible session: at 48 kHz a 64-bit counter lasts twelve million
    // years, and using the raw difference means "empty" and "full" are
    // distinguishable without wasting a slot on a sentinel.
    std::array<float, Capacity> m_buffer{};
    std::atomic<size_t> m_write{0};
    std::atomic<size_t> m_read{0};
    std::atomic<uint64_t> m_dropped{0};
};

// One second at 48 kHz, rounded up to a power of two. The analysis window is
// a few tens of milliseconds, so this is a very large margin - it exists so
// that a UI thread stalled by a file dialog does not lose the take.
using VoiceRing = AudioRing<65536>;

// ============================================================================
// Level metering, computed where it is cheap
// ============================================================================
/*
 * The peak of a block, which the callback can compute in one pass while the
 * data is already in cache. This is the one piece of analysis that belongs
 * on the audio side: it is O(n) with no allocation and no branching, and the
 * alternative is that the meter cannot move until the UI drains the ring.
 */
inline float blockPeak(const float* data, size_t count) {
    float peak = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float magnitude = std::fabs(data[i]);
        if (magnitude > peak) peak = magnitude;
    }
    return peak;
}

// A meter that falls slowly and rises instantly, so a transient is visible
// for longer than the one frame it occupied.
class PeakMeter {
public:
    void push(float peak, float releasePerFrame = 0.06f) {
        const float current = m_value.load(std::memory_order_relaxed);
        const float decayed = std::max(0.0f, current - releasePerFrame);
        m_value.store(std::max(decayed, peak), std::memory_order_relaxed);
    }
    float value() const { return m_value.load(std::memory_order_relaxed); }
    void reset() { m_value.store(0.0f, std::memory_order_relaxed); }

private:
    std::atomic<float> m_value{0.0f};
};

} // namespace ChiptuneTracker
