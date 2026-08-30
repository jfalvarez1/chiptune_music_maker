#pragma once

/*
 * ChiptuneTracker - Oscillator name mapping
 *
 * The string form of every OscillatorType, used by the project file format
 * and anything else that has to name an instrument in text.
 *
 * These two functions must stay exact inverses of each other. When they
 * drifted apart, instruments silently loaded back as Pulse - Vocoder,
 * KavinskyBass, Supersaw and the whole reggaeton kit were all lost that
 * way. tests/ChiptuneTests.cpp walks the enum and asserts the round trip
 * for every value, so a new oscillator added to only one of them fails the
 * build's test run rather than corrupting someone's song.
 */

#include "Types.h"

#include <string>

namespace ChiptuneTracker {

inline std::string oscillatorTypeToString(OscillatorType type) {
    switch (type) {
        case OscillatorType::Pulse: return "Pulse";
        case OscillatorType::Triangle: return "Triangle";
        case OscillatorType::Sawtooth: return "Sawtooth";
        case OscillatorType::Sine: return "Sine";
        case OscillatorType::Noise: return "Noise";
        case OscillatorType::Supersaw: return "Supersaw";
        case OscillatorType::Custom: return "Custom";
        // Synths
        case OscillatorType::SynthLead: return "SynthLead";
        case OscillatorType::SynthPad: return "SynthPad";
        case OscillatorType::SynthBass: return "SynthBass";
        case OscillatorType::SynthPluck: return "SynthPluck";
        case OscillatorType::SynthArp: return "SynthArp";
        case OscillatorType::SynthOrgan: return "SynthOrgan";
        case OscillatorType::SynthStrings: return "SynthStrings";
        case OscillatorType::SynthBrass: return "SynthBrass";
        case OscillatorType::SynthChip: return "SynthChip";
        case OscillatorType::SynthBell: return "SynthBell";
        
        // Additional Basses
        case OscillatorType::SynthwaveBass: return "SynthwaveBass";
        case OscillatorType::AcidBass: return "AcidBass";
        case OscillatorType::SubBass808: return "SubBass808";
        
        // Synthwave
        case OscillatorType::SynthwaveLead: return "SynthwaveLead";
        case OscillatorType::SynthwavePad: return "SynthwavePad";
        case OscillatorType::SynthwaveArp: return "SynthwaveArp";
        case OscillatorType::SynthwaveChord: return "SynthwaveChord";
        case OscillatorType::SynthwaveFM: return "SynthwaveFM";
        // Techno
        case OscillatorType::TechnoStab: return "TechnoStab";
        case OscillatorType::Hoover: return "Hoover";
        case OscillatorType::RaveChord: return "RaveChord";
        case OscillatorType::Reese: return "Reese";
        // Hip Hop
        case OscillatorType::LoFiKeys: return "LoFiKeys";
        case OscillatorType::VinylNoise: return "VinylNoise";
        case OscillatorType::TrapLead: return "TrapLead";
        // Extra
        case OscillatorType::GatedPad: return "GatedPad";
        case OscillatorType::PolySynth: return "PolySynth";
        case OscillatorType::SyncLead: return "SyncLead";

        // Drums
        case OscillatorType::Kick: return "Kick";
        case OscillatorType::Kick808: return "Kick808";
        case OscillatorType::KickHard: return "KickHard";
        case OscillatorType::KickSoft: return "KickSoft";
        case OscillatorType::Snare: return "Snare";
        case OscillatorType::Snare808: return "Snare808";
        case OscillatorType::SnareRim: return "SnareRim";
        case OscillatorType::Clap: return "Clap";
        case OscillatorType::HiHat: return "HiHat";
        case OscillatorType::HiHatOpen: return "HiHatOpen";
        case OscillatorType::HiHatPedal: return "HiHatPedal";
        case OscillatorType::Tom: return "Tom";
        case OscillatorType::TomLow: return "TomLow";
        case OscillatorType::TomHigh: return "TomHigh";
        case OscillatorType::Crash: return "Crash";
        case OscillatorType::Ride: return "Ride";
        case OscillatorType::Cowbell: return "Cowbell";
        case OscillatorType::Clave: return "Clave";
        case OscillatorType::Conga: return "Conga";
        case OscillatorType::Maracas: return "Maracas";
        case OscillatorType::Tambourine: return "Tambourine";
        // Reggaeton
        case OscillatorType::ReggaetonBass: return "ReggaetonBass";
        case OscillatorType::LatinBrass: return "LatinBrass";
        case OscillatorType::Guira: return "Guira";
        case OscillatorType::Bongo: return "Bongo";
        case OscillatorType::Timbale: return "Timbale";
        case OscillatorType::Dembow808: return "Dembow808";
        case OscillatorType::DembowSnare: return "DembowSnare";
        // High-Accuracy Recreations
        case OscillatorType::Vocoder: return "Vocoder";
        case OscillatorType::KavinskyBass: return "KavinskyBass";
        default: return "Pulse";
    }
}

inline OscillatorType stringToOscillatorType(const std::string& str) {
    if (str == "Pulse") return OscillatorType::Pulse;
    if (str == "Triangle") return OscillatorType::Triangle;
    if (str == "Sawtooth") return OscillatorType::Sawtooth;
    if (str == "Sine") return OscillatorType::Sine;
    if (str == "Noise") return OscillatorType::Noise;
    if (str == "Supersaw") return OscillatorType::Supersaw;
    if (str == "Custom") return OscillatorType::Custom;
    // Synths
    if (str == "SynthLead") return OscillatorType::SynthLead;
    if (str == "SynthPad") return OscillatorType::SynthPad;
    if (str == "SynthBass") return OscillatorType::SynthBass;
    if (str == "SynthPluck") return OscillatorType::SynthPluck;
    if (str == "SynthArp") return OscillatorType::SynthArp;
    if (str == "SynthOrgan") return OscillatorType::SynthOrgan;
    if (str == "SynthStrings") return OscillatorType::SynthStrings;
    if (str == "SynthBrass") return OscillatorType::SynthBrass;
    if (str == "SynthChip") return OscillatorType::SynthChip;
    if (str == "SynthBell") return OscillatorType::SynthBell;
    
    // Additional Basses
    if (str == "SynthwaveBass") return OscillatorType::SynthwaveBass;
    if (str == "AcidBass") return OscillatorType::AcidBass;
    if (str == "SubBass808") return OscillatorType::SubBass808;
    
    // Synthwave
    if (str == "SynthwaveLead") return OscillatorType::SynthwaveLead;
    if (str == "SynthwavePad") return OscillatorType::SynthwavePad;
    if (str == "SynthwaveArp") return OscillatorType::SynthwaveArp;
    if (str == "SynthwaveChord") return OscillatorType::SynthwaveChord;
    if (str == "SynthwaveFM") return OscillatorType::SynthwaveFM;
    // Techno
    if (str == "TechnoStab") return OscillatorType::TechnoStab;
    if (str == "Hoover") return OscillatorType::Hoover;
    if (str == "RaveChord") return OscillatorType::RaveChord;
    if (str == "Reese") return OscillatorType::Reese;
    // Hip Hop
    if (str == "LoFiKeys") return OscillatorType::LoFiKeys;
    if (str == "VinylNoise") return OscillatorType::VinylNoise;
    if (str == "TrapLead") return OscillatorType::TrapLead;
    // Extra
    if (str == "GatedPad") return OscillatorType::GatedPad;
    if (str == "PolySynth") return OscillatorType::PolySynth;
    if (str == "SyncLead") return OscillatorType::SyncLead;

    // Drums
    if (str == "Kick") return OscillatorType::Kick;
    if (str == "Kick808") return OscillatorType::Kick808;
    if (str == "KickHard") return OscillatorType::KickHard;
    if (str == "KickSoft") return OscillatorType::KickSoft;
    if (str == "Snare") return OscillatorType::Snare;
    if (str == "Snare808") return OscillatorType::Snare808;
    if (str == "SnareRim") return OscillatorType::SnareRim;
    if (str == "Clap") return OscillatorType::Clap;
    if (str == "HiHat") return OscillatorType::HiHat;
    if (str == "HiHatOpen") return OscillatorType::HiHatOpen;
    if (str == "HiHatPedal") return OscillatorType::HiHatPedal;
    if (str == "Tom") return OscillatorType::Tom;
    if (str == "TomLow") return OscillatorType::TomLow;
    if (str == "TomHigh") return OscillatorType::TomHigh;
    if (str == "Crash") return OscillatorType::Crash;
    if (str == "Ride") return OscillatorType::Ride;
    if (str == "Cowbell") return OscillatorType::Cowbell;
    if (str == "Clave") return OscillatorType::Clave;
    if (str == "Conga") return OscillatorType::Conga;
    if (str == "Maracas") return OscillatorType::Maracas;
    if (str == "Tambourine") return OscillatorType::Tambourine;
    // Reggaeton
    if (str == "ReggaetonBass") return OscillatorType::ReggaetonBass;
    if (str == "LatinBrass") return OscillatorType::LatinBrass;
    if (str == "Guira") return OscillatorType::Guira;
    if (str == "Bongo") return OscillatorType::Bongo;
    if (str == "Timbale") return OscillatorType::Timbale;
    if (str == "Dembow808") return OscillatorType::Dembow808;
    if (str == "DembowSnare") return OscillatorType::DembowSnare;
    // High-Accuracy Recreations
    if (str == "Vocoder") return OscillatorType::Vocoder;
    if (str == "KavinskyBass") return OscillatorType::KavinskyBass;
    return OscillatorType::Pulse;
}

} // namespace ChiptuneTracker
