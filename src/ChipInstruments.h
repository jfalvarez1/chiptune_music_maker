#pragma once

// ============================================================================
// Mega Drive FM patches: .tfi and .vgi
//
// There are decades of YM2612 patches in the world, in two tiny interchange
// formats that every Mega Drive tracker reads and writes: TFI from TFM Music
// Maker and VGI from VGM Music Maker. Both are a fixed handful of bytes with
// no header, no version and no strings - the whole file is the patch.
//
// Being able to open one means a bass or a brass stab somebody voiced on
// real hardware is one file away, instead of forty sliders away.
//
// WHAT THIS IS AND IS NOT. This is a TRANSLATION, not emulation. The
// synthesiser here is a six-operator phase-modulation engine with continuous
// ADSR envelopes; a YM2612 is four operators with rate-based envelopes,
// key-scaling, SSG-EG loops and a detune table that varies with the note. An
// imported patch lands in the same family as the original - the same
// algorithm, the same operator balance, the same rough envelope shapes - and
// it is not the same sound. Claiming otherwise would be worse than saying so
// here.
//
// Specifically dropped, because there is nothing to map them onto:
//   - RS / key scaling (envelopes here do not vary with pitch)
//   - SSG-EG (looping envelope segments)
//   - AMS / PMS / the chip LFO (the mod matrix covers this differently)
//
// HOW A WRONG GUESS FAILS. Neither format has a magic number, so "is this
// really a TFI" can only be answered by whether every field lands inside the
// range its chip register allows. Every one is checked. A file of the right
// length whose bytes are not a patch is rejected with a reason rather than
// imported as noise, which is the failure mode that matters: a bad import
// that loads is one somebody spends an hour trying to fix.
// ============================================================================

#include "FMSynth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// The chip's own view of a patch
// ============================================================================
/*
 * One YM2612 operator, in register units.
 *
 * Kept as the chip stores it rather than converted on the way in, so the
 * parsing and the translation are separate things that can be tested
 * separately. A file that parses into sane register values and translates
 * badly is a different bug from one that never parsed.
 */
struct YMOperator {
    int multiple = 1;    // MUL,  0..15  (0 means one half)
    int detune = 0;      // DT,   0..7   (4..7 are the negative side)
    int totalLevel = 0;  // TL,   0..127 (0 is loudest)
    int rateScale = 0;   // RS,   0..3   - dropped in translation
    int attack = 31;     // AR,   0..31
    int decay = 0;       // DR,   0..31
    int sustainRate = 0; // SR / D2R, 0..31
    int release = 15;    // RR,   0..15
    int sustainLevel = 0;// SL / D1L, 0..15 (15 is silence)
    int ssgEg = 0;       // 0..15 - dropped in translation
};

struct YMPatch {
    int algorithm = 0;   // 0..7
    int feedback = 0;    // 0..7
    int pms = 0;         // 0..7,  VGI only
    int ams = 0;         // 0..3,  VGI only
    YMOperator operators[4];
    std::string name;    // the file's own name; the formats carry none
};

// Why an import was refused, in the words the user needs.
enum class ChipImportError {
    Ok = 0,
    FileNotFound,
    WrongSize,
    OutOfRange,
    Unsupported
};

inline const char* chipImportErrorText(ChipImportError error) {
    switch (error) {
        case ChipImportError::Ok:           return "loaded";
        case ChipImportError::FileNotFound: return "the file could not be opened";
        case ChipImportError::WrongSize:
            return "the file is not the size a TFI or VGI patch is - it is "
                   "probably a different kind of file with that extension";
        case ChipImportError::OutOfRange:
            return "the bytes are the right length but are not a patch - some "
                   "of them fall outside what the chip registers can hold";
        case ChipImportError::Unsupported:  return "that format is not read yet";
    }
    return "unknown";
}

// ============================================================================
// Parsing
// ============================================================================
inline constexpr size_t TFI_SIZE = 42;   // 2 + 4 operators x 10
inline constexpr size_t VGI_SIZE = 43;   // 4 + 4 operators x 10, minus one

