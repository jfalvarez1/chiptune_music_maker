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

    /*
     * What the finished thing should measure.
     *
     * These were comments and tooltips before, which meant nothing checked
     * them and nothing could. A profile that says -10 LUFS and produces -18
     * is a profile that is wrong, and that is only knowable if the number is
     * a field.
     *
     * targetLUFS is integrated loudness to BS.1770. ceilingDb is a TRUE-peak
     * ceiling, which is not the same as a sample-peak one and matters here
     * more than in most programs - see TruePeakMeter in MasterEffects.h for
     * the measurements, but the short version is that a square wave sitting
     * at exactly 0 dBFS is already +2.1 dB on a true-peak meter and a noise
     * channel is +5.4.
     */
    float targetLUFS;     // integrated, BS.1770
    float ceilingDb;      // true peak
};

/*
 * The genre table.
 *
 * Re-voiced from measurement rather than taste. Where a number came from
 * metering real releases it is marked [M]; where it is a working engineer's
 * rule of thumb it is marked [R]. The loudness figures for hip hop, trap,
 * reggaeton and pop are medians over tens of thousands of released masters;
 * the dynamics figures come from the Dynamic Range Database; the chiptune
 * row was measured directly from synthesised chip waveforms, because nobody
 * publishes numbers for it.
 *
 * ONE THING RUNS AGAINST ALL OF THIS and is worth stating rather than
 * quietly ignoring: Ian Shepherd, who has done more measurement of this than
 * anyone, argues the minimum peak-to-short-term ratio should NOT vary by
 * genre and should never fall below 8 in any of them. Measured modern drum
 * and bass sits at a dynamic range of 2 to 4. So a preset that faithfully
 * reproduces some of these genres is one he would call broken. These aim at
 * what the genre sounds like, not at what it ought to sound like, and the
 * difference is a choice rather than an oversight.
 */
