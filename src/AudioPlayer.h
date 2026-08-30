#pragma once
#include <vector>
#include <mutex>
#include <atomic>
#include <cmath>
#include "../vendor/miniaudio/miniaudio.h"
#include "AudioAnalyzer.h" // For DetectedNote

class AudioPlayer {
public:
    AudioPlayer() : isPlaying(false), playbackMode(Mode::None), cursor(0), playbackTime(0.0f), sampleRate(44100), isAdvanced(false), isInitialized(false) {}

    ~AudioPlayer() {
        stop();
        if (isInitialized) ma_device_uninit(&device);
    }

    bool init(int rate = 44100) {
        if (isInitialized) {
            ma_device_uninit(&device);
            isInitialized = false;
        }

        this->sampleRate = rate;
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 1;
        config.sampleRate = sampleRate;
        config.dataCallback = data_callback;
        config.pUserData = this;

        if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
            return false;
        }
        isInitialized = true;
        return true;
    }

    void playRaw(const std::vector<float>& samples) {
        if (!isInitialized) return;
        stop();
        {
            std::lock_guard<std::mutex> lock(mutex);
            rawBuffer = samples;
            cursor = 0;
            playbackMode = Mode::Raw;
        }
        ma_device_start(&device);
        isPlaying = true;
    }

    void playNotes(const std::vector<DetectedNote>& notes, int instrumentType, bool advanced = false) {
        if (!isInitialized) return;
        stop();
        {
            std::lock_guard<std::mutex> lock(mutex);
            this->notes = notes;
            this->selectedInstrument = instrumentType;
            this->isAdvanced = advanced;
            playbackTime = 0.0f;
            playbackMode = Mode::Notes;
            
            // Calculate total duration
            totalDuration = 0.0f;
            for(const auto& n : notes) {
                float end = n.startTime + n.duration;
                if(end > totalDuration) totalDuration = end;
            }
            totalDuration += 0.5f; // buffer
        }
        ma_device_start(&device);
        isPlaying = true;
    }

    void stop() {
        if (!isInitialized) { isPlaying = false; return; }
        if (ma_device_get_state(&device) == ma_device_state_started) {
            ma_device_stop(&device);
        }
        isPlaying = false;
    }
    
    void update() {
        if (!isInitialized) return;
        if (!isPlaying && ma_device_get_state(&device) == ma_device_state_started) {
            ma_device_stop(&device);
        }
    }
    
    bool getIsPlaying() const { return isPlaying; }
    int getSampleRate() const { return sampleRate; }

