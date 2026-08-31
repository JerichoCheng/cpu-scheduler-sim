#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LEN           256
#define MAX_PROCS              8
#define MAX_FAULTS             8
#define DEFAULT_FAULT_PENALTY  4
#define DEFAULT_QUANTUM        10

enum output_format {
    FORMAT_HUMAN,
    FORMAT_CSV
};

struct process {
    char name[10];
    int priority;
    int total_time;
    int arrival_time;
    int nfaults;
    int faults[MAX_FAULTS];
    int cpuTime;
    int completion_time;
    int faults_triggered;
};

struct config;

// Function pointer type for process selection and time-slice allocation
typedef int (*policy_fn)(const struct process procs[], int nprocs, const int eligible[], const struct config *cfg, int *slice);

struct config {
    int quantum;
    int fault_penalty;
    int show_stats;
    enum output_format format;
    policy_fn pick;
    const char *policy_name;
    int arrivals[MAX_PROCS];
    int arrivals_count;
};

struct stats {
    int total_time;
    double avg_turnaround;
    double avg_waiting_total;
    double avg_waiting_queued;
    double cpu_utilisation;
    int faults_triggered;
    int total_penalty;
};

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

// Policy: Priority Round-Robin (lowest priority number first, ties broken by file order)
static int policy_priority_rr(const struct process procs[], int nprocs, const int eligible[], const struct config *cfg, int *slice) {
    int best = -1;
    for (int i = 0; i < nprocs; i++) {
        if (eligible[i]) {
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
static int policy_rr(const struct process procs[], int nprocs, const int eligible[], const struct config *cfg, int *slice) {
    (void)procs;
    for (int i = 0; i < nprocs; i++) {
        if (eligible[i]) {
            *slice = cfg->quantum;
            return i;
        }
    }
    return -1;
}

// Policy: Shortest Job First (non-preemptive: picks smallest total_time and runs to completion)
static int policy_sjf(const struct process procs[], int nprocs, const int eligible[], const struct config *cfg, int *slice) {
    (void)cfg;
    int best = -1;
    for (int i = 0; i < nprocs; i++) {
        if (eligible[i]) {
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

// Parse comma-separated arrival times
static int parse_arrivals(const char *str, int arrivals[], int *count) {
    *count = 0;
    const char *curr = str;
    while (*curr != '\0') {
        if (*count >= MAX_PROCS) {
            return 0;
        }
        char *end;
        long val = strtol(curr, &end, 10);
        if (end == curr || val < 0) {
            return 0;
        }
        arrivals[(*count)++] = (int)val;
        if (*end == ',') {
            curr = end + 1;
            if (*curr == '\0') {
                return 0;
            }
        } else if (*end == '\0') {
            break;
        } else {
            return 0;
        }
    }
    return 1;
}

// Compute performance metrics from completed processes
static struct stats compute_stats(const struct process procs[], int nprocs, int total_time, int fault_penalty) {
    struct stats s = {0};
    if (nprocs == 0) {
        return s;
    }

    int total_cpu_time = 0;
    int total_turnaround = 0;
    int total_waiting = 0;
    int total_faults = 0;

    for (int i = 0; i < nprocs; i++) {
        int turnaround = procs[i].completion_time - procs[i].arrival_time;
        int waiting = turnaround - procs[i].total_time;

        total_cpu_time += procs[i].total_time;
        total_turnaround += turnaround;
        total_waiting += waiting;
        total_faults += procs[i].faults_triggered;
    }

    int total_penalty = total_faults * fault_penalty;
    int total_queued = total_waiting - total_penalty;

    s.total_time = total_time;
    s.avg_turnaround = (double)total_turnaround / nprocs;
    s.avg_waiting_total = (double)total_waiting / nprocs;
    s.avg_waiting_queued = (double)total_queued / nprocs;
    s.cpu_utilisation = (total_time > 0) ? ((double)total_cpu_time / total_time) * 100.0 : 0.0;
    s.faults_triggered = total_faults;
    s.total_penalty = total_penalty;

    return s;
}

// Print statistics in human-readable format to stderr
static void print_stats_human(const struct stats *s, const char *policy_name) {
    fprintf(stderr, "--- Statistics ---\n");
    fprintf(stderr, "Scheduling policy: %s\n", policy_name);
    fprintf(stderr, "Total execution time: %d\n", s->total_time);
    fprintf(stderr, "Average turnaround time: %.2f\n", s->avg_turnaround);
    fprintf(stderr, "Average waiting time (total): %.2f\n", s->avg_waiting_total);
    fprintf(stderr, "Average waiting time (queued only): %.2f\n", s->avg_waiting_queued);
    fprintf(stderr, "CPU utilisation: %.2f%%\n", s->cpu_utilisation);
    fprintf(stderr, "Total page faults triggered: %d\n", s->faults_triggered);
    fprintf(stderr, "Total fault penalty: %d\n", s->total_penalty);
}

// Print statistics in CSV format to stdout
static void print_stats_csv(const struct stats *s, const char *policy_name) {
    printf("policy,total_time,avg_turnaround,avg_waiting_total,avg_waiting_queued,cpu_utilisation,faults_triggered,total_penalty\n");
    printf("%s,%d,%.2f,%.2f,%.2f,%.2f,%d,%d\n",
           policy_name,
           s->total_time,
           s->avg_turnaround,
           s->avg_waiting_total,
           s->avg_waiting_queued,
           s->cpu_utilisation,
           s->faults_triggered,
           s->total_penalty);
}

int main(int argc, char *argv[]) {
    struct config cfg = {
        .quantum = DEFAULT_QUANTUM,
        .fault_penalty = DEFAULT_FAULT_PENALTY,
        .show_stats = 0,
        .format = FORMAT_HUMAN,
        .pick = policy_priority_rr,
        .policy_name = "priority-rr",
        .arrivals = {0},
        .arrivals_count = 0
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
        } else if (strcmp(argv[i], "--format") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing argument for --format\n");
                return EXIT_FAILURE;
            }
            if (strcmp(argv[i + 1], "human") == 0) {
                cfg.format = FORMAT_HUMAN;
            } else if (strcmp(argv[i + 1], "csv") == 0) {
                cfg.format = FORMAT_CSV;
                cfg.show_stats = 1;
            } else {
                fprintf(stderr, "Error: Unknown format %s\n", argv[i + 1]);
                return EXIT_FAILURE;
            }
            i++;
        } else if (strcmp(argv[i], "--arrivals") == 0) {
            if (i + 1 >= argc || !parse_arrivals(argv[i + 1], cfg.arrivals, &cfg.arrivals_count)) {
                fprintf(stderr, "Error: Invalid or missing argument for --arrivals\n");
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
        fprintf(stderr, "Usage: %s [--quantum N] [--fault-penalty N] [--policy <priority-rr|rr|sjf>] [--format <human|csv>] [--arrivals <csv>] [--stats] <input-file>\n", argv[0]);
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
        procs[nprocs].arrival_time = 0;
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

    if (cfg.arrivals_count > 0) {
        if (cfg.arrivals_count != nprocs) {
            fprintf(stderr, "Error: Number of arrival times (%d) does not match process count (%d)\n", cfg.arrivals_count, nprocs);
            return EXIT_FAILURE;
        }
        for (int i = 0; i < nprocs; i++) {
            procs[i].arrival_time = cfg.arrivals[i];
        }
    }

    int global_time = 0;
    int ran[MAX_PROCS] = {0};

    // Multi-round scheduling
    while (has_unfinished_processes(procs, nprocs)) {
        int eligible[MAX_PROCS];
        for (int i = 0; i < nprocs; i++) {
            eligible[i] = (!ran[i] && procs[i].cpuTime < procs[i].total_time && procs[i].arrival_time <= global_time);
        }

        int slice = 0;
        int best = cfg.pick(procs, nprocs, eligible, &cfg, &slice);

        if (best != -1) {
            global_time += run_quantum(&procs[best], slice, cfg.fault_penalty);
            ran[best] = 1;

            if (procs[best].cpuTime >= procs[best].total_time) {
                procs[best].completion_time = global_time;
                if (cfg.format != FORMAT_CSV) {
                    printf("%s %d\n", procs[best].name, global_time);
                }
            }
        } else {
            int has_arrived_unfinished = 0;
            int next_arrival = -1;

            for (int i = 0; i < nprocs; i++) {
                if (procs[i].cpuTime < procs[i].total_time) {
                    if (procs[i].arrival_time <= global_time) {
                        has_arrived_unfinished = 1;
                    } else {
                        if (next_arrival == -1 || procs[i].arrival_time < next_arrival) {
                            next_arrival = procs[i].arrival_time;
                        }
                    }
                }
            }

            if (has_arrived_unfinished) {
                for (int i = 0; i < nprocs; i++) {
                    ran[i] = 0;
                }
            } else {
                if (next_arrival <= global_time) {
                    fprintf(stderr, "Error: Scheduler internal logic error advancing idle time\n");
                    return EXIT_FAILURE;
                }
                global_time = next_arrival;
                for (int i = 0; i < nprocs; i++) {
                    ran[i] = 0;
                }
            }
        }
    }

    if (cfg.show_stats) {
        struct stats s = compute_stats(procs, nprocs, global_time, cfg.fault_penalty);
        if (cfg.format == FORMAT_CSV) {
            print_stats_csv(&s, cfg.policy_name);
        } else {
            print_stats_human(&s, cfg.policy_name);
        }
    }

    return EXIT_SUCCESS;
}