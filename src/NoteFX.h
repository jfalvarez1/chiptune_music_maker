#pragma once

// ============================================================================
// Note effects - a rack that transforms notes rather than samples
//
// The insert rack in Effects.h processes floats: it changes how a note
// SOUNDS. This one processes notes: it changes WHICH notes there are. A
// chord module turns one note into three. An arpeggiator turns those three
// into a running sequence. A strum spreads them apart in time.
//
// WHY IT IS A RACK AND NOT A BUTTON. Every one of these could be a menu
// command that writes notes into the pattern, and that is how this program
// did arpeggios before: the notes appeared, and from then on they were
// simply notes. Changing the rate meant undoing and doing it again, and
// there was no way to hear the part without the arpeggio. As a rack it is a
// property of the channel - the pattern still holds the note you played, and
// what you hear is that note read through the rack, which you can reorder,
// switch off, or change while it plays.
//
// AUDIO THREAD RULES. Everything here works on a caller-supplied array of
// fixed capacity and allocates nothing. There are no vectors, no locks and
// no branches on anything the UI thread can resize. That is why the rack the
// sequencer reads is a fixed-size copy of the one the project stores, made
// at the same sync point that copies every other channel setting.
//
// WHAT IT DOES NOT DO YET, said here rather than left to be discovered: the
// rack sees one written note at a time, expanded into its hits. So the
// arpeggiator arpeggiates a chord that a Chord module made, and it
// arpeggiates one note across octaves, but three notes you typed into the
// same row of the pattern are three separate runs through the rack and are
// not arpeggiated together. Grouping simultaneous notes is a change to how
// the sequencer walks a pattern, not to this file.
// ============================================================================

#include "Scales.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// What a note becomes
// ============================================================================
/*
 * One sounding voice: a pitch, a span, a level.
 *
 * NoteEvents.h's NoteTrigger is the same idea without a pitch, because the
 * tracker effects it expands - delay, cut, retrigger, echo - never change
 * the pitch. These do, so the pitch travels with the voice.
 */
struct NoteVoice {
    float startBeat = 0.0f;
    float endBeat   = 0.0f;
    float velocity  = 1.0f;
    int   pitch     = 60;
};

// A chord of four notes, retriggered eight times, is already more than a
// chip channel can play; this bounds the stack buffer rather than the music.
inline constexpr int MAX_NOTE_VOICES = 32;

// ============================================================================
// The modules
// ============================================================================
enum class NoteFXType : int {
    Transpose = 0,   // move everything by a fixed number of semitones
    Chord,           // one note becomes several
    Arpeggio,        // several notes become a sequence
    Strum,           // a chord stops arriving all at once
    Octave,          // add a copy an octave away
    ScaleSnap,       // force everything into a key
    Range,           // pass only part of the keyboard
    Velocity         // set, scale or humanise the level
};

inline constexpr int NOTE_FX_TYPE_COUNT = 8;

inline const char* noteFXName(NoteFXType type) {
    switch (type) {
        case NoteFXType::Transpose: return "Transpose";
        case NoteFXType::Chord:     return "Chord";
        case NoteFXType::Arpeggio:  return "Arpeggio";
        case NoteFXType::Strum:     return "Strum";
        case NoteFXType::Octave:    return "Octave";
        case NoteFXType::ScaleSnap: return "Scale";
        case NoteFXType::Range:     return "Range";
        case NoteFXType::Velocity:  return "Velocity";
    }
    return "Note FX";
}

/*
 * A stable token per type, for the file format.
 *
 * Saved by name rather than by enumerator value, so inserting a module in
 * the middle of the enum later cannot silently reinterpret every project
 * that already exists - the same rule the insert rack follows.
 */
inline const char* noteFXTypeId(NoteFXType type) {
    switch (type) {
        case NoteFXType::Transpose: return "transpose";
        case NoteFXType::Chord:     return "chord";
        case NoteFXType::Arpeggio:  return "arpeggio";
        case NoteFXType::Strum:     return "strum";
        case NoteFXType::Octave:    return "octave";
        case NoteFXType::ScaleSnap: return "scale";
        case NoteFXType::Range:     return "range";
        case NoteFXType::Velocity:  return "velocity";
    }
    return "transpose";
}

