// test_main.cpp — Unit and integration tests for ngs_mpi
//
// Run with: mpiexec -n 4 .\build\Debug\ngs_tests.exe
//
// Tests:
//   1.  owner_map covers all nodes
//   2.  every node owned by exactly one rank
//   3.  cross-edge dst_rank != src rank
//   4.  graph loads without throwing
//   5.  leader election agrees on the same leader (integration)
//   6.  leader == max node ID in the graph
//   7.  Dijkstra: source distance is 0
//   8.  Dijkstra: known short graph produces correct distances

#include "graph.h"
#include "leader_election.h"
#include "dijkstra.h"
#include "metrics.h"

#include <mpi.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <limits>

// ---- Minimal test framework ----
static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            fprintf(stderr, "[FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); \
            g_failed++;                                              \
        } else {                                                     \
            printf("[PASS] %s\n", msg);                             \
            g_passed++;                                              \
        }                                                            \
    } while (0)

// ---- Helpers: write small test graphs to temp files ----

/// Write a tiny 4-node triangle + extra node partition JSON for rank 0
static std::string write_triangle_partition(int num_ranks) {
    // Graph: 0-1 (w=1), 1-2 (w=2), 2-0 (w=3), 2-3 (w=1)
    // Undirected, so edges in both directions.
    // Partition: contiguous — rank r owns node r (for num_ranks <= 4)
    int N = 4;
    std::string path = "test_triangle_part.json";

    // Build owner map: node i -> rank i (for up to 4 ranks)
    // Adjust if num_ranks < 4: stack extra nodes on rank num_ranks-1
    auto owner = [&](int n) { return std::min(n, num_ranks - 1); };

    std::ofstream f(path);
    f << "{\n";
    f << "  \"num_nodes\": " << N << ",\n";
    f << "  \"num_ranks\": " << num_ranks << ",\n";
    f << "  \"strategy\": \"contiguous\",\n";
    f << "  \"edge_cuts\": 0,\n";
    f << "  \"owner_map\": {";
    for (int i = 0; i < N; ++i) {
        f << "\"" << i << "\": " << owner(i);
        if (i < N - 1) f << ", ";
    }
    f << "},\n";

    // Full adjacency (undirected)
    // 0: 1(w1), 2(w3)
    // 1: 0(w1), 2(w2)
    // 2: 0(w3), 1(w2), 3(w1)
    // 3: 2(w1)
    struct E { int from, to; double w; };
    std::vector<E> all_edges = {
        {0,1,1},{0,2,3},{1,0,1},{1,2,2},{2,0,3},{2,1,2},{2,3,1},{3,2,1}
    };

    f << "  \"ranks\": {\n";
    for (int r = 0; r < num_ranks; ++r) {
        f << "    \"" << r << "\": {\n";
        // Owned nodes
        f << "      \"owned_nodes\": [";
        bool first = true;
        for (int n = 0; n < N; ++n) {
            if (owner(n) == r) {
                if (!first) f << ", ";
                f << n;
                first = false;
            }
        }
        f << "],\n";
        // Local and cross edges
        f << "      \"local_edges\": [";
        first = true;
        for (const auto& e : all_edges) {
            if (owner(e.from) == r && owner(e.to) == r) {
                if (!first) f << ", ";
                f << "{\"from\":" << e.from << ",\"to\":" << e.to
                  << ",\"weight\":" << e.w << "}";
                first = false;
            }
        }
        f << "],\n";
        f << "      \"cross_edges\": [";
        first = true;
        for (const auto& e : all_edges) {
            if (owner(e.from) == r && owner(e.to) != r) {
                if (!first) f << ", ";
                f << "{\"from\":" << e.from << ",\"to\":" << e.to
                  << ",\"weight\":" << e.w << ",\"dst_rank\":" << owner(e.to) << "}";
                first = false;
            }
        }
        f << "]\n";
        f << "    }";
        if (r < num_ranks - 1) f << ",";
        f << "\n";
    }
    f << "  }\n}\n";
    return path;
}

// ============================================================
int main(int argc, char* argv[]) {
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_SINGLE, &provided);

    int my_rank, nranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    if (my_rank == 0) {
        printf("=== ngs_mpi Test Suite (%d ranks) ===\n\n", nranks);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    // ---- Write test partition (all ranks see same file) ----
    std::string part_path;
    if (my_rank == 0) {
        part_path = write_triangle_partition(nranks);
    }
    // Broadcast path length + path
    int plen = (int)part_path.size();
    MPI_Bcast(&plen, 1, MPI_INT, 0, MPI_COMM_WORLD);
    part_path.resize(plen);
    MPI_Bcast(part_path.data(), plen, MPI_CHAR, 0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);

    // ---- Load partition ----
    GraphPartition gp;
    bool load_ok = true;
    try {
        gp = load_partition(part_path, my_rank);
    } catch (const std::exception& ex) {
        fprintf(stderr, "[Rank %d] Load error: %s\n", my_rank, ex.what());
        load_ok = false;
    }

    // Test 4: graph loads without throwing
    if (my_rank == 0) {
        CHECK(load_ok, "T4: partition file loads without exception");
    }

    if (!load_ok) {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Test 1: owner_map covers all nodes
    if (my_rank == 0) {
        bool covers_all = true;
        for (int n = 0; n < gp.num_nodes; ++n) {
            if (gp.owner_map.find(n) == gp.owner_map.end()) {
                covers_all = false;
                break;
            }
        }
        CHECK(covers_all, "T1: owner_map covers all node IDs 0..N-1");
    }

    // Test 2: every node owned by exactly one rank
    // Gather owned node counts
    {
        std::vector<int> owned_count(gp.num_nodes, 0);
        for (int n : gp.owned_nodes) owned_count[n]++;

        std::vector<int> global_count(gp.num_nodes, 0);
        MPI_Reduce(owned_count.data(), global_count.data(),
                   gp.num_nodes, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

        if (my_rank == 0) {
            bool exactly_one = true;
            for (int n = 0; n < gp.num_nodes; ++n) {
                if (global_count[n] != 1) { exactly_one = false; break; }
            }
            CHECK(exactly_one, "T2: each node owned by exactly one rank");
        }
    }

    // Test 3: cross-edge dst_rank != my_rank
    if (my_rank == 0) {  // Check rank 0's cross edges
        bool cross_ok = true;
        for (int n : gp.owned_nodes) {
            const auto it = gp.adjacency.find(n);
            if (it == gp.adjacency.end()) continue;
            for (const Edge& e : it->second) {
                if (e.dst_rank >= 0 && e.dst_rank == my_rank) {
                    cross_ok = false;
                }
            }
        }
        CHECK(cross_ok, "T3: cross-edge dst_rank always != src rank");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // ---- Leader election tests (integration) ----
    Metrics le_metrics;
    int leader = run_leader_election(gp, le_metrics, /*max_rounds=*/50);

    // Test 5: all ranks agree on the same leader
    {
        int min_leader = 0, max_leader = 0;
        MPI_Allreduce(&leader, &min_leader, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
        MPI_Allreduce(&leader, &max_leader, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
        if (my_rank == 0) {
            CHECK(min_leader == max_leader,
                  "T5: all ranks agree on the same leader ID");
        }
    }

    // Test 6: leader == max node ID (for a graph with IDs 0..N-1, max is N-1)
    if (my_rank == 0) {
        CHECK(leader == gp.num_nodes - 1,
              "T6: elected leader equals max node ID in graph");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // ---- Dijkstra tests ----
    Metrics dijk_metrics;
    auto dist = run_dijkstra(gp, /*source=*/0, dijk_metrics);

    // Test 7: source node distance == 0
    if (my_rank == gp.owner_map.at(0)) {
        auto it = dist.find(0);
        bool src_zero = (it != dist.end() && std::abs(it->second) < 1e-9);
        // Only rank 0 can print reliably; send result there
        int ok = src_zero ? 1 : 0;
        if (my_rank != 0) {
            MPI_Send(&ok, 1, MPI_INT, 0, 99, MPI_COMM_WORLD);
        } else {
            CHECK(src_zero, "T7: source node has distance 0");
        }
    } else if (my_rank == 0) {
        int ok = 0;
        MPI_Recv(&ok, 1, MPI_INT, gp.owner_map.at(0), 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        CHECK(ok == 1, "T7: source node has distance 0");
    }

    // Test 8: known distances on the triangle graph
    // Graph: 0-1(1), 1-2(2), 0-2(3), 2-3(1)  — undirected
    // From source 0:  d[0]=0, d[1]=1, d[2]=3, d[3]=4   (0->1->2->3)
    // (Note: 0->2 direct is 3 == 0->1->2 which is also 3, so d[2]=3 either way)
    {
        struct KnownDist { int node; double expected; };
        std::vector<KnownDist> known = {{0,0.0},{1,1.0},{2,3.0},{3,4.0}};

        for (const auto& kd : known) {
            int owner = gp.owner_map.at(kd.node);
            int ok = 0;
            if (my_rank == owner) {
                auto it = dist.find(kd.node);
                ok = (it != dist.end() && std::abs(it->second - kd.expected) < 1e-6) ? 1 : 0;
                if (owner != 0) {
                    MPI_Send(&ok, 1, MPI_INT, 0, 100 + kd.node, MPI_COMM_WORLD);
                }
            }
            if (my_rank == 0) {
                if (owner != 0) {
                    MPI_Recv(&ok, 1, MPI_INT, owner, 100 + kd.node,
                             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                char msg[64];
                snprintf(msg, sizeof(msg),
                         "T8: d[%d] == %.1f from source 0", kd.node, kd.expected);
                CHECK(ok == 1, msg);
            }
            MPI_Barrier(MPI_COMM_WORLD);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // ---- Summary (rank 0 only) ----
    if (my_rank == 0) {
        printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
        if (g_failed > 0) {
            printf("SOME TESTS FAILED\n");
        } else {
            printf("ALL TESTS PASSED\n");
        }
    }

    // Clean up temp file
    if (my_rank == 0) {
        std::remove(part_path.c_str());
    }

    MPI_Finalize();
    return (g_failed > 0) ? 1 : 0;
}
