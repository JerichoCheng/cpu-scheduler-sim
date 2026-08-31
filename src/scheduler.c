#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LEN           256
#define MAX_PROCS              8
#define MAX_FAULTS             8
#define DEFAULT_FAULT_PENALTY  4
#define DEFAULT_QUANTUM        10

struct process {
    char name[10];
    int priority;
    int total_time;
    int nfaults;
    int faults[MAX_FAULTS];
    int cpuTime;
    int completion_time;
    int faults_triggered;
};

struct config;

// Function pointer type for process selection and time-slice allocation
typedef int (*policy_fn)(const struct process procs[], int nprocs, const int ran[], const struct config *cfg, int *slice);

struct config {
    int quantum;
    int fault_penalty;
    int show_stats;
    policy_fn pick;
    const char *policy_name;
};

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

// Policy: Priority Round-Robin (lowest priority number first, ties broken by file order)
static int policy_priority_rr(const struct process procs[], int nprocs, const int ran[], const struct config *cfg, int *slice) {
    int best = -1;
    for (int i = 0; i < nprocs; i++) {
        if (!ran[i] && procs[i].cpuTime < procs[i].total_time) {
            if (best == -1 || procs[i].priority < procs[best].priority) {
                best = i;
            }
        }
    }
    if (best != -1) {
        *slice = cfg->quantum;
    }
    return best;
}

// Policy: Pure Round-Robin (ignores priority, picks the next eligible process in file order)
static int policy_rr(const struct process procs[], int nprocs, const int ran[], const struct config *cfg, int *slice) {
    for (int i = 0; i < nprocs; i++) {
        if (!ran[i] && procs[i].cpuTime < procs[i].total_time) {
            *slice = cfg->quantum;
            return i;
        }
    }
    return -1;
}

// Policy: Shortest Job First (non-preemptive: picks smallest total_time and runs to completion)
static int policy_sjf(const struct process procs[], int nprocs, const int ran[], const struct config *cfg, int *slice) {
    (void)cfg;
    int best = -1;
    for (int i = 0; i < nprocs; i++) {
        if (!ran[i] && procs[i].cpuTime < procs[i].total_time) {
            if (best == -1 || procs[i].total_time < procs[best].total_time) {
                best = i;
            }
        }
    }
    if (best != -1) {
        *slice = procs[best].total_time - procs[best].cpuTime;
    }
    return best;
}

// Advances process execution by a given slice and calculates page fault penalties
static int run_quantum(struct process *p, int slice, int fault_penalty) {
    int start_time = p->cpuTime;
    int advance = min_int(slice, p->total_time - start_time);
    int end_time = start_time + advance;

    // Strictly check the interval [start_time, end_time)
    int faults_in_window = 0;
    for (int i = 0; i < p->nfaults; i++) {
        // Execution covers time ticks [start_time, end_time). A fault occurring at exactly
        // end_time belongs to the start of the subsequent quantum, not the current one.
        if (p->faults[i] >= start_time && p->faults[i] < end_time) {
            faults_in_window++;
        }
    }

    p->cpuTime += advance;
    p->faults_triggered += faults_in_window;
    return advance + (faults_in_window * fault_penalty);
}

// Check whether all processes have completed
static int has_unfinished_processes(const struct process procs[], int nprocs) {
    for (int i = 0; i < nprocs; i++) {
        if (procs[i].cpuTime < procs[i].total_time) {
            return 1;
        }
    }
    return 0;
}

// Safely parse an integer argument with a configurable lower bound
static int parse_int_arg(const char *str, int *out, int lower_bound) {
    char *end;
    long val = strtol(str, &end, 10);
    if (*end != '\0' || end == str || val < lower_bound) {
        return 0;
    }
    *out = (int)val;
    return 1;
}

/*
 * Prints simulation and performance statistics to stderr.
 * 
 * Metric definitions:
 * - Turnaround Time: completion_time - arrival_time (all processes arrive at t=0).
 * - Total Waiting Time: turnaround_time - total_time (includes both queued ready-state time and fault penalties).
 * - Queued Waiting Time: total_waiting_time - fault_penalty_time (pure ready queue delay).
 * - CPU Utilisation: (sum(total_time) / total_elapsed_time) * 100%.
 */
static void print_statistics(const struct process procs[], int nprocs, int total_time, const struct config *cfg) {
    if (nprocs == 0) {
        return;
    }

    int total_cpu_time = 0;
    int total_turnaround = 0;
    int total_waiting = 0;
    int total_faults_triggered = 0;

    for (int i = 0; i < nprocs; i++) {
        int turnaround = procs[i].completion_time;
        int waiting = turnaround - procs[i].total_time;

        total_cpu_time += procs[i].total_time;
        total_turnaround += turnaround;
        total_waiting += waiting;
        total_faults_triggered += procs[i].faults_triggered;
    }

    int total_penalty_time = total_faults_triggered * cfg->fault_penalty;
    int total_queued_time = total_waiting - total_penalty_time;

    double avg_turnaround = (double)total_turnaround / nprocs;
    double avg_waiting = (double)total_waiting / nprocs;
    double avg_queued = (double)total_queued_time / nprocs;
    double cpu_utilisation = (total_time > 0) ? ((double)total_cpu_time / total_time) * 100.0 : 0.0;

    fprintf(stderr, "--- Statistics ---\n");
    fprintf(stderr, "Scheduling policy: %s\n", cfg->policy_name);
    fprintf(stderr, "Total execution time: %d\n", total_time);
    fprintf(stderr, "Average turnaround time: %.2f\n", avg_turnaround);
    fprintf(stderr, "Average waiting time (total): %.2f\n", avg_waiting);
    fprintf(stderr, "Average waiting time (queued only): %.2f\n", avg_queued);
    fprintf(stderr, "CPU utilisation: %.2f%%\n", cpu_utilisation);
    fprintf(stderr, "Total page faults triggered: %d\n", total_faults_triggered);
    fprintf(stderr, "Total fault penalty: %d\n", total_penalty_time);
}

