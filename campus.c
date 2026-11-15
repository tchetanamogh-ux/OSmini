// CampusCompute - OS Level 3 mini project   (build: make, run: ./campus help)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>

#define FOR(i, n) for (int i = 0; i < (n); i++)
#define P if (!quiet) printf            // print only when not quiet (compare runs quietly)
#define MAXJ 10
#define QUANTUM 2                       // Round Robin time quantum
#define NP 5                            // Banker's: processes
#define NR 3                            // Banker's: resource types
#define SHM_NAME "/campus_shm"
#define SEM_NAME "/campus_sem"

/* FR1: jobs (lower pr number = higher priority) */
typedef struct { int id, at, bt, pr, rem, ct, tat, wt, done; } Job;
Job jobs[MAXJ];
int njobs, quiet;
double avg_tat, avg_wt;

int add_job(int id, int at, int bt, int pr) {
    if (njobs == MAXJ || id <= 0 || at < 0 || bt <= 0 || bt > 100 || pr < 0) {
        fprintf(stderr, "Error: bad job (max %d jobs; id>0, arrival>=0, 0<burst<=100, priority>=0)\n", MAXJ);
        return -1;
    }
    FOR(i, njobs) if (jobs[i].id == id) {
        fprintf(stderr, "Error: duplicate job ID %d\n", id);
        return -1;
    }
    jobs[njobs++] = (Job){.id = id, .at = at, .bt = bt, .pr = pr, .rem = bt};
    return 0;
}

// tie-breaking rule everywhere: earlier arrival first, then lower job id
int by_arrival(const void *a, const void *b) {
    const Job *x = a, *y = b;
    return x->at != y->at ? x->at - y->at : x->id - y->id;
}

// reads lines "id arrival burst priority" from a file, or from built-in demo jobs if file is NULL
int load_jobs(const char *file) {
    const char *demo = "1 0 5 2\n2 1 3 1\n3 2 8 4\n4 3 2 1\n5 4 4 3\n";
    FILE *f = file ? fopen(file, "r") : fmemopen((void *)demo, strlen(demo), "r");
    if (!f) { perror("fopen"); return -1; }
    char line[100], extra;
    int ln = 0, v[4], r;
    njobs = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        r = sscanf(line, " %d %d %d %d %c", &v[0], &v[1], &v[2], &v[3], &extra);
        if (r == EOF) continue;
        if (r != 4) {
            fprintf(stderr, "Error: line %d must contain exactly four integers\n", ln);
            fclose(f);
            return -1;
        }
        if (add_job(v[0], v[1], v[2], v[3]) < 0) { fclose(f); return -1; }
    }
    fclose(f);
    if (!njobs) { fprintf(stderr, "Error: no jobs in file\n"); return -1; }
    qsort(jobs, njobs, sizeof(Job), by_arrival);
    return 0;
}

/* FR2: scheduling */
void start(const char *title) {         // reset jobs before each run
    FOR(i, njobs) { jobs[i].rem = jobs[i].bt; jobs[i].done = 0; }
    P("\n=== %s ===\n", title);
}

// TAT = CT - AT, WT = TAT - BT; prints the table and averages
void report(void) {
    double st = 0, sw = 0;
    P("  Job  AT  BT  PR   CT  TAT   WT  Status\n");
    FOR(i, njobs) {
        Job *j = &jobs[i];
        j->tat = j->ct - j->at;
        j->wt = j->tat - j->bt;
        st += j->tat; sw += j->wt;
        P("  J%-2d %3d %3d %3d %4d %4d %4d  %s\n", j->id, j->at, j->bt, j->pr, j->ct, j->tat, j->wt, j->done ? "DONE" : "NEW");
    }
    avg_tat = st / njobs; avg_wt = sw / njobs;
    P("  Average TAT = %.2f, Average WT = %.2f\n", avg_tat, avg_wt);
}

// non-preemptive: use_pr = 0 gives FCFS, use_pr = 1 gives Priority
void nonpreemptive(const char *title, int use_pr) {
    start(title);
    int t = 0;
    for (int k = 0; k < njobs; k++) {
        int b = -1;
        FOR(i, njobs)                   // array is sorted, so the first match wins ties
            if (!jobs[i].done && jobs[i].at <= t && (b < 0 || (use_pr && jobs[i].pr < jobs[b].pr))) b = i;
        if (b < 0) {                    // nothing arrived yet: CPU idle, jump to next arrival
            FOR(i, njobs) if (!jobs[i].done) { t = jobs[i].at; break; }
            k--; continue;
        }
        P("  [%2d - %2d] J%d\n", t, t + jobs[b].bt, jobs[b].id);
        t += jobs[b].bt;
        jobs[b].ct = t; jobs[b].done = 1;
    }
    report();
}
void fcfs(void) { nonpreemptive("FCFS (tie: earlier arrival, then lower job ID)", 0); }
void prio(void) { nonpreemptive("Priority, non-preemptive (lower number = higher priority; tie: earlier arrival, then lower ID)", 1); }

