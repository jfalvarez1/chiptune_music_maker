#pragma once

/*
 * ChiptuneTracker - MIDI Export
 *
 * Exports ChiptuneTracker projects to Standard MIDI File (SMF) Format 1
 * - Multi-track export (one MIDI track per channel)
 * - Tempo and time signature mapping
 * - Velocity conversion (0.0-1.0 float to 0-127 MIDI)
 * - Instrument mapping to General MIDI
 * - Drum mapping to GM Percussion (MIDI Channel 10)
 */

#include "Types.h"
#include "../vendor/midifile/include/MidiFile.h"
#include <algorithm>
#include <cmath>

namespace ChiptuneTracker {

class MIDIExporter {
public:
    // Export entire project to MIDI file
    static bool exportToMIDI(const Project& project, const std::string& filepath) {
        smf::MidiFile midifile;
        midifile.setTPQ(480); // 480 PPQN (Pulses Per Quarter Note)

        // Per-export state, held on the stack. It used to be a file-scope
        // static reset here; a static that outlives one export is the same
        // shape of bug this reset existed to paper over.
        ProgramSentFlags programSent{};

        // Track 0: Tempo and time signature meta events
        midifile.addTrack();
        addTempoTrack(midifile, project);

        // Tracks 1-8: One track per channel
        for (int ch = 0; ch < Project::MAX_CHANNELS; ch++) {
            midifile.addTrack();
            addChannelTrack(midifile, ch + 1, project, ch, programSent);
        }

        midifile.sortTracks(); // Ensure all events are in chronological order
        midifile.write(filepath);
        return true;
    }

private:
    // MIDI has sixteen channels and channel 9 is reserved for percussion.
    static constexpr int MIDI_CHANNEL_COUNT = 16;
    static constexpr int MIDI_DRUM_CHANNEL = 9;

    // Which MIDI channels have already received a program change during THIS
    // export. Passed down rather than held statically: the previous version
    // was a file-scope array indexed by the project channel, which was fine
    // at 8 channels and wrote off the end of it at 32.
    struct ProgramSentFlags {
        bool sent[MIDI_CHANNEL_COUNT] = {false};
    };

    /*
     * Map a project channel onto a MIDI channel.
     *
     * Thirty-two project channels cannot address sixteen MIDI channels
     * distinctly, so they wrap - which is what every DAW does. Channel 9 is
     * skipped because MIDI reserves it for percussion, and a melodic part
     * landing there would play as drums in any other program.
     */
    static int toMidiChannel(int projectChannel) {
        if (projectChannel < 0) return 0;
        const int wrapped = projectChannel % (MIDI_CHANNEL_COUNT - 1);
        return (wrapped >= MIDI_DRUM_CHANNEL) ? wrapped + 1 : wrapped;
    }

    /*
     * Track 0: the tempo and time-signature map.
     *
     * MIDI ticks are musical, not wall-clock - a tick is a fixed fraction of
     * a quarter note and the tempo events tell the player how fast to read
     * them. So a change at beat B goes at tick B * TPQ with no integration;
     * the player does that part.
     *
     * The denominator used to be passed as 2. midifile's `bottom` parameter
     * is the ACTUAL denominator and it takes the base-2 logarithm itself, so
     * every export has been writing x/2 - a half-note pulse - and any DAW
     * importing one saw bars twice the intended length. It is the real
     * denominator now, and there is a test that reads the bytes back.
     */
    static void addTempoTrack(smf::MidiFile& midi, const Project& project) {
        const int tpq = midi.getTPQ();
        auto tickOf = [tpq](float beat) {
            return static_cast<int>(std::lround(std::max(0.0f, beat) *
                                                static_cast<double>(tpq)));
        };

        // The opening tempo and meter, which is all a project without a map
        // has - and exactly what was written before.
        midi.addTempo(0, 0, static_cast<int>(60000000.0f /
                                             std::max(1.0f, project.bpm)));

        const MeterChange opening = project.meterAt(0.0f);
        midi.addTimeSignature(0, 0, opening.numerator, opening.denominator);

        for (int i = 0; i < project.tempoMap.tempoCount(); ++i) {
            const TempoChange& change = project.tempoMap.tempoAt(i);
            if (change.beat <= 0.0f) continue;   // already written at tick 0
            midi.addTempo(0, tickOf(change.beat),
                          static_cast<int>(60000000.0f / std::max(1.0f, change.bpm)));
        }

        for (int i = 0; i < project.tempoMap.meterCount(); ++i) {
            const MeterChange& change = project.tempoMap.meterAt(i);
            if (change.beat <= 0.0f) continue;
            midi.addTimeSignature(0, tickOf(change.beat),
                                  change.numerator, change.denominator);
        }
    }

