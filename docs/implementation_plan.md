# ChiptuneTracker - Feature Implementation Plan
**Date:** 2025-12-30
**Status:** Architectural Design & Implementation Roadmap

---

## Phase 1: Export/Interoperability

### 1.1 MIDI Export ⭐⭐⭐⭐⭐

#### Research Summary
- **MIDI Format:** Standard MIDI File (SMF) Format 1 (multitrack)
- **Library:** [craigsapp/midifile](https://github.com/craigsapp/midifile) - widely used, actively maintained
- **PPQ Resolution:** 480 PPQN (pulses per quarter note) - modern standard, balances accuracy and compatibility
- **Velocity Mapping:** ChiptuneTracker uses 0.0-1.0, MIDI uses 0-127
  - Standard dynamics: ppp=16, pp=32, p=48, mp=64, mf=80, f=96, ff=112, fff=127
  - Linear mapping: `midiVelocity = (int)(noteVelocity * 127)`

#### Technical Specification

**Data Flow:**
```
Project (Types.h)
  ├─ bpm, beatsPerMeasure
  ├─ channels[8] (ChannelConfig with oscillatorType)
  ├─ patterns (vector<Pattern>)
  └─ arrangement (vector<Clip>)
       ↓
    MIDI Export Algorithm
       ↓
   .mid file (SMF Format 1)
     ├─ Track 0: Tempo/Time Signature
     ├─ Track 1-8: MIDI channels (one per ChiptuneTracker channel)
     └─ Note On/Off events with velocity
```

**Implementation Steps:**

1. **Add midifile library**
   ```cpp
   // vendor/midifile/ (git submodule or copy)
   #include "MidiFile.h"
   ```

2. **Create MIDIExport.h**
   ```cpp
   namespace ChiptuneTracker {

   class MIDIExporter {
   public:
       static bool exportToMIDI(const Project& project, const std::string& filepath);

   private:
       static void addTempoTrack(smf::MidiFile& midi, float bpm);
       static void addChannelTrack(smf::MidiFile& midi, int trackNum,
                                    const Pattern& pattern,
                                    const ChannelConfig& channel,
                                    float bpm);
       static int velocityFloatToMIDI(float velocity);
       static int beatsToTicks(float beats, int ppq);
       static uint8_t oscillatorToGMProgram(OscillatorType osc);
   };

   } // namespace ChiptuneTracker
   ```

3. **Mapping ChiptuneTracker Instruments to General MIDI**
   ```cpp
   // General MIDI Program Change mapping
   uint8_t oscillatorToGMProgram(OscillatorType osc) {
       switch (osc) {
           // Lead Synths -> GM Lead (80-87)
           case OscillatorType::SynthLead:
           case OscillatorType::SynthwaveLead: return 81; // Lead 2 (sawtooth)

           // Pads -> GM Pad (88-95)
           case OscillatorType::SynthPad:
           case OscillatorType::SynthwavePad: return 91; // Pad 4 (choir)

           // Bass -> GM Bass (32-39)
           case OscillatorType::SynthBass:
           case OscillatorType::SynthwaveBass:
           case OscillatorType::SubBass808:
           case OscillatorType::AcidBass:
           case OscillatorType::ReggaetonBass: return 39; // Synth Bass 2

           // Pluck -> GM Guitar (24-31)
           case OscillatorType::SynthPluck: return 26; // Electric Guitar (jazz)

           // Organ -> GM Organ (16-23)
           case OscillatorType::SynthOrgan: return 16; // Drawbar Organ

           // Brass -> GM Brass (56-63)
           case OscillatorType::SynthBrass:
           case OscillatorType::LatinBrass: return 62; // Synth Brass 1

           // Strings -> GM Strings (48-55)
           case OscillatorType::SynthStrings: return 51; // Synth Strings 1

           // Basic Waveforms -> GM Synth Lead
           case OscillatorType::Pulse:
           case OscillatorType::Sawtooth:
           case OscillatorType::Triangle:
           case OscillatorType::Sine: return 80; // Lead 1 (square)

           // Drums (MIDI Channel 10, note-based percussion)
           // GM Percussion is note-based: Kick=36, Snare=38, etc.
           default: return 80; // Default to Lead 1
       }
   }
   ```

4. **Drum Mapping (MIDI Channel 10 - GM Percussion)**
   ```cpp
   // Map ChiptuneTracker drum oscillators to GM Percussion notes
   uint8_t drumTypeToMIDINote(OscillatorType drum) {
       switch (drum) {
           case OscillatorType::Kick:
           case OscillatorType::Kick808:
           case OscillatorType::Dembow808: return 36; // Bass Drum 1
           case OscillatorType::KickHard: return 35;  // Acoustic Bass Drum

           case OscillatorType::Snare:
           case OscillatorType::Snare808:
           case OscillatorType::DembowSnare: return 38; // Acoustic Snare
           case OscillatorType::SnareRim: return 37;     // Side Stick

           case OscillatorType::HiHat:
           case OscillatorType::HiHatPedal: return 42;   // Closed Hi-Hat
           case OscillatorType::HiHatOpen: return 46;    // Open Hi-Hat

           case OscillatorType::Tom: return 47;          // Low-Mid Tom
           case OscillatorType::TomLow: return 41;       // Low Floor Tom
           case OscillatorType::TomHigh: return 50;      // High Tom

           case OscillatorType::Crash: return 49;        // Crash Cymbal 1
           case OscillatorType::Ride: return 51;         // Ride Cymbal 1

           case OscillatorType::Clap: return 39;         // Hand Clap
           case OscillatorType::Cowbell: return 56;      // Cowbell
           case OscillatorType::Clave: return 75;        // Claves
           case OscillatorType::Conga: return 64;        // Low Conga
           case OscillatorType::Bongo: return 61;        // Low Bongo
           case OscillatorType::Tambourine: return 54;   // Tambourine
           case OscillatorType::Maracas: return 70;      // Maracas

           default: return 60; // Mid note as fallback
       }
   }
   ```

5. **Export Algorithm**
   ```cpp
   bool MIDIExporter::exportToMIDI(const Project& project, const std::string& filepath) {
       smf::MidiFile midifile;
       midifile.setTPQ(480); // 480 PPQN

       // Track 0: Tempo and time signature
       midifile.addTrack();
       addTempoTrack(midifile, project.bpm);

       // Tracks 1-8: One track per channel
       for (int ch = 0; ch < Project::MAX_CHANNELS; ch++) {
           midifile.addTrack();

           // Find all clips for this channel in arrangement
           std::vector<const Clip*> channelClips;
           for (const Clip& clip : project.arrangement) {
               if (clip.channelIndex == ch) {
                   channelClips.push_back(&clip);
               }
           }

           // Export each clip (pattern instance on timeline)
           for (const Clip* clip : channelClips) {
               const Pattern& pattern = project.patterns[clip->patternIndex];
               float clipStartBeat = clip->startBeat;

               // Add notes from this pattern
               for (const Note& note : pattern.notes) {
                   float absoluteBeat = clipStartBeat + note.startTime;
                   int tickOn = beatsToTicks(absoluteBeat, 480);
                   int tickOff = beatsToTicks(absoluteBeat + note.duration, 480);
                   int velocity = velocityFloatToMIDI(note.velocity);

                   // MIDI channel (0-based, but channel 9 is drums in GM)
                   int midiChannel = ch;

                   // Determine if this is a drum channel
                   bool isDrum = isDrumOscillator(note.oscillatorType);
                   if (isDrum) {
                       midiChannel = 9; // MIDI channel 10 (0-indexed = 9)
                       int drumNote = drumTypeToMIDINote(note.oscillatorType);
                       midifile.addNoteOn(ch + 1, tickOn, midiChannel, drumNote, velocity);
                       midifile.addNoteOff(ch + 1, tickOff, midiChannel, drumNote);
                   } else {
                       // Melodic instrument
                       uint8_t program = oscillatorToGMProgram(note.oscillatorType);
                       midifile.addPatchChange(ch + 1, 0, midiChannel, program);
                       midifile.addNoteOn(ch + 1, tickOn, midiChannel, note.pitch, velocity);
                       midifile.addNoteOff(ch + 1, tickOff, midiChannel, note.pitch);
                   }
               }
           }
       }

       midifile.sortTracks(); // Ensure events are in chronological order
       midifile.write(filepath);
       return true;
   }
   ```

6. **UI Integration (FileIO.h)**
   ```cpp
   // Add export function to FileIO.h
   inline bool exportProjectToMIDI(const Project& project, const std::string& filepath) {
       return MIDIExporter::exportToMIDI(project, filepath);
   }

   // Add to File menu in UI.h
   if (ImGui::MenuItem("Export to MIDI...")) {
       std::string filepath = openFileSaveDialog("midi");
       if (!filepath.empty()) {
           exportProjectToMIDI(project, filepath);
       }
   }
   ```

**Testing Plan:**
- Export simple 4-bar beat (kick, snare, hi-hat)
- Open in FL Studio / Ableton / MuseScore
- Verify tempo, time signature, note positions, velocities
- Test multi-channel export (8 channels)
- Test long arrangement (4+ minutes)

**References:**
- [MIDI SMF Specification - Stanford](https://ccrma.stanford.edu/~craig/14q/midifile/MidiFileFormat.html)
- [craigsapp/midifile library](https://github.com/craigsapp/midifile)
- [MIDI PPQ/Tempo Guide](http://midi.teragonaudio.com/tech/midifile/ppqn.htm)
- [MIDI Velocity Dynamics](https://www.soundonsound.com/techniques/midi-dynamics)

---

### 1.2 Sample Import (WAV/MP3/OGG) ⭐⭐⭐⭐

#### Research Summary
- **Audio Decoding:** miniaudio already included for playback - can use `ma_decoder` for loading samples
- **Formats:** WAV (uncompressed), MP3 (lossy), OGG/Vorbis (lossy)
- **Integration:** Extend `OscillatorType` to include sample playback mode

#### Technical Specification

**Architecture:**
```
Sample Library
  ├─ Sample struct (audio buffer + metadata)
  ├─ SamplePool (manages loaded samples)
  └─ SampleOscillator (playback engine)

Integration:
  Note.oscillatorType = OscillatorType::Sample
  Note.sampleID = <index into SamplePool>
```

**Implementation Steps:**

1. **Add Sample struct to Types.h**
   ```cpp
   struct Sample {
       std::string name;
       std::string filepath;
       std::vector<float> audioData; // Interleaved stereo or mono
       int sampleRate;
       int channels; // 1=mono, 2=stereo
       float lengthSeconds;

       // Playback settings
       bool loop = false;
       float loopStart = 0.0f; // Seconds
       float loopEnd = 0.0f;   // 0 = end of sample

       // Metadata
       int rootNote = 60; // C4 - for pitch-shifting samples
   };

   struct SamplePool {
       static constexpr int MAX_SAMPLES = 256;
       std::vector<Sample> samples;

       int loadSample(const std::string& filepath);
       const Sample* getSample(int id) const;
   };
   ```

2. **Add sample loading using miniaudio**
   ```cpp
   int SamplePool::loadSample(const std::string& filepath) {
       ma_decoder decoder;
       ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 48000);

       if (ma_decoder_init_file(filepath.c_str(), &config, &decoder) != MA_SUCCESS) {
           return -1; // Failed
       }

       Sample sample;
       sample.filepath = filepath;
       sample.sampleRate = decoder.outputSampleRate;
       sample.channels = decoder.outputChannels;

       // Read entire file into buffer
       ma_uint64 frameCount = 0;
       ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
       sample.audioData.resize(frameCount * decoder.outputChannels);
       ma_decoder_read_pcm_frames(&decoder, sample.audioData.data(), frameCount, nullptr);

       sample.lengthSeconds = (float)frameCount / sample.sampleRate;
       sample.loopEnd = sample.lengthSeconds;

       ma_decoder_uninit(&decoder);

       samples.push_back(sample);
       return (int)samples.size() - 1; // Return sample ID
   }
   ```

3. **Add SampleOscillator to Synthesizer.h**
   ```cpp
   class SampleOscillator {
   public:
       void trigger(const Sample* sample, float pitch, float velocity) {
           m_sample = sample;
           m_position = 0.0;
           m_playing = true;
           m_velocity = velocity;

           // Calculate pitch shift ratio (for pitched samples)
           float semitones = pitch - sample->rootNote;
           m_pitchRatio = std::pow(2.0f, semitones / 12.0f);
       }

       float process() {
           if (!m_playing || !m_sample) return 0.0f;

           // Linear interpolation for pitch-shifted playback
           int pos = (int)m_position;
           float frac = m_position - pos;

           if (pos >= (int)m_sample->audioData.size() - 1) {
               if (m_sample->loop) {
                   m_position = m_sample->loopStart * m_sample->sampleRate;
               } else {
                   m_playing = false;
                   return 0.0f;
               }
           }

           float s0 = m_sample->audioData[pos];
           float s1 = m_sample->audioData[pos + 1];
           float output = s0 + frac * (s1 - s0);

           m_position += m_pitchRatio; // Advance playback position
           return output * m_velocity;
       }

   private:
       const Sample* m_sample = nullptr;
       double m_position = 0.0;
       float m_pitchRatio = 1.0f;
       float m_velocity = 1.0f;
       bool m_playing = false;
   };
   ```

4. **UI - Sample Browser**
   ```cpp
   // New window: Sample Browser
   void DrawSampleBrowser(Project& project, SamplePool& samplePool) {
       ImGui::Begin("Sample Browser");

       if (ImGui::Button("Import Sample...")) {
           std::string path = openFileDialog("wav,mp3,ogg");
           if (!path.empty()) {
               int id = samplePool.loadSample(path);
               // Add to sample palette
           }
       }

       // Display loaded samples as palette
       for (int i = 0; i < samplePool.samples.size(); i++) {
           const Sample& sample = samplePool.samples[i];
           if (ImGui::Selectable(sample.name.c_str())) {
               // Set current sound to this sample
               ui.currentSound = sample.name;
               ui.currentSampleID = i;
           }
       }

       ImGui::End();
   }
   ```

5. **Extend Note struct**
   ```cpp
   struct Note {
       // ...existing fields...
       int sampleID = -1; // -1 = use oscillatorType, >=0 = use sample from pool
   };
   ```

**Testing:**
- Import 808 kick.wav
- Place on piano roll
- Verify pitch-shifting works (C4 plays at original pitch, C5 plays 1 octave higher)
- Test looping samples
- Test memory management (loading 50+ samples)

**References:**
- [miniaudio documentation](https://miniaud.io/)
- Modern DAW sample handling (Ableton, FL Studio Sampler)

---

### 1.3 MIDI Input Recording ⭐⭐⭐

#### Research Summary
- **Library:** RtMidi (cross-platform) or use miniaudio's MIDI support (if available)
- **Alternative:** libremidi (modern C++, header-only capable)
- **Recording Modes:** Overdub (add to existing notes) vs Replace (clear pattern first)
- **Quantization:** Snap to grid (1/4, 1/8, 1/16, 1/32, none)

#### Technical Specification

**Architecture:**
```
MIDI Input Device
  ↓
RtMidi / libremidi
  ↓
MIDI Event Queue (thread-safe ring buffer)
  ↓
Sequencer (processes during playback/recording)
  ↓
Pattern (notes added in real-time)
```

**Implementation Steps:**

1. **Add libremidi library**
   ```cpp
   // vendor/libremidi/ (header-only)
   #include <libremidi/libremidi.hpp>
   ```

2. **Create MIDIInput.h**
   ```cpp
   namespace ChiptuneTracker {

   struct MIDIInputEvent {
       enum Type { NoteOn, NoteOff, ControlChange };
       Type type;
       uint8_t note;
       uint8_t velocity;
       double timestamp; // Seconds since recording started
   };

   class MIDIInput {
   public:
       bool initialize();
       void startRecording(float startBeat);
       void stopRecording();
       std::vector<MIDIInputEvent> pollEvents();

   private:
       libremidi::midi_in midiIn;
       std::vector<MIDIInputEvent> eventQueue;
       double recordingStartTime = 0.0;
       bool recording = false;
   };

   } // namespace ChiptuneTracker
   ```

3. **Recording Logic**
   ```cpp
   // In Sequencer, during playback
   void Sequencer::update(float deltaTime) {
       if (m_recording) {
           auto events = m_midiInput.pollEvents();
           for (const auto& event : events) {
               if (event.type == MIDIInputEvent::NoteOn) {
                   // Convert timestamp to beats
                   float beat = event.timestamp * (m_bpm / 60.0f);

                   // Apply quantization
                   if (m_quantize > 0.0f) {
                       beat = std::round(beat / m_quantize) * m_quantize;
                   }

                   // Create note
                   Note note;
                   note.pitch = event.note;
                   note.velocity = event.velocity / 127.0f;
                   note.startTime = beat;
                   note.duration = 1.0f; // Will be updated on NoteOff
                   note.oscillatorType = m_currentOscillatorType;

                   m_recordingNotes[event.note] = note;
               }
               else if (event.type == MIDIInputEvent::NoteOff) {
                   if (m_recordingNotes.count(event.note)) {
                       Note& note = m_recordingNotes[event.note];
                       float beat = event.timestamp * (m_bpm / 60.0f);
                       note.duration = beat - note.startTime;

                       // Add to pattern
                       m_currentPattern.notes.push_back(note);
                       m_recordingNotes.erase(event.note);
                   }
               }
           }
       }
   }
   ```

4. **UI Controls**
   ```cpp
   // In Piano Roll toolbar
   if (ImGui::Button(recording ? "Stop Recording" : "Record (R)")) {
       if (!recording) {
           sequencer.startMIDIRecording();
       } else {
           sequencer.stopMIDIRecording();
       }
   }

   // Quantization selector
   const char* quantizeOptions[] = {"Off", "1/4", "1/8", "1/16", "1/32"};
   ImGui::Combo("Quantize", &quantizeIndex, quantizeOptions, 5);
   ```

**Testing:**
- Connect USB MIDI keyboard
- Record simple melody
- Verify timing accuracy
- Test quantization modes
- Test overdub vs replace

---

## Phase 2: Production Quality

### 2.1 Master Bus Effects Chain ⭐⭐⭐⭐

**Architecture:**
```
All channels → Mixer → Master Bus Effects → Audio Output
                          ├─ Parametric EQ
                          ├─ Compressor
                          ├─ Limiter
                          └─ Stereo Imager
```

**Implementation:**
```cpp
struct MasterFX {
    ParametricEQ eq;
    Compressor compressor;
    Limiter limiter;
    StereoImager imager;

    void process(float& left, float& right) {
        eq.process(left, right);
        compressor.process(left, right);
        imager.process(left, right);
        limiter.process(left, right);
    }
};
```

---

### 2.2 Parametric EQ ⭐⭐⭐⭐

**DSP Research:**
- Biquad filter implementation (standard for audio EQ)
- Filter types: Bell (peaking), Low Shelf, High Shelf, Notch
- Parameters: Frequency, Gain (dB), Q (bandwidth)

**Implementation:**
```cpp
class ParametricEQ {
public:
    struct Band {
        enum Type { Bell, LowShelf, HighShelf, Notch };
        Type type = Bell;
        float frequency = 1000.0f; // Hz
        float gain = 0.0f;         // dB (-12 to +12)
        float q = 1.0f;            // 0.1 to 10.0
        bool enabled = false;

        BiquadFilter filter;
    };

    static constexpr int NUM_BANDS = 8;
    std::array<Band, NUM_BANDS> bands;

    void process(float& left, float& right) {
        for (auto& band : bands) {
            if (band.enabled) {
                left = band.filter.process(left);
                right = band.filter.process(right);
            }
        }
    }
};
```

**Biquad Filter Math:**
```cpp
class BiquadFilter {
private:
    float b0, b1, b2, a1, a2;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

public:
    void setPeaking(float freq, float gain, float q, float sampleRate) {
        float A = std::pow(10.0f, gain / 40.0f);
        float omega = 2.0f * M_PI * freq / sampleRate;
        float sn = std::sin(omega);
        float cs = std::cos(omega);
        float alpha = sn / (2.0f * q);

        b0 = 1.0f + alpha * A;
        b1 = -2.0f * cs;
        b2 = 1.0f - alpha * A;
        a1 = -2.0f * cs;
        a2 = 1.0f - alpha / A;

        // Normalize
        float a0 = 1.0f + alpha / A;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }

    float process(float input) {
        float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = input;
        y2 = y1; y1 = output;
        return output;
    }
};
```

**Reference:**
- [Biquad Filter Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html)

---

### 2.3 Compressor Effect ⭐⭐⭐

**DSP Theory:**
- Threshold: Level above which compression starts
- Ratio: Amount of gain reduction (4:1 = 4dB input becomes 1dB output above threshold)
- Attack: How fast compressor reacts (1-100ms)
- Release: How fast compressor recovers (50-500ms)
- Makeup Gain: Compensate for reduced volume

**Implementation:**
```cpp
class Compressor {
public:
    float threshold = -20.0f;  // dB
    float ratio = 4.0f;        // 1.0 to 20.0
    float attack = 0.005f;     // seconds
    float release = 0.1f;      // seconds
    float makeupGain = 0.0f;   // dB
    float kneeWidth = 6.0f;    // dB (soft knee)

    void process(float& left, float& right, float sampleRate) {
        // Convert to dB
        float inputLevel = 20.0f * std::log10(std::max(std::abs(left), std::abs(right)));

        // Calculate gain reduction
        float gainReduction = 0.0f;
        if (inputLevel > threshold) {
            // Soft knee
            float overshoot = inputLevel - threshold;
            if (overshoot < kneeWidth / 2.0f) {
                overshoot = overshoot * overshoot / (2.0f * kneeWidth);
            } else {
                overshoot -= kneeWidth / 4.0f;
            }
            gainReduction = overshoot * (1.0f - 1.0f / ratio);
        }

        // Smooth gain reduction (attack/release)
        float coeff = (gainReduction > m_envelope) ?
                      std::exp(-1.0f / (attack * sampleRate)) :
                      std::exp(-1.0f / (release * sampleRate));
        m_envelope = gainReduction + coeff * (m_envelope - gainReduction);

        // Apply gain reduction + makeup gain
        float gain = std::pow(10.0f, (-m_envelope + makeupGain) / 20.0f);
        left *= gain;
        right *= gain;
    }

private:
    float m_envelope = 0.0f;
};
```

---

### 2.4 Spectrum Analyzer ⭐⭐⭐⭐

**FFT Research:**
- Use FFT for frequency domain analysis
- 2048-8192 point FFT (balance between resolution and performance)
- Hanning window to reduce spectral leakage
- Update rate: 30-60 FPS for smooth visualization

**Implementation:**
```cpp
class SpectrumAnalyzer {
public:
    static constexpr int FFT_SIZE = 4096;

    void analyze(const float* audioBuffer, int numSamples) {
        // Apply Hanning window
        for (int i = 0; i < FFT_SIZE; i++) {
            float window = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / FFT_SIZE));
            m_fftInput[i] = audioBuffer[i] * window;
        }

        // Perform FFT (use library like KissFFT or FFTW)
        fft(m_fftInput, m_fftOutput, FFT_SIZE);

        // Convert to magnitude spectrum
        for (int i = 0; i < FFT_SIZE / 2; i++) {
            float re = m_fftOutput[i * 2];
            float im = m_fftOutput[i * 2 + 1];
            m_magnitudes[i] = 20.0f * std::log10(std::sqrt(re * re + im * im) + 1e-10f);
        }
    }

    void drawSpectrum(ImVec2 size) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();

        // Draw frequency bars
        for (int i = 0; i < 512; i++) {
            float freq = (i / 512.0f) * 24000.0f; // Up to 24kHz
            float mag = m_magnitudes[i * (FFT_SIZE / 2) / 512];
            float height = (mag + 80.0f) / 80.0f * size.y; // -80dB to 0dB range

            ImU32 color = IM_COL32(0, 200, 255, 255);
            drawList->AddRectFilled(
                ImVec2(pos.x + i * size.x / 512, pos.y + size.y - height),
                ImVec2(pos.x + (i + 1) * size.x / 512, pos.y + size.y),
                color
            );
        }
    }

private:
    float m_fftInput[FFT_SIZE];
    float m_fftOutput[FFT_SIZE];
    float m_magnitudes[FFT_SIZE / 2];
};
```

**Library Options:**
- KissFFT (small, BSD license)
- FFTW (fastest, GPL license - may be licensing issue)
- pffft (permissive license, good performance)

---

## Phase 3: Workflow Enhancement

### 3.1 Automation Curves ⭐⭐⭐⭐⭐

**UI Design:**
- Automation lanes below piano roll
- Bezier curve editing (like FL Studio)
- Parameters: Volume, Pan, Filter Cutoff, Reverb Mix, etc.

**Data Structure:**
```cpp
struct AutomationPoint {
    float time;   // Beats
    float value;  // 0.0 to 1.0 (normalized)
    float curve;  // -1.0 (exponential) to +1.0 (logarithmic)
};

struct AutomationLane {
    enum Parameter {
        Volume, Pan, FilterCutoff, FilterResonance,
        ReverbMix, DelayMix, ChorusMix, DistortionDrive
    };
    Parameter parameter;
    std::vector<AutomationPoint> points;

    float interpolate(float time) const {
        // Find surrounding points and interpolate with curve
        // ...
    }
};
```

---

### 3.2 Swing/Groove Quantization ⭐⭐⭐⭐

**Already Partially Implemented!**
Check Types.h:386-390:
```cpp
float swing = 0.0f;             // Swing amount: 0.0 = no swing, 1.0 = max swing (triplet feel)
float swingGrid = 0.5f;         // Grid division for swing (0.5 = 8th notes, 0.25 = 16th notes)
bool humanize = false;          // Add random timing variation
float humanizeAmount = 0.02f;   // Humanize timing variation (beats)
float humanizeVelocity = 0.1f;  // Humanize velocity variation (0.0 to 1.0)
```

**Need to Implement:**
- Apply swing during playback in Synthesizer
- UI controls in mixer/transport section

---

### 3.3 Pattern Arranger View ⭐⭐⭐

**Design:**
- Horizontal timeline (like DAWs)
- Drag patterns to timeline positions
- Visual song structure (intro, verse, chorus, bridge, outro)

**Already Partially Implemented!**
Check Types.h:361-371:
```cpp
struct Clip {
    int patternIndex = 0;       // Which pattern to play
    int channelIndex = 0;       // Which channel
    float startBeat = 0.0f;     // Position on timeline
    float lengthBeats = 16.0f;  // Length (can be different from pattern)
    uint32_t color = 0xFF4488FF;
};
```

**Need to Implement:**
- Visual arranger window
- Drag-and-drop clips
- Timeline playback (already have Clip support)

---

## Phase 4: Sound Design

### 4.1 Flanger Effect ⭐⭐⭐

**DSP:**
```cpp
class Flanger {
public:
    float rate = 0.5f;        // LFO rate (Hz)
    float depth = 0.01f;      // Delay modulation depth (seconds)
    float feedback = 0.5f;    // -0.95 to +0.95
    float mix = 0.5f;         // 0.0 to 1.0

    void process(float& left, float& right, float sampleRate) {
        // LFO
        float lfo = std::sin(m_phase * TWO_PI);
        m_phase += rate / sampleRate;
        if (m_phase >= 1.0f) m_phase -= 1.0f;

        // Variable delay (1-10ms typically)
        float delayTime = 0.005f + depth * (lfo + 1.0f) * 0.5f;
        int delaySamples = (int)(delayTime * sampleRate);

        // Read from delay buffer
        int readPos = (m_writePos - delaySamples + BUFFER_SIZE) % BUFFER_SIZE;
        float delayed = m_delayBuffer[readPos];

        // Feedback
        m_delayBuffer[m_writePos] = left + delayed * feedback;
        m_writePos = (m_writePos + 1) % BUFFER_SIZE;

        // Mix
        left = left * (1.0f - mix) + delayed * mix;
    }

private:
    static constexpr int BUFFER_SIZE = 96000; // 2 seconds at 48kHz
    float m_delayBuffer[BUFFER_SIZE] = {};
    int m_writePos = 0;
    float m_phase = 0.0f;
};
```

---

### 4.2 Wavetable Editor ⭐⭐⭐

**UI Design:**
- Visual waveform drawing
- 256 samples per wavetable
- Morphing between multiple wavetables
- Presets: Saw, Square, Sine, Triangle, + custom shapes

**Implementation:**
```cpp
struct Wavetable {
    static constexpr int TABLE_SIZE = 256;
    std::array<float, TABLE_SIZE> samples;

    void drawWave(std::function<float(float)> func) {
        for (int i = 0; i < TABLE_SIZE; i++) {
            float t = (float)i / TABLE_SIZE;
            samples[i] = func(t);
        }
    }
};

class WavetableOscillator {
public:
    void setWavetable(const Wavetable& table) {
        m_table = &table;
    }

    float process(float frequency, float sampleRate) {
        // Linear interpolation for smooth playback
        int idx = (int)m_phase;
        float frac = m_phase - idx;
        float s0 = m_table->samples[idx % Wavetable::TABLE_SIZE];
        float s1 = m_table->samples[(idx + 1) % Wavetable::TABLE_SIZE];

        m_phase += Wavetable::TABLE_SIZE * frequency / sampleRate;
        if (m_phase >= Wavetable::TABLE_SIZE) {
            m_phase -= Wavetable::TABLE_SIZE;
        }

        return s0 + frac * (s1 - s0);
    }

private:
    const Wavetable* m_table = nullptr;
    float m_phase = 0.0f;
};
```

---

### 4.3 Voice-to-Note Tool ⭐⭐⭐⭐

**Pitch Detection Algorithms:**
1. **YIN Algorithm** (recommended - best for voice/monophonic)
2. **Autocorrelation** (fast, less accurate)
3. **FFT-based** (good for complex sounds)

**Implementation Plan:**
```cpp
class VoiceToNoteConverter {
public:
    struct DetectedNote {
        int midiNote;
        float confidence; // 0.0 to 1.0
        float startTime;
        float duration;
    };

    std::vector<DetectedNote> analyze(const float* audioBuffer,
                                      int numSamples,
                                      float sampleRate,
                                      bool drumMode = false);

private:
    float detectPitch_YIN(const float* buffer, int size, float sampleRate);
    bool detectOnset(const float* buffer, int size); // For rhythm detection
    int classifyDrum(const float* buffer, int size); // Kick vs Snare vs HiHat
};
```

**YIN Algorithm:**
- Best pitch detection for voice/monophonic instruments
- 99%+ accuracy on clean vocals
- Paper: "YIN, a fundamental frequency estimator for speech and music" (2002)

**Onset Detection (for rhythm/drums):**
- Spectral flux method
- Energy-based detection
- Classify as kick (low freq), snare (mid-high), hi-hat (high freq noise)

---

## Implementation Schedule

**Week 1-2: Phase 1 (Critical)**
- MIDI Export (3-4 days)
- Sample Import (3-4 days)
- MIDI Input Recording (2-3 days)

**Week 3-4: Phase 2 (Production)**
- Master Bus Effects (2 days)
- Parametric EQ (2-3 days)
- Compressor (2 days)
- Spectrum Analyzer (2-3 days)

**Week 5-6: Phase 3 (Workflow)**
- Automation Curves (4-5 days)
- Finish Swing/Groove (1 day - mostly done)
- Pattern Arranger View (2-3 days)

**Week 7-8: Phase 4 (Sound Design)**
- Flanger Effect (1 day)
- Wavetable Editor (3-4 days)
- Voice-to-Note Tool (4-5 days)

**Total: ~8 weeks of development**

---

## References

**MIDI:**
- [MIDI SMF Spec](https://ccrma.stanford.edu/~craig/14q/midifile/MidiFileFormat.html)
- [craigsapp/midifile](https://github.com/craigsapp/midifile)
- [libremidi](https://github.com/celtera/libremidi)

**DSP:**
- [Biquad Filter Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html)
- [YIN Pitch Detection](http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf)

**Synthwave Production:**
- [7 Must-Have Plugins](https://synthwavepro.com/7-must-have-plugins-for-synthwave-music-producers/)
- [Production Essentials](https://modeaudio.com/magazine/synthwave-5-production-essentials)

**Modern Trackers:**
- [Furnace](https://tildearrow.org/furnace/)
- [Bintracker](https://bintracker.org/)
- [Renoise](https://www.renoise.com/)
