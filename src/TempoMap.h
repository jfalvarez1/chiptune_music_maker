#pragma once

/*
 * ChiptuneTracker - Tempo and meter map, markers and regions
 *
 * The project had one `float bpm` and one `int beatsPerMeasure` for the whole
 * song. That is fine for a loop and wrong for a piece: a chiptune with a slow
 * intro, a double-time B section or a ritardando into the last bar could not
 * be written at all, and neither could anything that changes from 4/4 to 6/8.
 *
 * Design notes, in the order they matter:
 *
 * 1. The audio thread reads this every sample. So it is fixed-capacity POD -
 *    no vector, no string, no allocation, and nothing that can reallocate
 *    under a reader. Markers and regions DO carry names, and they are
 *    deliberately kept in a separate, UI-only container for that reason: the
 *    mixer never looks at them.
 *
 * 2. Beat-to-seconds is a piecewise integral, not a division. With one tempo,
 *    seconds = beats * 60 / bpm. With a tempo change at beat 16, the seconds
 *    before it and after it run at different rates, and anything that divides
 *    once by a single bpm - the arrangement's audio clips, the WAV renderer's
 *    length estimate, MIDI export's tick conversion - drifts further out of
 *    place the longer the song runs. Every one of those paths goes through
 *    beatToSeconds() now.
 *
 * 3. Bars are counted through the meter map, not by dividing by four. Snap
 *    to Bar, the arrangement ruler and MIDI's time-signature events all read
 *    the same barAt()/beatOfBar() pair, so a 6/8 section snaps to six eighths
 *    rather than to whatever the project's global number happens to be.
 *
 * ImGui-free and Project-free, so all of it is testable headlessly.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// The entries
// ============================================================================

// A tempo change takes effect AT its beat and holds until the next one.
// There is no ramp: a linear accelerando is a different feature with a
// different UI, and pretending a step is a ramp would put the playhead in
// the wrong place rather than merely sounding blunt.
struct TempoChange {
    float beat = 0.0f;
    float bpm = 120.0f;
};

struct MeterChange {
    float beat = 0.0f;
    int numerator = 4;      // beats per bar, in units of the denominator
    int denominator = 4;    // 4 = quarter, 8 = eighth
};

// A marker names a point. A region names a span. They are separate types
// rather than one with an optional length because they are used differently:
// you jump to a marker and you loop or select a region.
struct Marker {
    float beat = 0.0f;
    std::string name;
    uint32_t color = 0xFFCCCCCCu;
};

struct Region {
    float startBeat = 0.0f;
    float endBeat = 4.0f;
    std::string name;
    uint32_t color = 0xFF4488FFu;

    float length() const { return endBeat - startBeat; }
    bool contains(float beat) const {
        return beat >= startBeat && beat < endBeat;
    }
};

// ============================================================================
// TempoMap
// ============================================================================
/*
 * Capacities are fixed and generous. 64 tempo changes is more than any
 * chiptune has ever needed; the point of the cap is that the array cannot
 * move while the audio thread is reading it, not that 64 is a musical limit.
 */
class TempoMap {
public:
    static constexpr int MAX_TEMPO_CHANGES = 64;
    static constexpr int MAX_METER_CHANGES = 32;

    // Bounds every entry is clamped to. A bpm of zero would divide by zero in
    // the beat advance and hang the playhead; a negative one would run the
    // song backwards.
    static constexpr float MIN_BPM = 20.0f;
    static constexpr float MAX_BPM = 400.0f;

    TempoMap() = default;

    // ---- Content ---------------------------------------------------------

    int tempoCount() const { return m_tempoCount; }
    int meterCount() const { return m_meterCount; }

    const TempoChange& tempoAt(int index) const {
        return m_tempos[static_cast<size_t>(std::clamp(index, 0, MAX_TEMPO_CHANGES - 1))];
    }
    const MeterChange& meterAt(int index) const {
        return m_meters[static_cast<size_t>(std::clamp(index, 0, MAX_METER_CHANGES - 1))];
    }

    // True when the song runs at one tempo and one meter throughout, which is
    // every project written before this existed. Several paths take a faster
    // route in that case, and more importantly the file format writes nothing
    // at all - so an unchanged project's .ctp is byte-identical to before.
    bool isFlat() const { return m_tempoCount == 0 && m_meterCount == 0; }

    void clear() {
        m_tempoCount = 0;
        m_meterCount = 0;
    }

