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

#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>

namespace ChiptuneTracker {
namespace ctp {

inline constexpr int FORMAT_VERSION = 2;

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
};

inline constexpr IntField<ChannelConfig> CHANNEL_INTS[] = {
    {"fltType",  &ChannelConfig::filterType},
    {"dstType",  &ChannelConfig::distortionType},
    {"scSource", &ChannelConfig::sidechainSource},
    {"fmtVowel", &ChannelConfig::formantVowel},
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
    {"echOn",  &ChannelConfig::echoEnabled},
    {"fenvOn", &ChannelConfig::filterEnvEnabled},
    {"eqOn",   &ChannelConfig::eqEnabled},
    {"cmpOn",  &ChannelConfig::compressorEnabled},
    {"fmtOn",  &ChannelConfig::formantEnabled},
};

// --- Oscillator and envelope (nested inside ChannelConfig) --------------
inline constexpr FloatField<OscillatorConfig> OSC_FLOATS[] = {
    {"pw",     &OscillatorConfig::pulseWidth},
    {"slope",  &OscillatorConfig::triangleSlope},
    {"det",    &OscillatorConfig::detune},
    {"phase",  &OscillatorConfig::phase},
};

inline constexpr IntField<OscillatorConfig> OSC_INTS[] = {
    {"noisePeriod", &OscillatorConfig::noisePeriod},
};

inline constexpr BoolField<OscillatorConfig> OSC_BOOLS[] = {
    {"shortNoise", &OscillatorConfig::noiseShortMode},
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

        file << "CHANNEL " << ch << ' ' << ctp::quote(c.name)
             << ' ' << oscillatorTypeToString(c.oscillator.type);

        ctp::writeFloats(file, c, d, ctp::CHANNEL_FLOATS);
        ctp::writeInts(file, c, d, ctp::CHANNEL_INTS);
        ctp::writeBools(file, c, d, ctp::CHANNEL_BOOLS);

        // Nested oscillator and envelope settings, namespaced by prefix
        ctp::writeFloats(file, c.oscillator, d.oscillator, ctp::OSC_FLOATS, "osc.");
        ctp::writeInts(file, c.oscillator, d.oscillator, ctp::OSC_INTS, "osc.");
        ctp::writeBools(file, c.oscillator, d.oscillator, ctp::OSC_BOOLS, "osc.");
        ctp::writeFloats(file, c.envelope, d.envelope, ctp::ENV_FLOATS, "env.");

        file << "\n";

        // Instrument macros follow their channel, one line each
        ctp::writeMacro(file, ch, "vol",   c.macros.volume,   c.macros.rateHz);
        ctp::writeMacro(file, ch, "arp",   c.macros.arpeggio, c.macros.rateHz,
                        &c.macros.arpeggio.fixed);
        ctp::writeMacro(file, ch, "duty",  c.macros.duty,     c.macros.rateHz);
        ctp::writeMacro(file, ch, "pitch", c.macros.pitch,    c.macros.rateHz);
    }
    file << "\n";

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
        file << "CLIP " << clip.patternIndex << ' ' << clip.channelIndex << ' '
             << ctp::floatToToken(clip.startBeat) << ' '
             << ctp::floatToToken(clip.lengthBeats) << ' '
             << clip.color << "\n";
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
        else if (cmd == "CLIP") {
            Clip clip;
            iss >> clip.patternIndex >> clip.channelIndex
                >> clip.startBeat >> clip.lengthBeats >> clip.color;
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
