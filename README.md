# Priority Round-Robin Scheduler with Page-Fault Penalties

A discrete-event simulation of a priority-aware round-robin CPU scheduler in C11.
Written for CITS2002 Systems Programming (UWA).

> **Note:** this repository is private until course marks are released.

## Problem

Eight processes compete for a single CPU. Each has a fixed priority (1 = highest,
5 = lowest), a total execution time in milliseconds, and a list of positions in its
own execution progress at which it triggers a page fault.

The scheduler runs in *passes*. On each scheduling decision it scans every process
that is neither finished nor already run in the current pass, and picks the one with
the numerically lowest priority value; ties break by original file order. Once every
eligible process has had a turn, a new pass begins.

A scheduled process advances by at most one quantum (10 ms by default) of real
execution progress. Any page fault whose position falls inside that window costs a
flat penalty of wall-clock time **without** advancing the process's own progress. A
process finishes when its accumulated progress reaches its total execution time; the
program prints its name and the global elapsed time at that moment.

## Build and run

```
gcc -Wall -Wextra src/scheduler.c -o scheduler
./scheduler tests/test_input_1
```

Options:

```
./scheduler [--quantum N] [--fault-penalty N] <input-file>
```

`--quantum` must be at least 1: a quantum of zero would never advance `cpuTime`,
leaving the simulation unable to terminate. `--fault-penalty` may be zero, which
models a machine where faults are free — useful for isolating pure scheduling
behaviour from fault overhead.

## Tests

```
bash run_tests.sh
```

The harness runs every case in `tests/cases/`, compares stdout against the expected
output, and checks the exit status. It exits non-zero if any case fails, so it can
be wired into CI unchanged.

### Case format

Each `.case` file is one self-contained test — no separate expected-output file to
keep in sync:

```
# args: --fault-penalty 0 tests/test_input_1
# exit: 0
P8 300
P4 338
...
```

- `# args:` — arguments passed to the binary
- `# exit:` — expected exit status, optional, defaults to `0`
- everything else — expected stdout, verbatim

Adding a test means adding a file; the harness needs no changes.

### Current coverage

| Case | What it pins down |
|---|---|
| `default_1`, `default_2` | the two reference inputs under default settings |
| `quantum_20` | a longer quantum reorders completions but preserves the total |
| `zero_penalty` | with penalties disabled the total collapses to `sum(total_time)` |
| `reject_zero_quantum` | a zero quantum would never advance `cpuTime` — rejected rather than hanging |
| `reject_bad_value` | non-numeric flag values |
| `reject_missing_value` | a flag at the end of `argv` with nothing after it |
| `reject_no_input` | no input file given |

### Line endings

The binary emits CRLF on Windows and LF elsewhere, so the harness diffs with
`--strip-trailing-cr`. Case files are stored as LF; `.gitattributes` enforces this
on checkout.

## Input format

One process per line:

```
<name> <priority> <total_time> <n_faults> [fault_position ...]
```

Example:

```
P3 2 96 2 20 55
```

P3 has priority 2, needs 96 ms of CPU time, and faults twice — after 20 ms and after
55 ms of its own execution progress. A process with no faults has a count of 0 and
no positions.

## Design notes

**Three clocks, deliberately kept separate.** The single largest source of bugs here
is conflating them:

| Quantity | Advanced by | Stored in |
|---|---|---|
| `cpuTime` — a process's own progress | real execution only | `struct process` |
| global elapsed time | real execution **+** fault penalties | `main` |
| quantum | configuration, caps each turn | `struct config` |

**Fault windows are half-open: `[start, end)`.** A quantum advancing progress from
`start` to `end` triggers exactly those faults with `start <= position < end`. This
matters because fault positions in the sample data land precisely on quantum
boundaries. The closed-right alternative `(start, end]` fires every such fault one
quantum early — the total stays correct, but intermediate completion times come out
too high.

**A useful invariant.** Because the CPU never idles, the final completion time must
equal `sum(total_time) + penalty * total_fault_count`. For sample 1 under defaults
that is `544 + 52 = 596`; with `--fault-penalty 0` it collapses to `544`. If the last
line is right but earlier lines are wrong, the bug is in fault *timing*, not fault
*counting* — which splits the search space in half before reading any code.

**No dynamic allocation.** All storage is fixed-size arrays with stack duration, as
required by the assignment. Parsing uses a single `sscanf` over twelve conversion
specifiers; its return value is cross-checked against the declared fault count
(`matched == 4 + nfaults`) so malformed and blank lines are rejected rather than
silently producing a garbage process.

## Layout

```
.
├── src/scheduler.c     # the simulation
├── tests/              # inputs
│   └── cases/          # one file per test case
├── run_tests.sh        # diff-based test harness
└── Makefile
```

## Roadmap

See `docs/ROADMAP.md`.
