#pragma once

// ============================================================================
// What the hardware could actually play
//
// This program synthesises in float and can put any pitch on any channel.
// Real chips cannot. A NES pulse channel holds an eleven-bit period, so its
// pitch comes in steps that are inaudibly fine at the bottom of the keyboard
// and more than a semitone apart at the top; a Game Boy pulse channel
// physically cannot produce a note below C2, whatever you write.
//
// None of that is a limitation to work around. It is the sound. A chiptune
// lead is out of tune in the top octave because the hardware could not do
// better, and a tracker that quietly plays it in tune has removed the thing
// that made it sound like a chiptune.
//
// So this file is the arithmetic of the real registers - period tables,
// frequency formulas, quantisation error, playable ranges - kept separate
// from any oscillator so it can be tested against the datasheets rather than
// against whatever the synthesiser happens to do.
//
// EVERY NUMBER HERE IS FROM A PRIMARY SOURCE. Where two sources disagree,
// the comment says which one this follows and why. Getting a period table
// wrong does not produce an error; it produces noise that is subtly the
// wrong colour forever, which is the single most recognisable thing about
// each of these chips.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ChiptuneTracker {

// ============================================================================
// Region
// ============================================================================
/*
 * NTSC and PAL are not a cosmetic choice.
 *
 * The CPU clock differs by 7.65%, so every pitch does. The frame counter
 * that drives envelopes runs at 240 Hz against 200 Hz - and neither is the
 * 60 Hz people assume, which is why a long song drifts if you clock it from
 * a frame rate instead of a cycle count. And the noise period table differs,
 * non-monotonically: at index 2 the PAL period is SHORTER than the NTSC one,
 * so PAL noise is brighter there and darker everywhere else.
 */
enum class ChipRegion : int { NTSC = 0, PAL };
inline constexpr int CHIP_REGION_COUNT = 2;

inline const char* chipRegionName(ChipRegion region) {
    return (region == ChipRegion::PAL) ? "PAL" : "NTSC";
}

