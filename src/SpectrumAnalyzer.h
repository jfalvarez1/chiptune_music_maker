#pragma once

/*
 * ChiptuneTracker - Spectrum Analyzer
 *
 * Real-time FFT frequency display for mixing feedback.
 *
 * Two things this file is careful about, both learned the hard way:
 *
 * 1. THE AUDIO THREAD DOES ALMOST NOTHING. It writes one sample into a ring
 *    buffer and advances an atomic index. That is all. The windowing, the
 *    FFT and the dB conversion happen on the UI thread in update().
 *
 *    The previous version ran the whole FFT inside the audio callback -
 *    growing a std::vector, erasing from its front, allocating scratch
 *    buffers, and taking a mutex that the UI thread also held. Any one of
 *    those can stall the audio callback and produce a dropout; a mutex can
 *    stall it for as long as the UI holds the lock.
 *
 * 2. THE MAGNITUDES ARE NORMALISED. An unnormalised 2048-point FFT of a
 *    full-scale sine peaks around +54 dB, so mapping "-60..0 dB" over it
 *    clamped every bin to maximum and the display was a solid wall at every
 *    frequency. Scaling by the transform length and the window's coherent
 *    gain puts a full-scale sine at 0 dBFS, where it belongs.
 */

#include <array>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ChiptuneTracker {

class SpectrumAnalyzer {
public:
    static constexpr int FFT_SIZE = 2048;
    static constexpr int NUM_BINS = FFT_SIZE / 2;
    static constexpr int UPDATE_RATE = 24;      // spectrum refreshes per second

    // Bins below this are treated as silence, so an idle mix reads as empty
    // rather than as a floor of numerical noise.
    static constexpr float DB_FLOOR = -78.0f;

    SpectrumAnalyzer() {
        m_ring.fill(0.0f);
        m_magnitudes.assign(NUM_BINS, 0.0f);
        m_smoothed.assign(NUM_BINS, 0.0f);
        buildTables();
    }

    void setSampleRate(float sampleRate) {
        m_sampleRate = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
        m_samplesPerUpdate = std::max(1, static_cast<int>(m_sampleRate / UPDATE_RATE));
    }

    // ------------------------------------------------------------------
    // Audio thread. O(1), allocation-free, lock-free.
    // ------------------------------------------------------------------
    void process(float left, float right) {
        const float mono = 0.5f * (left + right);
        const uint32_t write = m_writeIndex.load(std::memory_order_relaxed);
        m_ring[write & RING_MASK] = std::isfinite(mono) ? mono : 0.0f;
        m_writeIndex.store(write + 1, std::memory_order_release);
    }

    // ------------------------------------------------------------------
    // UI thread. Does the real work, at the display's pace rather than the
    // audio thread's.
    // ------------------------------------------------------------------
    void update() {
        const uint32_t write = m_writeIndex.load(std::memory_order_acquire);
        if (write < static_cast<uint32_t>(FFT_SIZE)) return;         // not enough audio yet
        if (write - m_lastUpdateIndex < static_cast<uint32_t>(m_samplesPerUpdate)) return;
        m_lastUpdateIndex = write;

        // Copy the most recent FFT_SIZE samples out of the ring, windowing
        // as we go. The ring is four times the transform length, so the
        // audio thread cannot lap this copy: it would have to write 6144
        // samples - about 140ms - while we walk 2048.
        const uint32_t start = write - static_cast<uint32_t>(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; ++i) {
            m_real[i] = m_ring[(start + i) & RING_MASK] * m_window[i];
            m_imag[i] = 0.0f;
        }

        fftInPlace();

        for (int i = 0; i < NUM_BINS; ++i) {
            const float re = m_real[i];
            const float im = m_imag[i];
            float amplitude = std::sqrt(re * re + im * im) * m_normalization;

            // DC is not part of a mirrored pair, so it must not be doubled.
            if (i == 0) amplitude *= 0.5f;

            const float dB = 20.0f * std::log10(amplitude + 1e-9f);
            const float level = std::clamp((dB - DB_FLOOR) / -DB_FLOOR, 0.0f, 1.0f);

            m_magnitudes[i] = level;

            // Fast attack, slow release. A symmetric filter either flickers
            // or smears; transients should appear at once and fall away.
            float& smoothed = m_smoothed[i];
            smoothed = (level > smoothed) ? level
                                          : smoothed + (level - smoothed) * 0.35f;
        }
    }

    // The UI owns these, so no copy or lock is needed.
    const std::vector<float>& magnitudes() const { return m_smoothed; }

    // Kept for callers that want their own copy.
    std::vector<float> getAllMagnitudes() const { return m_smoothed; }

    float getMagnitude(int bin) const {
        if (bin < 0 || bin >= NUM_BINS) return 0.0f;
        return m_smoothed[static_cast<size_t>(bin)];
    }

