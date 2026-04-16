#!/usr/bin/env bash
# tools/graph_export/run.sh
# Usage: ./tools/graph_export/run.sh <config_or_dot> <output_json> [--seed N] [--directed]
#
# Example (from project root):
#   ./tools/graph_export/run.sh configs/small.conf outputs/graph.json

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <config_or_dot_file> <output_json> [extra args]"
    exit 1
fi

python3 "$SCRIPT_DIR/export_graph.py" "$@"
