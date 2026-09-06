#pragma once

// ============================================================================
// Naming a chord you already played
//
// The rest of this program generates chords: you pick C minor and it gives
// you the notes. This goes the other way - you have notes, drawn by hand or
// dragged around until they sounded right, and you want to know what they
// are.
//
// That is a harder question than it looks, and the reason is that it does not
// always have one answer. {C, E, G} is C major and nothing else. {C, Eb, Gb,
// A} is a diminished seventh, and it is EQUALLY a diminished seventh on Eb,
// on Gb and on A - the shape is symmetric, so all four spellings describe the
// same four notes and no analysis can prefer one. Any tool that prints a
// single confident name for that is guessing and not saying so.
//
// So this returns a ranked list with its reasoning attached - how many notes
// it had to ignore, how many it had to assume - and the caller shows enough
// of it that a musician can disagree. A wrong name offered as one of three is
// a suggestion; the same name alone is a lie.
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// How chords are spelled
// ============================================================================
/*
 * One name per pitch class, flats except for F#.
 *
 * A real answer needs the key: the same black note is D# in E major and Eb in
 * Bb minor, and both are correct. This program has no key signature to ask,
 * so rather than pretend, it picks the spelling that is most common in
 * isolation and stays consistent. Db over C#, Eb over D#, F# over Gb, Ab over
 * G#, Bb over A# - which is what a chord chart uses when nothing else is
 * known.
 */