    /*
     * Insert or replace a tempo change.
     *
     * Entries are kept sorted by beat, and two changes at the same beat
     * collapse into one: the later assignment wins. Without that, a user who
     * drags a change onto an existing one gets two entries at the same
     * position and the lookup returns whichever happens to be second, which
     * is a bug you can only find by ear.
     */
    bool setTempo(float beat, float bpm) {
        if (!std::isfinite(beat) || !std::isfinite(bpm)) return false;
        beat = std::max(0.0f, beat);
        bpm = std::clamp(bpm, MIN_BPM, MAX_BPM);

        const int existing = exactTempoIndex(beat);
        if (existing >= 0) {
            m_tempos[static_cast<size_t>(existing)].bpm = bpm;
            return true;
        }
        if (m_tempoCount >= MAX_TEMPO_CHANGES) return false;

        int insert = 0;
        while (insert < m_tempoCount && m_tempos[static_cast<size_t>(insert)].beat < beat) {
            ++insert;
        }
        for (int i = m_tempoCount; i > insert; --i) {
            m_tempos[static_cast<size_t>(i)] = m_tempos[static_cast<size_t>(i - 1)];
        }
        m_tempos[static_cast<size_t>(insert)] = TempoChange{beat, bpm};
        ++m_tempoCount;
        return true;
    }

    bool setMeter(float beat, int numerator, int denominator) {
        if (!std::isfinite(beat)) return false;
        beat = std::max(0.0f, beat);
        numerator = std::clamp(numerator, 1, 32);
        // Powers of two only. A 4/5 bar is not a thing, and allowing one
        // would put a non-representable value into the MIDI time signature
        // event, which stores the denominator as its base-2 logarithm.
        denominator = nearestPowerOfTwo(denominator);

        const int existing = exactMeterIndex(beat);
        if (existing >= 0) {
            m_meters[static_cast<size_t>(existing)].numerator = numerator;
            m_meters[static_cast<size_t>(existing)].denominator = denominator;
            return true;
        }
        if (m_meterCount >= MAX_METER_CHANGES) return false;

        int insert = 0;
        while (insert < m_meterCount && m_meters[static_cast<size_t>(insert)].beat < beat) {
            ++insert;
        }
        for (int i = m_meterCount; i > insert; --i) {
            m_meters[static_cast<size_t>(i)] = m_meters[static_cast<size_t>(i - 1)];
        }
        m_meters[static_cast<size_t>(insert)] = MeterChange{beat, numerator, denominator};
        ++m_meterCount;
        return true;
    }

    void removeTempoAt(int index) {
        if (index < 0 || index >= m_tempoCount) return;
        for (int i = index; i + 1 < m_tempoCount; ++i) {
            m_tempos[static_cast<size_t>(i)] = m_tempos[static_cast<size_t>(i + 1)];
        }
        --m_tempoCount;
    }

    void removeMeterAt(int index) {
        if (index < 0 || index >= m_meterCount) return;
        for (int i = index; i + 1 < m_meterCount; ++i) {
            m_meters[static_cast<size_t>(i)] = m_meters[static_cast<size_t>(i + 1)];
        }
        --m_meterCount;
    }

    // ---- Lookup ----------------------------------------------------------

    /*
     * The tempo in force at a beat.
     *
     * `baseBpm` is the project's own bpm, which applies from beat 0 until the
     * first change. Keeping it a parameter rather than an entry means a
     * project with no changes needs no entries at all, so the existing bpm
     * control keeps working untouched and the file gains nothing.
     */
    float bpmAtBeat(float beat, float baseBpm) const {
        float bpm = std::clamp(baseBpm, MIN_BPM, MAX_BPM);
        if (!std::isfinite(beat)) return bpm;

        for (int i = 0; i < m_tempoCount; ++i) {
            if (m_tempos[static_cast<size_t>(i)].beat > beat) break;
            bpm = m_tempos[static_cast<size_t>(i)].bpm;
        }
        return bpm;
    }

    MeterChange meterAtBeat(float beat, int baseNumerator, int baseDenominator = 4) const {
        MeterChange meter;
        meter.beat = 0.0f;
        meter.numerator = std::clamp(baseNumerator, 1, 32);
        meter.denominator = nearestPowerOfTwo(baseDenominator);
        if (!std::isfinite(beat)) return meter;

        for (int i = 0; i < m_meterCount; ++i) {
            if (m_meters[static_cast<size_t>(i)].beat > beat) break;
            meter = m_meters[static_cast<size_t>(i)];
        }
        return meter;
    }

