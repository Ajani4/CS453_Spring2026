#!/usr/bin/env python3
"""
experiment1.py
Experiment 1: Compare contiguous vs round-robin partitioning strategies.
Measures edge cuts and (if MPI binary is available) runtime and message counts
for both strategies on two graph sizes.

Run from the project root:
    python experiments/experiment1.py
"""

import subprocess
import json
import sys
import os
import time

GRAPH_EXPORT = "tools/graph_export/export_graph.py"
PARTITION    = "tools/partition/partition_graph.py"
MPI_BIN      = os.path.join("mpi_runtime", "build", "Debug", "ngs_mpi.exe")
if not os.path.exists(MPI_BIN):
    MPI_BIN  = os.path.join("mpi_runtime", "build", "ngs_mpi")

RANKS        = 4
CONFIGS = [
    {"dot": os.path.join("outputs", "small.ngs.dot"),  "label": "small (~50 nodes)"},
    {"dot": os.path.join("outputs", "large.ngs.dot"),  "label": "large (~300 nodes)"},
]
STRATEGIES = ["contiguous", "round-robin"]
RESULTS = []


def run(cmd):
    """Run a shell command and return (returncode, stdout, stderr)."""
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode, result.stdout, result.stderr


def partition_and_measure(dot_file, graph_json, part_json, strategy, ranks, label):
    print(f"\n  [{label}] strategy={strategy} ranks={ranks}")

    # Export graph
    rc, out, err = run([
        sys.executable, GRAPH_EXPORT,
        dot_file, graph_json, "--seed", "42"
    ])
    if rc != 0:
        print(f"    ERROR exporting graph: {err}")
        return None

    # Partition
    rc, out, err = run([
        sys.executable, PARTITION,
        graph_json, part_json,
        "--ranks", str(ranks),
        "--strategy", strategy
    ])
    if rc != 0:
        print(f"    ERROR partitioning: {err}")
        return None

    with open(part_json) as f:
        part = json.load(f)

    edge_cuts = part["edge_cuts"]
    num_nodes = part["num_nodes"]
    print(f"    Nodes={num_nodes}  Edge cuts={edge_cuts}")

    # Run MPI binary if available
    runtime_ms   = None
    total_msgs   = None
    if os.path.exists(MPI_BIN):
        t0 = time.time()
        rc, out, err = run([
            "mpiexec", "-n", str(ranks),
            MPI_BIN,
            "--part", part_json,
            "--algo", "both",
            "--source", "0",
            "--rounds", "200"
        ])
        runtime_ms = (time.time() - t0) * 1000
        if rc == 0:
            # Parse metrics from stdout
            for line in out.splitlines():
                if "Total msgs" in line:
                    try:
                        total_msgs = int(line.split(":")[-1].strip())
                    except ValueError:
                        pass
            print(f"    Runtime={runtime_ms:.1f}ms  Total msgs={total_msgs}")
        else:
            print(f"    MPI run failed (rc={rc}): {err[:200]}")

    return {
        "label":      label,
        "strategy":   strategy,
        "ranks":      ranks,
        "num_nodes":  num_nodes,
        "edge_cuts":  edge_cuts,
        "runtime_ms": runtime_ms,
        "total_msgs": total_msgs,
    }


def main():
    os.makedirs("outputs", exist_ok=True)

    print("=" * 60)
    print("Experiment 1: Partitioning Strategy Comparison")
    print("=" * 60)

    for cfg in CONFIGS:
        if not os.path.exists(cfg["dot"]):
            print(f"\nWARNING: {cfg['dot']} not found — skipping {cfg['label']}")
            print("  Run NetGameSim first to generate this graph.")
            continue

        for strategy in STRATEGIES:
            slug = cfg["label"].split()[0]
            graph_json = f"outputs/exp1_{slug}_graph.json"
            part_json  = f"outputs/exp1_{slug}_{strategy}_part.json"

            result = partition_and_measure(
                cfg["dot"], graph_json, part_json, strategy, RANKS, cfg["label"]
            )
            if result:
                RESULTS.append(result)

    # Print summary table
    print("\n" + "=" * 60)
    print("SUMMARY TABLE")
    print(f"{'Graph':<22} {'Strategy':<14} {'Nodes':>6} {'EdgeCuts':>10} {'Runtime(ms)':>12} {'TotalMsgs':>10}")
    print("-" * 78)
    for r in RESULTS:
        rt  = f"{r['runtime_ms']:.1f}" if r['runtime_ms'] else "N/A"
        msg = str(r['total_msgs']) if r['total_msgs'] else "N/A"
        print(f"{r['label']:<22} {r['strategy']:<14} {r['num_nodes']:>6} "
              f"{r['edge_cuts']:>10} {rt:>12} {msg:>10}")

    # Save results JSON
    out_path = "outputs/experiment1_results.json"
    with open(out_path, "w") as f:
        json.dump(RESULTS, f, indent=2)
    print(f"\nResults saved to {out_path}")


if __name__ == "__main__":
    main()
