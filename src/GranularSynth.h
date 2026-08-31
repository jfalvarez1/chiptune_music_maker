#pragma once

/*
 * ChiptuneTracker - Granular synthesis
 *
 * A granular synth cuts a recording into very short windowed fragments -
 * grains, typically 10 to 100 ms - and plays a stream of them. Move the read
 * position slowly through the sample and you get a time-stretch that does
 * not change pitch; hold the position still and you get a drone made of one
 * moment of the recording; scatter the positions and pitches and you get
 * clouds and textures that have no other way of being made.
 *
 * Four things decide whether this sounds like an instrument or like a fault:
 *
 * THE GRAIN WINDOW. Every grain is multiplied by an envelope that starts and
 * ends at zero. Without one, each grain begins and ends at whatever the
 * waveform happened to be doing, and a stream of grains is a stream of
 * clicks at the grain rate - which is in the audio band, so it is not a
 * texture, it is a buzz at the grain frequency.
 *
 * OVERLAP. Density times grain length is how many grains sound at once. Below
 * one the output has audible gaps and pulses at the grain rate. Around two to
 * four it is smooth. That product also decides the loudness, which is why the
 * output is scaled by it - otherwise turning density up is just a volume
 * control with extra steps.
 *
 * JITTER. Grains fired on an exact clock at an exact position produce a
 * strong periodic component at the grain rate, which is heard as a pitch of
 * its own on top of the material. Scattering the start times and read
 * positions slightly is what turns that tone back into texture.
 *
 * RANDOMNESS ON THE AUDIO THREAD. This uses its own xorshift, per voice.
 * rand() is not guaranteed reentrant, shares hidden state across threads, and
 * on some runtimes takes a lock - none of which belongs in a callback. A
 * per-voice generator is also reproducible, which is what makes the tests
 * below possible at all.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Sample.h"

namespace ChiptuneTracker {

// ============================================================================
// Parameters
// ============================================================================
struct GranularConfig {
    int sampleId = -1;

    // Where in the sample the grains are taken from, 0..1.
    float position = 0.0f;

    // How fast the read position travels on its own, in multiples of real
    // time. 0 freezes it - a drone made from one moment. 1 is normal speed,
    // and -1 plays the sample backwards, both without changing pitch, which
    // is the trick granular is most often wanted for.
    float positionRate = 1.0f;

    // How far grains scatter around the read position, in seconds. This is
    // the difference between a texture and a tone at the grain rate.
    float spray = 0.01f;

    float grainSeconds = 0.05f;      // 50 ms
    float grainsPerSecond = 40.0f;   // density

    // Pitch, and how far individual grains stray from it, in semitones.
    float pitchSemitones = 0.0f;
    float pitchJitter = 0.0f;

    /*
     * There is deliberately no per-grain stereo spread.
     *
     * Scattering grains across the stereo field is a signature granular
     * effect and it cannot reach the output here: Synthesizer::process
     * returns one float, and the whole voice chain is mono until the channel
     * pan in the Sequencer. Adding it would mean threading a second signal
     * through the synth, the per-channel mix array, the sends and the insert
     * rack - a stereo-voice change, not a granular one.
     *
     * A control that silently does nothing is worse than an absent one; the
     * dead stereo widener and the dead sidechain both shipped that way. When
     * voices go stereo, this is the first thing to add back.
     */

    // The chance a grain plays backwards, 0..1. Reversed grains inside a
    // forward stream are a granular signature.
    float reverseChance = 0.0f;

    // Grain window shape: 0 is a Hann (smoothest, most washed), 1 is nearly
    // rectangular with short fades (most transient, most rhythmic).
    float windowShape = 0.0f;

    bool followNote = true;   // pitch tracks the played note
    int rootKey = 60;
};

// ============================================================================
// One grain
// ============================================================================
struct Grain {
    bool active = false;
    double position = 0.0;      // read head, in source samples
    double step = 1.0;          // source samples per output sample
    float age = 0.0f;           // seconds since it started
    float length = 0.05f;       // seconds
    bool reversed = false;
    double start = 0.0;         // where it began, for the reverse read
};

// ============================================================================
// The voice
// ============================================================================
class GranularVoice {
public:
    // Enough for a 100 ms grain at 400 grains a second, which is far past
    // where the result stops being distinguishable from noise. Fixed, so the
    // audio thread allocates nothing.
    static constexpr int MAX_GRAINS = 48;

