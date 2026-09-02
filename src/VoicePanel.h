#pragma once

/*
 * ChiptuneTracker - Voice to notes
 *
 * Sing a melody or beatbox a groove and get notes in the pattern.
 *
 * The detection itself is not new: AudioAnalyzer.cpp has had melodic
 * tracking, drum classification, key detection and harmony for a long time.
 * What it did not have was a way in. It was compiled into a separate
 * VoiceToNote.exe that worked only on a WAV on disk, so using it meant
 * recording in one program, exporting a .ctp, and opening it in the other -
 * which is enough friction that the feature effectively did not exist.
 *
 * Two paths, sharing the same detector:
 *
 *   OFFLINE - record or import a whole take, analyse it, review what was
 *   found, insert it. Better accuracy, because it can look at the future.
 *
 *   LIVE - watch the note or the drum hit appear as it is sung. Worse
 *   accuracy, because it cannot, and worth it because you can hear
 *   immediately whether the thing is following you.
 *
 * The audio callback fills a lock-free ring and does nothing else. All
 * analysis is on the UI thread. This is the bug that was fixed in 3.3 when
 * the spectrum analyzer ran an FFT inside the callback, and it is not being
 * reintroduced.
 *
 * Notes are written into the pattern through g_UndoHistory like every other
 * edit, so a take that came out wrong is one Ctrl+Z away.
 */

#include "Types.h"
#include "LiveVoice.h"
#include "VoiceCapture.h"
#include "AudioAnalyzer.h"
#include "UndoHistory.h"

#include "../vendor/miniaudio/miniaudio.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// Capture device
// ============================================================================
/*
 * A microphone that writes into a ring and nothing else.
 *
 * The existing AudioRecorder takes a std::mutex and does a vector insert in
 * its callback. Both allocate or block, and the capture thread can do
 * neither. This one's callback is a peak scan and a ring write.
 */
class VoiceCaptureDevice {
public:
    struct DeviceInfo {
        std::string name;
        ma_device_id id;
    };

    ~VoiceCaptureDevice() { shutdown(); }

    std::vector<DeviceInfo> enumerate() {
        std::vector<DeviceInfo> found;
        ma_context context;
        if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) return found;

        ma_device_info* playback = nullptr;
        ma_uint32 playbackCount = 0;
        ma_device_info* capture = nullptr;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context, &playback, &playbackCount,
                                   &capture, &captureCount) == MA_SUCCESS) {
            for (ma_uint32 i = 0; i < captureCount; ++i) {
                found.push_back({capture[i].name, capture[i].id});
            }
        }
        ma_context_uninit(&context);
        return found;
    }

    bool start(const DeviceInfo* device) {
        shutdown();

        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.format = ma_format_f32;
        config.capture.channels = 1;
        // The device's own rate. Forcing 44100 makes WASAPI refuse outright
        // on hardware that runs at 48k or 16k, which is most headsets.
        config.sampleRate = 0;
        config.dataCallback = &VoiceCaptureDevice::callback;
        config.pUserData = this;

        if (device != nullptr) {
            config.capture.pDeviceID = const_cast<ma_device_id*>(&device->id);
        }

        if (ma_device_init(nullptr, &config, &m_device) != MA_SUCCESS) {
            // Fall back to the default rather than reporting failure: a named
            // device that has been unplugged is the common case.
            config.capture.pDeviceID = nullptr;
            if (ma_device_init(nullptr, &config, &m_device) != MA_SUCCESS) {
                return false;
            }
        }
        m_initialized = true;
        m_sampleRate = static_cast<int>(m_device.sampleRate);
        m_ring.clear();
        m_ring.resetDropped();

        if (ma_device_start(&m_device) != MA_SUCCESS) {
            shutdown();
            return false;
        }
        m_running = true;
        return true;
    }

    void shutdown() {
        if (m_initialized) {
            ma_device_uninit(&m_device);
            m_initialized = false;
        }
        m_running = false;
    }

    bool running() const { return m_running; }
    int sampleRate() const { return m_sampleRate; }
    VoiceRing& ring() { return m_ring; }
    float peak() const { return m_meter.value(); }
    void decayMeter() { m_meter.push(0.0f); }
    uint64_t dropped() const { return m_ring.droppedSamples(); }

private:
    static void callback(ma_device* device, void*, const void* input,
                         ma_uint32 frameCount) {
        auto* self = static_cast<VoiceCaptureDevice*>(device->pUserData);
        if (self == nullptr || input == nullptr) return;

        const float* samples = static_cast<const float*>(input);

        // The peak is computed here, where the data is already in cache and
        // the pass is O(n) with no allocation. It is the one piece of
        // analysis that belongs on this side - without it the meter could
        // not move until the UI happened to drain the ring.
        self->m_meter.push(blockPeak(samples, frameCount));
        self->m_ring.write(samples, frameCount);
    }

    ma_device m_device{};
    bool m_initialized = false;
    bool m_running = false;
    int m_sampleRate = 48000;
    VoiceRing m_ring;
    PeakMeter m_meter;
};

