#!/bin/bash
# Runs every case in tests/cases/*.case against the scheduler.
#
# A .case file is self-contained:
#   line 1  "# args: ..."     arguments passed to the binary
#           "# exit: N"       expected exit status (optional, default 0)
#           "# stderr: 1"     fold stderr into the comparison (optional, default off)
#   rest                      expected output, verbatim
#
# Exits non-zero if any case fails.

BIN=./scheduler
[ -f "$BIN" ] || BIN=./scheduler.exe

PASS=0
FAIL=0

if [ ! -f "$BIN" ]; then
    echo "Binary not found. Run 'make' first."
    exit 1
fi

for case_file in tests/cases/*.case; do
    [ -e "$case_file" ] || { echo "No cases found."; exit 1; }
    name=$(basename "$case_file" .case)

    args=$(sed -n 's/^# args: *//p' "$case_file" | head -1 | tr -d '\r')
    want_exit=$(sed -n 's/^# exit: *//p' "$case_file" | head -1 | tr -d '\r')
    want_exit=${want_exit:-0}
    want_stderr=$(sed -n 's/^# stderr: *//p' "$case_file" | head -1 | tr -d '\r')

    # Expected output is everything that is not a header comment
    grep -v '^# ' "$case_file" > "/tmp/${name}.want"

    # Capture the two streams separately, then concatenate stdout-then-stderr.
    # Redirecting both into one file with 2>&1 interleaves unpredictably: stderr is
    # unbuffered while stdout becomes block-buffered when it is not a terminal, so
    # stderr can appear first.
    $BIN $args > "/tmp/${name}.out" 2> "/tmp/${name}.err"
    got_exit=$?

    if [ "$want_stderr" = "1" ]; then
        cat "/tmp/${name}.out" "/tmp/${name}.err" > "/tmp/${name}.got"
    else
        cp "/tmp/${name}.out" "/tmp/${name}.got"
    fi

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