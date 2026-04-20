# REPORT.md - CS453 Spring 2026 Project Report
**NetGameSim to MPI Distributed Algorithms**

---

## 1. Approach and Overall Idea

The goal of this project is to build a complete end-to-end pipeline that takes a synthetically generated network graph, distributes it across multiple MPI processes, and runs two classic distributed algorithms on it: leader election and single-source shortest paths.

The central design idea is that graph nodes and MPI ranks are decoupled Rather than assigning one node per process, each MPI rank owns a subset of graph nodes. This mirrors how real distributed systems work as a cluster of machines each managing a portion of a larger logical data set.

The pipeline has four distinct stages:

1. **Graph generation**: Use NetGameSim to produce a random connected graph with realistic structure. NetGameSim models a network as a stateful system where each node has internal properties, making the generated graphs non-trivial and varied.

2. **Graph enrichment and export**: Parse the NetGameSim `.dot` output, remap node IDs to a contiguous range, reflect directed edges to make the graph undirected (required for global leader election), enforce connectivity, and serialize to a portable JSON format.

3. **Partitioning**: Divide the nodes across N MPI ranks. Emit a partition JSON that encodes which rank owns each node, what edges are local (both endpoints on the same rank), and what edges are cross-rank (requiring MPI messages).

4. **MPI runtime**: Load the partition on each rank and run FloodMax leader election and distributed Dijkstra. Collect metrics and write results.

The key insight driving the design is that by doing all graph structure analysis in Python before the MPI program starts, the C++ runtime can focus entirely on distributed algorithm logic rather than mixing graph parsing with algorithm execution.

---

## 2. Implementation Details

### Graph Export (`tools/graph_export/export_graph.py`)

The export tool accepts either a `.dot` file directly or a `.conf` config file (which specifies the dot file path and seed). It performs the following steps:

- **Parsing**: Extracts edges and weights from the NetGameSim `.dot` format using regex. NetGameSim encodes edge weights as `["weight"="3.0"]` attributes.
- **Node ID remapping**: NetGameSim node IDs are not always contiguous integers starting from 0. We sort all node IDs and remap them to `[0, N-1]`. This lets the C++ runtime use node IDs as direct map keys without gaps.
- **Undirected conversion**: NetGameSim outputs directed edges. We reflect every edge `u → v` as `v → u` as well, keeping the minimum weight on any duplicate. This is necessary because FloodMax requires every node to be reachable from every other node.
- **Connectivity enforcement**: NetGameSim uses a probabilistic edge model (`edgeProbability` in `application.conf`). Low probability settings can produce disconnected components. We detect components via BFS and add minimal bridge edges (weight 1) between adjacent components to guarantee connectivity.
- **Seed storage**: The seed used for any random decisions is stored in the output JSON so runs are reproducible.

Output format (`outputs/graph.json`):
```json
{
  "num_nodes": 51,
  "seed": 42,
  "directed": false,
  "adjacency": {
    "0": [{"to": 1, "weight": 3.0}, ...],
    ...
  }
}
```

### Partitioning (`tools/partition/partition_graph.py`)

The partitioner reads the graph JSON and assigns every node to exactly one rank. It supports two strategies:

- **Contiguous**: Node `i` goes to rank `floor(i * R / N)`. Produces contiguous blocks of node IDs per rank. Benefits from spatial locality when nearby node IDs share edges (common in NetGameSim graphs since nodes are added sequentially).
- **Round-robin**: Node `i` goes to rank `i % R`. Distributes nodes more evenly across ranks but interleaves them, typically increasing edge cuts.

For each rank, the partition records:
- `owned_nodes`: list of node IDs this rank is responsible for
- `local_edges`: edges where both endpoints are on this rank (no MPI needed)
- `cross_edges`: edges where the destination is on a different rank (requiring MPI send), including the destination rank

The `owner_map` encodes the full node-to-rank mapping and is used by both the partitioner and the C++ runtime.

Output format (`outputs/part.json`):
```json
{
  "num_nodes": 51,
  "num_ranks": 8,
  "owner_map": {"0": 0, "1": 0, ...},
  "ranks": {
    "0": {
      "owned_nodes": [0, 1, 2, ...],
      "local_edges": [{"from": 0, "to": 1, "weight": 3.0}],
      "cross_edges": [{"from": 0, "to": 7, "weight": 2.0, "dst_rank": 1}]
    }
  }
}
```

### MPI Runtime (`mpi_runtime/src/`)

The runtime is written in C++17 and uses MS-MPI on Windows / OpenMPI on Linux. It is split into focused modules:

