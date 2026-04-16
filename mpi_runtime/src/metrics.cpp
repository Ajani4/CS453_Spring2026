// metrics.cpp — Metrics collection and reporting

#include "metrics.h"
#include <mpi.h>
#include <cstdio>
#include <cstdint>

void Metrics::print_summary(int my_rank, int /*num_ranks*/) const {
    if (my_rank == 0) {
        printf("\n=== Metrics: %s ===\n", algorithm.c_str());
        printf("  Iterations  : %d\n", iterations);
        printf("  Runtime     : %.3f ms\n", runtime_ms);
        printf("  Msgs sent   : %llu (this rank)\n", (unsigned long long)msg_count);
        printf("  Bytes sent  : %llu (this rank)\n", (unsigned long long)bytes_sent);
    }
}

void Metrics::reduce_and_print(int my_rank) const {
    // Reduce msg_count and bytes_sent across all ranks
    uint64_t total_msgs  = 0;
    uint64_t total_bytes = 0;
    double   max_runtime = 0;
    int      max_iters   = 0;

    MPI_Reduce(&msg_count,  &total_msgs,  1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&bytes_sent, &total_bytes, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&runtime_ms, &max_runtime, 1, MPI_DOUBLE,   MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&iterations, &max_iters,   1, MPI_INT,      MPI_MAX, 0, MPI_COMM_WORLD);

    if (my_rank == 0) {
        printf("\n=== Metrics: %s ===\n", algorithm.c_str());
        printf("  Iterations  : %d\n",    max_iters);
        printf("  Runtime     : %.3f ms\n", max_runtime);
        printf("  Total msgs  : %llu\n",  (unsigned long long)total_msgs);
        printf("  Total bytes : %llu\n",  (unsigned long long)total_bytes);
    }
}