namespace detail {

// Every field, against the width of the register it came from. This is the
// whole verification, since neither format has a magic number.
inline bool ymOperatorInRange(const YMOperator& op) {
    return op.multiple     >= 0 && op.multiple     <= 15 &&
           op.detune       >= 0 && op.detune       <= 7  &&
           op.totalLevel   >= 0 && op.totalLevel   <= 127 &&
           op.rateScale    >= 0 && op.rateScale    <= 3  &&
           op.attack       >= 0 && op.attack       <= 31 &&
           op.decay        >= 0 && op.decay        <= 31 &&
           op.sustainRate  >= 0 && op.sustainRate  <= 31 &&
           op.release      >= 0 && op.release      <= 15 &&
           op.sustainLevel >= 0 && op.sustainLevel <= 15 &&
           op.ssgEg        >= 0 && op.ssgEg        <= 15;
}

inline void readOperator(const uint8_t* bytes, YMOperator& op) {
    op.multiple     = bytes[0];
    op.detune       = bytes[1];
    op.totalLevel   = bytes[2];
    op.rateScale    = bytes[3];
    op.attack       = bytes[4];
    op.decay        = bytes[5];
    op.sustainRate  = bytes[6];
    op.release      = bytes[7];
    op.sustainLevel = bytes[8];
    op.ssgEg        = bytes[9];
}

inline void writeOperator(const YMOperator& op, uint8_t* bytes) {
    bytes[0] = static_cast<uint8_t>(op.multiple);
    bytes[1] = static_cast<uint8_t>(op.detune);
    bytes[2] = static_cast<uint8_t>(op.totalLevel);
    bytes[3] = static_cast<uint8_t>(op.rateScale);
    bytes[4] = static_cast<uint8_t>(op.attack);
    bytes[5] = static_cast<uint8_t>(op.decay);
    bytes[6] = static_cast<uint8_t>(op.sustainRate);
    bytes[7] = static_cast<uint8_t>(op.release);
    bytes[8] = static_cast<uint8_t>(op.sustainLevel);
    bytes[9] = static_cast<uint8_t>(op.ssgEg);
}

}  // namespace detail

/*
 * Parse a buffer as TFI or VGI, deciding by its length.
 *
 * The two formats are the same operator block behind a different header:
 * TFI is algorithm and feedback, VGI adds the LFO sensitivities. Length is
 * the only thing that distinguishes them and it is enough, because they
 * differ by exactly one byte and nothing else is 42 or 43 bytes long by
 * coincidence often enough to matter - and if it is, the range check below
 * catches it.
 */
inline ChipImportError parseYMPatch(const uint8_t* bytes, size_t size,
                                    YMPatch& out) {
    if (bytes == nullptr) return ChipImportError::FileNotFound;

    size_t operatorsAt = 0;
    if (size == TFI_SIZE) {
        out.algorithm = bytes[0];
        out.feedback = bytes[1];
        out.pms = 0;
        out.ams = 0;
        operatorsAt = 2;
    } else if (size == VGI_SIZE) {
        out.algorithm = bytes[0];
        out.feedback = bytes[1];
        // One byte carries both sensitivities: PMS in the low three bits,
        // AMS in the next two.
        out.pms = bytes[2] & 0x07;
        out.ams = (bytes[2] >> 4) & 0x03;
        operatorsAt = 3;
    } else {
        return ChipImportError::WrongSize;
    }

    if (out.algorithm > 7 || out.feedback > 7) return ChipImportError::OutOfRange;

    for (int i = 0; i < 4; ++i) {
        detail::readOperator(bytes + operatorsAt + static_cast<size_t>(i) * 10,
                             out.operators[i]);
        if (!detail::ymOperatorInRange(out.operators[i])) {
            return ChipImportError::OutOfRange;
        }
    }

    return ChipImportError::Ok;
}

inline ChipImportError loadYMPatchFile(const std::string& path, YMPatch& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return ChipImportError::FileNotFound;

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() != TFI_SIZE && bytes.size() != VGI_SIZE) {
        return ChipImportError::WrongSize;
    }

    const ChipImportError result = parseYMPatch(bytes.data(), bytes.size(), out);
    if (result != ChipImportError::Ok) return result;

    // The formats carry no name, so the filename is the only one there is.
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    const size_t from = (slash == std::string::npos) ? 0 : slash + 1;
    const size_t to = (dot == std::string::npos || dot < from) ? path.size() : dot;
    out.name = path.substr(from, to - from);

    return ChipImportError::Ok;
}

