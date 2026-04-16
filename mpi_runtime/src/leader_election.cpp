// leader_election.cpp — FloodMax distributed leader election
//
// Each node starts as its own candidate. Every synchronous round:
//   1. Local neighbors are updated directly.
//   2. Cross-rank messages carry (dst_node, candidate) pairs.
//   3. Receiving rank updates candidate[dst_node] = max(current, received).
// After max_rounds rounds, every rank computes the global max via MPI_Allreduce.
// A convergence check allows early exit.

#include "leader_election.h"
#include <mpi.h>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>

int run_leader_election(const GraphPartition& gp, Metrics& metrics, int max_rounds) {
    metrics.algorithm = "leader_election";
    auto wall_start = std::chrono::high_resolution_clock::now();

    const int nranks   = gp.num_ranks;
    const int my_rank  = gp.my_rank;

    // candidate[node] = current best leader candidate known to this rank for 'node'
    std::unordered_map<int, int> candidate;
    candidate.reserve(gp.owned_nodes.size());
    for (int n : gp.owned_nodes) {
        candidate[n] = n;  // Each node starts as its own candidate
    }

    for (int round = 0; round < max_rounds; round++) {
        //    local propagation 
        // For local edges, update neighbor's candidate directly.
        // We do two sweeps so changes propagate within a round.
        for (int pass = 0; pass < 2; ++pass) {
            for (int n : gp.owned_nodes) {
                int cand = candidate[n];
                const auto it = gp.adjacency.find(n);
                if (it == gp.adjacency.end()) continue;
                for (const Edge& e : it->second) {
                    if (e.dst_rank < 0) {
                        // Local neighbor owned by this rank
                        auto nbr_it = candidate.find(e.to);
                        if (nbr_it != candidate.end() && cand > nbr_it->second) {
                            nbr_it->second = cand;
                        }
                    }
                }
            }
        }

        //    build cross-rank messages 
        // Message layout per rank: [dst_node, candidate, dst_node, candidate, ...]
        std::vector<std::vector<int>> outgoing(nranks);

        for (int n : gp.owned_nodes) {
            int cand = candidate[n];
            const auto it = gp.adjacency.find(n);
            if (it == gp.adjacency.end()) continue;
            for (const Edge& e : it->second) {
                if (e.dst_rank >= 0 && e.dst_rank != my_rank) {
                    outgoing[e.dst_rank].push_back(e.to);
                    outgoing[e.dst_rank].push_back(cand);
                }
            }
        }

        //   exchange message sizes 
        std::vector<int> send_counts(nranks, 0);
        for (int r = 0; r < nranks; ++r) {
            send_counts[r] = static_cast<int>(outgoing[r].size());
        }
        std::vector<int> recv_counts(nranks, 0);
        MPI_Alltoall(send_counts.data(), 1, MPI_INT,
                     recv_counts.data(), 1, MPI_INT,
                     MPI_COMM_WORLD);
        // MPI_Alltoall counts as one collective (all ranks participate)
        metrics.msg_count++;
        metrics.bytes_sent += static_cast<uint64_t>(nranks) * sizeof(int);

        //    Isend / Irecv data 
        std::vector<MPI_Request> reqs;
        reqs.reserve(2 * nranks);

        std::vector<std::vector<int>> recv_bufs(nranks);
        for (int r = 0; r < nranks; ++r) {
            if (recv_counts[r] > 0) {
                recv_bufs[r].resize(recv_counts[r]);
                MPI_Request req;
                MPI_Irecv(recv_bufs[r].data(), recv_counts[r], MPI_INT,
                          r, /*tag=*/10 + round % 1000, MPI_COMM_WORLD, &req);
                reqs.push_back(req);
            }
        }
        for (int r = 0; r < nranks; ++r) {
            if (!outgoing[r].empty()) {
                MPI_Request req;
                MPI_Isend(outgoing[r].data(), static_cast<int>(outgoing[r].size()),
                          MPI_INT, r, /*tag=*/10 + round % 1000, MPI_COMM_WORLD, &req);
                reqs.push_back(req);
                metrics.msg_count++;
                metrics.bytes_sent += outgoing[r].size() * sizeof(int);
            }
        }
        if (!reqs.empty()) {
            MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);
        }

        //    apply received updates 
        bool changed_local = false;
        for (int r = 0; r < nranks; ++r) {
            const auto& buf = recv_bufs[r];
            for (int i = 0; i + 1 < static_cast<int>(buf.size()); i += 2) {
                int dst_node = buf[i];
                int cand     = buf[i + 1];
                auto it = candidate.find(dst_node);
                if (it != candidate.end() && cand > it->second) {
                    it->second = cand;
                    changed_local = true;
                }
            }
        }

        //   convergence check 
        int local_changed  = changed_local ? 1 : 0;
        int global_changed = 0;
        MPI_Allreduce(&local_changed, &global_changed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
        metrics.msg_count++;
        metrics.iterations++;

        if (global_changed == 0) {
            if (my_rank == 0) {
                printf("[LeaderElection] Converged after %d/%d rounds.\n",
                       round + 1, max_rounds);
            }
            break;
        }
    }

    //  Determine leader: global max candidate 
    int local_leader = 0;
    for (const auto& [n, c] : candidate) {
        if (c > local_leader) local_leader = c;
    }

    int global_leader = 0;
    MPI_Allreduce(&local_leader, &global_leader, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    metrics.msg_count++;

    //  Verify agreement: every owned node must agree on global_leader 
    int local_agree = 1;
    for (const auto& [n, c] : candidate) {
        if (c != global_leader) {
            local_agree = 0;
            break;
        }
    }
    int global_agree = 0;
    MPI_Allreduce(&local_agree, &global_agree, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    auto wall_end = std::chrono::high_resolution_clock::now();
    metrics.runtime_ms =
        std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    if (my_rank == 0) {
        printf("[LeaderElection] Leader = %d | Agreement = %s\n",
               global_leader, global_agree ? "YES" : "NO (increase --rounds)");
    }

    return global_leader;
}
