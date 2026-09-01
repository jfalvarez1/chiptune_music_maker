#pragma once

/*
 * ChiptuneTracker - Project file format (.ctp)
 *
 * Format v1 stored seven fields per note and nothing else: no channel
 * settings, no effects, no arrangement, no master bus, no groove, and none
 * of the per-note tracker effects. Saving a finished song threw away the
 * entire mix and song structure and kept only the raw notes.
 *
 * v2 stores all of it. v1 files still load - the reader dispatches on the
 * version in the header and simply leaves v1's missing fields at their
 * defaults.
 *
 * Design note: the field tables below are the whole point of this file.
 * Writing and reading both walk the *same* table, so a field cannot be
 * saved but not loaded (which is how Vocoder, the reggaeton kit and
 * Supersaw all silently became Pulse). Adding a setting means adding one
 * row, and it is symmetric by construction.
 *
 * The format stays line-oriented text: diffable, mergeable, greppable, and
 * recoverable by hand if a file is ever damaged.
 */

#include "Types.h"
#include "ProjectValidation.h"
#include "OscillatorNames.h"
#include "Effects.h"   // EffectType and its stable tokens

#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <cstdlib>
#include <iomanip>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>

namespace ChiptuneTracker {
namespace ctp {

// v3 added the per-channel insert-rack order (the FXORDER line).
//
// The bump is honest but the migration is a no-op by construction: a rack
// count of 0 means the classic order, which is what every v1 and v2 file
// implies, so an old project cannot be misread. The old *On flags are still
// written too, so a v3 file also still loads in a 3.6 binary.
// v4 added sends and aux buses.
//
// Migration is a no-op by construction, like v3's: a send destination of -1
// is no send and a bus output of -1 is the master, and both are the
// defaults - which is precisely what a pre-v4 file means. A project that
// never touches routing writes no new lines at all.
// v5 added audio clips on the timeline.
//
// Audio clips write their own ACLIP line rather than extending CLIP: the two
// share almost no fields, and an older reader skips a line it does not
// recognise instead of misparsing a CLIP with unexpected trailing tokens.
// Sample data is not stored - the file carries paths and the pool reloads
// them, which keeps a .ctp a text file you can read.
// v6 added the tempo/meter map, markers and regions.
//
// All four are omit-if-default: a project at one tempo with no markers
// writes exactly the bytes it wrote at v5, so the bump costs nothing to
// anyone who never touches the feature. A v5 reader meeting a TEMPO line
// skips a line it does not know rather than misparsing one it does.
inline constexpr int FORMAT_VERSION = 6;

// ============================================================================
// Field tables
//
// One row per persisted setting. Save and load both iterate these, so the
// two directions cannot disagree about which fields exist or what they are
// called.
// ============================================================================

template <typename Owner>
struct FloatField { const char* key; float Owner::* member; };

template <typename Owner>
struct IntField { const char* key; int Owner::* member; };

template <typename Owner>
struct BoolField { const char* key; bool Owner::* member; };

// --- Channel -----------------------------------------------------------
inline constexpr FloatField<ChannelConfig> CHANNEL_FLOATS[] = {
    {"vol",        &ChannelConfig::volume},
    {"pan",        &ChannelConfig::pan},
    {"detune",     &ChannelConfig::detuneCents},

    {"revMix",     &ChannelConfig::reverbMix},
    {"revSize",    &ChannelConfig::reverbRoomSize},
    {"revDamp",    &ChannelConfig::reverbDamping},

    {"chrMix",     &ChannelConfig::chorusMix},
    {"chrRate",    &ChannelConfig::chorusRate},
    {"chrDepth",   &ChannelConfig::chorusDepth},

    {"dlyMix",     &ChannelConfig::delayMix},
    {"dlyTime",    &ChannelConfig::delayTime},
    {"dlyFb",      &ChannelConfig::delayFeedback},

    {"swWidth",    &ChannelConfig::stereoWidenerWidth},
    {"swHaas",     &ChannelConfig::stereoWidenerHaas},
    {"swMix",      &ChannelConfig::stereoWidenerMix},

    {"tapeDrive",  &ChannelConfig::tapeDrive},
    {"tapeWarm",   &ChannelConfig::tapeWarmth},
    {"tapeComp",   &ChannelConfig::tapeCompression},
    {"tapeMix",    &ChannelConfig::tapeMix},

    {"fltCut",     &ChannelConfig::filterCutoff},
    {"fltRes",     &ChannelConfig::filterResonance},

    {"dstDrive",   &ChannelConfig::distortionDrive},
    {"dstMix",     &ChannelConfig::distortionMix},

    {"bitDepth",   &ChannelConfig::bitDepth},
    {"srDiv",      &ChannelConfig::sampleRateDiv},

    {"phRate",     &ChannelConfig::phaserRate},
    {"phDepth",    &ChannelConfig::phaserDepth},
    {"phFb",       &ChannelConfig::phaserFeedback},

    {"flRate",     &ChannelConfig::flangerRate},
    {"flDepth",    &ChannelConfig::flangerDepth},
    {"flFb",       &ChannelConfig::flangerFeedback},
    {"flMix",      &ChannelConfig::flangerMix},

    {"trmRate",    &ChannelConfig::tremoloRate},
    {"trmDepth",   &ChannelConfig::tremoloDepth},

    {"scAmount",   &ChannelConfig::sidechainAmount},
    {"scRelease",  &ChannelConfig::sidechainRelease},
    {"scAttack",   &ChannelConfig::sidechainAttack},
    {"scThresh",   &ChannelConfig::sidechainThreshold},
    {"rmFreq",     &ChannelConfig::ringModFrequency},
    {"rmMix",      &ChannelConfig::ringModMix},

    {"echTime",    &ChannelConfig::echoTime},
    {"echFb",      &ChannelConfig::echoFeedback},
    {"echMix",     &ChannelConfig::echoMix},

    {"fenvAmt",    &ChannelConfig::filterEnvAmount},
    {"fenvAtk",    &ChannelConfig::filterEnvAttack},
    {"fenvDec",    &ChannelConfig::filterEnvDecay},

    {"eqLow",      &ChannelConfig::eqLow},
    {"eqMid",      &ChannelConfig::eqMid},
    {"eqHigh",     &ChannelConfig::eqHigh},
    {"eqLowF",     &ChannelConfig::eqLowFreq},
    {"eqMidF",     &ChannelConfig::eqMidFreq},
    {"eqHighF",    &ChannelConfig::eqHighFreq},

    {"cmpThr",     &ChannelConfig::compThreshold},
    {"cmpRatio",   &ChannelConfig::compRatio},
    {"cmpAtk",     &ChannelConfig::compAttack},
    {"cmpRel",     &ChannelConfig::compRelease},
    {"cmpGain",    &ChannelConfig::compGain},

    {"fmtRes",     &ChannelConfig::formantResonance},

    {"psSemi",     &ChannelConfig::pitchShiftSemitones},
    {"psMix",      &ChannelConfig::pitchShiftMix},
    {"fsSemi",     &ChannelConfig::formantShiftSemitones},
    {"fsMix",      &ChannelConfig::formantShiftMix},
    {"atStr",      &ChannelConfig::autoTuneStrength},
    {"atMix",      &ChannelConfig::autoTuneMix},
};

inline constexpr IntField<ChannelConfig> CHANNEL_INTS[] = {
    {"fltType",  &ChannelConfig::filterType},
    {"dstType",  &ChannelConfig::distortionType},
    {"scSource", &ChannelConfig::sidechainSource},
    {"fmtVowel", &ChannelConfig::formantVowel},
    {"revAlgo",  &ChannelConfig::reverbAlgorithm},
    {"atRoot",   &ChannelConfig::autoTuneRoot},
    {"atScale",  &ChannelConfig::autoTuneScaleMask},
};

inline constexpr BoolField<ChannelConfig> CHANNEL_BOOLS[] = {
    {"q4bit",  &ChannelConfig::quantizeVolume4Bit},
    {"mute",   &ChannelConfig::muted},
    {"solo",   &ChannelConfig::solo},
    {"arpOn",  &ChannelConfig::arpeggiatorEnabled},
    {"vibOn",  &ChannelConfig::vibratoEnabled},
    {"bitOn",  &ChannelConfig::bitcrusherEnabled},
    {"dstOn",  &ChannelConfig::distortionEnabled},
    {"dlyOn",  &ChannelConfig::delayEnabled},
    {"fltOn",  &ChannelConfig::filterEnabled},
    {"revOn",  &ChannelConfig::reverbEnabled},
    {"chrOn",  &ChannelConfig::chorusEnabled},
    {"swOn",   &ChannelConfig::stereoWidenerEnabled},
    {"tapeOn", &ChannelConfig::tapeSaturationEnabled},
    {"phOn",   &ChannelConfig::phaserEnabled},
    {"flOn",   &ChannelConfig::flangerEnabled},
    {"trmOn",  &ChannelConfig::tremoloEnabled},
    {"scOn",   &ChannelConfig::sidechainEnabled},
    {"rmOn",   &ChannelConfig::ringModEnabled},
    {"echOn",  &ChannelConfig::echoEnabled},
    {"fenvOn", &ChannelConfig::filterEnvEnabled},
    {"eqOn",   &ChannelConfig::eqEnabled},
    {"psOn",   &ChannelConfig::pitchShiftEnabled},
    {"fsOn",   &ChannelConfig::formantShiftEnabled},
    {"atOn",   &ChannelConfig::autoTuneEnabled},
    {"cmpOn",  &ChannelConfig::compressorEnabled},
    {"fmtOn",  &ChannelConfig::formantEnabled},
};

// --- Oscillator and envelope (nested inside ChannelConfig) --------------
inline constexpr FloatField<OscillatorConfig> OSC_FLOATS[] = {
    {"pw",     &OscillatorConfig::pulseWidth},
    {"slope",  &OscillatorConfig::triangleSlope},
    {"det",    &OscillatorConfig::detune},
    {"phase",  &OscillatorConfig::phase},
    {"wtMorph", &OscillatorConfig::wavetableMorph},
    {"wtSweep", &OscillatorConfig::wavetableMorphSweep},
    {"wtTime",  &OscillatorConfig::wavetableSweepTime},
};

inline constexpr IntField<OscillatorConfig> OSC_INTS[] = {
    {"noisePeriod", &OscillatorConfig::noisePeriod},
    {"wtBank",      &OscillatorConfig::wavetableBank},
    {"fmAlgo",      &OscillatorConfig::fmAlgorithmPreset},
};

inline constexpr BoolField<OscillatorConfig> OSC_BOOLS[] = {
    {"shortNoise", &OscillatorConfig::noiseShortMode},
};

// --- Granular ----------------------------------------------------------
inline constexpr FloatField<GranularConfig> GRAIN_FLOATS[] = {
    {"pos",     &GranularConfig::position},
    {"rate",    &GranularConfig::positionRate},
    {"spray",   &GranularConfig::spray},
    {"size",    &GranularConfig::grainSeconds},
    {"density", &GranularConfig::grainsPerSecond},
    {"pitch",   &GranularConfig::pitchSemitones},
    {"jitter",  &GranularConfig::pitchJitter},
    {"reverse", &GranularConfig::reverseChance},
    {"window",  &GranularConfig::windowShape},
};

inline constexpr IntField<GranularConfig> GRAIN_INTS[] = {
    {"sample", &GranularConfig::sampleId},
    {"root",   &GranularConfig::rootKey},
};

inline constexpr BoolField<GranularConfig> GRAIN_BOOLS[] = {
    {"follow", &GranularConfig::followNote},
};

// --- Modelled drums ------------------------------------------------------
inline constexpr FloatField<DrumModelConfig> DRUM_FLOATS[] = {
    {"tune",      &DrumModelConfig::tuneHz},
    {"decay",     &DrumModelConfig::decaySeconds},
    {"level",     &DrumModelConfig::level},
    {"snap",      &DrumModelConfig::snap},
    {"sweep",     &DrumModelConfig::pitchSweepSemitones},
    {"sweepTime", &DrumModelConfig::pitchSweepSeconds},
    {"noiseMix",  &DrumModelConfig::noiseMix},
    {"noiseTone", &DrumModelConfig::noiseTone},
    {"hpf",       &DrumModelConfig::hatHighpass},
    {"vel",       &DrumModelConfig::velocityToLevel},
};

inline constexpr FloatField<Envelope> ENV_FLOATS[] = {
    {"a", &Envelope::attack},
    {"d", &Envelope::decay},
    {"s", &Envelope::sustain},
    {"r", &Envelope::release},
};

// --- Note ---------------------------------------------------------------
// The seven v1 fields are written positionally for readability; everything
// added since is key=value so old and new readers can coexist.
inline constexpr FloatField<Note> NOTE_FLOATS[] = {
    {"vib",   &Note::vibrato},
    {"vibs",  &Note::vibratoSpeed},
    {"sld",   &Note::slide},
    {"swps",  &Note::sweepSpeed},
    {"swpa",  &Note::sweepAmount},
    {"ecd",   &Note::echoDelay},
    {"ecdk",  &Note::echoDecay},
    {"rts",   &Note::retriggerSpeed},
    {"nct",   &Note::noteCut},
    {"ndl",   &Note::noteDelay},
    {"trm",   &Note::tremolo},
    {"trms",  &Note::tremoloSpeed},
    {"prb",   &Note::probability},
};

inline constexpr IntField<Note> NOTE_INTS[] = {
    {"arp",  &Note::arpeggio},
    {"ecr",  &Note::echoRepeats},
    {"rtc",  &Note::retriggerCount},
    {"smp",  &Note::sampleID},
};

inline constexpr BoolField<Note> NOTE_BOOLS[] = {
    {"udty", &Note::useDutyCycle},
};

// --- Project (master bus and groove) ------------------------------------
inline constexpr FloatField<Project> PROJECT_FLOATS[] = {
    {"eqLow",     &Project::masterEQLowGain},
    {"eqMid",     &Project::masterEQMidGain},
    {"eqHigh",    &Project::masterEQHighGain},
    {"cmpThr",    &Project::masterCompThreshold},
    {"cmpRatio",  &Project::masterCompRatio},
    {"cmpAtk",    &Project::masterCompAttack},
    {"cmpRel",    &Project::masterCompRelease},
    {"cmpMakeup", &Project::masterCompMakeup},
    {"limCeil",   &Project::masterLimiterCeiling},
    {"limRel",    &Project::masterLimiterRelease},
    {"swing",     &Project::swing},
    {"swingGrid", &Project::swingGrid},
    {"humAmt",    &Project::humanizeAmount},
    {"humVel",    &Project::humanizeVelocity},
};

inline constexpr BoolField<Project> PROJECT_BOOLS[] = {
    {"eqOn",  &Project::masterEQEnabled},
    {"cmpOn", &Project::masterCompressorEnabled},
    {"limOn", &Project::masterLimiterEnabled},
    {"hum",   &Project::humanize},
    {"nlmix", &Project::chipMixEnabled},
    {"nlflt", &Project::chipFilterEnabled},
    {"nlfam", &Project::chipFilterFamicom},
    {"chip8", &Project::chipAuthentic},
};

// ============================================================================
// Key=value helpers
// ============================================================================

// Nine significant digits round-trips a float exactly.
inline std::string floatToToken(float v) {
    if (!std::isfinite(v)) v = 0.0f;
    std::ostringstream ss;
    ss << std::setprecision(9) << v;
    return ss.str();
}

// Emit only fields that differ from a freshly constructed object. Files stay
// short and readable, and a future default change does not rewrite old files
// into something unrecognisable.
template <typename Owner, size_t N>
void writeFloats(std::ostream& out, const Owner& obj, const Owner& defaults,
                 const FloatField<Owner> (&fields)[N], const char* prefix = "") {
    for (const auto& f : fields) {
        if (obj.*(f.member) != defaults.*(f.member)) {
            out << ' ' << prefix << f.key << '=' << floatToToken(obj.*(f.member));
        }
    }
}

template <typename Owner, size_t N>
void writeInts(std::ostream& out, const Owner& obj, const Owner& defaults,
               const IntField<Owner> (&fields)[N], const char* prefix = "") {
    for (const auto& f : fields) {
        if (obj.*(f.member) != defaults.*(f.member)) {
            out << ' ' << prefix << f.key << '=' << obj.*(f.member);
        }
    }
}

template <typename Owner, size_t N>
void writeBools(std::ostream& out, const Owner& obj, const Owner& defaults,
                const BoolField<Owner> (&fields)[N], const char* prefix = "") {
    for (const auto& f : fields) {
        if (obj.*(f.member) != defaults.*(f.member)) {
            out << ' ' << prefix << f.key << '=' << (obj.*(f.member) ? 1 : 0);
        }
    }
}

// Each of these returns true if the key belonged to that table. Callers chain
// them with || so a type without, say, any int fields simply omits that call.
template <typename Owner, size_t N>
bool applyFloat(Owner& obj, const std::string& key, const std::string& value,
                const FloatField<Owner> (&fields)[N]) {
    for (const auto& f : fields) {
        if (key == f.key) {
            try { obj.*(f.member) = std::stof(value); } catch (...) {}
            return true;
        }
    }
    return false;
}

template <typename Owner, size_t N>
bool applyInt(Owner& obj, const std::string& key, const std::string& value,
              const IntField<Owner> (&fields)[N]) {
    for (const auto& f : fields) {
        if (key == f.key) {
            try { obj.*(f.member) = std::stoi(value); } catch (...) {}
            return true;
        }
    }
    return false;
}

template <typename Owner, size_t N>
bool applyBool(Owner& obj, const std::string& key, const std::string& value,
               const BoolField<Owner> (&fields)[N]) {
    for (const auto& f : fields) {
        if (key == f.key) {
            obj.*(f.member) = (value != "0");
            return true;
        }
    }
    return false;
}

// Split "key=value"; returns false for a token with no '='.
inline bool splitToken(const std::string& token, std::string& key, std::string& value) {
    const size_t eq = token.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    key = token.substr(0, eq);
    value = token.substr(eq + 1);
    return true;
}

// ============================================================================
// Macro lines
//
// Macros are variable-length, so they get their own line rather than a
// key=value token:
//
//   MACRO <channel> <which> <loop> <release> <rate> : <step> <step> ...
//
// An arpeggio macro marks fixed steps with a trailing 'f' on the value, so
//   MACRO 0 arp 0 -1 60 : 0 4f 7
// means "root, the macro value 4 played fixed, fifth".
// ============================================================================
inline void writeMacro(std::ostream& out, int channel, const char* which,
                       const Macro& macro, float rateHz,
                       const std::vector<uint8_t>* fixedFlags = nullptr) {
    if (!macro.isActive()) return;

    out << "MACRO " << channel << ' ' << which << ' '
        << macro.loopStart << ' ' << macro.releaseStep << ' '
        << floatToToken(rateHz) << " :";

    for (size_t i = 0; i < macro.steps.size(); ++i) {
        out << ' ' << macro.steps[i];
        if (fixedFlags && i < fixedFlags->size() && (*fixedFlags)[i]) out << 'f';
    }
    out << "\n";
}

inline void readMacro(const std::string& line, Project& project) {
    std::istringstream iss(line);
    std::string cmd, which;
    int channel = -1, loop = -1, release = -1;
    float rate = 60.0f;

    iss >> cmd >> channel >> which >> loop >> release >> rate;
    if (channel < 0 || channel >= Project::MAX_CHANNELS) return;

    std::string colon;
    iss >> colon;
    if (colon != ":") return;

    std::vector<int> steps;
    std::vector<uint8_t> fixedFlags;
    std::string token;
    while (iss >> token && steps.size() < static_cast<size_t>(Macro::MAX_STEPS)) {
        bool isFixed = false;
        if (!token.empty() && (token.back() == 'f' || token.back() == 'F')) {
            isFixed = true;
            token.pop_back();
        }
        try {
            steps.push_back(std::stoi(token));
            fixedFlags.push_back(isFixed ? 1u : 0u);
        } catch (...) {
            // A damaged step is skipped rather than aborting the macro
        }
    }
    if (steps.empty()) return;

    InstrumentMacros& macros = project.channels[channel].macros;
    macros.rateHz = rate;

    auto fill = [&](Macro& m) {
        m.enabled = true;
        m.steps = steps;
        m.loopStart = loop;
        m.releaseStep = release;
    };

    if (which == "vol")       fill(macros.volume);
    else if (which == "arp") { fill(macros.arpeggio); macros.arpeggio.fixed = fixedFlags; }
    else if (which == "duty") fill(macros.duty);
    else if (which == "pitch") fill(macros.pitch);
}

// Quote a string for a single-line field, escaping the quote character.
inline std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n' || c == '\r') { out += ' '; continue; }
        out += c;
    }
    out += '"';
    return out;
}

