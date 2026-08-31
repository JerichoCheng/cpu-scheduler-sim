#!/usr/bin/env python3
"""Render the policy comparison chart from the scheduler's CSV output.

    python3 plot_policies.py

Runs every policy under two arrival scenarios and writes docs/policies.png.
Reads --format csv, so the human-readable summary can be reworded freely; only
reordering the CSV columns would break this.
"""

import csv
import io
import os
import subprocess
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

BINARY = "./scheduler.exe" if os.path.exists("./scheduler.exe") else "./scheduler"
INPUT = "tests/test_input_1"
POLICIES = ["priority-rr", "rr", "sjf", "srtf"]

SCENARIOS = [
    ("All at t=0", []),
    ("Long job first", ["--arrivals", "0,1,1,1,1,1,1,1"]),
]

OUT = "docs/policies.png"


def run(policy, extra):
    """Return the CSV row for one policy under one scenario."""
    cmd = [BINARY, "--policy", policy, "--format", "csv"] + extra + [INPUT]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"{' '.join(cmd)} failed:\n{proc.stderr}")
    rows = list(csv.DictReader(io.StringIO(proc.stdout)))
    if not rows:
        sys.exit(f"{' '.join(cmd)} produced no CSV")
    return rows[0]


def main():
    if not os.path.exists(BINARY):
        sys.exit("Binary not found. Compile first.")

    data = {
        label: [float(run(p, extra)["avg_turnaround"]) for p in POLICIES]
        for label, extra in SCENARIOS
    }

    x = range(len(POLICIES))
    width = 0.38
    fig, ax = plt.subplots(figsize=(7.5, 4.2))

    for offset, (label, values) in zip((-width / 2, width / 2), data.items()):
        bars = ax.bar([i + offset for i in x], values, width, label=label)
        ax.bar_label(bars, fmt="%.1f", fontsize=8, padding=2)

    ax.set_xticks(list(x))
    ax.set_xticklabels(POLICIES)
    ax.set_ylabel("Average turnaround time (ms)")
    ax.set_title("Lower is better — total elapsed time is 596 ms in every bar")
    ax.legend(frameon=False)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.set_ylim(0, max(max(v) for v in data.values()) * 1.18)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    fig.tight_layout()
    fig.savefig(OUT, dpi=150)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()