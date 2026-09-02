#define NOMINMAX
#include "AudioAnalyzer.h"

#include <array>
#include <numeric>
#include <deque>

namespace {
// Convert magnitude around a peak into a refined bin estimate.
float parabolicInterpolate(const std::vector<float>& buffer, int index) {
    if (index <= 0 || index >= static_cast<int>(buffer.size()) - 1) {
        return static_cast<float>(index);
    }
    const float alpha = buffer[index - 1];
    const float beta = buffer[index];
    const float gamma = buffer[index + 1];
    const float divisor = (alpha - 2.0f * beta + gamma);
    if (std::abs(divisor) < 1e-12f) {
        return static_cast<float>(index);
    }
    return static_cast<float>(index) + 0.5f * (alpha - gamma) / divisor;
}

float computeRMS(const std::vector<float>& window) {
    const float sum = std::inner_product(window.begin(), window.end(), window.begin(), 0.0f);
    return std::sqrt(sum / static_cast<float>(window.size()));
}

float dominantFreqFFT(const std::vector<float>& window, int sampleRate) {
    auto spectrum = DSP::computeMagnitudeSpectrum(window);
    if (spectrum.empty()) return -1.0f;
    int bestBin = 1;
    float bestMag = spectrum[1];
    for (int i = 2; i < static_cast<int>(spectrum.size()) - 1; ++i) {
        if (spectrum[i] > bestMag) {
            bestMag = spectrum[i];
            bestBin = i;
        }
    }
    const float refined = parabolicInterpolate(spectrum, bestBin);
    return refined * static_cast<float>(sampleRate) / static_cast<float>(spectrum.size() * 2); // spectrum is half-size
}

int medianFromDeque(const std::deque<int>& d) {
    if (d.empty()) return -1;
    std::vector<int> tmp(d.begin(), d.end());
    std::sort(tmp.begin(), tmp.end());
    return tmp[tmp.size() / 2];
}
} // namespace

static std::vector<DrumDebugInfo> g_lastDrumDebug;

const std::vector<DrumDebugInfo>& AudioAnalyzer::getLastDrumDebug() {
    return g_lastDrumDebug;
}

