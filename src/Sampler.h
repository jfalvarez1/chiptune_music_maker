#pragma once

/*
 * ChiptuneTracker - Multisample instrument
 *
 * SampleOscillator plays one sample across the whole keyboard, pitch-shifted
 * by however far the note is from its root. That works for a drum hit and
 * falls apart for anything else: shift a piano sample two octaves and it
 * sounds like a piano played at the wrong tape speed, because the formants
 * move with the pitch. The answer, and the reason every sampler since the
 * Mirage has worked this way, is several recordings across the keyboard with
 * each one used only near where it was recorded.
 *
 * So: key zones, velocity layers, and round-robin.
 *
 * KEY ZONES bound how far any one recording is stretched.
 *
 * VELOCITY LAYERS are the difference between a sampler and a tape player. A
 * piano played hard is not a piano played softly and turned up - it is a
 * different spectrum. One layer per dynamic, chosen by how hard the note was
 * played, is what makes a sampled instrument respond.
 *
 * ROUND-ROBIN cycles between recordings of the same note so that a fast
 * repeated hit does not phase against itself. Two identical samples played
 * a few milliseconds apart cancel and reinforce at comb-filter intervals -
 * the "machine gun" effect, and the reason it sounds artificial rather than
 * merely repetitive.
 *
 * One bug is fixed here on the way past. SampleOscillator advanced its read
 * position by the pitch ratio alone, with no term for the difference between
 * the sample's rate and the engine's. The pool decodes everything to 48 kHz
 * and the engine usually runs at 44.1, so every sample played 8.8% sharp -
 * nearly a semitone and a half - and every sample instrument in the program
 * has been slightly out of tune with the synths.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Sample.h"

namespace ChiptuneTracker {

// ============================================================================
// One zone
// ============================================================================
struct SampleZone {
    int sampleId = -1;

    // The keys this recording covers, and the key it was recorded at.
    int lowKey = 0;
    int highKey = 127;
    int rootKey = 60;

    // The velocities it covers, 0..1. Layers may overlap; see the crossfade.
    float lowVelocity = 0.0f;
    float highVelocity = 1.0f;

    float gain = 1.0f;
    float pan = 0.0f;
    float tuneCents = 0.0f;

    bool loop = false;
    float loopStartSeconds = 0.0f;
    float loopEndSeconds = 0.0f;   // 0 = to the end of the sample

    // Zones sharing a key and velocity range with different round-robin
    // indices are alternate takes of the same note.
    int roundRobinGroup = 0;

    bool matches(int note, float velocity) const {
        return note >= lowKey && note <= highKey &&
               velocity >= lowVelocity && velocity <= highVelocity;
    }
};

// ============================================================================
// The instrument
// ============================================================================
struct SamplerInstrument {
    /*
     * Twenty-four zones, not sixty-four.
     *
     * This array is inline in ChannelConfig and there are thirty-two
     * channels, so every zone is paid for 32 times whether or not any
     * channel is a sampler. At 64 it pushed Project from 23 KB to 170 KB and
     * started overflowing the stack of tests that hold two or three projects
     * as locals - which is a warning about the whole shape, not just the
     * tests.
     *
     * Twenty-four is eight key ranges across three velocity layers, or
     * twelve across two, which covers the instruments anyone is realistically
     * going to build here.
     */
    static constexpr int MAX_ZONES = 24;

    std::array<SampleZone, MAX_ZONES> zones{};
    int zoneCount = 0;

    /*
     * How far either side of a velocity boundary the two layers blend, in
     * velocity units. Zero is a hard switch, and a hard switch is audible:
     * play a crescendo and the instrument changes character on one note.
     * A narrow crossfade is what makes layers sound like one instrument
     * rather than several.
     */
    float velocityCrossfade = 0.08f;

    // Amplitude envelope. A sample with a hard start and stop clicks; this
    // is the whole reason a sampler has an envelope at all.
    float attack = 0.001f;
    float decay = 0.0f;
    float sustain = 1.0f;
    float release = 0.08f;

    // How much velocity scales the level, 0..1. At 0 every note is full
    // volume, which is what you want for a drum kit whose layers already
    // encode the dynamics.
    float velocityToLevel = 1.0f;

    bool addZone(const SampleZone& zone) {
        if (zoneCount >= MAX_ZONES) return false;
        zones[static_cast<size_t>(zoneCount++)] = zone;
        return true;
    }

    /*
     * The zone for a note, and how loudly it should sound.
     *
     * Returns -1 when nothing covers the note. That is deliberately not the
     * same as "use the first zone": a keyboard with a gap in it should be
     * silent there, so the gap is findable, rather than playing a wildly
     * stretched neighbour and sounding merely broken.
     *
     * `weight` comes back between 0 and 1: below 1 when the note falls in a
     * velocity crossfade, so the caller can sum two zones.
     */
    int findZone(int note, float velocity, int roundRobin, float& weight) const {
        weight = 0.0f;
        int best = -1;
        int bestGroupSize = 0;

        // Round-robin: count how many alternates cover this note, then take
        // the one the counter points at. Counting rather than storing a
        // per-zone index keeps the zones plain data that a file can hold.
        int alternates = 0;
        for (int i = 0; i < zoneCount; ++i) {
            if (zones[static_cast<size_t>(i)].matches(note, velocity)) ++alternates;
        }
        if (alternates == 0) return -1;

        const int wanted = (alternates > 0)
            ? ((roundRobin % alternates) + alternates) % alternates : 0;

        int seen = 0;
        for (int i = 0; i < zoneCount; ++i) {
            const SampleZone& zone = zones[static_cast<size_t>(i)];
            if (!zone.matches(note, velocity)) continue;
            if (seen == wanted) { best = i; bestGroupSize = alternates; break; }
            ++seen;
        }
        (void)bestGroupSize;

        if (best < 0) return -1;

        // Crossfade weight against the layer boundaries.
        const SampleZone& zone = zones[static_cast<size_t>(best)];
        weight = 1.0f;
        if (velocityCrossfade > 1e-4f) {
            const float fromLow = velocity - zone.lowVelocity;
            const float fromHigh = zone.highVelocity - velocity;
            if (zone.lowVelocity > 0.0f && fromLow < velocityCrossfade) {
                weight = std::min(weight, 0.5f + 0.5f * fromLow / velocityCrossfade);
            }
            if (zone.highVelocity < 1.0f && fromHigh < velocityCrossfade) {
                weight = std::min(weight, 0.5f + 0.5f * fromHigh / velocityCrossfade);
            }
        }
        return best;
    }
};

