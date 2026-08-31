#!/bin/bash
# Runs every policy against one input and prints a markdown comparison table.
#
#   ./compare_policies.sh tests/test_input_1
#
# Reads the --stats block from stderr. Regenerate the table in README.md with this
# rather than editing it by hand, so it cannot drift from the code.

BIN=./scheduler
[ -f "$BIN" ] || BIN=./scheduler.exe

INPUT=${1:-tests/test_input_1}
POLICIES="priority-rr rr sjf"

if [ ! -f "$BIN" ]; then
    echo "Binary not found. Compile first." >&2
    exit 1
fi

if [ ! -f "$INPUT" ]; then
    echo "Input not found: $INPUT" >&2
    exit 1
fi

# Pull one "Label: value" line out of a stats block
field() {
    sed -n "s/^$2: *//p" "$1" | tr -d '\r%' | head -1
}

echo "| Policy | Avg turnaround | Avg waiting (queued) | Total time | CPU util |"
echo "|---|---|---|---|---|"

for policy in $POLICIES; do
    $BIN --policy "$policy" --stats "$INPUT" > /dev/null 2> /tmp/stats.$$

    turnaround=$(field /tmp/stats.$$ "Average turnaround time")
    queued=$(field /tmp/stats.$$ "Average waiting time (queued only)")
    total=$(field /tmp/stats.$$ "Total execution time")
    util=$(field /tmp/stats.$$ "CPU utilisation")

    if [ -z "$total" ]; then
        echo "| \`$policy\` | (no stats returned) | | | |"
        continue
    fi

    echo "| \`$policy\` | $turnaround | $queued | $total | ${util}% |"
done

rm -f /tmp/stats.$$