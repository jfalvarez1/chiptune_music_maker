#pragma once

/*
 * ChiptuneTracker - Conflicting settings
 *
 * Validation (ProjectValidation.h) answers "could this crash or hang". This
 * answers a different question: "is this project quietly not doing what the
 * user thinks it is".
 *
 * Every finding here is a setting that is legal, saved, displayed as
 * enabled, and has no effect - or worse, silently cancels something else. A
 * send routed to a muted bus. A modulation route pointing at an engine the
 * channel is not running. A soloed channel three screens away that is why
 * nothing else is audible. None of these is a bug in the program, and all of
 * them are the program failing to say something it knows.
 *
 * This is the same failure mode the test suite was built around - a control
 * that is wired in the UI and reaches nothing - seen from the user's side
 * rather than the developer's.
 *
 * Design rules:
 *
 *   - Never change anything. This reports; the user decides. A "fix it for
 *     me" button that silently unmutes a channel would be worse than the
 *     confusion it solves.
 *   - Every finding says what is wrong, why it matters and what to do. A
 *     warning that only names a field is a puzzle, not a help.
 *   - No finding for a default. A fresh project must be completely silent
 *     here, or the panel becomes noise that everyone learns to ignore.
 *
 * Pure queries, no ImGui, so the rules are testable.
 */

#include <string>
#include <vector>

#include "Types.h"
#include "DrumKit.h"
#include "Effects.h"

namespace ChiptuneTracker {

enum class AuditSeverity : uint8_t {
    // Something is inaudible that the user plainly meant to hear.
    Problem = 0,
    // A setting that does nothing, or two that contradict each other.
    Warning,
    // Worth knowing, not worth fixing.
    Note
};

struct AuditFinding {
    AuditSeverity severity = AuditSeverity::Note;
    int channel = -1;            // -1 for project-wide
    std::string what;            // the conflict, in one line
    std::string why;             // why it matters
    std::string fix;             // what to do about it
};

inline const char* auditSeverityName(AuditSeverity severity) {
    switch (severity) {
        case AuditSeverity::Problem: return "Problem";
        case AuditSeverity::Warning: return "Warning";
        case AuditSeverity::Note:
        default:                     return "Note";
    }
}

namespace audit {

// Does this channel carry anything at all? A silent setting on a channel
// nobody uses is not worth a line.
inline bool channelIsInUse(const Project& project, int channel) {
    for (const Clip& clip : project.arrangement) {
        if (clip.channelIndex == channel) return true;
    }
    return false;
}

// Whether a modulation destination can reach anything on this channel. A
// route to Wavetable Morph on an FM channel is legal, saved, shown as
// enabled, and does nothing whatsoever.
inline bool destinationApplies(const ChannelConfig& channel,
                               ModDestination destination) {
    switch (destination) {
        case ModDestination::WavetableMorph:
            return channel.oscillator.type == OscillatorType::Custom;
        case ModDestination::FMBrightness:
            return channel.oscillator.type == OscillatorType::FMSynth;
        case ModDestination::GrainPosition:
        case ModDestination::GrainDensity:
            return channel.oscillator.type == OscillatorType::Granular;
        case ModDestination::PulseWidth:
            return channel.oscillator.type == OscillatorType::Pulse;
        case ModDestination::FilterCutoff:
        case ModDestination::FilterResonance:
            return channel.filterEnabled;
        case ModDestination::Pitch:
        case ModDestination::Level:
            return true;
        case ModDestination::None:
        case ModDestination::Count:
        default:
            return true;    // nothing routed; not this check's business
    }
}

// One decimal place, without pulling in a stream. std::to_string on a float
// gives six digits, and "2.500000 s" reads like a measurement when it is a
// slider position.
// The sign is carried separately rather than left to integer division,
// which loses it entirely between -1 and 0: -0.4 would print as "0.4".
inline std::string oneDecimal(float value) {
    const long scaled = std::lround(std::fabs(value) * 10.0f);
    const std::string sign = (value < 0.0f && scaled != 0) ? "-" : "";
    return sign + std::to_string(scaled / 10) + "." + std::to_string(scaled % 10);
}

} // namespace audit

/*
 * Everything worth saying about a project, worst first.
 *
 * Returns an empty list for a fresh project. That is a hard requirement: a
 * panel that always has something in it is a panel nobody reads.
 */
/*
 * The voice kit the audit consults.
 *
 * The kit lives in the Voice panel's own state rather than in the project,
 * because it is a property of how somebody is recording rather than of the
 * song. The audit still needs to see it, so it is reached through a hook
 * the UI sets - and defaults to a usable kit, so a headless caller with no
 * UI never reports a problem that does not exist.
 */
inline DrumKit& voiceKitForAudit() {
    static DrumKit kit;
    return kit;
}

inline std::vector<AuditFinding> auditProject(const Project& project) {
    std::vector<AuditFinding> findings;

    const int activeChannels = project.activeChannelCount();

    // ---- Solo, which is the classic "why is nothing playing" ---------------
    {
        int soloed = -1;
        int soloCount = 0;
        for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
            if (project.channels[static_cast<size_t>(ch)].solo) {
                if (soloed < 0) soloed = ch;
                ++soloCount;
            }
        }
        if (soloCount > 0) {
            findings.push_back({
                AuditSeverity::Problem, soloed,
                "Solo is on for " + std::to_string(soloCount) + " channel" +
                    (soloCount == 1 ? "" : "s"),
                "Every other channel is silenced while any solo is active, "
                "including channels on the other side of the mixer.",
                "Clear solo on channel " +
                    project.channels[static_cast<size_t>(soloed)].name +
                    " to hear the rest of the project again."
            });
        }
    }

