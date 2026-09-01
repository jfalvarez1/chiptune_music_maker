#pragma once

/*
 * ChiptuneTracker - Convolution reverb
 *
 * An algorithmic reverb approximates a space with delays and allpasses. A
 * convolution reverb does not approximate anything: it convolves the signal
 * with a recording of a real space's impulse response, so the output is what
 * that signal would have sounded like in that room. There is nothing to tune
 * and nothing to get subtly wrong - the room is the data.
 *
 * Two things make this hard, and both are the reason it is a separate file
 * rather than another entry in Reverbs.h.
 *
 * DIRECT CONVOLUTION IS FAR TOO SLOW. One second of impulse response at
 * 44.1 kHz is 44,100 multiply-accumulates per output sample, per channel.
 * That is roughly two billion operations a second for a single voice. The
 * standard answer is to do it in the frequency domain, where convolution is
 * multiplication - but a single FFT of the whole response would need the
 * whole input first, which is not a thing a real-time effect has.
 *
 * So: UNIFORMLY PARTITIONED OVERLAP-SAVE. The response is cut into blocks,
 * each transformed once when it is loaded. The input is transformed one
 * block at a time and kept in a frequency-domain delay line. Each output
 * block is the sum of every input block multiplied by the matching response
 * block - the same convolution, rearranged so it can be computed as the
 * audio arrives. Latency is one block, and it is reported.
 *
 * MEMORY. The delay line is two floats per sample of response per instance -
 * about 700 KB per second of IR. Thirty-two channels and four buses would be
 * a hundred megabytes for an effect almost nobody enables on more than one
 * or two channels, so the engine allocates nothing until it is switched on,
 * and the transformed responses are shared rather than copied per channel.
 * In practice a convolution reverb is a send effect, which is exactly the
 * usage this shape suits.
 *
 * NOT CHIP-AUTHENTIC, and off by default like everything else in Task G.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "FFT.h"

namespace ChiptuneTracker {

// ============================================================================
// The impulse responses
// ============================================================================
/*
 * Built-ins are synthesised rather than sampled: a real IR library is
 * hundreds of megabytes of recordings and a licensing question, and the
 * point here is that the engine works and is usable out of the box. A user
 * with a real IR loads a WAV.
 *
 * Each is exponentially decaying noise shaped in the frequency domain, which
 * is what a diffuse late reverberation tail actually is - the early
 * reflections that distinguish one real room from another are exactly what a
 * synthetic IR cannot give you, and the reason to load a real one.
 */
enum class ImpulseResponse : uint8_t {
    SmallRoom = 0,
    LargeHall,
    BrightPlate,
    DarkChamber,
    Custom,      // loaded from a file
    Count
};

inline const char* impulseResponseName(ImpulseResponse ir) {
    switch (ir) {
        case ImpulseResponse::LargeHall:   return "Large Hall";
        case ImpulseResponse::BrightPlate: return "Bright Plate";
        case ImpulseResponse::DarkChamber: return "Dark Chamber";
        case ImpulseResponse::Custom:      return "Loaded file";
        case ImpulseResponse::SmallRoom:
        default:                           return "Small Room";
    }
}

/*
 * Build one of the synthetic responses.
 *
 * UI thread. Deterministic, so the same choice always gives the same room -
 * a reverb that differed between sessions would be unusable, and it would
 * also make the tests meaningless.
 */