// Read a quoted string starting at `pos`; leaves `pos` just past the closing
// quote. Returns an empty string if the line is malformed.
inline std::string unquote(const std::string& line, size_t& pos) {
    while (pos < line.size() && line[pos] != '"') ++pos;
    if (pos >= line.size()) return {};
    ++pos; // skip opening quote

    std::string out;
    while (pos < line.size()) {
        char c = line[pos++];
        if (c == '\\' && pos < line.size()) { out += line[pos++]; continue; }
        if (c == '"') break;
        out += c;
    }
    return out;
}

} // namespace ctp

// ============================================================================
// Save
// ============================================================================
/*
 * Has this channel been touched?
 *
 * Used to decide whether a channel past the original eight earns a line in
 * the file. A channel counts as used if any clip plays on it, if it carries
 * sends or macros, or if any of its settings differ from the default -
 * which is checked by writing its tokens and seeing whether any appear.
 */
inline bool channelIsUsed(const Project& project, int channel,
                          const ChannelConfig& defaults) {
    const ChannelConfig& c = project.channels[static_cast<size_t>(channel)];

    for (const Clip& clip : project.arrangement) {
        if (clip.channelIndex == channel) return true;
    }

    if (c.name != defaults.name) return true;
    if (c.oscillator.type != defaults.oscillator.type) return true;
    if (c.fxSlotCount != 0) return true;
    if (c.sidechainBus >= 0) return true;
    for (const SendConfig& send : c.sends) {
        if (send.destination >= 0) return true;
    }
    if (!c.macros.volume.steps.empty() || !c.macros.arpeggio.steps.empty() ||
        !c.macros.duty.steps.empty() || !c.macros.pitch.steps.empty()) {
        return true;
    }

    std::ostringstream probe;
    ctp::writeFloats(probe, c, defaults, ctp::CHANNEL_FLOATS);
    ctp::writeInts(probe, c, defaults, ctp::CHANNEL_INTS);
    ctp::writeBools(probe, c, defaults, ctp::CHANNEL_BOOLS);
    ctp::writeFloats(probe, c.oscillator, defaults.oscillator, ctp::OSC_FLOATS, "osc.");
    ctp::writeInts(probe, c.oscillator, defaults.oscillator, ctp::OSC_INTS, "osc.");
    ctp::writeBools(probe, c.oscillator, defaults.oscillator, ctp::OSC_BOOLS, "osc.");
    ctp::writeFloats(probe, c.envelope, defaults.envelope, ctp::ENV_FLOATS, "env.");
    return !probe.str().empty();
}