    // ---- A groove and a swing, which cannot both mean what they say --------
    if (project.groove.active() && project.swing > 0.001f) {
        findings.push_back({
            AuditSeverity::Warning, -1,
            "A groove and a swing amount are both set",
            "A groove already decides where every row falls, so the swing "
            "slider is ignored while one is active. It is still saved, and "
            "it will come back the moment the groove is cleared - which "
            "looks like the timing changing on its own.",
            "Clear the swing, or clear the groove. The groove can express "
            "anything the swing slider can: \"7 5\" is the same feel."
        });
    }

    // ---- Content that cannot be heard --------------------------------------
    if (project.chipAuthentic) {
        int stranded = 0;
        for (const Clip& clip : project.arrangement) {
            if (clip.channelIndex >= activeChannels) ++stranded;
        }
        if (stranded > 0) {
            findings.push_back({
                AuditSeverity::Problem, -1,
                std::to_string(stranded) + " clip" + (stranded == 1 ? "" : "s") +
                    " sit past the chip channel limit",
                "Chip-authentic mode caps playback at " +
                    std::to_string(Project::CHIP_CHANNELS) +
                    " channels, so anything above that is not mixed at all - "
                    "it is still in the file, just inaudible.",
                "Move those clips down, or turn off chip-authentic mode in "
                "the Master Bus."
            });
        }
    }