    // How many engine beats one bar spans under the meter in force.
    //
    // The engine counts quarter notes, so 6/8 is six eighths = three beats,
    // not six. Getting this wrong makes Snap-to-Bar land on a different
    // place than the bar line the ruler drew, which reads as the grid being
    // broken rather than as a units mistake.
    static float barLengthBeats(const MeterChange& meter) {
        const int den = (meter.denominator > 0) ? meter.denominator : 4;
        const float beats = static_cast<float>(meter.numerator) * 4.0f /
                            static_cast<float>(den);
        return (beats > 0.0f) ? beats : 4.0f;
    }

    float barLengthAtBeat(float beat, int baseNumerator, int baseDenominator = 4) const {
        return barLengthBeats(meterAtBeat(beat, baseNumerator, baseDenominator));
    }

    /*
     * Where a beat falls in seconds.
     *
     * Piecewise: each tempo segment contributes its own beats * 60 / bpm, and
     * the segments are summed. A single division by the project bpm is only
     * correct when the tempo never changes, and every path that used to do
     * one - the audio clip mixer, the render length estimate, MIDI ticks -
     * drifts progressively worse as the song runs.
     */
    float beatToSeconds(float beat, float baseBpm) const {
        if (!std::isfinite(beat) || beat <= 0.0f) return 0.0f;

        float seconds = 0.0f;
        float cursor = 0.0f;
        float bpm = std::clamp(baseBpm, MIN_BPM, MAX_BPM);

        for (int i = 0; i < m_tempoCount; ++i) {
            const TempoChange& change = m_tempos[static_cast<size_t>(i)];
            if (change.beat >= beat) break;
            if (change.beat > cursor) {
                seconds += (change.beat - cursor) * 60.0f / bpm;
                cursor = change.beat;
            }
            bpm = change.bpm;
        }
        seconds += (beat - cursor) * 60.0f / bpm;
        return seconds;
    }

    // The inverse. Used by anything that knows a wall-clock position and needs
    // a musical one - the transport's time display, and seeking by seconds.
    float secondsToBeat(float seconds, float baseBpm) const {
        if (!std::isfinite(seconds) || seconds <= 0.0f) return 0.0f;

        float remaining = seconds;
        float cursor = 0.0f;
        float bpm = std::clamp(baseBpm, MIN_BPM, MAX_BPM);

        for (int i = 0; i < m_tempoCount; ++i) {
            const TempoChange& change = m_tempos[static_cast<size_t>(i)];
            if (change.beat <= cursor) { bpm = change.bpm; continue; }

            const float segmentSeconds = (change.beat - cursor) * 60.0f / bpm;
            if (segmentSeconds >= remaining) {
                return cursor + remaining * bpm / 60.0f;
            }
            remaining -= segmentSeconds;
            cursor = change.beat;
            bpm = change.bpm;
        }
        return cursor + remaining * bpm / 60.0f;
    }

    /*
     * Which bar a beat is in, zero-based, counting through every meter change.
     *
     * Not beat / beatsPerMeasure: a song that starts in 4/4 and moves to 3/4
     * at bar 8 has bar 9 starting at beat 32, then 35, then 38, and dividing
     * would put every later bar line in the wrong place.
     */
    int barAtBeat(float beat, int baseNumerator, int baseDenominator = 4) const {
        if (!std::isfinite(beat) || beat <= 0.0f) return 0;

        int bars = 0;
        float cursor = 0.0f;
        MeterChange meter;
        meter.numerator = std::clamp(baseNumerator, 1, 32);
        meter.denominator = nearestPowerOfTwo(baseDenominator);

        for (int i = 0; i < m_meterCount; ++i) {
            const MeterChange& change = m_meters[static_cast<size_t>(i)];
            if (change.beat >= beat) break;
            if (change.beat > cursor) {
                const float span = barLengthBeats(meter);
                bars += static_cast<int>((change.beat - cursor) / span);
                cursor = change.beat;
            }
            meter = change;
        }
        bars += static_cast<int>((beat - cursor) / barLengthBeats(meter));
        return bars;
    }