inline bool writeProject(std::ostream& file, const Project& project) {
    const Project projectDefaults;

    const Note noteDefaults;

    file << "CHIPTUNE_PROJECT v" << ctp::FORMAT_VERSION << "\n";
    file << "NAME " << project.name << "\n";
    file << "BPM " << ctp::floatToToken(project.bpm) << "\n";
    file << "BEATS_PER_MEASURE " << project.beatsPerMeasure << "\n";
    file << "MASTER_VOLUME " << ctp::floatToToken(project.masterVolume) << "\n";
    file << "SONG_LENGTH " << ctp::floatToToken(project.songLength) << "\n";

    // Master bus and groove
    file << "MASTER";
    ctp::writeFloats(file, project, projectDefaults, ctp::PROJECT_FLOATS);
    ctp::writeBools(file, project, projectDefaults, ctp::PROJECT_BOOLS);
    file << "\n\n";

    // Channels
    //
    // The omit-if-default rule has to compare against the default for *this*
    // channel, not a bare ChannelConfig: Project's constructor gives each of
    // the eight channels its own name, oscillator, volume and pan. Comparing
    // against a bare ChannelConfig omitted any value that happened to match
    // the struct default, and the loader then kept the channel's own default
    // instead - so a deliberate setting silently reverted.
    for (int ch = 0; ch < Project::MAX_CHANNELS; ++ch) {
        const ChannelConfig& c = project.channels[ch];
        const ChannelConfig& d = projectDefaults.channels[ch];

        // Raising the cap to 32 would otherwise have added 24 CHANNEL lines
        // to every file for channels nobody touched. The first eight are
        // always written so the format is unchanged for a chip project;
        // beyond that, a channel earns its line by differing from default
        // or by being used.
        if (ch >= Project::CHIP_CHANNELS && !channelIsUsed(project, ch, d)) {
            continue;
        }

        file << "CHANNEL " << ch << ' ' << ctp::quote(c.name)
             << ' ' << oscillatorTypeToString(c.oscillator.type);

        ctp::writeFloats(file, c, d, ctp::CHANNEL_FLOATS);
        ctp::writeInts(file, c, d, ctp::CHANNEL_INTS);
        ctp::writeBools(file, c, d, ctp::CHANNEL_BOOLS);

        // Nested oscillator and envelope settings, namespaced by prefix
        ctp::writeFloats(file, c.oscillator, d.oscillator, ctp::OSC_FLOATS, "osc.");
        ctp::writeInts(file, c.oscillator, d.oscillator, ctp::OSC_INTS, "osc.");
        ctp::writeBools(file, c.oscillator, d.oscillator, ctp::OSC_BOOLS, "osc.");
        ctp::writeFloats(file, c.oscillator.granular, d.oscillator.granular,
                         ctp::GRAIN_FLOATS, "gr.");
        ctp::writeInts(file, c.oscillator.granular, d.oscillator.granular,
                       ctp::GRAIN_INTS, "gr.");
        ctp::writeBools(file, c.oscillator.granular, d.oscillator.granular,
                        ctp::GRAIN_BOOLS, "gr.");
        ctp::writeFloats(file, c.oscillator.drumModel, d.oscillator.drumModel,
                         ctp::DRUM_FLOATS, "dm.");
        // The drum voice is an enum, so it rides as a plain int rather than
        // going through the typed tables.
        if (c.oscillator.drumModel.voice != d.oscillator.drumModel.voice) {
            file << " dm.voice="
                 << static_cast<int>(c.oscillator.drumModel.voice);
        }

        ctp::writeFloats(file, c.envelope, d.envelope, ctp::ENV_FLOATS, "env.");

        file << "\n";

        /*
         * The FM patch, on its own line after the channel's.
         *
         * Not rows in the OSC_FLOATS table: it is six operators of ten
         * fields plus a 36-entry matrix, a hundred-odd values that would be
         * a hundred table rows used by one oscillator type. Written only
         * when the channel actually is an FM channel, so a project that
         * never touches it gains nothing.
         *
         * After the channel's newline, beside SEND and FXORDER. Emitting it
         * before that newline appended every token to the CHANNEL line, and
         * the reader then saw one malformed CHANNEL line and no FM line at
         * all - which failed every field of the round trip at once,
         * including the oscillator type that has nothing to do with FM.
         */
        if (c.oscillator.type == OscillatorType::FMSynth) {
            const FMPatch& fm = c.oscillator.fm;
            file << "FM " << ch << ' ' << ctp::floatToToken(fm.index) << ' '
                 << ctp::floatToToken(fm.algorithm.feedback);
            for (int op = 0; op < FM_OPERATORS; ++op) {
                const FMOperator& o = fm.operators[static_cast<size_t>(op)];
                file << ' ' << ctp::floatToToken(o.ratio)
                     << ' ' << ctp::floatToToken(o.fixedHz)
                     << ' ' << ctp::floatToToken(o.level)
                     << ' ' << ctp::floatToToken(o.detuneCents)
                     << ' ' << ctp::floatToToken(o.phaseOffset)
                     << ' ' << ctp::floatToToken(o.attack)
                     << ' ' << ctp::floatToToken(o.decay)
                     << ' ' << ctp::floatToToken(o.sustain)
                     << ' ' << ctp::floatToToken(o.release)
                     << ' ' << ctp::floatToToken(o.velocitySensitivity)
                     << ' ' << (o.enabled ? 1 : 0);
            }
            for (int m = 0; m < FM_OPERATORS; ++m) {
                file << ' ' << ctp::floatToToken(fm.algorithm.carrier[static_cast<size_t>(m)]);
                for (int cc = 0; cc < FM_OPERATORS; ++cc) {
                    file << ' ' << ctp::floatToToken(
                        fm.algorithm.modulation[static_cast<size_t>(m)][static_cast<size_t>(cc)]);
                }
            }
            file << "\n";
        }

        /*
         * Modulation, written only when there is any.
         *
         * The LFOs and the second envelope go on the MOD line; each route
         * gets its own MROUTE. Per-route lines rather than one long one so a
         * .ctp stays readable and a diff shows which route changed, which is
         * the whole reason this format is text.
         */
        {
            const ModMatrix& m = c.oscillator.modMatrix;
            const ModMatrix& dm = d.oscillator.modMatrix;
            const bool interesting =
                m.routeCount > 0 || m.polyphonyLimit != dm.polyphonyLimit ||
                m.pitchBendSemitones != dm.pitchBendSemitones;

            if (interesting) {
                file << "MOD " << ch << ' ' << m.polyphonyLimit << ' '
                     << ctp::floatToToken(m.pitchBendSemitones) << ' '
                     << ctp::floatToToken(m.env2Attack) << ' '
                     << ctp::floatToToken(m.env2Decay) << ' '
                     << ctp::floatToToken(m.env2Sustain) << ' '
                     << ctp::floatToToken(m.env2Release);
                for (int i = 0; i < ModMatrix::LFO_COUNT; ++i) {
                    const LFOConfig& lfo = m.lfos[static_cast<size_t>(i)];
                    file << ' ' << static_cast<int>(lfo.shape)
                         << ' ' << ctp::floatToToken(lfo.rateHz)
                         << ' ' << ctp::floatToToken(lfo.delaySeconds)
                         << ' ' << ctp::floatToToken(lfo.fadeSeconds)
                         << ' ' << (lfo.retrigger ? 1 : 0);
                }
                file << "\n";

                for (int i = 0; i < m.routeCount; ++i) {
                    const ModRoute& route = m.routes[static_cast<size_t>(i)];
                    file << "MROUTE " << ch << ' '
                         << static_cast<int>(route.source) << ' '
                         << static_cast<int>(route.destination) << ' '
                         << ctp::floatToToken(route.amount) << ' '
                         << (route.enabled ? 1 : 0) << "\n";
                }
            }
        }

        // The multisample instrument: one line for its own settings, one
        // per zone. Per-zone lines rather than a single enormous one so a
        // 64-zone piano stays diffable and readable by hand, which is the
        // entire reason this format is text.
        if (c.oscillator.type == OscillatorType::Sampler) {
            const SamplerInstrument& s = c.oscillator.sampler;
            file << "SAMPLER " << ch << ' '
                 << ctp::floatToToken(s.velocityCrossfade) << ' '
                 << ctp::floatToToken(s.attack) << ' '
                 << ctp::floatToToken(s.decay) << ' '
                 << ctp::floatToToken(s.sustain) << ' '
                 << ctp::floatToToken(s.release) << ' '
                 << ctp::floatToToken(s.velocityToLevel) << "\n";

            for (int z = 0; z < s.zoneCount; ++z) {
                const SampleZone& zone = s.zones[static_cast<size_t>(z)];
                file << "SZONE " << ch << ' ' << zone.sampleId << ' '
                     << zone.lowKey << ' ' << zone.highKey << ' '
                     << zone.rootKey << ' '
                     << ctp::floatToToken(zone.lowVelocity) << ' '
                     << ctp::floatToToken(zone.highVelocity) << ' '
                     << ctp::floatToToken(zone.gain) << ' '
                     << ctp::floatToToken(zone.pan) << ' '
                     << ctp::floatToToken(zone.tuneCents) << ' '
                     << (zone.loop ? 1 : 0) << ' '
                     << ctp::floatToToken(zone.loopStartSeconds) << ' '
                     << ctp::floatToToken(zone.loopEndSeconds) << ' '
                     << zone.roundRobinGroup << "\n";
            }
        }

        // Sends, written only when they go somewhere.
        for (int slot = 0; slot < MAX_SENDS_PER_CHANNEL; ++slot) {
            const SendConfig& send = c.sends[static_cast<size_t>(slot)];
            if (send.destination < 0) continue;
            file << "SEND " << ch << ' ' << slot << ' ' << send.destination
                 << ' ' << ctp::floatToToken(send.level)
                 << ' ' << (send.preFader ? 1 : 0) << "\n";
        }
        if (c.sidechainBus >= 0) {
            file << "SCBUS " << ch << ' ' << c.sidechainBus << "\n";
        }

        // The insert-rack order, written only when it is not the classic
        // one - so a project nobody reordered produces the same bytes it
        // did before v3.
        if (c.fxSlotCount > 0) {
            file << "FXORDER " << ch;
            for (int slot = 0; slot < c.fxSlotCount && slot < MAX_FX_SLOTS; ++slot) {
                const uint8_t raw = c.fxOrder[static_cast<size_t>(slot)];
                if (raw >= static_cast<uint8_t>(EffectType::Count)) continue;
                file << ' ' << effectTypeId(static_cast<EffectType>(raw));
            }
            file << "\n";
        }

        // Instrument macros follow their channel, one line each
        ctp::writeMacro(file, ch, "vol",   c.macros.volume,   c.macros.rateHz);
        ctp::writeMacro(file, ch, "arp",   c.macros.arpeggio, c.macros.rateHz,
                        &c.macros.arpeggio.fixed);
        ctp::writeMacro(file, ch, "duty",  c.macros.duty,     c.macros.rateHz);
        ctp::writeMacro(file, ch, "pitch", c.macros.pitch,    c.macros.rateHz);
    }
    file << "\n";

    // Aux buses. Written only when a bus differs from its default, so a
    // project that never opened the routing panel is byte-identical to one
    // written before v4.
    {
        const AuxBusConfig busDefaults;
        for (int bus = 0; bus < Project::MAX_AUX_BUSES; ++bus) {
            const AuxBusConfig& b = project.auxBuses[static_cast<size_t>(bus)];

            const bool headerDiffers =
                b.name != busDefaults.name ||
                b.volume != busDefaults.volume ||
                b.pan != busDefaults.pan ||
                b.muted != busDefaults.muted ||
                b.output != busDefaults.output;

            std::ostringstream fxTokens;
            ctp::writeFloats(fxTokens, b.strip, busDefaults.strip, ctp::CHANNEL_FLOATS);
            ctp::writeInts(fxTokens, b.strip, busDefaults.strip, ctp::CHANNEL_INTS);
            ctp::writeBools(fxTokens, b.strip, busDefaults.strip, ctp::CHANNEL_BOOLS);
            const std::string fx = fxTokens.str();

            const bool orderDiffers = b.strip.fxSlotCount > 0;

            if (!headerDiffers && fx.empty() && !orderDiffers) continue;

            file << "AUXBUS " << bus << ' ' << ctp::quote(b.name)
                 << " out=" << b.output
                 << " vol=" << ctp::floatToToken(b.volume)
                 << " pan=" << ctp::floatToToken(b.pan)
                 << " mute=" << (b.muted ? 1 : 0) << "\n";

            if (!fx.empty()) {
                file << "AUXFX " << bus << fx << "\n";
            }
            if (orderDiffers) {
                file << "AUXORDER " << bus;
                for (int slot = 0; slot < b.strip.fxSlotCount && slot < MAX_FX_SLOTS; ++slot) {
                    const uint8_t raw = b.strip.fxOrder[static_cast<size_t>(slot)];
                    if (raw >= static_cast<uint8_t>(EffectType::Count)) continue;
                    file << ' ' << effectTypeId(static_cast<EffectType>(raw));
                }
                file << "\n";
            }
        }
    }

    // Tempo and meter changes, markers and regions. Written before the
    // arrangement so a reader has the map in hand by the time it places
    // anything against it.
    for (int i = 0; i < project.tempoMap.tempoCount(); ++i) {
        const TempoChange& change = project.tempoMap.tempoAt(i);
        file << "TEMPO " << ctp::floatToToken(change.beat) << ' '
             << ctp::floatToToken(change.bpm) << "\n";
    }
    for (int i = 0; i < project.tempoMap.meterCount(); ++i) {
        const MeterChange& change = project.tempoMap.meterAt(i);
        file << "METER " << ctp::floatToToken(change.beat) << ' '
             << change.numerator << ' ' << change.denominator << "\n";
    }
    for (const Marker& marker : project.markers) {
        file << "MARKER " << ctp::floatToToken(marker.beat) << ' '
             << marker.color << ' ' << ctp::quote(marker.name) << "\n";
    }
    for (const Region& region : project.regions) {
        file << "REGION " << ctp::floatToToken(region.startBeat) << ' '
             << ctp::floatToToken(region.endBeat) << ' '
             << region.color << ' ' << ctp::quote(region.name) << "\n";
    }
    if (!project.tempoMap.isFlat() || !project.markers.empty() ||
        !project.regions.empty()) {
        file << "\n";
    }

    // Samples the project references, by path. Written before the clips
    // that point at them so a reader has the pool populated by the time it
    // meets an ACLIP.
    {
        const int sampleCount = project.samplePool.count();
        for (int id = 0; id < sampleCount; ++id) {
            const Sample* sample = project.samplePool.getSample(id);
            if (sample == nullptr || sample->filepath.empty()) continue;
            file << "SAMPLE " << id << ' ' << ctp::quote(sample->filepath)
                 << ' ' << ctp::quote(sample->name) << "\n";
        }
        if (sampleCount > 0) file << "\n";
    }

    // Patterns
    for (const Pattern& pattern : project.patterns) {
        file << "PATTERN " << ctp::quote(pattern.name) << ' ' << pattern.length << "\n";

        for (const Note& note : pattern.notes) {
            // v1-compatible positional prefix
            file << "NOTE "
                 << note.pitch << ' '
                 << ctp::floatToToken(note.startTime) << ' '
                 << ctp::floatToToken(note.duration) << ' '
                 << ctp::floatToToken(note.velocity) << ' '
                 << oscillatorTypeToString(note.oscillatorType) << ' '
                 << ctp::floatToToken(note.fadeIn) << ' '
                 << ctp::floatToToken(note.fadeOut);

            // v2 extensions
            ctp::writeFloats(file, note, noteDefaults, ctp::NOTE_FLOATS);
            ctp::writeInts(file, note, noteDefaults, ctp::NOTE_INTS);
            ctp::writeBools(file, note, noteDefaults, ctp::NOTE_BOOLS);

            if (note.dutyCycle != noteDefaults.dutyCycle) {
                file << " duty=" << static_cast<int>(note.dutyCycle);
            }
            if (note.sweepDirection != noteDefaults.sweepDirection) {
                file << " swpd=" << static_cast<int>(note.sweepDirection);
            }

            file << "\n";
        }

        file << "END_PATTERN\n\n";
    }

    // Arrangement
    for (const Clip& clip : project.arrangement) {
        // transpose rides at the end so a pre-3.7 reader simply stops
        // before it, and a missing token below reads as 0 - the old
        // behaviour either way.
        if (clip.type == ClipType::Audio) {
            file << "ACLIP " << clip.sampleId << ' ' << clip.channelIndex << ' '
                 << ctp::floatToToken(clip.startBeat) << ' '
                 << ctp::floatToToken(clip.lengthBeats) << ' '
                 << clip.color << ' '
                 << ctp::floatToToken(clip.gain) << ' '
                 << ctp::floatToToken(clip.trimStartSeconds) << ' '
                 << ctp::floatToToken(clip.trimEndSeconds) << ' '
                 << ctp::floatToToken(clip.fadeInBeats) << ' '
                 << ctp::floatToToken(clip.fadeOutBeats) << ' '
                 << (clip.loopClip ? 1 : 0) << "\n";
            continue;
        }

        file << "CLIP " << clip.patternIndex << ' ' << clip.channelIndex << ' '
             << ctp::floatToToken(clip.startBeat) << ' '
             << ctp::floatToToken(clip.lengthBeats) << ' '
             << clip.color << ' ' << clip.transpose << "\n";
    }
    if (!project.arrangement.empty()) file << "\n";

    file << "END_PROJECT\n";
    return file.good();
}