- **`graph.cpp`**: Loads the partition JSON using the `nlohmann/json` single-header library. Validates required fields, checks rank bounds, and populates the `GraphPartition` struct. Emits warnings for non-positive edge weights.
- **`leader_election.cpp`**: Implements FloodMax. See algorithm section below.
- **`dijkstra.cpp`**: Implements distributed Dijkstra with global minimum selection. See algorithm section below.
- **`metrics.cpp`**: Tracks message count, bytes sent, iteration count, and wall-clock time per algorithm. Uses `MPI_Reduce` to aggregate across ranks and prints a summary on rank 0.
- **`main.cpp`**: Parses CLI arguments (`--part`, `--graph`, `--algo`, `--source`, `--rounds`, `--output`), loads the partition, runs the requested algorithm(s), and optionally writes Dijkstra distances to a CSV file.

### MPI Message Patterns

**Leader election** uses three MPI operations per round:
1. `MPI_Alltoall`: exchange the sizes of candidate update messages between all rank pairs
2. `MPI_Isend` / `MPI_Irecv`: non-blocking send/receive of `(dst_node, candidate_id)` pairs for cross-rank edges
3. `MPI_Allreduce (MPI_MAX)`: convergence check: detect whether any rank updated any candidate this round

**Dijkstra** uses two MPI operations per iteration:
1. `MPI_Allgather`: each rank proposes its best `(distance, node)` pair; all ranks receive all proposals and identify the global minimum
2. `MPI_Bcast`: the owning rank broadcasts the relaxation list for the settled node to all other ranks

All collectives are called in the same order on all ranks to prevent deadlock. MPI error codes are checked on critical calls.

### Testing (`mpi_runtime/tests/test_main.cpp`)

11 tests run under `mpiexec -n 4`:
- T1: owner map covers all node IDs
- T2: each node owned by exactly one rank
- T3: cross-edge destination rank always differs from source rank
- T4: partition file loads without exception
- T5: all ranks agree on the same leader (integration test)
- T6: elected leader equals the maximum node ID
- T7: source node has distance 0 in Dijkstra output
- T8 (×4): known distances on a 4-node triangle graph (`d[0]=0, d[1]=1, d[2]=3, d[3]=4`)

---

## 3. Algorithm Choices

### Leader Election - FloodMax

**Choice rationale**: FloodMax is the standard textbook algorithm for leader election on a general connected graph. It is correct regardless of cycles, requires no prior knowledge of graph topology or diameter, and is straightforward to implement in a synchronous message-passing model. The alternative (ring-based algorithms like LCR) requires a ring topology that NetGameSim graphs do not have.

**Algorithm**:
- Each node initializes its candidate to its own ID.
- Each synchronous round: every node sends its current candidate to all neighbors, then updates its candidate to the maximum value received.
- After `diameter` rounds, the global maximum ID has propagated to every node. All nodes agree on this value as the leader.

**MPI mapping**: Nodes owned by the same rank update each other directly in memory (no MPI). Nodes on different ranks exchange `(dst_node, candidate_id)` pairs via non-blocking send/receive. A global convergence check (`MPI_Allreduce`) detects when no node changed its candidate, enabling early exit.

**Correctness argument**: The candidate value at any node is monotonically non-decreasing. The global maximum ID is the only value that can stabilize everywhere. Any other value will eventually be replaced by a higher one received from a neighbor. Since the graph is connected, the maximum propagates to all nodes within `diameter` rounds.

### Distributed Dijkstra - Parallel Global Minimum

**Choice rationale**: The global-minimum-selection variant of Dijkstra is the standard correct MPI baseline for distributed shortest paths. It preserves the Dijkstra invariant exactly: on each step the globally optimal unsettled node is settled. This makes correctness easy to reason about and verify. The tradeoff is two collective operations per iteration, which is acceptable at the graph scales we test.

**Algorithm**:
1. Initialize all distances to ∞ except the source (distance 0).
2. Each iteration: every rank proposes its local best unsettled `(distance, node)` pair via `MPI_Allgather`. All ranks examine the proposals and identify the global minimum.
3. The owning rank of the minimum node broadcasts its outgoing edge relaxations.
4. Every rank updates distances for its owned nodes that appear in the relaxation list.
5. Terminate when no unsettled nodes remain.

**Why not ghost nodes**: The spec mentions ghost nodes as one approach. We chose broadcast-based relaxation instead. In the global-minimum variant, the settled node's relaxation list is small (bounded by its degree) and must be communicated to potentially all ranks anyway. Broadcasting it globally is simpler, avoids maintaining stale ghost state, and is correct. The tradeoff is higher per-iteration communication volume, which we note in the limitations.

