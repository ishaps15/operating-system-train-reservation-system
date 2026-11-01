#include "concurrency.h"
#include "booking_service.h"
#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

static ConcurrencyManager g_cm;

int concurrency_init(void) {
    if (g_cm.is_initialized) return 0;

    pthread_mutex_init(&g_cm.train_mutex, NULL);
    pthread_mutex_init(&g_cm.seat_mutex, NULL);
    pthread_mutex_init(&g_cm.pnr_mutex, NULL);
    pthread_mutex_init(&g_cm.audit_mutex, NULL);
    pthread_rwlock_init(&g_cm.inventory_rwlock, NULL);

    g_cm.file_lock_fd = -1;
    g_cm.is_initialized = true;
    return 0;
}

void concurrency_cleanup(void) {
    if (!g_cm.is_initialized) return;

    if (g_cm.file_lock_fd >= 0) {
        close(g_cm.file_lock_fd);
        g_cm.file_lock_fd = -1;
    }

    pthread_mutex_destroy(&g_cm.train_mutex);
    pthread_mutex_destroy(&g_cm.seat_mutex);
    pthread_mutex_destroy(&g_cm.pnr_mutex);
    pthread_mutex_destroy(&g_cm.audit_mutex);
    pthread_rwlock_destroy(&g_cm.inventory_rwlock);

    g_cm.is_initialized = false;
}

void concurrency_lock_seats(void) {
    pthread_mutex_lock(&g_cm.seat_mutex);
}

void concurrency_unlock_seats(void) {
    pthread_mutex_unlock(&g_cm.seat_mutex);
}

void concurrency_rdlock_inventory(void) {
    pthread_rwlock_rdlock(&g_cm.inventory_rwlock);
}

void concurrency_unlock_inventory(void) {
    pthread_rwlock_unlock(&g_cm.inventory_rwlock);
}

void concurrency_lock_pnr(void) {
    pthread_mutex_lock(&g_cm.pnr_mutex);
}

void concurrency_unlock_pnr(void) {
    pthread_mutex_unlock(&g_cm.pnr_mutex);
}

void concurrency_lock_audit(void) {
    pthread_mutex_lock(&g_cm.audit_mutex);
}

void concurrency_unlock_audit(void) {
    pthread_mutex_unlock(&g_cm.audit_mutex);
}

int concurrency_acquire_file_lock(const char *lock_file_path, bool exclusive) {
    int fd = open(lock_file_path ? lock_file_path : "data/railway.lock",
                  O_RDWR | O_CREAT, 0666);
    if (fd < 0) return -1;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = exclusive ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int concurrency_release_file_lock(int fd) {
    if (fd < 0) return 0;
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    fcntl(fd, F_SETLK, &fl);
    close(fd);
    return 0;
}

/* ============================================================================
 * Multi-User OS Concurrency Simulation Worker
 * ============================================================================ */

typedef struct {
    int  thread_id;
    int  user_id;
    char counter_name[32];
    SimulationConfig config;
    int  successful_cnf;
    int  successful_rac;
    int  successful_wl;
    int  rejected;
} WorkerContext;

static void* simulation_worker(void *arg) {
    WorkerContext *ctx = (WorkerContext*)arg;

    for (int i = 0; i < ctx->config.bookings_per_thread; i++) {
        BookingRequest req;
        memset(&req, 0, sizeof(req));
        req.user_id = ctx->user_id;
        req.train_no = ctx->config.train_no;
        strncpy(req.source, "DGR", sizeof(req.source)-1);
        strncpy(req.dest, "NDLS", sizeof(req.dest)-1);
        strncpy(req.travel_date, ctx->config.travel_date, sizeof(req.travel_date)-1);
        req.travel_class = ctx->config.travel_class;
        req.quota = QUOTA_GENERAL;
        req.passenger_count = 1;

        snprintf(req.passengers[0].name, sizeof(req.passengers[0].name),
                 "SimUser_%s_%d", ctx->counter_name, i + 1);
        req.passengers[0].age = 25 + (i % 40);
        req.passengers[0].gender = (i % 2 == 0) ? 'M' : 'F';
        req.passengers[0].preferred_berth = (BerthType)(1 + (i % 5));
        req.passengers[0].concession = CONC_NONE;
        strncpy(req.payment_mode, "SIM_COUNTER", sizeof(req.payment_mode)-1);

        Booking booking;
        int rc = booking_service_book_ticket(&req, &booking);
        if (rc == 0) {
            if (booking.overall_status == STATUS_CNF) {
                ctx->successful_cnf++;
            } else if (booking.overall_status == STATUS_RAC) {
                ctx->successful_rac++;
            } else if (booking.overall_status == STATUS_WL) {
                ctx->successful_wl++;
            }
        } else {
            ctx->rejected++;
        }
    }
    return NULL;
}

int concurrency_run_multiuser_simulation(const SimulationConfig *config,
                                          SimulationResult *result) {
    if (!config || !result) return -1;
    memset(result, 0, sizeof(SimulationResult));

    int num_threads = config->num_threads > 0 ? config->num_threads : 4;
    if (num_threads > 16) num_threads = 16;

    pthread_t threads[16];
    WorkerContext contexts[16];

    const char *counter_names[] = {
        "DGR_PRS_1", "DGR_PRS_2", "HWH_PRS_1", "HWH_PRS_2",
        "ASN_PRS_1", "DHN_PRS_1", "IRCTC_NET1", "IRCTC_NET2",
        "DGR_MOB_1", "DGR_MOB_2", "PRYJ_PRS1", "CNB_PRS_1",
        "NDLS_PRS1", "NDLS_PRS2", "RNC_PRS_1", "NJP_PRS_1"
    };

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    for (int i = 0; i < num_threads; i++) {
        contexts[i].thread_id = i;
        contexts[i].user_id = 2 + (i % 4); /* Booking clerk or passenger accounts */
        strncpy(contexts[i].counter_name, counter_names[i % 16], sizeof(contexts[i].counter_name)-1);
        contexts[i].config = *config;
        contexts[i].successful_cnf = 0;
        contexts[i].successful_rac = 0;
        contexts[i].successful_wl = 0;
        contexts[i].rejected = 0;

        pthread_create(&threads[i], NULL, simulation_worker, &contexts[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        result->successful_confirmed += contexts[i].successful_cnf;
        result->successful_rac += contexts[i].successful_rac;
        result->successful_wl += contexts[i].successful_wl;
        result->rejected_overcapacity += contexts[i].rejected;
    }

    gettimeofday(&end_time, NULL);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    if (elapsed <= 0.0001) elapsed = 0.0001;

    result->total_requests = num_threads * config->bookings_per_thread;
    result->elapsed_seconds = elapsed;
    result->transactions_per_sec = (double)result->total_requests / elapsed;

    /* Verify Zero Double Booking (Race condition verification across all assigned seats) */
    Passenger chart[MAX_CHART_ENTRIES];
    int chart_count = db_get_reservation_chart(config->train_no, config->travel_date, chart, MAX_CHART_ENTRIES);

    int seat_seen[MAX_SEATS_PER_COACH + 1];
    memset(seat_seen, 0, sizeof(seat_seen));

    for (int i = 0; i < chart_count; i++) {
        if (chart[i].status == STATUS_CNF && chart[i].seat_number > 0 && chart[i].seat_number <= MAX_SEATS_PER_COACH) {
            seat_seen[chart[i].seat_number]++;
            if (seat_seen[chart[i].seat_number] > 1) {
                result->race_conditions_detected++;
            }
        }
    }

    return 0;
}
