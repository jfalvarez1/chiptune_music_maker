#pragma once

// ============================================================================
// Which drums a beatboxed take is allowed to become
//
// Two things this gets you, and the second is the one that is not obvious.
//
// THE OBVIOUS ONE. You can beatbox a part that is only kick and snare, or
// only hats, and get exactly that - rather than a four-piece kit with a
// stray hi-hat in it because one "puh" came out breathy. Isolating an
// instrument is how anybody actually builds a groove: lay the kick down,
// then the snare against it, then the hats over both.
//
// THE ONE THAT IS NOT. Narrowing the kit makes the classification
// dramatically more reliable. Vocal percussion research measures four-way
// classification of freely chosen sounds at around 79%, and a two-way
// choice between well-separated sounds far higher - most of the error in
// the four-way case is snare-versus-hi-hat, and a two-piece kit containing
// only one of them cannot make that mistake at all.
//
// It also changes what you should be SAYING. The same research gives
// recommended mouth sounds per kit configuration, and they are not simply
// the four-piece ones with entries deleted: for a snare-and-hat kit the
// snare is best made with "kuh" rather than the "puh" it would use in a
// full kit, because "puh" is spoken for by the kick there and freeing it up
// changes the best answer. So the guidance below is per kit rather than per
// drum.
//
// /t/ is deliberately absent from every recommendation. It is the sound
// most people reach for and the worst performer measured - it scatters
// across the "tss" and "tshh" regions and cannot be reliably told apart
// from either.
// ============================================================================

#include <string>

#include "Types.h"
#include "VoiceTimbre.h"

namespace ChiptuneTracker {

/*
 * A kit: which drums are in play, and what each one sounds like.
 *
 * The two halves are independent on purpose. Which drums exist decides what
 * the classifier is allowed to answer; which instrument each becomes is
 * pure taste and changes nothing about detection - swapping a kick for an
 * 808 does not make it harder to hear that you said "puh".
 */
struct DrumKit {
    bool useKick = true;
    bool useSnare = true;
    bool useHatClosed = true;
    bool useHatOpen = false;

    OscillatorType kickInstrument = OscillatorType::Kick;
    OscillatorType snareInstrument = OscillatorType::Snare;
    OscillatorType hatClosedInstrument = OscillatorType::HiHat;
    OscillatorType hatOpenInstrument = OscillatorType::HiHatOpen;

    // Where each lands on the keyboard. A drum oscillator ignores pitch, but
    // the note needs one, and stacking them on a single key makes the piano
    // roll unreadable.
    int kickPitch = 36;
    int snarePitch = 38;
    int hatClosedPitch = 42;
    int hatOpenPitch = 46;

    bool enabled(DrumClass drum) const {
        switch (drum) {
            case DrumClass::Kick:      return useKick;
            case DrumClass::Snare:     return useSnare;
            case DrumClass::HatClosed: return useHatClosed;
            case DrumClass::HatOpen:   return useHatOpen;
            default:                   return false;
        }
    }

    int count() const {
        return (useKick ? 1 : 0) + (useSnare ? 1 : 0) +
               (useHatClosed ? 1 : 0) + (useHatOpen ? 1 : 0);
    }

    OscillatorType instrumentFor(DrumClass drum) const {
        switch (drum) {
            case DrumClass::Kick:      return kickInstrument;
            case DrumClass::Snare:     return snareInstrument;
            case DrumClass::HatOpen:   return hatOpenInstrument;
            default:                   return hatClosedInstrument;
        }
    }

    int pitchFor(DrumClass drum) const {
        switch (drum) {
            case DrumClass::Kick:      return kickPitch;
            case DrumClass::Snare:     return snarePitch;
            case DrumClass::HatOpen:   return hatOpenPitch;
            default:                   return hatClosedPitch;
        }
    }