// ============================================================================
// Per-voice playback
// ============================================================================
class SamplerVoice {
public:
    /*
     * Start a note.
     *
     * `engineRate` is the sample rate the mixer runs at, and it is not
     * optional. The read step is (sample rate / engine rate) * pitch ratio;
     * SampleOscillator omitted the first factor entirely, so a 48 kHz
     * recording played through a 44.1 kHz engine came out 8.8% sharp - and
     * every sample instrument in the program was a semitone and a half above
     * the synths.
     */
    void trigger(const SamplerInstrument& instrument, const SampleZone& zone,
                 const Sample* sample, int note, float velocity,
                 float weight, float engineRate) {
        m_sample = sample;
        m_zone = zone;
        m_playing = false;
        m_released = false;
        m_position = 0.0;
        m_envTime = 0.0f;
        m_envLevel = 0.0f;

        if (sample == nullptr || !sample->isLoaded || sample->audioData.empty()) {
            return;
        }
        if (engineRate <= 0.0f) return;

        const float semitones = static_cast<float>(note - zone.rootKey) +
                                zone.tuneCents / 100.0f;
        const float pitchRatio = std::pow(2.0f, semitones / 12.0f);
        const float rateRatio = static_cast<float>(sample->sampleRate) / engineRate;

        m_step = static_cast<double>(pitchRatio) * static_cast<double>(rateRatio);
        m_engineRate = engineRate;

        const float velocityLevel =
            1.0f - instrument.velocityToLevel * (1.0f - std::clamp(velocity, 0.0f, 1.0f));
        m_level = zone.gain * weight * velocityLevel;

        m_attack = instrument.attack;
        m_decay = instrument.decay;
        m_sustain = instrument.sustain;
        m_release = instrument.release;

        m_playing = true;
    }

    void release() {
        if (!m_released) {
            m_released = true;
            m_envTime = 0.0f;
        }
    }

    void stop() { m_playing = false; }
    bool isPlaying() const { return m_playing; }
    float pan() const { return m_zone.pan; }

