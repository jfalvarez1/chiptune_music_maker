#pragma once

/*
 * ChiptuneTracker - Real-time voice and beatbox tracking
 *
 * Sing a line or beatbox a groove and see the note or the drum hit as you
 * make it, then drop what you played into the pattern.
 *
 * Everything here runs on the UI thread, pulling from the lock-free ring in
 * VoiceCapture.h. Not one line of it belongs in the audio callback, and the
 * split is the point: the callback copies floats and updates two atomics,
 * and every FFT, autocorrelation and classification happens over here where
 * a stall costs a dropped frame rather than a dropped buffer.
 *
 * The methods, and why each one:
 *
 * ONSET - spectral flux with an adaptive median threshold (Bock & Widmer,
 * DAFx-13). Flux is the sum of positive changes in magnitude between
 * consecutive frames: it fires on the attack of a note or a drum, which is
 * what a person hears as "the hit". A plain energy threshold cannot tell a
 * new note from the same note getting louder, and a beatboxer's hi-hat is
 * quieter than their kick, so a fixed threshold would either miss the hats
 * or double-trigger the kicks. The threshold follows a running median of
 * recent flux, so it adapts to how loudly someone happens to be singing.
 *
 * PITCH - YIN's difference function with cumulative mean normalisation
 * (de Cheveigne & Kawahara, JASA 2002), reusing the implementation already
 * in AudioAnalyzer. Autocorrelation alone picks the octave below about as
 * often as the right note on a voice; the cumulative normalisation is
 * precisely the step that fixes it.
 *
 * DRUMS - spectral centroid, zero-crossing rate and low-band energy ratio,
 * the same three features AudioAnalyzer::analyzeDrums already uses offline,
 * so a beatbox pattern classifies the same way live as it does from a
 * recording. A kick is low centroid and high low-band energy; a hat is high
 * centroid and high zero-crossing rate; a snare sits between them with
 * broadband energy.
 *
 * LATENCY - the analysis hop is 256 samples (5.3 ms at 48 kHz) over a 1024
 * sample window (21 ms). A pitch is reported once two consecutive hops agree,
 * which costs one more hop. Worst case is therefore about 27 ms from the
 * sound to the readout, inside the 30 ms budget, and the window cannot be
 * shortened much further: 1024 samples at 48 kHz is 21 ms, and a 21 ms
 * window can barely see two periods of a low male voice at 85 Hz.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

#include "VoiceCapture.h"
#include "FFT.h"
#include "AudioAnalyzer.h"   // YIN and freqToMidi, shared with the offline path
#include "Types.h"
#include "Snap.h"
#include "TempoMap.h"

namespace ChiptuneTracker {

// ============================================================================
// What the tracker reports
// ============================================================================
enum class LiveVoiceMode : uint8_t {
    Melodic = 0,    // sing a line
    Drums           // beatbox a groove
};

struct LiveHit {
    float timeSeconds = 0.0f;   // when, on the tracker's own clock
    int midiNote = 60;          // melodic only
    float frequency = 0.0f;
    float velocity = 0.8f;
    bool isDrum = false;
    int drumType = 0;           // 0 kick, 1 snare, 2 hat - as AudioAnalyzer uses
    float confidence = 0.0f;
};

// ============================================================================
// The tracker
// ============================================================================
class LiveVoiceTracker {
public:
    // 1024 at 48 kHz is 21 ms - about two periods of an 85 Hz voice, which is
    // the lowest a pitch detector can see at all. Shorter would be faster and
    // would stop working on baritones.
    static constexpr int WINDOW = 1024;
    static constexpr int HOP = 256;
    static constexpr int SPECTRUM_BINS = WINDOW / 2;

    // How many past flux values the adaptive threshold looks at. At a 5.3 ms
    // hop this is about a third of a second, which is long enough to span a
    // couple of hits and short enough to follow someone getting louder.
    static constexpr int FLUX_HISTORY = 64;

    // An onset cannot retrigger within this. Below about 50 ms a single drum
    // hit's decay fires a second onset, so a beatboxed kick becomes two.
    static constexpr float MIN_ONSET_GAP_SECONDS = 0.05f;

    void setSampleRate(int rate) {
        if (rate > 0 && rate != m_sampleRate) {
            m_sampleRate = rate;
            reset();
        }
    }
    int sampleRate() const { return m_sampleRate; }

    void reset() {
        m_window.assign(WINDOW, 0.0f);
        m_filled = 0;
        m_previousSpectrum.assign(SPECTRUM_BINS, 0.0f);
        m_fluxHistory.clear();
        m_hits.clear();
        m_elapsedSamples = 0;
        m_lastOnsetSeconds = -1.0f;
        m_currentNote = -1;
        m_candidateNote = -1;
        m_currentFrequency = 0.0f;
        m_stableHops = 0;
        m_stableFrequencySum = 0.0f;
        m_lastEmittedNote = -1;
        m_currentLevel = 0.0f;
        m_previousLevel = 0.0f;
        m_levelFollower = 0.0f;
        m_lastFlux = 0.0f;
        m_lastThreshold = 0.0f;
        m_armed = true;
    }

    void setMode(LiveVoiceMode mode) { m_mode = mode; }
    LiveVoiceMode mode() const { return m_mode; }

    // Sensitivity, 0..1. Higher means a lower threshold and more onsets.
    void setSensitivity(float value) {
        m_sensitivity = std::clamp(value, 0.0f, 1.0f);
    }
    float sensitivity() const { return m_sensitivity; }

    /*
     * Feed everything the ring has. Call this once a frame from the UI.
     *
     * Whole hops only: a partial hop is left in the window and picked up next
     * frame, so the analysis grid stays regular no matter how the audio
     * device happens to chop up its blocks.
     */
    void process(const float* samples, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            m_window[static_cast<size_t>(m_filled++)] = samples[i];
            if (m_filled < WINDOW) continue;

            analyzeWindow();

            // Slide by one hop. A memmove of 1024 floats per hop is nothing
            // next to the FFT that follows it.
            std::copy(m_window.begin() + HOP, m_window.end(), m_window.begin());
            m_filled = WINDOW - HOP;
            m_elapsedSamples += HOP;
        }
    }

    // ---- Readouts ---------------------------------------------------------

    // The note being sung right now, or -1 for silence. This is what the
    // panel shows live.
    int currentNote() const { return m_currentNote; }
    float currentFrequency() const { return m_currentFrequency; }
    float currentLevel() const { return m_currentLevel; }
    float lastFlux() const { return m_lastFlux; }
    float lastThreshold() const { return m_lastThreshold; }
    float elapsedSeconds() const {
        return static_cast<float>(m_elapsedSamples) / static_cast<float>(m_sampleRate);
    }

    const std::vector<LiveHit>& hits() const { return m_hits; }
    void clearHits() { m_hits.clear(); }