std::vector<DetectedNote> AudioAnalyzer::analyzeMelody(const std::vector<float>& samples, int sampleRate, float rmsThreshold, bool bassMode) {
    std::vector<DetectedNote> notes;
    if (samples.empty() || sampleRate <= 0) return notes;

    std::deque<int> pitchWindow; // small median smoother for pitch
    float envRms = 0.0f; // running envelope for adaptive thresholds

    if (bassMode) {
        const int windowSize = 2048;
        const int hopSize = 256;
        const float onThreshold = rmsThreshold;
        const float offThreshold = rmsThreshold * 0.8f;
        const float minNoteDuration = 0.12f;
        const int releaseFrames = 4;

        int currentNote = -1;
        float noteStartTime = 0.0f;
        float currentVelocity = 0.0f;
        int releaseCounter = 0;
        float lastRms = 0.0f;

        for (size_t i = 0; i + windowSize < samples.size(); i += hopSize) {
            const float time = static_cast<float>(i) / static_cast<float>(sampleRate);
            std::vector<float> window(samples.begin() + i, samples.begin() + i + windowSize);
            const float rms = computeRMS(window);
            envRms = envRms * 0.98f + rms * 0.02f;

            if (currentNote == -1) {
                if (rms > onThreshold * 1.05f && rms > lastRms * 1.4f && rms > envRms * 1.15f) {
                    // Estimate pitch once at onset; fallback to kick if invalid
                    int midiNote = -1;
                    float freq = getPitchYIN(window, sampleRate);
                    if (freq <= 0.0f) freq = dominantFreqFFT(window, sampleRate);
                    if (freq > 0.0f) midiNote = freqToMidi(freq);
                    if (midiNote == -1) midiNote = 36;

                    currentNote = midiNote;
                    noteStartTime = time;
                    currentVelocity = std::min(1.0f, rms * 5.0f);
                    releaseCounter = 0;
                }
            } else {
                // Retrigger on strong new attack while active
                if (rms > lastRms * 1.4f && rms > onThreshold * 1.2f && rms > envRms * 1.2f && (time - noteStartTime) > 0.1f) {
                    DetectedNote dn;
                    dn.noteNumber = currentNote;
                    dn.startTime = noteStartTime;
                    dn.duration = time - noteStartTime;
                    dn.velocity = currentVelocity;
                    dn.isDrum = false;
                    dn.drumType = -1;
                    if (dn.duration >= minNoteDuration) notes.push_back(dn);

                    int midiNote = -1;
                    float freq = getPitchYIN(window, sampleRate);
                    if (freq <= 0.0f) freq = dominantFreqFFT(window, sampleRate);
                    if (freq > 0.0f) midiNote = freqToMidi(freq);
                    if (midiNote == -1) midiNote = 36;

                    currentNote = midiNote;
                    noteStartTime = time;
                    currentVelocity = std::min(1.0f, rms * 5.0f);
                    releaseCounter = 0;
                    lastRms = rms;
                    continue;
                }

                if (rms < offThreshold) {
                    ++releaseCounter;
                } else {
                    releaseCounter = 0;
                }

                if (releaseCounter >= releaseFrames) {
                    DetectedNote dn;
                    dn.noteNumber = currentNote;
                    dn.startTime = noteStartTime;
                    dn.duration = time - noteStartTime;
                    dn.velocity = currentVelocity;
                    dn.isDrum = false;
                    dn.drumType = -1;
                    if (dn.duration >= minNoteDuration) notes.push_back(dn);
                    currentNote = -1;
                    releaseCounter = 0;
                }
            }

            lastRms = rms;
        }

        // Close trailing note
        if (currentNote != -1) {
            DetectedNote dn;
            dn.noteNumber = currentNote;
            dn.startTime = noteStartTime;
            dn.duration = static_cast<float>(samples.size()) / static_cast<float>(sampleRate) - noteStartTime;
            dn.velocity = currentVelocity;
            dn.isDrum = false;
            dn.drumType = -1;
            if (dn.duration >= minNoteDuration) notes.push_back(dn);
        }

        return notes;
    }

    const int windowSize = 2048;
    const int hopSize = 256;
    const float retriggerThresholdFactor = 1.45f;
    const float minNoteDuration = 0.12f;
    const int releaseFrames = 3;

    int currentNote = -1;
    float noteStartTime = 0.0f;
    float currentVelocity = 0.0f;
    float lastRms = 0.0f;
    int releaseCounter = 0;
    int lastStablePitch = -1;

    for (size_t i = 0; i + windowSize < samples.size(); i += hopSize) {
        const float time = static_cast<float>(i) / static_cast<float>(sampleRate);
        std::vector<float> window(samples.begin() + i, samples.begin() + i + windowSize);

        const float rms = computeRMS(window);
        envRms = envRms * 0.985f + rms * 0.015f;
        const bool belowThreshold = rms < rmsThreshold || rms < envRms * 0.65f;

        // Retrigger when a fresh attack exceeds previous envelope (used to catch new "BOOM" hits)
        bool retrigger = false;
        if (currentNote != -1) {
            if (bassMode) {
                if (rms > lastRms * retriggerThresholdFactor && rms > rmsThreshold * 1.3f && (time - noteStartTime) > 0.2f && rms > envRms * 1.1f) {
                    retrigger = true;
                }
            } else if (rms > lastRms * retriggerThresholdFactor && rms > rmsThreshold * 1.2f && rms > envRms * 1.1f) {
                retrigger = true;
            }
        }
        lastRms = rms;

        // Manage release to avoid chopping a sustained bass hit into fragments
        if (belowThreshold || rms < envRms * 0.7f) {
            ++releaseCounter;
        } else {
            releaseCounter = 0;
        }

        // Calculate pitch only when above threshold
        int midiNote = -1;
        if (!belowThreshold) {
            float yinFreq = getPitchYIN(window, sampleRate);
            float fftFreq = dominantFreqFFT(window, sampleRate);
            float frequency = yinFreq > 0.0f ? yinFreq : fftFreq;

            // If both exist and disagree badly, prefer the one that is closer to last stable
            if (yinFreq > 0.0f && fftFreq > 0.0f && std::abs(freqToMidi(yinFreq) - freqToMidi(fftFreq)) > 3) {
                if (lastStablePitch != -1) {
                    int yinMidi = freqToMidi(yinFreq);
                    int fftMidi = freqToMidi(fftFreq);
                    frequency = (std::abs(yinMidi - lastStablePitch) < std::abs(fftMidi - lastStablePitch)) ? yinFreq : fftFreq;
                } else {
                    // take the stronger RMS-related: keep yin by default
                    frequency = yinFreq;
                }
            }

            if (frequency > 0.0f) {
                midiNote = freqToMidi(frequency);
                if (midiNote != -1) {
                    pitchWindow.push_back(midiNote);
                    if (pitchWindow.size() > 9) pitchWindow.pop_front();
                    int smoothed = medianFromDeque(pitchWindow);
                    if (smoothed != -1) midiNote = smoothed;
                }
            }

            // Outlier rejection: ignore sudden leaps unless the hit is strong
            if (midiNote != -1 && lastStablePitch != -1 && std::abs(midiNote - lastStablePitch) > 4 && rms < envRms * 1.35f) {
                midiNote = -1;
            }
        }

        // Sticky note logic to smooth jitter (especially for bass mode)
        if (currentNote != -1 && midiNote != -1 && std::abs(midiNote - currentNote) <= (bassMode ? 2 : 1)) {
            midiNote = currentNote;
        }

        // Close note if we've been under threshold for a few frames
        if (currentNote != -1 && releaseCounter >= releaseFrames) {
            DetectedNote dn;
            dn.noteNumber = currentNote;
            dn.startTime = noteStartTime;
            dn.duration = time - noteStartTime;
            dn.velocity = currentVelocity;
            dn.isDrum = false;
            dn.drumType = -1;
            if (dn.duration >= minNoteDuration) {
                notes.push_back(dn);
                lastStablePitch = currentNote;
            }
            currentNote = -1;
        }

        // Start or retrigger a note
        if (bassMode && midiNote == -1 && currentNote == -1) {
            midiNote = 36; // fallback to kick if pitch is undefined
        }

        const bool significantChange = (!bassMode && midiNote != -1 && currentNote != -1 && std::abs(midiNote - currentNote) >= 2);
        const bool shouldStart = (!belowThreshold && midiNote != -1 && (currentNote == -1 || significantChange || retrigger));
        if (shouldStart) {
            // If we are changing pitch without silence, close the previous one first
            if (currentNote != -1 && midiNote != currentNote) {
                DetectedNote dn;
                dn.noteNumber = currentNote;
                dn.startTime = noteStartTime;
                dn.duration = time - noteStartTime;
                dn.velocity = currentVelocity;
                dn.isDrum = false;
                dn.drumType = -1;
                if (dn.duration >= minNoteDuration) {
                    notes.push_back(dn);
                }
            }

            currentNote = midiNote;
            noteStartTime = time;
            currentVelocity = std::min(1.0f, rms * 5.0f);
            releaseCounter = 0;
        }
    }

    // Close any trailing note
        if (currentNote != -1) {
            DetectedNote dn;
            dn.noteNumber = currentNote;
            dn.startTime = noteStartTime;
            dn.duration = static_cast<float>(samples.size()) / static_cast<float>(sampleRate) - noteStartTime;
            dn.velocity = currentVelocity;
            dn.isDrum = false;
            dn.drumType = -1;
            if (dn.duration >= minNoteDuration) {
                notes.push_back(dn);
                lastStablePitch = currentNote;
            }
        }

    return notes;
}

