#define NOMINMAX
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <future>
#include <thread>
#include <numeric>

#define MINIAUDIO_IMPLEMENTATION
#include "../vendor/miniaudio/miniaudio.h"

#include "AudioAnalyzer.h"
#include "Synthesizer.h"

using namespace ChiptuneTracker;

// ============================================================================ 
// Test Data Duplicate (From UI.h)
// ============================================================================ 
struct TrackNote {
    float beat;         // Start beat (0-based)
    int pitch;          // MIDI note
    OscillatorType osc; // Instrument type
    float duration;     // Note duration in beats
    float velocity = 0.8f;    // Note velocity (0.0-1.0) for dynamics
    float vibrato = 0.0f;     // Vibrato depth in semitones (0 = none)
    float vibratoSpeed = 5.5f; // Vibrato speed in Hz
};

// Nightcall Track Data (Corrected Version)
static const TrackNote g_SynthwaveNightcall[] = {
    // === DRUMS (Heavy, gated reverb feel) ===
    // Kick: Driving beat (Boom... Boom-Boom...)
    {0.0f, 36, OscillatorType::Kick808, 0.6f},   // 1
    {1.5f, 36, OscillatorType::Kick808, 0.4f},   // 2& (ghost)
    {2.0f, 36, OscillatorType::Kick808, 0.6f},   // 3
    {2.5f, 36, OscillatorType::Kick808, 0.5f},   // 3&
    {4.0f, 36, OscillatorType::Kick808, 0.6f},   // 1
    {6.0f, 36, OscillatorType::Kick808, 0.6f},   // 3
    {6.5f, 36, OscillatorType::Kick808, 0.5f},   // 3&
    {8.0f, 36, OscillatorType::Kick808, 0.6f},   // 1
    {9.5f, 36, OscillatorType::Kick808, 0.4f},   // 2&
    {10.0f, 36, OscillatorType::Kick808, 0.6f},  // 3
    {10.5f, 36, OscillatorType::Kick808, 0.5f},  // 3&
    {12.0f, 36, OscillatorType::Kick808, 0.6f},  // 1
    {14.0f, 36, OscillatorType::Kick808, 0.6f},  // 3
    {14.5f, 36, OscillatorType::Kick808, 0.5f},  // 3&

    // Snare: Huge, gated sound on 2 and 4
    {1.0f, 38, OscillatorType::Snare808, 0.7f},
    {3.0f, 38, OscillatorType::Snare808, 0.7f},
    {5.0f, 38, OscillatorType::Snare808, 0.7f},
    {7.0f, 38, OscillatorType::Snare808, 0.7f},
    {9.0f, 38, OscillatorType::Snare808, 0.7f},
    {11.0f, 38, OscillatorType::Snare808, 0.7f},
    {13.0f, 38, OscillatorType::Snare808, 0.7f},
    {15.0f, 38, OscillatorType::Snare808, 0.7f},

    // Hi-Hats: Steady 16th notes (closed)
    {0.0f, 42, OscillatorType::HiHat, 0.3f}, {0.25f, 42, OscillatorType::HiHat, 0.2f}, {0.5f, 42, OscillatorType::HiHat, 0.3f}, {0.75f, 42, OscillatorType::HiHat, 0.2f},
    {1.0f, 42, OscillatorType::HiHat, 0.3f}, {1.25f, 42, OscillatorType::HiHat, 0.2f}, {1.5f, 42, OscillatorType::HiHat, 0.3f}, {1.75f, 42, OscillatorType::HiHat, 0.2f},
    {2.0f, 42, OscillatorType::HiHat, 0.3f}, {2.25f, 42, OscillatorType::HiHat, 0.2f}, {2.5f, 42, OscillatorType::HiHat, 0.3f}, {2.75f, 42, OscillatorType::HiHat, 0.2f},
    {3.0f, 42, OscillatorType::HiHat, 0.3f}, {3.25f, 42, OscillatorType::HiHat, 0.2f}, {3.5f, 42, OscillatorType::HiHat, 0.3f}, {3.75f, 42, OscillatorType::HiHat, 0.2f},
    // ... repeat for 4 bars ...
    {4.0f, 42, OscillatorType::HiHat, 0.3f}, {4.5f, 42, OscillatorType::HiHat, 0.3f}, {5.0f, 42, OscillatorType::HiHat, 0.3f}, {5.5f, 42, OscillatorType::HiHat, 0.3f},
    {6.0f, 42, OscillatorType::HiHat, 0.3f}, {6.5f, 42, OscillatorType::HiHat, 0.3f}, {7.0f, 42, OscillatorType::HiHat, 0.3f}, {7.5f, 42, OscillatorType::HiHat, 0.3f},
    {8.0f, 42, OscillatorType::HiHat, 0.3f}, {8.5f, 42, OscillatorType::HiHat, 0.3f}, {9.0f, 42, OscillatorType::HiHat, 0.3f}, {9.5f, 42, OscillatorType::HiHat, 0.3f},
    {10.0f, 42, OscillatorType::HiHat, 0.3f}, {10.5f, 42, OscillatorType::HiHat, 0.3f}, {11.0f, 42, OscillatorType::HiHat, 0.3f}, {11.5f, 42, OscillatorType::HiHat, 0.3f},
    {12.0f, 42, OscillatorType::HiHat, 0.3f}, {12.5f, 42, OscillatorType::HiHat, 0.3f}, {13.0f, 42, OscillatorType::HiHat, 0.3f}, {13.5f, 42, OscillatorType::HiHat, 0.3f},
    {14.0f, 42, OscillatorType::HiHat, 0.3f}, {14.5f, 42, OscillatorType::HiHat, 0.3f}, {15.0f, 42, OscillatorType::HiHat, 0.3f}, {15.5f, 42, OscillatorType::HiHat, 0.3f},

    // === BASS (Cm - Eb - Bb - Gm) ===
    // Driving 8th note pulse with octave jumps
    // Bar 1: Cm (C2)
    {0.0f, 36, OscillatorType::KavinskyBass, 0.5f}, {0.5f, 48, OscillatorType::KavinskyBass, 0.5f},
    {1.0f, 36, OscillatorType::KavinskyBass, 0.5f}, {1.5f, 48, OscillatorType::KavinskyBass, 0.5f},
    {2.0f, 36, OscillatorType::KavinskyBass, 0.5f}, {2.5f, 48, OscillatorType::KavinskyBass, 0.5f},
    {3.0f, 36, OscillatorType::KavinskyBass, 0.5f}, {3.5f, 48, OscillatorType::KavinskyBass, 0.5f},
    // Bar 2: Eb (Eb2)
    {4.0f, 39, OscillatorType::KavinskyBass, 0.5f}, {4.5f, 51, OscillatorType::KavinskyBass, 0.5f},
    {5.0f, 39, OscillatorType::KavinskyBass, 0.5f}, {5.5f, 51, OscillatorType::KavinskyBass, 0.5f},
    {6.0f, 39, OscillatorType::KavinskyBass, 0.5f}, {6.5f, 51, OscillatorType::KavinskyBass, 0.5f},
    {7.0f, 39, OscillatorType::KavinskyBass, 0.5f}, {7.5f, 51, OscillatorType::KavinskyBass, 0.5f},
    // Bar 3: Bb (Bb1)
    {8.0f, 34, OscillatorType::KavinskyBass, 0.5f}, {8.5f, 46, OscillatorType::KavinskyBass, 0.5f},
    {9.0f, 34, OscillatorType::KavinskyBass, 0.5f}, {9.5f, 46, OscillatorType::KavinskyBass, 0.5f},
    {10.0f, 34, OscillatorType::KavinskyBass, 0.5f}, {10.5f, 46, OscillatorType::KavinskyBass, 0.5f},
    {11.0f, 34, OscillatorType::KavinskyBass, 0.5f}, {11.5f, 46, OscillatorType::KavinskyBass, 0.5f},
    // Bar 4: Gm (G1)
    {12.0f, 31, OscillatorType::KavinskyBass, 0.5f}, {12.5f, 43, OscillatorType::KavinskyBass, 0.5f},
    {13.0f, 31, OscillatorType::KavinskyBass, 0.5f}, {13.5f, 43, OscillatorType::KavinskyBass, 0.5f},
    {14.0f, 31, OscillatorType::KavinskyBass, 0.5f}, {14.5f, 43, OscillatorType::KavinskyBass, 0.5f},
    {15.0f, 31, OscillatorType::KavinskyBass, 0.5f}, {15.5f, 43, OscillatorType::KavinskyBass, 0.5f},

    // === PADS (Dark, sustained) ===
    // Cm
    {0.0f, 48, OscillatorType::SynthwavePad, 4.0f}, {0.0f, 51, OscillatorType::SynthwavePad, 4.0f}, {0.0f, 55, OscillatorType::SynthwavePad, 4.0f},
    // Eb
    {4.0f, 51, OscillatorType::SynthwavePad, 4.0f}, {4.0f, 55, OscillatorType::SynthwavePad, 4.0f}, {4.0f, 58, OscillatorType::SynthwavePad, 4.0f},
    // Bb
    {8.0f, 46, OscillatorType::SynthwavePad, 4.0f}, {8.0f, 50, OscillatorType::SynthwavePad, 4.0f}, {8.0f, 53, OscillatorType::SynthwavePad, 4.0f},
    // Gm
    {12.0f, 43, OscillatorType::SynthwavePad, 4.0f}, {12.0f, 46, OscillatorType::SynthwavePad, 4.0f}, {12.0f, 50, OscillatorType::SynthwavePad, 4.0f},

    // === LEAD (Vocoder-style melody approximation) ===
    // "I'm giving you a night call..."
    {0.0f, 67, OscillatorType::Vocoder, 0.5f},  // G4
    {0.5f, 67, OscillatorType::Vocoder, 0.25f}, // G4
    {0.75f, 67, OscillatorType::Vocoder, 0.5f}, // G4
    {1.25f, 65, OscillatorType::Vocoder, 0.25f}, // F4
    {1.5f, 63, OscillatorType::Vocoder, 0.5f},  // Eb4
    {2.0f, 63, OscillatorType::Vocoder, 1.0f},  // Eb4
    {3.0f, 60, OscillatorType::Vocoder, 1.0f},  // C4

    // "To tell you how I feel"
    {4.0f, 67, OscillatorType::Vocoder, 0.5f},  // G4
    {4.5f, 67, OscillatorType::Vocoder, 0.25f}, // G4
    {4.75f, 67, OscillatorType::Vocoder, 0.5f}, // G4
    {5.25f, 68, OscillatorType::Vocoder, 0.25f}, // Ab4
    {5.5f, 67, OscillatorType::Vocoder, 0.5f},  // G4
    {6.0f, 65, OscillatorType::Vocoder, 1.0f},  // F4
    {7.0f, 63, OscillatorType::Vocoder, 1.0f},  // Eb4

    // "I want to drive you through the night"
    {8.0f, 65, OscillatorType::Vocoder, 0.5f},  // F4
    {8.5f, 65, OscillatorType::Vocoder, 0.25f}, // F4
    {8.75f, 65, OscillatorType::Vocoder, 0.5f}, // F4
    {9.25f, 63, OscillatorType::Vocoder, 0.25f}, // Eb4
    {9.5f, 62, OscillatorType::Vocoder, 0.5f},  // D4
    {10.0f, 62, OscillatorType::Vocoder, 1.0f}, // D4
    {11.0f, 58, OscillatorType::Vocoder, 1.0f}, // Bb3

    // "Down the hills"
    {12.0f, 62, OscillatorType::Vocoder, 0.5f}, // D4
    {12.5f, 62, OscillatorType::Vocoder, 0.25f}, // D4
    {12.75f, 62, OscillatorType::Vocoder, 0.5f}, // D4
    {13.25f, 63, OscillatorType::Vocoder, 0.25f}, // Eb4
    {13.5f, 62, OscillatorType::Vocoder, 0.5f}, // D4
    {14.0f, 60, OscillatorType::Vocoder, 2.0f}, // C4
};

