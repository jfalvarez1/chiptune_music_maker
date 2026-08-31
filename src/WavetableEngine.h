#pragma once

/*
 * ChiptuneTracker - Wavetable synthesis engine
 *
 * The tracker has had Wavetable, WavetableBank and a whole wavetable editor
 * for some time. What it has never had is anything that plays them: the
 * "Custom" oscillator ran `generateTriangle()`. You could draw a waveform,
 * watch the editor's preview redraw, save it in the project, and hear a
 * triangle. This is the engine that was missing.
 *
 * The entire difficulty is aliasing.
 *
 * A wavetable is a wavetable because it has harmonics. Reading a 256-point
 * table by stepping a phase accumulator reproduces every one of them,
 * including the ones above Nyquist, and those fold back down as inharmonic
 * tones that follow the note in the wrong direction - play a chromatic scale
 * and hear a second line descending through it. A drawn square is the worst
 * case: its harmonics fall off as 1/n and there are hundreds of them.
 *
 * The rest of the oscillators solve this with PolyBLEP, which works because
 * their discontinuities are known analytically. A drawn table's are not, so
 * this uses the other standard answer: mipmaps. Each table is pre-filtered
 * into a series of copies, each band-limited to a successively lower
 * harmonic, and playback reads the copy whose highest harmonic still fits
 * under Nyquist at the pitch being played. It is the same idea as texture
 * mipmapping and for exactly the same reason.
 *
 * Building a mip level means an FFT, a truncation and an inverse FFT, so it
 * happens once when the bank changes - on the UI thread - and never in the
 * audio callback. The audio thread only ever reads.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Types.h"
#include "FFT.h"

namespace ChiptuneTracker {

// ============================================================================
// One table, band-limited at several octaves
// ============================================================================
/*
 * Seven levels covers the whole keyboard, and seven is exactly the right
 * number rather than a guess. Level 0 keeps every harmonic a 256-point
 * table has - 127 of them - and each level after keeps half as many:
 * 127, 63, 31, 15, 7, 3, 1. Level 6 is already a single harmonic, which is
 * a sine, and a sine is the only thing that can be played at the very top
 * of the range without aliasing at all. Any further level would be a byte
 * for byte copy of level 6.
 *
 * The count matters more than it looks. Every level is 257 floats per
 * table, sixteen tables per bank, several banks, double-buffered - so three
 * redundant levels were 1.7 MB of duplicated sines, and the first thing
 * they did was overflow the stack of a test that declared a library as a
 * local.
 */
struct WavetableMips {
    static constexpr int LEVELS = 7;
    static constexpr int SIZE = Wavetable::TABLE_SIZE;

    // One extra sample per level, holding a copy of samples[0]. It removes
    // the wrap test from the interpolation in the audio thread, which is a
    // branch per sample per voice.
    std::array<std::array<float, SIZE + 1>, LEVELS> levels{};

    // How many harmonics level k retains. Level 0 is the unfiltered table.
    static int harmonicsAtLevel(int level) {
        const int top = SIZE / 2 - 1;               // 127 for a 256-point table
        const int kept = top >> std::clamp(level, 0, LEVELS - 1);
        return std::max(1, kept);
    }

    /*
     * Which level to play at a given phase increment.
     *
     * phaseIncrement is cycles per sample, so the highest harmonic that fits
     * under Nyquist is 0.5 / phaseIncrement. Pick the lowest-numbered level
     * whose harmonic count is under that, since a lower level is brighter.
     */
    static float levelFor(float phaseIncrement) {
        const float step = std::fabs(phaseIncrement);
        if (step <= 1e-9f) return 0.0f;

        const float maxHarmonic = 0.5f / step;
        if (maxHarmonic >= float(SIZE / 2 - 1)) return 0.0f;
        if (maxHarmonic <= 1.0f) return float(LEVELS - 1);

        // Level k keeps 127 >> k harmonics, so the level that just fits is
        // log2(127 / maxHarmonic). Kept fractional so playback can crossfade
        // between two levels rather than stepping, which would be an audible
        // click as a note glides across a boundary.
        const float exact = std::log2(float(SIZE / 2 - 1) / maxHarmonic);
        return std::clamp(exact, 0.0f, float(LEVELS - 1));
    }

    // Linear interpolation within one level.
    float sampleLevel(int level, float phase) const {
        const int clamped = std::clamp(level, 0, LEVELS - 1);
        const float position = phase * float(SIZE);
        int index = int(position);
        if (index < 0) index = 0;
        if (index >= SIZE) index = SIZE - 1;
        const float fraction = position - float(index);

        const auto& table = levels[size_t(clamped)];
        return table[size_t(index)] * (1.0f - fraction) +
               table[size_t(index) + 1] * fraction;
    }

    // Crossfaded across levels, so a glide does not click at a boundary.
    float sample(float phase, float level) const {
        const int lower = int(level);
        const float blend = level - float(lower);
        if (blend <= 1e-4f) return sampleLevel(lower, phase);
        return sampleLevel(lower, phase) * (1.0f - blend) +
               sampleLevel(lower + 1, phase) * blend;
    }
};

// ============================================================================
// A whole bank, ready to play
// ============================================================================
class WavetableSet {
public:
    static constexpr int MAX_TABLES = WavetableBank::MAX_TABLES;

    int count() const { return m_count; }
    bool empty() const { return m_count == 0; }