inline std::vector<float> makeImpulseResponse(ImpulseResponse which,
                                              float sampleRate) {
    struct Shape { float seconds; float decay; float lowpass; float predelay; };
    Shape shape{};
    switch (which) {
        case ImpulseResponse::LargeHall:   shape = {2.0f, 3.2f, 0.30f, 0.030f}; break;
        case ImpulseResponse::BrightPlate: shape = {1.1f, 5.0f, 0.08f, 0.002f}; break;
        case ImpulseResponse::DarkChamber: shape = {1.4f, 4.0f, 0.72f, 0.018f}; break;
        case ImpulseResponse::SmallRoom:
        default:                           shape = {0.6f, 8.0f, 0.35f, 0.006f}; break;
    }

    const float rate = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
    const auto length = static_cast<size_t>(shape.seconds * rate);
    std::vector<float> ir(length, 0.0f);
    if (length < 8) return ir;

    // Its own generator, and a fixed seed per response, so the room is the
    // same every time the program runs.
    uint32_t rng = 0x9E3779B9u ^ (static_cast<uint32_t>(which) * 2654435761u);
    auto noise = [&rng]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (static_cast<float>(rng & 0x00FFFFFFu) /
                static_cast<float>(0x00800000u)) - 1.0f;
    };

    const auto predelay = static_cast<size_t>(shape.predelay * rate);
    float lowpassState = 0.0f;

    for (size_t i = 0; i < length; ++i) {
        if (i < predelay) continue;

        const float t = static_cast<float>(i - predelay) / rate;
        const float envelope = std::exp(-t * shape.decay);

        // Damping increases with time: a real tail loses treble as it
        // decays, because air and surfaces absorb high frequencies faster.
        const float damping = std::min(0.95f, shape.lowpass + t * 0.12f);
        lowpassState = noise() * (1.0f - damping) + lowpassState * damping;

        ir[i] = lowpassState * envelope;
    }

    // The direct sound. Without it the reverb has no anchor and the dry
    // signal seems to arrive from somewhere else than its own reflections.
    ir[0] = 1.0f;

    // Normalise to unit energy rather than unit peak, so swapping rooms
    // does not also change the level - which would make A/B comparison
    // useless, since louder always sounds better.
    double energy = 0.0;
    for (float v : ir) energy += double(v) * v;
    if (energy > 1e-9) {
        const float scale = static_cast<float>(1.0 / std::sqrt(energy));
        for (float& v : ir) v *= scale;
    }
    return ir;
}

// ============================================================================
// A response, transformed and partitioned
// ============================================================================
/*
 * Read-only once built, so it can be shared by every channel using it rather
 * than copied. Building is O(P) FFTs and happens on the UI thread.
 */
class PartitionedIR {
public:
    static constexpr int BLOCK = 512;        // latency, in samples
    static constexpr int FFT_SIZE = BLOCK * 2;

    // Two seconds is already 172 partitions and 1.4 MB of spectra; past that
    // the cost stops being worth what the extra tail adds.
    static constexpr float MAX_SECONDS = 2.0f;

    void build(const std::vector<float>& ir, float sampleRate) {
        m_partitions = 0;
        m_real.clear();
        m_imag.clear();
        if (ir.empty()) return;

        const auto maxSamples = static_cast<size_t>(MAX_SECONDS *
            ((sampleRate > 0.0f) ? sampleRate : 44100.0f));
        const size_t used = std::min(ir.size(), maxSamples);

        m_partitions = static_cast<int>((used + BLOCK - 1) / BLOCK);
        m_real.assign(static_cast<size_t>(m_partitions) * FFT_SIZE, 0.0f);
        m_imag.assign(static_cast<size_t>(m_partitions) * FFT_SIZE, 0.0f);

        DSP::FFTPlan plan;
        plan.resize(FFT_SIZE);

        for (int p = 0; p < m_partitions; ++p) {
            const size_t base = static_cast<size_t>(p) * FFT_SIZE;

            // Each partition occupies the first half; the second half stays
            // zero. That zero padding is what makes the circular
            // multiplication below compute a linear convolution.
            for (int i = 0; i < BLOCK; ++i) {
                const size_t source = static_cast<size_t>(p) * BLOCK +
                                      static_cast<size_t>(i);
                m_real[base + static_cast<size_t>(i)] =
                    (source < used) ? ir[source] : 0.0f;
            }
            plan.transform(&m_real[base], &m_imag[base]);
        }
    }

