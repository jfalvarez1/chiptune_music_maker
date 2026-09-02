#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include "FFT.h"

struct DetectedNote {
    int noteNumber; // MIDI note number
    float startTime; // In seconds
    float duration; // In seconds
    float velocity = 0.8f; // 0.0 - 1.0, default to 0.8 to simplify
    bool isDrum;    // True if drum
    int drumType;   // 0: Kick, 1: Snare, 2: HiHat
    int instrumentOverride = -1; // -1 means use global default
};

struct DetectedKey {
    int root; // 0=C, 1=C#, ... 11=B
    bool isMinor;
    float correlation;

    /*
     * How far ahead the winning key is of the runner-up.
     *
     * A short hum is often genuinely ambiguous between relative major and
     * minor. Snapping notes to a confidently-wrong key destroys a take, so
     * a caller that is about to do that should check this first and decline
     * rather than guess. Near zero means "no idea".
     *
     * Appended, and every future field must be too - this struct is
     * aggregate-initialised.
     */
    float confidence = 0.0f;
};

struct DrumDebugInfo {
    float startTime;
    float centroid;
    float domFreq;
    float zcr;
    float lowRatio;
    int drumType;
};

class AudioAnalyzer {
public:
    static std::vector<DetectedNote> analyzeMelody(const std::vector<float>& samples, int sampleRate, float rmsThreshold = 0.02f, bool bassMode = false);
    static std::vector<DetectedNote> analyzeDrums(const std::vector<float>& samples, int sampleRate, float onsetThreshold = 0.05f);
    static std::vector<DetectedNote> analyzeRhythm(const std::vector<float>& samples, int sampleRate, float onsetThreshold = 0.02f);
    static std::vector<DetectedNote> analyzePolyphonic(const std::vector<float>& samples, int sampleRate, float threshold = 0.05f);
    static DetectedKey detectKey(const std::vector<float>& samples, int sampleRate);
    static int getSmartHarmonyNote(int note, const DetectedKey& key, int interval);
    static int freqToMidi(float freq);
    static float getPitchYIN(const std::vector<float>& buffer, int sampleRate);

    // Debug helpers
    static const std::vector<DrumDebugInfo>& getLastDrumDebug();
};