    for (int ch = 0; ch < activeChannels; ++ch) {
        const ChannelConfig& channel = project.channels[static_cast<size_t>(ch)];
        const bool used = audit::channelIsInUse(project, ch);
        const std::string label = channel.name;

        // A muted channel that has content on it. Legal and often deliberate,
        // so it is a Note rather than a Problem - but worth saying when
        // someone is wondering where a part went.
        if (used && channel.muted) {
            findings.push_back({
                AuditSeverity::Note, ch,
                label + " is muted but has clips on it",
                "Nothing on this channel will sound.",
                "Unmute it in the mixer if you were expecting to hear it."
            });
        }

        // Volume at zero without being muted: the mute button is the
        // discoverable way to do this, so a zero fader usually means a drag
        // that went too far.
        if (used && !channel.muted && channel.volume <= 0.0001f) {
            findings.push_back({
                AuditSeverity::Problem, ch,
                label + " has its fader at zero",
                "It is not muted, so nothing indicates why it is silent.",
                "Raise the fader, or use mute if you meant to silence it."
            });
        }

        // ---- Engines with nothing to play ----------------------------------
        if (channel.oscillator.type == OscillatorType::Sampler) {
            const SamplerInstrument& sampler = channel.oscillator.sampler;
            if (sampler.zoneCount == 0) {
                findings.push_back({
                    AuditSeverity::Problem, ch,
                    label + " is a sampler with no zones",
                    "A sampler with no zones has nothing mapped to any key, "
                    "so every note is silent.",
                    "Add a zone in the Channel Editor under Sampler."
                });
            } else {
                int playable = 0;
                for (int z = 0; z < sampler.zoneCount; ++z) {
                    if (sampler.zones[static_cast<size_t>(z)].sampleId >= 0) ++playable;
                }
                if (playable == 0) {
                    findings.push_back({
                        AuditSeverity::Problem, ch,
                        label + " has sampler zones but no audio behind them",
                        "Every zone points at a sample that is not loaded, so "
                        "the channel is silent.",
                        "Reload the audio, or check whether the files moved."
                    });
                }
            }
        }

        if (channel.oscillator.type == OscillatorType::Granular &&
            channel.oscillator.granular.sampleId < 0) {
            findings.push_back({
                AuditSeverity::Problem, ch,
                label + " is granular with no source audio",
                "Granular synthesis plays fragments of a recording; with no "
                "recording there is nothing to play.",
                "Load a sample in the Channel Editor under Granular."
            });
        }

        // ---- Settings that cancel each other -------------------------------
        if (channel.autoTuneEnabled && channel.pitchShiftEnabled &&
            channel.pitchShiftSemitones != 0.0f) {
            findings.push_back({
                AuditSeverity::Warning, ch,
                label + " has Auto-Tune and Pitch Shift both on",
                "Auto-Tune pulls the pitch onto a scale and the pitch "
                "shifter then moves it off again, so the correction lands "
                "somewhere between the two.",
                "Use one or the other, or set the shift to a whole number of "
                "semitones so the corrected note stays in the scale."
            });
        }

        if (channel.sidechainEnabled && channel.sidechainSource < 0 &&
            channel.sidechainBus < 0) {
            findings.push_back({
                AuditSeverity::Warning, ch,
                label + " has sidechain on with no source",
                "There is nothing for the compressor to duck against, so it "
                "does nothing.",
                "Pick a source channel or bus, or turn sidechain off."
            });
        }

        // Effects that are switched on with their mix at zero. Each is a
        // control that says it is doing something and is not.
        struct MixCheck { bool enabled; float mix; const char* name; };
        const MixCheck MIXES[] = {
            {channel.reverbEnabled,        channel.reverbMix,        "Reverb"},
            {channel.delayEnabled,         channel.delayMix,         "Delay"},
            {channel.chorusEnabled,        channel.chorusMix,        "Chorus"},
            {channel.distortionEnabled,    channel.distortionMix,    "Distortion"},
            {channel.flangerEnabled,       channel.flangerMix,       "Flanger"},
            {channel.tapeSaturationEnabled,channel.tapeMix,          "Tape Saturation"},
            {channel.echoEnabled,          channel.echoMix,          "Echo"},
            {channel.pitchShiftEnabled,    channel.pitchShiftMix,    "Pitch Shift"},
            {channel.formantShiftEnabled,  channel.formantShiftMix,  "Formant Shift"},
            {channel.autoTuneEnabled,      channel.autoTuneMix,      "Auto-Tune"},
        };
        for (const MixCheck& mix : MIXES) {
            if (mix.enabled && mix.mix <= 0.0001f) {
                findings.push_back({
                    AuditSeverity::Warning, ch,
                    label + ": " + mix.name + " is on with its mix at zero",
                    "The effect is running and costing CPU, and none of it "
                    "reaches the output.",
                    "Raise the mix, or switch the effect off."
                });
            }
        }

        // ---- Sends into nothing --------------------------------------------
        for (int slot = 0; slot < MAX_SENDS_PER_CHANNEL; ++slot) {
            const SendConfig& send = channel.sends[static_cast<size_t>(slot)];
            if (send.destination < 0) continue;

            const AuxBusConfig& bus =
                project.auxBuses[static_cast<size_t>(send.destination)];

            if (send.level > 0.0001f && bus.muted) {
                findings.push_back({
                    AuditSeverity::Warning, ch,
                    label + " sends to " + bus.name + ", which is muted",
                    "The send is doing nothing, and the bus being muted is "
                    "not visible from the channel.",
                    "Unmute " + bus.name + " in the Master Bus, or remove "
                    "the send."
                });
            }
            if (send.level <= 0.0001f) {
                findings.push_back({
                    AuditSeverity::Note, ch,
                    label + " has a send to " + bus.name + " at zero",
                    "The routing is set up but nothing is being sent.",
                    "Raise the send level, or clear the destination."
                });
            }
        }

        /*
         * ---- Note racks that cannot do what they were added for ------------
         *
         * These are the ones you cannot hear the problem with, because the
         * symptom is that nothing changed. A Range passing nothing silences
         * the channel; an Arpeggio with one note and one octave is a
         * repeated note; a Chord below an Arpeggio arpeggiates a single note
         * and then harmonises every step of it, which is almost never what
         * somebody stacking those two meant.
         */
        {
            int chordAt = -1;
            int arpAt = -1;

            for (size_t i = 0; i < channel.noteFX.size(); ++i) {
                const NoteFXSlot& fx = channel.noteFX[i];
                if (!fx.enabled) continue;

                if (fx.type == NoteFXType::Chord && chordAt < 0) {
                    chordAt = static_cast<int>(i);
                }
                if (fx.type == NoteFXType::Arpeggio && arpAt < 0) {
                    arpAt = static_cast<int>(i);
                }

                if (fx.type == NoteFXType::Range && fx.lowPitch > fx.highPitch) {
                    findings.push_back({
                        AuditSeverity::Problem, ch,
                        label + " has a note Range that passes nothing",
                        "The lowest note is above the highest, so every note "
                        "on this channel is filtered out and the channel is "
                        "silent.",
                        "Swap the two ends, or remove the Range module."
                    });
                }

                if (fx.type == NoteFXType::Octave && fx.octaves == 0) {
                    findings.push_back({
                        AuditSeverity::Warning, ch,
                        label + " has an Octave module set to zero octaves",
                        "It doubles every note with a copy of itself at the "
                        "same pitch, which only changes the level.",
                        "Set it to plus or minus an octave, or use a Velocity "
                        "module if a level change was what you wanted."
                    });
                }

                if (fx.type == NoteFXType::Strum && fx.strumBeats <= 0.0f) {
                    findings.push_back({
                        AuditSeverity::Warning, ch,
                        label + " has a Strum with no gap",
                        "Every note of the chord still arrives at the same "
                        "instant, which is what a strum exists to stop.",
                        "Raise the gap. Around 0.02 beats is a fast strum."
                    });
                }

                if (fx.type == NoteFXType::Velocity && fx.velocityScale <= 0.0f &&
                    fx.velocityFixed <= 0.0f) {
                    findings.push_back({
                        AuditSeverity::Problem, ch,
                        label + " has a Velocity module scaling to zero",
                        "Every note plays at the floor level, which is very "
                        "nearly silence, and the channel will read as broken.",
                        "Raise the scale, or set a fixed velocity instead."
                    });
                }
            }

            if (arpAt >= 0 && chordAt < 0) {
                const NoteFXSlot& arp =
                    channel.noteFX[static_cast<size_t>(arpAt)];
                if (arp.arpOctaves <= 1) {
                    findings.push_back({
                        AuditSeverity::Warning, ch,
                        label + " has an Arpeggio with nothing to arpeggiate",
                        "An arpeggio runs through the notes sounding at that "
                        "moment. With one note and one octave there is only "
                        "one, so it repeats it - which sounds like a "
                        "retrigger, not an arpeggio.",
                        "Add a Chord module above it, or raise the octave "
                        "range so it has somewhere to climb."
                    });
                }
            }

            if (arpAt >= 0 && chordAt > arpAt) {
                findings.push_back({
                    AuditSeverity::Note, ch,
                    label + " has a Chord below its Arpeggio",
                    "The arpeggio runs first, on the single written note, and "
                    "the chord then harmonises every step of it. That is a "
                    "real sound, and it is rarely the one somebody stacking "
                    "these two is after.",
                    "Move the Chord above the Arpeggio to arpeggiate a chord "
                    "instead."
                });
            }
        }

        // ---- Modulation that reaches nothing --------------------------------
        const ModMatrix& matrix = channel.oscillator.modMatrix;
        for (int r = 0; r < matrix.routeCount; ++r) {
            const ModRoute& route = matrix.routes[static_cast<size_t>(r)];
            if (!route.active()) continue;
            if (audit::destinationApplies(channel, route.destination)) continue;

            findings.push_back({
                AuditSeverity::Warning, ch,
                label + ": " + modSourceName(route.source) + " routed to " +
                    modDestinationName(route.destination) +
                    ", which this channel has no use for",
                "That destination belongs to an engine or effect this "
                "channel is not running, so the route does nothing.",
                std::string("Switch the channel to the matching engine, or ") +
                    "point the route somewhere it applies."
            });
        }

        // A monophonic channel carrying chords will steal its own voices.
        if (matrix.polyphonyLimit == 1) {
            int stacked = 0;
            for (const Clip& clip : project.arrangement) {
                if (clip.channelIndex != ch) continue;
                if (clip.patternIndex < 0 ||
                    clip.patternIndex >= static_cast<int>(project.patterns.size())) {
                    continue;
                }
                const Pattern& pattern =
                    project.patterns[static_cast<size_t>(clip.patternIndex)];
                for (size_t a = 0; a < pattern.notes.size(); ++a) {
                    for (size_t b = a + 1; b < pattern.notes.size(); ++b) {
                        if (std::fabs(pattern.notes[a].startTime -
                                      pattern.notes[b].startTime) < 1e-3f) {
                            ++stacked;
                        }
                    }
                }
            }
            if (stacked > 0) {
                findings.push_back({
                    AuditSeverity::Warning, ch,
                    label + " is monophonic but plays chords",
                    "With a polyphony limit of one, each new note steals the "
                    "voice from the one before it, so only the last note of "
                    "every chord sounds.",
                    "Raise the polyphony limit under Modulation, or move the "
                    "other notes to another channel."
                });
            }
        }
    }

