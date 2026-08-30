#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LEN           256
#define MAX_PROCS              8
#define MAX_FAULTS             8
#define DEFAULT_FAULT_PENALTY  4
#define DEFAULT_QUANTUM        10

struct config {
    int quantum;
    int fault_penalty;
};

struct process {
    char name[10];
    int priority;
    int total_time;
    int nfaults;
    int faults[MAX_FAULTS];
    int cpuTime;
};

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

// Advances process execution by one time quantum and calculates page fault penalties
static int run_quantum(struct process *p, const struct config *cfg) {
    int start_time = p->cpuTime;
    int advance = min_int(cfg->quantum, p->total_time - start_time);
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
    return advance + (faults_in_window * cfg->fault_penalty);
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

int main(int argc, char *argv[]) {
    struct config cfg = {
        .quantum = DEFAULT_QUANTUM,
        .fault_penalty = DEFAULT_FAULT_PENALTY
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
        fprintf(stderr, "Usage: %s [--quantum N] [--fault-penalty N] <input-file>\n", argv[0]);
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
            int best = -1;

            // Break ties by original file order: scanning from index 0 with strict inequality ('<') 
            // naturally preserves the process that appeared earlier in the input file
            for (int i = 0; i < nprocs; i++) {
                if (!ran[i] && procs[i].cpuTime < procs[i].total_time) {
                    if (best == -1 || procs[i].priority < procs[best].priority) {
                        best = i;
                    }
                }
            }

            if (best == -1) {
                break;
            }

            global_time += run_quantum(&procs[best], &cfg);
            ran[best] = 1;

            // Process completed, output results
            if (procs[best].cpuTime >= procs[best].total_time) {
                printf("%s %d\n", procs[best].name, global_time);
            }
        }
    }

    return EXIT_SUCCESS;
}