#pragma once

/*
 * ChiptuneTracker - MIDI Input Handler
 *
 * Real-time MIDI keyboard input with recording capabilities:
 * - Device enumeration and selection
 * - Note on/off event capture
 * - Quantization (snap to grid)
 * - Overdub vs Replace modes
 */

#include "Types.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

// libremidi header-only mode
#define LIBREMIDI_HEADER_ONLY
#include "../vendor/libremidi/include/libremidi/libremidi.hpp"

namespace ChiptuneTracker {

// ============================================================================
// MIDI Device Info
// ============================================================================
struct MIDIDeviceInfo {
    std::string name;
    int index;
};

// ============================================================================
// MIDI Note Event
// ============================================================================
struct MIDINoteEvent {
    int pitch;           // MIDI note number (0-127)
    float velocity;      // Velocity (0.0-1.0)
    bool isNoteOn;       // true = note on, false = note off
    double timestamp;    // Time when event occurred
};

// ============================================================================
// MIDI Input Handler
// ============================================================================
class MIDIInput {
public:
    enum class RecordMode {
        Off,        // Not recording, just playing through
        Replace,    // Replace existing notes in pattern
        Overdub     // Add to existing notes in pattern
    };

    MIDIInput() {
        enumerateDevices();
    }

    ~MIDIInput() {
        closeDevice();
    }

    // Get list of available MIDI devices
    std::vector<MIDIDeviceInfo> getDevices() const {
        return m_devices;
    }

    // Open a MIDI device by index
    bool openDevice(int deviceIndex) {
        closeDevice();

        try {
            // Find the device port
            libremidi::observer obs;
            auto ports = obs.get_input_ports();

            if (deviceIndex < 0 || deviceIndex >= (int)ports.size()) {
                return false;
            }

            // Create MIDI input with callback
            libremidi::input_configuration config;
            config.on_message = [this](const libremidi::message& message) {
                handleMIDIMessage(message);
            };

            // Create MIDI input and open the port
            m_midiIn = std::make_unique<libremidi::midi_in>(config);
            m_midiIn->open_port(ports[deviceIndex]);

            m_currentDevice = deviceIndex;
            return true;

        } catch (const std::exception& e) {
            m_errorMessage = e.what();
            return false;
        }
    }

    // Close current device
    void closeDevice() {
        m_midiIn.reset();
        m_currentDevice = -1;
    }

    // Get current device index
    int getCurrentDevice() const {
        return m_currentDevice;
    }

    // Set note event callback (for live playback)
    void setNoteEventCallback(std::function<void(int pitch, float velocity, bool isNoteOn)> callback) {
        m_noteEventCallback = callback;
    }

    // Recording control
    void setRecordMode(RecordMode mode) {
        m_recordMode = mode;
    }

    RecordMode getRecordMode() const {
        return m_recordMode;
    }

    void setQuantization(float beatGrid) {
        m_quantizeBeatGrid = beatGrid; // e.g., 0.25 for 16th notes, 0.5 for 8th notes
    }

    float getQuantization() const {
        return m_quantizeBeatGrid;
    }

    // Enable/disable quantization
    void setQuantizeEnabled(bool enabled) {
        m_quantizeEnabled = enabled;
    }

    bool isQuantizeEnabled() const {
        return m_quantizeEnabled;
    }

    // Get recorded notes (for adding to pattern)
    const std::vector<Note>& getRecordedNotes() const {
        return m_recordedNotes;
    }

    // Clear recorded notes
    void clearRecordedNotes() {
        m_recordedNotes.clear();
    }

    // Start recording (should be called when sequencer starts playing)
    void startRecording(float currentBeat) {
        m_recordingStartBeat = currentBeat;
        m_recordedNotes.clear();
        m_activeNotes.clear();
    }

    // Stop recording
    void stopRecording() {
        m_recordMode = RecordMode::Off;

        // Note off any still-active notes
        for (auto& pair : m_activeNotes) {
            finalizeNote(pair.first, pair.second.startBeat);
        }
        m_activeNotes.clear();
    }

