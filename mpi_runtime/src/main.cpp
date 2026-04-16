// main.cpp — Entry point for the MPI distributed algorithms runtime
//
// Usage:
//   mpiexec -n <ranks> ngs_mpi --part <partition.json> --algo <leader|dijkstra|both>
//                               [--source <node_id>] [--rounds <n>] [--output <file>]
//
// Exit codes: 0 = success, 1 = argument or file error, 2 = MPI or algorithm error

#include "graph.h"
#include "leader_election.h"
#include "dijkstra.h"
#include "metrics.h"

#include <mpi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <stdexcept>

//  Simple CLI argument parsing 
struct Args {
    std::string part_json;
    std::string graph_json; // optional, accepted for compatibility (--graph)
    std::string algo     = "both";
    std::string output;
    int         source   = 0;
    int         rounds   = 200;
};

static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage: mpiexec -n <ranks> %s\n"
        "  --part    <partition.json>           (required) partition file\n"
        "  --graph   <graph.json>               (optional) graph file (for reference)\n"
        "  --algo    <leader|dijkstra|both>     default: both\n"
        "  --source  <node_id>                  Dijkstra source node (default: 0)\n"
        "  --rounds  <n>                        FloodMax rounds      (default: 200)\n"
        "  --output  <file>                     write distance results to file\n",
        prog);
}

static Args parse_args(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--part") == 0 && i + 1 < argc) {
            a.part_json = argv[++i];
        } else if (std::strcmp(argv[i], "--graph") == 0 && i + 1 < argc) {
            a.graph_json = argv[++i];  // accepted for compatibility, not used 
        } else if (std::strcmp(argv[i], "--algo") == 0 && i + 1 < argc) {
            a.algo = argv[++i];
        } else if (std::strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            a.source = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
            a.rounds = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            a.output = argv[++i];
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    if (a.part_json.empty()) {
        fprintf(stderr, "ERROR: --part is required\n");
        print_usage(argv[0]);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (a.algo != "leader" && a.algo != "dijkstra" && a.algo != "both") {
        fprintf(stderr, "ERROR: --algo must be leader, dijkstra, or both\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (a.rounds < 1) {
        fprintf(stderr, "ERROR: --rounds must be >= 1\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    return a;
}

//  Write Dijkstra results to a file (rank 0 only) 
static void write_distances(
        const std::string& path,
        const std::unordered_map<int, double>& local_dist,
        const GraphPartition& gp) {

    // Gather all (node, dist) pairs to rank 0
    int my_rank = gp.my_rank;
    int nranks  = gp.num_ranks;

    // Pack local results
    std::vector<double> local_pack;  // [node_as_double, dist, ...]
    for (const auto& [n, d] : local_dist) {
        local_pack.push_back(static_cast<double>(n));
        local_pack.push_back(d);
    }
    int local_size = static_cast<int>(local_pack.size());

    // Gather sizes
    std::vector<int> sizes(nranks);
    MPI_Gather(&local_size, 1, MPI_INT, sizes.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    std::vector<int>    displs;
    std::vector<double> all_pack;
    int total = 0;

    if (my_rank == 0) {
        displs.resize(nranks);
        for (int r = 0; r < nranks; ++r) {
            displs[r] = total;
            total    += sizes[r];
        }
        all_pack.resize(total);
    }

    MPI_Gatherv(local_pack.data(), local_size, MPI_DOUBLE,
                all_pack.data(), sizes.data(), displs.data(), MPI_DOUBLE,
                0, MPI_COMM_WORLD);

    if (my_rank == 0) {
        // Sort by node ID for readability
        std::vector<std::pair<int, double>> results;
        for (int i = 0; i + 1 < total; i += 2) {
            results.push_back({static_cast<int>(all_pack[i]), all_pack[i + 1]});
        }
        std::sort(results.begin(), results.end());

        std::ofstream f(path);
        if (!f.is_open()) {
            fprintf(stderr, "WARNING: cannot write distances to %s\n", path.c_str());
            return;
        }
        f << "node,distance\n";
        for (const auto& [n, d] : results) {
            if (d == std::numeric_limits<double>::infinity()) {
                f << n << ",INF\n";
            } else {
                f << n << "," << d << "\n";
            }
        }
        printf("[Output] Distances written to %s\n", path.c_str());
    }
}


int main(int argc, char* argv[]) {
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_SINGLE, &provided);

    int my_rank, nranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    Args args = parse_args(argc, argv);

    if (my_rank == 0) {
        printf("=== NetGameSim MPI Runtime ===\n");
        printf("Ranks: %d | Algorithm: %s | Rounds: %d | Source: %d\n",
               nranks, args.algo.c_str(), args.rounds, args.source);
        printf("Partition: %s\n\n", args.part_json.c_str());
    }

    //  Load graph partition 
    GraphPartition gp;
    try {
        gp = load_partition(args.part_json, my_rank);
    } catch (const std::exception& ex) {
        fprintf(stderr, "[Rank %d] ERROR loading partition: %s\n", my_rank, ex.what());
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (nranks != gp.num_ranks) {
        if (my_rank == 0) {
            fprintf(stderr,
                "ERROR: launched with %d MPI ranks but partition was built for %d ranks.\n"
                "Re-run partition_graph.py with --ranks %d\n",
                nranks, gp.num_ranks, nranks);
        }
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Validate source node
    if (gp.owner_map.find(args.source) == gp.owner_map.end()) {
        if (my_rank == 0) {
            fprintf(stderr, "ERROR: source node %d is not in the graph (0..%d)\n",
                    args.source, gp.num_nodes - 1);
        }
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    //  Leader Election 
    if (args.algo == "leader" || args.algo == "both") {
        Metrics le_metrics;
        int leader = run_leader_election(gp, le_metrics, args.rounds);
        (void)leader;  // already printed inside
        le_metrics.reduce_and_print(my_rank);
        MPI_Barrier(MPI_COMM_WORLD);
    }

    //  Distributed Dijkstra 
    if (args.algo == "dijkstra" || args.algo == "both") {
        Metrics dijk_metrics;
        auto dist = run_dijkstra(gp, args.source, dijk_metrics);
        dijk_metrics.reduce_and_print(my_rank);

        if (!args.output.empty()) {
            write_distances(args.output, dist, gp);
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    if (my_rank == 0) {
        printf("\n=== Done ===\n");
    }

    MPI_Finalize();
    return 0;
}