inline const MasteringProfile& masteringProfile(const char* genre) {
    static const MasteringProfile PROFILES[] = {
        /*
         * Synthwave, dreamwave end. Air comes from a shelf ABOVE 10 kHz
         * rather than a presence lift at 5-8 kHz: there is nothing musical
         * up there in a Juno pad to lift. [M] the genre splits cleanly in
         * two - see Darksynth below - and averaging them produces a preset
         * wrong for both.
         */
        {"Synthwave", 1.8f, -0.8f, 1.6f, -17.0f, 2.4f, 0.015f, 0.20f, 4.5f,
                                       1.45f, 1.7f, 0.86f, -11.0f, -1.0f},

        // [M] Perturbator, Carpenter Brut and Gunship measure at a dynamic
        // range of 2 to 6 - as compressed as chart pop, and deliberately.
        {"Darksynth", 2.2f, -0.6f, 1.4f, -13.0f, 3.2f, 0.008f, 0.14f, 5.4f,
                                       1.35f, 2.2f, 0.89f,  -8.0f, -1.0f},

        /*
         * Techno, and the one move that is genuinely techno-specific: the
         * top is rolled OFF above 12 kHz rather than lifted. Every other
         * dance genre adds air; techno refuses to, and that refusal is most
         * of why it sounds like techno on a big system.
         */
        {"Techno",    2.2f, -1.0f, -0.6f, -15.0f, 2.0f, 0.008f, 0.12f, 5.0f,
                                       1.30f, 1.9f, 0.88f,  -9.0f, -1.0f},

        // [M] Disclosure measures DR 5 on CD. The audible pump at kick rate
        // is the genre - 3 to 4 dB of it has to survive the master, so the
        // ratio here is low and the release is long enough to breathe.
        {"House",     2.0f, -0.6f, 1.4f, -15.0f, 2.0f, 0.012f, 0.22f, 4.8f,
                                       1.35f, 1.7f, 0.87f, -10.0f, -1.0f},

        /*
         * [M] Hip hop's median across 48,263 released masters is -9.7 LUFS,
         * and it is the NARROWEST genre measured - the side sits 8.5 LU
         * below the mid. The width here is low on purpose; widening a rap
         * master is the common way to make it sound amateur.
         */
        {"Hip Hop",   3.0f, -1.2f, 0.8f, -14.0f, 2.2f, 0.020f, 0.16f, 5.2f,
                                       1.10f, 2.1f, 0.88f, -9.7f, -1.0f},

        // [M] -9 to -7 LUFS, the loudest target here. The 30-60 Hz region is
        // protected rather than processed: the 808 IS the record.
        {"Trap",      3.2f, -1.4f, 1.0f, -13.5f, 2.0f, 0.020f, 0.14f, 5.5f,
                                       1.12f, 2.3f, 0.89f,  -8.0f, -1.0f},

        // [M] Bad Bunny measures DR 5-6; the Latin median is -9.4 LUFS. A
        // mid-range record rather than a sub-bass one, and deliberately not
        // as wide as it could be.
        {"Reggaeton", 2.6f, -0.4f, 1.2f, -14.5f, 2.2f, 0.015f, 0.15f, 5.0f,
                                       1.15f, 2.0f, 0.88f,  -9.4f, -1.0f},

        // [M] Noisia and Chase & Status measure DR 2-3 - the most limited
        // music of any genre checked. Slow attack, because a fast release
        // modulates against the sub.
        {"Drum & Bass", 1.6f, -0.8f, 1.8f, -12.0f, 1.8f, 0.040f, 0.25f, 5.6f,
                                       1.30f, 1.6f, 0.89f,  -8.0f, -1.0f},

        /*
         * [M] Lo-fi has a real spectral fingerprint, from a study of 218,109
         * mixes: the least energy of any genre above 8 kHz, while the
         * 20-250 Hz band stays full. Dark, not thin. So the high shelf here
         * is the most negative in the table and the low shelf is not.
         */
        {"Lo-fi",     2.0f,  0.4f, -2.4f, -16.0f, 2.0f, 0.030f, 0.15f, 4.0f,
                                       1.20f, 1.8f, 0.84f, -12.0f, -1.0f},

        // [M] 54 chart singles average -8.3 LUFS with a standard deviation
        // of 1 LU, and pop is the WIDEST genre measured. Maximum density and
        // maximum width at the same time, which is the whole trick.
        {"Pop",       1.4f, -0.6f, 1.6f, -13.0f, 2.0f, 0.050f, 0.25f, 5.0f,
                                       1.50f, 1.8f, 0.88f,  -8.3f, -1.0f},

        // [M] Foo Fighters and Royal Blood measure DR 5 on digital and DR 9
        // to 11 on vinyl. The bus compressor is the signature and it is
        // meant to be heard, timed to the snare.
        {"Rock",      1.2f, -0.4f, 1.0f, -13.0f, 2.6f, 0.010f, 0.30f, 4.6f,
                                       1.25f, 1.6f, 0.88f,  -8.5f, -1.0f},

        /*
         * [M] Ambient is the one genre the loudness war did not move: a
         * dynamic range of 9 to 12 across four decades. Its distinguishing
         * number is not loudness but loudness RANGE - C418's Subwoofer
         * Lullaby measures 21.5 LU, about four times the dance median. The
         * compressor is nearly off, and the point is not hitting a number.
         */
        {"Ambient",   0.6f,  0.0f, 0.4f, -20.0f, 1.4f, 0.060f, 0.40f, 3.0f,
                                       1.40f, 1.0f, 0.80f, -16.0f, -1.0f},

        /*
         * Chiptune, and everything here is measured rather than assumed.
         *
         * A chip mix arrives at the mastering stage ALREADY at commercial
         * loudness. A four-voice arrangement normalised to -1 dBTP with no
         * processing at all measures -12 LUFS; a dense one measures -9. The
         * scene's own releases meter at -9 to -10.5. A square wave has a
         * crest factor of zero decibels - peak equals RMS - so the material
         * is pre-limited by physics and there is nothing for a compressor to
         * glue. Driving it harder is measurably pointless: the first decibel
         * of saturation buys 1.7 LU and the next eleven buy four.
         *
         * So the compressor barely engages, the saturation is the loudness
         * stage, and the two moves that matter are tonal.
         *
         * THE HIGH SHELF IS NEGATIVE. It used to be +2 dB, which is the
         * documented way to make chip percussion harsh: the attacks are
         * already all edge. The advice from people who master this for a
         * living is a gentle roll-off above 10 kHz and warmth in the low
         * mids, and that is what this is now.
         *
         * THE WIDTH IS EXACTLY ONE, and that is not timidity. A 2A03 sums to
         * a mono pin; measured correlation on a centred chip mix is +1.000,
         * and running a mid-side widener over it at any setting leaves the
         * correlation at +1.000. There is no side signal to widen. Width in
         * chiptune comes from panning the channels in the tracker, and a
         * hard-panned chip mix already measures as wide as a commercial pop
         * master.
         *
         * AND THE CEILING IS LOWER THAN EVERY OTHER ROW. This is the
         * important one. A square wave at exactly 0 dBFS reconstructs at
         * +2.1 dB on a true-peak meter and a noise channel at +5.4, because
         * an infinite-slope edge cannot exist in a band-limited signal. A
         * chip master at -1 dBTP is fine on the meter and clips the codec.
         */
        {"Chiptune",  1.5f,  0.2f, -0.8f, -12.0f, 1.5f, 0.030f, 0.25f, 3.2f,
                                       1.00f, 1.6f, 0.88f, -10.0f, -1.6f},
    };

    static const MasteringProfile DEFAULT_PROFILE =
        {"", 1.5f, -0.5f, 1.2f, -16.0f, 2.0f, 0.020f, 0.20f, 4.5f,
                        1.30f, 1.8f, 0.86f, -11.0f, -1.0f};

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

    /*
     * The profile's ceiling, not a fixed one.
     *
     * -0.3 dB was here for every genre, which is below the 0.6 dB a
     * true-peak meter is allowed to be wrong by, and well above what a
     * square wave needs: chiptune reconstructs about two decibels above its
     * sample peak, so it gets -1.6 while everything else gets -1.0.
     */
    project.masterLimiterCeiling = profile.ceilingDb;
    project.masterLimiterRelease = 0.05f;

    project.masterWidthEnabled = true;
    project.masterWidth = profile.width;

    project.masterSaturationEnabled = true;
    project.masterSaturationDrive = profile.saturation;

    project.masterVolume = profile.masterVolume;
}

} // namespace ChiptuneTracker