private:
    // ---- One window -------------------------------------------------------
    void analyzeWindow() {
        // Level first: silence is the common case and there is no reason to
        // run an FFT on it.
        float sumSquares = 0.0f;
        for (int i = 0; i < WINDOW; ++i) {
            sumSquares += m_window[static_cast<size_t>(i)] *
                          m_window[static_cast<size_t>(i)];
        }
        m_previousLevel = m_currentLevel;
        m_currentLevel = std::sqrt(sumSquares / static_cast<float>(WINDOW));

        // A one-pole follower over the level, updated before the onset test
        // so an attack is compared against where the signal HAS been rather
        // than where it is. The coefficient is a compromise: fast enough
        // that two hits 100 ms apart both register, slow enough that a
        // decaying tone's ripple never looks like a new attack.
        m_levelFollower += (m_currentLevel - m_levelFollower) * 0.25f;

        const float silenceFloor = 0.004f;
        if (m_currentLevel < silenceFloor) {
            m_currentNote = -1;
            m_candidateNote = -1;
            m_currentFrequency = 0.0f;
            m_stableHops = 0;
            m_stableFrequencySum = 0.0f;
            // Silence ends the note. Without this, humming the same pitch
            // either side of a rest produces one note, not two.
            m_lastEmittedNote = -1;
            std::fill(m_previousSpectrum.begin(), m_previousSpectrum.end(), 0.0f);

            // Silence still counts as a flux measurement, and it has to.
            // Skipping it meant the history was empty when the first hit
            // arrived, so the eight hops of that hit were discarded while it
            // filled - and then those eight huge values became the median,
            // which put the threshold above everything that followed. A
            // beatboxed kick after a count-in was detected as nothing at all.
            pushFlux(0.0f);
            m_armed = true;
            return;
        }

        computeSpectrum();
        const float flux = spectralFlux();
        const bool onset = isOnset(flux);

        if (m_mode == LiveVoiceMode::Melodic) {
            trackPitch(onset);
        } else if (onset) {
            emitDrumHit();
        }
    }

    void computeSpectrum() {
        if (m_plan.size() != WINDOW) {
            m_plan.resize(WINDOW);
            m_hann.resize(WINDOW);
            for (int i = 0; i < WINDOW; ++i) {
                m_hann[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
                    6.28318530718f * static_cast<float>(i) /
                    static_cast<float>(WINDOW - 1)));
            }
            m_real.resize(WINDOW);
            m_imag.resize(WINDOW);
            m_spectrum.resize(SPECTRUM_BINS);
        }

        // Hann window, precomputed. Without a window the rectangular one's
        // sidelobes leak across the spectrum and the flux ends up measuring
        // the leak rather than the note.
        for (int i = 0; i < WINDOW; ++i) {
            m_real[static_cast<size_t>(i)] =
                m_window[static_cast<size_t>(i)] * m_hann[static_cast<size_t>(i)];
            m_imag[static_cast<size_t>(i)] = 0.0f;
        }

        m_plan.transform(m_real.data(), m_imag.data());

        for (int bin = 0; bin < SPECTRUM_BINS; ++bin) {
            const float re = m_real[static_cast<size_t>(bin)];
            const float im = m_imag[static_cast<size_t>(bin)];
            m_spectrum[static_cast<size_t>(bin)] = std::sqrt(re * re + im * im);
        }
    }

    // Sum of POSITIVE magnitude changes only. A note ending is not an onset,
    // and counting the decay would fire a second trigger on every hit.
    float spectralFlux() {
        float flux = 0.0f;
        for (int bin = 0; bin < SPECTRUM_BINS; ++bin) {
            const float rise = m_spectrum[static_cast<size_t>(bin)] -
                               m_previousSpectrum[static_cast<size_t>(bin)];
            if (rise > 0.0f) flux += rise;
        }
        m_previousSpectrum = m_spectrum;
        m_lastFlux = flux;
        return flux;
    }

    /*
     * Adaptive threshold: a multiple of the running median of recent flux.
     *
     * The median rather than the mean because a single loud hit drags a mean
     * up enough to mask the next few quieter ones - which on a beatbox
     * pattern means the hats after a kick go missing.
     */
    void pushFlux(float flux) {
        m_fluxHistory.push_back(flux);
        if (static_cast<int>(m_fluxHistory.size()) > FLUX_HISTORY) {
            m_fluxHistory.pop_front();
        }
    }

    bool isOnset(float flux) {
        pushFlux(flux);
        if (m_fluxHistory.size() < 4) return false;

        // Reused rather than built fresh: this runs about 190 times a second,
        // and a vector allocated and freed at that rate shows up as frame
        // jitter even though none of it is on the audio thread.
        m_sortScratch.assign(m_fluxHistory.begin(), m_fluxHistory.end());
        const size_t middle = m_sortScratch.size() / 2;
        std::nth_element(m_sortScratch.begin(),
                         m_sortScratch.begin() + static_cast<long>(middle),
                         m_sortScratch.end());
        const float median = m_sortScratch[middle];

        // Sensitivity 0 -> 3.0x median, 1 -> 1.2x. Plus a small absolute
        // floor so that near-silence, where the median is almost zero, does
        // not trigger on noise.
        const float multiplier = 3.0f - 1.8f * m_sensitivity;
        m_lastThreshold = std::max(median * multiplier, 0.35f);

        /*
         * Hysteresis: the detector has to re-arm before it can fire again.
         *
         * The retrigger gap alone was not enough. A kick at 60 Hz is barely
         * one and a quarter periods of a 1024-sample window, so consecutive
         * windows see quite different spectra and the flux keeps spiking
         * through the whole decay - one hit came out as six. Requiring flux
         * to fall back well under the threshold in between turns that into
         * the one onset it actually was.
         */
        if (!m_armed) {
            if (flux < m_lastThreshold * 0.5f) m_armed = true;
            return false;
        }
        if (flux < m_lastThreshold) return false;

        /*
         * The envelope has to jump, not merely tick up.
         *
         * Spectral flux alone is not enough at low frequencies. A 60 Hz kick
         * is barely one and a quarter periods of a 1024-sample window, so
         * consecutive windows genuinely see different spectra and the flux
         * spikes right through the decay - one hit came out as seven.
         *
         * Comparing against the PREVIOUS window does not fix it either, for
         * the same reason: 21 ms of a 60 Hz tone is 1.28 periods, so the
         * windowed RMS oscillates with the phase and some windows during a
         * decay are louder than the one before. The comparison has to be
         * against a smoothed envelope, which rides through that ripple.
         *
         * An onset is an ATTACK: the level jumps well clear of where it has
         * been. A decay never does that, however lumpy it looks frame to
         * frame.
         */
        if (m_currentLevel < m_levelFollower * 1.35f) return false;

        const float now = elapsedSeconds();
        if (m_lastOnsetSeconds >= 0.0f &&
            now - m_lastOnsetSeconds < MIN_ONSET_GAP_SECONDS) {
            return false;
        }
        m_lastOnsetSeconds = now;
        m_armed = false;
        return true;
    }

    // ---- Melodic ----------------------------------------------------------
    /*
     * How long a pitch must hold before it is believed to be a note.
     *
     * At a 256-sample hop and 48 kHz this is 5.3 ms per hop, so six hops is
     * about 32 ms. That is long enough to sit out the slide between two
     * hummed notes - which takes 60-100 ms and passes through every
     * semitone on the way - and short enough that the note still lands close
     * to where it was sung.
     *
     * Every one of those passed-through semitones used to become a note the
     * user had to delete by hand. That is the single thing that makes people
     * abandon a voice-to-notes tool.
     */
    static constexpr int STABLE_HOPS = 9;

    void trackPitch(bool onset) {
        const float frequency = AudioAnalyzer::getPitchYIN(m_window, m_sampleRate);
        if (frequency <= 0.0f) {
            m_candidateNote = -1;
            m_stableHops = 0;
            return;
        }

        const int note = AudioAnalyzer::freqToMidi(frequency);
        if (note < 0 || note > 127) {
            m_candidateNote = -1;
            m_stableHops = 0;
            return;
        }

        /*
         * Two consecutive hops must agree before the READOUT changes.
         *
         * YIN reports the octave below on a single frame often enough that
         * without this the readout flickers between the note and its octave
         * while a perfectly steady note is being sung. One extra hop is
         * 5.3 ms, which is a cheap price for a readout that holds still.
         */
        if (note != m_candidateNote) {
            m_candidateNote = note;
            m_stableHops = 1;
            m_stableFrequencySum = frequency;
            if (!onset) return;
        } else {
            ++m_stableHops;
            m_stableFrequencySum += frequency;
        }

        m_currentNote = note;
        m_currentFrequency = frequency;

        /*
         * A hit is emitted once, when the pitch has held long enough to be
         * a note rather than a moment during a slide.
         *
         * An onset is allowed to emit immediately: a deliberately
         * re-articulated note is a real note even if the pitch has not
         * settled, and waiting on those would drop every repeated note in a
         * line sung on one pitch.
         */
        const bool settled = (m_stableHops == STABLE_HOPS);
        const bool restated = onset && (m_stableHops < STABLE_HOPS);
        if (!settled && !restated) return;

        // Not the frequency of this instant, but the average across the
        // hops that agreed - which is the note's stable portion, and is what
        // the pitch should have been read from all along.
        const float stableFrequency =
            m_stableFrequencySum / float(std::max(1, m_stableHops));

        // The note this actually settled on, which after averaging can
        // differ from the instantaneous reading at the edge of a bin.
        const int settledNote = AudioAnalyzer::freqToMidi(stableFrequency);
        if (settledNote < 0 || settledNote > 127) return;

        // The same note still sounding is not a new note. Without this a
        // held note re-emits every time an onset flickers underneath it.
        if (!restated && settledNote == m_lastEmittedNote) return;
        m_lastEmittedNote = settledNote;

        LiveHit hit;
        // Backdated to where the note actually started, rather than to the
        // moment it was confirmed - otherwise every note lands late by the
        // length of the confirmation window, and a whole part drags.
        hit.timeSeconds = std::max(0.0f,
            elapsedSeconds() - float(m_stableHops * HOP) / float(m_sampleRate));
        hit.midiNote = settledNote;
        hit.frequency = stableFrequency;
        // Velocity from how hard it was sung. The mapping is deliberately
        // compressed: a voice has nothing like the dynamic range of the
        // 0..1 the note format wants, and a linear map leaves everything
        // sung at a conversational level down at 0.1.
        hit.velocity = std::clamp(0.35f + m_currentLevel * 4.0f, 0.1f, 1.0f);
        hit.confidence = std::min(1.0f, float(m_stableHops) / float(STABLE_HOPS));
        appendHit(hit);
    }

    // ---- Drums ------------------------------------------------------------
    void emitDrumHit() {
        // The same three features analyzeDrums uses offline, so a beatboxed
        // groove classifies live the way it does from a recording.
        float weighted = 0.0f;
        float total = 0.0f;
        float lowEnergy = 0.0f;
        float midEnergy = 0.0f;
        const float binHz = static_cast<float>(m_sampleRate) / static_cast<float>(WINDOW);

        for (int bin = 1; bin < SPECTRUM_BINS; ++bin) {
            const float magnitude = m_spectrum[static_cast<size_t>(bin)];
            const float hz = static_cast<float>(bin) * binHz;
            weighted += hz * magnitude;
            total += magnitude;
            if (hz < 250.0f) lowEnergy += magnitude;
            else if (hz < 2000.0f) midEnergy += magnitude;
        }
        if (total <= 1e-6f) return;

        const float centroid = weighted / total;
        const float lowRatio = lowEnergy / total;
        // The band a snare lives in and a hi-hat does not. Centroid and
        // zero-crossing rate alone cannot separate the two: a snare is half
        // noise, so both of those land it in hi-hat territory. What actually
        // distinguishes them is that a snare has a body around 200 Hz to
        // 1 kHz and a hi-hat has almost nothing below 2 kHz.
        const float midRatio = midEnergy / total;

        int zeroCrossings = 0;
        for (int i = 1; i < WINDOW; ++i) {
            const float a = m_window[static_cast<size_t>(i - 1)];
            const float b = m_window[static_cast<size_t>(i)];
            if ((a >= 0.0f) != (b >= 0.0f)) ++zeroCrossings;
        }
        const float zcr = static_cast<float>(zeroCrossings) / static_cast<float>(WINDOW);

        LiveHit hit;
        hit.timeSeconds = elapsedSeconds();
        hit.isDrum = true;
        hit.velocity = std::clamp(0.4f + m_currentLevel * 4.0f, 0.15f, 1.0f);

        // Ordered by how distinctive each is. A kick is unmistakable - almost
        // all of its energy is under 250 Hz - so it is tested first and the
        // ambiguous middle falls to the snare, which is what a snare is.
        if (lowRatio > 0.55f && centroid < 900.0f) {
            hit.drumType = 0;                      // kick
            hit.confidence = lowRatio;
        } else if ((zcr > 0.16f || centroid > 4200.0f) &&
                   (lowRatio + midRatio) < 0.35f) {
            hit.drumType = 2;                      // hat
            hit.confidence = std::min(1.0f, zcr * 4.0f);
        } else {
            hit.drumType = 1;                      // snare
            hit.confidence = 0.4f + midRatio;
        }
        appendHit(hit);
    }

    void appendHit(const LiveHit& hit) {
        // Bounded: a session left running for an hour must not grow without
        // limit, and nobody is going to use the ten-thousandth hit.
        constexpr size_t MAX_HITS = 4096;
        if (m_hits.size() >= MAX_HITS) {
            m_hits.erase(m_hits.begin(),
                         m_hits.begin() + static_cast<long>(MAX_HITS / 4));
        }
        m_hits.push_back(hit);
    }

    // How many consecutive hops the candidate pitch has held, and the sum
    // of those readings - so the emitted pitch is the average over the
    // stable portion rather than one instant during a slide.
    int m_stableHops = 0;
    float m_stableFrequencySum = 0.0f;
    int m_lastEmittedNote = -1;

    int m_sampleRate = 48000;
    LiveVoiceMode m_mode = LiveVoiceMode::Melodic;
    float m_sensitivity = 0.5f;

    std::vector<float> m_window = std::vector<float>(WINDOW, 0.0f);
    int m_filled = 0;

    DSP::FFTPlan m_plan;
    std::vector<float> m_hann;
    std::vector<float> m_real, m_imag, m_spectrum;
    std::vector<float> m_previousSpectrum = std::vector<float>(SPECTRUM_BINS, 0.0f);
    std::deque<float> m_fluxHistory;
    std::vector<float> m_sortScratch;

    uint64_t m_elapsedSamples = 0;
    float m_lastOnsetSeconds = -1.0f;
    int m_currentNote = -1;
    int m_candidateNote = -1;
    float m_currentFrequency = 0.0f;
    float m_currentLevel = 0.0f;
    float m_previousLevel = 0.0f;
    float m_levelFollower = 0.0f;
    float m_lastFlux = 0.0f;
    float m_lastThreshold = 0.0f;

    // Whether the onset detector may fire. Cleared on each onset and set
    // again once flux has fallen back well below the threshold.
    bool m_armed = true;

    std::vector<LiveHit> m_hits;
};