std::vector<DetectedNote> AudioAnalyzer::analyzeDrums(const std::vector<float>& samples, int sampleRate, float onsetThreshold) {
    std::vector<DetectedNote> notes;
    if (samples.empty() || sampleRate <= 0) return notes;

    const int windowSize = 1024;
    const int hopSize = 256;
    const float onThresh = std::max(0.0015f, onsetThreshold * 0.7f); // ensure sensitivity and allow quieter booms
    const float offThresh = onThresh * 0.55f;
    const int releaseFrames = 3;
    const float minGapSec = 0.08f; // avoid double triggers but allow closer sequences
    const float minDur = 0.08f;
    const float maxDur = 0.40f;

    float lastRms = 0.0f;
    int releaseCounter = 0;
    bool active = false;
    float lastOnsetTime = -minGapSec;
    float activeStartTime = 0.0f;
    int activeNote = 36;
    int activeType = 0;
    float peakRms = 0.0f;
    float centroidAtOnset = 0.0f;
    float domFreqAtOnset = 0.0f;
    float lowRatioAtOnset = 0.0f;
    float zcrAtOnset = 0.0f;
    bool kickLock = false;

    std::vector<float> onsetCentroid;
    std::vector<float> onsetDomFreq;
    std::vector<float> onsetLowRatio;
    g_lastDrumDebug.clear();

    auto classifyOnset = [&](const std::vector<float>& window,
                             float& centroidOut,
                             float& domFreqOut,
                             float& lowRatioOut,
                             float& zcrOut,
                             int& noteOut,
                             int& typeOut,
                             bool& kickLockOut) {
        auto spectrum = DSP::computeMagnitudeSpectrum(window); // size = N/2
        float low = 0.0f, mid = 0.0f, high = 0.0f, weightedFreq = 0.0f, total = 0.0f;
        const float binHz = static_cast<float>(sampleRate) / static_cast<float>(spectrum.size() * 2);
        for (size_t bin = 0; bin < spectrum.size(); ++bin) {
            float f = bin * binHz;
            float v = spectrum[bin] * spectrum[bin];
            if (f < 200.0f) low += v;
            else if (f < 2000.0f) mid += v;
            else high += v;
            weightedFreq += f * v;
            total += v;
        }
        const float centroid = total > 0.0f ? weightedFreq / total : 0.0f;
        const float totalEnergy = low + mid + high + 1e-9f;
        const float lowRatio = low / totalEnergy;

        int zeroCross = 0;
        for (int n = 1; n < windowSize; ++n) {
            if ((window[n - 1] >= 0.0f && window[n] < 0.0f) || (window[n - 1] < 0.0f && window[n] >= 0.0f)) {
                ++zeroCross;
            }
        }
        const float zcrRate = static_cast<float>(zeroCross) / static_cast<float>(windowSize);

        float domFreq = dominantFreqFFT(window, sampleRate);

        bool kick = false;
        bool hat = false;

        if ((domFreq > 0.0f && domFreq < 900.0f) || centroid < 2200.0f || low > high * 0.8f || lowRatio >= 0.10f || zcrRate < 0.15f) {
            kick = true;
        }
        if (domFreq > 4000.0f || centroid > 3500.0f || zcrRate > 0.35f || high > low * 1.5f) {
            hat = true;
        }
        if (lowRatio < 0.05f && centroid > 1800.0f) {
            hat = true;
            if (domFreq < 500.0f) {
                kick = false;
            }
        }

        bool strongKickOnset = (domFreq > 0.0f && domFreq < 500.0f && centroid < 1200.0f);

        if (hat && !kick) {
            noteOut = 42; // Hat
            typeOut = 2;
        } else if (kick && !hat) {
            noteOut = 36; // Kick
            typeOut = 0;
        } else if (kick && hat) {
            if (lowRatio >= 0.2f || zcrRate < 0.18f) {
                noteOut = 36; typeOut = 0;
            } else {
                noteOut = 42; typeOut = 2;
            }
        } else {
            noteOut = 38; // Snare fallback
            typeOut = 1;
        }

        // Final sanity: if we labeled kick but spectrum is bright, flip to hat
        if (typeOut == 0 && (centroid > 2800.0f || domFreq > 2500.0f)) {
            noteOut = 42;
            typeOut = 2;
            strongKickOnset = false;
        }

        centroidOut = centroid;
        domFreqOut = domFreq;
        lowRatioOut = lowRatio;
        zcrOut = zcrRate;
        kickLockOut = strongKickOnset && kick;
    };

    for (size_t i = 0; i + windowSize < samples.size(); i += hopSize) {
        const float time = static_cast<float>(i) / static_cast<float>(sampleRate);
        std::vector<float> window(samples.begin() + i, samples.begin() + i + windowSize);
            const float rms = computeRMS(window);

        if (!active) {
            if (rms > onThresh && rms > lastRms * 1.4f && (time - lastOnsetTime) > minGapSec) {
                classifyOnset(window, centroidAtOnset, domFreqAtOnset, lowRatioAtOnset, zcrAtOnset, activeNote, activeType, kickLock);

                active = true;
                activeStartTime = time;
                peakRms = rms;
                releaseCounter = 0;
                lastOnsetTime = time;
            }
        } else {
            peakRms = std::max(peakRms, rms);

            // Allow retrigger while active if a clear new attack arrives (prevents long windows swallowing consecutive hits)
            if ((time - activeStartTime) > minGapSec * 0.8f && rms > onThresh * 1.1f && rms > lastRms * 1.35f) {
                const float endTime = time;
                const float rawDur = endTime - activeStartTime;
                const float clampedDur = std::clamp(rawDur, minDur, maxDur);

                DetectedNote dn;
                dn.isDrum = true;
                dn.startTime = activeStartTime;
                dn.duration = clampedDur;
                dn.velocity = std::min(1.0f, peakRms * 5.0f);
                dn.instrumentOverride = -1;
                dn.noteNumber = activeNote;
                dn.drumType = activeType;
                if (dn.velocity >= 0.1f) {
                    notes.push_back(dn);
                    onsetCentroid.push_back(centroidAtOnset);
                    onsetDomFreq.push_back(domFreqAtOnset);
                    onsetLowRatio.push_back(lowRatioAtOnset);
                    g_lastDrumDebug.push_back({dn.startTime, centroidAtOnset, domFreqAtOnset, zcrAtOnset, lowRatioAtOnset, activeType});
                }

                // Start a new onset immediately from the current frame
                classifyOnset(window, centroidAtOnset, domFreqAtOnset, lowRatioAtOnset, zcrAtOnset, activeNote, activeType, kickLock);
                activeStartTime = time;
                peakRms = rms;
                releaseCounter = 0;
                lastOnsetTime = time;
                continue;
            }

            // For first 150 ms after onset, lock classification based on onset centroid/domFreq/ZCR
            if ((time - activeStartTime) <= 0.15f) {
                bool strongKickCue = (domFreqAtOnset > 0.0f && domFreqAtOnset < 500.0f && centroidAtOnset < 1200.0f && zcrAtOnset < 0.28f);
                bool strongHatCue = (domFreqAtOnset > 3500.0f || centroidAtOnset > 3500.0f || zcrAtOnset > 0.35f);

                if (activeType == 0) { // kick lock
                    if (!kickLock && strongHatCue) {
                        activeType = 2;
                        activeNote = 42;
                    }
                } else if (activeType == 2) { // hat lock
                    if (strongKickCue || kickLock) {
                        activeType = 0;
                        activeNote = 36;
                    }
                }
            }

            if (rms < offThresh) ++releaseCounter; else releaseCounter = 0;
            if (releaseCounter >= releaseFrames) {
                const float endTime = time + static_cast<float>(hopSize) / static_cast<float>(sampleRate);
                const float rawDur = endTime - activeStartTime;
                const float clampedDur = std::clamp(rawDur, minDur, maxDur);

                DetectedNote dn;
                dn.isDrum = true;
                dn.startTime = activeStartTime;
                dn.duration = clampedDur;
                dn.velocity = std::min(1.0f, peakRms * 5.0f);
                dn.instrumentOverride = -1;
                dn.noteNumber = activeNote;
                dn.drumType = activeType;

                if (dn.velocity >= 0.1f) {
                    notes.push_back(dn);
                    onsetCentroid.push_back(centroidAtOnset);
                    onsetDomFreq.push_back(domFreqAtOnset);
                    onsetLowRatio.push_back(lowRatioAtOnset);
                    g_lastDrumDebug.push_back({dn.startTime, centroidAtOnset, domFreqAtOnset, zcrAtOnset, lowRatioAtOnset, activeType});
                }

                active = false;
                releaseCounter = 0;
            }
        }

        lastRms = rms * 0.9f + lastRms * 0.1f; // small smoothing
    }

    // Close trailing active hit
    if (active) {
        const float endTime = static_cast<float>(samples.size()) / static_cast<float>(sampleRate);
        const float rawDur = endTime - activeStartTime;
        const float clampedDur = std::clamp(rawDur, minDur, maxDur);

        DetectedNote dn;
        dn.isDrum = true;
        dn.startTime = activeStartTime;
        dn.duration = clampedDur;
        dn.velocity = std::min(1.0f, peakRms * 5.0f);
        dn.instrumentOverride = -1;
        dn.noteNumber = activeNote;
        dn.drumType = activeType;
        if (dn.velocity >= 0.1f) {
            notes.push_back(dn);
            onsetCentroid.push_back(centroidAtOnset);
            onsetDomFreq.push_back(domFreqAtOnset);
            onsetLowRatio.push_back(lowRatioAtOnset);
            g_lastDrumDebug.push_back({dn.startTime, centroidAtOnset, domFreqAtOnset, zcrAtOnset, lowRatioAtOnset, activeType});
        }
    }

    // Post-classification adjustment: promote low/low-mid onsets mis-labeled as hats into kicks
    for (size_t i = 0; i < notes.size(); ++i) {
        float c = onsetCentroid[i];
        float d = onsetDomFreq[i];
        float lr = onsetLowRatio[i];
        bool kick = (d > 0.0f && d < 900.0f) || c < 2200.0f || lr >= 0.10f;
        bool hat = (d > 3500.0f) || c > 3500.0f || (lr < 0.05f && c > 1800.0f);
        if (kick && !hat) { notes[i].drumType = 0; notes[i].noteNumber = 36; }
        else if (hat && !kick) { notes[i].drumType = 2; notes[i].noteNumber = 42; }
        else if (kick && hat) {
            if (lr >= 0.12f) { notes[i].drumType = 0; notes[i].noteNumber = 36; }
            else { notes[i].drumType = 2; notes[i].noteNumber = 42; }
        }
    }

    // Post-pass: merge adjacent hits that are very close (any type) to avoid double triggers
    std::vector<DetectedNote> merged;
    const float mergeGapKick = 0.12f;
    const float mergeGapHat = 0.05f;
    const float mergeGapOther = 0.08f;
    for (const auto& n : notes) {
        if (!merged.empty()) {
            float prevEnd = merged.back().startTime + merged.back().duration;
            float gapLimit = mergeGapOther;
            if (merged.back().drumType == 0) gapLimit = mergeGapKick;
            else if (merged.back().drumType == 2) gapLimit = mergeGapHat;

            float startGap = n.startTime - merged.back().startTime;
            if (n.drumType == merged.back().drumType && startGap <= gapLimit) {
                merged.back().duration = std::max(prevEnd, n.startTime + n.duration) - merged.back().startTime;
                merged.back().velocity = std::max(merged.back().velocity, n.velocity);
                continue;
            }
        }
        merged.push_back(n);
    }

    return merged;
    return notes;
}