    // ---- Project-wide -------------------------------------------------------
    if (project.masterVolume <= 0.0001f) {
        findings.push_back({
            AuditSeverity::Problem, -1,
            "Master volume is at zero",
            "Nothing will be heard or exported, whatever the channels do.",
            "Raise the master volume in the transport bar."
        });
    }

    if (!project.missingSamples.empty()) {
        findings.push_back({
            AuditSeverity::Problem, -1,
            std::to_string(project.missingSamples.size()) +
                " audio file" + (project.missingSamples.size() == 1 ? "" : "s") +
                " could not be found",
            "Clips and zones that pointed at them keep their edits but have "
            "no audio behind them.",
            "Use Import Audio with the clip selected to point it at the file "
            "again."
        });
    }

    // Tempo or meter changes past the end of the song never take effect.
    {
        int stranded = 0;
        for (int i = 0; i < project.tempoMap.tempoCount(); ++i) {
            if (project.tempoMap.tempoAt(i).beat > project.songLength) ++stranded;
        }
        for (int i = 0; i < project.tempoMap.meterCount(); ++i) {
            if (project.tempoMap.meterAt(i).beat > project.songLength) ++stranded;
        }
        if (stranded > 0) {
            findings.push_back({
                AuditSeverity::Note, -1,
                std::to_string(stranded) + " tempo or meter change" +
                    (stranded == 1 ? " sits" : "s sit") + " past the end of the song",
                "The playhead never reaches them, so they have no effect.",
                "Extend the song length, or move them earlier."
            });
        }
    }