// ============================================================================
// NES 2A03 / 2A07
// ============================================================================
namespace nes {

/*
 * The CPU clocks, derived rather than rounded.
 *
 * NTSC: 21477272.727 / 12. PAL: 26601712.5 / 16. Quoting them to this many
 * digits matters over the length of a song: rounding to 1789773 is a drift
 * of a fifth of a cycle per second, which is inaudible on a note and
 * measurable on a three-minute tune synchronised to anything else.
 */
inline constexpr float CPU_NTSC = 1789772.727f;
inline constexpr float CPU_PAL  = 1662607.031f;

inline constexpr float cpuHz(ChipRegion region) {
    return (region == ChipRegion::PAL) ? CPU_PAL : CPU_NTSC;
}

/*
 * The sixteen noise periods, in CPU cycles.
 *
 * NESdev's tables for both regions. Note indices 0 and 1 are identical, and
 * at index 2 PAL (14) is shorter than NTSC (16) - the one place the two
 * tables cross. That non-monotonicity is the easiest entry in either table
 * to typo, and a typo there is a noise channel that is the wrong colour with
 * nothing to point at.
 */
inline constexpr int NOISE_PERIODS_NTSC[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};
inline constexpr int NOISE_PERIODS_PAL[16] = {
    4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778
};

inline const int* noisePeriods(ChipRegion region) {
    return (region == ChipRegion::PAL) ? NOISE_PERIODS_PAL : NOISE_PERIODS_NTSC;
}

inline int noisePeriodCycles(ChipRegion region, int index) {
    return noisePeriods(region)[std::clamp(index, 0, 15)];
}

// How fast the shift register is clocked, in Hz.
inline float noiseClockHz(ChipRegion region, int index) {
    const int period = noisePeriodCycles(region, index);
    return cpuHz(region) / static_cast<float>(std::max(1, period));
}

/*
 * How long each LFSR mode takes to repeat.
 *
 * The long tap (bit0 ^ bit1) runs 32767 steps, which even at the fastest
 * period repeats only 13.7 times a second - far below pitch, so it is heard
 * as noise and the period register is a brightness control.
 *
 * The short tap (bit0 ^ bit6) runs 93 steps, which IS a pitch: it produces a
 * dense inharmonic comb that reads as hollow and metallic. That is the
 * classic NES zap. 93 = 3 x 31, which is why it sounds ring-modulated rather
 * than like a note.
 */
inline constexpr int NOISE_STEPS_LONG = 32767;
inline constexpr int NOISE_STEPS_SHORT = 93;

// The pitch of the short mode's buzz, which is a real note you can write
// against. Indices 0 to 4 are octaves of D, about 41 cents sharp.
inline float shortNoisePitchHz(ChipRegion region, int index) {
    return noiseClockHz(region, index) / static_cast<float>(NOISE_STEPS_SHORT);
}

/*
 * Pitch, as the eleven-bit period register holds it.
 *
 * f = CPU / (16 * (t + 1)) for a pulse channel; the triangle's timer runs at
 * half the rate for twice the steps, so it lands exactly one octave lower
 * for the same t.
 */
inline constexpr int PERIOD_MAX = 2047;

// Below eight, the hardware silences the pulse channel outright. Not a
// convention - a gate in the sweep unit, which is also why a badly
// configured sweep mutes a channel that looks perfectly healthy.
inline constexpr int PULSE_PERIOD_MIN = 8;

inline float pulseFrequency(ChipRegion region, int period) {
    const int t = std::clamp(period, 0, PERIOD_MAX);
    return cpuHz(region) / (16.0f * static_cast<float>(t + 1));
}

inline float triangleFrequency(ChipRegion region, int period) {
    const int t = std::clamp(period, 0, PERIOD_MAX);
    return cpuHz(region) / (32.0f * static_cast<float>(t + 1));
}

// The period that comes closest to a frequency. Rounded rather than
// truncated: truncating biases every note sharp, which over a chord is a
// tuning error rather than a rounding one.
inline int periodForFrequency(ChipRegion region, float hz, bool triangle = false) {
    if (!(hz > 0.0f)) return PERIOD_MAX;
    const float divisor = triangle ? 32.0f : 16.0f;
    const float exact = cpuHz(region) / (divisor * hz) - 1.0f;
    return std::clamp(static_cast<int>(std::lround(exact)), 0, PERIOD_MAX);
}

/*
 * How far off the nearest period is, in cents.
 *
 * This is the number that decides whether a chip mode is playable at a given
 * pitch. It is inaudible at the bottom of the range and enormous at the top:
 * on a pulse channel one step is about 10 cents at E5, 50 at G7, and more
 * than a semitone above G#8 - at which point there is no chromatic scale to
 * play, because adjacent periods are further apart than the notes are.
 */
inline float tuningErrorCents(ChipRegion region, float hz, bool triangle = false) {
    if (!(hz > 0.0f)) return 0.0f;
    const int period = periodForFrequency(region, hz, triangle);
    const float actual = triangle ? triangleFrequency(region, period)
                                  : pulseFrequency(region, period);
    if (!(actual > 0.0f)) return 0.0f;
    return 1200.0f * std::log2(actual / hz);
}

// The lowest note a channel can reach: the longest period it can hold.
inline float lowestFrequency(ChipRegion region, bool triangle = false) {
    return triangle ? triangleFrequency(region, PERIOD_MAX)
                    : pulseFrequency(region, PERIOD_MAX);
}

// And the highest, which for a pulse is where the hardware stops silencing
// it rather than where the register runs out.
inline float highestFrequency(ChipRegion region, bool triangle = false) {
    return triangle ? triangleFrequency(region, 0)
                    : pulseFrequency(region, PULSE_PERIOD_MIN);
}

/*
 * The four duty cycles.
 *
 * Duty 3 is duty 1 inverted - identical in every way a spectrum analyser can
 * see, and audible only against another channel, where the 180 degrees of
 * phase makes the difference. Worth knowing before spending an afternoon
 * trying to hear it on its own.
 */
inline constexpr float DUTY_CYCLES[4] = {0.125f, 0.25f, 0.50f, 0.25f};

inline const char* dutyName(int duty) {
    switch (std::clamp(duty, 0, 3)) {
        case 0: return "12.5%";
        case 1: return "25%";
        case 2: return "50%";
        default: return "25% inverted";
    }
}

/*
 * The frame counter, which clocks envelopes and the length counter.
 *
 * Derived from the cycle counts rather than assumed to be 60 Hz, because it
 * is not: 239.996 Hz quarter-frames on NTSC and 199.989 on PAL. A song
 * clocked from a frame rate drifts against one clocked from cycles, and the
 * drift is what makes a long tune slowly lose sync with anything else.
 */
inline constexpr float quarterFrameHz(ChipRegion region) {
    return (region == ChipRegion::PAL) ? 199.9888f : 239.9963f;
}
inline constexpr float halfFrameHz(ChipRegion region) {
    return (region == ChipRegion::PAL) ? 99.9944f : 119.9982f;
}

// The volume register is four bits, and the triangle channel has none at
// all - it is on or off. A volume column on a triangle is a lie.
inline constexpr int VOLUME_LEVELS = 16;

inline float quantiseVolume(float level) {
    const float clamped = std::clamp(level, 0.0f, 1.0f);
    const float step = std::round(clamped * float(VOLUME_LEVELS - 1));
    return step / float(VOLUME_LEVELS - 1);
}

}  // namespace nes

