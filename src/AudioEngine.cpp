/*
 * ChiptuneTracker - AudioEngine Implementation
 *
 * Implements real-time audio synthesis with:
 *   - PolyBLEP antialiased oscillators
 *   - LFSR noise generation (NES-style)
 *   - Zero allocations in audio callback
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "AudioEngine.h"
#include <cstring>

namespace ChiptuneTracker {

// ============================================================================
// Constructor / Destructor
// ============================================================================

AudioEngine::AudioEngine() {
    m_device = new ma_device();
}

AudioEngine::~AudioEngine() {
    shutdown();
    delete m_device;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool AudioEngine::initialize() {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = SAMPLE_RATE;
    config.dataCallback      = reinterpret_cast<ma_device_data_proc>(audioCallback);
    config.pUserData         = this;
    config.periodSizeInFrames = BUFFER_SIZE;

    if (ma_device_init(nullptr, &config, m_device) != MA_SUCCESS) {
        return false;
    }

    // Initialize oscillator
    m_oscillator.frequency = 440.0f;
    m_oscillator.volume = 0.5f;
    m_oscillator.phase = 0.0f;
    m_oscillator.waveform = WaveformType::Sine;
    m_oscillator.updatePhaseIncrement(SAMPLE_RATE);

    // Initialize LFSR with non-zero seed
    m_oscillator.lfsr = 0x0001;

    return true;
}

void AudioEngine::shutdown() {
    stop();
    if (m_device) {
        ma_device_uninit(m_device);
    }
}

bool AudioEngine::start() {
    if (ma_device_start(m_device) != MA_SUCCESS) {
        return false;
    }
    m_running.store(true, std::memory_order_release);
    return true;
}

void AudioEngine::stop() {
    m_running.store(false, std::memory_order_release);
    if (m_device) {
        ma_device_stop(m_device);
    }
}

// ============================================================================
// UI Thread Interface (Lock-Free Command Submission)
// ============================================================================

void AudioEngine::setFrequency(float hz) {
    AudioCommand cmd;
    cmd.type = AudioCommandType::SetFrequency;
    cmd.data.frequency = hz;
    m_commandQueue.push(cmd);

    // Update display value immediately for UI responsiveness
    m_displayFrequency.store(hz, std::memory_order_relaxed);
}

void AudioEngine::setVolume(float vol) {
    AudioCommand cmd;
    cmd.type = AudioCommandType::SetVolume;
    cmd.data.volume = vol;
    m_commandQueue.push(cmd);

    m_displayVolume.store(vol, std::memory_order_relaxed);
}

void AudioEngine::setWaveform(WaveformType type) {
    AudioCommand cmd;
    cmd.type = AudioCommandType::SetWaveform;
    cmd.data.waveform = type;
    m_commandQueue.push(cmd);
}

// ============================================================================
// Audio Callback (Called by miniaudio from audio thread)
// ============================================================================

void AudioEngine::audioCallback(void* device, void* output, const void* /*input*/, uint32_t frameCount) {
    ma_device* pDevice = static_cast<ma_device*>(device);
    AudioEngine* engine = static_cast<AudioEngine*>(pDevice->pUserData);

    if (engine) {
        engine->render(static_cast<float*>(output), frameCount);
    }
}

// ============================================================================
// Render Method (Audio Thread - Zero Allocations)
// ============================================================================

void AudioEngine::render(float* output, uint32_t frameCount) {
    // Process any pending commands from UI thread
    processCommands();

    // Generate audio samples
    for (uint32_t i = 0; i < frameCount; ++i) {
        float sample = 0.0f;

        if (m_oscillator.active) {
            sample = generateSample(m_oscillator);
        }

        // Stereo output (same signal both channels)
        output[i * 2]     = sample;
        output[i * 2 + 1] = sample;
    }
}

// ============================================================================
// Command Processing (Audio Thread)
// ============================================================================