    /*
     * ---- Notes the hardware could not play -----------------------------
     *
     * Only when the project has said it wants to be chip-legal, because
     * outside that this is not a fault - it is the whole point of a float
     * engine.
     *
     * Two separate problems, and they fail differently. A note below the
     * longest period the register can hold simply does not exist on the
     * chip. A note near the top exists and is out of tune, by more the
     * higher it goes, because the period steps get further apart than the
     * notes do - and above about G#8 there is no chromatic scale at all.
     */
    if (project.chipAuthentic) {
        int belowRange = 0;
        int badlyTuned = 0;
        float worstCents = 0.0f;
        int worstPitch = 0;

        for (const Pattern& pattern : project.patterns) {
            for (const Note& note : pattern.notes) {
                const float hz = noteToHz(note.pitch);
                const ChipPitchCheck check =
                    checkChipPitch(ChipVoice::NESPulse, hz, project.chipRegion);

                if (check.belowRange) {
                    ++belowRange;
                } else if (std::fabs(check.errorCents) >
                           CHIP_TUNING_TOLERANCE_CENTS) {
                    ++badlyTuned;
                    if (std::fabs(check.errorCents) > std::fabs(worstCents)) {
                        worstCents = check.errorCents;
                        worstPitch = note.pitch;
                    }
                }
            }
        }

        if (belowRange > 0) {
            findings.push_back({
                AuditSeverity::Warning, -1,
                std::to_string(belowRange) + " note" +
                    (belowRange == 1 ? " is" : "s are") +
                    " below what a pulse channel can reach",
                std::string("The period register runs out at about ") +
                    (project.chipRegion == ChipRegion::PAL ? "50.7" : "54.6") +
                    " Hz on " + chipRegionName(project.chipRegion) +
                    ", which is just under A1. A real chip would play the "
                    "lowest note it has, not the note that was written.",
                "Move them up an octave, or put the bass on the triangle "
                "channel - it reaches an octave lower for the same register."
            });
        }

        if (badlyTuned > 0) {
            findings.push_back({
                AuditSeverity::Note, -1,
                std::to_string(badlyTuned) + " note" +
                    (badlyTuned == 1 ? "" : "s") +
                    " land more than a quarter-tone off on real hardware",
                "The pitch register is eleven bits, so its steps get further "
                "apart the higher you go - inaudible at the bottom of the "
                "keyboard and more than a semitone at the top. The worst here "
                "is " + std::to_string(static_cast<int>(worstCents)) +
                    " cents, on MIDI note " + std::to_string(worstPitch) + ".",
                "This is what chiptune leads actually sound like up there, so "
                "it may be exactly right. Move them down an octave if it is "
                "not."
            });
        }

        // Engines no chip could run, on a project claiming to be chip-legal.
        int modern = 0;
        for (int ch = 0; ch < activeChannels; ++ch) {
            const OscillatorType type =
                project.channels[static_cast<size_t>(ch)].oscillator.type;
            if (type == OscillatorType::Sampler ||
                type == OscillatorType::Granular ||
                type == OscillatorType::FMSynth) {
                if (audit::channelIsInUse(project, ch)) ++modern;
            }
        }
        if (modern > 0) {
            findings.push_back({
                AuditSeverity::Note, -1,
                std::to_string(modern) + " channel" + (modern == 1 ? "" : "s") +
                    " use engines a 2A03 did not have",
                "Chip-authentic mode caps the channel count; it does not "
                "restrict the instruments. The project will not run on real "
                "hardware.",
                "Nothing to do unless hardware accuracy is the goal - FM in "
                "particular was real chip hardware, just not this chip's."
            });
        }
    }