    int partitions() const { return m_partitions; }
    bool empty() const { return m_partitions == 0; }
    const float* real(int partition) const {
        return &m_real[static_cast<size_t>(partition) * FFT_SIZE];
    }
    const float* imag(int partition) const {
        return &m_imag[static_cast<size_t>(partition) * FFT_SIZE];
    }

private:
    int m_partitions = 0;
    std::vector<float> m_real;
    std::vector<float> m_imag;
};

// ============================================================================
// The engine
// ============================================================================
class ConvolutionEngine {
public:
    static constexpr int BLOCK = PartitionedIR::BLOCK;
    static constexpr int FFT_SIZE = PartitionedIR::FFT_SIZE;

    bool active() const { return m_ir != nullptr && !m_ir->empty(); }
    int latency() const { return active() ? BLOCK : 0; }

    /*
     * Attach a response and size the delay line. UI thread: this is the only
     * place the engine allocates, and it is why the effect costs nothing at
     * all until it is switched on.
     */
    void prepare(const PartitionedIR* ir) {
        m_ir = ir;
        if (!active()) {
            m_fdlReal.clear();
            m_fdlImag.clear();
            m_input.clear();
            m_output.clear();
            return;
        }

        m_plan.resize(FFT_SIZE);
        const size_t slots = static_cast<size_t>(m_ir->partitions()) * FFT_SIZE;
        m_fdlReal.assign(slots, 0.0f);
        m_fdlImag.assign(slots, 0.0f);

        m_input.assign(FFT_SIZE, 0.0f);
        m_output.assign(FFT_SIZE, 0.0f);
        m_scratchReal.assign(FFT_SIZE, 0.0f);
        m_scratchImag.assign(FFT_SIZE, 0.0f);
        m_accReal.assign(FFT_SIZE, 0.0f);
        m_accImag.assign(FFT_SIZE, 0.0f);

        m_filled = 0;
        m_slot = 0;
        m_primed = 0;
    }

    void reset() {
        std::fill(m_fdlReal.begin(), m_fdlReal.end(), 0.0f);
        std::fill(m_fdlImag.begin(), m_fdlImag.end(), 0.0f);
        std::fill(m_input.begin(), m_input.end(), 0.0f);
        std::fill(m_output.begin(), m_output.end(), 0.0f);
        m_filled = 0;
        m_slot = 0;
        m_primed = 0;
    }