void AudioEngine::processCommands() {
    AudioCommand cmd;

    // Process all pending commands (non-blocking)
    while (m_commandQueue.pop(cmd)) {
        switch (cmd.type) {
            case AudioCommandType::SetFrequency:
                m_oscillator.frequency = cmd.data.frequency;
                m_oscillator.updatePhaseIncrement(SAMPLE_RATE);
                break;

            case AudioCommandType::SetVolume:
                m_oscillator.volume = cmd.data.volume;
                break;

            case AudioCommandType::SetWaveform:
                m_oscillator.waveform = cmd.data.waveform;
                // Reset phase on waveform change for clean transition
                m_oscillator.phase = 0.0f;
                break;

            case AudioCommandType::NoteOn:
                m_oscillator.active = true;
                break;

            case AudioCommandType::NoteOff:
                m_oscillator.active = false;
                break;
        }
    }
}

// ============================================================================
// Sample Generation (Audio Thread - Inline Critical Path)
// ============================================================================

float AudioEngine::generateSample(OscillatorState& osc) {
    float sample = 0.0f;
    const float dt = osc.phaseIncrement;
    const float t = osc.phase;

    switch (osc.waveform) {
        case WaveformType::Sine:
            sample = std::sin(t * TWO_PI);
            break;

        case WaveformType::Square: {
            // Naive square wave with PolyBLEP correction
            sample = (t < 0.5f) ? 1.0f : -1.0f;
            sample += polyBlep(t, dt);
            sample -= polyBlep(std::fmod(t + 0.5f, 1.0f), dt);
            break;
        }

        case WaveformType::Sawtooth: {
            // Naive sawtooth with PolyBLEP correction
            sample = 2.0f * t - 1.0f;
            sample -= polyBlep(t, dt);
            break;
        }

        case WaveformType::Triangle: {
            // Triangle from integrated square wave (naturally bandlimited)
            // Using naive approach - sufficient for low frequencies
            sample = (t < 0.5f)
                ? 4.0f * t - 1.0f
                : 3.0f - 4.0f * t;
            break;
        }

        case WaveformType::Noise:
            sample = generateNoise(osc);
            break;
    }

    // Advance phase
    osc.phase += dt;
    if (osc.phase >= 1.0f) {
        osc.phase -= 1.0f;
    }

    return sample * osc.volume;
}

// ============================================================================
// PolyBLEP Antialiasing
// ============================================================================

float AudioEngine::polyBlep(float t, float dt) {
    // PolyBLEP (Polynomial Bandlimited Step)
    // Reduces aliasing at discontinuities in square/saw waves

    if (t < dt) {
        // Beginning of period
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    else if (t > 1.0f - dt) {
        // End of period
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

// ============================================================================
// LFSR Noise Generation (NES-Style)
// ============================================================================

float AudioEngine::generateNoise(OscillatorState& osc) {
    // 15-bit LFSR (Linear Feedback Shift Register)
    // NES uses taps at bits 0 and 1 (short mode) or 0 and 6 (long mode)
    // We use long mode (bit 0 XOR bit 6) for richer noise

    // Clock the LFSR based on frequency
    // Higher frequency = more clocks per sample = higher pitched noise
    static float noiseAccum = 0.0f;
    noiseAccum += osc.phaseIncrement * 16.0f; // Scale factor for audible range

    while (noiseAccum >= 1.0f) {
        noiseAccum -= 1.0f;

        // Compute feedback (bit 0 XOR bit 6)
        uint16_t feedback = ((osc.lfsr >> 0) ^ (osc.lfsr >> 6)) & 1;

        // Shift right and insert feedback at bit 14
        osc.lfsr = (osc.lfsr >> 1) | (feedback << 14);
    }

    // Output: convert bit 0 to -1.0 or +1.0
    return (osc.lfsr & 1) ? 1.0f : -1.0f;
}

} // namespace ChiptuneTracker
