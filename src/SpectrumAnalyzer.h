#pragma once

/*
 * ChiptuneTracker - Spectrum Analyzer
 *
 * Real-time FFT-based frequency spectrum visualization
 * for visual mixing feedback and frequency analysis.
 */

#include "FFT.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace ChiptuneTracker {

// ============================================================================
// SpectrumAnalyzer - Real-time frequency spectrum visualization
// ============================================================================
class SpectrumAnalyzer {
public:
    static constexpr int FFT_SIZE = 2048;        // FFT window size (power of 2)
    static constexpr int NUM_BINS = FFT_SIZE / 2; // Number of frequency bins (Nyquist)
    static constexpr int UPDATE_RATE = 24;        // Hz - update rate for visualization

    SpectrumAnalyzer() {
        m_audioBuffer.reserve(FFT_SIZE);
        m_magnitudes.resize(NUM_BINS, 0.0f);
        m_smoothedMagnitudes.resize(NUM_BINS, 0.0f);
    }

    void setSampleRate(float sr) {
        m_sampleRate = sr;
        m_samplesPerUpdate = (int)(sr / UPDATE_RATE);
    }

    // Process stereo audio samples (called from audio callback)
    void process(float left, float right) {
        // Mix to mono
        float mono = (left + right) * 0.5f;

        // Accumulate samples
        m_audioBuffer.push_back(mono);
        m_sampleCounter++;

        // When we have enough samples, compute FFT
        if (m_sampleCounter >= m_samplesPerUpdate && m_audioBuffer.size() >= FFT_SIZE) {
            computeSpectrum();
            m_sampleCounter = 0;

            // Keep most recent samples, discard old ones
            if (m_audioBuffer.size() > FFT_SIZE) {
                m_audioBuffer.erase(m_audioBuffer.begin(),
                                   m_audioBuffer.begin() + (m_audioBuffer.size() - FFT_SIZE));
            }
        }

        // Prevent buffer from growing indefinitely
        if (m_audioBuffer.size() > FFT_SIZE * 2) {
            m_audioBuffer.erase(m_audioBuffer.begin(),
                               m_audioBuffer.begin() + FFT_SIZE);
        }
    }

    // Get magnitude for a specific frequency bin
    float getMagnitude(int binIndex) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (binIndex >= 0 && binIndex < NUM_BINS) {
            return m_smoothedMagnitudes[binIndex];
        }
        return 0.0f;
    }

    // Get all magnitudes (for UI rendering)
    std::vector<float> getAllMagnitudes() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_smoothedMagnitudes;
    }

    // Convert bin index to frequency (Hz)
    float binToFrequency(int binIndex) const {
        return (float)binIndex * m_sampleRate / (float)FFT_SIZE;
    }

    // Convert frequency to bin index
    int frequencyToBin(float frequency) const {
        return (int)(frequency * FFT_SIZE / m_sampleRate);
    }

    // Reset analyzer
    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_audioBuffer.clear();
        std::fill(m_magnitudes.begin(), m_magnitudes.end(), 0.0f);
        std::fill(m_smoothedMagnitudes.begin(), m_smoothedMagnitudes.end(), 0.0f);
        m_sampleCounter = 0;
    }

    // Get peak frequency
    float getPeakFrequency() const {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto maxIt = std::max_element(m_smoothedMagnitudes.begin(), m_smoothedMagnitudes.end());
        if (maxIt != m_smoothedMagnitudes.end()) {
            int binIndex = (int)std::distance(m_smoothedMagnitudes.begin(), maxIt);
            return binToFrequency(binIndex);
        }
        return 0.0f;
    }

private:
    void computeSpectrum() {
        // Get most recent FFT_SIZE samples
        std::vector<float> samples(FFT_SIZE);
        int startIdx = std::max(0, (int)m_audioBuffer.size() - FFT_SIZE);
        for (int i = 0; i < FFT_SIZE && (startIdx + i) < (int)m_audioBuffer.size(); i++) {
            // Apply Hann window to reduce spectral leakage
            float hannWindow = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (FFT_SIZE - 1)));
            samples[i] = m_audioBuffer[startIdx + i] * hannWindow;
        }

        // Compute magnitude spectrum
        std::vector<float> rawMagnitudes = DSP::computeMagnitudeSpectrum(samples);

        // Apply smoothing for less flickery visualization
        std::lock_guard<std::mutex> lock(m_mutex);
        const float smoothingFactor = 0.7f; // Higher = more smoothing
        for (int i = 0; i < NUM_BINS && i < (int)rawMagnitudes.size(); i++) {
            // Convert to dB scale
            float magnitude = rawMagnitudes[i];
            float dB = 20.0f * std::log10(magnitude + 1e-6f); // Add small value to avoid log(0)

            // Normalize to 0.0 - 1.0 range (assuming -60 dB to 0 dB range)
            dB = std::clamp((dB + 60.0f) / 60.0f, 0.0f, 1.0f);

            // Exponential smoothing
            m_smoothedMagnitudes[i] = smoothingFactor * m_smoothedMagnitudes[i] +
                                     (1.0f - smoothingFactor) * dB;
        }
    }

    float m_sampleRate = 48000.0f;
    int m_samplesPerUpdate = 2000;
    int m_sampleCounter = 0;

    std::vector<float> m_audioBuffer;
    std::vector<float> m_magnitudes;
    std::vector<float> m_smoothedMagnitudes;

    mutable std::mutex m_mutex; // Thread safety for UI access
};

} // namespace ChiptuneTracker