    // Add all notes from a channel to its MIDI track
    static void addChannelTrack(smf::MidiFile& midi, int trackNum,
                                 const Project& project, int channelIndex,
                                 ProgramSentFlags& programSent) {
        const ChannelConfig& channel = project.channels[channelIndex];

        // Find all clips for this channel in the arrangement
        std::vector<const Clip*> channelClips;
        for (const Clip& clip : project.arrangement) {
            if (clip.channelIndex == channelIndex) {
                channelClips.push_back(&clip);
            }
        }

        // If no clips in arrangement, export the patterns directly
        if (channelClips.empty() && channelIndex < (int)project.patterns.size()) {
            // Export pattern 0 for this channel (fallback for simple projects)
            if (!project.patterns.empty()) {
                exportPattern(midi, trackNum, project.patterns[0],
                             channel, channelIndex, 0.0f, programSent);
            }
            return;
        }

        // Export each clip
        for (const Clip* clip : channelClips) {
            if (clip->patternIndex >= 0 && clip->patternIndex < (int)project.patterns.size()) {
                const Pattern& pattern = project.patterns[clip->patternIndex];
                exportPattern(midi, trackNum, pattern, channel,
                             channelIndex, clip->startBeat, programSent);
            }
        }
    }

    // Export a single pattern to MIDI track
    static void exportPattern(smf::MidiFile& midi, int trackNum,
                              const Pattern& pattern,
                              const ChannelConfig& channel,
                              int midiChannel, float clipStartBeat,
                              ProgramSentFlags& programSent) {
        for (const Note& note : pattern.notes) {
            // Calculate absolute beat position
            float absoluteBeat = clipStartBeat + note.startTime;
            int tickOn = beatsToTicks(absoluteBeat, 480);
            int tickOff = beatsToTicks(absoluteBeat + note.duration, 480);

            // Convert velocity (0.0-1.0) to MIDI (0-127)
            int velocity = velocityFloatToMIDI(note.velocity);

            // Determine if this is a drum instrument
            bool isDrum = isDrumOscillator(note.oscillatorType);

            if (isDrum) {
                // Drums go to MIDI Channel 10 (0-indexed = channel 9)
                int drumNote = drumTypeToMIDINote(note.oscillatorType);
                midi.addNoteOn(trackNum, tickOn, 9, drumNote, velocity);
                midi.addNoteOff(trackNum, tickOff, 9, drumNote);
            } else {
                // Melodic instruments
                uint8_t program = oscillatorToGMProgram(note.oscillatorType);

                // Add program change at start of track (tick 0)
                // Wrapped into MIDI's sixteen, so this index is in range
                // for any project channel count.
                const int wire = toMidiChannel(midiChannel);
                if (!programSent.sent[wire]) {
                    midi.addPatchChange(trackNum, 0, wire, program);
                    programSent.sent[wire] = true;
                }

                // Add note on/off events
                midi.addNoteOn(trackNum, tickOn, wire, note.pitch, velocity);
                midi.addNoteOff(trackNum, tickOff, wire, note.pitch);
            }
        }
    }

    // Convert beats to MIDI ticks
    static int beatsToTicks(float beats, int ppq) {
        return (int)(beats * ppq);
    }

    // Convert float velocity (0.0-1.0) to MIDI velocity (0-127)
    static int velocityFloatToMIDI(float velocity) {
        int midiVel = (int)(velocity * 127.0f);
        return std::clamp(midiVel, 0, 127);
    }

    // Check if oscillator type is a drum
    static bool isDrumOscillator(OscillatorType osc) {
        switch (osc) {
            case OscillatorType::Kick:
            case OscillatorType::Kick808:
            case OscillatorType::KickHard:
            case OscillatorType::KickSoft:
            case OscillatorType::Snare:
            case OscillatorType::Snare808:
            case OscillatorType::SnareRim:
            case OscillatorType::Clap:
            case OscillatorType::HiHat:
            case OscillatorType::HiHatOpen:
            case OscillatorType::HiHatPedal:
            case OscillatorType::Tom:
            case OscillatorType::TomLow:
            case OscillatorType::TomHigh:
            case OscillatorType::Crash:
            case OscillatorType::Ride:
            case OscillatorType::Cowbell:
            case OscillatorType::Clave:
            case OscillatorType::Conga:
            case OscillatorType::Maracas:
            case OscillatorType::Tambourine:
            case OscillatorType::Bongo:
            case OscillatorType::Timbale:
            case OscillatorType::Guira:
            case OscillatorType::Dembow808:
            case OscillatorType::DembowSnare:
                return true;
            default:
                return false;
        }
    }

