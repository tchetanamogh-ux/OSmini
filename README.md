# CampusCompute – OS Mini-Project

## Requirements
Ubuntu / WSL Ubuntu with GCC and Make (`sudo apt install build-essential`).

## Build
```sh
make
```

## Commands
```sh
./campus help
./campus fcfs [jobs.txt]
./campus rr [jobs.txt]
./campus prio [jobs.txt]
./campus compare [jobs.txt]
./campus logger
./campus ipc
./campus banker safe
./campus banker unsafe
./campus worker ok
./campus worker fail
./campus worker crash
./campus limit
./campus all [jobs.txt]
```

Each job-file line must contain exactly four integers: `id arrival burst priority`.
IDs must be unique. Arrival time and priority must be non-negative; burst must be 1–100.
At most 10 jobs are accepted.

## Scheduling rules
- Ties: earlier arrival, then lower job ID.
- Lower priority number means higher priority.
- Round Robin time quantum: 2.

Worker failures are controlled simulations (exit code 3 or self-sent SIGTERM), not actual OS failures.

## Cleanup
```sh
make clean
```
This removes only the executable and generated log. The program removes the shared-memory
and semaphore objects it creates (`/campus_shm` and `/campus_sem`). IPC names are created
exclusively so an existing object is not overwritten or unlinked by another run. If a prior
crash leaves one behind, inspect it before removing it; do not delete unrelated IPC objects.