// ============================================================================
// Turning what was played into notes in a pattern
// ============================================================================
/*
 * Quantisation, and why it is offered rather than imposed.
 *
 * A sung line that lands 30 ms early is not wrong, it is played; forcing it
 * onto the grid is a choice, and one a performer often does not want. So the
 * conversion takes a snap division and Off is a real option that keeps the
 * timing exactly as sung.
 *
 * Times run through the project's tempo map rather than a single bpm, so a
 * take recorded over a tempo change lands where it was actually played.
 */

// ============================================================================
// What a hum is meant to become
//
// A part is not just notes, it is notes for something. The role decides the
// register it belongs in, how long its notes are, and whether snapping it to
// a key makes sense - and those differ enough between a bass line and a lead
// that one set of defaults cannot serve both.
// ============================================================================
enum class VoiceRole : uint8_t {
    Lead = 0,     // a melody, up where a lead sits
    Bass,         // low, monophonic, longer notes
    Drums,        // beatboxed; pitch is a drum choice, not a note
    Chords,       // a hummed arpeggio, kept mid-register
};

inline const char* voiceRoleName(VoiceRole role) {
    switch (role) {
        case VoiceRole::Lead:   return "Lead";
        case VoiceRole::Bass:   return "Bass";
        case VoiceRole::Drums:  return "Drums";
        case VoiceRole::Chords: return "Chords";
        default:                return "?";
    }
}

