#pragma once

/*
 * ChiptuneTracker - AudioEngine
 *
 * Real-time audio engine using miniaudio with zero-allocation audio thread.
 * Implements a render callback pattern for lock-free audio processing.
 *
 * Architecture:
 *   - UI Thread: Sends commands via lock-free ring buffer
 *   - Audio Thread: Processes samples in real-time callback
 *   - No mutex, no allocations in hot path
 */

#include <atomic>
#include <cstdint>
#include <cmath>
#include <array>

// Forward declare miniaudio types to avoid including in header
struct ma_device;
struct ma_device_config;

namespace ChiptuneTracker {

// ============================================================================
// Constants
// ============================================================================
constexpr uint32_t SAMPLE_RATE = 44100;
constexpr uint32_t BUFFER_SIZE = 512;
constexpr float    TWO_PI = 6.28318530718f;

// ============================================================================
// Lock-Free Ring Buffer for UI -> Audio Thread Communication
// ============================================================================
template<typename T, size_t Capacity>
class LockFreeRingBuffer {
public:
    LockFreeRingBuffer() : m_head(0), m_tail(0) {}

    // Producer (UI thread) - Returns true if push succeeded
    bool push(const T& item) {
        const size_t currentTail = m_tail.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) % Capacity;

        if (nextTail == m_head.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }

        m_buffer[currentTail] = item;
        m_tail.store(nextTail, std::memory_order_release);
        return true;
    }

    // Consumer (Audio thread) - Returns true if pop succeeded
    bool pop(T& item) {
        const size_t currentHead = m_head.load(std::memory_order_relaxed);

        if (currentHead == m_tail.load(std::memory_order_acquire)) {
            return false; // Buffer empty
        }

        item = m_buffer[currentHead];
        m_head.store((currentHead + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> m_buffer;
    std::atomic<size_t> m_head;
    std::atomic<size_t> m_tail;
};

// ============================================================================
// Audio Commands (UI -> Audio Thread)
// ============================================================================
enum class AudioCommandType : uint8_t {
    SetFrequency,
    SetVolume,
    SetWaveform,
    NoteOn,
    NoteOff
};

enum class WaveformType : uint8_t {
    Sine,       // For testing
    Square,     // NES Pulse (will add PolyBLEP)
    Triangle,   // NES Triangle
    Sawtooth,   // PolyBLEP corrected
    Noise       // LFSR
};

struct AudioCommand {
    AudioCommandType type;
    union {
        float    frequency;
        float    volume;
        WaveformType waveform;
        uint8_t  note;
    } data;
};

// ============================================================================
// Oscillator State (Per-voice, no allocations)
// ============================================================================
struct OscillatorState {
    float    phase      = 0.0f;
    float    frequency  = 440.0f;
    float    volume     = 0.5f;
    WaveformType waveform = WaveformType::Sine;
    bool     active     = true;

    // LFSR state for noise
    uint16_t lfsr       = 0x0001;

    // PolyBLEP phase increment (cached for performance)
    float    phaseIncrement = 0.0f;

    void updatePhaseIncrement(uint32_t sampleRate) {
        phaseIncrement = frequency / static_cast<float>(sampleRate);
    }
};

// ============================================================================
// Audio Engine Class
// ============================================================================
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Lifecycle
    bool initialize();
    void shutdown();
    bool start();
    void stop();

    // State queries (thread-safe via atomics)
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    float getCpuLoad() const { return m_cpuLoad.load(std::memory_order_relaxed); }

    // UI Thread Interface (lock-free commands)
    void setFrequency(float hz);
    void setVolume(float vol);
    void setWaveform(WaveformType type);

    // Direct access for immediate UI feedback (atomic)
    float getCurrentFrequency() const { return m_displayFrequency.load(std::memory_order_relaxed); }
    float getCurrentVolume() const { return m_displayVolume.load(std::memory_order_relaxed); }

private:
    // Miniaudio callback (static to match C callback signature)
    static void audioCallback(void* device, void* output, const void* input, uint32_t frameCount);

    // Instance render method called by static callback
    void render(float* output, uint32_t frameCount);

    // Process pending commands from UI thread
    void processCommands();

    // Waveform generation (inline for performance)
    float generateSample(OscillatorState& osc);

    // PolyBLEP antialiasing correction
    float polyBlep(float t, float dt);

    // LFSR noise generation (NES-style)
    float generateNoise(OscillatorState& osc);

private:
    // Miniaudio device (opaque pointer to avoid header inclusion)
    ma_device* m_device = nullptr;

    // Audio thread state (no locks, direct access only in audio callback)
    OscillatorState m_oscillator;

    // Lock-free command queue (UI -> Audio)
    LockFreeRingBuffer<AudioCommand, 256> m_commandQueue;

    // Atomic state for thread-safe access
    std::atomic<bool>  m_running{false};
    std::atomic<float> m_cpuLoad{0.0f};

    // Display state (updated by audio thread, read by UI)
    std::atomic<float> m_displayFrequency{440.0f};
    std::atomic<float> m_displayVolume{0.5f};
};

} // namespace ChiptuneTracker