void rr(void) {
    char title[40];
    sprintf(title, "Round Robin (quantum = %d)", QUANTUM);
    start(title);
    int q[2000], h = 0, tl = 0, in[MAXJ] = {0}, t = 0, left = njobs;
    while (left > 0) {
        FOR(j, njobs) if (!in[j] && jobs[j].at <= t) { q[tl++] = j; in[j] = 1; }   // queue arrived jobs
        if (h == tl) {                  // queue empty: jump to next arrival
            FOR(j, njobs) if (!in[j]) { t = jobs[j].at; break; }
            continue;
        }
        int i = q[h++], run = jobs[i].rem < QUANTUM ? jobs[i].rem : QUANTUM;
        P("  [%2d - %2d] J%d\n", t, t + run, jobs[i].id);
        t += run;
        jobs[i].rem -= run;
        FOR(j, njobs) if (!in[j] && jobs[j].at <= t) { q[tl++] = j; in[j] = 1; }   // arrivals go before the preempted job
        if (jobs[i].rem > 0) q[tl++] = i;
        else { jobs[i].ct = t; jobs[i].done = 1; left--; }
    }
    report();
}

void compare(void) {
    void (*run[3])(void) = {fcfs, rr, prio};
    char rrname[16];
    sprintf(rrname, "RR (q=%d)", QUANTUM);
    const char *name[3] = {"FCFS", rrname, "Priority"};
    double a[3][2];
    quiet = 1;
    FOR(i, 3) { run[i](); a[i][0] = avg_tat; a[i][1] = avg_wt; }
    quiet = 0;
    printf("\n=== Comparison (same job set) ===\n  Policy       Avg TAT   Avg WT\n");
    FOR(i, 3) printf("  %-12s %7.2f  %7.2f\n", name[i], a[i][0], a[i][1]);
}

/* FR3: producer-consumer logger (bounded queue, mutex + condition variables) */
#define QN 4
char qbuf[QN][64];
int qh, qt, qc;                         // head, tail, count
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t not_full = PTHREAD_COND_INITIALIZER, not_empty = PTHREAD_COND_INITIALIZER;

void *producer(void *arg) {
    int id = (int)(long)arg;
    for (int i = 1; i <= 5; i++) {
        pthread_mutex_lock(&m);
        while (qc == QN) pthread_cond_wait(&not_full, &m);       // queue full: wait
        snprintf(qbuf[qt], 64, "Producer %d: job J%d submitted", id, i);
        printf("  [P%d] queued: %s (queue size=%d)\n", id, qbuf[qt], qc + 1);
        qt = (qt + 1) % QN;
        qc++;
        pthread_cond_signal(&not_empty);
        pthread_mutex_unlock(&m);
        usleep(10000 * id);
    }
    return NULL;
}

void *consumer(void *arg) {
    for (int k = 1; k <= 10; k++) {                              // 2 producers x 5 messages
        pthread_mutex_lock(&m);
        while (qc == 0) pthread_cond_wait(&not_empty, &m);       // queue empty: wait
        fprintf((FILE *)arg, "Entry %d: %s\n", k, qbuf[qh]);
        qh = (qh + 1) % QN;
        qc--;
        printf("  [C ] logged entry %d\n", k);
        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&m);
        usleep(20000);
    }
    return NULL;
}

int logger(void) {
    printf("\n=== Producer-Consumer Logger ===\n");
    FILE *f = fopen("campus.log", "w");
    if (!f) { perror("fopen"); return -1; }
    pthread_t p[2], c;
    qh = qt = qc = 0;
    if (pthread_create(&c, NULL, consumer, f)) { perror("pthread_create"); exit(1); }
    for (long i = 0; i < 2; i++)
        if (pthread_create(&p[i], NULL, producer, (void *)(i + 1))) { perror("pthread_create"); exit(1); }
    FOR(i, 2) pthread_join(p[i], NULL);
    pthread_join(c, NULL);
    fclose(f);
    printf("  All threads joined cleanly. Log saved in campus.log\n");
    return 0;
}

/* FR4: shared memory + named semaphore */
typedef struct { int n; int burst[5]; } Shared;