    void reset(uint32_t seed) {
        for (Grain& grain : m_grains) grain.active = false;
        m_readPosition = 0.0;
        m_untilNextGrain = 0.0f;
        m_playing = false;
        // Never zero: xorshift is stuck at zero forever, which would make
        // every grain land at exactly the same place - a tone at the grain
        // rate rather than a texture, and a very confusing bug to look at.
        m_rng = (seed == 0u) ? 0x9E3779B9u : seed;
    }

    void trigger(const GranularConfig& config, const Sample* sample,
                 int note, float velocity, float engineRate, uint32_t seed) {
        reset(seed);
        m_sample = sample;
        m_engineRate = (engineRate > 0.0f) ? engineRate : 44100.0f;
        m_velocity = std::clamp(velocity, 0.0f, 1.0f);

        if (sample == nullptr || !sample->isLoaded || sample->audioData.size() < 4) {
            return;
        }

        const float semitones = config.pitchSemitones +
            (config.followNote ? static_cast<float>(note - config.rootKey) : 0.0f);
        m_pitchRatio = std::pow(2.0f, semitones / 12.0f);

        // The sample's rate against the engine's, the same term the sampler
        // needed and the old SampleOscillator was missing.
        m_rateRatio = static_cast<float>(sample->sampleRate) / m_engineRate;

        m_readPosition = static_cast<double>(std::clamp(config.position, 0.0f, 1.0f)) *
                         static_cast<double>(sample->audioData.size() - 2);
        m_playing = true;
    }

    void stop() { m_playing = false; }
    bool isPlaying() const { return m_playing; }

    /*
     * One sample.
     *
     * Audio thread: fixed arrays, no allocation, no locks, and its own RNG.
     */
    float process(const GranularConfig& config) {
        if (!m_playing || m_sample == nullptr) return 0.0f;

        const auto sourceSize = static_cast<double>(m_sample->audioData.size());
        if (sourceSize < 4.0) { m_playing = false; return 0.0f; }

        float out = 0.0f;

        const float step = 1.0f / m_engineRate;

        // Advance the read head. It wraps rather than stopping: a granular
        // voice held past the end of its source should keep making sound,
        // which is what "freeze" and "drone" mean here.
        const double sourceRate = static_cast<double>(m_sample->sampleRate);
        m_readPosition += static_cast<double>(config.positionRate) *
                          sourceRate * static_cast<double>(step);
        if (m_readPosition >= sourceSize - 2.0) {
            m_readPosition = std::fmod(m_readPosition, sourceSize - 2.0);
        }
        if (m_readPosition < 0.0) {
            m_readPosition += sourceSize - 2.0;
        }

        // Fire new grains.
        const float density = std::max(0.1f, config.grainsPerSecond);
        m_untilNextGrain -= step;
        if (m_untilNextGrain <= 0.0f) {
            spawn(config, sourceSize);
            // The interval is jittered, because grains on an exact clock
            // produce a strong periodic component at the grain rate - heard
            // as a pitch of its own sitting on top of the material.
            const float interval = 1.0f / density;
            m_untilNextGrain = interval * (0.75f + 0.5f * random01());
        }

        int sounding = 0;
        for (Grain& grain : m_grains) {
            if (!grain.active) continue;

            grain.age += step;
            if (grain.age >= grain.length) { grain.active = false; continue; }

            const double readAt = grain.reversed
                ? grain.start - (grain.position - grain.start)
                : grain.position;

            if (readAt < 0.0 || readAt >= sourceSize - 1.0) {
                grain.active = false;
                continue;
            }

            const auto index = static_cast<size_t>(readAt);
            const float fraction = static_cast<float>(readAt - static_cast<double>(index));
            const float a = m_sample->audioData[index];
            const float b = m_sample->audioData[index + 1];
            out += (a + (b - a) * fraction) *
                   grainWindow(grain.age / grain.length, config.windowShape);

            grain.position += grain.step;
            ++sounding;
        }

        /*
         * Normalise by the expected overlap, not by the count actually
         * sounding.
         *
         * Dividing by the live count would make the output level jump every
         * time a grain started or ended, which is an amplitude modulation at
         * the grain rate - the exact artefact the windowing exists to avoid.
         * The expected overlap is smooth because it depends only on the
         * settings.
         */
        const float overlap = std::max(1.0f, density * config.grainSeconds);
        (void)sounding;
        return out * m_velocity / std::sqrt(overlap);
    }

private:
    void spawn(const GranularConfig& config, double sourceSize) {
        Grain* slot = nullptr;
        for (Grain& grain : m_grains) {
            if (!grain.active) { slot = &grain; break; }
        }
        // All slots busy: drop the grain rather than stealing one. Stealing
        // cuts a grain off mid-window, which is the click the window exists
        // to prevent.
        if (slot == nullptr) return;

        const double sourceRate = static_cast<double>(m_sample->sampleRate);
        const double sprayed = m_readPosition +
            static_cast<double>((random01() * 2.0f - 1.0f) * config.spray) * sourceRate;

        double start = sprayed;
        if (start < 0.0) start += sourceSize - 2.0;
        if (start >= sourceSize - 2.0) start = std::fmod(start, sourceSize - 2.0);

        const float jitter = (random01() * 2.0f - 1.0f) * config.pitchJitter;
        const float ratio = m_pitchRatio * std::pow(2.0f, jitter / 12.0f);

        slot->active = true;
        slot->start = start;
        slot->position = start;
        slot->step = static_cast<double>(ratio) * static_cast<double>(m_rateRatio);
        slot->age = 0.0f;
        slot->length = std::max(0.002f, config.grainSeconds);
        slot->reversed = (random01() < config.reverseChance);
    }

