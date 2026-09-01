#pragma once

/*
 * ChiptuneTracker - Take lanes and comping
 *
 * Record a part four times, then build the keeper out of the best moments of
 * each. That is comping, and it is how almost every recorded vocal and solo
 * you have ever heard was actually made.
 *
 * THE CENTRAL DECISION: a comp produces ordinary audio clips.
 *
 * The alternative is for the mixer to resolve the comp live - walk the
 * segments every sample and decide which take is sounding. That is what it
 * would mean to make comping a first-class playback concept, and it would
 * duplicate the whole of the audio-clip path: trimming, fades, looping,
 * sample-rate conversion, missing-sample handling, all of it tested and all
 * of it working.
 *
 * So instead the comp is an EDITING model, and flattening turns the choices
 * into clips on the arrangement. Playback does not know comping exists. The
 * result is inspectable - you can see exactly what your swipe produced,
 * because it is right there on the timeline - and the clips can then be
 * nudged, trimmed and faded like any others.
 *
 * The cost is that flattening has to be re-run when the comp changes. It is
 * a UI-thread operation over a handful of segments, so that is cheap; and
 * the clips it emits are tagged, so re-running replaces its own output
 * rather than accumulating.
 *
 * Pure data and interval arithmetic. No ImGui, no audio thread, no I/O.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "Types.h"

namespace ChiptuneTracker {

/*
 * Take, CompSegment and CompGroup are declared in Types.h, because Project
 * holds them and this header needs a complete Project to flatten into. What
 * lives here is everything that OPERATES on them.
 */