std::vector<DetectedNote> AudioAnalyzer::analyzeRhythm(const std::vector<float>& samples, int sampleRate, float onsetThreshold) {
    std::vector<DetectedNote> notes;
    if (samples.empty() || sampleRate <= 0) return notes;

    const int windowSize = 1024;
    const int hopSize = 256;
    const float onThresh = std::max(0.0015f, onsetThreshold * 0.9f);
    const float offThresh = onThresh * 0.6f;
    const int releaseFrames = 3;
    const float minGapSec = 0.05f;
    const float minDur = 0.05f;
    const float maxDur = 0.8f;

    bool active = false;
    float lastRms = 0.0f;
    float activeStart = 0.0f;
    float peak = 0.0f;
    float lastOnsetTime = -minGapSec;
    int releaseCounter = 0;
    float envRms = 0.0f;
    std::vector<float> prevSpec;

    for (size_t i = 0; i + windowSize < samples.size(); i += hopSize) {
        const float time = static_cast<float>(i) / static_cast<float>(sampleRate);
        std::vector<float> window(samples.begin() + i, samples.begin() + i + windowSize);
        const float rms = computeRMS(window);
        envRms = envRms * 0.97f + rms * 0.03f;

        // Spectral flux to detect onsets even at low RMS
        auto spectrum = DSP::computeMagnitudeSpectrum(window);
        float flux = 0.0f;
        if (!prevSpec.empty()) {
            const size_t count = std::min(prevSpec.size(), spectrum.size());
            for (size_t b = 0; b < count; ++b) {
                float diff = spectrum[b] - prevSpec[b];
                if (diff > 0.0f) flux += diff;
            }
        }
        prevSpec = std::move(spectrum);
        float fluxNorm = flux / 1000.0f; // rough scaling

        if (!active) {
            if (rms > onThresh && rms > lastRms * 1.25f && rms > envRms * 1.1f && (time - lastOnsetTime) > minGapSec) {
                active = true;
                activeStart = time;
                lastOnsetTime = time;
                peak = rms;
                releaseCounter = 0;
            } else if (fluxNorm > 0.12f && rms > onThresh * 0.6f && (time - lastOnsetTime) > minGapSec) {
                active = true;
                activeStart = time;
                lastOnsetTime = time;
                peak = rms;
                releaseCounter = 0;
            }
        } else {
            peak = std::max(peak, rms);
            if (rms < offThresh || rms < envRms * 0.65f) ++releaseCounter; else releaseCounter = 0;

            if (releaseCounter >= releaseFrames) {
                float endTime = time + static_cast<float>(hopSize) / static_cast<float>(sampleRate);
                float dur = std::clamp(endTime - activeStart, minDur, maxDur);

                DetectedNote dn;
                dn.isDrum = false;
                dn.drumType = -1;
                dn.instrumentOverride = -1;
                dn.noteNumber = 60; // middle C placeholder for rhythm-only mode
                dn.startTime = activeStart;
                dn.duration = dur;
                dn.velocity = std::min(1.0f, peak * 5.0f);

                notes.push_back(dn);
                active = false;
                releaseCounter = 0;
            }
        }

        lastRms = rms * 0.9f + lastRms * 0.1f;
    }

    if (active) {
        float endTime = static_cast<float>(samples.size()) / static_cast<float>(sampleRate);
        float dur = std::clamp(endTime - activeStart, minDur, maxDur);

        DetectedNote dn;
        dn.isDrum = false;
        dn.drumType = -1;
        dn.instrumentOverride = -1;
        dn.noteNumber = 60;
        dn.startTime = activeStart;
        dn.duration = dur;
        dn.velocity = std::min(1.0f, peak * 5.0f);
        notes.push_back(dn);
    }

    return notes;
}

