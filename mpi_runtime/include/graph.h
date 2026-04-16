#pragma once
// graph.h — Data structures and loader for partitioned graph in MPI runtime

#include <vector>
#include <unordered_map>
#include <string>

/// A directed edge in the local adjacency list.
struct Edge {
    int    to;       /// Destination node ID (global)
    double weight;   /// Positive edge weight
    int    dst_rank; /// Owning rank of 'to'; -1 if local (same rank as src)
};

/// All graph and partition data needed by one MPI rank.
struct GraphPartition {
    int num_nodes;  /// Total nodes across all ranks
    int num_ranks;  /// Total MPI ranks
    int my_rank;    /// This rank's ID

    std::vector<int> owned_nodes;  ///< Node IDs owned by this rank

    /// Adjacency list: node -> outgoing edges
    std::unordered_map<int, std::vector<Edge>> adjacency;

    /// Global owner map: node ID -> rank that owns it
    std::unordered_map<int, int> owner_map;
};

/**
 * Load the partition for this rank from the partition JSON file.
 * @param part_json  Path to partition JSON produced by partition_graph.py
 * @param my_rank    This process's MPI rank
 * @return           Populated GraphPartition for this rank
 * @throws std::runtime_error on file or parse error
 */
GraphPartition load_partition(const std::string& part_json, int my_rank);