    // Map ChiptuneTracker drum types to General MIDI percussion notes
    static uint8_t drumTypeToMIDINote(OscillatorType drum) {
        switch (drum) {
            // Kicks
            case OscillatorType::Kick:
            case OscillatorType::Kick808:
            case OscillatorType::Dembow808:         return 36; // Bass Drum 1
            case OscillatorType::KickHard:          return 35; // Acoustic Bass Drum
            case OscillatorType::KickSoft:          return 36; // Bass Drum 1

            // Snares
            case OscillatorType::Snare:
            case OscillatorType::Snare808:
            case OscillatorType::DembowSnare:       return 38; // Acoustic Snare
            case OscillatorType::SnareRim:          return 37; // Side Stick

            // Hi-Hats
            case OscillatorType::HiHat:
            case OscillatorType::HiHatPedal:        return 42; // Closed Hi-Hat
            case OscillatorType::HiHatOpen:         return 46; // Open Hi-Hat

            // Toms
            case OscillatorType::Tom:               return 47; // Low-Mid Tom
            case OscillatorType::TomLow:            return 41; // Low Floor Tom
            case OscillatorType::TomHigh:           return 50; // High Tom

            // Cymbals
            case OscillatorType::Crash:             return 49; // Crash Cymbal 1
            case OscillatorType::Ride:              return 51; // Ride Cymbal 1

            // Percussion
            case OscillatorType::Clap:              return 39; // Hand Clap
            case OscillatorType::Cowbell:           return 56; // Cowbell
            case OscillatorType::Clave:             return 75; // Claves
            case OscillatorType::Conga:             return 64; // Low Conga
            case OscillatorType::Bongo:             return 61; // Low Bongo
            case OscillatorType::Tambourine:        return 54; // Tambourine
            case OscillatorType::Maracas:           return 70; // Maracas
            case OscillatorType::Timbale:           return 65; // High Timbale
            case OscillatorType::Guira:             return 73; // Short Guiro

            default:                                return 60; // Fallback
        }
    }

    // Map ChiptuneTracker instruments to General MIDI Program Change
    static uint8_t oscillatorToGMProgram(OscillatorType osc) {
        switch (osc) {
            // === Synth Leads (GM 80-87) ===
            case OscillatorType::SynthLead:
            case OscillatorType::SynthwaveLead:
            case OscillatorType::SyncLead:          return 81; // Lead 2 (sawtooth)
            case OscillatorType::TrapLead:          return 80; // Lead 1 (square)
            case OscillatorType::SynthArp:
            case OscillatorType::SynthwaveArp:      return 82; // Lead 3 (calliope)

            // === Synth Pads (GM 88-95) ===
            case OscillatorType::SynthPad:
            case OscillatorType::SynthwavePad:      return 91; // Pad 4 (choir)
            case OscillatorType::GatedPad:          return 90; // Pad 3 (polysynth)
            case OscillatorType::PolySynth:         return 90; // Pad 3 (polysynth)

            // === Bass (GM 32-39) ===
            case OscillatorType::SynthBass:
            case OscillatorType::SynthwaveBass:
            case OscillatorType::AcidBass:
            case OscillatorType::Reese:
            case OscillatorType::ReggaetonBass:     return 39; // Synth Bass 2
            case OscillatorType::SubBass808:        return 38; // Synth Bass 1

            // === Synth FX (GM 96-103) ===
            case OscillatorType::SynthwaveFM:       return 101; // FX 6 (goblins)
            case OscillatorType::Hoover:            return 99;  // FX 4 (atmosphere)

            // === Organ (GM 16-23) ===
            case OscillatorType::SynthOrgan:        return 16; // Drawbar Organ

            // === Brass (GM 56-63) ===
            case OscillatorType::SynthBrass:
            case OscillatorType::LatinBrass:        return 62; // Synth Brass 1

            // === Strings (GM 48-55) ===
            case OscillatorType::SynthStrings:      return 51; // Synth Strings 1

            // === Pluck/Bell (GM 8-15, 112-119) ===
            case OscillatorType::SynthPluck:        return 26; // Electric Guitar (jazz)
            case OscillatorType::SynthBell:         return 14; // Tubular Bells

            // === Stabs/Chords (GM 88-95 Pads) ===
            case OscillatorType::TechnoStab:
            case OscillatorType::SynthwaveChord:
            case OscillatorType::RaveChord:         return 88; // Pad 1 (new age)

            // === Lo-Fi (GM 0-7 Piano) ===
            case OscillatorType::LoFiKeys:          return 3;  // Honky-tonk Piano

            // === Special/Noise (GM 120-127 Sound Effects) ===
            case OscillatorType::VinylNoise:        return 122; // Seashore (noise texture)
            case OscillatorType::Noise:             return 120; // Guitar Fret Noise

            // === Basic Waveforms -> Synth Lead ===
            case OscillatorType::Pulse:             return 80; // Lead 1 (square)
            case OscillatorType::Sawtooth:          return 81; // Lead 2 (sawtooth)
            case OscillatorType::Triangle:
            case OscillatorType::Sine:              return 80; // Lead 1 (square)

            // === Chiptune -> Synth Lead ===
            case OscillatorType::SynthChip:
            case OscillatorType::Supersaw:          return 80; // Lead 1 (square)

            // === Vocoder -> Synth Voice (GM 54) ===
            case OscillatorType::Vocoder:           return 54; // Synth Voice

            // === Bass (Special) ===
            case OscillatorType::KavinskyBass:      return 39; // Synth Bass 2

            // === Default ===
            default:                                return 80; // Lead 1 (square)
        }
    }
};

} // namespace ChiptuneTracker
