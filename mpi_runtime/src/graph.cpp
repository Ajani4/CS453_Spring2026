// graph.cpp — Load partition JSON into GraphPartition

#include "graph.h"
#include "json.hpp"
#include <fstream>
#include <stdexcept>
#include <iostream>

using json = nlohmann::json;

GraphPartition load_partition(const std::string& part_json, int my_rank) {
    GraphPartition gp;
    gp.my_rank = my_rank;

    std::ifstream pf(part_json);
    if (!pf.is_open()) {
        throw std::runtime_error("Cannot open partition file: " + part_json);
    }

    json part;
    try {
        pf >> part;
    } catch (const json::parse_error& e) {
        throw std::runtime_error(
            std::string("JSON parse error in partition file: ") + e.what());
    }

    // Validate required fields
    for (const auto& key : {"num_nodes", "num_ranks", "owner_map", "ranks"}) {
        if (!part.contains(key)) {
            throw std::runtime_error(
                std::string("Partition JSON missing required key: ") + key);
        }
    }

    gp.num_nodes = part["num_nodes"].get<int>();
    gp.num_ranks = part["num_ranks"].get<int>();

    if (my_rank >= gp.num_ranks) {
        throw std::runtime_error("MPI rank " + std::to_string(my_rank) +
                                 " >= num_ranks " + std::to_string(gp.num_ranks));
    }

    // Load owner map: "node_id" -> rank
    for (const auto& [k, v] : part["owner_map"].items()) {
        gp.owner_map[std::stoi(k)] = v.get<int>();
    }

    // Load this rank's partition data
    const std::string rank_key = std::to_string(my_rank);
    if (!part["ranks"].contains(rank_key)) {
        throw std::runtime_error("Partition has no data for rank " + rank_key);
    }

    const auto& rdata = part["ranks"][rank_key];

    for (const auto& n : rdata["owned_nodes"]) {
        gp.owned_nodes.push_back(n.get<int>());
    }

    // Local edges (dst_rank == -1)
    for (const auto& e : rdata["local_edges"]) {
        int from   = e["from"].get<int>();
        int to     = e["to"].get<int>();
        double w   = e["weight"].get<double>();
        if (w <= 0.0) {
            std::cerr << "WARNING: non-positive weight " << w
                      << " on edge " << from << "->" << to << "\n";
        }
        gp.adjacency[from].push_back(Edge{to, w, -1});
    }

    // Cross-rank edges
    for (const auto& e : rdata["cross_edges"]) {
        int from      = e["from"].get<int>();
        int to        = e["to"].get<int>();
        double w      = e["weight"].get<double>();
        int dst_rank  = e["dst_rank"].get<int>();
        if (w <= 0.0) {
            std::cerr << "WARNING: non-positive weight " << w
                      << " on edge " << from << "->" << to << "\n";
        }
        gp.adjacency[from].push_back(Edge{to, w, dst_rank});
    }

    // Ensure all owned nodes have an entry in adjacency (even if no edges)
    for (int n : gp.owned_nodes) {
        if (gp.adjacency.find(n) == gp.adjacency.end()) {
            gp.adjacency[n] = {};
        }
    }

    return gp;
}
