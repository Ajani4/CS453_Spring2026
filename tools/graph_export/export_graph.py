#!/usr/bin/env python3
"""
export_graph.py
Parses a NetGameSim .dot file and exports a connected, weighted graph as JSON.
Edges are made bidirectional to guarantee connectivity for leader election.
"""

import re
import json
import sys
import argparse
import random
from collections import defaultdict, deque


def parse_dot(dot_file: str):
    """Parse a NetGameSim .dot file and return (nodes, edges)."""
    nodes = set()
    edges = []

    with open(dot_file, "r") as f:
        content = f.read()

    # Parse edges: "src" -> "dst" ["weight"="w"]
    edge_pat = re.compile(r'"(\d+)"\s*->\s*"(\d+)"\s*\["weight"="([\d.]+)"\]')
    for m in edge_pat.finditer(content):
        src, dst, w = int(m.group(1)), int(m.group(2)), float(m.group(3))
        edges.append((src, dst, w))
        nodes.add(src)
        nodes.add(dst)

    # Parse standalone node declarations
    node_pat = re.compile(r'"(\d+)"\s*\[')
    for m in node_pat.finditer(content):
        nodes.add(int(m.group(1)))

    return sorted(nodes), edges


def remap_nodes(nodes):
    """Return a mapping from original IDs to contiguous 0..N-1 IDs."""
    return {old: new for new, old in enumerate(nodes)}


def build_adjacency(nodes, edges, id_map, undirected: bool, seed: int):
    """
    Build adjacency list with positive weights.
    If undirected=True, each edge is added in both directions.
    """
    random.seed(seed)
    N = len(nodes)
    adj = defaultdict(dict)  # adj[src][dst] = weight (dedup by keeping min weight)

    for src, dst, w in edges:
        s, d = id_map[src], id_map[dst]
        # Keep minimum weight on duplicate edges
        if d not in adj[s] or w < adj[s][d]:
            adj[s][d] = w
        if undirected:
            if s not in adj[d] or w < adj[d][s]:
                adj[d][s] = w

    # Ensure all nodes exist in adj even with no edges
    for i in range(N):
        if i not in adj:
            adj[i] = {}

    return adj


def is_connected(N: int, adj) -> bool:
    """BFS connectivity check."""
    if N == 0:
        return True
    visited = set()
    queue = deque([0])
    visited.add(0)
    while queue:
        node = queue.popleft()
        for nbr in adj[node]:
            if nbr not in visited:
                visited.add(nbr)
                queue.append(nbr)
    return len(visited) == N


def connect_components(N: int, adj, seed: int):
    """
    If the graph is disconnected, add minimal bridge edges between components.
    Uses BFS to find components and connects adjacent ones with weight 1.
    """
    random.seed(seed + 1)
    visited = {}
    comp_id = 0
    for start in range(N):
        if start not in visited:
            queue = deque([start])
            visited[start] = comp_id
            while queue:
                node = queue.popleft()
                for nbr in adj[node]:
                    if nbr not in visited:
                        visited[nbr] = comp_id
                        queue.append(nbr)
            comp_id += 1

    if comp_id == 1:
        return  # Already connected

    # Connect component i to component i+1 by linking one node from each
    comp_nodes = defaultdict(list)
    for node, cid in visited.items():
        comp_nodes[cid].append(node)

    for cid in range(comp_id - 1):
        u = comp_nodes[cid][0]
        v = comp_nodes[cid + 1][0]
        w = float(random.randint(1, 10))
        adj[u][v] = w
        adj[v][u] = w


def build_graph_json(dot_file: str, output_json: str, seed: int, directed: bool):
    raw_nodes, raw_edges = parse_dot(dot_file)
    id_map = remap_nodes(raw_nodes)
    N = len(raw_nodes)

    adj = build_adjacency(raw_nodes, raw_edges, id_map, undirected=not directed, seed=seed)
    connect_components(N, adj, seed)

    if not is_connected(N, adj):
        print("WARNING: graph is still not connected after bridging.", file=sys.stderr)

    # Build serializable adjacency list
    adjacency = {}
    for node in range(N):
        adjacency[str(node)] = [
            {"to": nbr, "weight": w}
            for nbr, w in sorted(adj[node].items())
        ]

    graph = {
        "num_nodes": N,
        "seed": seed,
        "directed": directed,
        "source_dot": dot_file,
        "id_remap": {str(k): v for k, v in id_map.items()},
        "adjacency": adjacency,
    }

    with open(output_json, "w") as f:
        json.dump(graph, f, indent=2)

    total_edges = sum(len(v) for v in adjacency.values())
    print(f"Exported {N} nodes, {total_edges} directed edges to {output_json}")
    print(f"Connected: {is_connected(N, adj)}")
    print(f"Seed: {seed}")
    return graph


def parse_conf(conf_path: str) -> dict:
    """Parse a simple key = value .conf file. Returns a dict of settings."""
    settings = {}
    with open(conf_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                key, _, val = line.partition("=")
                settings[key.strip()] = val.strip()
    return settings


def main():
    parser = argparse.ArgumentParser(
        description="Export NetGameSim .dot graph to JSON. "
                    "Input can be a .dot file directly or a .conf config file.")
    parser.add_argument("input", help="Input: a .dot file OR a .conf config file")
    parser.add_argument("output_json", help="Output JSON file path")
    parser.add_argument("--seed", type=int, default=None, help="Random seed (default: 42)")
    parser.add_argument("--directed", action="store_true",
                        help="Keep edges directed (default: make undirected for connectivity)")
    args = parser.parse_args()

    # Determine if input is a .conf or a .dot file
    if args.input.endswith(".conf"):
        conf = parse_conf(args.input)
        dot_file = conf.get("dot_file", "")
        if not dot_file:
            print("ERROR: .conf file missing 'dot_file' key", file=sys.stderr)
            sys.exit(1)
        seed     = args.seed if args.seed is not None else int(conf.get("seed", 42))
        directed = args.directed or conf.get("directed", "false").lower() == "true"
    else:
        dot_file = args.input
        seed     = args.seed if args.seed is not None else 42
        directed = args.directed

    build_graph_json(dot_file, args.output_json, seed, directed)


if __name__ == "__main__":
    main()
