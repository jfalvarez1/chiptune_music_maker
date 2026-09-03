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

    if (project.chipAuthentic) {
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
