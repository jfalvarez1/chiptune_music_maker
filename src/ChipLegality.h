#pragma once

// ============================================================================
// Would this run on the machine?
//
// The single clearest thing the community research turned up: chiptune
// listeners do not object to modern production. They object to being told
// something is a chiptune when it could not run on a chip. The complaint is
// about SILENCE, not about impossibility - a track made with a sampler and a
// convolution reverb is fine and welcome, right up to the moment it is
// entered in a category that means "this is a NES ROM".
//
// Battle of the Bits encodes exactly that as a ladder - nsf_classic through
// chipflavored to fakebit - and the useful thing about it is that it is a
// LADDER OF CLAIMS, not of quality. Nobody there thinks fakebit is worse.
// They think mislabelling is worse.
//
// So this file computes which rung a project is on and, more importantly,
// WHY - naming the specific channel and the specific setting that moved it.
// It never blocks anything and it never suggests fixing anything, because
// there is usually nothing to fix. It answers a question that is otherwise
// unanswerable from inside the program, and then gets out of the way.
// ============================================================================

#include <algorithm>
#include <string>
#include <vector>

#include "Types.h"

namespace ChiptuneTracker {

// ============================================================================
// The rungs
// ============================================================================
/*
 * ORDERED, because the verdict is the worst finding and that needs a
 * comparison. The order is "how much hardware would be required", not
 * "how good".
 */
enum class ChipTier : int {
    HardwareLegal = 0,  // a stock machine could play this
    Expansion,          // a real chip could, but not the stock one
    ChipFlavoured,      // chip voices through processing the machine had none of
    Fakebit             // chip-style music made on a modern synth
};

inline constexpr int CHIP_TIER_COUNT = 4;

inline const char* chipTierName(ChipTier tier) {
    switch (tier) {
        case ChipTier::HardwareLegal: return "Hardware-legal";
        case ChipTier::Expansion:     return "Expansion hardware";
        case ChipTier::ChipFlavoured: return "Chip-flavoured";
        case ChipTier::Fakebit:       return "Fakebit";
    }
    return "Fakebit";
}

/*
 * What each rung means, in the words the label is short for.
 *
 * Every one of these is deliberately neutral. "Fakebit" is a real term the
 * scene uses without contempt, and if this text implied otherwise the
 * feature would be doing the opposite of its purpose.
 */
inline const char* chipTierDescription(ChipTier tier) {
    switch (tier) {
        case ChipTier::HardwareLegal:
            return "Every channel is held to a real chip and nothing here "
                   "needs hardware the machine did not have.";
        case ChipTier::Expansion:
            return "A real chip could play this, but not a stock one - it "
                   "needs more voices or a different chip than the base "
                   "machine had. Cartridges did exactly this.";
        case ChipTier::ChipFlavoured:
            return "Chip voices through processing no console had. This is "
                   "most released chiptune, and it is not a lesser thing - "
                   "it just would not run on the hardware.";
        case ChipTier::Fakebit:
            return "Chip-style music made on a modern synth. A perfectly "
                   "ordinary way to write this music; it simply is not a "
                   "claim about hardware.";
    }
    return "";
}

// One reason the verdict is what it is.
struct ChipLegalityNote {
    ChipTier caps = ChipTier::HardwareLegal;  // the best rung this allows
    int channel = -1;                          // -1 for project-wide
    std::string what;                          // the setting, named
    std::string why;                           // what the hardware could not do
};

struct ChipLegalityReport {
    ChipTier tier = ChipTier::HardwareLegal;
    std::vector<ChipLegalityNote> notes;   // worst first
    int contentChannels = 0;
    int constrainedChannels = 0;           // channels held to a chip voice
    std::string summary;
};

namespace chiplegal {

// Does this channel carry anything? A setting on a channel nobody uses has
// no bearing on whether the music would play.
inline bool channelHasContent(const Project& project, int channel) {
    for (const Clip& clip : project.arrangement) {
        if (clip.channelIndex == channel) return true;
    }
    return false;
}

/*
 * What kind of hardware an oscillator would need.
 *
 * The awkward cases are the interesting ones and none of them is arbitrary:
 *
 * - A WAVETABLE is chip-native. The Game Boy's third channel is literally a
 *   32-step wavetable and so is the PC Engine's. Filing it with the modern
 *   engines would be wrong about the actual history.
 * - FM is chip-native too - a YM2612 is a chip and a VRC7 plugged into a
 *   Famicom - but it is not a 2A03, so it needs expansion hardware rather
 *   than none.
 * - A SAWTOOTH is a real chip waveform on a SID or a POKEY and on nothing
 *   we model, so it lands on the same rung as FM for the same reason.
 * - A SINE is the odd one out: essentially no classic sound chip generated
 *   one directly, and the ones that approached it did it through a
 *   wavetable. It is flavour rather than expansion.
 */
inline ChipTier tierForOscillator(OscillatorType type) {
    switch (type) {
        case OscillatorType::Pulse:
        case OscillatorType::Triangle:
        case OscillatorType::Noise:
        case OscillatorType::Custom:     // wavetable - Game Boy channel 3
        case OscillatorType::SynthChip:  // a 12.5% pulse under another name
            return ChipTier::HardwareLegal;

        case OscillatorType::FMSynth:
        case OscillatorType::Sawtooth:
            return ChipTier::Expansion;

        case OscillatorType::Sine:
            return ChipTier::ChipFlavoured;

        // Sampled and granular playback is the line. A DPCM channel played
        // back short samples, but nothing on these machines streamed
        // arbitrary audio or resynthesised it, and calling that legal would
        // empty the word out.
        default:
            return ChipTier::Fakebit;
    }
}

// The name, for a report that has to say which engine.
inline const char* oscillatorFamilyName(OscillatorType type) {
    switch (tierForOscillator(type)) {
        case ChipTier::HardwareLegal: return "a chip oscillator";
        case ChipTier::Expansion:     return "an expansion-chip oscillator";
        case ChipTier::ChipFlavoured: return "a waveform no chip generated directly";
        default:                      return "a modern engine";
    }
}

}  // namespace chiplegal

/*
 * The verdict, and every reason for it.
 *
 * Two things this deliberately does NOT do:
 *
 * - It does not care whether notes are in range. A channel with a chip voice
 *   already plays what the register would produce, saturation and all, so a
 *   bass note written below a Game Boy's floor is not illegal - it is
 *   exactly what the machine would emit. Project Check reports it because it
 *   is probably not what you wanted; it is not reported here because it is
 *   not a hardware question.
 *
 * - It does not offer fixes. Every other advisory in this program suggests
 *   what to do, and this one must not: there is usually nothing wrong. A
 *   reverb on a lead is a choice, and a panel telling you to remove it in
 *   order to score better would be inventing a hierarchy the scene does not
 *   have.
 */
inline ChipLegalityReport auditChipLegality(const Project& project) {
    ChipLegalityReport report;

    const int activeChannels = project.activeChannelCount();

    for (int ch = 0; ch < activeChannels; ++ch) {
        if (!chiplegal::channelHasContent(project, ch)) continue;
        const ChannelConfig& channel = project.channels[static_cast<size_t>(ch)];
        ++report.contentChannels;
        if (channel.chipVoice != ChipVoice::None) ++report.constrainedChannels;

        // ---- The engine ----------------------------------------------------
        const ChipTier engineTier = chiplegal::tierForOscillator(channel.oscillator.type);
        if (engineTier != ChipTier::HardwareLegal) {
            report.notes.push_back({
                engineTier, ch,
                std::string(channel.name) + " uses " +
                    chiplegal::oscillatorFamilyName(channel.oscillator.type),
                (engineTier == ChipTier::Expansion)
                    ? "Real chip hardware, but not the base machine's - a "
                      "cartridge would have had to carry it."
                    : "No sound chip of the era produced this directly."
            });
        }

        // ---- Nothing holding it to the registers ---------------------------
        //
        // A pulse channel with no chip voice is a float oscillator that
        // sounds like a chip. That is the whole distinction this panel
        // exists to draw, so it cannot be skipped just because the waveform
        // looks right.
        if (engineTier == ChipTier::HardwareLegal &&
            channel.chipVoice == ChipVoice::None) {
            report.notes.push_back({
                ChipTier::ChipFlavoured, ch,
                std::string(channel.name) + " is not held to a chip",
                "The waveform is one a chip could make, but its pitch and "
                "volume are not going through any register - so it can play "
                "notes and levels the hardware had no way to represent."
            });
        }

        // ---- Stereo, which these machines did not have ---------------------
        if (std::fabs(channel.pan) > 0.01f) {
            report.notes.push_back({
                ChipTier::ChipFlavoured, ch,
                std::string(channel.name) + " is panned off centre",
                "A 2A03 and a DMG are mono. Stereo chiptune is a recording "
                "decision, not something the machine did."
            });
        }

        // ---- Effects the console had no silicon for ------------------------
        struct EffectFlag { bool on; const char* name; };
        const EffectFlag EFFECTS[] = {
            {channel.reverbEnabled,       "reverb"},
            {channel.convolutionEnabled,  "convolution reverb"},
            {channel.delayEnabled,        "delay"},
            {channel.chorusEnabled,       "chorus"},
            {channel.phaserEnabled,       "a phaser"},
            {channel.flangerEnabled,      "a flanger"},
            {channel.pitchShiftEnabled,   "pitch shifting"},
            {channel.formantShiftEnabled, "formant shifting"},
            {channel.autoTuneEnabled,     "auto-tune"},
            {channel.formantEnabled,      "a formant filter"},
            {channel.filterEnabled,       "a filter"},
        };
        std::vector<std::string> present;
        for (const EffectFlag& effect : EFFECTS) {
            if (effect.on) present.push_back(effect.name);
        }
        if (!present.empty()) {
            std::string list = present[0];
            for (size_t i = 1; i + 1 < present.size(); ++i) list += ", " + present[i];
            if (present.size() > 1) list += " and " + present.back();

            report.notes.push_back({
                ChipTier::ChipFlavoured, ch,
                std::string(channel.name) + " runs " + list,
                "Nothing on these machines processed a channel after it left "
                "the oscillator. The console's own output filter is the only "
                "filtering there was, and it was not adjustable."
            });
        }
    }

    // ---- More voices than the machine has ----------------------------------
    //
    // Five on a stock 2A03 - two pulses, triangle, noise, DPCM. This is the
    // count people are surprised by, because the program offers thirty-two
    // and eight of them look like a chip.
    if (report.contentChannels > 5) {
        report.notes.push_back({
            ChipTier::Expansion, -1,
            std::to_string(report.contentChannels) +
                " channels carry music; a stock 2A03 has five",
            "Two pulses, a triangle, noise and DPCM. Cartridges added more "
            "with expansion chips - VRC6, Sunsoft 5B, Namco 163 - so this is "
            "ordinary, but it is a cartridge rather than a console."
        });
    }

    // ---- The master chain --------------------------------------------------
    //
    // Only stereo width, and the omissions are deliberate. A limiter, an EQ
    // and saturation are things you do to a RECORDING of a console, which
    // every released NES rip has had done to it - flagging those would mean
    // calling every legitimate chip release illegal. Widening a stereo image
    // is different in kind: there was no stereo image to widen.
    //
    // There is no master reverb in this program to check, which is why this
    // block is one test rather than the two it looks like it should be.
    if (project.masterWidthEnabled && std::fabs(project.masterWidth - 1.0f) > 0.02f) {
        report.notes.push_back({
            ChipTier::ChipFlavoured, -1,
            "The master bus is widening the stereo image",
            "There is no stereo image on a mono machine to widen. On a "
            "centred chip mix there is no side signal at all, so this is "
            "audible only after something else has made one."
        });
    }

    // ---- The verdict -------------------------------------------------------
    for (const ChipLegalityNote& note : report.notes) {
        if (static_cast<int>(note.caps) > static_cast<int>(report.tier)) {
            report.tier = note.caps;
        }
    }

    // Worst first, then by channel, so the list reads in the order that
    // explains the verdict.
    std::stable_sort(report.notes.begin(), report.notes.end(),
                     [](const ChipLegalityNote& a, const ChipLegalityNote& b) {
                         if (a.caps != b.caps)
                             return static_cast<int>(a.caps) > static_cast<int>(b.caps);
                         return a.channel < b.channel;
                     });

    if (report.contentChannels == 0) {
        report.tier = ChipTier::HardwareLegal;
        report.notes.clear();
        report.summary = "Nothing to judge yet - no channel is carrying music.";
    } else if (report.notes.empty()) {
        report.summary = "This would run on the hardware.";
    } else {
        report.summary = std::string(chipTierName(report.tier)) + " - " +
                         std::to_string(report.notes.size()) +
                         (report.notes.size() == 1 ? " reason" : " reasons");
    }

    return report;
}

}  // namespace ChiptuneTracker