// ============================================================================ 
// Helpers
// ============================================================================ 

// Load WAV helper
static bool loadWavFile(const std::string& filepath, int& sampleRateOut, std::vector<float>& samplesOut) {
    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 0); // Use native sample rate
    if (ma_decoder_init_file(filepath.c_str(), &config, &decoder) != MA_SUCCESS) return false;
    sampleRateOut = decoder.outputSampleRate;
    ma_uint64 frames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) != MA_SUCCESS) { ma_decoder_uninit(&decoder); return false; }
    samplesOut.resize(frames);
    ma_uint64 read = 0;
    if (ma_decoder_read_pcm_frames(&decoder, samplesOut.data(), frames, &read) != MA_SUCCESS) { ma_decoder_uninit(&decoder); return false; }
    samplesOut.resize(static_cast<size_t>(read));
    ma_decoder_uninit(&decoder);
    return true;
}

// Normalize float buffer
void normalize(std::vector<float>& buffer) {
    float maxVal = 0.0f;
    for (float v : buffer) maxVal = std::max(maxVal, std::abs(v));
    if (maxVal > 0.0f) {
        float scale = 1.0f / maxVal;
        for (float& v : buffer) v *= scale;
    }
}

// Calculate spectrogram similarity (percentage match)
float calculateMatch(const std::vector<float>& ref, int refRate, const std::vector<float>& gen, int genRate) {
    // Simple RMS window comparison for structure
    int windowSizeRef = refRate / 20; // 50ms resolution (faster analysis)
    int windowSizeGen = genRate / 20; 
    
    size_t numWindows = std::min(ref.size() / windowSizeRef, gen.size() / windowSizeGen);
    if (numWindows == 0) return 0.0f;
    
    // Parallelize analysis loop
    unsigned int numThreads = std::thread::hardware_concurrency();
    std::vector<std::future<float>> futures;
    
    size_t windowsPerThread = numWindows / numThreads;
    
    for (unsigned int t = 0; t < numThreads; ++t) {
        size_t startWin = t * windowsPerThread;
        size_t endWin = (t == numThreads - 1) ? numWindows : (t + 1) * windowsPerThread;
        
        futures.push_back(std::async(std::launch::async, [=, &ref, &gen]() {
            float localScore = 0.0f;
            for (size_t i = startWin; i < endWin; ++i) {
                float rmsRef = 0.0f;
                float zcrRef = 0.0f;
                for (int j = 0; j < windowSizeRef; ++j) {
                    float val = ref[i * windowSizeRef + j];
                    rmsRef += val * val;
                    if (j > 0 && (ref[i * windowSizeRef + j] * ref[i * windowSizeRef + j - 1] < 0)) zcrRef += 1.0f;
                }
                rmsRef = std::sqrt(rmsRef / windowSizeRef);
                zcrRef /= windowSizeRef;
                
                float rmsGen = 0.0f;
                float zcrGen = 0.0f;
                for (int j = 0; j < windowSizeGen; ++j) {
                    float val = gen[i * windowSizeGen + j];
                    rmsGen += val * val;
                    if (j > 0 && (gen[i * windowSizeGen + j] * gen[i * windowSizeGen + j - 1] < 0)) zcrGen += 1.0f;
                }
                rmsGen = std::sqrt(rmsGen / windowSizeGen);
                zcrGen /= windowSizeGen;
                
                float ampDiff = std::abs(rmsRef - rmsGen);
                float ampScore = std::max(0.0f, 1.0f - ampDiff * 2.0f);
                float zcrDiff = std::abs(zcrRef - zcrGen);
                float timbreScore = std::max(0.0f, 1.0f - zcrDiff * 5.0f);
                
                localScore += (ampScore * 0.6f + timbreScore * 0.4f);
            }
            return localScore;
        }));
    }
    
    float totalScore = 0.0f;
    for (auto& f : futures) {
        totalScore += f.get();
    }
    
    return (totalScore / numWindows) * 100.0f;
}

