#pragma once

// ============================================================================
// Groove: how long each row lasts
//
// A tracker's timing is a list of tick counts, one per row, repeating. "6 6
// 6 6" is straight; "7 5 7 5" gives every other row an extra tick and takes
// one from the next, which is swing; "7 5 6 6" is a shuffle that only limps
// on the first half of the beat. Whole genres live in that list.
//
// THIS IS NOT THE SWING CONTROL THAT ALREADY EXISTS. Swing here is one
// number applied to every off-beat, which can express "7 5 7 5" and nothing
// else. A groove is a pattern, and the patterns people actually want -
// three-against-four, a limp that resolves every other bar, the specific
// unevenness of a particular drum machine - are lists rather than amounts.
//
// THE HARD PART IS THAT THIS ENGINE IS NOT ROW-BASED. It runs on floating
// point beats, which is why it can place a note anywhere and why audio clips
// and a tempo map work at all. So a groove here is a MAPPING on beat
// positions rather than a table the playback walks: given a straight beat,
// where does that beat actually fall.
//
// The mapping's one hard requirement is that it must not change the tempo. A
// groove that ran 2% fast would drift against every audio clip in the
// project and against anything the song is played alongside. So a full cycle
// of the list always maps onto exactly as many rows of straight time as it
// has entries, whatever the numbers in it are - which also means somebody
// can type "9 4 7 3" without having to make it average out.
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace ChiptuneTracker {

struct GroovePattern {
    // Sixteen is a bar of sixteenths. Past that a groove stops being a feel
    // and becomes a sequence, which is what patterns are for.
    static constexpr int MAX_STEPS = 16;

    // Ticks per row. The absolute values do not matter, only their ratios -
    // "7 5" and "14 10" are the same groove - because a cycle is normalised
    // onto the same span either way.
    std::array<int, MAX_STEPS> speeds{};

    // 0 means no groove: timing is straight and nothing here is consulted.
    int count = 0;

    // Which grid the list applies to. Four is one entry per sixteenth note,
    // which is what a tracker row usually is here.
    int rowsPerBeat = 4;

    bool active() const {
        if (count <= 0 || rowsPerBeat <= 0) return false;
        // A list whose entries are all equal is straight timing written the
        // long way, and doing the arithmetic for it would only introduce
        // rounding.
        for (int i = 1; i < std::min(count, MAX_STEPS); ++i) {
            if (speeds[static_cast<size_t>(i)] !=
                speeds[static_cast<size_t>(0)]) {
                return true;
            }
        }
        return false;
    }

    int totalTicks() const {
        int total = 0;
        for (int i = 0; i < std::min(count, MAX_STEPS); ++i) {
            total += std::max(1, speeds[static_cast<size_t>(i)]);
        }
        return total;
    }

    /*
     * Where a straight beat actually falls.
     *
     * Rows are the unit. Row r starts, in straight time, at r / rowsPerBeat;
     * under the groove it starts at the fraction of the cycle its ticks
     * account for, scaled back up so a whole cycle covers exactly `count`
     * rows. Anything between two rows is interpolated, which stretches and
     * compresses the space between hits rather than only moving the hits -
     * the difference matters for a note whose start does not land on a row.
     *
     * Negative beats pass through untouched. They only occur in the
     * arrangement's own arithmetic, never as a note position, and wrapping
     * them into a cycle would displace a clip that begins before bar one.
     */
    float displace(float beat) const {
        if (!active() || !(beat > 0.0f)) return beat;

        const int steps = std::min(count, MAX_STEPS);
        const int total = totalTicks();
        if (total <= 0) return beat;

        const float rows = beat * static_cast<float>(rowsPerBeat);
        const float rowFloor = std::floor(rows);
        const float within = rows - rowFloor;

        const long long row = static_cast<long long>(rowFloor);
        const long long cycle = row / steps;
        int index = static_cast<int>(row % steps);
        if (index < 0) index += steps;

        // Ticks elapsed inside this cycle before the current row.
        int elapsed = 0;
        for (int i = 0; i < index; ++i) {
            elapsed += std::max(1, speeds[static_cast<size_t>(i)]);
        }
        const int thisRow = std::max(1, speeds[static_cast<size_t>(index)]);

        // A cycle of `steps` rows is worth `total` ticks, so one tick is
        // steps/total of a row. That normalisation is what keeps the tempo.
        const float scale = static_cast<float>(steps) / static_cast<float>(total);
        const float displacedRow =
            static_cast<float>(cycle * steps) +
            (static_cast<float>(elapsed) + within * static_cast<float>(thisRow)) *
                scale;

        return displacedRow / static_cast<float>(rowsPerBeat);
    }

    void clear() {
        speeds.fill(6);
        count = 0;
        rowsPerBeat = 4;
    }
};

/*
 * Reading and writing a groove as text.
 *
 * "7 5" rather than a row of spin boxes, because a groove is a short list
 * that people already write down that way, copy from a forum post, and
 * compare at a glance.
 */
inline std::string grooveToText(const GroovePattern& groove) {
    std::string out;
    for (int i = 0; i < std::min(groove.count, GroovePattern::MAX_STEPS); ++i) {
        if (!out.empty()) out += ' ';
        out += std::to_string(std::max(1, groove.speeds[static_cast<size_t>(i)]));
    }
    return out;
}

/*
 * Parse a groove from text, taking whatever separators arrive.
 *
 * Spaces, commas, slashes and dashes all work, because a groove copied from
 * somewhere else arrives written in whatever way that place writes them.
 * Anything that is not a number ends the value rather than failing the
 * parse - a half-typed groove should still play as far as it makes sense.
 */
inline bool grooveFromText(const std::string& text, GroovePattern& out) {
    out.count = 0;
    out.speeds.fill(6);

    int value = 0;
    bool inNumber = false;
    bool any = false;

    auto commit = [&]() {
        if (!inNumber) return;
        if (out.count < GroovePattern::MAX_STEPS) {
            // A speed of zero would be a row that takes no time at all, and
            // the whole cycle would collapse onto one instant.
            out.speeds[static_cast<size_t>(out.count++)] = std::clamp(value, 1, 31);
            any = true;
        }
        value = 0;
        inNumber = false;
    };

    for (char c : text) {
        if (c >= '0' && c <= '9') {
            value = std::min(value * 10 + (c - '0'), 99);
            inNumber = true;
        } else {
            commit();
        }
    }
    commit();

    return any;
}

// ============================================================================
// Presets
// ============================================================================
/*
 * A few grooves worth having by name.
 *
 * Each is the shortest list that produces the feel, because a groove read at
 * a glance is one somebody can modify. The tick numbers are the ones the
 * trackers that popularised each feel actually used.
 */
struct GroovePresetEntry {
    const char* name;
    const char* speeds;
    const char* description;
};

inline constexpr GroovePresetEntry GROOVE_TIMING_PRESETS[] = {
    {"Straight",     "6 6",
     "Every row the same length. The default, and what a groove is measured "
     "against."},
    {"Light swing",  "7 5",
     "One tick borrowed from every second row. The smallest amount of swing "
     "that is not straight."},
    {"Hard swing",   "8 4",
     "Two thirds and one third - a triplet feel, and as far as a two-row "
     "groove goes before it stops sounding like swing and starts sounding "
     "like a mistake."},
    {"Shuffle",      "7 5 6 6",
     "Swung on the first half of the beat and straight on the second. The "
     "limp that a single swing amount cannot express."},
    {"Half swing",   "7 5 7 5 6 6 6 6",
     "Two swung beats then two straight ones, so the feel resolves every "
     "bar rather than every beat."},
    {"Drag",         "5 7",
     "Swung backwards - the off-beat arrives early. Rare, and it is what "
     "makes a part sound like it is rushing on purpose."},
};

inline constexpr int GROOVE_TIMING_PRESET_COUNT =
    static_cast<int>(sizeof(GROOVE_TIMING_PRESETS) /
                     sizeof(GROOVE_TIMING_PRESETS[0]));

}  // namespace ChiptuneTracker