int ipc_demo(void) {
    printf("\n=== Shared Memory IPC + Named Semaphore ===\n");
    int rc = -1, sum = 0, c, fd = -1;
    int made_shm = 0, made_sem = 0;
    Shared *sh = MAP_FAILED;
    sem_t *sem = SEM_FAILED;
    pid_t pid;

    fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open (resource may already exist)"); goto out; }
    made_shm = 1;
    if (ftruncate(fd, sizeof(Shared)) < 0) { perror("ftruncate"); goto out; }
    sh = mmap(NULL, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (sh == MAP_FAILED) { perror("mmap"); goto out; }
    sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0600, 0);
    if (sem == SEM_FAILED) { perror("sem_open (resource may already exist)"); goto out; }
    made_sem = 1;

    fflush(stdout);
    if ((pid = fork()) < 0) { perror("fork"); goto out; }
    if (pid == 0) {                     // child = job submitter
        int b[5] = {5, 3, 8, 2, 4};
        sh->n = 5;
        FOR(i, 5) { sh->burst[i] = b[i]; printf("  [child %d] wrote burst %d\n", getpid(), b[i]); }
        sem_post(sem);                  // tell parent the data is ready
        exit(0);
    }
    while (sem_wait(sem) < 0) {
        if (errno == EINTR) continue;
        perror("sem_wait");
        waitpid(pid, NULL, 0);
        goto out;
    }
    FOR(i, sh->n) sum += sh->burst[i];
    printf("  [parent %d] read %d bursts, total CPU burst = %d\n", getpid(), sh->n, sum);
    waitpid(pid, NULL, 0);
    rc = 0;
out:
    if (sh != MAP_FAILED) munmap(sh, sizeof(Shared));
    if (fd >= 0) close(fd);
    if (sem != SEM_FAILED) sem_close(sem);
    if (made_shm) shm_unlink(SHM_NAME);
    if (made_sem) sem_unlink(SEM_NAME);
    c = shm_open(SHM_NAME, O_RDONLY, 0);                         // verify cleanup: both names should be gone
    printf(c < 0 && errno == ENOENT && sem_open(SEM_NAME, 0) == SEM_FAILED ?
           "  Cleanup verified: %s and %s removed\n" : "  WARNING: IPC objects still exist (%s, %s)\n", SHM_NAME, SEM_NAME);
    if (c >= 0) close(c);
    return rc;
}

/* FR5: Banker's safety algorithm */
int safety(int av[NR], int al[NP][NR], int mx[NP][NR]) {
    int need[NP][NR], work[NR], fin[NP] = {0}, seq[NP], cnt = 0;
    printf("  Available: %d %d %d\n", av[0], av[1], av[2]);
    FOR(i, NP) {
        FOR(j, NR) need[i][j] = mx[i][j] - al[i][j];             // Need = Max - Allocation
        printf("  P%d: alloc %d %d %d | max %d %d %d | need %d %d %d\n", i, al[i][0], al[i][1], al[i][2],
               mx[i][0], mx[i][1], mx[i][2], need[i][0], need[i][1], need[i][2]);
    }
    memcpy(work, av, sizeof work);
    for (int found = 1; found && cnt < NP;) {
        found = 0;
        FOR(i, NP) {
            int ok = !fin[i];
            FOR(j, NR) if (need[i][j] > work[j]) ok = 0;
            if (!ok) continue;
            FOR(j, NR) work[j] += al[i][j];                      // P(i) finishes, returns its resources
            fin[i] = found = 1;
            seq[cnt++] = i;
        }
    }
    if (cnt == NP) {
        printf("  SAFE state. Safe sequence: ");
        FOR(i, NP) printf("P%d%s", seq[i], i < NP - 1 ? " -> " : "\n");
    } else {
        printf("  UNSAFE state! No process can finish. Stuck processes:");
        FOR(i, NP) if (!fin[i]) printf(" P%d", i);
        printf("\n");
    }
    return cnt == NP;
}

void banker(const char *mode) {
    int al[NP][NR] = {{0,1,0},{2,0,0},{3,0,2},{2,1,1},{0,0,2}};
    int mx[NP][NR] = {{7,5,3},{3,2,2},{9,0,2},{2,2,2},{4,3,3}};
    int av[2][NR] = {{3,3,2}, {1,0,0}};    // row 0 = safe test, row 1 = unsafe test
    printf("\n=== Banker's - %s test ===\n", mode);
    safety(av[strcmp(mode, "safe") != 0], al, mx);
}

