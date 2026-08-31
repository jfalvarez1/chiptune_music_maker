#pragma once

/*
 * ChiptuneTracker - Aux bus routing
 *
 * A send is a copy of a channel's signal delivered somewhere else at its own
 * level - one reverb shared by six channels instead of six reverbs. An aux
 * bus receives those copies, runs its own insert rack over the sum, and
 * feeds the master or another bus.
 *
 * Buses feeding buses is where this gets dangerous: A into B into A is a
 * loop the audio thread would either spin on or fill with garbage. So the
 * graph is checked, not trusted. Three functions here, all pure:
 *
 *   wouldCreateCycle  - asked BEFORE a routing change, so the UI can refuse
 *   computeBusOrder   - a processing order where every bus runs after the
 *                       buses feeding it, computed once when routing changes
 *   breakRoutingCycles - repairs a file that already contains a loop
 *
 * The audio thread never inspects the graph; it walks the precomputed order.
 * Pure and ImGui-free, so every shape of graph is testable.
 */

#include "Types.h"

#include <array>

namespace ChiptuneTracker {

// A bus output of -1 means the master. Anything else is an aux index.
inline constexpr int ROUTE_TO_MASTER = -1;

/*
 * Would routing `from` into `candidate` close a loop?
 *
 * Walks the chain forward from the proposed destination looking for the
 * origin. Bounded by the bus count, so a graph that is ALREADY cyclic
 * cannot make this spin.
 */
inline bool wouldCreateCycle(const std::array<AuxBusConfig, Project::MAX_AUX_BUSES>& buses,
                             int from, int candidate) {
    if (from < 0 || from >= Project::MAX_AUX_BUSES) return false;
    if (candidate == ROUTE_TO_MASTER) return false;          // master ends everything
    if (candidate < 0 || candidate >= Project::MAX_AUX_BUSES) return false;
    if (candidate == from) return true;                      // straight into itself

    int step = candidate;
    for (int guard = 0; guard < Project::MAX_AUX_BUSES + 1; ++guard) {
        if (step == ROUTE_TO_MASTER) return false;
        if (step < 0 || step >= Project::MAX_AUX_BUSES) return false;
        if (step == from) return true;
        step = buses[static_cast<size_t>(step)].output;
    }
    // Ran out of steps without reaching the master: the existing graph is
    // already looped, so adding to it certainly does not help.
    return true;
}

/*
 * A processing order in which every bus comes after the buses feeding it.
 *
 * Kahn's algorithm over a graph where each node has at most one outgoing
 * edge. Returns false and writes nothing usable if a cycle exists - the
 * caller is expected to have refused or repaired it by then.
 */
inline bool computeBusOrder(const std::array<AuxBusConfig, Project::MAX_AUX_BUSES>& buses,
                            int* orderOut, int& countOut) {
    countOut = 0;
    if (orderOut == nullptr) return false;

    int indegree[Project::MAX_AUX_BUSES] = {};
    for (int i = 0; i < Project::MAX_AUX_BUSES; ++i) {
        const int out = buses[static_cast<size_t>(i)].output;
        if (out >= 0 && out < Project::MAX_AUX_BUSES && out != i) {
            ++indegree[out];
        }
    }

    bool placed[Project::MAX_AUX_BUSES] = {};
    for (int pass = 0; pass < Project::MAX_AUX_BUSES; ++pass) {
        int next = -1;
        for (int i = 0; i < Project::MAX_AUX_BUSES; ++i) {
            if (!placed[i] && indegree[i] == 0) { next = i; break; }
        }
        if (next < 0) break;   // everything left is in a cycle

        placed[next] = true;
        orderOut[countOut++] = next;

        const int out = buses[static_cast<size_t>(next)].output;
        if (out >= 0 && out < Project::MAX_AUX_BUSES && out != next) {
            --indegree[out];
        }
    }

    return countOut == Project::MAX_AUX_BUSES;
}

/*
 * Repair a graph that already contains a loop.
 *
 * A project file can carry one - hand-edited, or written by a future
 * version with more buses. Offending buses are routed to the master rather
 * than dropped: losing a send is recoverable, an audio thread walking a
 * loop is not. Returns how many were repaired so the caller can say so.
 */
inline int breakRoutingCycles(std::array<AuxBusConfig, Project::MAX_AUX_BUSES>& buses) {
    int repaired = 0;

    for (int i = 0; i < Project::MAX_AUX_BUSES; ++i) {
        int& output = buses[static_cast<size_t>(i)].output;

        // Out-of-range destinations are meaningless; treat as master.
        if (output != ROUTE_TO_MASTER &&
            (output < 0 || output >= Project::MAX_AUX_BUSES)) {
            output = ROUTE_TO_MASTER;
            ++repaired;
            continue;
        }
        if (output == i) {                    // feeding itself
            output = ROUTE_TO_MASTER;
            ++repaired;
            continue;
        }
    }

    // Now break any remaining multi-bus loops, lowest index first so the
    // repair is deterministic rather than dependent on traversal order.
    int order[Project::MAX_AUX_BUSES];
    int count = 0;
    while (!computeBusOrder(buses, order, count)) {
        bool broke = false;
        bool placed[Project::MAX_AUX_BUSES] = {};
        for (int i = 0; i < count; ++i) placed[order[i]] = true;

        for (int i = 0; i < Project::MAX_AUX_BUSES; ++i) {
            if (!placed[i]) {                 // this one is inside a cycle
                buses[static_cast<size_t>(i)].output = ROUTE_TO_MASTER;
                ++repaired;
                broke = true;
                break;
            }
        }
        if (!broke) break;                    // nothing left to break
    }

    return repaired;
}

} // namespace ChiptuneTracker
