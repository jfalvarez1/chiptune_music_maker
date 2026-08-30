#pragma once

/*
 * ChiptuneTracker - Project validation
 *
 * A .ctp file is untrusted input. It may be truncated, hand-edited, written
 * by an older version of the app, or simply corrupt. Whatever comes out of
 * the loader is handed straight to the sequencer and the audio thread, so a
 * nonsensical value there does not stay a cosmetic problem: a negative song
 * length becomes a hang, a NaN BPM becomes silence, an out-of-range pattern
 * index becomes a crash.
 *
 * This header is the single place that decides what "usable" means for every
 * field. Keep it free of I/O and UI concerns so it can be unit-tested on its
 * own and reused anywhere a Project arrives from outside the app (file load,
 * MIDI import, undo history, a future network or plugin path).
 */

#include "Types.h"

#include <cmath>
#include <algorithm>

namespace ChiptuneTracker {

// ============================================================================
// Limits
//
// These are deliberately generous. The goal is to reject the impossible, not
// to impose taste - a 300 BPM song in 7/8 is unusual, not invalid.
// ============================================================================
namespace ProjectLimits {

inline constexpr float MIN_BPM = 20.0f;
inline constexpr float MAX_BPM = 999.0f;
inline constexpr float DEFAULT_BPM = 120.0f;

inline constexpr int MIN_BEATS_PER_MEASURE = 1;
inline constexpr int MAX_BEATS_PER_MEASURE = 32;
inline constexpr int DEFAULT_BEATS_PER_MEASURE = 4;

inline constexpr float MIN_SONG_LENGTH = 1.0f;
inline constexpr float MAX_SONG_LENGTH = 100000.0f;   // ~17 hours at 100 BPM
inline constexpr float DEFAULT_SONG_LENGTH = 64.0f;

inline constexpr int MIN_PATTERN_LENGTH = 1;
inline constexpr int MAX_PATTERN_LENGTH = 4096;

inline constexpr float MAX_NOTE_DURATION = 10000.0f;  // beats
inline constexpr float MAX_NOTE_START = 100000.0f;    // beats

inline constexpr int MIN_PITCH = 0;
inline constexpr int MAX_PITCH = 127;

} // namespace ProjectLimits

// ============================================================================
// Helpers
// ============================================================================

// Clamp a float into [lo, hi], substituting `fallback` for NaN and infinity.
// Every numeric field below goes through this, because a NaN silently
// poisons every arithmetic operation downstream of it.
inline float sanitizeFloat(float value, float lo, float hi, float fallback) {
    if (!std::isfinite(value)) return fallback;
    return std::max(lo, std::min(value, hi));
}

inline int sanitizeInt(int value, int lo, int hi, int fallback) {
    if (value < lo || value > hi) return fallback;
    return value;
}

// ============================================================================
// Per-object validation
// ============================================================================

inline void clampNoteToValidRanges(Note& note) {
    using namespace ProjectLimits;

    note.pitch = std::max(MIN_PITCH, std::min(note.pitch, MAX_PITCH));
    note.velocity = sanitizeFloat(note.velocity, 0.0f, 1.0f, 1.0f);
    note.startTime = sanitizeFloat(note.startTime, 0.0f, MAX_NOTE_START, 0.0f);

    // A zero or negative duration would be a note that never ends or never
    // sounds; give it the shortest audible length instead of dropping it, so
    // a damaged file still shows the user where their notes were.
    note.duration = sanitizeFloat(note.duration, 0.0f, MAX_NOTE_DURATION, 1.0f);
    if (note.duration <= 0.0f) note.duration = 0.0625f;

    note.fadeIn = sanitizeFloat(note.fadeIn, 0.0f, MAX_NOTE_DURATION, 0.0f);
    note.fadeOut = sanitizeFloat(note.fadeOut, 0.0f, MAX_NOTE_DURATION, 0.0f);

    // Tracker effects
    note.arpeggio = sanitizeInt(note.arpeggio, 0, 0xFF, 0);
    note.vibrato = sanitizeFloat(note.vibrato, 0.0f, 24.0f, 0.0f);
    note.vibratoSpeed = sanitizeFloat(note.vibratoSpeed, 0.0f, 100.0f, 6.0f);
    note.slide = sanitizeFloat(note.slide, -96.0f, 96.0f, 0.0f);

    note.sweepSpeed = sanitizeFloat(note.sweepSpeed, 0.0f, 100.0f, 1.0f);
    note.sweepAmount = sanitizeFloat(note.sweepAmount, 0.0f, 96.0f, 12.0f);

    note.echoRepeats = sanitizeInt(note.echoRepeats, 0, 16, 0);
    note.echoDelay = sanitizeFloat(note.echoDelay, 0.0f, 16.0f, 0.25f);
    note.echoDecay = sanitizeFloat(note.echoDecay, 0.0f, 1.0f, 0.5f);

    note.retriggerCount = sanitizeInt(note.retriggerCount, 0, 64, 0);
    note.retriggerSpeed = sanitizeFloat(note.retriggerSpeed, 0.001f, 16.0f, 0.125f);

    note.noteCut = sanitizeFloat(note.noteCut, 0.0f, MAX_NOTE_DURATION, 0.0f);
    note.noteDelay = sanitizeFloat(note.noteDelay, 0.0f, MAX_NOTE_DURATION, 0.0f);

    note.tremolo = sanitizeFloat(note.tremolo, 0.0f, 1.0f, 0.0f);
    note.tremoloSpeed = sanitizeFloat(note.tremoloSpeed, 0.0f, 100.0f, 4.0f);

    // sampleID is an index into the sample pool; -1 means "use the oscillator"
    if (note.sampleID < -1) note.sampleID = -1;
}

inline void clampChannelToValidRanges(ChannelConfig& c) {
    c.volume = sanitizeFloat(c.volume, 0.0f, 2.0f, 0.8f);
    c.pan = sanitizeFloat(c.pan, -1.0f, 1.0f, 0.0f);

    // Envelope
    c.envelope.attack = sanitizeFloat(c.envelope.attack, 0.0f, 60.0f, 0.01f);
    c.envelope.decay = sanitizeFloat(c.envelope.decay, 0.0f, 60.0f, 0.1f);
    c.envelope.sustain = sanitizeFloat(c.envelope.sustain, 0.0f, 1.0f, 0.7f);
    c.envelope.release = sanitizeFloat(c.envelope.release, 0.0f, 60.0f, 0.2f);

    // Filter
    c.filterCutoff = sanitizeFloat(c.filterCutoff, 20.0f, 20000.0f, 2000.0f);
    c.filterResonance = sanitizeFloat(c.filterResonance, 0.0f, 1.0f, 0.0f);
    c.filterEnvAmount = sanitizeFloat(c.filterEnvAmount, -1.0f, 1.0f, 0.0f);
    c.filterEnvAttack = sanitizeFloat(c.filterEnvAttack, 0.0f, 10.0f, 0.0f);
    c.filterEnvDecay = sanitizeFloat(c.filterEnvDecay, 0.0f, 10.0f, 0.1f);

    // Drive-style effects
    c.bitDepth = sanitizeInt(c.bitDepth, 1, 16, 8);
    c.sampleRateDiv = sanitizeInt(c.sampleRateDiv, 1, 128, 1);
    c.distortionDrive = sanitizeFloat(c.distortionDrive, 0.0f, 100.0f, 1.0f);
    c.distortionMix = sanitizeFloat(c.distortionMix, 0.0f, 1.0f, 0.5f);

    // EQ (dB)
    c.eqLow = sanitizeFloat(c.eqLow, -24.0f, 24.0f, 0.0f);
    c.eqMid = sanitizeFloat(c.eqMid, -24.0f, 24.0f, 0.0f);
    c.eqHigh = sanitizeFloat(c.eqHigh, -24.0f, 24.0f, 0.0f);
    c.eqLowFreq = sanitizeFloat(c.eqLowFreq, 20.0f, 20000.0f, 200.0f);
    c.eqMidFreq = sanitizeFloat(c.eqMidFreq, 20.0f, 20000.0f, 1000.0f);
    c.eqHighFreq = sanitizeFloat(c.eqHighFreq, 20.0f, 20000.0f, 5000.0f);

    // Compressor. threshold is a LINEAR level here, not dB - a negative value
    // would make the gain calculation invert the signal.
    c.compThreshold = sanitizeFloat(c.compThreshold, 0.0001f, 1.0f, 0.5f);
    c.compRatio = sanitizeFloat(c.compRatio, 1.0f, 20.0f, 4.0f);
    c.compAttack = sanitizeFloat(c.compAttack, 0.0f, 1.0f, 0.01f);
    c.compRelease = sanitizeFloat(c.compRelease, 0.0f, 5.0f, 0.1f);
    c.compGain = sanitizeFloat(c.compGain, 0.0f, 8.0f, 1.0f);

    // Formant
    c.formantResonance = sanitizeFloat(c.formantResonance, 0.0f, 1.0f, 0.5f);

    // Time-based effects. Feedback strictly below 1.0 or they self-oscillate.
    c.delayTime = sanitizeFloat(c.delayTime, 0.0f, 1.0f, 0.25f);
    c.delayFeedback = sanitizeFloat(c.delayFeedback, 0.0f, 0.95f, 0.4f);
    c.delayMix = sanitizeFloat(c.delayMix, 0.0f, 1.0f, 0.3f);

    c.chorusRate = sanitizeFloat(c.chorusRate, 0.0f, 20.0f, 1.0f);
    c.chorusDepth = sanitizeFloat(c.chorusDepth, 0.0f, 1.0f, 0.5f);
    c.chorusMix = sanitizeFloat(c.chorusMix, 0.0f, 1.0f, 0.5f);

    c.flangerRate = sanitizeFloat(c.flangerRate, 0.0f, 20.0f, 0.5f);
    c.flangerDepth = sanitizeFloat(c.flangerDepth, 0.0f, 0.02f, 0.005f);
    c.flangerFeedback = sanitizeFloat(c.flangerFeedback, -0.95f, 0.95f, 0.5f);
    c.flangerMix = sanitizeFloat(c.flangerMix, 0.0f, 1.0f, 0.5f);

    c.phaserRate = sanitizeFloat(c.phaserRate, 0.0f, 20.0f, 0.5f);
    c.phaserDepth = sanitizeFloat(c.phaserDepth, 0.0f, 1.0f, 0.5f);
    c.phaserFeedback = sanitizeFloat(c.phaserFeedback, -0.95f, 0.95f, 0.5f);

    c.tremoloRate = sanitizeFloat(c.tremoloRate, 0.0f, 50.0f, 4.0f);
    c.tremoloDepth = sanitizeFloat(c.tremoloDepth, 0.0f, 1.0f, 0.5f);

    c.sidechainAmount = sanitizeFloat(c.sidechainAmount, 0.0f, 1.0f, 0.5f);
    c.sidechainRelease = sanitizeFloat(c.sidechainRelease, 0.001f, 2.0f, 0.2f);

    c.reverbMix = sanitizeFloat(c.reverbMix, 0.0f, 1.0f, 0.2f);
    c.reverbRoomSize = sanitizeFloat(c.reverbRoomSize, 0.0f, 1.0f, 0.5f);
    c.reverbDamping = sanitizeFloat(c.reverbDamping, 0.0f, 1.0f, 0.5f);

    c.stereoWidenerWidth = sanitizeFloat(c.stereoWidenerWidth, 0.0f, 2.0f, 1.0f);
    c.stereoWidenerHaas = sanitizeFloat(c.stereoWidenerHaas, 0.0f, 0.05f, 0.01f);
    c.stereoWidenerMix = sanitizeFloat(c.stereoWidenerMix, 0.0f, 1.0f, 0.5f);

    c.tapeDrive = sanitizeFloat(c.tapeDrive, 0.0f, 1.0f, 0.3f);
    c.tapeWarmth = sanitizeFloat(c.tapeWarmth, 0.0f, 1.0f, 0.5f);
    c.tapeCompression = sanitizeFloat(c.tapeCompression, 0.0f, 1.0f, 0.3f);
    c.tapeMix = sanitizeFloat(c.tapeMix, 0.0f, 1.0f, 1.0f);
}

// ============================================================================
// Whole-project validation
// ============================================================================
inline void clampProjectToValidRanges(Project& project) {
    using namespace ProjectLimits;

    project.bpm = sanitizeFloat(project.bpm, MIN_BPM, MAX_BPM, DEFAULT_BPM);
    project.beatsPerMeasure = sanitizeInt(project.beatsPerMeasure,
                                          MIN_BEATS_PER_MEASURE,
                                          MAX_BEATS_PER_MEASURE,
                                          DEFAULT_BEATS_PER_MEASURE);
    project.masterVolume = sanitizeFloat(project.masterVolume, 0.0f, 2.0f, 0.7f);
    project.songLength = sanitizeFloat(project.songLength, MIN_SONG_LENGTH,
                                       MAX_SONG_LENGTH, DEFAULT_SONG_LENGTH);

    // Groove
    project.swing = sanitizeFloat(project.swing, 0.0f, 1.0f, 0.0f);
    project.swingGrid = sanitizeFloat(project.swingGrid, 0.03125f, 4.0f, 0.5f);
    project.humanizeAmount = sanitizeFloat(project.humanizeAmount, 0.0f, 1.0f, 0.02f);
    project.humanizeVelocity = sanitizeFloat(project.humanizeVelocity, 0.0f, 1.0f, 0.1f);

    // Master bus
    project.masterEQLowGain = sanitizeFloat(project.masterEQLowGain, -24.0f, 24.0f, 0.0f);
    project.masterEQMidGain = sanitizeFloat(project.masterEQMidGain, -24.0f, 24.0f, 0.0f);
    project.masterEQHighGain = sanitizeFloat(project.masterEQHighGain, -24.0f, 24.0f, 0.0f);
    project.masterCompThreshold = sanitizeFloat(project.masterCompThreshold, -60.0f, 0.0f, -12.0f);
    project.masterCompRatio = sanitizeFloat(project.masterCompRatio, 1.0f, 20.0f, 2.5f);
    project.masterCompAttack = sanitizeFloat(project.masterCompAttack, 0.0f, 1.0f, 0.01f);
    project.masterCompRelease = sanitizeFloat(project.masterCompRelease, 0.0f, 5.0f, 0.1f);
    project.masterCompMakeup = sanitizeFloat(project.masterCompMakeup, -12.0f, 24.0f, 2.0f);
    project.masterLimiterCeiling = sanitizeFloat(project.masterLimiterCeiling, -12.0f, 0.0f, -0.3f);
    project.masterLimiterRelease = sanitizeFloat(project.masterLimiterRelease, 0.001f, 2.0f, 0.05f);

    for (ChannelConfig& channel : project.channels) {
        clampChannelToValidRanges(channel);
    }

    // Patterns
    if (project.patterns.size() > static_cast<size_t>(Project::MAX_PATTERNS)) {
        project.patterns.resize(Project::MAX_PATTERNS);
    }
    for (Pattern& pattern : project.patterns) {
        pattern.length = sanitizeInt(pattern.length, MIN_PATTERN_LENGTH,
                                     MAX_PATTERN_LENGTH, Pattern::DEFAULT_LENGTH);
        for (Note& note : pattern.notes) {
            clampNoteToValidRanges(note);
        }
    }

    // Arrangement. A clip pointing at a pattern or channel that does not exist
    // is dropped rather than clamped - silently retargeting someone's clip to
    // a different pattern would be worse than losing it.
    const int patternCount = static_cast<int>(project.patterns.size());
    project.arrangement.erase(
        std::remove_if(project.arrangement.begin(), project.arrangement.end(),
                       [patternCount](const Clip& clip) {
                           return clip.patternIndex < 0 ||
                                  clip.patternIndex >= patternCount ||
                                  clip.channelIndex < 0 ||
                                  clip.channelIndex >= Project::MAX_CHANNELS;
                       }),
        project.arrangement.end());

    for (Clip& clip : project.arrangement) {
        clip.startBeat = sanitizeFloat(clip.startBeat, 0.0f, MAX_SONG_LENGTH, 0.0f);
        clip.lengthBeats = sanitizeFloat(clip.lengthBeats, 0.0f, MAX_SONG_LENGTH, 16.0f);
        if (clip.lengthBeats <= 0.0f) clip.lengthBeats = 1.0f;
    }
}

} // namespace ChiptuneTracker
