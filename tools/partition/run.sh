#!/usr/bin/env bash
# tools/partition/run.sh
# Usage: ./tools/partition/run.sh <graph_json> --ranks <N> --out <output_json> [--strategy S]
#
# Example (from project root):
#   ./tools/partition/run.sh outputs/graph.json --ranks 8 --out outputs/part.json

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <graph_json> --ranks <N> --out <output_json> [--strategy contiguous|round-robin]"
    exit 1
fi

# Translate --out to the positional output_json argument expected by partition_graph.py
GRAPH_JSON=""
OUTPUT_JSON=""
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out)
            OUTPUT_JSON="$2"
            shift 2
            ;;
        --*)
            EXTRA_ARGS+=("$1" "$2")
            shift 2
            ;;
        *)
            GRAPH_JSON="$1"
            shift
            ;;
    esac
done

if [ -z "$GRAPH_JSON" ] || [ -z "$OUTPUT_JSON" ]; then
    echo "ERROR: must provide <graph_json> and --out <output_json>"
    exit 1
fi

python3 "$SCRIPT_DIR/partition_graph.py" "$GRAPH_JSON" "$OUTPUT_JSON" "${EXTRA_ARGS[@]}"