    /*
     * ---- Channels held to a chip, and the notes that do not fit ---------
     *
     * Different from the block above in the way that matters: that one asks
     * a hypothetical - what a 2A03 WOULD do with these notes - and this one
     * reports what the engine IS doing, because a channel with a chip voice
     * set is already playing the quantised pitch.
     *
     * So the wording has to change with it. "would be out of tune on real
     * hardware" is advice; "is playing an octave above what you wrote" is a
     * description of the sound coming out of the speakers right now, and the
     * two deserve different urgency.
     *
     * Per channel, via the arrangement, because the constraint is per
     * channel: the same pattern under a triangle and under a Game Boy pulse
     * has entirely different problems, and a pattern that is never placed
     * has none.
     */
    {
        struct ChipChannelReport {
            int clamped = 0;      // played at the wrong pitch entirely
            int silenced = 0;     // the hardware mutes the channel up there
            int detuned = 0;      // playable, past the tolerance
            float worstCents = 0.0f;
            int worstPitch = 0;
            int lowestWritten = 128;
        };
        std::vector<ChipChannelReport> reports(
            static_cast<size_t>(Project::MAX_CHANNELS));

        for (const Clip& clip : project.arrangement) {
            if (clip.type != ClipType::Pattern) continue;
            if (clip.channelIndex < 0 || clip.channelIndex >= activeChannels) continue;
            if (clip.patternIndex < 0 ||
                clip.patternIndex >= static_cast<int>(project.patterns.size())) continue;

            const ChannelConfig& channel =
                project.channels[static_cast<size_t>(clip.channelIndex)];
            if (channel.chipVoice == ChipVoice::None) continue;
            if (!chipVoiceConstrainsPitch(channel.chipVoice)) continue;

            ChipChannelReport& report =
                reports[static_cast<size_t>(clip.channelIndex)];
            const Pattern& pattern =
                project.patterns[static_cast<size_t>(clip.patternIndex)];

            for (const Note& note : pattern.notes) {
                // The clip's transpose is part of what actually sounds, so
                // it is part of what is checked. A pattern that fits and a
                // clip that drops it an octave is exactly the case a check
                // reading patterns alone would miss.
                const int pitch = std::clamp(note.pitch + clip.transpose, 0, 127);
                const float hz = noteToHz(pitch);
                const float actual =
                    quantiseChipFrequency(channel.chipVoice, hz, project.chipRegion);

                if (!(actual > 0.0f)) {
                    ++report.silenced;
                    continue;
                }

                const float cents = 1200.0f * std::log2(actual / hz);
                // Half a semitone is the line between "out of tune" and
                // "a different note", and the register saturating at the
                // bottom of its range lands well past it.
                if (std::fabs(cents) > 50.0f) {
                    ++report.clamped;
                    report.lowestWritten = std::min(report.lowestWritten, pitch);
                } else if (std::fabs(cents) > CHIP_TUNING_TOLERANCE_CENTS) {
                    ++report.detuned;
                }

                if (std::fabs(cents) > std::fabs(report.worstCents)) {
                    report.worstCents = cents;
                    report.worstPitch = pitch;
                }
            }
        }

        for (int ch = 0; ch < activeChannels; ++ch) {
            const ChipChannelReport& report = reports[static_cast<size_t>(ch)];
            const ChannelConfig& channel = project.channels[static_cast<size_t>(ch)];
            const char* voiceName = chipVoiceName(channel.chipVoice);
            const int floorNote = chipLowestNote(channel.chipVoice, project.chipRegion);

            if (report.clamped > 0) {
                findings.push_back({
                    AuditSeverity::Problem, ch,
                    std::to_string(report.clamped) + " note" +
                        (report.clamped == 1 ? " is" : "s are") +
                        " below what a " + voiceName + " can reach",
                    std::string("The period register saturates, so they are "
                                "sounding at the lowest note the channel has "
                                "- MIDI ") + std::to_string(floorNote) +
                        ", and the lowest written here is MIDI " +
                        std::to_string(report.lowestWritten) + ". This is "
                        "not a tuning error; they are playing in the wrong "
                        "octave right now.",
                    (channel.chipVoice == ChipVoice::GameBoyPulse)
                        ? "Move the part up, or put it on a Game Boy wave "
                          "channel - it reaches an octave lower, which is why "
                          "it was the only bass instrument on the machine."
                        : "Move the part up an octave, or use a NES triangle "
                          "voice - it reaches an octave lower for the same "
                          "register."
                });
            }

            if (report.silenced > 0) {
                findings.push_back({
                    AuditSeverity::Problem, ch,
                    std::to_string(report.silenced) + " note" +
                        (report.silenced == 1 ? "" : "s") +
                        " are silent on a " + voiceName,
                    "Below period 8 the sweep unit mutes the channel outright "
                    "rather than playing it sharp, so these produce nothing "
                    "at all. It only happens at the very top of the keyboard.",
                    "Move them down an octave."
                });
            }

            if (report.detuned > 0) {
                findings.push_back({
                    AuditSeverity::Note, ch,
                    std::to_string(report.detuned) + " note" +
                        (report.detuned == 1 ? "" : "s") +
                        " land more than a quarter-tone off on this " + voiceName,
                    "The pitch register's steps get further apart the higher "
                    "you go. The worst here is " +
                        std::to_string(static_cast<int>(report.worstCents)) +
                        " cents, on MIDI note " +
                        std::to_string(report.worstPitch) + ". Above MIDI " +
                        std::to_string(chipHighestUsableNote(channel.chipVoice,
                                                             project.chipRegion)) +
                        " there is no chromatic scale left at all.",
                    "This is what a chiptune lead actually sounds like up "
                    "there, so it may be exactly what you want. Move it down "
                    "an octave if it is not."
                });
            }

            /*
             * The Game Boy envelope, which is reported rather than enforced.
             *
             * A deliberate asymmetry with pitch and volume, and the line is
             * this: the period and volume registers are what the channel
             * PLAYS, so quantising them is playing it correctly. The
             * envelope generator is what the channel IS, and reshaping it
             * would silently rewrite a patch somebody voiced by ear. So the
             * mode says what a Game Boy could not have done and leaves the
             * decision where it belongs.
             *
             * Three separate limits, and the second is the one that catches
             * people: the envelope steps at 64 Hz through at most 15 levels
             * with a period of at most 7 steps, so 1.64 seconds is the
             * longest automatic fade the machine had, in one direction,
             * once. It cannot loop and there is no sustain stage - which is
             * most of why Game Boy instruments have that one-shot character.
             */
            if (channel.chipVoice == ChipVoice::GameBoyPulse ||
                channel.chipVoice == ChipVoice::GameBoyWave) {
                const Envelope& env = channel.envelope;
                const float longest = gameboy::LONGEST_ENVELOPE_SECONDS;

                if (env.attack > longest || env.decay > longest ||
                    env.release > longest) {
                    const float worst =
                        std::max(env.attack, std::max(env.decay, env.release));
                    findings.push_back({
                        AuditSeverity::Note, ch,
                        "This channel's envelope is longer than a Game Boy's "
                        "could be",
                        "The envelope unit steps at 64 Hz through 15 levels "
                        "with a period of at most 7 steps, so its longest "
                        "automatic fade is 1.64 seconds. The longest stage "
                        "here is " + audit::oneDecimal(worst) + " s.",
                        "Shorten it, or leave it - the pitch and volume "
                        "registers are enforced because they are what the "
                        "channel plays; the envelope is left alone because it "
                        "is a patch you voiced."
                    });
                }

                // Attack and decay both present is two directions, and the
                // hardware had one.
                if (env.attack > 0.02f && env.decay > 0.02f &&
                    env.sustain < 0.99f) {
                    findings.push_back({
                        AuditSeverity::Note, ch,
                        "A Game Boy envelope runs in one direction only",
                        "It has a single ramp - up or down, never both - and "
                        "no sustain stage, which is much of why Game Boy "
                        "instruments have their one-shot character. This "
                        "channel has an attack and a decay.",
                        "Set the attack to zero for the usual chip shape: "
                        "straight to full, then a single fall."
                    });
                }
            }
        }
    }

