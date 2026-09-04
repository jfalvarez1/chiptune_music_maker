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
#include "PluginHost.h"
#include "DelayCompensation.h"
#include "Scopes.h"
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
    // Must agree with Project's, or the two would disagree about how many
    // synths exist and the mix would walk off the end of one of them.
    static constexpr int MAX_CHANNELS = Project::MAX_CHANNELS;
    static_assert(MAX_CHANNELS == Project::MAX_CHANNELS,
                  "the sequencer and the project must agree on the channel count");

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

        /*
         * The beat advance follows the tempo map.
         *
         * This used to be one division per block from a single project bpm.
         * With a tempo change in the song that is wrong from the change
         * onward, and wrong by more the longer it runs.
         *
         * The lookup is hoisted out of the per-sample loop when the map is
         * flat, which is every project that has no tempo changes - so the
         * common case costs exactly what it did before. When the map is not
         * flat the rate is recomputed per sample, which is a scan of at most
         * 64 floats and still allocates nothing.
         */
        const bool flatTempo = m_project->tempoMap.isFlat();
        const float baseBpm = m_project->bpm;
        float bpm = flatTempo ? baseBpm
                              : m_project->tempoMap.bpmAtBeat(m_state.currentBeat, baseBpm);
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
                if (!flatTempo) {
                    bpm = m_project->tempoMap.bpmAtBeat(m_state.currentBeat, baseBpm);
                    beatsPerSample = bpm / 60.0f / m_sampleRate;
                }
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

            // Chip-authentic mode stops the mix at eight channels, so it
            // is a real constraint rather than a UI hint - and the CPU that
            // 24 unused channels would cost is not spent either.
            const int activeChannels = m_project->activeChannelCount();

            // Pass 1: Generate all channel samples (pre-sidechain)
            std::array<float, MAX_CHANNELS> channelSamples = {};
            for (int ch = 0; ch < activeChannels; ++ch) {
                channelSamples[ch] = m_synths[ch].process(m_state.currentTime);

                /*
                 * Hosted plugins, after the channel's own voice.
                 *
                 * active() is false unless something actually loaded, so a
                 * project with no plugins - which is every project in a
                 * build with no format loader - pays one predictable branch
                 * per channel and nothing else.
                 *
                 * The chain buffers to its block size internally and
                 * therefore delays this channel; see PluginChain, which
                 * reports that latency rather than hiding it.
                 */
                if (m_pluginChains[ch].active()) {
                    channelSamples[ch] =
                        m_pluginChains[ch].processSample(channelSamples[ch]);
                }

                /*
                 * Delay compensation.
                 *
                 * Some effects cannot produce output at the instant they
                 * receive input - a convolution has to fill a block, a
                 * phase vocoder needs a whole window - so the channels
                 * carrying them come out late. Every other channel waits
                 * the difference, so the song is late as a whole by a fixed
                 * and inaudible amount and internally in time.
                 *
                 * Free when nothing has latency: the delay line returns its
                 * input directly at zero.
                 */
                channelSamples[ch] = m_compensation[ch].process(channelSamples[ch]);

                /*
                 * The channel oscilloscope, taken here.
                 *
                 * After the channel is finished with and before the mixer
                 * touches it, so what the scope shows is the channel - not
                 * the channel times its fader, and not the channel after
                 * whatever the pan law did to it.
                 */
                m_scopes[ch].write(channelSamples[ch]);
            }

            // Audio clips join their channel here, before the insert rack -
            // so a recorded part gets the channel's effects, volume, pan and
            // sends exactly like a played one, with no separate routing.
            if (m_state.isPlaying) {
                mixAudioClips(channelSamples, m_state.currentBeat, activeChannels);
            }

            // Pass 2: Update sidechain envelopes and apply sidechain compression
            for (int ch = 0; ch < activeChannels; ++ch) {
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
            for (int c = 0; c < activeChannels; ++c) {
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

                for (int ch = 0; ch < activeChannels; ++ch) {
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

            for (int ch = 0; ch < activeChannels; ++ch) {
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

                /*
                 * The direct path waits for the buses.
                 *
                 * After the send tap and not before it, which is the whole
                 * subtlety: the copy going to a bus still has the bus's own
                 * processing ahead of it, so it must leave here early. What
                 * goes straight to the master has nothing left to do and has
                 * to wait for the copy to come back, or the two blend a few
                 * milliseconds apart and comb-filter each other.
                 *
                 * Zero, and therefore free, unless some bus actually has
                 * latency on it.
                 */
                sample = m_directDelay[ch].process(sample);

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

                /*
                 * Two arrivals, held apart on purpose.
                 *
                 * m_auxAccum carries the copies sent here by channels, all
                 * equally old. m_auxBusAccum carries what other buses have
                 * already finished with, which is older still. Delaying the
                 * bus input as a whole would push both back and leave the
                 * one that was already right too late; only the channel
                 * sends wait.
                 */
                const size_t b = static_cast<size_t>(bus);
                float busSample = m_busSendDelay[b].process(m_auxAccum[b]) +
                                  m_auxBusAccum[b];
                m_auxCombined[b] = busSample;

                busSample = m_auxChains[b].process(busSample, m_state.currentTime);
                if (config.muted) busSample = 0.0f;
                busSample *= config.volume;

                // And the bus's output waits for whatever it is about to be
                // mixed with, wherever that is.
                busSample = m_busOutDelay[b].process(busSample);

                if (config.output >= 0 && config.output < Project::MAX_AUX_BUSES &&
                    config.output != bus) {
                    m_auxBusAccum[static_cast<size_t>(config.output)] += busSample;
                } else {
                    const float busPan = config.pan;
                    left += busSample * std::cos((busPan + 1.0f) * 0.25f * PI);
                    right += busSample * std::sin((busPan + 1.0f) * 0.25f * PI);
                }
            }

            // Kept for next sample's bus sidechaining, then cleared. The
            // COMBINED input, so ducking off a bus follows what the bus
            // actually heard rather than only the part of it that arrived
            // by send.
            m_auxPrevious = m_auxCombined;
            m_auxAccum.fill(0.0f);
            m_auxBusAccum.fill(0.0f);
            m_auxCombined.fill(0.0f);

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

            /*
             * A safety net, and only that.
             *
             * This used to run unconditionally, after the limiter, on every
             * project. tanh(0.966) is 0.747, so a limiter set to a -0.3 dB
             * ceiling actually delivered -2.5 dBFS and the top two decibels
             * of everything were soft-clipped a second time. It also made
             * both of the tests that check for clipping vacuous, because
             * |tanh(x)| < 1 for every finite x - they could not fail no
             * matter what the master chain did.
             *
             * The limiter's job is the ceiling. This is what catches a
             * project with the limiter switched off, and it stays out of the
             * way of one where it is on.
             */
            if (!m_masterEffects.limiterEnabled) {
                left = std::tanh(left);
                right = std::tanh(right);
            } else {
                // Even so, nothing leaves here outside the representable
                // range: a NaN from anywhere upstream must not reach a
                // sound card.
                if (!std::isfinite(left)) left = 0.0f;
                if (!std::isfinite(right)) right = 0.0f;
                left = std::clamp(left, -1.0f, 1.0f);
                right = std::clamp(right, -1.0f, 1.0f);
            }

            // Feed to spectrum analyzer for visualization
            m_spectrumAnalyzer.process(left, right);

            // And the master scope. Both sides separately, because the one
            // question an X-Y plot answers is whether they agree - a mix
            // that has gone out of phase is a straight diagonal, and it
            // reads as normal on every other display in the program.
            m_masterScopeLeft.write(left);
            m_masterScopeRight.write(right);

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
    /*
     * Rebuild every channel's plugin chain from the project. UI thread only.
     *
     * Called when a project is loaded and when a plugin is added or removed
     * - never from the audio callback, which must not open a file.
     *
     * Reports which slots failed, so the UI can say a plugin is missing
     * rather than showing an empty rack and letting the user conclude their
     * settings are gone.
     */
    void rebuildPluginChains(PluginManager& manager,
                             std::vector<std::string>* problemsOut = nullptr) {
        if (problemsOut != nullptr) problemsOut->clear();
        if (m_project == nullptr) return;

        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            const ChannelConfig& config = m_project->channels[static_cast<size_t>(ch)];

            if (config.plugins.empty()) {
                m_pluginChains[ch].clear();
                continue;
            }

            m_pluginChains[ch].setSampleRate(m_sampleRate);

            std::vector<PluginLoadError> errors;
            m_pluginChains[ch].build(config.plugins, manager, &errors);

            if (problemsOut == nullptr) continue;
            for (size_t i = 0; i < errors.size(); ++i) {
                if (errors[i] == PluginLoadError::Ok) continue;
                problemsOut->push_back(
                    config.name + ": " +
                    (i < config.plugins.size() ? config.plugins[i].name
                                               : std::string("plugin")) +
                    " - " + pluginLoadErrorText(errors[i]));
            }
        }

        // A plugin is the largest single source of latency in the program, so
        // this is the call site that matters most.
        updateDelayCompensation();
    }

    PluginChain& pluginChain(int channel) {
        return m_pluginChains[static_cast<size_t>(
            std::clamp(channel, 0, MAX_CHANNELS - 1))];
    }

    /*
     * The largest plugin delay on any channel.
     *
     * Reported so the UI can say so. Nothing compensates for it yet - the
     * mixer has no delay line on the channels that do not have plugins -
     * and claiming otherwise would be worse than saying plainly that a
     * channel with an insert runs a block late.
     */
    /*
     * Recompute the compensating delays. UI thread.
     *
     * Called whenever the effects or the plugins change, which is the only
     * time a channel's latency can move. Doing it per sample would mean
     * summing a rack in the audio callback for no benefit, since the answer
     * cannot change between one sample and the next.
     */
    void updateDelayCompensation() {
        std::vector<int> latencies(MAX_CHANNELS, 0);

        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            int total = m_synths[ch].effects().latencySamples();
            if (m_pluginChains[ch].active()) {
                total += m_pluginChains[ch].latencySamples();
            }
            latencies[static_cast<size_t>(ch)] = total;
            m_channelLatency[static_cast<size_t>(ch)] = total;
        }

        std::vector<int> delays;
        computeCompensation(latencies, delays);

        int channelLevel = 0;
        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            m_compensation[ch].setDelay(delays[static_cast<size_t>(ch)]);
            channelLevel = std::max(channelLevel,
                                    delays[static_cast<size_t>(ch)] +
                                        m_channelLatency[static_cast<size_t>(ch)]);
        }

        updateBusCompensation(channelLevel);
    }

    /*
     * Level the send graph on top of the levelled channels.
     *
     * Separate from the channel pass because it depends on its result: every
     * channel is `channelLevel` samples old by the time a send is taken from
     * it, and that is the number the bus arithmetic starts from.
     */
    void updateBusCompensation(int channelLevel) {
        BusGraph graph;
        graph.channelLatency = channelLevel;
        graph.latency.assign(Project::MAX_AUX_BUSES, 0);
        graph.output.assign(Project::MAX_AUX_BUSES, -1);
        graph.receivesSend.assign(Project::MAX_AUX_BUSES, 0);

        if (m_project != nullptr) {
            for (int bus = 0; bus < Project::MAX_AUX_BUSES; ++bus) {
                const size_t b = static_cast<size_t>(bus);
                graph.latency[b] = m_auxChains[b].latencySamples();
                graph.output[b] = m_project->auxBuses[b].output;
            }

            for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                const ChannelConfig& config =
                    m_project->channels[static_cast<size_t>(ch)];
                for (int slot = 0; slot < MAX_SENDS_PER_CHANNEL; ++slot) {
                    const SendConfig& send =
                        config.sends[static_cast<size_t>(slot)];
                    if (send.destination < 0 ||
                        send.destination >= Project::MAX_AUX_BUSES) {
                        continue;
                    }
                    if (send.level <= 0.0f) continue;
                    graph.receivesSend[static_cast<size_t>(send.destination)] = 1;
                }
            }
        }

        // The mixer's own walk, which is already topological.
        graph.order.assign(m_busOrder.begin(),
                           m_busOrder.begin() + std::clamp(m_busOrderCount, 0,
                                                           Project::MAX_AUX_BUSES));

        BusCompensation compensation;
        computeBusCompensation(graph, compensation);

        for (int bus = 0; bus < Project::MAX_AUX_BUSES; ++bus) {
            const size_t b = static_cast<size_t>(bus);
            m_busSendDelay[b].setDelay(compensation.sendInput[b]);
            m_busOutDelay[b].setDelay(compensation.busOutput[b]);
        }
        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            m_directDelay[ch].setDelay(compensation.direct);
        }
        m_mixLatency = compensation.total;
    }

    // What the whole mix is delayed by, for anything that needs to know: the
    // length of the longest path from any channel to the master, which every
    // other path has been brought out to meet.
    int compensatedLatencySamples() const { return m_mixLatency; }

    // The delays that were actually installed, so a test can add up a path
    // through the graph and assert every path is the same length. Reporting
    // the intent is not the same as reporting what got written.
    int directDelaySamples() const { return m_directDelay[0].delay(); }

    int busSendDelaySamples(int bus) const {
        const int index = std::clamp(bus, 0, Project::MAX_AUX_BUSES - 1);
        return m_busSendDelay[static_cast<size_t>(index)].delay();
    }

    int busOutputDelaySamples(int bus) const {
        const int index = std::clamp(bus, 0, Project::MAX_AUX_BUSES - 1);
        return m_busOutDelay[static_cast<size_t>(index)].delay();
    }

    int busLatencySamples(int bus) {
        const int index = std::clamp(bus, 0, Project::MAX_AUX_BUSES - 1);
        return m_auxChains[static_cast<size_t>(index)].latencySamples();
    }

    // Needed to turn a sample count into milliseconds anywhere the number is
    // shown to a person, who thinks in milliseconds and not in samples.
    float sampleRate() const { return m_sampleRate; }

    // ---- Oscilloscopes ---------------------------------------------------
    const ScopeBuffer& channelScope(int channel) const {
        const int index = std::clamp(channel, 0, MAX_CHANNELS - 1);
        return m_scopes[static_cast<size_t>(index)];
    }
    const ScopeBuffer& masterScopeLeft() const { return m_masterScopeLeft; }
    const ScopeBuffer& masterScopeRight() const { return m_masterScopeRight; }

    /*
     * What is sounding on a channel, so a scope can size its window to it.
     *
     * A fixed window shows one cycle of a lead and eight of a bass, and the
     * bass is then an unreadable blur. Asking the synth what it is playing
     * costs nothing and makes both legible.
     */
    float channelFrequency(int channel) const {
        const int index = std::clamp(channel, 0, MAX_CHANNELS - 1);
        return m_synths[static_cast<size_t>(index)].loudestVoiceFrequency();
    }

    int channelLatencySamples(int channel) const {
        const int index = std::clamp(channel, 0, MAX_CHANNELS - 1);
        return m_channelLatency[static_cast<size_t>(index)];
    }

    int maxPluginLatencySamples() const {
        int worst = 0;
        for (const PluginChain& chain : m_pluginChains) {
            worst = std::max(worst, chain.latencySamples());
        }
        return worst;
    }

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

    /*
     * Rebuild the band-limited wavetables.
     *
     * Separate from updateChannelConfigs because it is expensive - ten FFTs
     * per table - and the banks change far less often than a fader does.
     * The UI calls it when a table is edited; updateChannelConfigs calls it
     * once so that loading a project never leaves a channel pointing at a
     * library that was never built.
     */
    void updateWavetables() {
        if (!m_project) return;
        m_wavetables.rebuild(m_project->wavetableBanks);
    }

    /*
     * Build the impulse responses.
     *
     * Expensive - one FFT per 512-sample partition per response - and the
     * responses depend only on the sample rate, so this runs once rather
     * than on every config sync.
     */
    void updateImpulseResponses() {
        m_irLibrary.rebuild(m_sampleRate, m_customIR);
        m_irBuilt = true;
    }

    // A user-supplied impulse response, from a WAV. Rebuilds the library,
    // so UI thread only.
    void setCustomIR(std::vector<float> samples) {
        m_customIR = std::move(samples);
        updateImpulseResponses();
    }

    const IRLibrary& impulseResponses() const { return m_irLibrary; }

    const WavetableLibrary& wavetables() const { return m_wavetables; }

    void updateChannelConfigs() {
        if (!m_project) return;

        if (!m_wavetablesBuilt) {
            updateWavetables();
            m_wavetablesBuilt = true;
        }
        if (!m_irBuilt) updateImpulseResponses();

        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            m_synths[ch].setWavetables(&m_wavetables);
            m_synths[ch].setSamplePool(&m_project->samplePool);
            m_synths[ch].setIRLibrary(&m_irLibrary);
            const auto& config = m_project->channels[ch];
            // Single sync point: oscillator, envelope, filter envelope and the
            // full per-channel effects chain (see Synthesizer::setChannelConfig).
            m_synths[ch].setChipRegion(m_project->chipRegion);
            m_synths[ch].setChannelConfig(config);

            // The note rack, copied into a fixed-size array. The project
            // stores a vector because the editor and the serializer want
            // one; the audio thread must never walk a container the UI
            // thread can resize underneath it.
            buildNoteFXRack(config.noteFX, m_noteFX[ch]);
        }

        // Aux buses. Their strips are ChannelConfigs, so the same rack sync
        // the channels use configures them - one code path, not two.
        for (int bus = 0; bus < Project::MAX_AUX_BUSES; ++bus) {
            // No convolution on the aux buses: they are already where a
            // convolution reverb would be used FROM, and a send into a send
            // is not a thing this mixer does.
            applyEffectsConfig(m_project->auxBuses[static_cast<size_t>(bus)].strip,
                               m_auxChains[static_cast<size_t>(bus)], nullptr);
            m_auxChains[static_cast<size_t>(bus)].setSampleRate(m_sampleRate);
        }

        // Resolve the routing graph here, on the UI thread, so the audio
        // thread only ever walks a precomputed order. A file carrying a
        // loop is repaired rather than trusted.
        if (!computeBusOrder(m_project->auxBuses, m_busOrder.data(), m_busOrderCount)) {
            breakRoutingCycles(m_project->auxBuses);
            computeBusOrder(m_project->auxBuses, m_busOrder.data(), m_busOrderCount);
        }

        /*
         * Latency changed, so the compensation has to be recomputed.
         *
         * Here rather than at every call site: switching on a convolution
         * reverb or a pitch shifter goes through this function, and a
         * compensation that is only updated when somebody remembers is one
         * that is wrong exactly when it matters.
         *
         * Last, because the bus half of it reads the routing order that was
         * only just resolved. Running it before that walked whatever order
         * the previous project left behind.
         */
        updateDelayCompensation();
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

        // Width and saturation - the two that make a mix sound produced
        // rather than merely correct.
        m_masterEffects.widthEnabled = m_project->masterWidthEnabled;
        m_masterEffects.stereoWidth.width = m_project->masterWidth;
        m_masterEffects.saturationEnabled = m_project->masterSaturationEnabled;
        m_masterEffects.saturator.drive = m_project->masterSaturationDrive;

        // Fold the new settings into the coefficients the audio thread
        // reads. The limiter's ceiling, lookahead and release all resolve
        // here rather than being recomputed per sample, which is what the
        // old one did with a pow() and two exp() calls.
        m_masterEffects.prepare();
    }

    // Loudness of the mix, measured to BS.1770 rather than guessed at.
    float masterLoudnessLUFS() const { return m_masterEffects.getLUFS(); }
    float masterShortTermLUFS() const {
        return m_masterEffects.lufsMeter.shortTerm();
    }
    float masterLoudnessRange() const {
        return m_masterEffects.lufsMeter.loudnessRange();
    }
    float masterLimiterReductionDB() const {
        return m_masterEffects.getLimiterGainReductionDB();
    }

    // What the listener's converter will reconstruct, which for square waves
    // runs about two decibels above what the file says.
    float masterTruePeakDB() const { return m_masterEffects.getTruePeakDB(); }
    void resetLoudness() { m_masterEffects.lufsMeter.reset(); }

