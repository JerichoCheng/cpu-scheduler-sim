// Needed so glibc exposes getline() under the strict -std=c11 the Makefile builds with.
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY       4
#define DEFAULT_FAULT_PENALTY  4
#define DEFAULT_QUANTUM        10

enum output_format {
    FORMAT_HUMAN,
    FORMAT_CSV
};

struct process {
    char *name;          // dynamically allocated, exactly strlen(name) + 1 bytes
    int priority;
    int total_time;
    int arrival_time;
    int nfaults;
    int *faults;         // dynamically allocated, exactly nfaults entries (NULL if nfaults == 0)
    int cpuTime;
    int completion_time;
    int faults_triggered;
};

struct config;

// Function pointer type for process selection and time-slice allocation
typedef int (*policy_fn)(const struct process procs[], int nprocs, const int eligible[], const struct config *cfg, int *slice);

struct policy {
    policy_fn pick;
    const char *name;
    int uses_pass; // 1 = constrained by ran[] within a pass, 0 = re-evaluates across all eligible processes each turn
};

struct config {
    int quantum;
    int fault_penalty;
    int show_stats;
    enum output_format format;
    const struct policy *policy;
    int *arrivals;        // dynamically allocated, exactly arrivals_count entries
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

// Policy: Shortest Remaining Time First (preemptive: picks smallest remaining CPU time, allocated by quantum)
static int policy_srtf(const struct process procs[], int nprocs, const int eligible[], const struct config *cfg, int *slice) {
    int best = -1;
    for (int i = 0; i < nprocs; i++) {
        if (eligible[i]) {
            int rem_curr = procs[i].total_time - procs[i].cpuTime;
            if (best == -1) {
                best = i;
            } else {
                int rem_best = procs[best].total_time - procs[best].cpuTime;
                if (rem_curr < rem_best) {
                    best = i;
                }
            }
        }
    }
    if (best != -1) {
        *slice = cfg->quantum;
    }
    return best;
}

// Table of available scheduling policies
static const struct policy POLICIES[] = {
    { policy_priority_rr, "priority-rr", 1 },
    { policy_rr,          "rr",          1 },
    { policy_sjf,         "sjf",         1 },
    { policy_srtf,        "srtf",        0 }
};

#define NUM_POLICIES (sizeof(POLICIES) / sizeof(POLICIES[0]))

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

// Release every process's dynamically-allocated name and fault list, then the table itself
static void free_procs(struct process *procs, int nprocs) {
    if (procs == NULL) {
        return;
    }
    for (int i = 0; i < nprocs; i++) {
        free(procs[i].name);
        free(procs[i].faults);
    }
    free(procs);
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

// Parse comma-separated arrival times into a freshly malloc'd array. On success, *out
// receives the array (caller owns it and must free it) and *count its length. The
// argument list is not bounded to a fixed process count: it grows the same way the
// process table does, by doubling capacity as needed.
static int parse_arrivals(const char *str, int **out, int *count) {
    int capacity = INITIAL_CAPACITY;
    int n = 0;
    int *arrivals = malloc((size_t)capacity * sizeof(int));
    if (arrivals == NULL) {
        return 0;
    }

    const char *curr = str;
    while (*curr != '\0') {
        char *end;
        long val = strtol(curr, &end, 10);
        if (end == curr || val < 0) {
            free(arrivals);
            return 0;
        }

        if (n == capacity) {
            capacity *= 2;
            int *grown = realloc(arrivals, (size_t)capacity * sizeof(int));
            if (grown == NULL) {
                free(arrivals);
                return 0;
            }
            arrivals = grown;
        }
        arrivals[n++] = (int)val;

        if (*end == ',') {
            curr = end + 1;
            if (*curr == '\0') {
                free(arrivals);
                return 0;
            }
        } else if (*end == '\0') {
            break;
        } else {
            free(arrivals);
            return 0;
        }
    }

    *out = arrivals;
    *count = n;
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

// Helper to format available policies into usage string
static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [--quantum N] [--fault-penalty N] [--policy <", prog_name);
    for (size_t i = 0; i < NUM_POLICIES; i++) {
        fprintf(stderr, "%s%s", POLICIES[i].name, (i + 1 < NUM_POLICIES) ? "|" : "");
    }
    fprintf(stderr, ">] [--format <human|csv>] [--arrivals <csv>] [--stats] <input-file>\n");
}

int main(int argc, char *argv[]) {
    struct config cfg = {
        .quantum = DEFAULT_QUANTUM,
        .fault_penalty = DEFAULT_FAULT_PENALTY,
        .show_stats = 0,
        .format = FORMAT_HUMAN,
        .policy = &POLICIES[0],
        .arrivals = NULL,
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
            int found = 0;
            for (size_t j = 0; j < NUM_POLICIES; j++) {
                if (strcmp(argv[i + 1], POLICIES[j].name) == 0) {
                    cfg.policy = &POLICIES[j];
                    found = 1;
                    break;
                }
            }
            if (!found) {
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
            if (i + 1 >= argc || !parse_arrivals(argv[i + 1], &cfg.arrivals, &cfg.arrivals_count)) {
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
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    FILE *fp = fopen(input_file, "r");
    if (fp == NULL) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    int capacity = INITIAL_CAPACITY;
    int nprocs = 0;
    struct process *procs = malloc((size_t)capacity * sizeof(struct process));
    if (procs == NULL) {
        perror("malloc failed");
        fclose(fp);
        return EXIT_FAILURE;
    }

    // Read one process record per line, growing both the process table and each
    // process's own name/fault-list allocations to exactly what the line needs —
    // nothing here assumes a maximum process count, name length, or fault count.
    char *line = NULL;
    size_t line_cap = 0;

    while (getline(&line, &line_cap, fp) != -1) {
        char *p = line;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            continue; // skip blank lines
        }

        // Process name runs up to the next whitespace character.
        char *name_start = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
        size_t name_len = (size_t)(p - name_start);

        char *name = malloc(name_len + 1);
        if (name == NULL) {
            perror("malloc failed");
            break;
        }
        memcpy(name, name_start, name_len);
        name[name_len] = '\0';

        // Chain strtol calls through the three fixed fields: priority, total_time, nfaults.
        char *next = NULL;
        long priority = strtol(p, &next, 10);
        p = next;
        long total_time = strtol(p, &next, 10);
        p = next;
        long nfaults = strtol(p, &next, 10);
        p = next;

        int *faults = NULL;
        if (nfaults > 0) {
            faults = malloc((size_t)nfaults * sizeof(int));
            if (faults == NULL) {
                perror("malloc failed");
                free(name);
                break;
            }
            for (long i = 0; i < nfaults; i++) {
                faults[i] = (int)strtol(p, &next, 10);
                p = next;
            }
        }

        if (nprocs == capacity) {
            capacity *= 2;
            struct process *grown = realloc(procs, (size_t)capacity * sizeof(struct process));
            if (grown == NULL) {
                perror("realloc failed");
                free(name);
                free(faults);
                break;
            }
            procs = grown;
        }

        procs[nprocs].name = name;
        procs[nprocs].priority = (int)priority;
        procs[nprocs].total_time = (int)total_time;
        procs[nprocs].nfaults = (int)nfaults;
        procs[nprocs].faults = faults;
        procs[nprocs].cpuTime = 0;
        procs[nprocs].arrival_time = 0;
        procs[nprocs].completion_time = 0;
        procs[nprocs].faults_triggered = 0;
        nprocs++;
    }

    free(line);
    fclose(fp);

    if (cfg.arrivals_count > 0) {
        if (cfg.arrivals_count != nprocs) {
            fprintf(stderr, "Error: Number of arrival times (%d) does not match process count (%d)\n", cfg.arrivals_count, nprocs);
            free_procs(procs, nprocs);
            free(cfg.arrivals);
            return EXIT_FAILURE;
        }
        for (int i = 0; i < nprocs; i++) {
            procs[i].arrival_time = cfg.arrivals[i];
        }
    }

    int global_time = 0;
    int *ran = calloc((size_t)nprocs, sizeof(int));
    if (nprocs > 0 && ran == NULL) {
        perror("calloc failed");
        free_procs(procs, nprocs);
        free(cfg.arrivals);
        return EXIT_FAILURE;
    }
    int *eligible = malloc((size_t)nprocs * sizeof(int));
    if (nprocs > 0 && eligible == NULL) {
        perror("malloc failed");
        free(ran);
        free_procs(procs, nprocs);
        free(cfg.arrivals);
        return EXIT_FAILURE;
    }

    // Main scheduling loop
    while (has_unfinished_processes(procs, nprocs)) {
        for (int i = 0; i < nprocs; i++) {
            eligible[i] = (procs[i].cpuTime < procs[i].total_time &&
                           procs[i].arrival_time <= global_time &&
                           (!cfg.policy->uses_pass || !ran[i]));
        }

        int slice = 0;
        int best = cfg.policy->pick(procs, nprocs, eligible, &cfg, &slice);

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
                // Pass completed: reset ran array for policies utilizing passes
                for (int i = 0; i < nprocs; i++) {
                    ran[i] = 0;
                }
            } else {
                // CPU is idle: advance clock to next arrival
                if (next_arrival <= global_time) {
                    fprintf(stderr, "Error: Scheduler internal logic error advancing idle time\n");
                    free(eligible);
                    free(ran);
                    free_procs(procs, nprocs);
                    free(cfg.arrivals);
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
            print_stats_csv(&s, cfg.policy->name);
        } else {
            print_stats_human(&s, cfg.policy->name);
        }
    }

    free(eligible);
    free(ran);
    free_procs(procs, nprocs);
    free(cfg.arrivals);

    return EXIT_SUCCESS;
}