// ============================================================================
// Panel state
// ============================================================================
struct VoicePanelState {
    bool open = false;

    // Live vs offline.
    bool liveMode = true;

    // Device selection.
    std::vector<VoiceCaptureDevice::DeviceInfo> devices;
    int selectedDevice = -1;
    bool devicesEnumerated = false;
    std::string lastError;

    // Offline capture: the whole take, accumulated on the UI thread from the
    // ring. Kept here rather than in the device so that stopping the device
    // does not throw the take away.
    std::vector<float> take;
    bool capturingTake = false;
    int takeSampleRate = 48000;

    // What the detector found.
    std::vector<DetectedNote> detected;
    DetectedKey detectedKey{0, false, 0.0f};
    bool keyDetected = false;

    // Conversion options.
    VoiceToNotesOptions options;
    int analysisMode = 0;    // 0 melody, 1 drums, 2 rhythm, 3 polyphonic
    float sensitivity = 0.5f;
    bool replacePattern = false;
    int targetChannel = 0;

    /*
     * Where the take is going.
     *
     * -1 means a new pattern, which is the default: a take that silently
     * merges into whatever pattern was last selected somewhere else is how
     * people lose work they did not know was at risk.
     */
    int targetPattern = -1;
    char newPatternName[48] = {};

    // Where in the pattern the take starts.
    enum class Placement : int { PatternStart = 0, Playhead, Bar };
    int placement = static_cast<int>(Placement::PatternStart);
    int placementBar = 1;      // 1-based, as bars are shown everywhere else

    // Renaming the pattern being written into, from here, so the take can be
    // labelled without going to find another panel.
    char renameBuffer[48] = {};
    bool renaming = false;


    // Live tracking.
    LiveVoiceTracker tracker;
    bool armed = false;

    // A short history of the level, for the meter trace.
    std::array<float, 128> levelHistory{};
    int levelCursor = 0;
};


/*
 * Shift a take to where it should start, and say how long it needs.
 *
 * Separate from the UI so the arithmetic can be tested: an off-by-one on
 * the bar number puts a carefully sung part one bar out, which is the kind
 * of thing that is obvious in a test and maddening in a session.
 */
inline void placeNotesAt(std::vector<Note>& notes, float startBeat) {
    if (startBeat <= 0.0f) return;
    for (Note& note : notes) note.startTime += startBeat;
}

// The beat a take should start on, given the placement choice.
inline float placementStartBeat(int placement, int bar, float playheadBeat,
                                int beatsPerMeasure) {
    switch (static_cast<VoicePanelState::Placement>(placement)) {
        case VoicePanelState::Placement::Playhead:
            return std::max(0.0f, playheadBeat);
        case VoicePanelState::Placement::Bar:
            // Bars are 1-based in the UI and 0-based in beats, which is
            // exactly the sort of thing that silently puts a part one bar
            // out if it is done at the call site each time.
            return std::max(0, bar - 1) * float(std::max(1, beatsPerMeasure));
        default:
            return 0.0f;
    }
}

// How long a pattern has to be to hold these notes without clipping the end.
inline int patternLengthFor(const std::vector<Note>& notes, int existingLength) {
    float furthest = 0.0f;
    for (const Note& note : notes) {
        furthest = std::max(furthest, note.startTime + note.duration);
    }
    return std::max(existingLength, static_cast<int>(std::ceil(furthest)));
}

// ============================================================================
// Turning DetectedNote (seconds) into Note (beats)
// ============================================================================
/*
 * The offline detector reports seconds; a pattern holds beats. The
 * conversion goes through the tempo map rather than a single bpm, so a take
 * recorded over a tempo change lands where it was played rather than
 * drifting further out the longer it runs.
 *
 * Free function taking a map so it can be tested without a Project.
 */
inline std::vector<Note> detectedToNotes(const std::vector<DetectedNote>& detected,
                                         const TempoMap& map, float baseBpm,
                                         int beatsPerMeasure,
                                         const VoiceToNotesOptions& options) {
    std::vector<LiveHit> hits;
    hits.reserve(detected.size());
    for (const DetectedNote& note : detected) {
        LiveHit hit;
        hit.timeSeconds = note.startTime;
        hit.midiNote = note.noteNumber;
        hit.velocity = note.velocity;
        hit.isDrum = note.isDrum;
        hit.drumType = note.drumType;
        hits.push_back(hit);
    }

    std::vector<Note> notes = hitsToNotes(hits, map, baseBpm, beatsPerMeasure, options);

    // The offline detector knows how long each note actually lasted, which
    // the live path has to guess at from the gap to the next hit. Prefer the
    // measured duration where there is one.
    for (size_t i = 0; i < notes.size() && i < detected.size(); ++i) {
        if (detected[i].isDrum) continue;
        const float bpm = map.bpmAtBeat(notes[i].startTime, baseBpm);
        const float beats = detected[i].duration * bpm / 60.0f;
        if (beats > options.minDurationBeats) {
            notes[i].duration = snapDuration(beats, options.snap, beatsPerMeasure);
        }
    }
    return notes;
}

} // namespace ChiptuneTracker
