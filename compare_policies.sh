#!/bin/bash
# Runs every policy against one input and prints a markdown comparison table.
#
#   ./compare_policies.sh tests/test_input_1
#   ./compare_policies.sh tests/test_input_1 --arrivals 0,1,1,1,1,1,1,1
#
# Anything after the input file is passed straight through to the binary, so the
# same scenario can be run under different arrival patterns.
#
# Reads the --format csv block, not the human summary, so rewording the human
# output cannot break this script. Only reordering the CSV columns can.

BIN=./scheduler
[ -f "$BIN" ] || BIN=./scheduler.exe

INPUT=${1:-tests/test_input_1}
shift 2>/dev/null
EXTRA="$@"

POLICIES="priority-rr rr sjf srtf"

if [ ! -f "$BIN" ]; then
    echo "Binary not found. Compile first." >&2
    exit 1
fi

if [ ! -f "$INPUT" ]; then
    echo "Input not found: $INPUT" >&2
    exit 1
fi

echo "| Policy | Avg turnaround | Avg waiting (queued) | Total time |"
echo "|---|---|---|---|"

for policy in $POLICIES; do
    line=$($BIN --policy "$policy" --format csv $EXTRA "$INPUT" | tail -1 | tr -d '\r')

    if [ -z "$line" ]; then
        echo "| \`$policy\` | (failed) | | |"
        continue
    fi

    IFS=',' read -r name total turnaround wtotal wqueued util faults penalty <<< "$line"
    echo "| \`$policy\` | $turnaround | $wqueued | $total |"
done