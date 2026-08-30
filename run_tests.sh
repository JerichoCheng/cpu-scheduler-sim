#!/bin/bash
# Runs the scheduler against every tests/test_input_N and diffs the result
# against tests/expected_N. Exits non-zero if any case fails.

BIN=./scheduler
PASS=0
FAIL=0

if [ ! -x "$BIN" ]; then
    echo "Binary not found. Run 'make' first."
    exit 1
fi

for input in tests/test_input_*; do
    name=$(basename "$input")
    num=${name##*_}
    expected="tests/expected_${num}"

    if [ ! -f "$expected" ]; then
        echo "SKIP  ${name}  (no expected_${num})"
        continue
    fi

    actual="tests/${name}.actual"
    "$BIN" "$input" > "$actual"

    if diff -q "$actual" "$expected" > /dev/null; then
        echo "PASS  ${name}"
        PASS=$((PASS + 1))
    else
        echo "FAIL  ${name}"
        diff "$actual" "$expected" | sed 's/^/      /'
        FAIL=$((FAIL + 1))
    fi
done

echo "----"
echo "${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
