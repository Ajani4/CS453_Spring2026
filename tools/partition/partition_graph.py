#!/usr/bin/env python3
"""
partition_graph.py
Partitions a graph JSON across N MPI ranks using contiguous or round-robin strategy.
Emits a partition JSON with per-rank owned nodes, local edges, and cross-rank edges.
"""

import json
import argparse
import sys
from collections import defaultdict


def partition_contiguous(N: int, num_ranks: int) -> dict:
    """Assigns node i to rank floor(i * num_ranks / N). Produces contiguous blocks."""
    return {node: node * num_ranks // N for node in range(N)}


def partition_round_robin(N: int, num_ranks: int) -> dict:
    """Assigns node i to rank i % num_ranks. Interleaves nodes across ranks."""
    return {node: node % num_ranks for node in range(N)}


def build_partition(graph: dict, num_ranks: int, strategy: str) -> dict:
    N = graph["num_nodes"]

    if strategy == "contiguous":
        owner = partition_contiguous(N, num_ranks)
    elif strategy == "round-robin":
        owner = partition_round_robin(N, num_ranks)
    else:
        print(f"Unknown strategy '{strategy}', defaulting to contiguous.", file=sys.stderr)
        owner = partition_contiguous(N, num_ranks)

    # Build per-rank data
    ranks = {
        r: {"owned_nodes": [], "local_edges": [], "cross_edges": []}
        for r in range(num_ranks)
    }

    for node in range(N):
        r = owner[node]
        ranks[r]["owned_nodes"].append(node)
        for edge in graph["adjacency"].get(str(node), []):
            dst = edge["to"]
            w = edge["weight"]
            dst_rank = owner[dst]
            if dst_rank == r:
                ranks[r]["local_edges"].append({"from": node, "to": dst, "weight": w})
            else:
                ranks[r]["cross_edges"].append(
                    {"from": node, "to": dst, "weight": w, "dst_rank": dst_rank}
                )

    # Compute edge cut
    edge_cuts = sum(len(ranks[r]["cross_edges"]) for r in range(num_ranks))

    return {
        "num_nodes": N,
        "num_ranks": num_ranks,
        "strategy": strategy,
        "edge_cuts": edge_cuts,
        "owner_map": {str(k): v for k, v in owner.items()},
        "ranks": {str(r): ranks[r] for r in range(num_ranks)},
    }


def print_stats(part: dict):
    num_ranks = part["num_ranks"]
    print(f"Partitioned {part['num_nodes']} nodes across {num_ranks} ranks "
          f"(strategy: {part['strategy']})")
    print(f"Total edge cuts: {part['edge_cuts']}")
    for r in range(num_ranks):
        rd = part["ranks"][str(r)]
        n_owned = len(rd["owned_nodes"])
        n_local = len(rd["local_edges"])
        n_cross = len(rd["cross_edges"])
        print(f"  Rank {r}: {n_owned:4d} nodes | {n_local:5d} local edges | {n_cross:4d} cross edges")


def main():
    parser = argparse.ArgumentParser(description="Partition graph JSON across MPI ranks")
    parser.add_argument("graph_json", help="Input graph JSON (from export_graph.py)")
    parser.add_argument("output_json", help="Output partition JSON")
    parser.add_argument("--ranks", type=int, default=4, help="Number of MPI ranks (default: 4)")
    parser.add_argument("--strategy", choices=["contiguous", "round-robin"],
                        default="contiguous", help="Partitioning strategy (default: contiguous)")
    args = parser.parse_args()

    with open(args.graph_json) as f:
        graph = json.load(f)

    if args.ranks < 1:
        print("ERROR: --ranks must be >= 1", file=sys.stderr)
        sys.exit(1)
    if args.ranks > graph["num_nodes"]:
        print(f"ERROR: more ranks ({args.ranks}) than nodes ({graph['num_nodes']})", file=sys.stderr)
        sys.exit(1)

    part = build_partition(graph, args.ranks, args.strategy)

    with open(args.output_json, "w") as f:
        json.dump(part, f, indent=2)

    print_stats(part)
    print(f"Partition saved to {args.output_json}")


if __name__ == "__main__":
    main()
