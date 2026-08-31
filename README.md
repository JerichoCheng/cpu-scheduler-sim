# Priority Round-Robin Scheduler with Page-Fault Penalties

A discrete-event simulation of a round-robin CPU scheduler with configurable
selection policies and page-fault penalties, in C11. Written for CITS2002 Systems
Programming (UWA).

> **Note:** this repository is private until course marks are released.

## Problem

Eight processes compete for a single CPU. Each has a fixed priority (1 = highest,
5 = lowest), a total execution time in milliseconds, and a list of positions in its
own execution progress at which it triggers a page fault.

The scheduler runs in *passes*. On each scheduling decision it considers every
process that is neither finished nor already run in the current pass, and hands the
choice to the active policy. Once every eligible process has had a turn, a new pass
begins.

A scheduled process advances by at most one quantum of real execution progress —
10 ms by default, settable with `--quantum`. Any page fault whose position falls
inside that window costs a flat penalty of wall-clock time (4 ms by default,
settable with `--fault-penalty`) **without** advancing the process's own progress.
A process finishes when its accumulated progress reaches its total execution time;
the program prints its name and the global elapsed time at that moment.

## Build and run

```
gcc -Wall -Wextra src/scheduler.c -o scheduler
./scheduler tests/test_input_1
```

Full usage:

```
./scheduler [--quantum N] [--fault-penalty N] [--policy priority-rr|rr] [--stats] <input-file>
```

`--quantum` must be at least 1: a quantum of zero would never advance `cpuTime`,
leaving the simulation unable to terminate. `--fault-penalty` may be zero, which
models a machine where faults are free — useful for isolating scheduling behaviour
from fault overhead.

Completion events go to `stdout`, so they can be piped:

```
./scheduler tests/test_input_1 | sort -k2 -n
```

`--stats` writes its summary to `stderr` instead, keeping `stdout` parseable.

## Scheduling policies

| Policy | Avg turnaround | Avg waiting (queued) | Total time |
|---|---|---|---|
| `priority-rr` (default) | 492.00 | 417.50 | 596 |
| `rr` | 501.75 | 427.25 | 596 |

The total is identical under both policies, because it is fixed by
`sum(total_time) + penalty * faults_triggered`. Scheduling cannot change how much
work there is — it only redistributes which processes wait longer. Priority
scheduling buys about 10 ms of average turnaround here, entirely at the expense of
the low-priority processes.

Selection is reached through a `policy_fn` function pointer, so the scheduling loop
never knows which policy is active. Adding one means writing a function with that
signature and registering a `--policy` name for it.

## Tests

```
bash run_tests.sh
```

The harness runs every case in `tests/cases/`, compares output against the
expected text, and checks the exit status. It exits non-zero if any case fails, so
it can be wired into CI unchanged.

### Case format

Each `.case` file is one self-contained test — no separate expected-output file to
keep in sync:

```
# args: --stats tests/test_input_1
# stderr: 1
P8 324
...
```

- `# args:` — arguments passed to the binary
- `# exit:` — expected exit status, optional, defaults to `0`
- `# stderr: 1` — fold stderr into the comparison, optional, off by default
- everything else — expected output, verbatim

Adding a test means adding a file; the harness needs no changes.

### Current coverage

| Case | What it pins down |
|---|---|
| `default_1`, `default_2` | the two reference inputs under default settings |
| `quantum_20` | a longer quantum reorders completions but preserves the total |
| `zero_penalty` | with penalties disabled the total collapses to `sum(total_time)` |
| `policy_rr` | ignoring priority changes the order and raises average turnaround |
| `stats` | the three invariants: total 596, 13 faults triggered, 52 ms penalty |
| `reject_zero_quantum` | a zero quantum would never advance `cpuTime` — rejected rather than hanging |
| `reject_bad_value` | non-numeric flag values |
| `reject_missing_value` | a flag at the end of `argv` with nothing after it |
| `reject_unknown_policy` | an unrecognised `--policy` name |
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
equal `sum(total_time) + penalty * faults_triggered`. For sample 1 under defaults
that is `544 + 52 = 596`; with `--fault-penalty 0` it collapses to `544`. If the last
line is right but earlier lines are wrong, the bug is in fault *timing*, not fault
*counting* — which splits the search space in half before reading any code.

Statistics count faults actually triggered during the simulation rather than the
count declared in the input file. The two agree on this data, but only because every
fault position happens to fall strictly below its process's total time.

**Streams are captured separately, not merged.** The obvious way to test `--stats`
output is `binary > got 2>&1`. It does not work: `stderr` is unbuffered while
`stdout` becomes block-buffered once it is not a terminal, so the statistics land in
the file before the completion lines that were printed first. The harness redirects
the two streams to separate files and concatenates them in a fixed order.

**No dynamic allocation.** All storage is fixed-size arrays with stack duration, as
required by the assignment. Parsing uses a single `sscanf` over twelve conversion
specifiers; its return value is cross-checked against the declared fault count
(`matched == 4 + nfaults`) so malformed and blank lines are rejected rather than
silently producing a garbage process.

## Layout

```
.
├── src/scheduler.c     # simulation and policy implementations
├── tests/              # input workloads
│   └── cases/          # one file per test case
├── docs/ROADMAP.md     # planned extensions
├── run_tests.sh        # test harness
└── Makefile
```

## Roadmap

See `docs/ROADMAP.md`.