inline bool noteFXTypeFromId(const char* id, NoteFXType& out) {
    if (id == nullptr) return false;
    for (int i = 0; i < NOTE_FX_TYPE_COUNT; ++i) {
        const NoteFXType type = static_cast<NoteFXType>(i);
        const char* token = noteFXTypeId(type);
        const char* a = token;
        const char* b = id;
        while (*a != '\0' && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') { out = type; return true; }
    }
    return false;
}

// ---- Chords ----------------------------------------------------------------
enum class ChordShape : int {
    MajorTriad = 0, MinorTriad, Power, Sus2, Sus4,
    Major7, Minor7, Dominant7, Octaves, Diatonic
};
inline constexpr int CHORD_SHAPE_COUNT = 10;

inline const char* chordShapeName(int shape) {
    switch (shape) {
        case 0: return "Major";
        case 1: return "Minor";
        case 2: return "Power (1-5)";
        case 3: return "Sus2";
        case 4: return "Sus4";
        case 5: return "Major 7th";
        case 6: return "Minor 7th";
        case 7: return "Dominant 7th";
        case 8: return "Octaves";
        case 9: return "Diatonic (in key)";
        default: return "Major";
    }
}

// Semitones above the played note. The played note is always included and is
// not listed here, so an empty list means "no change".
inline const int* chordIntervals(int shape, int& countOut) {
    static constexpr int MAJOR[]      = {4, 7};
    static constexpr int MINOR[]      = {3, 7};
    static constexpr int POWER[]      = {7};
    static constexpr int SUS2[]       = {2, 7};
    static constexpr int SUS4[]       = {5, 7};
    static constexpr int MAJOR7[]     = {4, 7, 11};
    static constexpr int MINOR7[]     = {3, 7, 10};
    static constexpr int DOMINANT7[]  = {4, 7, 10};
    static constexpr int OCTAVES[]    = {12};

    switch (shape) {
        case 0: countOut = 2; return MAJOR;
        case 1: countOut = 2; return MINOR;
        case 2: countOut = 1; return POWER;
        case 3: countOut = 2; return SUS2;
        case 4: countOut = 2; return SUS4;
        case 5: countOut = 3; return MAJOR7;
        case 6: countOut = 3; return MINOR7;
        case 7: countOut = 3; return DOMINANT7;
        case 8: countOut = 1; return OCTAVES;
        default: countOut = 0; return MAJOR;   // Diatonic is computed, not listed
    }
}

/*
 * Move a pitch up by whole scale degrees.
 *
 * This is what makes a diatonic chord diatonic: the third above C in C major
 * is E (four semitones) and the third above D is F (three), and a fixed
 * interval table cannot express that. A pitch outside the scale is snapped
 * into it first, because "two degrees above a note that has no degree" has
 * no other sensible answer.
 */
inline int scaleDegreesAbove(int pitch, int degrees, int root, int type) {
    int listed = 7;
    const int* intervals = scaleIntervals(type, listed);

    int size = 0;
    while (size < listed && intervals[size] >= 0) ++size;
    if (size <= 0) return pitch;

    const int snapped = snapToScale(pitch, root, type);
    const int pitchClass = ((snapped - root) % 12 + 12) % 12;

    int index = 0;
    for (int i = 0; i < size; ++i) {
        if (intervals[i] == pitchClass) { index = i; break; }
    }

    const int target = index + degrees;
    // Floored division, so a negative degree walks down an octave rather
    // than truncating toward zero and landing on the wrong note.
    int octave = target / size;
    int within = target % size;
    if (within < 0) { within += size; --octave; }

    return snapped - intervals[index] + intervals[within] + 12 * octave;
}

// ---- Arpeggios -------------------------------------------------------------
enum class NoteArpMode : int { Up = 0, Down, UpDown, Random, AsPlayed };
inline constexpr int NOTE_ARP_MODE_COUNT = 5;

inline const char* noteArpModeName(int mode) {
    switch (mode) {
        case 0: return "Up";
        case 1: return "Down";
        case 2: return "Up-down";
        case 3: return "Random";
        case 4: return "As played";
        default: return "Up";
    }
}

// ============================================================================
// One slot
// ============================================================================
/*
 * Every parameter every module needs, in one flat struct.
 *
 * Flat rather than a variant, for the same reason ChannelConfig is flat:
 * the serializer writes named fields with defaults omitted, so a slot that
 * is a Transpose costs one line in the file and carries no arpeggio
 * settings it will never read. Which module reads which field is written
 * against each one.
 */
struct NoteFXSlot {
    NoteFXType type = NoteFXType::Transpose;
    bool enabled = true;

    int semitones = 0;              // Transpose: -48..48
    int octaves = 1;                // Octave: -3..3, 0 does nothing
    float mix = 0.7f;               // Octave: level of the added copy

    int chordShape = 0;             // Chord: a ChordShape
    bool chordInversion = false;    // Chord: drop the top voice an octave

    int arpMode = 0;                // Arpeggio: a NoteArpMode
    float arpRate = 0.25f;          // Arpeggio: beats per step
    int arpOctaves = 1;             // Arpeggio: 1..4, how far it climbs
    float arpGate = 0.9f;           // Arpeggio: how much of a step sounds

    float strumBeats = 0.02f;       // Strum: gap between voices
    bool strumDown = false;         // Strum: high note first

    int scaleRoot = 0;              // ScaleSnap, Chord/Diatonic: 0 = C
    int scaleType = 0;              // ScaleSnap, Chord/Diatonic: a Scales.h type

    int lowPitch = 0;               // Range: inclusive
    int highPitch = 127;            // Range: inclusive
    bool rangeInvert = false;       // Range: pass what falls OUTSIDE instead

    float velocityScale = 1.0f;     // Velocity: multiply
    float velocityFixed = 0.0f;     // Velocity: >0 replaces rather than scales
    float velocityRandom = 0.0f;    // Velocity: +/- this much, deterministic
};

// Eight is more than any of these racks wants to be. A ninth module is
// almost always a sign the part should have been written out.
inline constexpr int MAX_NOTE_FX = 8;

/*
 * The fixed-size rack the audio thread reads.
 *
 * The project stores a std::vector<NoteFXSlot> per channel because that is
 * what the serializer and the editor want. The sequencer copies it into one
 * of these at the same sync point that copies every other channel setting,
 * so the audio thread never walks a container the UI thread can resize
 * underneath it.
 */
struct NoteFXRack {
    NoteFXSlot slots[MAX_NOTE_FX];
    int count = 0;

    bool empty() const { return count <= 0; }

    void clear() { count = 0; }

    void push(const NoteFXSlot& slot) {
        if (count >= MAX_NOTE_FX) return;
        slots[count++] = slot;
    }

    // Anything actually going to change? A rack of nothing but disabled
    // slots must cost the same as no rack at all.
    bool active() const {
        for (int i = 0; i < count; ++i) {
            if (slots[i].enabled) return true;
        }
        return false;
    }
};

// ============================================================================
// Applying it
// ============================================================================
namespace detail {

// The same hash the probability roll uses: a pure function of the note, so
// the answer is identical every time it is asked. A running generator would
// give a different answer at note-on and note-off.
inline uint32_t noteFXHash(uint32_t seed, int salt) {
    uint32_t h = seed ^ (static_cast<uint32_t>(salt) * 2654435761u);
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

inline float unitFromHash(uint32_t h) {
    return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

inline bool pitchIsPlayable(int pitch) { return pitch >= 0 && pitch <= 127; }

}  // namespace detail

/*
 * Run one rack over a set of voices, in place.
 *
 * `count` voices are already in `voices`; up to `capacity` may come out.
 * Returns the new count. Never allocates, never reads past `capacity`, and
 * drops rather than wraps anything pushed off the ends of MIDI - a bass note
 * that reappears ten octaves up is a far stranger thing to hear than a
 * missing one.
 *
 * `seed` should be derived from the note, so the random modules answer the
 * same way for the note-on and the note-off that arrive thousands of samples
 * apart.
 */
inline int applyNoteFX(const NoteFXRack& rack, NoteVoice* voices, int count,
                       int capacity, uint32_t seed) {
    if (voices == nullptr || count <= 0 || capacity <= 0) return 0;
    count = std::min(count, capacity);
    if (!rack.active()) return count;

    NoteVoice scratch[MAX_NOTE_VOICES];
    const int room = std::min(capacity, MAX_NOTE_VOICES);

    for (int slotIndex = 0; slotIndex < rack.count && count > 0; ++slotIndex) {
        const NoteFXSlot& slot = rack.slots[slotIndex];
        if (!slot.enabled) continue;

        switch (slot.type) {

        // ---- Transpose -----------------------------------------------------
        case NoteFXType::Transpose: {
            int kept = 0;
            for (int i = 0; i < count; ++i) {
                const int pitch = voices[i].pitch + slot.semitones;
                if (!detail::pitchIsPlayable(pitch)) continue;
                voices[kept] = voices[i];
                voices[kept].pitch = pitch;
                ++kept;
            }
            count = kept;
            break;
        }

        // ---- Scale snap ----------------------------------------------------
        case NoteFXType::ScaleSnap: {
            for (int i = 0; i < count; ++i) {
                voices[i].pitch = std::clamp(
                    snapToScale(voices[i].pitch, slot.scaleRoot, slot.scaleType),
                    0, 127);
            }
            break;
        }

        // ---- Range ---------------------------------------------------------
        case NoteFXType::Range: {
            const int low = std::min(slot.lowPitch, slot.highPitch);
            const int high = std::max(slot.lowPitch, slot.highPitch);
            int kept = 0;
            for (int i = 0; i < count; ++i) {
                const bool inside = voices[i].pitch >= low && voices[i].pitch <= high;
                if (inside == slot.rangeInvert) continue;
                voices[kept++] = voices[i];
            }
            count = kept;
            break;
        }

        // ---- Velocity ------------------------------------------------------
        case NoteFXType::Velocity: {
            for (int i = 0; i < count; ++i) {
                float velocity = (slot.velocityFixed > 0.0f)
                                     ? slot.velocityFixed
                                     : voices[i].velocity * slot.velocityScale;
                if (slot.velocityRandom > 0.0f) {
                    const float roll = detail::unitFromHash(
                        detail::noteFXHash(seed, slotIndex * 31 + i));
                    velocity += (roll * 2.0f - 1.0f) * slot.velocityRandom;
                }
                // A velocity of zero is a note that plays silently and still
                // takes a voice, so the floor is audible rather than nothing.
                voices[i].velocity = std::clamp(velocity, 0.02f, 1.0f);
            }
            break;
        }

        // ---- Octave --------------------------------------------------------
        case NoteFXType::Octave: {
            if (slot.octaves == 0) break;
            const int existing = count;
            for (int i = 0; i < existing && count < room; ++i) {
                const int pitch = voices[i].pitch + 12 * slot.octaves;
                if (!detail::pitchIsPlayable(pitch)) continue;
                voices[count] = voices[i];
                voices[count].pitch = pitch;
                voices[count].velocity =
                    std::clamp(voices[i].velocity * slot.mix, 0.02f, 1.0f);
                ++count;
            }
            break;
        }

        // ---- Chord ---------------------------------------------------------
        case NoteFXType::Chord: {
            const int existing = count;

            if (slot.chordShape == static_cast<int>(ChordShape::Diatonic)) {
                for (int i = 0; i < existing && count < room; ++i) {
                    for (int degree = 2; degree <= 4 && count < room; degree += 2) {
                        const int pitch = scaleDegreesAbove(
                            voices[i].pitch, degree, slot.scaleRoot, slot.scaleType);
                        if (!detail::pitchIsPlayable(pitch)) continue;
                        voices[count] = voices[i];
                        voices[count].pitch = pitch;
                        ++count;
                    }
                }
            } else {
                int intervalCount = 0;
                const int* intervals = chordIntervals(slot.chordShape, intervalCount);
                for (int i = 0; i < existing && count < room; ++i) {
                    for (int k = 0; k < intervalCount && count < room; ++k) {
                        // An inversion drops the top voice an octave, which
                        // is what keeps a chord under a melody instead of
                        // over it.
                        const int offset =
                            (slot.chordInversion && k == intervalCount - 1)
                                ? intervals[k] - 12
                                : intervals[k];
                        const int pitch = voices[i].pitch + offset;
                        if (!detail::pitchIsPlayable(pitch)) continue;
                        voices[count] = voices[i];
                        voices[count].pitch = pitch;
                        ++count;
                    }
                }
            }
            break;
        }

        // ---- Strum ---------------------------------------------------------
        //
        // A chord that arrives all at once is a keyboard. A chord whose
        // notes arrive a few milliseconds apart is a hand.
        case NoteFXType::Strum: {
            if (slot.strumBeats <= 0.0f || count < 2) break;

            for (int i = 0; i < count; ++i) scratch[i] = voices[i];
            std::sort(scratch, scratch + count,
                      [](const NoteVoice& a, const NoteVoice& b) {
                          if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
                          return a.pitch < b.pitch;
                      });

            for (int i = 0; i < count; ++i) {
                const int order = slot.strumDown ? (count - 1 - i) : i;
                const float shift = static_cast<float>(order) * slot.strumBeats;
                // The whole voice moves. Shifting only the start would make
                // the low notes of a strummed chord longer than the high
                // ones, which is a different effect and not this one.
                scratch[i].startBeat += shift;
                scratch[i].endBeat += shift;
                voices[i] = scratch[i];
            }
            break;
        }

        // ---- Arpeggio ------------------------------------------------------
        //
        // Everything currently sounding becomes one run instead. The span is
        // the union of what came in, so an arpeggio lasts exactly as long as
        // the note or chord it replaced.
        case NoteFXType::Arpeggio: {
            const float rate = std::max(1.0f / 64.0f, slot.arpRate);

            float from = voices[0].startBeat;
            float to = voices[0].endBeat;
            float velocity = 0.0f;
            for (int i = 0; i < count; ++i) {
                from = std::min(from, voices[i].startBeat);
                to = std::max(to, voices[i].endBeat);
                velocity = std::max(velocity, voices[i].velocity);
            }
            if (to <= from) break;

            // The pitches to run through, in the order the mode asks for.
            int pitches[MAX_NOTE_VOICES];
            int pitchCount = 0;
            for (int i = 0; i < count && pitchCount < MAX_NOTE_VOICES; ++i) {
                bool seen = false;
                for (int k = 0; k < pitchCount; ++k) {
                    if (pitches[k] == voices[i].pitch) { seen = true; break; }
                }
                if (!seen) pitches[pitchCount++] = voices[i].pitch;
            }
            if (pitchCount <= 0) break;

            const int mode = slot.arpMode;
            if (mode != static_cast<int>(NoteArpMode::AsPlayed)) {
                std::sort(pitches, pitches + pitchCount);
            }
            if (mode == static_cast<int>(NoteArpMode::Down)) {
                std::reverse(pitches, pitches + pitchCount);
            }

            const int octaves = std::clamp(slot.arpOctaves, 1, 4);
            const int cycle = (mode == static_cast<int>(NoteArpMode::UpDown) &&
                               pitchCount * octaves > 1)
                                  ? pitchCount * octaves * 2 - 2
                                  : pitchCount * octaves;

            const float gate = std::clamp(slot.arpGate, 0.05f, 1.0f);

            int emitted = 0;
            for (int step = 0; emitted < room; ++step) {
                // Accumulated from the start rather than added up step by
                // step: over a long held note the running sum drifts, and a
                // drifting arpeggio is one that slowly stops lining up with
                // the drums.
                const float at = from + static_cast<float>(step) * rate;
                if (at >= to - 1e-4f) break;

                int index = (cycle > 0) ? (step % cycle) : 0;

                // Random picks a step rather than walking one. Hashed from
                // the note and the step number, not drawn from a running
                // generator, so the run is the same every time the note
                // plays within a pass and different on the next pass round
                // the loop - the same rule the probability roll follows.
                if (mode == static_cast<int>(NoteArpMode::Random)) {
                    const int span = std::max(1, pitchCount * octaves);
                    index = static_cast<int>(
                        detail::noteFXHash(seed, slotIndex * 977 + step) %
                        static_cast<uint32_t>(span));
                }
                // Up-down folds the run back on itself without repeating the
                // two turning notes, which is what makes it sound like one
                // gesture rather than two.
                if (index >= pitchCount * octaves) {
                    index = pitchCount * octaves * 2 - 2 - index;
                }

                // Down descends through the octaves as well as through the
                // notes. Climbing octaves while descending inside each one
                // is a different figure, and not the one anybody means by
                // "down".
                const int octaveStep = index / pitchCount;
                const int octave = (mode == static_cast<int>(NoteArpMode::Down))
                                       ? (octaves - 1 - octaveStep)
                                       : octaveStep;

                const int pitch = pitches[index % pitchCount] + 12 * octave;
                if (!detail::pitchIsPlayable(pitch)) continue;

                voices[emitted].startBeat = at;
                voices[emitted].endBeat = std::min(to, at + rate * gate);
                voices[emitted].velocity = velocity;
                voices[emitted].pitch = pitch;
                ++emitted;
            }

            count = emitted;
            break;
        }

        }   // switch
    }

    return std::min(count, room);
}

/*
 * Copy a project's rack into the fixed-size one the audio thread reads.
 *
 * Kept here rather than in the sequencer so the truncation rule lives beside
 * the capacity it truncates to.
 */
inline void buildNoteFXRack(const std::vector<NoteFXSlot>& source,
                            NoteFXRack& out) {
    out.clear();
    for (size_t i = 0; i < source.size() && out.count < MAX_NOTE_FX; ++i) {
        out.push(source[i]);
    }
}

}  // namespace ChiptuneTracker