/*
 * The middle of the register each role belongs in, as a MIDI note.
 *
 * People hum in a comfortable vocal range - very roughly C3 for a male
 * voice, and lower still when they are humming a bass line, because they
 * are already thinking low. A chiptune lead belongs around C5. Without a
 * correction the part lands two octaves below where it should and sounds
 * muddy and wrong, which is the most common complaint about every tool of
 * this kind.
 */
inline int voiceRoleCentre(VoiceRole role) {
    switch (role) {
        case VoiceRole::Bass:   return 40;   // E2
        case VoiceRole::Chords: return 60;   // C4
        case VoiceRole::Lead:   return 72;   // C5
        default:                return 60;
    }
}

/*
 * Move a whole part into its register, in octaves.
 *
 * By octaves and applied to the WHOLE part, for two reasons. Octaves keep
 * the pitch classes, so a part that was in key stays in key. Applying one
 * shift to everything keeps the melodic contour - folding each note into
 * the register independently would flatten a rising line into a jagged one,
 * which is a worse problem than the one being fixed.
 *
 * The median rather than the mean, so one stray octave error cannot drag
 * the whole part with it.
 */
inline int octaveShiftForRole(const std::vector<Note>& notes, VoiceRole role) {
    if (role == VoiceRole::Drums) return 0;   // a drum note's pitch is a choice of drum

    std::vector<int> pitches;
    pitches.reserve(notes.size());
    for (const Note& note : notes) pitches.push_back(note.pitch);
    if (pitches.empty()) return 0;

    std::nth_element(pitches.begin(), pitches.begin() + long(pitches.size() / 2),
                     pitches.end());
    const int median = pitches[pitches.size() / 2];

    const int centre = voiceRoleCentre(role);
    const int octaves = static_cast<int>(
        std::lround(double(centre - median) / 12.0));
    return octaves * 12;
}

