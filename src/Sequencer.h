#pragma once

/*
 * ChiptuneTracker - Sequencer Module
 *
 * Handles pattern playback, timeline arrangement,
 * and real-time note scheduling.
 */

#include "Types.h"
#include "ChipMix.h"
#include "Routing.h"
#include "LoopRange.h"
#include "NoteEvents.h"
#include "Synthesizer.h"
#include "MasterEffects.h"
#include "SpectrumAnalyzer.h"
#include "MIDIInput.h"
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

        // Set up MIDI input callback
        m_midiInput.setNoteEventCallback([this](int pitch, float velocity, bool isNoteOn) {
            if (isNoteOn) {
                triggerNote(m_previewChannel, pitch, velocity);
            } else {
                releaseNote(m_previewChannel, pitch);
            }
        });
    }

    void setSampleRate(float sr) {
        m_sampleRate = sr;
        for (auto& synth : m_synths) {
            synth.setSampleRate(sr);
        }
        m_masterEffects.setSampleRate(sr);
        m_spectrumAnalyzer.setSampleRate(sr);
    }

    void setProject(Project* project) {
        m_project = project;
        updateChannelConfigs();
        updateMasterEffects();
    }

    // ========================================================================
    // Transport Controls
    // ========================================================================
    void play() {
        // Starting outside the loop range would run silently until the
        // playhead happened to wander in. Pressing play should always
        // audibly do something.
        if (m_state.loop) {
            m_state.currentBeat = clampStartBeat(m_state.currentBeat,
                                                 currentLoopWindow(), true);
            m_state.currentTime = beatToTime(m_state.currentBeat);
        } else {
            // Non-looping playback stops with the playhead parked exactly
            // at the end. Without this, pressing PLAY there set isPlaying
            // and the first audio block immediately stopped it again - the
            // user had to press STOP first, with nothing playing that STOP
            // could plausibly be for. Play at the end means from the top.
            const LoopWindow window = currentLoopWindow();
            if (window.valid && m_state.currentBeat >= window.end - 1e-4f) {
                m_state.currentBeat = 0.0f;
                m_state.currentTime = 0.0f;
            }
        }
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

    // A range drawn on the arrangement ruler. Anything shorter than
    // MIN_LOOP_BEATS is a mis-drag rather than an intent to loop, and would
    // spin the playhead at audio rate, so it does not take.
    void setLoopRange(float start, float end) {
        m_state.loopStart = start;
        m_state.loopEnd = end;
        m_state.loopRangeActive = (end - start) >= MIN_LOOP_BEATS;
    }

    void clearLoopRange() {
        m_state.loopRangeActive = false;
    }

    // Which time round the loop we are on. Probabilistic notes hash against
    // it, so they re-roll on every repeat rather than freezing into a fixed
    // pattern the first time through.
    uint32_t loopPass() const { return m_loopPass; }

    // The span playback actually repeats over: the user's range if they drew
    // one, otherwise the extent of the content, which is the old behaviour.
    LoopWindow currentLoopWindow() const {
        return resolveLoopWindow(m_state.loopRangeActive, m_state.loopStart,
                                 m_state.loopEnd, getPatternEndTime());
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

        // Console filters are reconfigured here, once per block - the
        // coefficients depend only on the sample rate and the chosen
        // voicing, neither of which changes between samples.
        const ChipFilterChain::Mode wantFilterMode = m_project->chipFilterFamicom
            ? ChipFilterChain::Mode::Famicom
            : ChipFilterChain::Mode::NES;
        if (!m_chipFilterReady || wantFilterMode != m_chipFilterMode) {
            m_chipFilter.configure(m_sampleRate, wantFilterMode);
            m_chipFilterMode = wantFilterMode;
            m_chipFilterReady = true;
        }

        for (uint32_t i = 0; i < frameCount; ++i) {
            float prevBeat = m_state.currentBeat;

            // Advance time if playing
            if (m_state.isPlaying) {
                m_state.currentBeat += beatsPerSample;
                m_state.currentTime += 1.0f / m_sampleRate;

                // The span to repeat: the user's range if they drew one on
                // the ruler, otherwise the extent of the content.
                const LoopWindow window = currentLoopWindow();

                if (window.valid && m_state.currentBeat >= window.end) {
                    if (m_state.loop) {
                        // wrapIntoWindow rather than a bare assignment: a long
                        // block, or a range shortened mid-playback, can leave
                        // the playhead more than one window past the end.
                        m_state.currentBeat = wrapIntoWindow(m_state.currentBeat, window);
                        // Every repeat re-rolls probabilistic notes.
                        ++m_loopPass;
                        allNotesOff();
                    } else {
                        // Stop playback when the last note ends
                        m_state.isPlaying = false;
                        m_state.currentBeat = window.end;
                        allNotesOff();
                    }
                }

                // Process note events that occurred in this sample
                processNoteEvents(prevBeat, m_state.currentBeat);
            }

            // Apply automation to parameters at current playback position
            applyAutomation(m_state.currentBeat);

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
                if (!fx.sidechainEnabled) continue;

                // A bus source wins when set; otherwise the legacy channel
                // tap, which is how every pre-v4 project works.
                //
                // The bus value is last sample's sum: sends are accumulated
                // during the channel loop below, and using this sample's
                // value would need a second pass over every channel. 23
                // microseconds at 44.1 kHz, against a real ordering problem.
                const int busSource = m_project->channels[ch].sidechainBus;
                if (busSource >= 0 && busSource < Project::MAX_AUX_BUSES) {
                    fx.sidechain.updateEnvelope(m_auxPrevious[static_cast<size_t>(busSource)]);
                    channelSamples[ch] = fx.sidechain.process(channelSamples[ch]);
                } else if (fx.sidechainSource >= 0 && fx.sidechainSource < MAX_CHANNELS) {
                    fx.sidechain.updateEnvelope(channelSamples[fx.sidechainSource]);
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

            // Non-linear channel mixing. A real 2A03 shares one DAC between
            // its two pulses and another between triangle, noise and DMC, so
            // channels duck each other and the triangle sits louder against
            // the pulses than a linear sum makes it. Off by default.
            ChipMixGains chipGains;
            if (m_project->chipMixEnabled) {
                float pulseMag = 0.0f;
                float triangleMag = 0.0f;
                float noiseMag = 0.0f;

                for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                    if (m_project->channels[ch].muted) continue;
                    if (hasSolo && !m_project->channels[ch].solo) continue;

                    const float mag = std::fabs(channelSamples[ch]) *
                                      m_project->channels[ch].volume;
                    switch (chipMixGroupFor(m_project->channels[ch].oscillator.type)) {
                        case ChipMixGroup::Pulse:    pulseMag += mag; break;
                        case ChipMixGroup::Triangle: triangleMag += mag; break;
                        case ChipMixGroup::Noise:    noiseMag += mag; break;
                        default: break;   // not a 2A03 voice; mixed linearly
                    }
                }
                chipGains = computeChipMixGains(pulseMag, triangleMag, noiseMag);
            }

            for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                if (m_project->channels[ch].muted) continue;
                if (hasSolo && !m_project->channels[ch].solo) continue;

                float sample = channelSamples[ch];
                float volume = m_project->channels[ch].volume;

                if (m_project->chipMixEnabled) {
                    switch (chipMixGroupFor(m_project->channels[ch].oscillator.type)) {
                        case ChipMixGroup::Pulse:
                            sample *= chipGains.pulse;
                            break;
                        case ChipMixGroup::Triangle:
                        case ChipMixGroup::Noise:
                            sample *= chipGains.tnd;
                            break;
                        default:
                            break;
                    }
                }
                float pan = m_project->channels[ch].pan;

                // Pan law (constant power)
                float leftGain = std::cos((pan + 1.0f) * 0.25f * PI) * volume;
                float rightGain = std::sin((pan + 1.0f) * 0.25f * PI) * volume;

                // The stereo widener is the one effect that cannot live in the
                // mono EffectsChain - it turns one sample into two. This is the
                // only point where a channel becomes stereo, so it belongs here.
                // Per-channel level for the mixer meters. A decaying peak
                // rather than an instantaneous value: at 44.1kHz the raw
                // sample is meaningless to the eye, and the UI reads this
                // at whatever rate it happens to redraw.
                {
                    const float magnitude = std::fabs(sample) * volume;
                    float& peak = m_channelPeaks[ch];
                    peak = (magnitude > peak) ? magnitude : peak * 0.9995f;
                }

                // Sends: a copy of this channel into a bus, at its own
                // level. Pre-fader takes it before the channel's volume, so
                // pulling the channel down leaves the send standing - which
                // is how a dry signal fades into its own reverb tail.
                for (int slot = 0; slot < MAX_SENDS_PER_CHANNEL; ++slot) {
                    const SendConfig& send =
                        m_project->channels[ch].sends[static_cast<size_t>(slot)];
                    if (send.destination < 0 ||
                        send.destination >= Project::MAX_AUX_BUSES) {
                        continue;
                    }
                    if (send.level <= 0.0f) continue;

                    const float tap = send.preFader ? sample : (sample * volume);
                    m_auxAccum[static_cast<size_t>(send.destination)] +=
                        tap * send.level;
                }

                auto& fx = m_synths[ch].effects();
                if (fx.stereoWidenerEnabled) {
                    auto [wideL, wideR] = fx.stereoWidener.process(sample);
                    left += wideL * leftGain;
                    right += wideR * rightGain;
                } else {
                    left += sample * leftGain;
                    right += sample * rightGain;
                }
            }

            // ---- Aux buses --------------------------------------------------
            //
            // Walked in the precomputed order, so a bus always runs after
            // everything feeding it. No graph traversal here.
            for (int slot = 0; slot < m_busOrderCount; ++slot) {
                const int bus = m_busOrder[static_cast<size_t>(slot)];
                if (bus < 0 || bus >= Project::MAX_AUX_BUSES) continue;

                const AuxBusConfig& config =
                    m_project->auxBuses[static_cast<size_t>(bus)];

                float busSample = m_auxAccum[static_cast<size_t>(bus)];
                busSample = m_auxChains[static_cast<size_t>(bus)].process(busSample,
                                                                          m_state.currentTime);
                if (config.muted) busSample = 0.0f;
                busSample *= config.volume;

                if (config.output >= 0 && config.output < Project::MAX_AUX_BUSES &&
                    config.output != bus) {
                    m_auxAccum[static_cast<size_t>(config.output)] += busSample;
                } else {
                    const float busPan = config.pan;
                    left += busSample * std::cos((busPan + 1.0f) * 0.25f * PI);
                    right += busSample * std::sin((busPan + 1.0f) * 0.25f * PI);
                }
            }

            // Kept for next sample's bus sidechaining, then cleared.
            m_auxPrevious = m_auxAccum;
            m_auxAccum.fill(0.0f);

            // The console's own output filters, applied before the master
            // bus: on hardware these are the last thing between the APU and
            // the RF modulator, and our master bus is the studio processing
            // that would come after it.
            if (m_project->chipFilterEnabled) {
                m_chipFilter.process(left, right);
            }

            // Apply master volume
            float master = m_project->masterVolume;
            left *= master;
            right *= master;

            // Apply master bus effects (EQ, Compressor, Limiter)
            m_masterEffects.process(left, right);

            // Soft clip (in case limiter is disabled)
            left = std::tanh(left);
            right = std::tanh(right);

            // Feed to spectrum analyzer for visualization
            m_spectrumAnalyzer.process(left, right);

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
    // Current output level of one channel, for the mixer meters.
    // Written by the audio thread and read by the UI; a float read that is
    // one frame stale is exactly as good for a meter as a locked one.
    float getChannelLevel(int channel) const {
        if (channel < 0 || channel >= MAX_CHANNELS) return 0.0f;
        const float level = m_channelPeaks[channel];
        return std::isfinite(level) ? level : 0.0f;
    }

    Synthesizer& getSynth(int channel) {
        return m_synths[channel % MAX_CHANNELS];
    }

    // ========================================================================
    // Master Effects Access
    // ========================================================================
    MasterEffects& getMasterEffects() {
        return m_masterEffects;
    }

    // ========================================================================
    // Spectrum Analyzer Access
    // ========================================================================
    SpectrumAnalyzer& getSpectrumAnalyzer() {
        return m_spectrumAnalyzer;
    }

    // ========================================================================
    // MIDI Input Access
    // ========================================================================
    MIDIInput& getMIDIInput() {
        return m_midiInput;
    }

    // Read-only view of a channel's live insert rack, so a test can assert
    // what the audio thread is actually about to run rather than what the
    // config claims it should be.
    const EffectsChain& channelEffects(int channel) const {
        const int index = (channel >= 0 && channel < MAX_CHANNELS) ? channel : 0;
        return m_synths[static_cast<size_t>(index)].effects();
    }

    void updateChannelConfigs() {
        if (!m_project) return;

        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            const auto& config = m_project->channels[ch];
            // Single sync point: oscillator, envelope, filter envelope and the
            // full per-channel effects chain (see Synthesizer::setChannelConfig).
            m_synths[ch].setChannelConfig(config);
        }

        // Aux buses. Their strips are ChannelConfigs, so the same rack sync
        // the channels use configures them - one code path, not two.
        for (int bus = 0; bus < Project::MAX_AUX_BUSES; ++bus) {
            applyEffectsConfig(m_project->auxBuses[static_cast<size_t>(bus)].strip,
                               m_auxChains[static_cast<size_t>(bus)]);
            m_auxChains[static_cast<size_t>(bus)].setSampleRate(m_sampleRate);
        }

        // Resolve the routing graph here, on the UI thread, so the audio
        // thread only ever walks a precomputed order. A file carrying a
        // loop is repaired rather than trusted.
        if (!computeBusOrder(m_project->auxBuses, m_busOrder.data(), m_busOrderCount)) {
            breakRoutingCycles(m_project->auxBuses);
            computeBusOrder(m_project->auxBuses, m_busOrder.data(), m_busOrderCount);
        }
    }

    void updateMasterEffects() {
        if (!m_project) return;

        // Master EQ settings
        m_masterEffects.eqEnabled = m_project->masterEQEnabled;
        // The Project stores these in decibels, which is what the UI shows.
        // ThreeBandEQ and Compressor want linear gain, where 1.0 is unity.
        // Handing them the dB value meant enabling the master EQ at its
        // default of 0 dB multiplied the whole mix by zero and silenced the
        // song. Limiter::ceiling is the exception - it converts dB itself.
        auto dbToLinear = [](float db) {
            if (!std::isfinite(db)) return 1.0f;
            return std::pow(10.0f, db / 20.0f);
        };

        m_masterEffects.eq.lowGain = dbToLinear(m_project->masterEQLowGain);
        m_masterEffects.eq.midGain = dbToLinear(m_project->masterEQMidGain);
        m_masterEffects.eq.highGain = dbToLinear(m_project->masterEQHighGain);

        // Master Compressor settings
        m_masterEffects.compressorEnabled = m_project->masterCompressorEnabled;
        m_masterEffects.compressor.threshold = dbToLinear(m_project->masterCompThreshold);
        m_masterEffects.compressor.ratio = m_project->masterCompRatio;
        m_masterEffects.compressor.attack = m_project->masterCompAttack;
        m_masterEffects.compressor.release = m_project->masterCompRelease;
        m_masterEffects.compressor.makeupGain = dbToLinear(m_project->masterCompMakeup);

        // Master Limiter settings
        m_masterEffects.limiterEnabled = m_project->masterLimiterEnabled;
        m_masterEffects.limiter.ceiling = m_project->masterLimiterCeiling;
        m_masterEffects.limiter.release = m_project->masterLimiterRelease;
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

            // The channel index was not checked, and it indexes an array of
            // eight 35KB synths - so a clip naming channel 99 wrote megabytes
            // past the end of the array from the audio thread. Loading a file
            // drops such clips, but a project built in memory never passes
            // through that, so the check belongs here as well.
            if (clip.channelIndex < 0 || clip.channelIndex >= MAX_CHANNELS) {
                continue;
            }

            const auto& pattern = m_project->patterns[clip.patternIndex];

            // Check if this clip is active in current beat range.
            //
            // The tail margin lets a note's echo repeats ring out past the
            // clip boundary instead of being cut off by the gate below; each
            // trigger still checks its own start and end, so widening this
            // cannot make anything sound early.
            constexpr float CLIP_TAIL_MARGIN = 8.0f;
            float clipEnd = clip.startBeat + clip.lengthBeats;
            if (toBeat < clip.startBeat || fromBeat > clipEnd + CLIP_TAIL_MARGIN) {
                continue;
            }

            // Process notes in this pattern
            for (const auto& note : pattern.notes) {
                // One note is not necessarily one sound: note delay, note cut,
                // retrigger and echo expand it into a list of hits.
                NoteTrigger triggers[MAX_NOTE_TRIGGERS];
                const int triggerCount =
                    expandNote(note, clip.startBeat, triggers, MAX_NOTE_TRIGGERS);

                // Rolled once for the whole note rather than per hit, so a
                // retriggered note either stutters or stays silent, never half.
                if (!noteShouldSound(note, clip.startBeat + note.startTime,
                                     m_loopPass)) {
                    continue;
                }

                // The placement's transpose, applied identically at note-on
                // and note-off - if they disagreed a note would never stop.
                // A pitch pushed off the ends of MIDI is skipped, not
                // wrapped: a wrapped bass note ten octaves up is a far
                // stranger bug to hear than a missing one.
                const int soundingPitch = note.pitch + clip.transpose;
                if (soundingPitch < 0 || soundingPitch > 127) continue;

                for (int t = 0; t < triggerCount; ++t) {
                    const NoteTrigger& trigger = triggers[t];

                    // Swing, which the arrangement path never applied: the
                    // slider moved a value only the pattern preview read, so
                    // a song played from the timeline was always straight.
                    // applySwing is periodic in the grid, so absolute beats
                    // are fine. The hit moves; its length does not, or a
                    // swung note would also be a shorter one.
                    const float swungStart = applySwing(trigger.startBeat);
                    const float swungEnd = swungStart +
                        (trigger.endBeat - trigger.startBeat);

                    // Note on
                    if (swungStart >= fromBeat && swungStart < toBeat) {
                        // Convert fade times from beats to seconds
                        float fadeInSec = beatsToSeconds(note.fadeIn);
                        float fadeOutSec = beatsToSeconds(note.fadeOut);
                        float durationSec =
                            beatsToSeconds(trigger.endBeat - trigger.startBeat);

                        // Humanize was preview-only for the same reason.
                        float startTime = m_state.currentTime;
                        float velocity = trigger.velocity;
                        applyHumanize(startTime, velocity);

                        m_synths[clip.channelIndex].noteOn(
                            soundingPitch, velocity, startTime,
                            fadeInSec, fadeOutSec, durationSec, note.oscillatorType,
                            note.vibrato, note.arpeggio, note.slide,
                            note.dutyCycle, note.useDutyCycle,
                            note.sweepDirection, note.sweepSpeed, note.sweepAmount,
                            note.tremolo, note.tremoloSpeed);
                    }

                    // Note off
                    if (swungEnd >= fromBeat && swungEnd < toBeat) {
                        m_synths[clip.channelIndex].noteOff(
                            soundingPitch, m_state.currentTime);
                    }
                }
            }
        }

        // Also check pattern preview (current selected pattern, not on timeline)
        if (m_previewPattern >= 0 &&
            m_previewPattern < static_cast<int>(m_project->patterns.size())) {

            const auto& pattern = m_project->patterns[m_previewPattern];

            // process() already folds currentBeat into the loop window, so
            // the beat handed here is always inside it. The old fmod here
            // assumed the window started at 0 and would misplace every note
            // once a user range began anywhere else.
            const LoopWindow window = currentLoopWindow();
            const float localFrom = fromBeat;
            const float localTo = toBeat;

            // A wrap happened between these two samples: play the tail of the
            // window and then the head, so nothing is dropped at the seam.
            if (localTo < localFrom) {
                processPatternNotes(pattern, localFrom,
                                    window.valid ? window.end : localFrom);
                processPatternNotes(pattern,
                                    window.valid ? window.start : 0.0f, localTo);
            } else {
                processPatternNotes(pattern, localFrom, localTo);
            }
        }
    }

    void processPatternNotes(const Pattern& pattern, float fromBeat, float toBeat) {
        for (const auto& note : pattern.notes) {
            if (!noteShouldSound(note, note.startTime, m_loopPass)) continue;

            NoteTrigger triggers[MAX_NOTE_TRIGGERS];
            const int triggerCount =
                expandNote(note, 0.0f, triggers, MAX_NOTE_TRIGGERS);

            for (int t = 0; t < triggerCount; ++t) {
                const NoteTrigger& trigger = triggers[t];

                // Swing displaces the hit, but must not change how long it
                // lasts, or a swung note would also be a shorter one.
                const float swungStart = applySwing(trigger.startBeat);
                const float held = trigger.endBeat - trigger.startBeat;

                // Note on
                if (swungStart >= fromBeat && swungStart < toBeat) {
                    // Convert fade times from beats to seconds
                    float fadeInSec = beatsToSeconds(note.fadeIn);
                    float fadeOutSec = beatsToSeconds(note.fadeOut);
                    float durationSec = beatsToSeconds(held);

                    // Apply humanize
                    float startTime = m_state.currentTime;
                    float velocity = trigger.velocity;
                    applyHumanize(startTime, velocity);

                    m_synths[m_previewChannel].noteOn(
                        note.pitch, velocity, startTime,
                        fadeInSec, fadeOutSec, durationSec, note.oscillatorType,
                        note.vibrato, note.arpeggio, note.slide,
                        note.dutyCycle, note.useDutyCycle,
                        note.sweepDirection, note.sweepSpeed, note.sweepAmount,
                        note.tremolo, note.tremoloSpeed);
                }

                // Note off (also swing the end time)
                const float swungEnd = swungStart + held;
                if (swungEnd >= fromBeat && swungEnd < toBeat) {
                    m_synths[m_previewChannel].noteOff(note.pitch, m_state.currentTime);
                }
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

    // ========================================================================
    // Automation Processing
    // ========================================================================

    // Apply automation curves to parameters at current beat
    void applyAutomation(float currentBeat) {
        if (!m_project) return;

        for (const auto& lane : m_project->automationLanes) {
            if (!lane.enabled || lane.curve.points.empty()) continue;

            // Evaluate automation curve at current beat (returns 0.0-1.0)
            float normalizedValue = lane.curve.evaluate(currentBeat);

            // Apply to appropriate parameter
            applyAutomationValue(lane.channelIndex, lane.target, normalizedValue);
        }
    }

    // Map normalized value (0-1) to actual parameter value and apply it
    void applyAutomationValue(int channelIndex, AutomationTarget target, float normalizedValue) {
        if (!m_project) return;

        // Master channel parameters (channelIndex == -1)
        if (channelIndex < 0) {
            switch (target) {
                case AutomationTarget::MasterVolume:
                    m_project->masterVolume = normalizedValue;
                    break;
                case AutomationTarget::MasterEQLow:
                    m_project->masterEQLowGain = normalizedValue * 24.0f - 12.0f; // -12 to +12 dB
                    break;
                case AutomationTarget::MasterEQMid:
                    m_project->masterEQMidGain = normalizedValue * 24.0f - 12.0f;
                    break;
                case AutomationTarget::MasterEQHigh:
                    m_project->masterEQHighGain = normalizedValue * 24.0f - 12.0f;
                    break;
                case AutomationTarget::MasterCompThreshold:
                    m_project->masterCompThreshold = normalizedValue * 48.0f - 48.0f; // -48 to 0 dB
                    break;
                case AutomationTarget::MasterLimiterCeiling:
                    m_project->masterLimiterCeiling = normalizedValue * 12.0f - 12.0f; // -12 to 0 dB
                    break;
                default:
                    break;
            }
            // Update master effects with new values
            updateMasterEffects();
            return;
        }

        // Channel parameters
        if (channelIndex < 0 || channelIndex >= MAX_CHANNELS) return;
        auto& channel = m_project->channels[channelIndex];

        switch (target) {
            case AutomationTarget::Volume:
                channel.volume = normalizedValue;
                break;

            case AutomationTarget::Pan:
                channel.pan = normalizedValue * 2.0f - 1.0f; // -1.0 to +1.0
                break;

            case AutomationTarget::FilterCutoff:
                channel.filterCutoff = 20.0f + normalizedValue * 19980.0f; // 20 Hz to 20 kHz (log scale would be better)
                break;

            case AutomationTarget::FilterResonance:
                channel.filterResonance = normalizedValue; // 0.0 to 1.0
                break;

            case AutomationTarget::ReverbMix:
                channel.reverbMix = normalizedValue;
                break;

            case AutomationTarget::DelayMix:
                channel.delayMix = normalizedValue;
                break;

            case AutomationTarget::ChorusMix:
                channel.chorusMix = normalizedValue;
                break;

            case AutomationTarget::DistortionDrive:
                channel.distortionDrive = 1.0f + normalizedValue * 9.0f; // 1.0 to 10.0
                break;

            case AutomationTarget::BitcrusherDepth:
                channel.bitDepth = 1.0f + normalizedValue * 15.0f; // 1 to 16 bits
                break;

            case AutomationTarget::PhaserRate:
                channel.phaserRate = normalizedValue * 10.0f; // 0.0 to 10.0 Hz
                break;

            case AutomationTarget::FlangerRate:
                channel.flangerRate = 0.1f + normalizedValue * 9.9f; // 0.1 to 10.0 Hz
                break;

            case AutomationTarget::TremoloRate:
                channel.tremoloRate = normalizedValue * 20.0f; // 0.0 to 20.0 Hz
                break;

            case AutomationTarget::CompressorThreshold:
                channel.compThreshold = normalizedValue; // 0.0 to 1.0
                break;

            case AutomationTarget::EQLow:
                channel.eqLow = normalizedValue * 2.0f; // 0.0 to 2.0 (gain multiplier)
                break;

            case AutomationTarget::EQMid:
                channel.eqMid = normalizedValue * 2.0f;
                break;

            case AutomationTarget::EQHigh:
                channel.eqHigh = normalizedValue * 2.0f;
                break;

            case AutomationTarget::StereoWidth:
                channel.stereoWidenerWidth = normalizedValue;
                break;

            case AutomationTarget::TapeDrive:
                channel.tapeDrive = 1.0f + normalizedValue * 4.0f; // 1.0 to 5.0
                break;

            default:
                break;
        }

        // Sync channel config to synthesizer effects
        auto& fx = m_synths[channelIndex].effects();
        fx.filter.cutoff = channel.filterCutoff;
        fx.filter.resonance = channel.filterResonance;
        fx.bitcrusher.bitDepth = channel.bitDepth;
        fx.distortion.drive = channel.distortionDrive;
        fx.reverb.mix = channel.reverbMix;
        fx.delay.mix = channel.delayMix;
        fx.chorus.mix = channel.chorusMix;
        fx.phaser.rate = channel.phaserRate;
        fx.flanger.rate = channel.flangerRate;
        fx.tremolo.rate = channel.tremoloRate;
        fx.compressor.threshold = channel.compThreshold;
        fx.eq.lowGain = channel.eqLow;
        fx.eq.midGain = channel.eqMid;
        fx.eq.highGain = channel.eqHigh;
        fx.stereoWidener.width = channel.stereoWidenerWidth;
        fx.tapeSaturation.drive = channel.tapeDrive;
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
    uint32_t m_loopPass = 0;

    // ---- Aux buses -------------------------------------------------------
    //
    // The chains are full EffectsChains, so a bus gets the whole Task A rack.
    // The order is resolved when configs change, never in the callback.
    std::array<EffectsChain, Project::MAX_AUX_BUSES> m_auxChains;
    std::array<float, Project::MAX_AUX_BUSES> m_auxAccum{};
    // Last sample's bus sums, for sidechaining off a bus without needing a
    // second pass over every channel.
    std::array<float, Project::MAX_AUX_BUSES> m_auxPrevious{};
    std::array<int, Project::MAX_AUX_BUSES> m_busOrder{};
    int m_busOrderCount = 0;

    ChipFilterChain m_chipFilter;
    ChipFilterChain::Mode m_chipFilterMode = ChipFilterChain::Mode::NES;
    bool m_chipFilterReady = false;

    std::array<Synthesizer, MAX_CHANNELS> m_synths;

    // Decaying peak level per channel, for the mixer meters
    std::array<float, MAX_CHANNELS> m_channelPeaks = {};

    // Master bus effects (post-mixer processing)
    MasterEffects m_masterEffects;

    // Spectrum analyzer (frequency visualization)
    SpectrumAnalyzer m_spectrumAnalyzer;

    // MIDI input (keyboard input and recording)
    MIDIInput m_midiInput;

    // Pattern preview mode
    int m_previewPattern = -1;
    int m_previewChannel = 0;
};

} // namespace ChiptuneTracker
