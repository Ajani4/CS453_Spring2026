#pragma once
// metrics.h — Collects runtime statistics for each algorithm pass

#include <cstdint>
#include <string>

struct Metrics {
    std::string algorithm;
    uint64_t    msg_count   = 0;  /// Total MPI messages sent by this rank
    uint64_t    bytes_sent  = 0;  /// Total bytes sent by this rank
    int         iterations  = 0;  /// Algorithm rounds / Dijkstra iterations
    double      runtime_ms  = 0;  /// Wall-clock time in milliseconds

    /// Print a one-line summary (only from rank 0).
    void print_summary(int my_rank, int num_ranks) const;

    /// Reduce across all ranks and print aggregate (call from all ranks).
    void reduce_and_print(int my_rank) const;
};