    /*
     * The grain envelope.
     *
     * shape 0 is a Hann window: smooth, and the most "washed" sounding.
     * shape 1 is nearly rectangular with short raised-cosine fades, which
     * keeps transients intact and makes the result rhythmic rather than
     * smeared. Both start and end at exactly zero, which is the whole point
     * - a grain that does not is a click at the grain rate.
     */
    static float grainWindow(float through, float shape) {
        through = std::clamp(through, 0.0f, 1.0f);
        const float clampedShape = std::clamp(shape, 0.0f, 1.0f);

        const float hann = 0.5f * (1.0f - std::cos(6.28318530718f * through));

        // The flat-topped end: fade over the first and last `edge` fraction.
        const float edge = 0.5f - 0.45f * clampedShape;   // 0.5 -> 0.05
        float flat = 1.0f;
        if (through < edge) {
            flat = 0.5f * (1.0f - std::cos(3.14159265359f * through / edge));
        } else if (through > 1.0f - edge) {
            const float from = (1.0f - through) / edge;
            flat = 0.5f * (1.0f - std::cos(3.14159265359f * from));
        }

        return hann * (1.0f - clampedShape) + flat * clampedShape;
    }

    // xorshift32. Deterministic per voice, which is what makes the tests
    // below possible, and free of the hidden shared state rand() carries.
    float random01() {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return static_cast<float>(m_rng & 0x00FFFFFFu) /
               static_cast<float>(0x01000000u);
    }

    std::array<Grain, MAX_GRAINS> m_grains{};
    const Sample* m_sample = nullptr;
    double m_readPosition = 0.0;
    float m_untilNextGrain = 0.0f;
    float m_engineRate = 44100.0f;
    float m_pitchRatio = 1.0f;
    float m_rateRatio = 1.0f;
    float m_velocity = 1.0f;
    bool m_playing = false;
    uint32_t m_rng = 0x9E3779B9u;
};

// ============================================================================
// Validation
// ============================================================================
inline void clampGranularConfig(GranularConfig& config, int poolSize) {
    auto sane = [](float value, float lo, float hi, float fallback) {
        if (!std::isfinite(value)) return fallback;
        return std::max(lo, std::min(value, hi));
    };

    if (config.sampleId < 0 || config.sampleId >= poolSize) config.sampleId = -1;

    config.position = sane(config.position, 0.0f, 1.0f, 0.0f);
    config.positionRate = sane(config.positionRate, -4.0f, 4.0f, 1.0f);
    config.spray = sane(config.spray, 0.0f, 5.0f, 0.01f);

    // A grain shorter than about 2 ms is a click however it is windowed, and
    // one longer than a second is not a grain.
    config.grainSeconds = sane(config.grainSeconds, 0.002f, 1.0f, 0.05f);

    // Density has a floor as well as a ceiling: zero would divide by zero
    // working out the interval, in the audio thread.
    config.grainsPerSecond = sane(config.grainsPerSecond, 0.5f, 400.0f, 40.0f);

    config.pitchSemitones = sane(config.pitchSemitones, -48.0f, 48.0f, 0.0f);
    config.pitchJitter = sane(config.pitchJitter, 0.0f, 24.0f, 0.0f);
    config.reverseChance = sane(config.reverseChance, 0.0f, 1.0f, 0.0f);
    config.windowShape = sane(config.windowShape, 0.0f, 1.0f, 0.0f);
    config.rootKey = std::clamp(config.rootKey, 0, 127);
}

} // namespace ChiptuneTracker
