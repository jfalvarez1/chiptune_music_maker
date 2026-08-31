#pragma once

/*
 * The pre-rack effects chain, frozen.
 *
 * This is `struct EffectsChain` exactly as it stood at commit 9aa6320, before
 * Task A turned it into a reorderable insert rack: seventeen members,
 * seventeen bools, and the processing order hardcoded into process().
 *
 * It exists only so the new rack can be proven sample-identical to it. It is
 * a TEST FIXTURE and must never be included by the app. Do not "fix" or
 * modernise it - the moment it stops being a verbatim copy of the old
 * behaviour, it stops being evidence of anything.
 *
 * The members are shared by value with the live effect classes in Effects.h,
 * which is deliberate: the identity test configures both chains from the same
 * parameters, so any change to an effect's own DSP moves both sides together
 * and the test keeps testing the ordering, which is what Task A changed.
 */

#include "Effects.h"

namespace ChiptuneTracker {
namespace legacy {

struct LegacyEffectsChain {
    // Effect instances
    Bitcrusher bitcrusher;
    Distortion distortion;
    Filter filter;
    ThreeBandEQ eq;
    Compressor compressor;
    FormantFilter formant;
    Delay delay;
    Chorus chorus;
    Tremolo tremolo;
    Phaser phaser;
    Flanger flanger;
    RingModulator ringMod;
    Sidechain sidechain;
    Reverb reverb;
    StereoWidener stereoWidener;
    TapeSaturation tapeSaturation;
    Unison unison;

    // Enable flags
    bool bitcrusherEnabled = false;
    bool distortionEnabled = false;
    bool filterEnabled = false;
    bool eqEnabled = false;
    bool compressorEnabled = false;
    bool formantEnabled = false;
    bool delayEnabled = false;
    bool chorusEnabled = false;
    bool tremoloEnabled = false;
    bool phaserEnabled = false;
    bool flangerEnabled = false;
    bool ringModEnabled = false;
    bool sidechainEnabled = false;
    bool reverbEnabled = false;
    bool stereoWidenerEnabled = false;
    bool tapeSaturationEnabled = false;
    int sidechainSource = -1;

    void setSampleRate(float sr) {
        filter.setSampleRate(sr);
        eq.setSampleRate(sr);
        compressor.setSampleRate(sr);
        formant.setSampleRate(sr);
        delay.setSampleRate(sr);
        chorus.setSampleRate(sr);
        flanger.init((int)sr);
        sidechain.setSampleRate(sr);
        reverb.setSampleRate(sr);
        stereoWidener.setSampleRate(sr);
        tapeSaturation.setSampleRate(sr);
    }

    // The order, verbatim. This is the thing under test.
    float process(float input, float time) {
        float output = input;

        if (eqEnabled)         output = eq.process(output);
        if (tapeSaturationEnabled) output = tapeSaturation.process(output);
        if (formantEnabled)    output = formant.process(output);
        if (compressorEnabled) output = compressor.process(output);
        if (bitcrusherEnabled) output = bitcrusher.process(output);
        if (distortionEnabled) output = distortion.process(output);
        if (filterEnabled)     output = filter.process(output);
        if (ringModEnabled)    output = ringMod.process(output, time);
        if (tremoloEnabled)    output *= tremolo.process(time);
        if (phaserEnabled)     output = phaser.process(output, time);
        if (flangerEnabled)    output = flanger.process(output);
        if (chorusEnabled)     output = chorus.process(output, time);
        if (delayEnabled)      output = delay.process(output);
        if (reverbEnabled)     output = reverb.process(output);

        return output;
    }

    void reset() {
        bitcrusher.reset();
        filter.reset();
        eq.reset();
        compressor.reset();
        formant.reset();
        delay.reset();
        stereoWidener.reset();
        tapeSaturation.reset();
        chorus.reset();
        phaser.reset();
        flanger.reset();
        sidechain.reset();
        reverb.reset();
    }
};

} // namespace legacy
} // namespace ChiptuneTracker