std::vector<DetectedNote> AudioAnalyzer::analyzePolyphonic(const std::vector<float>& samples, int sampleRate, float threshold) {
    std::vector<DetectedNote> notes;
    if (samples.empty() || sampleRate <= 0) return notes;

    const int windowSize = 4096;
    const int hopSize = 1024;
    const float minNoteDuration = 0.07f;
    float envRms = 0.0f;

    for (size_t i = 0; i + windowSize < samples.size(); i += hopSize) {
        const float time = static_cast<float>(i) / static_cast<float>(sampleRate);
        std::vector<float> window(samples.begin() + i, samples.begin() + i + windowSize);
        const float rms = computeRMS(window);
        envRms = envRms * 0.98f + rms * 0.02f;
        const float gate = std::max(threshold, envRms * 0.8f);
        if (rms < gate) continue;

        auto spectrum = DSP::computeMagnitudeSpectrum(window);

        // Find top 3 peaks
        struct Peak { float mag; int bin; };
        std::array<Peak, 3> peaks = {Peak{0.0f, 0}, Peak{0.0f, 0}, Peak{0.0f, 0}};
        for (int bin = 1; bin < static_cast<int>(spectrum.size()) - 1; ++bin) {
            const float mag = spectrum[bin];
            if (mag > peaks[0].mag) {
                peaks[2] = peaks[1];
                peaks[1] = peaks[0];
                peaks[0] = {mag, bin};
            } else if (mag > peaks[1].mag) {
                peaks[2] = peaks[1];
                peaks[1] = {mag, bin};
            } else if (mag > peaks[2].mag) {
                peaks[2] = {mag, bin};
            }
        }

        for (const auto& p : peaks) {
            if (p.mag <= 0.0f) continue;
            const float refinedBin = parabolicInterpolate(spectrum, p.bin);
            const float freq = refinedBin * static_cast<float>(sampleRate) / static_cast<float>(windowSize);
            if (freq <= 0.0f) continue;

            DetectedNote dn;
            dn.noteNumber = freqToMidi(freq);
            dn.startTime = time;
            dn.duration = minNoteDuration;
            dn.velocity = std::min(1.0f, rms * 5.0f);
            dn.isDrum = false;
            dn.drumType = -1;

            notes.push_back(dn);
        }
    }

    return notes;
}