    /*
     * The first drum that is switched on.
     *
     * A kit with nothing in it cannot be used, and silently producing
     * nothing would look like the microphone had failed - so a kit is never
     * allowed to be empty, and this is what an unclassifiable hit falls
     * back to.
     */
    DrumClass firstEnabled() const {
        if (useKick) return DrumClass::Kick;
        if (useSnare) return DrumClass::Snare;
        if (useHatClosed) return DrumClass::HatClosed;
        if (useHatOpen) return DrumClass::HatOpen;
        return DrumClass::Kick;
    }
};

/*
 * What to say for each drum, given the kit.
 *
 * Per kit rather than per drum, because the best sound for one instrument
 * depends on what else is present: with no kick in the kit, "puh" is free
 * and the snare should use it; with a kick there, the snare moves to "kuh"
 * so the two stay far apart.
 */
inline const char* kitPhonemeFor(const DrumKit& kit, DrumClass drum) {
    switch (drum) {
        case DrumClass::Kick:
            return "\"puh\"";

        case DrumClass::Snare:
            // "puh" is the best snare sound in isolation and the best kick
            // sound too, so it goes to the kick when both are present.
            return kit.useKick ? "\"kuh\"" : "\"puh\"";

        case DrumClass::HatClosed:
            // With both hats in play they need the two most separable
            // fricatives; with only one, the crisper of them.
            return "\"tss\"";

        case DrumClass::HatOpen:
            return kit.useHatClosed ? "\"tshh\"" : "\"tss\"";

        default:
            return "";
    }
}

// ---- Ready-made kits ---------------------------------------------------------

inline DrumKit kitFullFour() {
    DrumKit kit;
    kit.useKick = kit.useSnare = kit.useHatClosed = kit.useHatOpen = true;
    return kit;
}

inline DrumKit kitKickSnare() {
    DrumKit kit;
    kit.useKick = kit.useSnare = true;
    kit.useHatClosed = kit.useHatOpen = false;
    return kit;
}

inline DrumKit kitKickHat() {
    DrumKit kit;
    kit.useKick = kit.useHatClosed = true;
    kit.useSnare = kit.useHatOpen = false;
    return kit;
}

inline DrumKit kitSnareHat() {
    DrumKit kit;
    kit.useSnare = kit.useHatClosed = true;
    kit.useKick = kit.useHatOpen = false;
    return kit;
}

inline DrumKit kitKickOnly() {
    DrumKit kit;
    kit.useKick = true;
    kit.useSnare = kit.useHatClosed = kit.useHatOpen = false;
    return kit;
}

inline DrumKit kitHatsOnly() {
    DrumKit kit;
    kit.useHatClosed = kit.useHatOpen = true;
    kit.useKick = kit.useSnare = false;
    return kit;
}

// The same three-piece shape, on 808 voices. A taste choice rather than a
// detection one - what you say is identical.
inline DrumKit kit808() {
    DrumKit kit;
    kit.useKick = kit.useSnare = kit.useHatClosed = true;
    kit.useHatOpen = false;
    kit.kickInstrument = OscillatorType::Kick808;
    kit.snareInstrument = OscillatorType::Snare808;
    kit.hatClosedInstrument = OscillatorType::HiHat;
    return kit;
}

struct DrumKitPreset {
    const char* name;
    DrumKit (*make)();
    const char* description;
};

inline const DrumKitPreset* drumKitPresets(int& countOut) {
    static const DrumKitPreset PRESETS[] = {
        {"Kick + Snare", &kitKickSnare,
         "The two that separate best. Most reliable of all."},
        {"Kick + Hats", &kitKickHat,
         "A groove with no backbeat."},
        {"Snare + Hats", &kitSnareHat,
         "Top end only, to lay over a kick you already have."},
        {"Kick only", &kitKickOnly,
         "Everything you make becomes a kick."},
        {"Hats only", &kitHatsOnly,
         "Closed and open, for a hat pattern on its own."},
        {"808s", &kit808,
         "Kick, snare and hat on 808 voices."},
        {"Full kit", &kitFullFour,
         "All four. The least reliable - most mistakes are snare against hat."},
    };
    countOut = static_cast<int>(sizeof(PRESETS) / sizeof(PRESETS[0]));
    return PRESETS;
}

/*
 * Classify a hit, but only as something the kit contains.
 *
 * This is where the accuracy comes from. Asking a four-way classifier for
 * an answer and then discarding it when the kit does not contain that drum
 * would throw away real hits; asking it to choose among only the drums that
 * are present cannot produce an answer that has to be thrown away, and
 * removes the confusion between the classes that are not there.
 */
inline DrumClass classifyWithinKit(const DrumClassifier& classifier,
                                   const TimbreFeatures& features,
                                   const DrumKit& kit, float& confidenceOut) {
    confidenceOut = 0.0f;
    if (kit.count() == 0) return DrumClass::Kick;

    // With one drum in the kit there is nothing to decide.
    if (kit.count() == 1) {
        confidenceOut = 1.0f;
        return kit.firstEnabled();
    }

    if (classifier.trained()) {
        const DrumClassifier::Result guess =
            classifier.classifyAmong(features, kit.useKick, kit.useSnare,
                                     kit.useHatClosed, kit.useHatOpen);
        if (guess.valid) {
            confidenceOut = guess.confidence;
            return guess.label;
        }
    }

    return kit.firstEnabled();
}

/*
 * Fold a four-way heuristic answer into the kit.
 *
 * Used when nothing has been taught, so the built-in rules still produce an
 * answer the kit can hold. The substitutions follow what the sounds are:
 * a hat with no hats in the kit is the brightest thing available, and a
 * kick with no kick is the darkest.
 */
inline DrumClass foldIntoKit(DrumClass guess, const DrumKit& kit) {
    if (kit.enabled(guess)) return guess;
    if (kit.count() == 0) return DrumClass::Kick;

    switch (guess) {
        case DrumClass::Kick:
            // Darkest first.
            if (kit.useSnare) return DrumClass::Snare;
            if (kit.useHatClosed) return DrumClass::HatClosed;
            return DrumClass::HatOpen;

        case DrumClass::Snare:
            // A snare sits between the two, so either neighbour will do -
            // the kick is the closer of them in body.
            if (kit.useKick) return DrumClass::Kick;
            if (kit.useHatClosed) return DrumClass::HatClosed;
            return DrumClass::HatOpen;

        case DrumClass::HatOpen:
            if (kit.useHatClosed) return DrumClass::HatClosed;
            if (kit.useSnare) return DrumClass::Snare;
            return DrumClass::Kick;

        default:   // HatClosed
            if (kit.useHatOpen) return DrumClass::HatOpen;
            if (kit.useSnare) return DrumClass::Snare;
            return DrumClass::Kick;
    }
}

} // namespace ChiptuneTracker
