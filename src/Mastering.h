#pragma once

// ============================================================================
// The mastering stage
//
// Sample tracks arrive with a great deal of care taken over their channels -
// gated snare reverb, sidechained bass, per-channel EQ - and then nothing
// at all on the master bus. That is why they do not sound mastered: every
// part is treated and the mix as a whole is not, so it lands quiet, wide in
// dynamic range, and without the glue that makes separate parts sound like
// one record.
//
// WHAT MASTERING IS DOING HERE, and it is deliberately not much:
//
//   GLUE. One gentle bus compressor across everything, slow enough to let
//   transients through. Its job is to make the parts move together, not to
//   flatten them - so a low ratio and a threshold that only catches the
//   loud moments.
//
//   TONE. Broad, small EQ moves. A master EQ is for tilting a whole mix by
//   a decibel or two; anything that needs more than that is a mix problem
//   and should be fixed on the channel that causes it.
//
//   LEVEL. The part people actually notice. A mix that peaks at 0.3 sounds
//   unfinished next to one that peaks at 0.9 even when everything else
//   about it is better, and this is most of what "doesn't sound mastered"
//   means in practice.
//
//   A CEILING. The limiter catches what is left, and sits below full scale
//   so nothing clips.
//
// It is per genre because the right answer differs: trap wants weight and
// obvious compression, chiptune wants brightness and almost none, and a
// single setting flatters neither.
// ============================================================================

#include <algorithm>
#include <cstring>
#include <string>

#include "Types.h"

namespace ChiptuneTracker {

/*
 * The master chain for a genre.
 *
 * Every field is set, including the ones being turned off, so that applying
 * one genre's mastering after another's leaves nothing behind from the
 * first. A half-applied chain is worse than none, because the leftover is
 * invisible.
 */
struct MasteringProfile {
    const char* genre;

    float lowGain;        // dB
    float midGain;
    float highGain;

    float compThreshold;  // dB
    float compRatio;
    float compAttack;     // seconds
    float compRelease;
    float compMakeup;     // dB

    /*
     * The two audible ones.
     *
     * Everything above is what a mastering engineer would call correct and
     * is deliberately hard to hear. These are what make a mix sound
     * PRODUCED: width pushes everything off-centre outwards, and saturation
     * adds harmonics that read as warmth and as loudness at the same
     * measured level.
     */
    float width;          // 1 = untouched, >1 wider
    float saturation;     // 1 = clean, up to about 3 before it is obvious

    float masterVolume;   // the level everything else is judged against
};

inline const MasteringProfile& masteringProfile(const char* genre) {
    static const MasteringProfile PROFILES[] = {
        // Warm low end, a little air, and gentle glue. Synthwave lives on
        // its pads sitting together rather than on punch.
        {"Synthwave", 1.8f, -0.8f, 1.6f, -17.0f, 2.4f, 0.015f, 0.20f, 4.5f,
                                                          1.45f, 1.7f, 0.86f},

        // Tighter and louder, with the compressor doing audible work -
        // pumping is part of the sound rather than a fault.
        {"Techno",    2.2f, -1.0f, 1.2f, -15.0f, 3.0f, 0.008f, 0.12f, 5.0f,
                                                          1.30f, 1.9f, 0.88f},
        {"House",     2.0f, -0.6f, 1.4f, -15.0f, 2.8f, 0.010f, 0.14f, 4.8f,
                                                          1.35f, 1.7f, 0.87f},

        // Weight, and enough compression to hear. The low shelf is the
        // largest move in the table and still only three decibels.
        {"Hip Hop",   3.0f, -1.2f, 0.8f, -14.0f, 3.2f, 0.012f, 0.16f, 5.2f,
                                                          1.20f, 2.1f, 0.88f},
        {"Trap",      3.2f, -1.4f, 1.0f, -13.5f, 3.4f, 0.010f, 0.14f, 5.5f,
                                                          1.22f, 2.3f, 0.89f},
        {"Reggaeton", 2.6f, -0.8f, 1.2f, -14.5f, 3.0f, 0.011f, 0.15f, 5.0f,
                                                          1.25f, 2.0f, 0.88f},

        /*
         * Chiptune is the odd one out and deliberately so.
         *
         * A 2A03 has almost no low end to lift and its channels are already
         * square waves with no dynamics to speak of, so compressing it hard
         * achieves nothing but noise. Brightness and level, and little else.
         */
        // Barely widened: a 2A03 is very nearly a mono machine, and
        // pushing its sides out mostly pushes out nothing.
        {"Chiptune",  0.4f,  0.2f, 2.0f, -18.0f, 2.0f, 0.020f, 0.22f, 4.0f,
                                                          1.12f, 1.5f, 0.86f},
    };

    static const MasteringProfile DEFAULT_PROFILE =
        {"", 1.5f, -0.5f, 1.2f, -16.0f, 2.4f, 0.012f, 0.18f, 4.5f,
                                                          1.30f, 1.8f, 0.86f};

    if (genre != nullptr) {
        for (const MasteringProfile& profile : PROFILES) {
            if (std::strcmp(profile.genre, genre) == 0) return profile;
        }
    }
    return DEFAULT_PROFILE;
}

/*
 * Put a master chain on a project.
 *
 * Everything is set explicitly rather than only the parts that differ, so
 * applying this twice, or after another genre's, leaves nothing behind.
 */
inline void applyMastering(Project& project, const char* genre) {
    const MasteringProfile& profile = masteringProfile(genre);

    project.masterEQEnabled = true;
    project.masterEQLowGain = profile.lowGain;
    project.masterEQMidGain = profile.midGain;
    project.masterEQHighGain = profile.highGain;

    project.masterCompressorEnabled = true;
    project.masterCompThreshold = profile.compThreshold;
    project.masterCompRatio = profile.compRatio;
    project.masterCompAttack = profile.compAttack;
    project.masterCompRelease = profile.compRelease;
    project.masterCompMakeup = profile.compMakeup;

    /*
     * Always on, and below full scale.
     *
     * The limiter is the only thing standing between a mix that has just
     * been given makeup gain and a clipped one, so it is never left to the
     * caller to remember.
     */
    project.masterLimiterEnabled = true;
    project.masterLimiterCeiling = -0.3f;
    project.masterLimiterRelease = 0.05f;

    project.masterWidthEnabled = true;
    project.masterWidth = profile.width;

    project.masterSaturationEnabled = true;
    project.masterSaturationDrive = profile.saturation;

    project.masterVolume = profile.masterVolume;
}

} // namespace ChiptuneTracker