DetectedKey AudioAnalyzer::detectKey(const std::vector<float>& samples, int sampleRate) {
    DetectedKey key{0, false, 0.0f};
    if (samples.empty() || sampleRate <= 0) return key;

    const int windowSize = 4096;
    const int hopSize = 2048;
    const float pitchThreshold = 0.01f;
    std::array<float, 12> histogram{};
    histogram.fill(0.0f);

    for (size_t i = 0; i + windowSize < samples.size(); i += hopSize) {
        std::vector<float> window(samples.begin() + i, samples.begin() + i + windowSize);
        const float rms = computeRMS(window);
        if (rms < pitchThreshold) continue;

        const float freq = getPitchYIN(window, sampleRate);
        if (freq <= 0.0f) continue;
        const int midi = freqToMidi(freq);
        if (midi < 0) continue;

        /*
         * Weighted by duration, not by loudness.
         *
         * Every frame is the same length, so counting frames is counting
         * time. Weighting by RMS instead - which this did - lets one
         * loudly-hummed note decide the key of the whole take, and people
         * do not hum at a constant level.
         */
        histogram[midi % 12] += 1.0f;
    }

    /*
     * Aarden-Essen profiles, not Krumhansl-Kessler.
     *
     * Aarden-Essen was fitted to the Essen folksong corpus - monophonic
     * melody, which is exactly what a hum is. Krumhansl-Kessler came from
     * probe-tone experiments on chords.
     *
     * They also matter for a second reason: KK's two halves sum to 41.79
     * and 44.51, so scoring them with a dot product gives minor a 6.5%
     * advantage on any flat or noisy histogram - before a single note is
     * considered. That is what this function used to do, and a hum makes
     * precisely the kind of flat histogram that trips it. Both Aarden-Essen
     * halves sum to 100, and the Pearson correlation below is
     * scale-invariant anyway, so the bias cannot come back by either route.
     */
    static const float majorProfile[12] = {
        17.7661f, 0.145624f, 14.9265f, 0.160186f, 19.8049f, 11.3587f,
        0.291248f, 22.062f, 0.145624f, 8.15494f, 0.232998f, 4.95122f};
    static const float minorProfile[12] = {
        18.2648f, 0.737619f, 14.0499f, 16.8599f, 0.702494f, 14.4362f,
        0.702494f, 18.6161f, 4.56621f, 1.93186f, 7.37619f, 1.75623f};

    // Pearson correlation between the histogram and a rotated profile.
    auto pearson = [&](const float profile[12], int shift) {
        float meanH = 0.0f, meanP = 0.0f;
        for (int i = 0; i < 12; ++i) {
            meanH += histogram[static_cast<size_t>(i)];
            meanP += profile[i];
        }
        meanH /= 12.0f;
        meanP /= 12.0f;

        float covariance = 0.0f, varianceH = 0.0f, varianceP = 0.0f;
        for (int i = 0; i < 12; ++i) {
            const float h = histogram[static_cast<size_t>(i)] - meanH;
            const float p = profile[(i + 12 - shift) % 12] - meanP;
            covariance += h * p;
            varianceH += h * h;
            varianceP += p * p;
        }
        if (varianceH <= 1e-9f || varianceP <= 1e-9f) return 0.0f;
        return covariance / std::sqrt(varianceH * varianceP);
    };

    // Every one of the 24 keys, so the runner-up is known and the margin
    // between first and second can be reported.
    float bestScore = -2.0f, secondScore = -2.0f;
    int bestRoot = 0;
    bool bestMinor = false;

    for (int shift = 0; shift < 12; ++shift) {
        for (int minorPass = 0; minorPass < 2; ++minorPass) {
            const float score =
                pearson(minorPass ? minorProfile : majorProfile, shift);
            if (score > bestScore) {
                secondScore = bestScore;
                bestScore = score;
                bestRoot = shift;
                bestMinor = (minorPass != 0);
            } else if (score > secondScore) {
                secondScore = score;
            }
        }
    }

    key.root = bestRoot;
    key.isMinor = bestMinor;
    key.correlation = bestScore;

    /*
     * How much better the winner is than the runner-up.
     *
     * A short hum is often genuinely ambiguous between relative major and
     * minor, and a confidently-wrong key destroys a take when notes get
     * snapped to it. Reporting the margin lets the caller decline to snap
     * rather than guess - which the research is clear is worth more than
     * any accuracy improvement to the guess itself.
     */
    key.confidence = std::max(0.0f, bestScore - secondScore);
    return key;
}