private:
    enum class Mode { None, Raw, Notes };
    
    ma_device device;
    std::atomic<bool> isPlaying;
    bool isInitialized;
    Mode playbackMode;
    std::mutex mutex;
    int sampleRate;
    bool isAdvanced;
    
    // Raw playback state
    std::vector<float> rawBuffer;
    size_t cursor;
    
    // Note playback state
    std::vector<DetectedNote> notes;
    int selectedInstrument;
    float playbackTime;
    float totalDuration;

    static float noteToFreq(int note) {
        return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
    }

    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        AudioPlayer* player = (AudioPlayer*)pDevice->pUserData;
        float* output = (float*)pOutput;
        std::lock_guard<std::mutex> lock(player->mutex);

        if (!player->isPlaying) {
            for (ma_uint32 i = 0; i < frameCount; ++i) output[i] = 0.0f;
            return;
        }

        if (player->playbackMode == Mode::Raw) {
            for (ma_uint32 i = 0; i < frameCount; ++i) {
                if (player->cursor < player->rawBuffer.size()) {
                    output[i] = player->rawBuffer[player->cursor++];
                } else {
                    output[i] = 0.0f;
                    player->isPlaying = false;
                }
            }
        }
        else if (player->playbackMode == Mode::Notes) {
            float dt = 1.0f / player->sampleRate;
            
            for (ma_uint32 i = 0; i < frameCount; ++i) {
                float mix = 0.0f;
                float time = player->playbackTime;
                
                for (const auto& note : player->notes) {
                    if (time >= note.startTime && time < note.startTime + note.duration + 0.2f) { // +0.2f for release tail
                        float localTime = time - note.startTime;
                        if (localTime < 0) continue;

                        // ADSR Defaults (Simple / Raw)
                        float attack = 0.01f;
                        float decay = 0.0f;
                        float sustain = 1.0f;
                        float release = 0.05f;
                        float baseFreq = noteToFreq(note.noteNumber);
                        float freq = baseFreq;

                        // Advanced Mode: Configure ADSR & Effects based on preset
                        if (player->isAdvanced) {
                            switch(player->selectedInstrument) {
                                case 8: // Synth Pad
                                case 13: // Synth Strings
                                    attack = 0.5f; release = 0.5f; break;
                                case 10: // Synth Pluck
                                case 19: // Guira
                                case 20: // Bongo
                                case 21: // Timbale
                                case 23: // Dembow Snare
                                    attack = 0.001f; decay = 0.2f; sustain = 0.0f; release = 0.1f; break;
                                case 17: // Reggaeton Bass
                                case 22: // Dembow 808
                                case 26: // Sub Bass 808
                                    // Pitch drop effect
                                    if (localTime < 0.1f) freq *= (1.5f - localTime * 5.0f);
                                    break;
                                case 16: // Bell
                                case 46: // Lo-Fi Keys
                                    attack = 0.001f; decay = 0.5f; sustain = 0.0f; release = 0.5f; break;
                                case 38: // Synthwave Pad
                                case 49: // Gated Pad
                                    attack = 0.5f; release = 0.5f; break;
                                case 42: // Techno Stab
                                    attack = 0.001f; decay = 0.15f; sustain = 0.0f; break;
                            }
                        }

                        // Calculate Envelope Gain
                        if (!note.isDrum) {
                            // Ensure melodic notes get a small fade-in/out based on their duration to avoid clicks
                            float fadeIn = std::max(0.003f, std::min(0.02f, note.duration * 0.15f));
                            float fadeOut = std::max(0.05f, std::min(0.3f, note.duration * 0.25f));
                            attack = std::max(attack, fadeIn);
                            release = std::max(release, fadeOut * 0.5f);
                        }

                        float envGain = 0.0f;
                        if (localTime < attack) {
                            envGain = localTime / attack;
                        } else if (localTime < note.duration) {
                            // Sustain phase (simplified decay)
                             if (decay > 0 && localTime - attack < decay) {
                                envGain = 1.0f - (1.0f - sustain) * ((localTime - attack) / decay);
                             } else {
                                envGain = sustain;
                             }
                        } else {
                            // Release phase
                            float releaseTime = localTime - note.duration;
                            if (releaseTime < release) {
                                envGain = sustain * (1.0f - releaseTime / release);
                            } else {
                                envGain = 0.0f;
                            }
                        }

                        // Extra per-note fade window (non-drum) to guarantee smooth tails even when release is tiny
                        if (!note.isDrum && envGain > 0.0f) {
                            float fadeIn = std::max(0.003f, std::min(0.02f, note.duration * 0.15f));
                            float fadeOut = std::max(0.05f, std::min(0.3f, note.duration * 0.25f));
                            if (localTime < fadeIn) {
                                envGain *= (localTime / fadeIn);
                            } else if (localTime > note.duration - fadeOut) {
                                float t = std::clamp((note.duration - localTime) / fadeOut, 0.0f, 1.0f);
                                envGain *= t;
                            }
                        }

                        if (envGain <= 0.0f) continue;

                        // Synthesis
                        float phase = localTime * freq;
                        phase -= floor(phase); // 0.0 to 1.0
                        
                        float sample = 0.0f;
                        int type = (note.instrumentOverride != -1) ? note.instrumentOverride : player->selectedInstrument;
                        
                        // Only use auto-detection logic if NO override is present
                        bool useAutoDrum = note.isDrum && note.instrumentOverride == -1;

                        if (useAutoDrum) {
                            // Simple Auto-Detected Drum Synthesis
                            if (note.drumType == 0) { // Kick
                                float f = 150.0f * exp(-20.0f * localTime);
                                sample = sin(f * localTime * 6.28f) * exp(-5.0f * localTime);
                            } else if (note.drumType == 1) { // Snare
                                float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                                sample = noise * exp(-15.0f * localTime);
                            } else { // Hat
                                float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                                sample = noise * exp(-40.0f * localTime);
                            }
                        } else {
                                                        switch (type) {
                                                            case 0: // Pulse 50
                                                                sample = (phase < 0.5f) ? 1.0f : -1.0f; break;
                                                            case 1: // Pulse 25
                                                            case 11: // Synth Arp
                                                            case 39: // Synthwave Arp
                                                                sample = (phase < 0.25f) ? 1.0f : -1.0f; break;
                                                            case 2: // Pulse 12.5
                                                            case 15: // Synth Chip
                                                                sample = (phase < 0.125f) ? 1.0f : -1.0f; break;
                                                            case 3: // Triangle
                                                            case 8: // Synth Pad
                                                            case 10: // Synth Pluck
                                                            case 13: // Synth Strings
                                                                sample = 4.0f * fabsf(phase - 0.5f) - 1.0f; break;
                                                            case 4: // Saw
                                                            case 7: // Synth Lead
                                                            case 9: // Synth Bass
                                                            case 14: // Synth Brass
                                                            case 18: // Latin Brass
                                                            case 24: // Synthwave Bass
                                                            case 25: // Acid Bass
                                                            case 38: // Synthwave Pad
                                                            case 42: // Techno Stab
                                                            case 43: // Hoover
                                                            case 45: // Reese
                                                            case 49: // Gated Pad
                                                            case 50: // Poly Synth
                                                                sample = 2.0f * phase - 1.0f; break;
                                                            case 5: // Sine
                                                            case 12: // Synth Organ
                                                            case 16: // Synth Bell
                                                            case 17: // Reggaeton Bass
                                                            case 22: // Dembow 808
                                                            case 26: // Sub Bass 808
                                                            case 41: // Synthwave FM
                                                            case 46: // Lo-Fi Keys
                                                                sample = sin(phase * 6.28318f); break;
                                                            case 6: // Noise
                                                            case 19: // Guira
                                                            case 23: // Dembow Snare
                                                            case 47: // Vinyl Noise
                                                                sample = ((float)rand() / RAND_MAX) * 2.0f - 1.0f; break;
                                                            case 20: // Bongo
                                                            case 21: // Timbale
                                                                sample = sin(phase * 6.28f) * 0.5f + sin(phase * 2.5f * 6.28f) * 0.5f; break;
                                                            
                                                            // Complex Synths (Approximations)
                                                            case 37: // Synthwave Lead
                                                            case 48: // Trap Lead
                                                            case 51: // Sync Lead
                                                                sample = (phase < 0.5f ? 1.0f : -1.0f) * 0.5f + (2.0f * phase - 1.0f) * 0.5f; break;
                                                            case 40: // Synthwave Chord
                                                            case 44: // Rave Chord
                                                                sample = (phase < 0.25f ? 1.0f : -1.0f); break;
                            
                                                            // Standard Drums (Manual Selection)
                                                            case 27: // Kick
                                                            case 52: // Kick Hard
                                                            case 53: // Kick Soft
                                                            {
                                                                float f = 150.0f * exp(-20.0f * localTime);
                                                                sample = sin(f * localTime * 6.28f) * exp(-5.0f * localTime); 
                                                                break;
                                                            }
                                                            case 28: // Kick 808
                                                            {
                                                                float f = 100.0f * exp(-15.0f * localTime);
                                                                sample = sin(f * localTime * 6.28f) * exp(-2.0f * localTime); 
                                                                break;
                                                            }
                                                            case 29: // Snare
                                                            case 54: // Snare Rim
                                                            {
                                                                float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                                                                sample = noise * exp(-15.0f * localTime);
                                                                break;
                                                            }
                                                            case 30: // Snare 808
                                                            {
                                                                float tone = sin(180.0f * localTime * 6.28f) * exp(-20.0f * localTime);
                                                                float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                                                                sample = tone * 0.6f + noise * 0.4f * exp(-15.0f * localTime);
                                                                break;
                                                            }
                                                            case 31: // Clap
                                                            {
                                                                float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                                                                float env = exp(-15.0f * localTime);
                                                                if (localTime < 0.03f) env *= (rand() % 2); 
                                                                sample = noise * env;
                                                                break;
                                                            }
                                                            case 32: // HiHat
                                                            case 55: // HiHat Pedal
                                                            {
                                                                float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                                                                sample = noise * exp(-40.0f * localTime);
                                                                break;
                                                            }
                                                            case 33: // HiHat Open
                                                            {
                                                                float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                                                                sample = noise * exp(-10.0f * localTime);
                                                                break;
                                                            }
                                                            case 34: // Tom
                                                            case 56: // Tom Low
                                                            case 57: // Tom High
                                                            {
                                                                float f = 150.0f * exp(-10.0f * localTime) + 100.0f;
                                                                sample = sin(f * localTime * 6.28f) * exp(-8.0f * localTime); 
                                                                break;
                                                            }
                                                            case 35: // Crash
                                                            case 36: // Ride
                                                            {
                                                                float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                                                                sample = noise * exp(-3.0f * localTime);
                                                                break;
                                                            }
                                                            case 58: // Cowbell
                                                            case 59: // Clave
                                                            case 60: // Conga
                                                            case 61: // Maracas
                                                            case 62: // Tambourine
                                                                sample = ((float)rand() / RAND_MAX) * 2.0f - 1.0f; break; 
                                                            default: 
                                                                sample = (phase < 0.5f) ? 1.0f : -1.0f; break;
                                                        }
                                                    }                        
                        // Apply advanced effects (Vibrato)
                        if (player->isAdvanced && note.duration > 0.5f) {
                             float vib = sin(localTime * 30.0f) * 0.5f; // Simple AM tremolo for now
                             envGain *= (1.0f + vib * 0.2f);
                        }

                        mix += sample * 0.5f * envGain * note.velocity;
                    }
                }
                
                output[i] = mix;
                player->playbackTime += dt;
            }
            
            if (player->playbackTime > player->totalDuration) {
                player->isPlaying = false;
            }
        }
    }
};