private:
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    // An absolute position, so it integrates across every tempo change
    // before it. Dividing once by the project bpm is only right when the
    // tempo never changes, and drifts further out the longer the song runs.
    float beatToTime(float beat) const {
        if (!m_project) return 0.0f;
        return m_project->beatToSeconds(beat);
    }

    void allNotesOff() {
        for (auto& synth : m_synths) {
            synth.allNotesOff();
        }
    }

    /*
     * Add every sounding audio clip into the per-channel buffer.
     *
     * One pass over the arrangement, not one per channel: at 32 channels
     * the per-channel version would do the same work 32 times over. Runs on
     * the audio thread, so it only indexes - no allocation, no locks, and
     * the pool's capacity is reserved up front so the vector it reads
     * cannot be reallocated underneath it.
     */
    void mixAudioClips(std::array<float, MAX_CHANNELS>& channelSamples,
                       float beat, int activeChannels) {
        if (!m_project) return;

        // The tempo where the playhead is, so a clip under a tempo change
        // plays at the tempo the user can hear rather than the project's
        // opening one.
        const float bpm = std::max(1.0f, m_project->bpmAt(beat));
        const float secondsPerBeat = 60.0f / bpm;

        for (const Clip& clip : m_project->arrangement) {
            if (clip.type != ClipType::Audio) continue;
            if (clip.channelIndex < 0 || clip.channelIndex >= activeChannels) continue;
            if (beat < clip.startBeat) continue;
            if (beat >= clip.startBeat + clip.lengthBeats) continue;

            const Sample* sample = m_project->samplePool.getSample(clip.sampleId);
            if (sample == nullptr || !sample->isLoaded) continue;
            if (sample->audioData.empty()) continue;

            const float intoClipBeats = beat - clip.startBeat;
            float intoSampleSeconds = intoClipBeats * secondsPerBeat +
                                      clip.trimStartSeconds;

            // The trimmed region, and how the clip fills its length.
            const float regionEnd = (clip.trimEndSeconds > clip.trimStartSeconds)
                ? clip.trimEndSeconds : sample->lengthSeconds;
            const float regionLength = regionEnd - clip.trimStartSeconds;
            if (regionLength <= 0.0f) continue;

            if (intoSampleSeconds >= regionEnd) {
                if (!clip.loopClip) continue;      // played out; silence
                const float past = intoSampleSeconds - clip.trimStartSeconds;
                intoSampleSeconds = clip.trimStartSeconds +
                                    std::fmod(past, regionLength);
            }

            // Linear interpolation on a fractional index. The pool decodes
            // to 48 kHz and the engine usually runs at 44.1, so reading the
            // raw array would be a 9% pitch error - a semitone and a half.
            const float position = intoSampleSeconds *
                                   static_cast<float>(sample->sampleRate);
            if (position < 0.0f) continue;

            const size_t index = static_cast<size_t>(position);
            if (index + 1 >= sample->audioData.size()) continue;

            const float fraction = position - static_cast<float>(index);
            const float a = sample->audioData[index];
            const float b = sample->audioData[index + 1];
            float value = a + (b - a) * fraction;

            value *= clip.gain;

            // Fades, in beats so they follow the tempo like everything else.
            if (clip.fadeInBeats > 0.0f && intoClipBeats < clip.fadeInBeats) {
                value *= intoClipBeats / clip.fadeInBeats;
            }
            if (clip.fadeOutBeats > 0.0f) {
                const float untilEnd = clip.lengthBeats - intoClipBeats;
                if (untilEnd < clip.fadeOutBeats) {
                    value *= (untilEnd > 0.0f) ? (untilEnd / clip.fadeOutBeats) : 0.0f;
                }
            }

            channelSamples[static_cast<size_t>(clip.channelIndex)] += value;
        }
    }

    /*
     * Attach a pitch to the hits expandNote produced.
     *
     * NoteTrigger carries no pitch because nothing it expands - delay, cut,
     * retrigger, echo - ever changes one. The note rack does, so the rack
     * works on voices instead.
     */
    static int voicesFromTriggers(const NoteTrigger* triggers, int count,
                                  int pitch, NoteVoice* out) {
        const int n = std::min(count, MAX_NOTE_VOICES);
        for (int i = 0; i < n; ++i) {
            out[i].startBeat = triggers[i].startBeat;
            out[i].endBeat   = triggers[i].endBeat;
            out[i].velocity  = triggers[i].velocity;
            out[i].pitch     = pitch;
        }
        return n;
    }

    // A note's identity, for the modules that roll dice. Includes the loop
    // pass, so a randomised velocity re-rolls each time round rather than
    // freezing into a fixed pattern on the first repeat.
    uint32_t noteFXSeed(int pitch, float absStartBeat) const {
        uint32_t h = static_cast<uint32_t>(static_cast<int32_t>(absStartBeat * 256.0f));
        h ^= static_cast<uint32_t>(pitch) * 2654435761u;
        h ^= m_loopPass + 0x9E3779B9u + (h << 6) + (h >> 2);
        return h;
    }

    /*
     * Does a tied note start where this one ends?
     *
     * If so this note must not be released, or the slur has a dip in it. The
     * scan is over the pattern rather than a precomputed index because a
     * pattern is small and the alternative is a cache that has to be
     * invalidated on every edit - and a stale one would hang a note forever,
     * which is a far worse failure than a linear scan.
     */
    static bool tiedAfter(const Pattern& pattern, const Note& note) {
        const float end = note.startTime + note.duration;
        for (const Note& other : pattern.notes) {
            if (&other == &note) continue;
            if (!other.tie) continue;
            if (std::fabs(other.startTime - end) < 1e-3f) return true;
        }
        return false;
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

                /*
                 * The channel's note rack.
                 *
                 * Between the pattern and the instrument: the pattern still
                 * holds the note that was written, and what sounds is that
                 * note read through the rack. A chord module turns one voice
                 * into three, an arpeggiator turns those into a run.
                 *
                 * The seed is the note, not a counter, because the note-on
                 * and the note-off arrive in different calls thousands of
                 * samples apart and a random module has to answer the same
                 * way to both - the same reason noteShouldSound is a hash.
                 */
                NoteVoice voices[MAX_NOTE_VOICES];
                int voiceCount =
                    voicesFromTriggers(triggers, triggerCount, soundingPitch, voices);

                const NoteFXRack& rack = m_noteFX[clip.channelIndex];
                if (rack.active()) {
                    const uint32_t seed =
                        noteFXSeed(soundingPitch, clip.startBeat + note.startTime);
                    voiceCount = applyNoteFX(rack, voices, voiceCount,
                                             MAX_NOTE_VOICES, seed);
                }

                for (int t = 0; t < voiceCount; ++t) {
                    const NoteVoice& trigger = voices[t];

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

                        /*
                         * Articulation, set immediately before the note.
                         *
                         * A tied note takes over the voice that is already
                         * sounding instead of starting a new one, and with a
                         * slide it glides to the new pitch at that many
                         * semitones per second - which is tone portamento,
                         * and is the thing the slide effect could never do.
                         */
                        m_synths[clip.channelIndex].setNextArticulation(
                            note.tie, note.tie ? std::fabs(note.slide) : 0.0f);

                        m_synths[clip.channelIndex].noteOn(
                            trigger.pitch, velocity, startTime,
                            fadeInSec, fadeOutSec, durationSec, note.oscillatorType,
                            note.vibrato, note.arpeggio, note.slide,
                            note.dutyCycle, note.useDutyCycle,
                            note.sweepDirection, note.sweepSpeed, note.sweepAmount,
                            note.tremolo, note.tremoloSpeed);
                    }

                    /*
                     * Note off - unless something ties to this note.
                     *
                     * Releasing and then immediately tying would work, since
                     * a tie brings a releasing voice back to sustain. But it
                     * would put a dip in the envelope at every slur, which is
                     * exactly the artefact legato exists to remove. Cheaper
                     * and cleaner to not let go in the first place.
                     */
                    if (swungEnd >= fromBeat && swungEnd < toBeat &&
                        !tiedAfter(pattern, note)) {
                        m_synths[clip.channelIndex].noteOff(
                            trigger.pitch, m_state.currentTime);
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

            // The preview channel's rack, so the pattern being edited sounds
            // the way it will on the timeline. A rack you can only hear once
            // the pattern is placed is a rack you cannot dial in.
            NoteVoice voices[MAX_NOTE_VOICES];
            int voiceCount =
                voicesFromTriggers(triggers, triggerCount, note.pitch, voices);

            const NoteFXRack& rack = m_noteFX[m_previewChannel];
            if (rack.active()) {
                voiceCount = applyNoteFX(rack, voices, voiceCount, MAX_NOTE_VOICES,
                                         noteFXSeed(note.pitch, note.startTime));
            }

            for (int t = 0; t < voiceCount; ++t) {
                const NoteVoice& trigger = voices[t];

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

                    m_synths[m_previewChannel].setNextArticulation(
                        note.tie, note.tie ? std::fabs(note.slide) : 0.0f);

                    m_synths[m_previewChannel].noteOn(
                        trigger.pitch, velocity, startTime,
                        fadeInSec, fadeOutSec, durationSec, note.oscillatorType,
                        note.vibrato, note.arpeggio, note.slide,
                        note.dutyCycle, note.useDutyCycle,
                        note.sweepDirection, note.sweepSpeed, note.sweepAmount,
                        note.tremolo, note.tremoloSpeed);
                }

                // Note off (also swing the end time)
                const float swungEnd = swungStart + held;
                if (swungEnd >= fromBeat && swungEnd < toBeat &&
                    !tiedAfter(pattern, note)) {
                    m_synths[m_previewChannel].noteOff(trigger.pitch, m_state.currentTime);
                }
            }
        }
    }

    // Convert beats to seconds based on current BPM
    // A DURATION, not a position: how long `beats` lasts at the tempo in
    // force right now. Deliberately not integrated through the map - the
    // callers use it for note lengths and envelope times at the playhead,
    // where the tempo three minutes later is not the question being asked.
    float beatsToSeconds(float beats) const {
        if (!m_project) return 0.0f;
        const float bpm = m_project->bpmAt(m_state.currentBeat);
        if (bpm <= 0.0f) return 0.0f;
        return beats * 60.0f / bpm;
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
    // Owned here and shared by every channel: a mipmapped bank is about
    // 150 KB, and thirty-two channels each holding their own copy of the
    // same tables would be five megabytes of duplicates.
    WavetableLibrary m_wavetables;
    bool m_wavetablesBuilt = false;

    // Shared impulse responses, and whatever the user loaded.
    IRLibrary m_irLibrary;
    std::vector<float> m_customIR;
    bool m_irBuilt = false;

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

    /*
     * One hosted-plugin chain per channel.
     *
     * Held here rather than on the Synthesizer because a plugin runs on
     * blocks and a Synthesizer is per-sample, and because a chain owns
     * heap-allocated instances that a Synthesizer - which is copied - must
     * not.
     */
    std::array<PluginChain, MAX_CHANNELS> m_pluginChains;

    /*
     * One compensating delay per channel, and what each channel's own
     * latency was when they were last computed.
     *
     * Fixed-capacity lines, so nothing allocates on the audio thread.
     */
    std::array<CompensationDelay, MAX_CHANNELS> m_compensation;
    std::array<int, MAX_CHANNELS> m_channelLatency{};

    /*
     * The rest of the graph.
     *
     * m_directDelay holds a channel's straight-to-master path back until the
     * copy that went out through a bus has come back. The bus lines level
     * the sends arriving at each bus and each bus's own output.
     */
    // Each channel's note rack, synced from the project (see
    // updateChannelConfigs) so the audio thread reads a fixed-size copy.
    std::array<NoteFXRack, MAX_CHANNELS> m_noteFX;

    std::array<CompensationDelay, MAX_CHANNELS> m_directDelay;
    std::array<CompensationDelay, Project::MAX_AUX_BUSES> m_busSendDelay;
    std::array<CompensationDelay, Project::MAX_AUX_BUSES> m_busOutDelay;
    std::array<float, Project::MAX_AUX_BUSES> m_auxBusAccum{};
    std::array<float, Project::MAX_AUX_BUSES> m_auxCombined{};
    int m_mixLatency = 0;

    // Decaying peak level per channel, for the mixer meters
    std::array<float, MAX_CHANNELS> m_channelPeaks = {};

    /*
     * One oscilloscope ring per channel, plus the master.
     *
     * A meter says a channel is doing something; a scope says what. On chip
     * work that is most of the difference - a duty change and a volume
     * change look the same on a meter.
     */
    std::array<ScopeBuffer, MAX_CHANNELS> m_scopes;
    ScopeBuffer m_masterScopeLeft;
    ScopeBuffer m_masterScopeRight;

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