int AudioAnalyzer::getSmartHarmonyNote(int note, const DetectedKey& key, int interval) {
    const int normalized = ((note % 12) + 12) % 12;
    const bool useMinor = key.isMinor;

    // Decide between major/minor third relative to key context
    int offset = interval;
    if (interval == 3) {
        const int majorThird = 4;
        const int minorThird = 3;
        const int rel = ((normalized - key.root) + 12) % 12;
        const bool preferMinor = useMinor || rel == 2 || rel == 5 || rel == 9; // rough heuristic
        offset = preferMinor ? minorThird : majorThird;
    }

    return note + offset;
}

int AudioAnalyzer::freqToMidi(float freq) {
    if (freq <= 0.0f) return -1;
    return static_cast<int>(std::lround(69.0 + 12.0 * std::log2(freq / 440.0f)));
}

float AudioAnalyzer::getPitchYIN(const std::vector<float>& buffer, int sampleRate) {
    const size_t bufferSize = buffer.size();
    if (bufferSize < 16 || sampleRate <= 0) return -1.0f;

    const size_t tauMax = bufferSize / 2;
    std::vector<float> diff(tauMax, 0.0f);
    std::vector<float> cmnd(tauMax, 0.0f);

    for (size_t tau = 1; tau < tauMax; ++tau) {
        float sum = 0.0f;
        for (size_t i = 0; i < bufferSize - tau; ++i) {
            const float delta = buffer[i] - buffer[i + tau];
            sum += delta * delta;
        }
        diff[tau] = sum;
    }

    cmnd[0] = 1.0f;
    float runningSum = 0.0f;
    for (size_t tau = 1; tau < tauMax; ++tau) {
        runningSum += diff[tau];
        cmnd[tau] = diff[tau] * static_cast<float>(tau) / runningSum;
    }

    const float threshold = 0.1f;
    size_t tauEstimate = 0;
    for (size_t tau = 2; tau < tauMax; ++tau) {
        if (cmnd[tau] < threshold) {
            while (tau + 1 < tauMax && cmnd[tau + 1] < cmnd[tau]) {
                ++tau;
            }
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate == 0) return -1.0f;

    // Parabolic interpolation for better precision
    const size_t tau0 = tauEstimate > 0 ? tauEstimate - 1 : tauEstimate;
    const size_t tau2 = tauEstimate + 1 < tauMax ? tauEstimate + 1 : tauEstimate;
    const float s0 = cmnd[tau0];
    const float s1 = cmnd[tauEstimate];
    const float s2 = cmnd[tau2];
    const float denom = (s0 + s2 - 2.0f * s1);
    float betterTau = static_cast<float>(tauEstimate);
    if (std::abs(denom) > 1e-12f) {
        betterTau += 0.5f * (s0 - s2) / denom;
    }

    return static_cast<float>(sampleRate) / betterTau;
}