// The file wrappers are deliberately thin. Undo snapshots go through the same
// writeProject/readProject pair, so the two paths cannot drift apart in what
// they preserve - an undo that silently dropped a field would be much harder
// to notice than a save that did.
inline bool saveProjectFile(const Project& project, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    return writeProject(file, project);
}

// ============================================================================
// Load
// ============================================================================
inline bool readProject(std::istream& file, Project& project) {
    std::string line;
    if (!std::getline(file, line)) return false;
    if (line.find("CHIPTUNE_PROJECT") == std::string::npos) return false;

    // Start from a clean project so fields absent from the file (a v1 file,
    // or a truncated one) take their defaults rather than whatever the
    // caller's Project happened to hold.
    project = Project();
    project.patterns.clear();
    project.arrangement.clear();

    Pattern* currentPattern = nullptr;

    /*
     * File sample id -> pool sample id.
     *
     * These are not the same number. The writer emits the pool index it had
     * at save time; on load, loadSample() assigns whatever index the pool
     * hands out. One sample that no longer exists on disk shifts every later
     * index by one, and without this map every ACLIP after the missing file
     * would play the wrong audio - silently, since a valid index into the
     * wrong sample looks exactly like a correct load.
     */
    std::unordered_map<int, int> sampleIdMap;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "NAME") {
            std::getline(iss >> std::ws, project.name);
        }
        else if (cmd == "BPM")               { iss >> project.bpm; }
        else if (cmd == "BEATS_PER_MEASURE") { iss >> project.beatsPerMeasure; }
        else if (cmd == "MASTER_VOLUME")     { iss >> project.masterVolume; }
        else if (cmd == "SONG_LENGTH")       { iss >> project.songLength; }
        else if (cmd == "MASTER") {
            std::string token, key, value;
            while (iss >> token) {
                if (!ctp::splitToken(token, key, value)) continue;
                ctp::applyFloat(project, key, value, ctp::PROJECT_FLOATS) ||
                ctp::applyBool(project, key, value, ctp::PROJECT_BOOLS);
            }
        }
        else if (cmd == "CHANNEL") {
            int index = -1;
            iss >> index;
            if (index < 0 || index >= Project::MAX_CHANNELS) continue;

            ChannelConfig& c = project.channels[index];

            // The name is the first quoted run on the line; everything after
            // the closing quote is the oscillator name then key=value tokens.
            size_t pos = 0;
            c.name = ctp::unquote(line, pos);

            std::istringstream rest(line.substr(pos));
            std::string token;
            if (rest >> token) {
                c.oscillator.type = stringToOscillatorType(token);
            }

            std::string key, value;
            while (rest >> token) {
                if (!ctp::splitToken(token, key, value)) continue;

                if (key.rfind("osc.", 0) == 0) {
                    const std::string sub = key.substr(4);
                    ctp::applyFloat(c.oscillator, sub, value, ctp::OSC_FLOATS) ||
                    ctp::applyInt(c.oscillator, sub, value, ctp::OSC_INTS) ||
                    ctp::applyBool(c.oscillator, sub, value, ctp::OSC_BOOLS);
                } else if (key.rfind("gr.", 0) == 0) {
                    const std::string sub = key.substr(3);
                    ctp::applyFloat(c.oscillator.granular, sub, value,
                                    ctp::GRAIN_FLOATS) ||
                    ctp::applyInt(c.oscillator.granular, sub, value,
                                  ctp::GRAIN_INTS) ||
                    ctp::applyBool(c.oscillator.granular, sub, value,
                                   ctp::GRAIN_BOOLS);
                } else if (key.rfind("dm.", 0) == 0) {
                    const std::string sub = key.substr(3);
                    if (sub == "voice") {
                        // The drum voice is an enum; the typed tables only
                        // know float, int and bool members.
                        const int raw = std::atoi(value.c_str());
                        if (raw >= 0 &&
                            raw < static_cast<int>(DrumVoiceType::Count)) {
                            c.oscillator.drumModel.voice =
                                static_cast<DrumVoiceType>(raw);
                        }
                    } else {
                        ctp::applyFloat(c.oscillator.drumModel, sub, value,
                                        ctp::DRUM_FLOATS);
                    }
                } else if (key.rfind("env.", 0) == 0) {
                    const std::string sub = key.substr(4);
                    ctp::applyFloat(c.envelope, sub, value, ctp::ENV_FLOATS);
                } else {
                    ctp::applyFloat(c, key, value, ctp::CHANNEL_FLOATS) ||
                    ctp::applyInt(c, key, value, ctp::CHANNEL_INTS) ||
                    ctp::applyBool(c, key, value, ctp::CHANNEL_BOOLS);
                }
            }
        }
        else if (cmd == "MACRO") {
            ctp::readMacro(line, project);
        }
        else if (cmd == "PATTERN") {
            size_t pos = 0;
            std::string patternName = ctp::unquote(line, pos);
            if (patternName.empty()) patternName = "Pattern";

            int length = Pattern::DEFAULT_LENGTH;
            std::istringstream rest(line.substr(pos));
            rest >> length;

            project.patterns.push_back(Pattern());
            currentPattern = &project.patterns.back();
            currentPattern->name = patternName;
            currentPattern->length = length;
        }
        else if (cmd == "NOTE" && currentPattern) {
            Note note;
            std::string oscTypeStr;
            iss >> note.pitch >> note.startTime >> note.duration
                >> note.velocity >> oscTypeStr >> note.fadeIn >> note.fadeOut;
            note.oscillatorType = stringToOscillatorType(oscTypeStr);

            std::string token, key, value;
            while (iss >> token) {
                if (!ctp::splitToken(token, key, value)) continue;

                if (key == "duty") {
                    try {
                        note.dutyCycle = static_cast<DutyCycle>(std::stoi(value));
                    } catch (...) {}
                } else if (key == "swpd") {
                    try {
                        note.sweepDirection = static_cast<SweepDirection>(std::stoi(value));
                    } catch (...) {}
                } else {
                    ctp::applyFloat(note, key, value, ctp::NOTE_FLOATS) ||
                    ctp::applyInt(note, key, value, ctp::NOTE_INTS) ||
                    ctp::applyBool(note, key, value, ctp::NOTE_BOOLS);
                }
            }

            currentPattern->notes.push_back(note);
        }
        else if (cmd == "END_PATTERN") {
            currentPattern = nullptr;
        }
        else if (cmd == "SEND") {
            int channel = -1, slot = -1, destination = -1, pre = 0;
            float level = 0.0f;
            iss >> channel >> slot >> destination >> level >> pre;
            if (channel >= 0 && channel < Project::MAX_CHANNELS &&
                slot >= 0 && slot < MAX_SENDS_PER_CHANNEL) {
                SendConfig& send =
                    project.channels[static_cast<size_t>(channel)].sends[static_cast<size_t>(slot)];
                send.destination = destination;
                send.level = level;
                send.preFader = (pre != 0);
            }
        }
        else if (cmd == "SCBUS") {
            int channel = -1, bus = -1;
            iss >> channel >> bus;
            if (channel >= 0 && channel < Project::MAX_CHANNELS) {
                project.channels[static_cast<size_t>(channel)].sidechainBus = bus;
            }
        }
        else if (cmd == "AUXBUS") {
            int index = -1;
            iss >> index;
            if (index >= 0 && index < Project::MAX_AUX_BUSES) {
                AuxBusConfig& b = project.auxBuses[static_cast<size_t>(index)];

                size_t pos = 0;
                b.name = ctp::unquote(line, pos);

                std::istringstream rest(line.substr(pos));
                std::string token, key, value;
                while (rest >> token) {
                    if (!ctp::splitToken(token, key, value)) continue;
                    if (key == "out")       b.output = std::atoi(value.c_str());
                    else if (key == "vol")  b.volume = std::strtof(value.c_str(), nullptr);
                    else if (key == "pan")  b.pan = std::strtof(value.c_str(), nullptr);
                    else if (key == "mute") b.muted = (value == "1");
                }
            }
        }
        else if (cmd == "AUXFX") {
            int index = -1;
            iss >> index;
            if (index >= 0 && index < Project::MAX_AUX_BUSES) {
                ChannelConfig& strip = project.auxBuses[static_cast<size_t>(index)].strip;
                std::string token, key, value;
                while (iss >> token) {
                    if (!ctp::splitToken(token, key, value)) continue;
                    ctp::applyFloat(strip, key, value, ctp::CHANNEL_FLOATS) ||
                    ctp::applyInt(strip, key, value, ctp::CHANNEL_INTS) ||
                    ctp::applyBool(strip, key, value, ctp::CHANNEL_BOOLS);
                }
            }
        }
        else if (cmd == "AUXORDER") {
            int index = -1;
            iss >> index;
            if (index >= 0 && index < Project::MAX_AUX_BUSES) {
                ChannelConfig& strip = project.auxBuses[static_cast<size_t>(index)].strip;
                int count = 0;
                std::string token;
                while ((iss >> token) && count < MAX_FX_SLOTS) {
                    EffectType type;
                    if (!effectTypeFromId(token.c_str(), type)) continue;
                    strip.fxOrder[static_cast<size_t>(count++)] =
                        static_cast<uint8_t>(type);
                }
                strip.fxSlotCount = count;
            }
        }
        else if (cmd == "FXORDER") {
            // Carries its own channel index rather than relying on read
            // order, so a hand-edited or reordered file cannot land a rack
            // on the wrong channel.
            int index = -1;
            iss >> index;
            if (index >= 0 && index < Project::MAX_CHANNELS) {
                ChannelConfig& target = project.channels[static_cast<size_t>(index)];
                int count = 0;
                std::string token;
                // Unknown tokens are dropped rather than guessed at - a
                // newer version's effect has no sensible substitute here.
                while ((iss >> token) && count < MAX_FX_SLOTS) {
                    EffectType type;
                    if (!effectTypeFromId(token.c_str(), type)) continue;
                    target.fxOrder[static_cast<size_t>(count++)] =
                        static_cast<uint8_t>(type);
                }
                target.fxSlotCount = count;
            }
        }
        else if (cmd == "MOD") {
            int ch = -1;
            iss >> ch;
            if (ch >= 0 && ch < Project::MAX_CHANNELS) {
                ModMatrix& m = project.channels[static_cast<size_t>(ch)]
                                   .oscillator.modMatrix;
                iss >> m.polyphonyLimit >> m.pitchBendSemitones
                    >> m.env2Attack >> m.env2Decay >> m.env2Sustain
                    >> m.env2Release;
                for (int i = 0; i < ModMatrix::LFO_COUNT; ++i) {
                    LFOConfig& lfo = m.lfos[static_cast<size_t>(i)];
                    int shape = 0;
                    int retrigger = 1;
                    iss >> shape >> lfo.rateHz >> lfo.delaySeconds
                        >> lfo.fadeSeconds >> retrigger;
                    if (shape >= 0 && shape < static_cast<int>(LFOShape::Count)) {
                        lfo.shape = static_cast<LFOShape>(shape);
                    }
                    lfo.retrigger = (retrigger != 0);
                }
                // The routes arrive on their own lines after this one.
                m.routeCount = 0;
            }
        }
        else if (cmd == "MROUTE") {
            int ch = -1;
            iss >> ch;
            if (ch >= 0 && ch < Project::MAX_CHANNELS) {
                ModMatrix& m = project.channels[static_cast<size_t>(ch)]
                                   .oscillator.modMatrix;
                int source = 0, destination = 0, enabled = 1;
                ModRoute route;
                iss >> source >> destination >> route.amount >> enabled;
                // A source or destination a newer build wrote becomes None
                // rather than wrapping onto whatever sits at that index,
                // which would silently route the file's LFO somewhere else.
                route.source = (source >= 0 && source < static_cast<int>(ModSource::Count))
                    ? static_cast<ModSource>(source) : ModSource::None;
                route.destination =
                    (destination >= 0 &&
                     destination < static_cast<int>(ModDestination::Count))
                        ? static_cast<ModDestination>(destination)
                        : ModDestination::None;
                route.enabled = (enabled != 0);
                m.addRoute(route);
            }
        }
        else if (cmd == "SAMPLER") {
            int ch = -1;
            iss >> ch;
            if (ch >= 0 && ch < Project::MAX_CHANNELS) {
                SamplerInstrument& s =
                    project.channels[static_cast<size_t>(ch)].oscillator.sampler;
                iss >> s.velocityCrossfade >> s.attack >> s.decay
                    >> s.sustain >> s.release >> s.velocityToLevel;
                // The zones arrive on their own lines after this one, so the
                // count starts at zero and each SZONE appends.
                s.zoneCount = 0;
            }
        }
        else if (cmd == "SZONE") {
            int ch = -1;
            iss >> ch;
            if (ch >= 0 && ch < Project::MAX_CHANNELS) {
                SamplerInstrument& s =
                    project.channels[static_cast<size_t>(ch)].oscillator.sampler;
                SampleZone zone;
                int loop = 0;
                iss >> zone.sampleId >> zone.lowKey >> zone.highKey
                    >> zone.rootKey >> zone.lowVelocity >> zone.highVelocity
                    >> zone.gain >> zone.pan >> zone.tuneCents >> loop
                    >> zone.loopStartSeconds >> zone.loopEndSeconds
                    >> zone.roundRobinGroup;
                zone.loop = (loop != 0);
                s.addZone(zone);
            }
        }
        else if (cmd == "FM") {
            int ch = -1;
            iss >> ch;
            if (ch >= 0 && ch < Project::MAX_CHANNELS) {
                FMPatch& fm = project.channels[static_cast<size_t>(ch)].oscillator.fm;
                iss >> fm.index >> fm.algorithm.feedback;
                for (int op = 0; op < FM_OPERATORS; ++op) {
                    FMOperator& o = fm.operators[static_cast<size_t>(op)];
                    int enabled = 1;
                    iss >> o.ratio >> o.fixedHz >> o.level >> o.detuneCents
                        >> o.phaseOffset >> o.attack >> o.decay >> o.sustain
                        >> o.release >> o.velocitySensitivity >> enabled;
                    o.enabled = (enabled != 0);
                }
                for (int m = 0; m < FM_OPERATORS; ++m) {
                    iss >> fm.algorithm.carrier[static_cast<size_t>(m)];
                    for (int cc = 0; cc < FM_OPERATORS; ++cc) {
                        iss >> fm.algorithm.modulation[static_cast<size_t>(m)]
                                                      [static_cast<size_t>(cc)];
                    }
                }
            }
        }
        else if (cmd == "TEMPO") {
            float beat = 0.0f, bpm = 120.0f;
            iss >> beat >> bpm;
            project.tempoMap.setTempo(beat, bpm);
        }
        else if (cmd == "METER") {
            float beat = 0.0f;
            int numerator = 4, denominator = 4;
            iss >> beat >> numerator >> denominator;
            project.tempoMap.setMeter(beat, numerator, denominator);
        }
        else if (cmd == "MARKER") {
            Marker marker;
            iss >> marker.beat >> marker.color;
            size_t pos = 0;
            marker.name = ctp::unquote(line, pos);
            if (static_cast<int>(project.markers.size()) < MAX_MARKERS) {
                project.markers.push_back(marker);
            }
        }
        else if (cmd == "REGION") {
            Region region;
            iss >> region.startBeat >> region.endBeat >> region.color;
            size_t pos = 0;
            region.name = ctp::unquote(line, pos);
            if (static_cast<int>(project.regions.size()) < MAX_REGIONS) {
                project.regions.push_back(region);
            }
        }
        else if (cmd == "SAMPLE") {
            int fileId = -1;
            iss >> fileId;

            size_t pos = 0;
            const std::string path = ctp::unquote(line, pos);
            const std::string name = ctp::unquote(line, pos);

            // Audio is reloaded from disk, not stored in the file. A sample
            // that has moved is reported rather than silently dropped - a
            // clip playing silence with no explanation is worse than one
            // that says which file went missing.
            if (!path.empty()) {
                const int loaded = project.samplePool.loadSample(path);
                if (loaded < 0) {
                    project.missingSamples.push_back(path);
                } else {
                    if (!name.empty()) project.samplePool.renameSample(loaded, name);
                    sampleIdMap[fileId] = loaded;
                }
            }
        }
        else if (cmd == "ACLIP") {
            Clip clip;
            clip.type = ClipType::Audio;
            int loopFlag = 0;
            int fileSampleId = -1;
            iss >> fileSampleId >> clip.channelIndex
                >> clip.startBeat >> clip.lengthBeats >> clip.color
                >> clip.gain >> clip.trimStartSeconds >> clip.trimEndSeconds
                >> clip.fadeInBeats >> clip.fadeOutBeats >> loopFlag;
            clip.loopClip = (loopFlag != 0);

            // A clip whose sample did not load keeps its place on the
            // timeline with sampleId -1. Dropping it would lose the edit -
            // the trim, the fades, the position - for a file the user may
            // simply have moved and can point us at again.
            const auto mapped = sampleIdMap.find(fileSampleId);
            clip.sampleId = (mapped != sampleIdMap.end()) ? mapped->second : -1;

            project.arrangement.push_back(clip);
        }
        else if (cmd == "CLIP") {
            Clip clip;
            iss >> clip.patternIndex >> clip.channelIndex
                >> clip.startBeat >> clip.lengthBeats >> clip.color;
            // Optional sixth token, absent from every file before 3.7. The
            // failed extraction leaves the default 0 and poisons only the
            // stream, which is discarded at the end of this line anyway.
            iss >> clip.transpose;
            project.arrangement.push_back(clip);
        }
        else if (cmd == "END_PROJECT") {
            break;
        }
    }

    if (project.patterns.empty()) {
        project.patterns.push_back(Pattern());
        project.patterns[0].name = "Pattern 1";
    }

    // Everything from here on is untrusted-input hygiene.
    clampProjectToValidRanges(project);
    return true;
}

inline bool loadProjectFile(Project& project, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    return readProject(file, project);
}

} // namespace ChiptuneTracker
