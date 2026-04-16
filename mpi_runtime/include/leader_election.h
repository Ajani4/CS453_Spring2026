#pragma once
// leader_election.h — Distributed FloodMax leader election over a partitioned graph
//
// Assumptions for correctness:
//   - All node IDs are unique non-negative integers.
//   - The graph is connected (undirected sense): every node can reach every other node.
//   - max_rounds >= graph diameter ensures full propagation.
//     A safe upper bound is num_nodes; typical graphs converge much earlier.

#include "graph.h"
#include "metrics.h"

/**
 * Run FloodMax distributed leader election.
 *
 * Each node starts with its own ID as its leader candidate. In each
 * synchronous round every rank:
 *   1. Propagates the maximum candidate ID to local neighbors directly.
 *   2. Sends candidate IDs along cross-rank edges via MPI point-to-point.
 *   3. Updates each owned node's candidate to the maximum received.
 * After max_rounds rounds the globally maximum node ID is the leader.
 *
 * @param gp          This rank's partition data
 * @param metrics     Filled with message counts and timing
 * @param max_rounds  Number of synchronous rounds to run
 * @return            Agreed-upon leader node ID (same on all ranks)
 */
int run_leader_election(const GraphPartition& gp, Metrics& metrics, int max_rounds);