// ============================================================================ 
// Main Analyzer
// ============================================================================ 
int main(int argc, char** argv) {
    std::cout << "=== ChiptuneTracker Debug Analyzer (Multithreaded) ===\n";
    
    std::string refPath = "C:\\Users\\zerav\\Documents\\python scripts\\chiptune_music_maker\\Kavinsky_nightcall.mp3";
    if (argc > 1) refPath = argv[1];
    
    std::cout << "Loading reference: " << refPath << "...\n";
    
    int refRate = 0;
    std::vector<float> refSamples;
    if (!loadWavFile(refPath, refRate, refSamples)) {
        std::cerr << "Error: Could not load reference file. (Ensure miniaudio supports MP3 or convert to WAV)\n";
        return 1;
    }
    std::cout << "Loaded " << refSamples.size() << " samples @ " << refRate << "Hz\n";
    normalize(refSamples);
    
    // ======================================================================== 
    // Generate Track
    // ======================================================================== 
    std::cout << "Synthesizing 'Nightcall' recreation (Parallel Channels)...";
    
    const int NUM_CHANNELS = 5;
    
    // Collect notes for scheduling
    int bpm = 92;
    float samplesPerBeat = (refRate * 60.0f) / bpm;
    int totalBeats = 16;
    int totalSamples = static_cast<int>(totalBeats * samplesPerBeat);
    
    struct ScheduledNote {
        float startTime; // in samples
        float duration;  // in samples
        int channel;
        TrackNote note;
    };
    std::vector<ScheduledNote> schedule;
    
    int noteCount = sizeof(g_SynthwaveNightcall) / sizeof(TrackNote);
    for (int i = 0; i < noteCount; ++i) {
        const auto& tn = g_SynthwaveNightcall[i];
        int ch = 4;
        if (isDrumType(tn.osc)) ch = 0;
        else if (tn.osc == OscillatorType::KavinskyBass || tn.osc == OscillatorType::SynthwaveBass) ch = 1;
        else if (tn.osc == OscillatorType::SynthwavePad) ch = 3;
        else ch = 2; // Lead
        
        ScheduledNote sn;
        sn.startTime = tn.beat * samplesPerBeat;
        sn.duration = tn.duration * samplesPerBeat;
        sn.channel = ch;
        sn.note = tn;
        schedule.push_back(sn);
    }
    
    // Parallel Render per Channel
    std::vector<std::future<std::vector<float>>> channelFutures;
    
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        channelFutures.push_back(std::async(std::launch::async, [=, &schedule]() {
            Synthesizer synth;
            synth.setSampleRate((float)refRate);
            
            // Apply basic config based on channel
            // (Note: Simplified here compared to main app, but captures essence)
            
            std::vector<float> buffer(totalSamples, 0.0f);
            int samplesDone = 0;
            const int blockSize = 256;
            
            while (samplesDone < totalSamples) {
                // Trigger notes for this channel
                for (const auto& sn : schedule) {
                    if (sn.channel == ch) {
                        if (sn.startTime >= samplesDone && sn.startTime < samplesDone + blockSize) {
                            synth.noteOn(sn.note.pitch, sn.note.velocity, 0.0f,
                                0.01f, 0.01f, sn.note.duration, sn.note.osc);
                        }
                        if (sn.startTime + sn.duration >= samplesDone && sn.startTime + sn.duration < samplesDone + blockSize) {
                            synth.noteOff(sn.note.pitch, 0.0f);
                        }
                    }
                }
                
                // Process
                for (int i = 0; i < blockSize && samplesDone < totalSamples; ++i) {
                    buffer[samplesDone++] = synth.process((float)samplesDone / refRate);
                }
            }
            return buffer;
        }));
    }
    
    // Mix channels
    std::vector<float> genSamples(totalSamples, 0.0f);
    for (auto& f : channelFutures) {
        std::vector<float> chBuffer = f.get();
        for (size_t i = 0; i < genSamples.size(); ++i) {
            genSamples[i] += chBuffer[i];
        }
    }
    
    normalize(genSamples);
    
    // ======================================================================== 
    // Compare
    // ======================================================================== 
    std::cout << "Comparing...\n";
    
    size_t cmpLen = std::min(refSamples.size(), genSamples.size());
    std::vector<float> refTrim(refSamples.begin(), refSamples.begin() + cmpLen);
    std::vector<float> genTrim(genSamples.begin(), genSamples.begin() + cmpLen);
    
    float match = calculateMatch(refTrim, refRate, genTrim, refRate);
    
    std::cout << "\n========================================\n";
    std::cout << "MATCH RESULT: " << std::fixed << std::setprecision(1) << match << "%\n";
    std::cout << "========================================\n";
    
    if (match > 80.0f) std::cout << "Status: EXCELLENT MATCH\n";
    else if (match > 50.0f) std::cout << "Status: GOOD MATCH (Timbre/Timing needs work)\n";
    else std::cout << "Status: POOR MATCH (Check Tempo, Key, or Instruments)\n";
    
    return 0;
}