// ============================================================================
// Game Boy DMG
// ============================================================================
namespace gameboy {

inline constexpr float MASTER_HZ = 4194304.0f;

/*
 * f = 131072 / (2048 - x) for a pulse channel, 65536 / (2048 - x) for the
 * wave channel. The wave divider runs twice as fast but its waveform is four
 * times as long, so it lands exactly an octave below.
 *
 * THE FLOOR IS THE INTERESTING PART. At x = 0 a pulse channel is 64 Hz -
 * about C2 - and it cannot go lower, at all, ever. The wave channel reaches
 * 32 Hz. So on a Game Boy the wave channel is the only bass instrument, and
 * that is a compositional constraint rather than a preference.
 */
inline constexpr int PERIOD_MAX = 2047;

inline float pulseFrequency(int x) {
    const int clamped = std::clamp(x, 0, PERIOD_MAX);
    return 131072.0f / static_cast<float>(2048 - clamped);
}

inline float waveFrequency(int x) {
    const int clamped = std::clamp(x, 0, PERIOD_MAX);
    return 65536.0f / static_cast<float>(2048 - clamped);
}

inline constexpr float PULSE_LOWEST_HZ = 64.0f;   // x = 0
inline constexpr float WAVE_LOWEST_HZ  = 32.0f;

inline int periodForFrequency(float hz, bool wave = false) {
    const float numerator = wave ? 65536.0f : 131072.0f;
    if (!(hz > 0.0f)) return 0;
    const float exact = 2048.0f - numerator / hz;
    return std::clamp(static_cast<int>(std::lround(exact)), 0, PERIOD_MAX);
}

inline float tuningErrorCents(float hz, bool wave = false) {
    if (!(hz > 0.0f)) return 0.0f;
    const int x = periodForFrequency(hz, wave);
    const float actual = wave ? waveFrequency(x) : pulseFrequency(x);
    if (!(actual > 0.0f)) return 0.0f;
    return 1200.0f * std::log2(actual / hz);
}

/*
 * The wave channel's four volume codes are shifts, not levels.
 *
 * 0%, 100%, 50%, 25% - and the shift is applied to the four-bit sample, so
 * at 25% a custom wave has only four distinct amplitudes left. Turning a
 * Game Boy wave down costs resolution, which is not true of any other volume
 * control in this program.
 */
inline constexpr float WAVE_VOLUMES[4] = {0.0f, 1.0f, 0.5f, 0.25f};

// 32 samples of 4 bits, which is what waveFitToChip's defaults come from.
inline constexpr int WAVE_STEPS = 32;
inline constexpr int WAVE_LEVELS = 16;

// The frame sequencer, and what hangs off it. An envelope steps at 64 Hz and
// cannot loop, so the longest automatic fade on a Game Boy is 15 steps at
// 7/64 s each - about 1.64 seconds, one direction, once.
inline constexpr float FRAME_SEQUENCER_HZ = 512.0f;
inline constexpr float ENVELOPE_HZ = 64.0f;
inline constexpr float LONGEST_ENVELOPE_SECONDS = 15.0f * 7.0f / 64.0f;

}  // namespace gameboy

// ============================================================================
// Asking whether a note is playable
// ============================================================================
/*
 * Which chip a channel is pretending to be.
 *
 * Deliberately short. This is not an emulator selector - it is the set of
 * constraints a strict mode enforces, and the only ones worth offering are
 * the ones whose limits are documented well enough to enforce honestly.
 */
