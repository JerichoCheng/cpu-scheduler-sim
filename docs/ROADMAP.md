# Roadmap

Staged plan for growing this from a single-policy simulation into a general
scheduler simulator. Each stage is independently shippable and testable.

## Stage 1 — Decouple configuration

Move `QUANTUM` and `FAULT_PENALTY` out of `#define` and into a `struct config`
populated from command-line flags:

```
./scheduler --quantum 20 --fault-penalty 2 tests/test_input_1
```

Use `getopt` (`<unistd.h>`). Defaults must reproduce the current output exactly,
so the existing tests become a regression suite for free.

## Stage 2 — Statistics

The completion times alone are a weak measure. Add, behind a `--stats` flag:

- turnaround time per process (completion − arrival; arrival is 0 for now)
- waiting time (turnaround − total execution time)
- averages across all processes
- CPU utilisation (useful work ÷ wall clock) — which is exactly
  `sum(total_time) / final_time`, the invariant from the README seen from the
  other side
- total faults and total penalty time

These are the metrics an OS course actually grades scheduling policies on.

## Stage 3 — Pluggable policies

Extract the "pick the next process" step behind a function pointer:

```c
typedef int (*policy_fn)(const struct process *procs, int n, const int *ran);
```

Then implement, selected by `--policy`:

- `rr` — plain round robin, ignore priority
- `priority-rr` — the current behaviour
- `sjf` — shortest job first (non-preemptive)
- `srtf` — shortest remaining time first (preemptive)
- `mlfq` — multi-level feedback queue

Comparing these on identical input is the payoff: it shows starvation under
strict priority, convoy effects under FCFS, and why MLFQ exists.

## Stage 4 — Arbitrary process counts

Drop the fixed `MAX_PROCS 8`. Count the lines, then `malloc` the array; free it
before exit. This is the natural place to practise the dynamic allocation the
next lab requires. Run it under `valgrind` (or `leaks` on macOS) and keep the
output clean.

## Stage 5 — Arrival times

Add an optional arrival-time field. Processes not yet arrived are not eligible;
if none are eligible the CPU idles and the global clock jumps to the next
arrival. This breaks the README's invariant, which is the point — idle time is
now real, and CPU utilisation stops being trivially 100%.

## Stage 6 — Visualisation

Emit a Gantt-style timeline, either as ASCII in the terminal or as a CSV that a
short Python script plots:

```
P2 |####      ####      ...
P4 |    ####      ####  ...
```

Good README material, and it makes policy differences immediately legible.

## Housekeeping, throughout

- keep `make test` green at every stage
- add a test case per stage (empty file, single process, all-same-priority,
  zero faults, faults on every boundary)
- build with `-Wall -Wextra -Werror`
- one commit per logical change, with a message saying *why*