    float binToFrequency(int bin) const {
        return static_cast<float>(bin) * m_sampleRate / static_cast<float>(FFT_SIZE);
    }

    int frequencyToBin(float frequency) const {
        return static_cast<int>(frequency * static_cast<float>(FFT_SIZE) / m_sampleRate);
    }

    float getPeakFrequency() const {
        const auto peak = std::max_element(m_smoothed.begin(), m_smoothed.end());
        if (peak == m_smoothed.end()) return 0.0f;
        return binToFrequency(static_cast<int>(std::distance(m_smoothed.begin(), peak)));
    }

    void reset() {
        m_ring.fill(0.0f);
        std::fill(m_magnitudes.begin(), m_magnitudes.end(), 0.0f);
        std::fill(m_smoothed.begin(), m_smoothed.end(), 0.0f);
        m_writeIndex.store(0, std::memory_order_release);
        m_lastUpdateIndex = 0;
    }

private:
    // Four transforms deep: enough headroom that the writer cannot catch the
    // reader mid-copy. Power of two so the index wrap is a mask.
    static constexpr int RING_SIZE = FFT_SIZE * 4;
    static constexpr uint32_t RING_MASK = RING_SIZE - 1;

    void buildTables() {
        constexpr float TWO_PI = 6.28318530717958647692f;

        // Hann window, and the scale that turns raw FFT output into an
        // amplitude. Single-sided spectrum, so 2/N, divided by the window's
        // coherent gain (0.5 for Hann) - which works out to 4/N.
        float coherentGain = 0.0f;
        for (int i = 0; i < FFT_SIZE; ++i) {
            m_window[i] = 0.5f * (1.0f - std::cos(TWO_PI * i / (FFT_SIZE - 1)));
            coherentGain += m_window[i];
        }
        coherentGain /= static_cast<float>(FFT_SIZE);
        m_normalization = 2.0f / (static_cast<float>(FFT_SIZE) * coherentGain);

        // Bit-reversal permutation, precomputed so the transform allocates
        // nothing and does no division.
        int bits = 0;
        while ((1 << bits) < FFT_SIZE) ++bits;
        for (int i = 0; i < FFT_SIZE; ++i) {
            int reversed = 0;
            for (int b = 0; b < bits; ++b) {
                if (i & (1 << b)) reversed |= 1 << (bits - 1 - b);
            }
            m_bitReverse[i] = reversed;
        }

        for (int i = 0; i < FFT_SIZE / 2; ++i) {
            const float angle = -TWO_PI * i / FFT_SIZE;
            m_twiddleReal[i] = std::cos(angle);
            m_twiddleImag[i] = std::sin(angle);
        }
    }

    // Iterative radix-2 Cooley-Tukey, in place over the preallocated
    // buffers. The recursive version elsewhere in the codebase allocates two
    // vectors per level, which is fine offline and not fine per frame.
    void fftInPlace() {
        for (int i = 0; i < FFT_SIZE; ++i) {
            const int j = m_bitReverse[i];
            if (j > i) {
                std::swap(m_real[i], m_real[j]);
                std::swap(m_imag[i], m_imag[j]);
            }
        }

        for (int len = 2; len <= FFT_SIZE; len <<= 1) {
            const int half = len >> 1;
            const int step = FFT_SIZE / len;
            for (int i = 0; i < FFT_SIZE; i += len) {
                for (int k = 0; k < half; ++k) {
                    const float wr = m_twiddleReal[k * step];
                    const float wi = m_twiddleImag[k * step];
                    const int a = i + k;
                    const int b = a + half;

                    const float tr = m_real[b] * wr - m_imag[b] * wi;
                    const float ti = m_real[b] * wi + m_imag[b] * wr;

                    m_real[b] = m_real[a] - tr;
                    m_imag[b] = m_imag[a] - ti;
                    m_real[a] += tr;
                    m_imag[a] += ti;
                }
            }
        }
    }

    float m_sampleRate = 44100.0f;
    int m_samplesPerUpdate = 1837;

    // Written by the audio thread, read by the UI thread.
    std::array<float, RING_SIZE> m_ring{};
    std::atomic<uint32_t> m_writeIndex{0};

    // UI thread only.
    uint32_t m_lastUpdateIndex = 0;
    std::array<float, FFT_SIZE> m_real{};
    std::array<float, FFT_SIZE> m_imag{};
    std::array<float, FFT_SIZE> m_window{};
    std::array<int, FFT_SIZE> m_bitReverse{};
    std::array<float, FFT_SIZE / 2> m_twiddleReal{};
    std::array<float, FFT_SIZE / 2> m_twiddleImag{};
    float m_normalization = 1.0f;

    std::vector<float> m_magnitudes;
    std::vector<float> m_smoothed;
};

} // namespace ChiptuneTracker
