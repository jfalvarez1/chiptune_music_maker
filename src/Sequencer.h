#pragma once

/*
 * ChiptuneTracker - Sequencer Module
 *
 * Handles pattern playback, timeline arrangement,
 * and real-time note scheduling.
 */

#include "Types.h"
#include "Synthesizer.h"
#include <array>
#include <algorithm>
#include <cstdlib>

namespace ChiptuneTracker {

// ============================================================================
// Sequencer - Manages playback and note scheduling
// ============================================================================
class Sequencer {
public:
    static constexpr int MAX_CHANNELS = 8;

    Sequencer() {
        for (auto& synth : m_synths) {
            synth.setSampleRate(44100.0f);
        }
    }

    void setSampleRate(float sr) {
        m_sampleRate = sr;
        for (auto& synth : m_synths) {
            synth.setSampleRate(sr);
        }
    }

    void setProject(Project* project) {
        m_project = project;
        updateChannelConfigs();
    }

    // ========================================================================
    // Transport Controls
    // ========================================================================
    void play() {
        m_state.isPlaying = true;
    }

    void pause() {
        m_state.isPlaying = false;
    }

    void stop() {
        m_state.isPlaying = false;
        m_state.currentBeat = 0.0f;
        m_state.currentTime = 0.0f;
        allNotesOff();
    }

    void setPosition(float beat) {
        m_state.currentBeat = beat;
        m_state.currentTime = beatToTime(beat);
        allNotesOff();
    }

    void setLoop(bool enabled, float start, float end) {
        m_state.loop = enabled;
        m_state.loopStart = start;
        m_state.loopEnd = end;
    }

    void setLoopEnabled(bool enabled) {
        m_state.loop = enabled;
    }

    void setBPM(float bpm) {
        if (m_project) {
            m_project->bpm = bpm;
        }
    }

    // ========================================================================
    // State Queries
    // ========================================================================
    const PlaybackState& getState() const { return m_state; }
    float getCurrentBeat() const { return m_state.currentBeat; }
    float getCurrentTime() const { return m_state.currentTime; }
    bool isPlaying() const { return m_state.isPlaying; }

    // ========================================================================
    // Audio Processing (Called from audio thread)
    // ========================================================================
    void process(float* leftOut, float* rightOut, uint32_t frameCount) {
        if (!m_project) {
            std::fill_n(leftOut, frameCount, 0.0f);
            std::fill_n(rightOut, frameCount, 0.0f);
            return;
        }

        float bpm = m_project->bpm;
        float beatsPerSample = bpm / 60.0f / m_sampleRate;

        for (uint32_t i = 0; i < frameCount; ++i) {
            float prevBeat = m_state.currentBeat;

            // Advance time if playing
            if (m_state.isPlaying) {
                m_state.currentBeat += beatsPerSample;
                m_state.currentTime += 1.0f / m_sampleRate;

                // Get the actual end time based on notes in the pattern
                float effectiveEnd = getPatternEndTime();

                // Handle looping or stop at end of last note
                if (m_state.currentBeat >= effectiveEnd && effectiveEnd > 0.0f) {
                    if (m_state.loop) {
                        // Loop back to start
                        m_state.currentBeat = m_state.loopStart;
                        allNotesOff();
                    } else {
                        // Stop playback when last note ends
                        m_state.isPlaying = false;
                        m_state.currentBeat = effectiveEnd;
                        allNotesOff();
                    }
                }

                // Process note events that occurred in this sample
                processNoteEvents(prevBeat, m_state.currentBeat);
            }

            // ============================================================
            // Two-pass mix for sidechain support
            // ============================================================

            // Pass 1: Generate all channel samples (pre-sidechain)
            std::array<float, MAX_CHANNELS> channelSamples = {};
            for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                channelSamples[ch] = m_synths[ch].process(m_state.currentTime);
            }

            // Pass 2: Update sidechain envelopes and apply sidechain compression
            for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                auto& fx = m_synths[ch].effects();
                if (fx.sidechainEnabled && fx.sidechainSource >= 0 && fx.sidechainSource < MAX_CHANNELS) {
                    // Update envelope from source channel
                    fx.sidechain.updateEnvelope(channelSamples[fx.sidechainSource]);
                    // Apply sidechain compression to this channel
                    channelSamples[ch] = fx.sidechain.process(channelSamples[ch]);
                }
            }

            // Pass 3: Mix channels to stereo output
            float left = 0.0f;
            float right = 0.0f;

            // Check for solo state once
            bool hasSolo = false;
            for (int c = 0; c < MAX_CHANNELS; ++c) {
                if (m_project->channels[c].solo) {
                    hasSolo = true;
                    break;
                }
            }

