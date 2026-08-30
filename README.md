# Priority Round-Robin Scheduler with Page-Fault Penalties

A discrete-event simulation of a priority-aware round-robin CPU scheduler in C11.

## Problem

Eight processes compete for a single CPU. Each has a fixed priority (1 = highest,
5 = lowest), a total execution time in milliseconds, and a list of positions in its
own execution progress at which it triggers a page fault.

The scheduler runs in *passes*. On each scheduling decision it scans every process
that is neither finished nor already run in the current pass, and picks the one with
the numerically lowest priority value; ties break by original file order. Once every
eligible process has had a turn, a new pass begins.

A scheduled process advances by at most one quantum (10 ms) of real execution
progress. Any page fault whose position falls inside that window costs a flat 4 ms
of wall-clock time **without** advancing the process's own progress. A process
finishes when its accumulated progress reaches its total execution time; the
program prints its name and the global elapsed time at that moment.

## Build and run

```
make
./scheduler tests/test_input_1
```

Or run the full suite:

```
make test
```

## Input format

One process per line:

```
<name> <priority> <total_time> <n_faults> [fault_position ...]
```

Example:

```
P3 2 96 2 20 55
```

P3 has priority 2, needs 96 ms of CPU time, and faults twice — after 20 ms and
after 55 ms of its own execution progress. A process with no faults has a count of
0 and no positions.

## Design notes

**Three clocks, deliberately kept separate.** The single largest source of bugs here
is conflating them:

| Quantity | Advanced by | Stored in |
|---|---|---|
| `cpuTime` — a process's own progress | real execution only | `struct process` |
| global elapsed time | real execution **+** fault penalties | `main` |
| quantum | constant, caps each turn | `#define` |

**Fault windows are half-open: `[start, end)`.** A quantum advancing progress from
`start` to `end` triggers exactly those faults with `start <= position < end`.
This matters because fault positions in the sample data land precisely on quantum
boundaries. The closed-right alternative `(start, end]` fires every such fault one
quantum early — the total stays correct, but intermediate completion times come out
too high.

**A useful invariant.** Because the CPU never idles, the final completion time must
equal `sum(total_time) + 4 * total_fault_count`. For sample 1 that is
`544 + 52 = 596`. If the last line is right but earlier lines are wrong, the bug is
in fault *timing*, not fault *counting* — which splits the search space in half
before reading any code.

**No dynamic allocation.** All storage is fixed-size arrays with stack duration, as
required by the assignment. Parsing uses a single `sscanf` over twelve conversion
specifiers; its return value is cross-checked against the declared fault count
(`matched == 4 + nfaults`) so malformed and blank lines are rejected rather than
silently producing a garbage process.

## Layout

```
.
├── src/scheduler.c     # the simulation
├── tests/              # inputs and expected outputs
├── run_tests.sh        # diff-based test harness
└── Makefile
```

## Roadmap

See `docs/ROADMAP.md`.