**Correctness assumption**: All edge weights must be strictly positive. Dijkstra's greedy selection is only valid when settling a node cannot decrease distances to already-settled nodes. This holds if and only if all weights are positive.

---

## 4. Experimental Hypothesis and Expected Results

### Experiment 1: Partitioning Strategy Comparison

**Hypothesis**: Contiguous partitioning will produce fewer edge cuts than round-robin on NetGameSim graphs. This is because NetGameSim assigns node IDs sequentially as the graph is built, so nodes with nearby IDs are more likely to have been added in the same phase and share edges. Grouping them into contiguous blocks keeps those edges local. Round-robin interleaves nodes from different phases, separating likely-connected nodes across ranks.

**Expected**: Contiguous → fewer edge cuts → fewer cross-rank messages → lower runtime. The difference may be small at 4 ranks on a 50-node graph but should grow on larger graphs.

### Experiment 2: Algorithm Scaling

**Hypothesis**:
- Dijkstra iterations will scale linearly with node count (N iterations for N nodes, since each iteration settles exactly one node).
- Dijkstra message counts will scale linearly with N (two collectives per iteration).
- Leader election iterations will scale with graph diameter, not node count, so a larger graph does not necessarily mean more rounds.
- Wall time will be dominated by MPI startup overhead at this scale, making both graphs appear similar in total time.

---

## 5. Actual Results and Explanation

### Experiment 1: Partitioning Strategy Comparison

| Graph | Strategy | Nodes | Edge Cuts | Runtime (ms) | Total Msgs |
|-------|----------|-------|-----------|--------------|------------|
| Small | contiguous  | 51  | 72  | 43.3  | 612  |
| Small | round-robin | 51  | 84  | 43.2  | 612  |
| Large | contiguous  | 301 | 862 | 61.8  | 3612 |
| Large | round-robin | 301 | 854 | 65.2  | 3612 |

**Explanation**:

The hypothesis was only partially confirmed and produced one surprising result. For the small graph, contiguous partitioning produced fewer edge cuts, 72 vs 84, consistent with the prediction that nearby node IDs share edges in NetGameSim graphs. However, for the large graph the result flipped, round-robin actually produced fewer edge cuts than contiguous. This suggests that at larger graph sizes, the node ID ordering in NetGameSim does not correlate as strongly with edge structure, so round-robin's even distribution of nodes across ranks can occasionally outperform contiguous blocking.

Total message counts are identical between strategies for the same graph regardless of edge cuts. This is because message counts are driven by the number of collective operations per algorithm, not by the raw number of cross-rank edges.

Runtimes are nearly identical between strategies on both graphs (43.3ms vs 43.2ms for small, 61.8ms vs 65.2ms for large), confirming that at this scale the partitioning choice does not meaningfully affect wall-clock performance.

**Key insight**: Contiguous partitioning is not always better than round-robin. On small graphs with strong sequential node ID locality it wins on edge cuts, but on larger graphs where NetGameSim's edge structure is less correlated with node ID order, round-robin can match or beat it. Neither strategy significantly affects runtime or message counts because the collective operations dominate communication cost regardless of how nodes are partitioned.

### Experiment 2: Algorithm Scaling

| Graph | Nodes | LE Iters | LE Msgs | Dijkstra Iters | Dijkstra Msgs | Wall (ms) |
|-------|-------|----------|---------|----------------|---------------|-----------|
| Small | 51    | 13       | 264     | 51             | 612           | 48.5      |
| Large | 301   | 6        | 124     | 301            | 3612          | 65.0      |

**Explanation**:

Dijkstra results matched the hypothesis exactly. Iterations scale linearly with node count (51 for 51 nodes, 301 for 301 nodes) and message counts scale proportionally (612 vs 3612, a ratio of ~5.9x matching the node count ratio of ~5.9x). This confirms the algorithm is working correctly and its complexity is as expected.

Leader election again produced a counterintuitive result: the large graph converged in only 6 rounds compared to 13 for the small graph. This means the large NetGameSim graph has a significantly smaller diameter than the small one despite having six times as many nodes. 

Wall time is close between graphs, 48.5ms vs 65.0ms, which is a much more realistic ratio than earlier runs. The 16.5ms difference reflects the cost of 250 additional Dijkstra iterations and larger broadcasts on the 301-node graph.

**Key insight**: For Dijkstra, the bottleneck is number of nodes since iterations scale exactly with N. For leader election, the bottleneck is graph diameter, not size and NetGameSim's larger graphs are denser with smaller diameters, making leader election converge faster on them despite being six times larger. This highlights how graph structure rather than raw size determines distributed algorithm performance.

---
