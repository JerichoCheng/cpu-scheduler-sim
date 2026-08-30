#!/bin/bash
# Runs every case in tests/cases/*.case against the scheduler.
#
# A .case file is self-contained:
#   line 1  "# args: ..."   arguments passed to the binary
#   line 2  "# exit: N"     expected exit status (optional, default 0)
#   rest                    expected stdout, verbatim
#
# Exits non-zero if any case fails.

BIN=./scheduler
[ -x "$BIN" ] || BIN=./scheduler.exe

PASS=0
FAIL=0

if [ ! -x "$BIN" ]; then
    echo "Binary not found. Run 'make' first."
    exit 1
fi

for case_file in tests/cases/*.case; do
    [ -e "$case_file" ] || { echo "No cases found."; exit 1; }
    name=$(basename "$case_file" .case)

    args=$(sed -n 's/^# args: *//p' "$case_file" | head -1)
    want_exit=$(sed -n 's/^# exit: *//p' "$case_file" | head -1)
    want_exit=${want_exit:-0}

    # Expected stdout is everything that is not a header comment
    grep -v '^# ' "$case_file" > "/tmp/${name}.want"

    $BIN $args > "/tmp/${name}.got" 2>/dev/null
    got_exit=$?

    if [ "$got_exit" != "$want_exit" ]; then
        echo "FAIL  ${name}  (exit ${got_exit}, wanted ${want_exit})"
        FAIL=$((FAIL + 1))
   elif ! diff -q --strip-trailing-cr "/tmp/${name}.got" "/tmp/${name}.want" > /dev/null; then
    echo "FAIL  ${name}"
    diff --strip-trailing-cr "/tmp/${name}.got" "/tmp/${name}.want" | sed 's/^/      /'
        FAIL=$((FAIL + 1))
    else
        echo "PASS  ${name}"
        PASS=$((PASS + 1))
    fi
done

echo "----"
echo "${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
