#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include "models.h"
#include <pthread.h>
#include <stdbool.h>

/* Concurrency Synchronization Manager */
typedef struct {
    pthread_mutex_t train_mutex;      /* Mutex for train catalog updates */
    pthread_mutex_t seat_mutex;       /* Mutex for seat inventory and atomic booking */
    pthread_mutex_t pnr_mutex;        /* Mutex for unique PNR generation */
    pthread_mutex_t audit_mutex;      /* Mutex for audit trail and WAL */
    pthread_rwlock_t inventory_rwlock;/* Read-Write lock for concurrent queries */
    int             file_lock_fd;     /* File descriptor for POSIX fcntl file locking */
    bool            is_initialized;
} ConcurrencyManager;

/* Multi-User Simulation Options and Results */
typedef struct {
    int  num_threads;                 /* Number of concurrent users/counters */
    int  bookings_per_thread;         /* Bookings requested per thread */
    int  train_no;                    /* Target train */
    char travel_date[MAX_STR_XS];     /* Target date */
    TravelClass travel_class;         /* Target class */
} SimulationConfig;

typedef struct {
    int    total_requests;
    int    successful_confirmed;
    int    successful_rac;
    int    successful_wl;
    int    rejected_overcapacity;
    int    race_conditions_detected;  /* Should strictly be 0 */
    double elapsed_seconds;
    double transactions_per_sec;
} SimulationResult;

/* Lifecycle */
int  concurrency_init(void);
void concurrency_cleanup(void);

/* Synchronization Locks */
void concurrency_lock_seats(void);
void concurrency_unlock_seats(void);
void concurrency_rdlock_inventory(void);
void concurrency_unlock_inventory(void);
void concurrency_lock_pnr(void);
void concurrency_unlock_pnr(void);
void concurrency_lock_audit(void);
void concurrency_unlock_audit(void);

/* OS Advisory File Locking (multi-process safety) */
int  concurrency_acquire_file_lock(const char *lock_file_path, bool exclusive);
int  concurrency_release_file_lock(int fd);

/* Multi-User OS Concurrency Simulation */
int  concurrency_run_multiuser_simulation(const SimulationConfig *config,
                                          SimulationResult *result);

#endif /* CONCURRENCY_H */
