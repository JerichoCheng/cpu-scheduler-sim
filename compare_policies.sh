#!/usr/bin/env bash
set -euo pipefail

BIN=./scheduler
[ -f "$BIN" ] || BIN=./scheduler.exe

# Extract input file (last positional argument) and optional flags
ARGS=("$@")
if [ ${#ARGS[@]} -eq 0 ]; then
    INPUT="tests/test_input_1"
    EXTRA_FLAGS=()
else
    INPUT="${ARGS[${#ARGS[@]}-1]}"
    unset "ARGS[${#ARGS[@]}-1]"
    EXTRA_FLAGS=("${ARGS[@]}")
fi

if [ ! -f "$INPUT" ]; then
    echo "Error: Input file '$INPUT' not found." >&2
    exit 1
fi

echo "| Policy | Avg turnaround | Avg waiting (queued) | Total time |"
echo "|---|---|---|---|"

for policy in priority-rr rr sjf srtf; do
    line=$("$BIN" --policy "$policy" --format csv "${EXTRA_FLAGS[@]}" "$INPUT" | tail -n 1 | tr -d '\r')
    IFS=',' read -r name total turnaround wtotal wqueued util faults penalty <<< "$line"
    echo "| \`$name\` | $turnaround | $wqueued | $total |"
done