    float process() {
        if (!m_playing || m_sample == nullptr) return 0.0f;

        const auto size = static_cast<double>(m_sample->audioData.size());
        if (size < 2.0) { m_playing = false; return 0.0f; }

        // Loop bounds, in samples.
        const double loopStart = static_cast<double>(m_zone.loopStartSeconds) *
                                 static_cast<double>(m_sample->sampleRate);
        const double loopEnd = (m_zone.loopEndSeconds > m_zone.loopStartSeconds)
            ? static_cast<double>(m_zone.loopEndSeconds) *
              static_cast<double>(m_sample->sampleRate)
            : size - 1.0;

        if (m_zone.loop && m_position >= loopEnd) {
            const double span = loopEnd - loopStart;
            if (span > 1.0) {
                // fmod, not a single subtraction: a high pitch ratio can step
                // several loop lengths past the end in one sample.
                m_position = loopStart + std::fmod(m_position - loopStart, span);
            } else {
                m_position = loopStart;
            }
        }

        if (m_position >= size - 1.0) { m_playing = false; return 0.0f; }
        if (m_position < 0.0) m_position = 0.0;

        const auto index = static_cast<size_t>(m_position);
        const float fraction = static_cast<float>(m_position - static_cast<double>(index));
        const float a = m_sample->audioData[index];
        const float b = m_sample->audioData[index + 1];
        float value = a + (b - a) * fraction;

        value *= envelope() * m_level;

        m_position += m_step;
        m_envTime += 1.0f / m_engineRate;
        return value;
    }

private:
    float envelope() {
        if (m_released) {
            if (m_release <= 1e-5f) { m_playing = false; return 0.0f; }
            const float through = m_envTime / m_release;
            if (through >= 1.0f) { m_playing = false; return 0.0f; }
            m_envLevel = m_sustain * (1.0f - through);
            return m_envLevel;
        }
        if (m_envTime < m_attack) {
            m_envLevel = (m_attack > 1e-6f) ? m_envTime / m_attack : 1.0f;
            return m_envLevel;
        }
        const float intoDecay = m_envTime - m_attack;
        if (m_decay > 1e-6f && intoDecay < m_decay) {
            m_envLevel = 1.0f + (m_sustain - 1.0f) * (intoDecay / m_decay);
            return m_envLevel;
        }
        m_envLevel = m_sustain;
        return m_envLevel;
    }

    const Sample* m_sample = nullptr;
    SampleZone m_zone{};
    double m_position = 0.0;
    double m_step = 1.0;
    float m_engineRate = 44100.0f;
    float m_level = 1.0f;
    bool m_playing = false;
    bool m_released = false;

    float m_envTime = 0.0f;
    float m_envLevel = 0.0f;
    float m_attack = 0.001f;
    float m_decay = 0.0f;
    float m_sustain = 1.0f;
    float m_release = 0.08f;
};

// ============================================================================
// Validation
// ============================================================================
inline void clampSamplerInstrument(SamplerInstrument& instrument, int poolSize) {
    auto sane = [](float value, float lo, float hi, float fallback) {
        if (!std::isfinite(value)) return fallback;
        return std::max(lo, std::min(value, hi));
    };

    instrument.zoneCount = std::clamp(instrument.zoneCount, 0,
                                      SamplerInstrument::MAX_ZONES);
    instrument.velocityCrossfade = sane(instrument.velocityCrossfade, 0.0f, 0.5f, 0.08f);
    instrument.attack = sane(instrument.attack, 0.0f, 10.0f, 0.001f);
    instrument.decay = sane(instrument.decay, 0.0f, 20.0f, 0.0f);
    instrument.sustain = sane(instrument.sustain, 0.0f, 1.0f, 1.0f);
    instrument.release = sane(instrument.release, 0.0f, 20.0f, 0.08f);
    instrument.velocityToLevel = sane(instrument.velocityToLevel, 0.0f, 1.0f, 1.0f);

    for (int i = 0; i < instrument.zoneCount; ++i) {
        SampleZone& zone = instrument.zones[static_cast<size_t>(i)];

        // A sample id past the end of the pool would be an out-of-bounds
        // read in the audio thread.
        if (zone.sampleId < 0 || zone.sampleId >= poolSize) zone.sampleId = -1;

        zone.lowKey = std::clamp(zone.lowKey, 0, 127);
        zone.highKey = std::clamp(zone.highKey, 0, 127);
        if (zone.highKey < zone.lowKey) std::swap(zone.lowKey, zone.highKey);
        zone.rootKey = std::clamp(zone.rootKey, 0, 127);

        zone.lowVelocity = sane(zone.lowVelocity, 0.0f, 1.0f, 0.0f);
        zone.highVelocity = sane(zone.highVelocity, 0.0f, 1.0f, 1.0f);
        if (zone.highVelocity < zone.lowVelocity) {
            std::swap(zone.lowVelocity, zone.highVelocity);
        }

        zone.gain = sane(zone.gain, 0.0f, 4.0f, 1.0f);
        zone.pan = sane(zone.pan, -1.0f, 1.0f, 0.0f);
        zone.tuneCents = sane(zone.tuneCents, -2400.0f, 2400.0f, 0.0f);
        zone.loopStartSeconds = sane(zone.loopStartSeconds, 0.0f, 3600.0f, 0.0f);
        zone.loopEndSeconds = sane(zone.loopEndSeconds, 0.0f, 3600.0f, 0.0f);
        if (zone.loopEndSeconds > 0.0f && zone.loopEndSeconds <= zone.loopStartSeconds) {
            zone.loopEndSeconds = 0.0f;   // 0 means "to the end"
        }
        zone.roundRobinGroup = std::clamp(zone.roundRobinGroup, 0, 63);
    }
}

} // namespace ChiptuneTracker