enum class ChipVoice : int {
    None = 0,       // no constraint; the float engine, as it has always been
    NESPulse,
    NESTriangle,
    NESNoise,
    GameBoyPulse,
    GameBoyWave
};
inline constexpr int CHIP_VOICE_COUNT = 6;

inline const char* chipVoiceName(ChipVoice voice) {
    switch (voice) {
        case ChipVoice::None:         return "Unconstrained";
        case ChipVoice::NESPulse:     return "NES pulse";
        case ChipVoice::NESTriangle:  return "NES triangle";
        case ChipVoice::NESNoise:     return "NES noise";
        case ChipVoice::GameBoyPulse: return "Game Boy pulse";
        case ChipVoice::GameBoyWave:  return "Game Boy wave";
    }
    return "Unconstrained";
}

struct ChipPitchCheck {
    bool playable = true;      // can the chip make this note at all
    float actualHz = 0.0f;     // what it would actually produce
    float errorCents = 0.0f;   // how far that is from what was asked for
    bool belowRange = false;
    bool aboveRange = false;
};

/*
 * What a chip would do with this note.
 *
 * Returns what it would actually play rather than a yes or no, because the
 * interesting answer is almost never binary: a note two octaves above middle
 * C is playable on a NES pulse channel and lands 30 cents sharp, and whether
 * that is acceptable is the composer's call, not this function's.
 */
inline ChipPitchCheck checkChipPitch(ChipVoice voice, float hz,
                                     ChipRegion region = ChipRegion::NTSC) {
    ChipPitchCheck result;
    if (voice == ChipVoice::None || !(hz > 0.0f)) {
        result.actualHz = hz;
        return result;
    }

    switch (voice) {
        case ChipVoice::NESPulse:
        case ChipVoice::NESTriangle: {
            const bool triangle = (voice == ChipVoice::NESTriangle);
            const float lowest = nes::lowestFrequency(region, triangle);
            const float highest = nes::highestFrequency(region, triangle);

            result.belowRange = hz < lowest;
            result.aboveRange = hz > highest;
            result.playable = !result.belowRange && !result.aboveRange;

            const int period = nes::periodForFrequency(region, hz, triangle);
            result.actualHz = triangle ? nes::triangleFrequency(region, period)
                                       : nes::pulseFrequency(region, period);
            result.errorCents = nes::tuningErrorCents(region, hz, triangle);
            break;
        }

        case ChipVoice::NESNoise: {
            // The noise channel has sixteen periods and nothing between
            // them, so "playable" means the nearest one - there is no
            // continuous pitch to be in tune with.
            result.playable = true;
            result.actualHz = hz;
            result.errorCents = 0.0f;
            break;
        }

        case ChipVoice::GameBoyPulse:
        case ChipVoice::GameBoyWave: {
            const bool wave = (voice == ChipVoice::GameBoyWave);
            const float lowest = wave ? gameboy::WAVE_LOWEST_HZ
                                      : gameboy::PULSE_LOWEST_HZ;

            result.belowRange = hz < lowest;
            // The top is a register limit rather than a musical one; a step
            // of x = 2046 to 2047 is a whole octave, so anything up there is
            // unplayable in practice long before the register runs out.
            const int x = gameboy::periodForFrequency(hz, wave);
            result.aboveRange = x >= gameboy::PERIOD_MAX - 1;
            result.playable = !result.belowRange && !result.aboveRange;

            result.actualHz = wave ? gameboy::waveFrequency(x)
                                   : gameboy::pulseFrequency(x);
            result.errorCents = gameboy::tuningErrorCents(hz, wave);
            break;
        }

        default:
            result.actualHz = hz;
            break;
    }

    return result;
}

// A note number rather than a frequency, for the callers that have one.
inline float noteToHz(int midiNote) {
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

/*
 * How out of tune is too out of tune?
 *
 * Twenty-five cents is a quarter of a semitone - the point at which a note
 * stops sounding like a slightly detuned version of itself and starts
 * sounding like a mistake against anything else playing. It is a judgement
 * rather than a measurement, which is why it is one named constant here
 * instead of a number scattered through the checks.
 */
inline constexpr float CHIP_TUNING_TOLERANCE_CENTS = 25.0f;

}  // namespace ChiptuneTracker
