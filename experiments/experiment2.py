#!/usr/bin/env python3
"""
experiment2.py
Experiment 2: Algorithm performance on small (~50 nodes) vs large (~300 nodes) graphs.
Compares leader election and Dijkstra in terms of iterations, messages, and runtime.

Run from the project root:
    python experiments/experiment2.py
"""

import subprocess
import json
import sys
import os
import re
import time

GRAPH_EXPORT = "tools/graph_export/export_graph.py"
PARTITION    = "tools/partition/partition_graph.py"

def find_mpi_binary():
    candidates = [
        os.path.join("build", "Debug", "ngs_mpi.exe"),
        os.path.join("build", "Release", "ngs_mpi.exe"),
        os.path.join("build", "ngs_mpi"),
        os.path.join("mpi_runtime", "build", "Debug", "ngs_mpi.exe"),
        os.path.join("mpi_runtime", "build", "ngs_mpi"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None

MPI_BIN = find_mpi_binary()

CONFIGS = [
    {
        "dot":   os.path.join("outputs", "small.ngs.dot"),
        "label": "small",
        "ranks": 4,
        "rounds": 100,
        "source": 0,
    },
    {
        "dot":   os.path.join("outputs", "large.ngs.dot"),
        "label": "large",
        "ranks": 4,
        "rounds": 400,
        "source": 0,
    },
]
RESULTS = []


def run(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode, result.stdout, result.stderr


def parse_metrics(stdout, algo_name):
    """Extract iteration count and total message count from ngs_mpi output."""
    iters = None
    msgs  = None
    in_block = False
    for line in stdout.splitlines():
        if algo_name in line and "Metrics" in line:
            in_block = True
        if in_block:
            m = re.search(r"Iterations\s*:\s*(\d+)", line)
            if m:
                iters = int(m.group(1))
            m = re.search(r"Total msgs\s*:\s*(\d+)", line)
            if m:
                msgs = int(m.group(1))
                in_block = False
    return iters, msgs


def run_experiment(cfg):
    label  = cfg["label"]
    dot    = cfg["dot"]
    ranks  = cfg["ranks"]
    rounds = cfg["rounds"]
    source = cfg["source"]

    print(f"\n[{label}] {dot}  ranks={ranks}")

    graph_json = f"outputs/exp2_{label}_graph.json"
    part_json  = f"outputs/exp2_{label}_part.json"
    dist_out   = f"outputs/exp2_{label}_distances.csv"

    # Export
    rc, out, err = run([sys.executable, GRAPH_EXPORT, dot, graph_json, "--seed", "42"])
    if rc != 0:
        print(f"  ERROR exporting: {err[:200]}")
        return

    with open(graph_json) as f:
        g = json.load(f)
    num_nodes = g["num_nodes"]
    print(f"  Nodes: {num_nodes}")

    # Partition (contiguous)
    rc, out, err = run([
        sys.executable, PARTITION, graph_json, part_json,
        "--ranks", str(ranks), "--strategy", "contiguous"
    ])
    if rc != 0:
        print(f"  ERROR partitioning: {err[:200]}")
        return

    if not os.path.exists(MPI_BIN):
        print(f"  MPI binary not found at {MPI_BIN} — skipping runtime measurement")
        return

    # Run both algorithms
    t0 = time.time()
    rc, out, err = run([
        "mpiexec", "-n", str(ranks),
        MPI_BIN,
        "--part", part_json,
        "--algo", "both",
        "--source", str(source),
        "--rounds", str(rounds),
        "--output", dist_out,
    ])
    wall_ms = (time.time() - t0) * 1000

    if rc != 0:
        print(f"  MPI run failed (rc={rc}): {err[:300]}")
        return

    le_iters,  le_msgs  = parse_metrics(out, "leader_election")
    dijk_iters, dijk_msgs = parse_metrics(out, "dijkstra")

    print(f"  [LeaderElection] iters={le_iters}  msgs={le_msgs}")
    print(f"  [Dijkstra]       iters={dijk_iters}  msgs={dijk_msgs}")
    print(f"  Wall time: {wall_ms:.1f} ms")
    if os.path.exists(dist_out):
        print(f"  Distances written to {dist_out}")

    RESULTS.append({
        "label":       label,
        "num_nodes":   num_nodes,
        "ranks":       ranks,
        "le_iters":    le_iters,
        "le_msgs":     le_msgs,
        "dijk_iters":  dijk_iters,
        "dijk_msgs":   dijk_msgs,
        "wall_ms":     wall_ms,
    })


def main():
    os.makedirs("outputs", exist_ok=True)

    print("=" * 60)
    print("Experiment 2: Algorithm Scaling (Small vs Large Graph)")
    print("=" * 60)

    for cfg in CONFIGS:
        if not os.path.exists(cfg["dot"]):
            print(f"\nWARNING: {cfg['dot']} not found — skipping {cfg['label']}")
            print("  Run NetGameSim to generate graphs first.")
            continue
        run_experiment(cfg)

    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY TABLE")
    print(f"{'Graph':<8} {'Nodes':>6} {'LE iters':>9} {'LE msgs':>9} "
          f"{'Dijk iters':>11} {'Dijk msgs':>10} {'Wall(ms)':>10}")
    print("-" * 67)
    for r in RESULTS:
        print(f"{r['label']:<8} {r['num_nodes']:>6} {str(r['le_iters']):>9} "
              f"{str(r['le_msgs']):>9} {str(r['dijk_iters']):>11} "
              f"{str(r['dijk_msgs']):>10} {r['wall_ms']:>10.1f}")

    out_path = "outputs/experiment2_results.json"
    with open(out_path, "w") as f:
        json.dump(RESULTS, f, indent=2)
    print(f"\nResults saved to {out_path}")


if __name__ == "__main__":
    main()
