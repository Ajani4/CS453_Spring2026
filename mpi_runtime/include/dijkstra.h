#pragma once
// dijkstra.h — Distributed Dijkstra shortest paths over a partitioned graph
//
// Assumptions for correctness:
//   - All edge weights are strictly positive (required by Dijkstra).
//   - The graph is connected so all nodes are reachable from the source.
//   - Node IDs are contiguous integers in [0, num_nodes).

#include "graph.h"
#include "metrics.h"
#include <unordered_map>

/**
 * Run distributed Dijkstra shortest paths from a source node.
 *
 * Algorithm (parallel Dijkstra with global minimum selection):
 *   Each iteration:
 *     1. Every rank proposes its local best unsettled (node, distance) pair.
 *     2. MPI_Allgather collects all proposals; global minimum is identified.
 *     3. The owning rank broadcasts relaxations for all outgoing edges of the
 *        settled node via MPI_Bcast.
 *     4. Every rank updates distances for its owned nodes.
 *   Terminates when no unsettled node remains.
 *
 * @param gp      This rank's partition data
 * @param source  Global source node ID
 * @param metrics Filled with message counts and timing
 * @return        Map from owned node ID -> shortest distance from source
 *                (INF = unreachable)
 */
std::unordered_map<int, double> run_dijkstra(
    const GraphPartition& gp,
    int source,
    Metrics& metrics);
