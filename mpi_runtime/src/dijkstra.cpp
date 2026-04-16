// dijkstra.cpp — Distributed Dijkstra shortest paths
//
// Algorithm: parallel Dijkstra with global minimum selection via MPI_Allgather,
// followed by MPI_Bcast of relaxation updates from the owning rank.
//
// Correctness assumptions:
//   - All edge weights are strictly positive.
//   - The graph is connected (all nodes reachable from source).
//   - Node IDs are in [0, num_nodes).

#include "dijkstra.h"
#include <mpi.h>
#include <unordered_map>
#include <queue>
#include <vector>
#include <limits>
#include <cstdio>
#include <cstring>
#include <chrono>

static constexpr double INF = std::numeric_limits<double>::infinity();

/// Packed struct used in Allgather to propose (distance, node) pairs.
struct Proposal {
    double dist;
    int    node;
};

std::unordered_map<int, double> run_dijkstra(
        const GraphPartition& gp,
        int source,
        Metrics& metrics) {

    metrics.algorithm = "dijkstra";
    auto wall_start = std::chrono::high_resolution_clock::now();

    const int nranks  = gp.num_ranks;
    const int my_rank = gp.my_rank;
    const int N       = gp.num_nodes;

    // Validate source
    if (gp.owner_map.find(source) == gp.owner_map.end()) {
        if (my_rank == 0) {
            fprintf(stderr, "[Dijkstra] ERROR: source node %d not in owner_map\n", source);
        }
        return {};
    }
    const int source_rank = gp.owner_map.at(source);

    //  Initialize distances 
    std::unordered_map<int, double> dist;
    std::unordered_map<int, bool>   settled;
    dist.reserve(gp.owned_nodes.size());
    settled.reserve(gp.owned_nodes.size());

    for (int n : gp.owned_nodes) {
        dist[n]    = INF;
        settled[n] = false;
    }
    if (my_rank == source_rank) {
        dist[source] = 0.0;
    }

    // Local priority queue: (tentative_dist, node_id)
    using PQEntry = std::pair<double, int>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;
    if (my_rank == source_rank) {
        pq.push({0.0, source});
    }

    //  Main Dijkstra loop: up to N iterations 
    for (int iter = 0; iter < N; ++iter) {

        // each rank proposes its best unsettled node 
        Proposal local_prop = {INF, -1};
        while (!pq.empty()) {
            auto [d, n] = pq.top();
            if (settled[n] || d > dist[n]) {
                pq.pop();
                continue;
            }
            local_prop = {d, n};
            break;
        }

        //  Allgather proposals — find global minimum 
        std::vector<Proposal> all_props(nranks);
        MPI_Allgather(&local_prop,      sizeof(Proposal), MPI_BYTE,
                      all_props.data(), sizeof(Proposal), MPI_BYTE,
                      MPI_COMM_WORLD);
        metrics.msg_count++;
        metrics.bytes_sent += static_cast<uint64_t>(nranks) * sizeof(Proposal);
        metrics.iterations++;

        int    global_node = -1;
        double global_dist = INF;
        int    global_owner = -1;

        for (int r = 0; r < nranks; ++r) {
            if (all_props[r].node >= 0 && all_props[r].dist < global_dist) {
                global_dist  = all_props[r].dist;
                global_node  = all_props[r].node;
                global_owner = r;
            }
        }

        if (global_node < 0) break;  // All settled or no reachable nodes left

        // owning rank settles node and computes relaxations 
        // Relaxation array: [to_node_as_double, new_dist, to_node, new_dist, ...]
        // We pack (node_id, new_dist) pairs as pairs of doubles for MPI_Bcast.
        std::vector<double> relax_buf;  // [node_as_double, dist, node_as_double, dist, ...]
        int relax_count = 0;

        if (my_rank == global_owner) {
            settled[global_node] = true;
            if (!pq.empty()) pq.pop();  // Remove the settled node from local pq

            const auto adj_it = gp.adjacency.find(global_node);
            if (adj_it != gp.adjacency.end()) {
                for (const Edge& e : adj_it->second) {
                    // Positive weight check (defensive)
                    if (e.weight <= 0.0) continue;
                    double new_dist = global_dist + e.weight;
                    relax_buf.push_back(static_cast<double>(e.to));
                    relax_buf.push_back(new_dist);
                }
            }
            relax_count = static_cast<int>(relax_buf.size());
        }

        //   broadcast relaxation count, then relaxation data 
        MPI_Bcast(&relax_count, 1, MPI_INT, global_owner, MPI_COMM_WORLD);
        metrics.msg_count++;
        metrics.bytes_sent += sizeof(int);

        if (relax_count > 0) {
            if (my_rank != global_owner) {
                relax_buf.resize(relax_count);
            }
            MPI_Bcast(relax_buf.data(), relax_count, MPI_DOUBLE, global_owner, MPI_COMM_WORLD);
            metrics.msg_count++;
            metrics.bytes_sent += static_cast<uint64_t>(relax_count) * sizeof(double);

            //  each rank applies relaxations to its owned nodes 
            for (int i = 0; i + 1 < relax_count; i += 2) {
                int    nbr      = static_cast<int>(relax_buf[i]);
                double new_dist = relax_buf[i + 1];
                auto   it       = dist.find(nbr);
                if (it != dist.end() && !settled[nbr] && new_dist < it->second) {
                    it->second = new_dist;
                    pq.push({new_dist, nbr});
                }
            }
        }
    }

    //  Report results (rank 0 gathers distances via Gatherv) 
    auto wall_end = std::chrono::high_resolution_clock::now();
    metrics.runtime_ms =
        std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    if (my_rank == 0) {
        printf("[Dijkstra] Source = %d | Completed %d iteration(s)\n",
               source, metrics.iterations);
    }

    return dist;
}