// ============================================================================
// Translating to this engine
// ============================================================================
namespace detail {

/*
 * A YM2612 envelope rate as a time in seconds.
 *
 * The chip counts down a rate; higher is faster, and each step of four
 * roughly halves the time. Rate 0 means the segment never advances, which is
 * a very long time rather than an instant one - getting that backwards turns
 * a slow pad into a click.
 *
 * Approximate on purpose. The hardware's rate also depends on the note and
 * on key scaling, neither of which exists here, so a table copied to the
 * sample would be false precision.
 */
inline float ymRateToSeconds(int rate, float shortest = 0.0008f) {
    if (rate <= 0) return 30.0f;
    const int clamped = std::clamp(rate, 1, 31);
    return shortest * std::pow(2.0f, static_cast<float>(31 - clamped) / 3.0f);
}

// TL is attenuation in units of 0.75 dB, and 0 is loudest.
inline float ymTotalLevelToGain(int totalLevel) {
    const int clamped = std::clamp(totalLevel, 0, 127);
    if (clamped >= 127) return 0.0f;
    return std::pow(10.0f, -0.75f * static_cast<float>(clamped) / 20.0f);
}

// SL is where the decay stops, in units of 3 dB. 15 means silence, not
// "-45 dB" - the chip treats the top code as the floor.
inline float ymSustainLevelToGain(int sustainLevel) {
    const int clamped = std::clamp(sustainLevel, 0, 15);
    if (clamped >= 15) return 0.0f;
    return std::pow(10.0f, -3.0f * static_cast<float>(clamped) / 20.0f);
}

// MUL is the harmonic number, except that 0 means a half.
inline float ymMultipleToRatio(int multiple) {
    const int clamped = std::clamp(multiple, 0, 15);
    return (clamped == 0) ? 0.5f : static_cast<float>(clamped);
}

// DT is three magnitudes and a sign bit. The real amount varies with the
// note; this is the small fixed detune that gives the same thickening.
inline float ymDetuneToCents(int detune) {
    const int clamped = std::clamp(detune, 0, 7);
    const int magnitude = clamped & 0x03;
    const float cents = static_cast<float>(magnitude) * 6.0f;
    return (clamped >= 4) ? -cents : cents;
}

}  // namespace detail

/*
 * Which of the four operators reach the output, per algorithm.
 *
 * The YM2612's eight algorithms, as connections among operators 1 to 4.
 * Written out rather than derived, because they are a fixed table in
 * silicon and any formula for them would be a coincidence.
 */
inline void ymAlgorithmConnections(int algorithm,
                                   bool modulates[4][4],
                                   bool carrier[4]) {
    for (int m = 0; m < 4; ++m) {
        carrier[m] = false;
        for (int c = 0; c < 4; ++c) modulates[m][c] = false;
    }

    auto link = [&](int from, int to) { modulates[from - 1][to - 1] = true; };
    auto out = [&](int op) { carrier[op - 1] = true; };

    switch (std::clamp(algorithm, 0, 7)) {
        case 0:  link(1,2); link(2,3); link(3,4); out(4); break;
        case 1:  link(1,3); link(2,3); link(3,4); out(4); break;
        case 2:  link(1,4); link(2,3); link(3,4); out(4); break;
        case 3:  link(1,2); link(2,4); link(3,4); out(4); break;
        case 4:  link(1,2); link(3,4); out(2); out(4); break;
        case 5:  link(1,2); link(1,3); link(1,4); out(2); out(3); out(4); break;
        case 6:  link(1,2); out(2); out(3); out(4); break;
        default: out(1); out(2); out(3); out(4); break;   // 7, all carriers
    }
}

/*
 * Where each chip operator lives in this engine's six.
 *
 * The engine evaluates from operator 5 down to 0 and only allows a higher
 * index to modulate a lower one, so the chain has to run downward. Chip
 * operator 1 goes to engine operator 5 for a second reason: that is the only
 * one with self-feedback, and on a YM2612 feedback is on operator 1.
 *
 * Engine operators 0 and 1 are left disabled. A four-operator patch is four
 * operators; filling the spare two with anything would be inventing sound
 * the file does not contain.
 */
inline int engineOperatorForChip(int chipOperator) {
    static constexpr int MAP[4] = {5, 4, 3, 2};
    return MAP[std::clamp(chipOperator, 0, 3)];
}

/*
 * Translate a parsed chip patch into an FMPatch this engine can play.
 *
 * Deliberately separate from parsing: a file that reads into sane register
 * values and still sounds wrong is a different bug from one that never
 * parsed, and keeping them apart is what makes either testable.
 */