namespace comp {

/*
 * Add a take, and make it the choice everywhere it covers.
 *
 * A newly recorded pass becoming the active one is the behaviour every DAW
 * has, and it is right: you record because you want to hear the new one. The
 * old take is still there, one click away.
 */
inline int addTake(CompGroup& group, int sampleId, float startBeat,
                   float lengthBeats, const std::string& name);

// Which take sounds at a beat, or -1.
inline int takeAt(const CompGroup& group, float beat) {
    for (const CompSegment& segment : group.segments) {
        if (beat >= segment.startBeat && beat < segment.endBeat) {
            return segment.takeIndex;
        }
    }
    return -1;
}

/*
 * Restore the invariant: sorted, non-overlapping, gapless, and with adjacent
 * runs of the same take merged.
 *
 * Merging is not cosmetic. Without it every swipe leaves another boundary
 * behind, the segment list grows without bound over a session, and flattening
 * emits a separate clip per fragment - so a comp that is one take from start
 * to finish would come out as fifty clips that happen to abut.
 */
inline void normalise(CompGroup& group) {
    std::vector<CompSegment>& segments = group.segments;

    segments.erase(std::remove_if(segments.begin(), segments.end(),
                                  [](const CompSegment& s) {
                                      return !(s.endBeat > s.startBeat + 1e-4f);
                                  }),
                   segments.end());

    std::stable_sort(segments.begin(), segments.end(),
                     [](const CompSegment& a, const CompSegment& b) {
                         return a.startBeat < b.startBeat;
                     });

    std::vector<CompSegment> merged;
    merged.reserve(segments.size());
    for (const CompSegment& segment : segments) {
        if (!merged.empty() &&
            merged.back().takeIndex == segment.takeIndex &&
            std::fabs(merged.back().endBeat - segment.startBeat) < 1e-4f) {
            merged.back().endBeat = segment.endBeat;
        } else {
            merged.push_back(segment);
        }
    }
    segments.swap(merged);
}

/*
 * The swipe: choose `takeIndex` from `fromBeat` to `toBeat`.
 *
 * This is the one operation comping actually is. Everything it touches is
 * replaced; everything overlapping at the edges is trimmed rather than
 * dropped, so swiping across the middle of a segment leaves the two ends
 * intact instead of losing the whole thing - which is what makes it feel
 * like painting rather than like a series of destructive edits.
 */
inline void chooseTake(CompGroup& group, float fromBeat, float toBeat,
                       int takeIndex) {
    if (!std::isfinite(fromBeat) || !std::isfinite(toBeat)) return;
    if (toBeat < fromBeat) std::swap(fromBeat, toBeat);
    if (toBeat - fromBeat < 1e-4f) return;

    std::vector<CompSegment> rebuilt;
    rebuilt.reserve(group.segments.size() + 2);

    for (const CompSegment& segment : group.segments) {
        // Entirely outside the swipe: untouched.
        if (segment.endBeat <= fromBeat || segment.startBeat >= toBeat) {
            rebuilt.push_back(segment);
            continue;
        }
        // The part before the swipe survives.
        if (segment.startBeat < fromBeat) {
            CompSegment head = segment;
            head.endBeat = fromBeat;
            rebuilt.push_back(head);
        }
        // And the part after.
        if (segment.endBeat > toBeat) {
            CompSegment tail = segment;
            tail.startBeat = toBeat;
            rebuilt.push_back(tail);
        }
        // The overlap itself is replaced by the swipe below.
    }

    CompSegment chosen;
    chosen.startBeat = fromBeat;
    chosen.endBeat = toBeat;
    chosen.takeIndex = takeIndex;
    rebuilt.push_back(chosen);

    group.segments.swap(rebuilt);
    normalise(group);
}

inline int addTake(CompGroup& group, int sampleId, float startBeat,
                   float lengthBeats, const std::string& name) {
    if (static_cast<int>(group.takes.size()) >= CompGroup::MAX_TAKES) return -1;
    if (!std::isfinite(startBeat) || !std::isfinite(lengthBeats)) return -1;
    if (lengthBeats <= 0.0f) return -1;

    Take take;
    take.sampleId = sampleId;
    take.startBeat = std::max(0.0f, startBeat);
    take.lengthBeats = lengthBeats;
    take.name = name.empty()
        ? ("Take " + std::to_string(group.takes.size() + 1)) : name;

    group.takes.push_back(take);
    const int index = static_cast<int>(group.takes.size()) - 1;

    // The new pass wins over the span it covers.
    chooseTake(group, take.startBeat, take.endBeat(), index);
    return index;
}

/*
 * Remove a take, and everything that referred to it.
 *
 * Every later take shifts down by one, so every segment pointing past the
 * removed index has to shift with it. Getting this wrong does not crash - it
 * silently plays the wrong take, which is far harder to notice.
 */
inline void removeTake(CompGroup& group, int takeIndex) {
    if (takeIndex < 0 || takeIndex >= static_cast<int>(group.takes.size())) return;

    group.takes.erase(group.takes.begin() + takeIndex);

    for (CompSegment& segment : group.segments) {
        if (segment.takeIndex == takeIndex) {
            segment.takeIndex = -1;             // becomes a hole
        } else if (segment.takeIndex > takeIndex) {
            --segment.takeIndex;
        }
    }
    normalise(group);
}

// How much of the comp each take actually won, for a lane display that says
// what is being used rather than just what was recorded.
inline float takeCoverage(const CompGroup& group, int takeIndex) {
    float total = 0.0f;
    for (const CompSegment& segment : group.segments) {
        if (segment.takeIndex == takeIndex) total += segment.length();
    }
    return total;
}

/*
 * Turn the comp into clips on the arrangement.
 *
 * Replaces this group's own previous output rather than adding to it -
 * clips carry the group's index, so re-flattening after every swipe stays
 * idempotent instead of piling up.
 *
 * Each segment becomes one clip trimmed into its take: the take started at
 * some beat, the segment covers part of it, and the difference is where in
 * the recording to begin. Getting that offset wrong is the classic comping
 * bug - every segment plays from the top of its take, so the comp is in
 * time with nothing.
 */
inline int flattenToClips(Project& project, const CompGroup& group,
                          int groupIndex) {
    // Clear what this group emitted last time.
    project.arrangement.erase(
        std::remove_if(project.arrangement.begin(), project.arrangement.end(),
                       [groupIndex](const Clip& clip) {
                           return clip.compGroup == groupIndex;
                       }),
        project.arrangement.end());

    const float bpm = (project.bpm > 1.0f) ? project.bpm : 120.0f;
    const float secondsPerBeat = 60.0f / bpm;

    int emitted = 0;
    for (const CompSegment& segment : group.segments) {
        if (segment.takeIndex < 0) continue;
        if (segment.takeIndex >= static_cast<int>(group.takes.size())) continue;

        const Take& take = group.takes[static_cast<size_t>(segment.takeIndex)];
        if (take.muted || take.sampleId < 0) continue;

        // Clip the segment to the take: a segment can extend past a take
        // that was punched in over part of the span.
        const float from = std::max(segment.startBeat, take.startBeat);
        const float to = std::min(segment.endBeat, take.endBeat());
        if (to - from < 1e-4f) continue;

        Clip clip;
        clip.type = ClipType::Audio;
        clip.sampleId = take.sampleId;
        clip.channelIndex = group.channelIndex;
        clip.startBeat = from;
        clip.lengthBeats = to - from;
        clip.compGroup = groupIndex;

        // Where in the recording this segment begins. The take was recorded
        // starting at take.startBeat, so a segment beginning later starts
        // that far into the audio.
        clip.trimStartSeconds = (from - take.startBeat) * secondsPerBeat;
        clip.trimEndSeconds = (to - take.startBeat) * secondsPerBeat;

        // A short crossfade at every internal join. Butting two takes
        // together at a zero crossing that is not shared produces a click,
        // and a click at every comp point is the thing that makes an
        // amateur comp obvious.
        const float fade = std::min(0.02f, clip.lengthBeats * 0.25f);
        if (from > group.startBeat + 1e-4f) clip.fadeInBeats = fade;
        if (to < group.endBeat() - 1e-4f) clip.fadeOutBeats = fade;

        project.arrangement.push_back(clip);
        ++emitted;
    }
    return emitted;
}

// Flatten every group. Called after anything that could have changed one.
inline int flattenAll(Project& project) {
    int total = 0;
    for (size_t i = 0; i < project.compGroups.size(); ++i) {
        total += flattenToClips(project, project.compGroups[i],
                                static_cast<int>(i));
    }
    return total;
}

/*
 * Punch: is this beat inside the range being recorded over?
 *
 * Punch recording exists so that a good take can be repaired in one place
 * without risking the rest of it. The range being explicit - rather than
 * "wherever you happened to be armed" - is the whole safety of it.
 */
inline bool insidePunch(float beat, bool punchEnabled, float punchIn,
                        float punchOut) {
    if (!punchEnabled) return true;
    if (punchOut <= punchIn) return true;      // an empty range punches nothing
    return beat >= punchIn && beat < punchOut;
}

} // namespace comp