/* FR6: worker processes (failures are simulated) */
int worker(const char *mode) {
    printf("\n=== Worker process ===\n");
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        printf("  [worker %d] started (mode=%s)\n", getpid(), mode);
        if (!strcmp(mode, "fail")) {
            printf("  [worker %d] SIMULATED FAILURE: exiting with code 3 (not a real OS failure)\n", getpid());
            exit(3);
        }
        if (!strcmp(mode, "crash")) {
            printf("  [worker %d] SIMULATED CRASH: sending myself SIGTERM (not a real OS failure)\n", getpid());
            fflush(stdout);
            raise(SIGTERM);
        }
        usleep(100000);                 // pretend to work
        printf("  [worker %d] work finished\n", getpid());
        exit(0);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); return -1; }
    if (WIFEXITED(st))
        printf("  [parent] worker %d exited with status %d -> %s\n", pid, WEXITSTATUS(st),
               WEXITSTATUS(st) == 0 ? "NORMAL completion" : "FAILURE detected (simulated)");
    else if (WIFSIGNALED(st))
        printf("  [parent] worker %d killed by signal %d (%s) -> FAILURE detected (simulated)\n",
               pid, WTERMSIG(st), strsignal(WTERMSIG(st)));
    return 0;
}

/* FR7: resource limit (set in a child so the parent is not affected) */
int limit_demo(void) {
    printf("\n=== Resource limit (RLIMIT_NOFILE) ===\n");
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        struct rlimit rl = {8, 8};
        if (setrlimit(RLIMIT_NOFILE, &rl) < 0) { perror("setrlimit"); exit(1); }
        printf("  [child] max open files set to %d\n", (int)rl.rlim_cur);
        int fds[20], n = 0;
        while (n < 20 && (fds[n] = open("/dev/null", O_RDONLY)) >= 0) n++;
        if (n < 20) printf("  [child] open #%d failed: %s\n", n + 1, strerror(errno));
        printf("  [child] opened %d extra files before limit was hit (fds 0,1,2 already used)\n", n);
        FOR(i, n) close(fds[i]);
        exit(0);
    }
    waitpid(pid, NULL, 0);
    printf("  [parent] child done, parent limits unchanged\n");
    return 0;
}

/* FR8: command line */
void usage(void) {
    printf("CampusCompute usage: ./campus <command> [argument]\n"
           "  fcfs [jobfile]      FCFS scheduling\n"
           "  rr [jobfile]        Round Robin scheduling (quantum %d)\n"
           "  prio [jobfile]      Priority scheduling (non-preemptive)\n"
           "  compare [jobfile]   compare all three policies\n"
           "  logger              producer-consumer logger (writes campus.log)\n"
           "  ipc                 shared memory + semaphore demo\n"
           "  banker safe|unsafe  Banker's safety algorithm\n"
           "  worker ok|fail|crash  fork/waitpid worker demo (failures are simulated)\n"
           "  limit               setrlimit demo\n"
           "  all [jobfile]       run everything and print a summary\n"
           "  help                show this message\n"
           "Job file lines: id arrival burst priority\n", QUANTUM);
}

int run_all(const char *file) {
    if (load_jobs(file) < 0) return 1;
    fcfs(); rr(); prio(); compare();
    int fails = (logger() < 0) + (ipc_demo() < 0);
    banker("safe"); banker("unsafe");
    fails += (worker("ok") < 0) + (worker("fail") < 0) + (limit_demo() < 0);
    printf("\n=== SUMMARY ===\n  Scheduling, logger, IPC, Banker's, workers, limit demos executed.\n");
    printf("  Result: %s\n", fails ? "some demos FAILED" : "all demos completed OK");
    return fails != 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        if (argc > 3) fprintf(stderr, "Error: too many arguments\n");
        usage();
        return 1;
    }
    const char *cmd = argv[1], *arg = argc > 2 ? argv[2] : NULL;
#define IS(s) (!strcmp(cmd, s))
#define ARG(s) (arg && !strcmp(arg, s))
    if (IS("help")) {
        if (arg) { fprintf(stderr, "Error: help takes no arguments\n"); usage(); return 1; }
        usage();
        return 0;
    }
    if (IS("fcfs") || IS("rr") || IS("prio") || IS("compare")) {
        if (load_jobs(arg) < 0) return 1;
        (IS("fcfs") ? fcfs : IS("rr") ? rr : IS("prio") ? prio : compare)();
        return 0;
    }
    if (IS("logger") || IS("ipc") || IS("limit")) {
        if (arg) { fprintf(stderr, "Error: '%s' takes no arguments\n", cmd); usage(); return 1; }
        if (IS("logger")) return logger() < 0;
        if (IS("ipc")) return ipc_demo() < 0;
        return limit_demo() < 0;
    }
    if (IS("all")) return run_all(arg);
    if (IS("banker") && (ARG("safe") || ARG("unsafe"))) { banker(arg); return 0; }
    if (IS("worker") && (ARG("ok") || ARG("fail") || ARG("crash"))) return worker(arg) < 0;
    if (IS("banker") || IS("worker")) fprintf(stderr, "Error: bad or missing argument for '%s'\n", cmd);
    else fprintf(stderr, "Error: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
