# CS453 Spring 2026 — NetGameSim to MPI Distributed Algorithms

End-to-end pipeline: generate random graphs with NetGameSim → partition across MPI ranks → run distributed leader election and Dijkstra shortest paths.

---

## Repository Layout

```
/
  netgamesim/                 Upstream NetGameSim (git submodule)
  tools/
    graph_export/
      export_graph.py         Parse NetGameSim .dot → weighted JSON graph
      run.sh                  Shell wrapper (Linux/Mac)
      run.bat                 Shell wrapper (Windows)
    partition/
      partition_graph.py      Partition graph across MPI ranks → partition JSON
      run.sh                  Shell wrapper (Linux/Mac)
      run.bat                 Shell wrapper (Windows)
  mpi_runtime/
    src/                      C++17 MPI runtime
      main.cpp                CLI entry point
      graph.cpp               Partition JSON loader
      leader_election.cpp     Distributed FloodMax leader election
      dijkstra.cpp            Distributed Dijkstra shortest paths
      metrics.cpp             Runtime metrics collection
    include/
      graph.h                 GraphPartition and Edge structs
      leader_election.h       Leader election API + correctness assumptions
      dijkstra.h              Dijkstra API + correctness assumptions
      metrics.h               Metrics struct
      json.hpp                nlohmann/json single-header library
    tests/
      test_main.cpp           8 unit/integration tests
    CMakeLists.txt            Build configuration
  configs/
    small.conf                Config for ~50 node graph
    large.conf                Config for ~300 node graph
  experiments/
    experiment1.py            Partitioning strategy comparison
    experiment2.py            Algorithm scaling: small vs large graph
  outputs/                    Generated graphs, partitions, logs, results
  REPORT.md                   Experiment write-up
```

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| Java JDK | 17+ | For NetGameSim |
| Scala / SBT | 3.x / 1.8+ | For NetGameSim |
| Python | 3.8+ | Graph export and partition tools |
| MS-MPI | Latest | [Download](https://github.com/microsoft/Microsoft-MPI/releases) — install both `msmpisetup.exe` and `msmpisdk.msi` |
| Visual Studio 2022 | Community | C++ workload required |
| CMake | 3.16+ | Included with Visual Studio or download separately |
| Git | Any | Required for submodule checkout |

---

## Installation

### 1. Clone this repo with the NetGameSim submodule

```powershell
git clone --recurse-submodules https://github.com/Ajani4/CS453_Spring2026.git
cd CS453_Project
```

If you already cloned without `--recurse-submodules`, run:

```powershell
git submodule update --init --recursive
```

### 2. Build NetGameSim
Follow all steps within netgamesim repo if not already installed https://github.com/0x1DOCD00D/CS453_Spring2026/blob/main/CourseProject.md
```powershell
cd netgamesim
sbt clean compile assembly
cd ..
```

### 3. Configure NetGameSim output directory

Before running NetGameSim you must tell it where to write its output files. Open:

```
netgamesim\GenericSimUtilities\src\main\resources\application.conf
```

Find the `outputDirectory` line and set it to the CS453_Project/outputsfolder inside this project and not netgamesim/outputs. Use forward slashes even on Windows. Backslashes cause a parse error in HOCON config files:

```hocon
NGSimulator {
    seed = 100
    outputDirectory = "yourPath/CS453_Project/outputs"
    ...
}
```



```powershell
cd netgamesim
sbt clean compile assembly
cd ..
```

### 4. Configure graph export configs

`configs/small.conf` and `configs/large.conf` tell the graph export tool which `.dot` file to read and what seed to use. Open each file and verify the `dot_file` path matches where NetGameSim will write its output. Small.conf and large.conf can be reconfigures to represent different ngs.dot outputs from NetGameSim but it should follow that small.conf is configured to an output with a small node count and large.conf a large node count.

**`configs/small.conf`** — used for the ~50 node graph:
```hocon
# Path to the .dot file NetGameSim generates for the small graph.
# This should match your outputDirectory in application.conf + the filename.
dot_file = outputs/small.ngs.dot

# Seed for reproducibility — must match NGSimulator.seed in application.conf
# if you want fully reproducible graphs end-to-end.
seed = 42

# Keep false — undirected edges are required for leader election correctness.
directed = false
```

**`configs/large.conf`** — used for the ~300 node graph:
```hocon
dot_file = outputs/large.ngs.dot
seed = 42
directed = false
```

The `dot_file` paths are relative to the project root. If your `outputDirectory` in `application.conf` points to `outputs` inside this project (as shown in step 3), no changes are needed — the defaults will work.

### 5. Download json.hpp

```powershell
Invoke-WebRequest -Uri "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" `
  -OutFile "mpi_runtime\include\json.hpp"
```

### 6. Build the MPI runtime

```powershell
cmake -S mpi_runtime -B build
cmake --build build
```

The executables will be at:
- `build\Debug\ngs_mpi.exe`
- `build\Debug\ngs_tests.exe`

---

## End-to-End Workflow

The commands below match the workflow specified in `CourseProject.md`. Run all commands from the project root.

### Step 1: Generate graphs with NetGameSim

Make sure `application.conf` is configured as described in the Installation section above. Then run from the project root:

These are commands I used for Windows, but the NetGameSim repo has the command used in a linux system.

```powershell
cd netgamesim

# Small graph (~50 nodes)
java -Xms2G -Xmx8G "-DNGSimulator.NetModel.statesTotal=50" -jar target/scala-3.2.2/netmodelsim.jar small

# Large graph (~300 nodes)
java -Xms2G -Xmx8G "-DNGSimulator.NetModel.statesTotal=300" -jar target/scala-3.2.2/netmodelsim.jar large

cd ..
```

NetGameSim will write `small.ngs.dot` and `large.ngs.dot` directly into the `outputs/` folder (because `outputDirectory` in `application.conf` points there). No manual copy step is needed.

### Step 2: Generate and enrich a connected weighted graph

```powershell
# Windows
tools\graph_export\run.bat configs\small.conf outputs\graph.json

# Linux / Mac
./tools/graph_export/run.sh configs/small.conf outputs/graph.json
```

### Step 3: Partition the graph across ranks

```powershell
# Windows
tools\partition\run.bat outputs\graph.json --ranks 8 --out outputs\part.json

# Linux / Mac
./tools/partition/run.sh outputs/graph.json --ranks 8 --out outputs/part.json
```

### Step 4: Build the MPI runtime

```powershell
cmake -S mpi_runtime -B build
cmake --build build
```

### Step 5: Run leader election

```powershell
# Windows
mpiexec -n 8 build\Debug\ngs_mpi.exe --graph outputs\graph.json --part outputs\part.json --algo leader --rounds 200

# Linux / Mac
mpirun -n 8 ./build/ngs_mpi --graph outputs/graph.json --part outputs/part.json --algo leader --rounds 200
```

### Step 6: Run distributed Dijkstra

```powershell
# Windows
mpiexec -n 8 build\Debug\ngs_mpi.exe --graph outputs\graph.json --part outputs\part.json --algo dijkstra --source 0

# Linux / Mac
mpirun -n 8 ./build/ngs_mpi --graph outputs/graph.json --part outputs/part.json --algo dijkstra --source 0
```

---

## Running Tests

```powershell
mpiexec -n 4 build\Debug\ngs_tests.exe
```

Expected output: `ALL TESTS PASSED` with 8 tests.

Tests cover:
1. Owner map covers all node IDs
2. Each node is owned by exactly one rank
3. Cross-edge destination rank is always different from source rank
4. Partition file loads without exception
5. All ranks agree on the same leader (integration)
6. Elected leader equals the maximum node ID
7. Source node has distance 0 in Dijkstra output
8. Known distances on a small triangle graph are correct

---

## Running Experiments

```powershell
# Experiment 1: Partitioning strategy comparison
python experiments/experiment1.py

# Experiment 2: Algorithm scaling (small vs large graph)
python experiments/experiment2.py
```

Results are saved as JSON in `outputs/`.

---

## CLI Reference

### export_graph.py

```
python tools/graph_export/export_graph.py <dot_file> <output_json> [--seed N] [--directed]
```

| Argument | Description |
|----------|-------------|
| `dot_file` | Input `.dot` file from NetGameSim |
| `output_json` | Output weighted graph JSON |
| `--seed N` | Random seed for reproducibility (default: 42) |
| `--directed` | Keep directed edges (default: make undirected) |

### partition_graph.py

```
python tools/partition/partition_graph.py <graph_json> <output_json> [--ranks N] [--strategy S]
```

| Argument | Description |
|----------|-------------|
| `graph_json` | Input graph JSON |
| `output_json` | Output partition JSON |
| `--ranks N` | Number of MPI ranks (default: 4) |
| `--strategy` | `contiguous` or `round-robin` (default: contiguous) |

### ngs_mpi

```
mpiexec -n <ranks> ngs_mpi.exe --part <partition.json> --algo <leader|dijkstra|both>
                                [--source N] [--rounds N] [--output <file>]
```

| Argument | Description |
|----------|-------------|
| `--part` | Partition JSON (required) |
| `--algo` | Algorithm to run (default: both) |
| `--source` | Dijkstra source node ID (default: 0) |
| `--rounds` | FloodMax max rounds (default: 200) |
| `--output` | Write Dijkstra distances to CSV file |

---

## Correctness Assumptions

- **Graph connectivity**: the graph must be connected (checked during export). Leader election and Dijkstra are undefined on disconnected graphs.
- **Positive edge weights**: Dijkstra requires strictly positive weights. NetGameSim outputs weights ≥ 1.0; the exporter rejects zero or negative weights with a warning.
- **Unique node IDs**: all node IDs must be unique integers in [0, N−1]. The exporter remaps NetGameSim IDs to ensure this.
- **FloodMax rounds**: `--rounds` must be ≥ the graph diameter for guaranteed convergence. The default of 200 is sufficient for graphs up to a few hundred nodes.
- **Rank count match**: the MPI rank count passed to `mpiexec -n` must match `--ranks` used when partitioning.

---

## Reproducibility

- Graph generation seed is stored in the exported `graph.json` under the `seed` key.
- Re-run `export_graph.py` with the same `--seed` and `.dot` file to reproduce the same JSON.
- Re-run NetGameSim with the same `seed` value in `application.conf` to reproduce the same `.dot` file.

---
