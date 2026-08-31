#!/usr/bin/env bash
set -euo pipefail

BIN=./scheduler
[ -f "$BIN" ] || BIN=./scheduler.exe

INPUT="${1:-tests/test_input_1}"

if [ ! -f "$INPUT" ]; then
    echo "Error: Input file '$INPUT' not found." >&2
    exit 1
fi

echo "| Policy | Avg turnaround | Avg waiting (queued) | Total time |"
echo "|---|---|---|---|"

for policy in priority-rr rr sjf; do
    line=$("$BIN" --policy "$policy" --format csv "$INPUT" | tail -n 1 | tr -d '\r')
    IFS=',' read -r name total turnaround wtotal wqueued util faults penalty <<< "$line"
    echo "| \`$name\` | $turnaround | $wqueued | $total |"
done