#pragma once

// ============================================================================
// Reading a Standard MIDI File
//
// Export has been here since early on. Import never was - and the awkward
// part is that everything it needs already shipped: vendor/midifile is
// vendored, linked into both the app and the tests, and smf::MidiFile::read
// sits next to the write call that gets used. The file browser even lists
// .mid as a project type, so it will show you files it cannot open.
//
// WHAT IMPORT CAN AND CANNOT BE. A MIDI file is notes and timing. It is not
// sound: there is no instrument in it, only a program number that means
// "harpsichord" to a General MIDI synthesiser and nothing at all here. So
// this brings across pitch, timing, length and velocity, and leaves the
// instruments alone - which is honest, and is also what somebody importing
// a part into a chiptune tracker wants. Nobody imports a MIDI file hoping
// for its harpsichord.
//
// WHAT IT DOES SAY. The result carries a report: how many tracks and notes
// arrived, what was dropped and why. An import that quietly loses half a
// file because the tracks ran past the channel count is the kind of thing
// somebody spends an hour on before checking the source file.
// ============================================================================

#include "Types.h"
#include "../vendor/midifile/include/MidiFile.h"

#include <algorithm>
#include <cmath>

#include <string>
#include <vector>

namespace ChiptuneTracker {

struct MidiImportReport {
    bool ok = false;
    std::string error;          // why it failed, if it did

    int tracksRead = 0;         // tracks that had notes in them
    int notesImported = 0;
    int notesDropped = 0;       // past the pattern cap
    int tracksDropped = 0;      // past the channel count
    float bpm = 0.0f;           // what the file said, if it said anything

    std::string summary() const {
        if (!ok) return "Could not import: " + error;

        std::string text = "Imported " + std::to_string(notesImported) +
                           " notes across " + std::to_string(tracksRead) +
                           " track" + (tracksRead == 1 ? "" : "s");
        if (bpm > 0.0f) {
            text += " at " + std::to_string(static_cast<int>(bpm + 0.5f)) + " BPM";
        }
        if (tracksDropped > 0) {
            text += ". " + std::to_string(tracksDropped) + " track" +
                    (tracksDropped == 1 ? " was" : "s were") +
                    " past the channel count and skipped";
        }
        if (notesDropped > 0) {
            text += ". " + std::to_string(notesDropped) +
                    " notes did not fit the pattern and were dropped";
        }
        return text + ".";
    }
};

/*
 * Turn a MIDI file into a project.
 *
 * One pattern per track, one track per channel, and the whole file becomes
 * one clip each - rather than trying to guess where the bars are and cut it
 * up. Guessing wrong produces a project that looks arranged and is not,
 * which is harder to fix than one long pattern somebody can split
 * themselves.
 *
 * Replaces the project's patterns and arrangement. Channel settings, the
 * master chain and everything else are left alone, so importing into a
 * project you have already voiced keeps the sound and swaps the notes.
 */
inline MidiImportReport importMidiFile(const std::string& path, Project& project) {
    MidiImportReport report;

    smf::MidiFile midi;
    if (!midi.read(path)) {
        report.error = "the file could not be read as a MIDI file";
        return report;
    }

    // Absolute ticks, and note-ons paired with their note-offs. Without the
    // pairing every note would need a manual search for its end, and a
    // note-on with velocity zero - which is how a great many files write a
    // note-off - would be imported as a silent note that never stops.
    midi.doTimeAnalysis();
    midi.linkNotePairs();

    const int tpq = midi.getTicksPerQuarterNote();
    if (tpq <= 0) {
        report.error = "the file declares no timebase";
        return report;
    }

    /*
     * Tempo.
     *
     * Taken from the first tempo event rather than the tempo map, because
     * this brings a file in as patterns and a pattern has no tempo changes
     * in it. A file that speeds up halfway through arrives at its opening
     * tempo, which is at least the tempo its beginning was written at.
     */
    for (int track = 0; track < midi.getTrackCount() && report.bpm <= 0.0f; ++track) {
        for (int event = 0; event < midi[track].size(); ++event) {
            if (midi[track][event].isTempo()) {
                report.bpm = static_cast<float>(midi[track][event].getTempoBPM());
                break;
            }
        }
    }
    /*
     * A tempo outside this range is not a tempo.
     *
     * The range is what stopped a real bug from arriving silently: this
     * program's own exporter was writing microseconds per quarter note into
     * a field that takes BPM, so its files came back at about 454,000 BPM.
     * The importer read that correctly, refused it, and kept the project's
     * own tempo - which is how the exporter's bug was found rather than
     * inherited.
     */
    if (report.bpm > 20.0f && report.bpm < 400.0f) {
        project.bpm = report.bpm;
    } else {
        report.bpm = 0.0f;   // nothing usable said; leave the project's own
    }

    project.patterns.clear();
    project.arrangement.clear();

    int channel = 0;
    float longestBeats = 0.0f;

    for (int track = 0; track < midi.getTrackCount(); ++track) {
        Pattern pattern;
        float trackEnd = 0.0f;

        for (int event = 0; event < midi[track].size(); ++event) {
            const smf::MidiEvent& midiEvent = midi[track][event];
            if (!midiEvent.isNoteOn()) continue;

            // A note-on with no matching off is a note that never ends. The
            // file is malformed rather than empty, so it is skipped and
            // counted rather than given an arbitrary length.
            const int durationTicks = midiEvent.getTickDuration();
            if (durationTicks <= 0) {
                ++report.notesDropped;
                continue;
            }

            if (pattern.notes.size() >= static_cast<size_t>(Pattern::MAX_NOTES)) {
                ++report.notesDropped;
                continue;
            }

            Note note;
            note.pitch = std::clamp(midiEvent.getKeyNumber(), 0, 127);
            note.velocity =
                std::clamp(static_cast<float>(midiEvent.getVelocity()) / 127.0f,
                           0.01f, 1.0f);
            note.startTime =
                static_cast<float>(midiEvent.tick) / static_cast<float>(tpq);
            note.duration =
                static_cast<float>(durationTicks) / static_cast<float>(tpq);

            // A note shorter than a thousandth of a beat is a timing
            // artefact rather than a note; give it something audible instead
            // of a duration that rounds to nothing.
            if (note.duration < 0.001f) note.duration = 0.0625f;

            trackEnd = std::max(trackEnd, note.startTime + note.duration);
            pattern.notes.push_back(note);
            ++report.notesImported;
        }

        if (pattern.notes.empty()) continue;   // a tempo or marker track

        if (channel >= Project::MAX_CHANNELS) {
            ++report.tracksDropped;
            // The notes on it were counted as imported a moment ago, which
            // would be a lie. Take them back off.
            report.notesImported -= static_cast<int>(pattern.notes.size());
            continue;
        }

        pattern.name = "Track " + std::to_string(track);
        pattern.length = std::max(1, static_cast<int>(std::ceil(trackEnd)));

        Clip clip;
        clip.patternIndex = static_cast<int>(project.patterns.size());
        clip.channelIndex = channel;
        clip.startBeat = 0.0f;
        clip.lengthBeats = std::max(1.0f, trackEnd);

        project.patterns.push_back(std::move(pattern));
        project.arrangement.push_back(clip);

        longestBeats = std::max(longestBeats, trackEnd);
        ++report.tracksRead;
        ++channel;
    }

    if (report.tracksRead == 0) {
        report.error = "the file has no notes in it";
        project.patterns.clear();
        project.arrangement.clear();
        return report;
    }

    project.songLength = std::max(4.0f, std::ceil(longestBeats));
    report.ok = true;
    return report;
}

}  // namespace ChiptuneTracker