    // Worst first, then by channel, so the panel reads top-down in the order
    // someone would actually act on it.
    std::stable_sort(findings.begin(), findings.end(),
                     [](const AuditFinding& a, const AuditFinding& b) {
                         if (a.severity != b.severity) return a.severity < b.severity;
                         return a.channel < b.channel;
                     });

    // ---- Mastering that is switched on and doing nothing --------------------
    //
    // Both of these are the exact shape this panel exists for: a control
    // that is on, that the user believes is working, and that is a no-op at
    // its current value.
    if (project.masterWidthEnabled &&
        std::fabs(project.masterWidth - 1.0f) < 0.02f) {
        findings.push_back({
            AuditSeverity::Note, -1,
            "Stereo width is on but set to 1.0",
            "At 1.0 the sides are scaled by exactly one, so the widener is "
            "running and changing nothing.",
            "Raise the width above 1.0 on the Master Bus, or switch it off."
        });
    }

    if (project.masterSaturationEnabled &&
        project.masterSaturationDrive <= 1.001f) {
        findings.push_back({
            AuditSeverity::Note, -1,
            "Saturation is on but its drive is at the minimum",
            "Drive 1 passes the signal through untouched by design, so the "
            "saturator is doing nothing at all.",
            "Raise the drive above 1, or switch saturation off."
        });
    }