    // Update recording state (call this from sequencer during playback)
    void updateRecording(float currentBeat) {
        if (m_recordMode == RecordMode::Off) return;

        // Process any pending MIDI events and convert to notes
        // This is handled by the MIDI callback which adds to m_recordedNotes
    }

    // Get error message
    std::string getErrorMessage() const {
        return m_errorMessage;
    }

private:
    void enumerateDevices() {
        m_devices.clear();

        try {
            libremidi::observer obs;
            auto ports = obs.get_input_ports();

            for (size_t i = 0; i < ports.size(); i++) {
                MIDIDeviceInfo info;
                info.name = ports[i].port_name;
                info.index = (int)i;
                m_devices.push_back(info);
            }

        } catch (const std::exception& e) {
            m_errorMessage = e.what();
        }
    }

    void handleMIDIMessage(const libremidi::message& message) {
        if (message.size() < 3) return;

        uint8_t status = message[0];
        uint8_t data1 = message[1];
        uint8_t data2 = message[2];

        // Note on (status 0x90-0x9F)
        if ((status & 0xF0) == 0x90) {
            int pitch = data1;
            float velocity = data2 / 127.0f;

            // Velocity 0 is actually note off
            if (velocity == 0.0f) {
                handleNoteOff(pitch);
            } else {
                handleNoteOn(pitch, velocity);
            }
        }
        // Note off (status 0x80-0x8F)
        else if ((status & 0xF0) == 0x80) {
            int pitch = data1;
            handleNoteOff(pitch);
        }
    }

    void handleNoteOn(int pitch, float velocity) {
        // Trigger note event callback for live playback
        if (m_noteEventCallback) {
            m_noteEventCallback(pitch, velocity, true);
        }

        // If recording, track this note
        if (m_recordMode != RecordMode::Off) {
            // Get current time (you'll need to pass this from sequencer)
            // For now, we'll use system time and convert later
            ActiveNote activeNote;
            activeNote.pitch = pitch;
            activeNote.velocity = velocity;
            activeNote.startBeat = 0.0f; // Will be set by sequencer
            m_activeNotes[pitch] = activeNote;
        }
    }

    void handleNoteOff(int pitch) {
        // Trigger note event callback for live playback
        if (m_noteEventCallback) {
            m_noteEventCallback(pitch, 0.0f, false);
        }

        // If recording, finalize this note
        if (m_recordMode != RecordMode::Off && m_activeNotes.count(pitch) > 0) {
            ActiveNote& activeNote = m_activeNotes[pitch];
            finalizeNote(pitch, activeNote.startBeat);
            m_activeNotes.erase(pitch);
        }
    }

    void finalizeNote(int pitch, float startBeat) {
        // Create a Note and add to recorded notes
        // This will be called when note off is received
        // The sequencer will handle adding these to the pattern
    }

    float quantize(float beat) const {
        if (!m_quantizeEnabled || m_quantizeBeatGrid <= 0.0f) {
            return beat;
        }
        return std::round(beat / m_quantizeBeatGrid) * m_quantizeBeatGrid;
    }

    struct ActiveNote {
        int pitch;
        float velocity;
        float startBeat;
    };

    std::unique_ptr<libremidi::midi_in> m_midiIn;
    std::vector<MIDIDeviceInfo> m_devices;
    int m_currentDevice = -1;
    std::string m_errorMessage;

    std::function<void(int pitch, float velocity, bool isNoteOn)> m_noteEventCallback;

    RecordMode m_recordMode = RecordMode::Off;
    float m_quantizeBeatGrid = 0.25f; // 16th notes by default
    bool m_quantizeEnabled = true;

    float m_recordingStartBeat = 0.0f;
    std::vector<Note> m_recordedNotes;
    std::map<int, ActiveNote> m_activeNotes; // pitch -> ActiveNote
};

} // namespace ChiptuneTracker