inline void ymPatchToFM(const YMPatch& patch, FMPatch& out) {
    out = FMPatch();
    out.algorithm = FMAlgorithm();      // everything off; filled in below
    out.algorithm.feedback =
        std::clamp(static_cast<float>(patch.feedback) / 7.0f, 0.0f, 1.0f);

    for (int i = 0; i < FM_OPERATORS; ++i) {
        out.operators[static_cast<size_t>(i)].enabled = false;
    }

    bool modulates[4][4];
    bool carrier[4];
    ymAlgorithmConnections(patch.algorithm, modulates, carrier);

    int carrierCount = 0;
    for (int i = 0; i < 4; ++i) if (carrier[i]) ++carrierCount;
    if (carrierCount <= 0) carrierCount = 1;

    for (int i = 0; i < 4; ++i) {
        const YMOperator& source = patch.operators[i];
        const size_t slot = static_cast<size_t>(engineOperatorForChip(i));
        FMOperator& target = out.operators[slot];

        target.enabled = true;
        target.ratio = detail::ymMultipleToRatio(source.multiple);
        target.fixedHz = -1.0f;
        target.level = detail::ymTotalLevelToGain(source.totalLevel);
        target.detuneCents = detail::ymDetuneToCents(source.detune);
        target.phaseOffset = 0.0f;

        target.attack = detail::ymRateToSeconds(source.attack);
        target.decay = detail::ymRateToSeconds(source.decay);
        target.sustain = detail::ymSustainLevelToGain(source.sustainLevel);

        // RR is four bits where the others are five: the chip doubles it and
        // adds one to get the same scale. Treating it as a five-bit rate
        // would make every release about eight times too long.
        target.release = detail::ymRateToSeconds(source.release * 2 + 1);

        // A sustain rate of zero means the note holds; anything else is a
        // slow decay the engine has no separate stage for, so it is folded
        // into the sustain level rather than dropped silently.
        if (source.sustainRate > 0) {
            const float fade = 1.0f -
                std::clamp(static_cast<float>(source.sustainRate) / 31.0f,
                           0.0f, 0.9f);
            target.sustain *= fade;
        }

        target.velocitySensitivity = carrier[i] ? 0.0f : 0.5f;

        for (int c = 0; c < 4; ++c) {
            if (!modulates[i][c]) continue;
            const size_t to = static_cast<size_t>(engineOperatorForChip(c));
            // Depth is 1 because the modulator's own level already carries
            // its TL, which is what sets how bright it makes its carrier.
            out.algorithm.modulation[slot][to] = 1.0f;
        }

        if (carrier[i]) {
            // Shared out among the carriers, so an algorithm with four of
            // them is not four times louder than one with a single carrier.
            // A pure gain change: every carrier is scaled the same, so the
            // balance the patch was voiced with survives.
            out.algorithm.carrier[slot] =
                1.0f / static_cast<float>(carrierCount);
        }
    }
}

// The whole journey, for the common case.
inline ChipImportError importFMPatchFile(const std::string& path, FMPatch& out,
                                         std::string* nameOut = nullptr) {
    YMPatch patch;
    const ChipImportError result = loadYMPatchFile(path, patch);
    if (result != ChipImportError::Ok) return result;

    ymPatchToFM(patch, out);
    if (nameOut != nullptr) *nameOut = patch.name;
    return ChipImportError::Ok;
}

// ============================================================================
// Writing one back out
// ============================================================================
/*
 * Serialise a chip patch as TFI.
 *
 * The inverse of the parser rather than of the translation: what goes out is
 * the register values that came in, so a patch imported and exported without
 * being edited is the same forty-two bytes. Exporting a patch that was
 * voiced HERE would mean inverting an approximation, which cannot be
 * lossless and is a separate job.
 */
inline void writeTFI(const YMPatch& patch, uint8_t* bytes) {
    if (bytes == nullptr) return;
    bytes[0] = static_cast<uint8_t>(std::clamp(patch.algorithm, 0, 7));
    bytes[1] = static_cast<uint8_t>(std::clamp(patch.feedback, 0, 7));
    for (int i = 0; i < 4; ++i) {
        detail::writeOperator(patch.operators[i],
                              bytes + 2 + static_cast<size_t>(i) * 10);
    }
}

inline bool saveTFIFile(const std::string& path, const YMPatch& patch) {
    uint8_t bytes[TFI_SIZE] = {};
    writeTFI(patch, bytes);

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(bytes),
               static_cast<std::streamsize>(TFI_SIZE));
    return file.good();
}

}  // namespace ChiptuneTracker
