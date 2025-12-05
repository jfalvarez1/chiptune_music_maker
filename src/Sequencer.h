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

                // Handle looping
                if (m_state.loop && m_state.currentBeat >= m_state.loopEnd) {
                    m_state.currentBeat = m_state.loopStart;
                    allNotesOff();
                }

                // Process note events that occurred in this sample
                processNoteEvents(prevBeat, m_state.currentBeat);
            }

            // Mix all channels
            float left = 0.0f;
            float right = 0.0f;

            for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                if (m_project->channels[ch].muted) continue;

                // Check solo
                bool hasSolo = false;
                for (int c = 0; c < MAX_CHANNELS; ++c) {
                    if (m_project->channels[c].solo) {
                        hasSolo = true;
                        break;
                    }
                }
                if (hasSolo && !m_project->channels[ch].solo) continue;

                float sample = m_synths[ch].process(m_state.currentTime);
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
                        fadeInSec, fadeOutSec, durationSec);
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
            // Note on
            if (note.startTime >= fromBeat && note.startTime < toBeat) {
                // Convert fade times from beats to seconds
                float fadeInSec = beatsToSeconds(note.fadeIn);
                float fadeOutSec = beatsToSeconds(note.fadeOut);
                float durationSec = beatsToSeconds(note.duration);

                m_synths[m_previewChannel].noteOn(
                    note.pitch, note.velocity, m_state.currentTime,
                    fadeInSec, fadeOutSec, durationSec);
            }

            // Note off
            float noteEnd = note.startTime + note.duration;
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