int main(int argc, char *argv[]) {
    struct config cfg = {
        .quantum = DEFAULT_QUANTUM,
        .fault_penalty = DEFAULT_FAULT_PENALTY,
        .show_stats = 0,
        .pick = policy_priority_rr,
        .policy_name = "priority-rr"
    };

    const char *input_file = NULL;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quantum") == 0) {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &cfg.quantum, 1)) {
                fprintf(stderr, "Error: Invalid or missing argument for --quantum\n");
                return EXIT_FAILURE;
            }
            i++;
        } else if (strcmp(argv[i], "--fault-penalty") == 0) {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &cfg.fault_penalty, 0)) {
                fprintf(stderr, "Error: Invalid or missing argument for --fault-penalty\n");
                return EXIT_FAILURE;
            }
            i++;
        } else if (strcmp(argv[i], "--policy") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing argument for --policy\n");
                return EXIT_FAILURE;
            }
            if (strcmp(argv[i + 1], "priority-rr") == 0) {
                cfg.pick = policy_priority_rr;
                cfg.policy_name = "priority-rr";
            } else if (strcmp(argv[i + 1], "rr") == 0) {
                cfg.pick = policy_rr;
                cfg.policy_name = "rr";
            } else if (strcmp(argv[i + 1], "sjf") == 0) {
                cfg.pick = policy_sjf;
                cfg.policy_name = "sjf";
            } else {
                fprintf(stderr, "Error: Unknown policy %s\n", argv[i + 1]);
                return EXIT_FAILURE;
            }
            i++;
        } else if (strcmp(argv[i], "--stats") == 0) {
            cfg.show_stats = 1;
        } else if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(stderr, "Error: Unknown option %s\n", argv[i]);
            return EXIT_FAILURE;
        } else {
            if (input_file != NULL) {
                fprintf(stderr, "Error: Multiple input files specified\n");
                return EXIT_FAILURE;
            }
            input_file = argv[i];
        }
    }

    if (input_file == NULL) {
        fprintf(stderr, "Usage: %s [--quantum N] [--fault-penalty N] [--policy <priority-rr|rr|sjf>] [--stats] <input-file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *fp = fopen(input_file, "r");
    if (fp == NULL) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    struct process procs[MAX_PROCS];
    int nprocs = 0;
    char line[MAX_LINE_LEN];

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (nprocs >= MAX_PROCS) {
            break;
        }

        procs[nprocs].cpuTime = 0;
        procs[nprocs].completion_time = 0;
        procs[nprocs].faults_triggered = 0;
        for (int i = 0; i < MAX_FAULTS; i++) {
            procs[nprocs].faults[i] = 0;
        }

        int matched = sscanf(line,
            "%9s %d %d %d %d %d %d %d %d %d %d %d",
            procs[nprocs].name,
            &procs[nprocs].priority,
            &procs[nprocs].total_time,
            &procs[nprocs].nfaults,
            &procs[nprocs].faults[0],
            &procs[nprocs].faults[1],
            &procs[nprocs].faults[2],
            &procs[nprocs].faults[3],
            &procs[nprocs].faults[4],
            &procs[nprocs].faults[5],
            &procs[nprocs].faults[6],
            &procs[nprocs].faults[7]
        );

        if (matched >= 4 && matched == 4 + procs[nprocs].nfaults) {
            nprocs++;
        }
    }
    fclose(fp);

    int global_time = 0;
    int ran[MAX_PROCS];

    // Multi-round scheduling
    while (has_unfinished_processes(procs, nprocs)) {
        for (int i = 0; i < nprocs; i++) {
            ran[i] = 0;
        }

        while (1) {
            int slice = 0;
            int best = cfg.pick(procs, nprocs, ran, &cfg, &slice);
            if (best == -1) {
                break;
            }

            global_time += run_quantum(&procs[best], slice, cfg.fault_penalty);
            ran[best] = 1;

            // Process completed, record completion time and output
            if (procs[best].cpuTime >= procs[best].total_time) {
                procs[best].completion_time = global_time;
                printf("%s %d\n", procs[best].name, global_time);
            }
        }
    }

    if (cfg.show_stats) {
        print_statistics(procs, nprocs, global_time, &cfg);
    }

    return EXIT_SUCCESS;
}