inline const char* CHORD_ROOT_NAMES[12] = {
    "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

inline std::string chordRootName(int pitchClass) {
    const int pc = ((pitchClass % 12) + 12) % 12;
    return CHORD_ROOT_NAMES[pc];
}

// ============================================================================
// The shapes
// ============================================================================
/*
 * Intervals in semitones above the root.
 *
 * ORDERED BY HOW OFTEN THEY TURN UP, because that order is the tie-breaker.
 * Several of these are the same set of pitch classes read from a different
 * root - a C6 and an Am7 are the same four notes - and when nothing else
 * separates two readings, the commoner one goes first. That is a convention,
 * not a fact, which is why it is the weakest term in the score below and why
 * the loser is still in the list.
 */
struct ChordShapeDef {
    const char* suffix;      // appended to the root name
    const char* longName;    // for the tooltip, spelled out
    std::array<int, 7> intervals;
    int count;
};

inline const ChordShapeDef CHORD_SHAPES[] = {
    {"",       "major",                  {0, 4, 7, 0, 0, 0, 0}, 3},
    {"m",      "minor",                  {0, 3, 7, 0, 0, 0, 0}, 3},
    {"7",      "dominant 7th",           {0, 4, 7, 10, 0, 0, 0}, 4},
    {"maj7",   "major 7th",              {0, 4, 7, 11, 0, 0, 0}, 4},
    {"m7",     "minor 7th",              {0, 3, 7, 10, 0, 0, 0}, 4},
    {"dim",    "diminished",             {0, 3, 6, 0, 0, 0, 0}, 3},
    {"aug",    "augmented",              {0, 4, 8, 0, 0, 0, 0}, 3},
    {"sus4",   "suspended 4th",          {0, 5, 7, 0, 0, 0, 0}, 3},
    {"sus2",   "suspended 2nd",          {0, 2, 7, 0, 0, 0, 0}, 3},
    {"6",      "major 6th",              {0, 4, 7, 9, 0, 0, 0}, 4},
    {"m6",     "minor 6th",              {0, 3, 7, 9, 0, 0, 0}, 4},
    {"m7b5",   "half-diminished 7th",    {0, 3, 6, 10, 0, 0, 0}, 4},
    {"dim7",   "diminished 7th",         {0, 3, 6, 9, 0, 0, 0}, 4},
    {"mMaj7",  "minor major 7th",        {0, 3, 7, 11, 0, 0, 0}, 4},
    {"add9",   "added 9th",              {0, 2, 4, 7, 0, 0, 0}, 4},
    {"madd9",  "minor added 9th",        {0, 2, 3, 7, 0, 0, 0}, 4},
    {"9",      "dominant 9th",           {0, 2, 4, 7, 10, 0, 0}, 5},
    {"maj9",   "major 9th",              {0, 2, 4, 7, 11, 0, 0}, 5},
    {"m9",     "minor 9th",              {0, 2, 3, 7, 10, 0, 0}, 5},
    {"7sus4",  "dominant 7th suspended", {0, 5, 7, 10, 0, 0, 0}, 4},
    {"6/9",    "six-nine",               {0, 2, 4, 7, 9, 0, 0}, 5},
    {"7b9",    "dominant 7th flat 9",    {0, 1, 4, 7, 10, 0, 0}, 5},
    {"7#9",    "dominant 7th sharp 9",   {0, 3, 4, 7, 10, 0, 0}, 5},
    {"7#5",    "augmented 7th",          {0, 4, 8, 10, 0, 0, 0}, 4},
    {"7b5",    "dominant 7th flat 5",    {0, 4, 6, 10, 0, 0, 0}, 4},
    {"11",     "dominant 11th",          {0, 2, 5, 7, 10, 0, 0}, 5},
    {"13",     "dominant 13th",          {0, 4, 7, 9, 10, 0, 0}, 5},
    {"m11",    "minor 11th",             {0, 2, 3, 5, 7, 10, 0}, 6},
    {"5",      "power chord",            {0, 7, 0, 0, 0, 0, 0}, 2},
};

inline constexpr int CHORD_SHAPE_DEF_COUNT =
    static_cast<int>(sizeof(CHORD_SHAPES) / sizeof(CHORD_SHAPES[0]));

// The intervals, named, for two-note selections and for explaining a match.
inline const char* intervalName(int semitones) {
    switch (((semitones % 12) + 12) % 12) {
        case 0:  return "unison";
        case 1:  return "minor 2nd";
        case 2:  return "major 2nd";
        case 3:  return "minor 3rd";
        case 4:  return "major 3rd";
        case 5:  return "perfect 4th";
        case 6:  return "tritone";
        case 7:  return "perfect 5th";
        case 8:  return "minor 6th";
        case 9:  return "major 6th";
        case 10: return "minor 7th";
        default: return "major 7th";
    }
}

// ============================================================================
// A candidate reading
// ============================================================================
struct ChordMatch {
    std::string name;          // "Cmaj7", "Am/C"
    std::string longName;      // "C major 7th, first inversion"
    int rootPitchClass = 0;
    int shape = -1;            // index into CHORD_SHAPES
    int missing = 0;           // chord tones the selection does not contain
    int extra = 0;             // selected notes the chord does not contain
    int inversion = 0;         // 0 root position, 1 first, ...
    bool exact = false;        // every tone present and nothing else
    bool symmetric = false;    // the shape repeats, so the root is a choice
    float score = 0.0f;
};

struct ChordIdentification {
    std::vector<ChordMatch> matches;   // best first
    std::vector<int> pitchClasses;     // what was analysed, sorted
    int bassPitchClass = -1;
    int noteCount = 0;                 // distinct pitch classes
    std::string summary;               // one line, for a caller with no room
};

namespace chordid {

/*
 * A shape is symmetric when transposing it lands on itself.
 *
 * This is the whole reason a diminished 7th has no single name: its four
 * notes are three semitones apart all the way round, so rotating it by three
 * semitones gives the identical set. Nothing in the notes prefers one root
 * over another, and a tool that picks one without saying so is inventing
 * information. Computed rather than flagged by hand, so adding a shape to the
 * table cannot forget to declare it.
 */
inline bool shapeIsSymmetric(const ChordShapeDef& shape) {
    bool present[12] = {false};
    for (int i = 0; i < shape.count; ++i) {
        present[((shape.intervals[static_cast<size_t>(i)] % 12) + 12) % 12] = true;
    }
    for (int rotation = 1; rotation < 12; ++rotation) {
        bool same = true;
        for (int pc = 0; pc < 12; ++pc) {
            if (present[pc] != present[(pc + rotation) % 12]) { same = false; break; }
        }
        if (same) return true;
    }
    return false;
}

}  // namespace chordid

/*
 * What these notes are, ranked.
 *
 * The scoring, and why each weight is what it is:
 *
 * - An EXTRA note costs most. A reading that ignores something you actually
 *   played is describing a different chord, and "C major, ignore that B" is
 *   worse than any name that accounts for the B.
 *
 * - A MISSING note costs less, because real voicings leave notes out all the
 *   time - and the fifth costs least of all, since it is the first thing
 *   every guitarist and every four-voice arrangement drops. Charging the same
 *   for an absent fifth as for an absent third would make rootless and
 *   fifthless voicings unnameable, which is most of jazz.
 *
 * - The BASS being the root is worth a little. It is genuine evidence and
 *   not proof: first-inversion chords are ordinary, so this nudges rather
 *   than decides.
 *
 * - COMMONNESS breaks ties and nothing else. It is the only term here that
 *   is taste rather than arithmetic, so it is scaled to lose against every
 *   other consideration.
 */
inline ChordIdentification identifyChord(const std::vector<int>& midiPitches,
                                         int maxResults = 4) {
    ChordIdentification result;
    if (midiPitches.empty()) {
        result.summary = "nothing selected";
        return result;
    }

    // Pitch classes, unique and sorted; the bass is the lowest note actually
    // played, which is a fact about the voicing rather than about the set.
    int lowest = midiPitches[0];
    bool seen[12] = {false};
    for (int pitch : midiPitches) {
        lowest = std::min(lowest, pitch);
        seen[((pitch % 12) + 12) % 12] = true;
    }
    for (int pc = 0; pc < 12; ++pc) {
        if (seen[pc]) result.pitchClasses.push_back(pc);
    }
    result.bassPitchClass = ((lowest % 12) + 12) % 12;
    result.noteCount = static_cast<int>(result.pitchClasses.size());

    // One note is a note, and two are an interval. Naming either as a chord
    // would mean inventing the notes that are not there.
    if (result.noteCount == 1) {
        result.summary = chordRootName(result.pitchClasses[0]) +
                         " - a single note, in " +
                         std::to_string(midiPitches.size()) +
                         (midiPitches.size() == 1 ? " voice" : " voices");
        return result;
    }
    if (result.noteCount == 2) {
        const int bass = result.bassPitchClass;
        const int other = (result.pitchClasses[0] == bass)
            ? result.pitchClasses[1] : result.pitchClasses[0];
        const int gap = ((other - bass) % 12 + 12) % 12;
        result.summary = chordRootName(bass) + " to " + chordRootName(other) +
                         " - a " + intervalName(gap);
        if (gap == 7) {
            result.summary += " (" + chordRootName(bass) + "5, a power chord)";
        } else if (gap == 5) {
            // The same two notes the other way up, which on a keyboard is
            // usually what was meant.
            result.summary += " (or " + chordRootName(other) + "5 inverted)";
        }
        return result;
    }

    for (int root = 0; root < 12; ++root) {
        for (int s = 0; s < CHORD_SHAPE_DEF_COUNT; ++s) {
            const ChordShapeDef& shape = CHORD_SHAPES[s];

            bool inChord[12] = {false};
            for (int i = 0; i < shape.count; ++i) {
                inChord[(root + shape.intervals[static_cast<size_t>(i)]) % 12] = true;
            }

            int extra = 0;
            for (int pc : result.pitchClasses) {
                if (!inChord[pc]) ++extra;
            }

            int missing = 0;
            float missingPenalty = 0.0f;
            for (int i = 0; i < shape.count; ++i) {
                const int interval = shape.intervals[static_cast<size_t>(i)];
                const int pc = (root + interval) % 12;
                if (!seen[pc]) {
                    ++missing;
                    // The fifth is the note every voicing drops first.
                    missingPenalty += (interval == 7) ? 3.0f : 9.0f;
                }
            }

            // A reading that supplies more than it found is not a reading.
            if (missing >= shape.count - 1) continue;

            float score = 100.0f;
            score -= 14.0f * float(extra);
            score -= missingPenalty;
            if (root == result.bassPitchClass) score += 5.0f;
            score -= 0.05f * float(s);

            ChordMatch match;
            match.rootPitchClass = root;
            match.shape = s;
            match.missing = missing;
            match.extra = extra;
            match.exact = (extra == 0 && missing == 0);
            match.symmetric = chordid::shapeIsSymmetric(shape);
            match.score = score;

            match.name = chordRootName(root) + std::string(shape.suffix);
            if (root != result.bassPitchClass) {
                match.name += "/" + chordRootName(result.bassPitchClass);
            }

            /*
             * Which inversion, counted by where the bass sits in the shape.
             *
             * Only meaningful when the bass IS a chord tone. A bass note the
             * chord does not contain is not an inversion of it - it is a
             * slash chord, and the name already says so.
             */
            match.inversion = 0;
            for (int i = 0; i < shape.count; ++i) {
                if ((root + shape.intervals[static_cast<size_t>(i)]) % 12 ==
                    result.bassPitchClass) {
                    match.inversion = i;
                    break;
                }
            }

            match.longName = chordRootName(root) + std::string(" ") + shape.longName;
            if (match.inversion == 1) match.longName += ", first inversion";
            else if (match.inversion == 2) match.longName += ", second inversion";
            else if (match.inversion == 3) match.longName += ", third inversion";
            if (missing > 0) {
                match.longName += ", no ";
                bool first = true;
                for (int i = 0; i < shape.count; ++i) {
                    const int interval = shape.intervals[static_cast<size_t>(i)];
                    if (seen[(root + interval) % 12]) continue;
                    if (!first) match.longName += " or ";
                    match.longName += intervalName(interval);
                    first = false;
                }
            }
            if (extra > 0) {
                match.longName += ", ignoring " + std::to_string(extra) +
                                  (extra == 1 ? " note" : " notes");
            }

            result.matches.push_back(match);
        }
    }

    /*
     * Best first, and deterministically.
     *
     * The float score alone is not a total order - several readings tie
     * exactly, and which one std::sort happens to leave on top would then
     * depend on the standard library. A chord that renames itself between
     * builds is a bug nobody can reproduce, so root and shape break the
     * remaining ties.
     */
    std::sort(result.matches.begin(), result.matches.end(),
              [](const ChordMatch& a, const ChordMatch& b) {
                  if (a.score != b.score) return a.score > b.score;
                  if (a.rootPitchClass != b.rootPitchClass)
                      return a.rootPitchClass < b.rootPitchClass;
                  return a.shape < b.shape;
              });

    if (static_cast<int>(result.matches.size()) > maxResults) {
        result.matches.resize(static_cast<size_t>(maxResults));
    }

    if (result.matches.empty()) {
        result.summary = std::to_string(result.noteCount) +
                         " notes with no chord shape to fit them";
    } else {
        const ChordMatch& best = result.matches[0];
        result.summary = best.name;
        if (!best.exact) result.summary += " (approximate)";
        else if (best.symmetric) result.summary += " (symmetric - any of its notes could be the root)";
    }

    return result;
}

/*
 * The readings that are genuinely alternatives, rather than just next.
 *
 * "Also reads as" has to mean something. Given C E G B, the ranked list
 * contains plain C major - reached by ignoring the B - and offering that as
 * another way to read the chord is not a second opinion, it is a worse one
 * dressed as a choice. The same goes the other way: when nothing fits
 * exactly, every candidate is approximate and the runners-up are real
 * alternatives worth seeing.
 *
 * So the rule is that an alternative must be AS GOOD AS the best reading -
 * exact if the best was exact - and must not simply be the same chord under
 * another root, which for a symmetric shape would fill the list with four
 * spellings of one thing.
 */
inline std::vector<ChordMatch> alternativeReadings(const ChordIdentification& id) {
    std::vector<ChordMatch> out;
    if (id.matches.size() < 2) return out;

    const ChordMatch& best = id.matches[0];
    for (size_t i = 1; i < id.matches.size(); ++i) {
        const ChordMatch& other = id.matches[i];
        if (best.exact && !other.exact) continue;
        if (other.shape == best.shape && other.symmetric) continue;
        out.push_back(other);
    }
    return out;
}

}  // namespace ChiptuneTracker