    // Where a bar line falls. The inverse of barAtBeat, and what the ruler
    // and Snap-to-Bar both walk.
    float beatOfBar(int bar, int baseNumerator, int baseDenominator = 4) const {
        if (bar <= 0) return 0.0f;

        int barsDone = 0;
        float cursor = 0.0f;
        MeterChange meter;
        meter.numerator = std::clamp(baseNumerator, 1, 32);
        meter.denominator = nearestPowerOfTwo(baseDenominator);

        for (int i = 0; i < m_meterCount; ++i) {
            const MeterChange& change = m_meters[static_cast<size_t>(i)];
            if (change.beat <= cursor) { meter = change; continue; }

            const float span = barLengthBeats(meter);
            const int barsHere = static_cast<int>((change.beat - cursor) / span);
            if (barsDone + barsHere >= bar) {
                return cursor + static_cast<float>(bar - barsDone) * span;
            }
            barsDone += barsHere;
            cursor += static_cast<float>(barsHere) * span;
            meter = change;
        }
        return cursor + static_cast<float>(bar - barsDone) * barLengthBeats(meter);
    }

    // The start of the bar containing a beat. This is what Snap-to-Bar needs:
    // dividing by a bar length is only right when every bar before it was the
    // same length.
    float barStartAtBeat(float beat, int baseNumerator, int baseDenominator = 4) const {
        return beatOfBar(barAtBeat(beat, baseNumerator, baseDenominator),
                         baseNumerator, baseDenominator);
    }

private:
    static int nearestPowerOfTwo(int value) {
        static const int allowed[] = {1, 2, 4, 8, 16, 32};
        int best = 4;
        int bestDistance = 1 << 30;
        for (int candidate : allowed) {
            const int distance = std::abs(candidate - value);
            if (distance < bestDistance) { bestDistance = distance; best = candidate; }
        }
        return best;
    }

    int exactTempoIndex(float beat) const {
        for (int i = 0; i < m_tempoCount; ++i) {
            if (std::fabs(m_tempos[static_cast<size_t>(i)].beat - beat) < 1e-4f) return i;
        }
        return -1;
    }

    int exactMeterIndex(float beat) const {
        for (int i = 0; i < m_meterCount; ++i) {
            if (std::fabs(m_meters[static_cast<size_t>(i)].beat - beat) < 1e-4f) return i;
        }
        return -1;
    }

    std::array<TempoChange, MAX_TEMPO_CHANGES> m_tempos{};
    std::array<MeterChange, MAX_METER_CHANGES> m_meters{};
    int m_tempoCount = 0;
    int m_meterCount = 0;
};

// ============================================================================
// Markers and regions
// ============================================================================
/*
 * These carry names, so they are std::string and std::vector and live only on
 * the UI side. The audio thread has no reason to read them: jumping to a
 * marker is a transport command the UI issues, not something the mixer
 * decides. Keeping them out of the audio-thread structures is what lets them
 * have names at all.
 */
inline constexpr int MAX_MARKERS = 128;
inline constexpr int MAX_REGIONS = 64;

// The marker at or before a beat; -1 when the beat is before the first one.
inline int markerAtOrBefore(const std::vector<Marker>& markers, float beat) {
    int found = -1;
    for (size_t i = 0; i < markers.size(); ++i) {
        if (markers[i].beat <= beat + 1e-4f) found = static_cast<int>(i);
    }
    return found;
}

// The next marker strictly after a beat; -1 when there is none.
inline int markerAfter(const std::vector<Marker>& markers, float beat) {
    for (size_t i = 0; i < markers.size(); ++i) {
        if (markers[i].beat > beat + 1e-4f) return static_cast<int>(i);
    }
    return -1;
}

// Markers are kept sorted so "next" and "previous" are a step through the
// list rather than a search, and so the ruler draws them in order.
inline void sortMarkers(std::vector<Marker>& markers) {
    std::stable_sort(markers.begin(), markers.end(),
                     [](const Marker& a, const Marker& b) { return a.beat < b.beat; });
}

inline void sortRegions(std::vector<Region>& regions) {
    std::stable_sort(regions.begin(), regions.end(),
                     [](const Region& a, const Region& b) { return a.startBeat < b.startBeat; });
}

// The region containing a beat, innermost first: regions may nest (a chorus
// inside a section), and the shorter one is the one the user means.
inline int regionAtBeat(const std::vector<Region>& regions, float beat) {
    int found = -1;
    float bestLength = 0.0f;
    for (size_t i = 0; i < regions.size(); ++i) {
        if (!regions[i].contains(beat)) continue;
        const float length = regions[i].length();
        if (found < 0 || length < bestLength) {
            found = static_cast<int>(i);
            bestLength = length;
        }
    }
    return found;
}

} // namespace ChiptuneTracker