    /*
     * Width far enough to hollow out the centre.
     *
     * Anything mono - which on a chiptune project is most of it - loses
     * level as the sides are pushed out, and past about 1.7 that is audible
     * as the middle of the mix going thin.
     */
    if (project.masterWidthEnabled && project.masterWidth > 1.7f) {
        findings.push_back({
            AuditSeverity::Warning, -1,
            "Stereo width is very high",
            "Pushing the sides this far thins anything sitting in the "
            "centre, which on a mostly-mono project is the kick, the bass "
            "and often the lead.",
            "Try a width nearer 1.3, and check the mix still holds up in "
            "mono."
        });
    }

    // Makeup gain large enough that the limiter is doing the mastering.
    if (project.masterCompressorEnabled && project.masterLimiterEnabled &&
        project.masterCompMakeup > 8.0f) {
        findings.push_back({
            AuditSeverity::Warning, -1,
            "Master makeup gain is very high",
            "The limiter will be catching most of this rather than the "
            "compressor shaping it, which flattens the mix instead of "
            "gluing it.",
            "Lower the makeup gain, or lower the compressor threshold so it "
            "does the work."
        });
    }

    // A compressor with nothing to compress.
    if (project.masterCompressorEnabled && project.masterCompRatio <= 1.05f) {
        findings.push_back({
            AuditSeverity::Note, -1,
            "Master compressor is on at a ratio of 1:1",
            "A ratio of one applies no compression whatever the threshold "
            "is, so the compressor is running and doing nothing.",
            "Raise the ratio, or switch the compressor off."
        });
    }

    // ---- Voice: a drum kit with nothing in it -------------------------------
    //
    // A kit with no drums enabled produces no notes from a beatboxed take,
    // which looks exactly like the microphone having failed.
    {
        const DrumKit& kit = voiceKitForAudit();
        if (kit.count() == 0) {
            findings.push_back({
                AuditSeverity::Problem, -1,
                "Every drum is switched off in the voice kit",
                "A beatboxed take cannot become any drum, so recording one "
                "produces nothing - which looks like the microphone not "
                "working rather than a setting.",
                "Switch at least one drum back on in Voice to Notes."
            });
        }
    }

    // ---- Plugins that cannot be loaded --------------------------------------
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        const ChannelConfig& channel = project.channels[static_cast<size_t>(ch)];
        for (const PluginSlot& plugin : channel.plugins) {
            if (!plugin.enabled) continue;

            /*
             * A plugin in a project this build cannot host.
             *
             * Reported as a Note rather than a Problem: the settings are
             * safe, and the project will sound right again on a machine
             * that can load it. Silence about it would be worse, because
             * the channel simply sounds wrong with no explanation.
             */
            findings.push_back({
                AuditSeverity::Note, ch,
                "Channel " + channel.name + " uses the plugin \"" +
                    (plugin.name.empty() ? std::string("(unnamed)") : plugin.name) +
                    "\"",
                "This build cannot host plugins, so it is not in the signal "
                "path. Its settings are kept and will come back on a build "
                "that can load it.",
                "Nothing to do - the channel will simply sound as it does "
                "without that plugin."
            });
        }
    }

    return findings;
}

// How many of each severity, for a one-line summary that does not need the
// whole list expanded.
inline int auditCount(const std::vector<AuditFinding>& findings,
                      AuditSeverity severity) {
    int count = 0;
    for (const AuditFinding& finding : findings) {
        if (finding.severity == severity) ++count;
    }
    return count;
}

} // namespace ChiptuneTracker