    /*
     * Band-limit every table in the bank. UI thread only: this runs
     * LEVELS FFTs per table and allocates while it does.
     */
    void build(const WavetableBank& bank) {
        m_count = std::min(static_cast<int>(bank.tables.size()), MAX_TABLES);

        const size_t size = WavetableMips::SIZE;
        std::vector<DSP::Complex> spectrum(size);
        std::vector<DSP::Complex> work(size);

        for (int t = 0; t < m_count; ++t) {
            const Wavetable& source = bank.tables[static_cast<size_t>(t)];
            WavetableMips& mips = m_tables[static_cast<size_t>(t)];

            for (size_t i = 0; i < size; ++i) {
                spectrum[i] = DSP::Complex(source.samples[i], 0.0f);
            }
            DSP::fft(spectrum);

            for (int level = 0; level < WavetableMips::LEVELS; ++level) {
                const int keep = WavetableMips::harmonicsAtLevel(level);

                work = spectrum;
                // Zero every harmonic above the cutoff, and its mirror.
                // Both halves, because a real signal's spectrum is
                // conjugate-symmetric and zeroing only one produces a
                // complex result whose real part is a half-amplitude
                // Hilbert-ish mess rather than the filtered wave.
                for (size_t bin = static_cast<size_t>(keep) + 1; bin < size; ++bin) {
                    if (bin >= size - static_cast<size_t>(keep)) continue;
                    work[bin] = DSP::Complex(0.0f, 0.0f);
                }
                DSP::ifft(work);

                auto& table = mips.levels[static_cast<size_t>(level)];
                for (size_t i = 0; i < size; ++i) {
                    table[i] = work[i].real();
                }
                table[size] = table[0];    // the wrap guard
            }
        }

        // A bank with no tables at all would be silence, which reads as the
        // engine being broken rather than as an empty bank. One sine is a
        // better answer to "you have not drawn anything yet".
        if (m_count == 0) {
            Wavetable sine;
            sine.initSine();
            WavetableBank fallback;
            fallback.tables.clear();
            fallback.tables.push_back(sine);
            build(fallback);
        }
    }

    /*
     * One sample, morphing across the bank and band-limited for the pitch.
     *
     * Audio thread. No allocation, no locks, no branches beyond the two
     * blends - and no wrap test, because every level carries a guard sample.
     */
    float sample(float phase, float morph, float phaseIncrement) const {
        if (m_count <= 0) return 0.0f;

        // Phase can arrive slightly out of range from detune and vibrato.
        phase -= std::floor(phase);

        const float level = WavetableMips::levelFor(phaseIncrement);

        if (m_count == 1) {
            return m_tables[0].sample(phase, level);
        }

        const float position = std::clamp(morph, 0.0f, 1.0f) * float(m_count - 1);
        const int lower = std::clamp(int(position), 0, m_count - 1);
        const int upper = std::clamp(lower + 1, 0, m_count - 1);
        const float blend = position - float(lower);

        const float a = m_tables[static_cast<size_t>(lower)].sample(phase, level);
        if (upper == lower || blend <= 1e-4f) return a;
        const float b = m_tables[static_cast<size_t>(upper)].sample(phase, level);
        return a * (1.0f - blend) + b * blend;
    }

    const WavetableMips& table(int index) const {
        return m_tables[static_cast<size_t>(std::clamp(index, 0, MAX_TABLES - 1))];
    }

private:
    std::array<WavetableMips, MAX_TABLES> m_tables{};
    int m_count = 0;
};

// ============================================================================
// Every bank in the project, published to the audio thread
// ============================================================================
/*
 * Double-buffered with a release store, exactly like the effect rack's slot
 * order. Rebuilding in place while the audio thread reads would let a voice
 * read half of the old table and half of the new one - which is not a subtle
 * artefact, it is a click on every sample until the write finishes.
 */
class WavetableLibrary {
public:
    // Four banks, not eight. Each mipmapped bank is around 115 KB and this
    // is double-buffered, so the count is paid for twice over; a project
    // with more than four distinct wavetable banks in play at once is not a
    // case worth a megabyte.
    static constexpr int MAX_BANKS = 4;

    // UI thread.
    void rebuild(const std::vector<WavetableBank>& banks) {
        const int inactive = 1 - m_active.load(std::memory_order_relaxed);
        auto& target = m_sets[static_cast<size_t>(inactive)];

        const int count = std::min(static_cast<int>(banks.size()), MAX_BANKS);
        for (int i = 0; i < count; ++i) {
            target[static_cast<size_t>(i)].build(banks[static_cast<size_t>(i)]);
        }
        // Any bank the project no longer has becomes a plain sine rather
        // than whatever it used to be, so a channel still pointing at a
        // deleted bank makes a defensible sound instead of a stale one.
        for (int i = count; i < MAX_BANKS; ++i) {
            target[static_cast<size_t>(i)].build(WavetableBank{});
        }
        m_count.store(std::max(count, 1), std::memory_order_relaxed);
        m_active.store(inactive, std::memory_order_release);
    }

    // Audio thread.
    const WavetableSet& bank(int index) const {
        const auto& sets = m_sets[static_cast<size_t>(
            m_active.load(std::memory_order_acquire))];
        return sets[static_cast<size_t>(std::clamp(index, 0, MAX_BANKS - 1))];
    }

    int bankCount() const { return m_count.load(std::memory_order_relaxed); }

private:
    std::array<std::array<WavetableSet, MAX_BANKS>, 2> m_sets{};
    std::atomic<int> m_active{0};
    std::atomic<int> m_count{0};
};

} // namespace ChiptuneTracker