struct VoiceToNotesOptions {
    SnapDivision snap = SnapDivision::Sixteenth;
    int transpose = 0;

    /*
     * What the part is for. Decides its register, and how long its notes
     * are held.
     */
    VoiceRole role = VoiceRole::Lead;

    /*
     * Whether to move the part into its role's register.
     *
     * On by default, because humming two octaves below where the part
     * belongs is what people actually do and the result otherwise sounds
     * wrong for a reason they cannot name. Off for anybody who wants the
     * notes exactly where they sang them.
     */
    bool placeInRegister = true;
    float minDurationBeats = 0.125f;
    bool useVelocity = true;

    // Drum mode: which oscillator each of the three classes becomes.
    OscillatorType kick = OscillatorType::Kick;
    OscillatorType snare = OscillatorType::Snare;
    OscillatorType hat = OscillatorType::HiHat;

    // Where drum hits sit on the keyboard. A drum oscillator ignores pitch,
    // but the note still has to have one, and putting all three on the same
    // key makes the piano roll unreadable.
    int kickPitch = 36;      // C2, the General MIDI kick
    int snarePitch = 38;
    int hatPitch = 42;
};

// Convert detected hits into notes, in beats, ready to append to a pattern.
//
// Free function taking a tempo map rather than a Project so it can be tested
// without building one.
inline std::vector<Note> hitsToNotes(const std::vector<LiveHit>& hits,
                                     const TempoMap& map, float baseBpm,
                                     int beatsPerMeasure,
                                     const VoiceToNotesOptions& options) {
    std::vector<Note> notes;
    notes.reserve(hits.size());

    for (size_t i = 0; i < hits.size(); ++i) {
        const LiveHit& hit = hits[i];

        float startBeat = map.secondsToBeat(hit.timeSeconds, baseBpm);
        startBeat = snapBeatNearestMapped(startBeat, options.snap, map, beatsPerMeasure);
        if (startBeat < 0.0f) startBeat = 0.0f;

        // A note runs until the next hit, so a sung line comes back as a
        // legato line rather than a row of clicks. The last one gets a beat.
        float endBeat = startBeat + 1.0f;
        if (i + 1 < hits.size()) {
            const float nextBeat = snapBeatNearestMapped(
                map.secondsToBeat(hits[i + 1].timeSeconds, baseBpm),
                options.snap, map, beatsPerMeasure);
            if (nextBeat > startBeat) endBeat = nextBeat;
        }

        Note note;
        note.startTime = startBeat;
        note.duration = std::max(options.minDurationBeats, endBeat - startBeat);
        note.velocity = options.useVelocity ? std::clamp(hit.velocity, 0.05f, 1.0f)
                                            : 0.8f;

        if (hit.isDrum) {
            // A drum hit is short by nature. Holding it until the next hit
            // would make every kick a whole note, and a drum oscillator that
            // is retriggered while still sounding chokes itself.
            note.duration = std::max(options.minDurationBeats, 0.125f);
            switch (hit.drumType) {
                case 0:
                    note.pitch = options.kickPitch;
                    note.oscillatorType = options.kick;
                    break;
                case 2:
                    note.pitch = options.hatPitch;
                    note.oscillatorType = options.hat;
                    break;
                default:
                    note.pitch = options.snarePitch;
                    note.oscillatorType = options.snare;
                    break;
            }
        } else {
            note.pitch = std::clamp(hit.midiNote + options.transpose, 0, 127);
        }

        notes.push_back(note);
    }

    /*
     * Into the register the role belongs in.
     *
     * After the notes are built, because it needs the median pitch of the
     * whole part - and before the duplicate pass, because shifting by whole
     * octaves cannot turn two different notes into the same one but the
     * duplicate check should still see the final pitches.
     */
    /*
     * An explicit transpose wins.
     *
     * Somebody who asked for -12 means twelve below where they sang, not
     * twelve below wherever the placement decided - so setting a transpose
     * turns the automatic placement off rather than fighting it. Two
     * features silently cancelling each other is worse than either.
     */
    if (options.placeInRegister && options.transpose == 0 && !notes.empty()) {
        const int shift = octaveShiftForRole(notes, options.role);
        if (shift != 0) {
            for (Note& note : notes) {
                // Clamped per note: a part spanning two octaves can have
                // its extremes fall off the keyboard when the middle is
                // placed correctly, and a clamped note is better than a
                // wrapped one.
                note.pitch = std::clamp(note.pitch + shift, 0, 127);
            }
        }
    }

    /*
     * A bass line is monophonic and its notes are held.
     *
     * A hummed bass part with sixteenth-length notes reads as a blip track
     * rather than a bass line; holding each note until the next is what
     * makes it sound like one.
     */
    if (options.role == VoiceRole::Bass) {
        for (size_t i = 0; i + 1 < notes.size(); ++i) {
            const float gap = notes[i + 1].startTime - notes[i].startTime;
            if (gap > 0.0f) notes[i].duration = gap;
        }
    }

    // Two hits that quantise onto the same beat with the same pitch are one
    // note played slightly unevenly, not two. Keeping both would stack two
    // voices on one key, which sounds like a phasing artefact.
    notes.erase(std::unique(notes.begin(), notes.end(),
                            [](const Note& a, const Note& b) {
                                return a.pitch == b.pitch &&
                                       std::fabs(a.startTime - b.startTime) < 1e-4f;
                            }),
                notes.end());
    return notes;
}

} // namespace ChiptuneTracker
