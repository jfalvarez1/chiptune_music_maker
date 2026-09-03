#pragma once

// ============================================================================
// Voice Mode - build a song a part at a time, by singing it
//
// The Voice to Notes panel converts one take. This is the workflow around
// it: lay down a drum groove, keep it, then hum a bass line against what you
// just made, keep that, then a melody over both. Each part plays while you
// record the next, which is the whole point - you are playing along with
// your own song rather than into silence.
//
// WHY A MODE RATHER THAN A PANEL. The panel asks you to understand it: pick
// a detection mode, a quantise setting, a destination, a role. That is the
// right shape for someone tuning a take. It is the wrong shape for the
// thing people actually want, which is to get four parts out of their head
// and into a song without learning a tool first.
//
// So this makes exactly one decision at a time, in order, and does
// everything else from the answer: choosing "Bass" sets the detection mode,
// the register, the note lengths, the instrument, and which channel it
// lands on. Every one of those is reachable in the panel afterwards for
// anybody who wants to argue with it.
//
// WHAT IT DELIBERATELY DOES NOT DO. It never overwrites a part you have
// already kept. Each take goes to its own channel and its own pattern, and
// "Try again" throws away only the take in hand. The cost of that is a
// project with more channels than a tidy person would use; the alternative
// is losing a part you liked to a take you were only experimenting with,
// which is not a trade worth making.
// ============================================================================

#include <algorithm>
#include <string>
#include <vector>

#include "Types.h"
#include "LiveVoice.h"
#include "VoicePanel.h"
#include "VoicePost.h"
#include "TempoDetect.h"

namespace ChiptuneTracker {

// ============================================================================
// Where you are in the loop
// ============================================================================
enum class VoiceModeStep : uint8_t {
    Choose = 0,   // what are you making?
    Armed,        // ready, waiting for you to start
    Recording,    // listening
    Review,       // here is what I heard - keep it?
};

/*
 * A part that has been kept.
 *
 * Remembered so the mode can list what the song is made of, and so a part
 * can be removed again without hunting through the mixer for whichever
 * channel it went to.
 */
struct VoicePart {
    VoiceRole role = VoiceRole::Lead;
    int channelIndex = 0;
    int patternIndex = 0;
    std::string name;
    int noteCount = 0;
    bool muted = false;
};

struct VoiceModeState {
    VoiceModeStep step = VoiceModeStep::Choose;
    VoiceRole role = VoiceRole::Drums;

    std::vector<VoicePart> parts;

    // How long each part is, in bars. Fixed for the session so the parts
    // stack into a loop rather than drifting out of phase with each other.
    int barsPerPart = 4;

    // Set once the first part decides the tempo, so later parts are not
    // asked about it again.
    bool tempoSettled = false;

    std::string message;      // what just happened
    std::string warning;      // what went wrong, if anything