    /*
     * One sample in, one sample out, delayed by a block.
     *
     * Audio thread. Everything is preallocated; the only work here is the
     * per-block burst of an FFT, P complex multiply-accumulates and an
     * inverse - which is why the block size is a latency-versus-spike
     * trade rather than a free parameter.
     */
    float process(float input) {
        if (!active() || m_input.empty()) return 0.0f;

        // The second half of the input buffer is the current block; the
        // first half is the previous one, which is what overlap-save needs
        // in order to discard the wrapped part of the result.
        m_input[static_cast<size_t>(BLOCK + m_filled)] = input;
        const float out = m_output[static_cast<size_t>(BLOCK + m_filled)];
        ++m_filled;

        if (m_filled >= BLOCK) {
            processBlock();
            m_filled = 0;
        }

        if (m_primed < BLOCK) { ++m_primed; return 0.0f; }
        return out;
    }

private:
    void processBlock() {
        const int partitions = m_ir->partitions();

        // ---- Transform this block and file it in the delay line -----------
        std::copy(m_input.begin(), m_input.end(), m_scratchReal.begin());
        std::fill(m_scratchImag.begin(), m_scratchImag.end(), 0.0f);
        m_plan.transform(m_scratchReal.data(), m_scratchImag.data());

        const size_t base = static_cast<size_t>(m_slot) * FFT_SIZE;
        std::copy(m_scratchReal.begin(), m_scratchReal.end(), m_fdlReal.begin() + base);
        std::copy(m_scratchImag.begin(), m_scratchImag.end(), m_fdlImag.begin() + base);

        // ---- Multiply and accumulate across every partition ---------------
        std::fill(m_accReal.begin(), m_accReal.end(), 0.0f);
        std::fill(m_accImag.begin(), m_accImag.end(), 0.0f);

        for (int p = 0; p < partitions; ++p) {
            // Partition p pairs with the input block from p blocks ago; that
            // walk backwards through the ring IS the delay in the delay line.
            int slot = m_slot - p;
            while (slot < 0) slot += partitions;
            const size_t inputBase = static_cast<size_t>(slot) * FFT_SIZE;

            const float* hr = m_ir->real(p);
            const float* hi = m_ir->imag(p);

            for (int k = 0; k < FFT_SIZE; ++k) {
                const size_t index = inputBase + static_cast<size_t>(k);
                const float xr = m_fdlReal[index];
                const float xi = m_fdlImag[index];
                const float yr = hr[k];
                const float yi = hi[k];

                m_accReal[static_cast<size_t>(k)] += xr * yr - xi * yi;
                m_accImag[static_cast<size_t>(k)] += xr * yi + xi * yr;
            }
        }

        // ---- Back to the time domain ---------------------------------------
        // Inverse by conjugation: conj, forward, conj, scale.
        for (int k = 0; k < FFT_SIZE; ++k) {
            m_accImag[static_cast<size_t>(k)] = -m_accImag[static_cast<size_t>(k)];
        }
        m_plan.transform(m_accReal.data(), m_accImag.data());

        const float scale = 1.0f / static_cast<float>(FFT_SIZE);
        for (int k = 0; k < FFT_SIZE; ++k) {
            m_output[static_cast<size_t>(k)] = m_accReal[static_cast<size_t>(k)] * scale;
        }

        // Slide the input window: this block becomes the previous one.
        for (int i = 0; i < BLOCK; ++i) {
            m_input[static_cast<size_t>(i)] = m_input[static_cast<size_t>(BLOCK + i)];
        }

        m_slot = (m_slot + 1) % partitions;
    }

    const PartitionedIR* m_ir = nullptr;
    DSP::FFTPlan m_plan;

    std::vector<float> m_fdlReal, m_fdlImag;
    std::vector<float> m_input, m_output;
    std::vector<float> m_scratchReal, m_scratchImag;
    std::vector<float> m_accReal, m_accImag;

    int m_filled = 0;
    int m_slot = 0;
    int m_primed = 0;
};

// ============================================================================
// The shared library of responses
// ============================================================================
/*
 * Owned by the Sequencer, like the wavetable library, and for the same
 * reason: the transformed spectra are read-only, and a copy per channel
 * would be megabytes of identical data.
 */
class IRLibrary {
public:
    static constexpr int SLOTS = static_cast<int>(ImpulseResponse::Count);

    // UI thread.
    void rebuild(float sampleRate, const std::vector<float>& customIR) {
        for (int i = 0; i < SLOTS; ++i) {
            const ImpulseResponse which = static_cast<ImpulseResponse>(i);
            if (which == ImpulseResponse::Custom) {
                m_ir[static_cast<size_t>(i)].build(customIR, sampleRate);
            } else {
                m_ir[static_cast<size_t>(i)].build(
                    makeImpulseResponse(which, sampleRate), sampleRate);
            }
        }
        m_sampleRate = sampleRate;
    }

    const PartitionedIR* get(int index) const {
        if (index < 0 || index >= SLOTS) return &m_ir[0];
        return &m_ir[static_cast<size_t>(index)];
    }

    float sampleRate() const { return m_sampleRate; }

private:
    std::array<PartitionedIR, SLOTS> m_ir;
    float m_sampleRate = 0.0f;
};

} // namespace ChiptuneTracker