            for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                if (m_project->channels[ch].muted) continue;
                if (hasSolo && !m_project->channels[ch].solo) continue;

                float sample = channelSamples[ch];
                float volume = m_project->channels[ch].volume;
                float pan = m_project->channels[ch].pan;

                // Pan law (constant power)
                float leftGain = std::cos((pan + 1.0f) * 0.25f * PI) * volume;
                float rightGain = std::sin((pan + 1.0f) * 0.25f * PI) * volume;

                left += sample * leftGain;
                right += sample * rightGain;
            }

            // Apply master volume and soft clip
            float master = m_project->masterVolume;
            left = std::tanh(left * master);
            right = std::tanh(right * master);

            leftOut[i] = left;
            rightOut[i] = right;
        }
    }

    // ========================================================================
    // Manual Note Trigger (For live play / testing)
    // ========================================================================
    void triggerNote(int channel, int note, float velocity) {
        if (channel >= 0 && channel < MAX_CHANNELS) {
            m_synths[channel].noteOn(note, velocity, m_state.currentTime);
        }
    }

    void releaseNote(int channel, int note) {
        if (channel >= 0 && channel < MAX_CHANNELS) {
            m_synths[channel].noteOff(note, m_state.currentTime);
        }
    }

    // Preview note with specific oscillator type (for sound preview when placing)
    void previewNote(int note, float velocity, OscillatorType oscType, float durationSec = 0.3f) {
        // Use a dedicated preview channel (last channel)
        const int previewChannel = MAX_CHANNELS - 1;

        // Stop any currently playing preview sounds first
        m_synths[previewChannel].allNotesOff();

        // For drums, use their natural duration
        if (isDrumType(oscType)) {
            durationSec = getDrumDecayTime(oscType) * 1.5f;
        }

        m_synths[previewChannel].noteOn(
            note, velocity, m_state.currentTime,
            0.0f,  // fadeIn
            0.05f, // fadeOut (short fade to avoid clicks)
            durationSec,
            oscType
        );
    }

    // ========================================================================
    // Channel Access
    // ========================================================================
    Synthesizer& getSynth(int channel) {
        return m_synths[channel % MAX_CHANNELS];
    }

    void updateChannelConfigs() {
        if (!m_project) return;

        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            const auto& config = m_project->channels[ch];
            m_synths[ch].setConfig(config.oscillator, config.envelope);

            // Sync effect enables
            auto& fx = m_synths[ch].effects();
            fx.bitcrusherEnabled = config.bitcrusherEnabled;
            fx.distortionEnabled = config.distortionEnabled;
            fx.filterEnabled = config.filterEnabled;
            fx.delayEnabled = config.delayEnabled;
        }
    }