    // The take under review, converted, so Keep and the preview agree
    // exactly rather than converting twice and hoping.
    std::vector<Note> pending;
};

/*
 * The instrument a role should land on.
 *
 * Drums are absent on purpose: a beatboxed part carries its own oscillator
 * per hit - kick, snare and hat are different instruments - so the
 * channel's own type is never consulted for one.
 */
inline OscillatorType instrumentForRole(VoiceRole role) {
    switch (role) {
        case VoiceRole::Bass:   return OscillatorType::SynthBass;
        case VoiceRole::Chords: return OscillatorType::PolySynth;
        case VoiceRole::Lead:   return OscillatorType::SynthLead;
        default:                return OscillatorType::Pulse;
    }
}

// What the mode should listen for. Beatboxing and humming need different
// detectors, and asking the user to also pick one is asking them to know
// something they should not have to.
inline int analysisModeForRole(VoiceRole role) {
    return (role == VoiceRole::Drums) ? 1 : 0;
}

inline const char* roleHint(VoiceRole role) {
    switch (role) {
        case VoiceRole::Drums:
            return "Beatbox a groove. \"puh\" for kick, \"kuh\" for snare, "
                   "\"tss\" for hats.";
        case VoiceRole::Bass:
            return "Hum the bass line. Sing it wherever is comfortable - it "
                   "gets moved down for you.";
        case VoiceRole::Chords:
            return "Hum the chords one note at a time, like an arpeggio.";
        default:
            return "Hum the tune. Sing it wherever is comfortable - it gets "
                   "moved up for you.";
    }
}

/*
 * The next channel to put a part on.
 *
 * Never one already used by a kept part, so a new take cannot land on top
 * of something that is working. Returns -1 when there are none left, which
 * the caller reports rather than silently overwriting channel 0.
 */
inline int nextFreeChannel(const Project& project, const VoiceModeState& state) {
    for (int channel = 0; channel < project.activeChannelCount(); ++channel) {
        bool taken = false;
        for (const VoicePart& part : state.parts) {
            if (part.channelIndex == channel) taken = true;
        }
        if (taken) continue;

        // Also skip a channel somebody has already written to by hand.
        bool used = false;
        for (const Clip& clip : project.arrangement) {
            if (clip.channelIndex == channel) used = true;
        }
        if (used) continue;

        return channel;
    }
    return -1;
}

/*
 * Commit a reviewed take as a part of the song.
 *
 * Creates the pattern, sets the channel up for the role, and places a clip
 * so it plays as part of the arrangement rather than only existing in a
 * pattern nobody triggers - which is the difference between "the notes are
 * saved" and "I can hear my song".
 *
 * Returns the part, or a part with channelIndex -1 if there was no room.
 */
inline VoicePart commitVoicePart(Project& project, VoiceModeState& state,
                                 const std::vector<Note>& notes) {
    VoicePart part;
    part.channelIndex = -1;
    if (notes.empty()) return part;

    const int channel = nextFreeChannel(project, state);
    if (channel < 0) return part;

    const int lengthBeats =
        std::max(1, state.barsPerPart * std::max(1, project.beatsPerMeasure));

    Pattern pattern;
    pattern.name = std::string(voiceRoleName(state.role)) + " " +
                   std::to_string(state.parts.size() + 1);
    pattern.notes = notes;

    // Long enough to hold what was sung, and at least the loop length, so
    // the parts stack rather than one of them ending early.
    float furthest = 0.0f;
    for (const Note& note : pattern.notes) {
        furthest = std::max(furthest, note.startTime + note.duration);
    }
    pattern.length = std::max(lengthBeats, static_cast<int>(std::ceil(furthest)));

    project.patterns.push_back(pattern);
    const int patternIndex = static_cast<int>(project.patterns.size()) - 1;

    ChannelConfig& config = project.channels[static_cast<size_t>(channel)];
    config.name = pattern.name;
    if (state.role != VoiceRole::Drums) {
        config.oscillator.type = instrumentForRole(state.role);
    }

    // On the timeline from the top, so everything kept so far plays
    // together and the next take is sung against a song rather than into
    // silence.
    Clip clip;
    clip.patternIndex = patternIndex;
    clip.channelIndex = channel;
    clip.startBeat = 0.0f;
    clip.lengthBeats = static_cast<float>(pattern.length);
    project.arrangement.push_back(clip);

    part.role = state.role;
    part.channelIndex = channel;
    part.patternIndex = patternIndex;
    part.name = pattern.name;
    part.noteCount = static_cast<int>(notes.size());
    return part;
}

/*
 * Take a part back out.
 *
 * Removes its clip and silences its channel, but leaves the pattern in the
 * project. Deleting the pattern would renumber every clip that refers to a
 * later one - and somebody who removes a part from the mode has not
 * necessarily decided to throw the notes away.
 */
inline void removeVoicePart(Project& project, VoiceModeState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.parts.size())) return;

    const VoicePart& part = state.parts[static_cast<size_t>(index)];

    project.arrangement.erase(
        std::remove_if(project.arrangement.begin(), project.arrangement.end(),
                       [&part](const Clip& clip) {
                           return clip.channelIndex == part.channelIndex &&
                                  clip.patternIndex == part.patternIndex;
                       }),
        project.arrangement.end());

    if (part.channelIndex >= 0 && part.channelIndex < Project::MAX_CHANNELS) {
        project.channels[static_cast<size_t>(part.channelIndex)].name = "Channel";
    }

    state.parts.erase(state.parts.begin() + index);
}

} // namespace ChiptuneTracker