// ============================================================================
// Validation
// ============================================================================
inline void clampCompGroups(Project& project) {
    if (project.compGroups.size() > 32) project.compGroups.resize(32);

    for (CompGroup& group : project.compGroups) {
        group.channelIndex = std::clamp(group.channelIndex, 0,
                                        Project::MAX_CHANNELS - 1);
        if (!std::isfinite(group.startBeat)) group.startBeat = 0.0f;
        if (!std::isfinite(group.lengthBeats) || group.lengthBeats <= 0.0f) {
            group.lengthBeats = 16.0f;
        }
        group.startBeat = std::clamp(group.startBeat, 0.0f, 100000.0f);
        group.lengthBeats = std::clamp(group.lengthBeats, 0.25f, 100000.0f);

        if (static_cast<int>(group.takes.size()) > CompGroup::MAX_TAKES) {
            group.takes.resize(CompGroup::MAX_TAKES);
        }
        for (Take& take : group.takes) {
            if (take.sampleId < -1) take.sampleId = -1;
            if (take.sampleId >= project.samplePool.count()) take.sampleId = -1;
            if (!std::isfinite(take.startBeat)) take.startBeat = 0.0f;
            if (!std::isfinite(take.lengthBeats) || take.lengthBeats <= 0.0f) {
                take.lengthBeats = 1.0f;
            }
            take.startBeat = std::clamp(take.startBeat, 0.0f, 100000.0f);
            take.lengthBeats = std::clamp(take.lengthBeats, 0.01f, 100000.0f);
        }

        // A segment naming a take that is gone would index off the end of
        // the vector during flattening.
        const int takeCount = static_cast<int>(group.takes.size());
        for (CompSegment& segment : group.segments) {
            if (!std::isfinite(segment.startBeat)) segment.startBeat = 0.0f;
            if (!std::isfinite(segment.endBeat)) segment.endBeat = segment.startBeat;
            if (segment.takeIndex < -1 || segment.takeIndex >= takeCount) {
                segment.takeIndex = -1;
            }
        }
        comp::normalise(group);
    }
}

} // namespace ChiptuneTracker