private:
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    float beatToTime(float beat) const {
        if (!m_project) return 0.0f;
        return beat * 60.0f / m_project->bpm;
    }

    void allNotesOff() {
        for (auto& synth : m_synths) {
            synth.allNotesOff();
        }
    }

    void processNoteEvents(float fromBeat, float toBeat) {
        if (!m_project) return;

        // Check all clips in arrangement
        for (const auto& clip : m_project->arrangement) {
            if (clip.patternIndex < 0 ||
                clip.patternIndex >= static_cast<int>(m_project->patterns.size())) {
                continue;
            }

            const auto& pattern = m_project->patterns[clip.patternIndex];

            // Check if this clip is active in current beat range
            float clipEnd = clip.startBeat + clip.lengthBeats;
            if (toBeat < clip.startBeat || fromBeat > clipEnd) {
                continue;
            }

            // Process notes in this pattern
            for (const auto& note : pattern.notes) {
                float noteAbsStart = clip.startBeat + note.startTime;
                float noteAbsEnd = noteAbsStart + note.duration;

                // Note on
                if (noteAbsStart >= fromBeat && noteAbsStart < toBeat) {
                    // Convert fade times from beats to seconds
                    float fadeInSec = beatsToSeconds(note.fadeIn);
                    float fadeOutSec = beatsToSeconds(note.fadeOut);
                    float durationSec = beatsToSeconds(note.duration);

                    m_synths[clip.channelIndex].noteOn(
                        note.pitch, note.velocity, m_state.currentTime,
                        fadeInSec, fadeOutSec, durationSec, note.oscillatorType,
                        note.vibrato, note.arpeggio, note.slide);
                }

                // Note off
                if (noteAbsEnd >= fromBeat && noteAbsEnd < toBeat) {
                    m_synths[clip.channelIndex].noteOff(
                        note.pitch, m_state.currentTime);
                }
            }
        }

        // Also check pattern preview (current selected pattern, not on timeline)
        if (m_previewPattern >= 0 &&
            m_previewPattern < static_cast<int>(m_project->patterns.size())) {

            const auto& pattern = m_project->patterns[m_previewPattern];
            float loopLength = static_cast<float>(pattern.length);

            // Wrap beat position for pattern preview
            float localFrom = std::fmod(fromBeat, loopLength);
            float localTo = std::fmod(toBeat, loopLength);

            // Handle wrap-around
            if (localTo < localFrom) {
                processPatternNotes(pattern, localFrom, loopLength);
                processPatternNotes(pattern, 0.0f, localTo);
            } else {
                processPatternNotes(pattern, localFrom, localTo);
            }
        }
    }

    void processPatternNotes(const Pattern& pattern, float fromBeat, float toBeat) {
        for (const auto& note : pattern.notes) {
            // Apply swing to note start time
            float swungStart = applySwing(note.startTime);

            // Note on
            if (swungStart >= fromBeat && swungStart < toBeat) {
                // Convert fade times from beats to seconds
                float fadeInSec = beatsToSeconds(note.fadeIn);
                float fadeOutSec = beatsToSeconds(note.fadeOut);
                float durationSec = beatsToSeconds(note.duration);

                // Apply humanize
                float startTime = m_state.currentTime;
                float velocity = note.velocity;
                applyHumanize(startTime, velocity);

                m_synths[m_previewChannel].noteOn(
                    note.pitch, velocity, startTime,
                    fadeInSec, fadeOutSec, durationSec, note.oscillatorType,
                    note.vibrato, note.arpeggio, note.slide);
            }

            // Note off (also swing the end time)
            float noteEnd = applySwing(note.startTime) + note.duration;
            if (noteEnd >= fromBeat && noteEnd < toBeat) {
                m_synths[m_previewChannel].noteOff(note.pitch, m_state.currentTime);
            }
        }
    }

    // Convert beats to seconds based on current BPM
    float beatsToSeconds(float beats) const {
        if (!m_project || m_project->bpm <= 0.0f) return 0.0f;
        return beats * 60.0f / m_project->bpm;
    }

    // Apply swing to a beat position
    // Swing shifts off-beat notes forward in time (e.g., 8th note upbeats)
    float applySwing(float beat) const {
        if (!m_project || m_project->swing <= 0.0f) return beat;

        float grid = m_project->swingGrid;  // e.g., 0.5 for 8th notes
        float swing = m_project->swing;     // 0.0 to 1.0

        // Find position within the grid
        float gridPos = std::fmod(beat, grid * 2.0f);

        // Check if this is an off-beat (second half of the pair)
        if (gridPos >= grid - 0.001f && gridPos < grid * 2.0f - 0.001f) {
            // This is an off-beat - shift it forward
            // Maximum swing (1.0) creates triplet feel (shift by grid/3)
            float swingOffset = grid * swing * 0.333f;
            float basePos = std::floor(beat / grid) * grid;
            float offBeatStart = basePos + grid;
            return offBeatStart + swingOffset;
        }

        return beat;
    }

    // Apply humanize (random timing/velocity variation)
    void applyHumanize(float& startTime, float& velocity) const {
        if (!m_project || !m_project->humanize) return;

        // Add random timing variation
        float timeVariation = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
        startTime += timeVariation * m_project->humanizeAmount;

        // Add random velocity variation
        float velVariation = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
        velocity = std::max(0.1f, std::min(1.0f, velocity + velVariation * m_project->humanizeVelocity));
    }

    // Calculate when the last note in the pattern ends
    float getPatternEndTime() const {
        if (!m_project || m_previewPattern < 0 ||
            m_previewPattern >= static_cast<int>(m_project->patterns.size())) {
            return m_state.loopEnd;  // Fallback to fixed loop end
        }

        const Pattern& pattern = m_project->patterns[m_previewPattern];
        if (pattern.notes.empty()) {
            return 0.0f;  // No notes, end immediately
        }

        float maxEndTime = 0.0f;
        for (const Note& note : pattern.notes) {
            float noteEnd = note.startTime + note.duration;
            if (noteEnd > maxEndTime) {
                maxEndTime = noteEnd;
            }
        }
        return maxEndTime;
    }

public:
    void setPreviewPattern(int patternIndex, int channel) {
        m_previewPattern = patternIndex;
        m_previewChannel = channel;
    }

    void clearPreviewPattern() {
        m_previewPattern = -1;
    }

private:
    float m_sampleRate = 44100.0f;
    Project* m_project = nullptr;
    PlaybackState m_state;

    std::array<Synthesizer, MAX_CHANNELS> m_synths;

    // Pattern preview mode
    int m_previewPattern = -1;
    int m_previewChannel = 0;
};

} // namespace ChiptuneTracker
