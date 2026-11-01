#include "models.h"
#include "auth.h"
#include "booking_service.h"
#include "train_service.h"
#include "concurrency.h"
#include "server.h"
#include "client.h"
#include "ui.h"
#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static void handle_sigint(int sig) {
    (void)sig;
    printf("\n[System] Intercepted termination signal. Flushing WAL buffers and unlinking locks...\n");
    db_flush_to_disk();
    concurrency_cleanup();
    exit(0);
}

static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Operating System-Based Train Ticket Reservation System (Eastern Railway / Durgapur Hub)\n\n");
    printf("Options:\n");
    printf("  --interactive              Launch interactive ANSI terminal menu (default)\n");
    printf("  --server [--port <port>]   Start central PRS multi-threaded TCP server daemon (default: 9090)\n");
    printf("  --client [--host <host>]   Connect as a remote booking counter terminal client\n");
    printf("  --simulate                 Run OS multi-user concurrency and race-condition stress test\n");
    printf("  --mysql                    Connect to live MySQL / MariaDB database instance\n");
    printf("  --host <hostname>          Database or server host (default: 127.0.0.1)\n");
    printf("  --port <port_num>          Port number\n");
    printf("  --user <username>          MySQL user (default: root)\n");
    printf("  --pass <password>          MySQL password\n");
    printf("  --db   <dbname>            MySQL database name (default: train_reservation_db)\n");
    printf("  --help                     Display this help manual\n");
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    DBConfig db_cfg;
    memset(&db_cfg, 0, sizeof(DBConfig));
    strncpy(db_cfg.host, "127.0.0.1", sizeof(db_cfg.host)-1);
    db_cfg.port = 3306;
    strncpy(db_cfg.user, "root", sizeof(db_cfg.user)-1);
    strncpy(db_cfg.database, "train_reservation_db", sizeof(db_cfg.database)-1);
    db_cfg.use_live_mysql = false;

    bool run_server = false;
    bool run_client = false;
    bool run_simulate = false;
    int  server_port = DEFAULT_SERVER_PORT;
    char client_host[64] = "127.0.0.1";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--server") == 0) {
            run_server = true;
        } else if (strcmp(argv[i], "--client") == 0) {
            run_client = true;
        } else if (strcmp(argv[i], "--simulate") == 0) {
            run_simulate = true;
        } else if (strcmp(argv[i], "--mysql") == 0) {
            db_cfg.use_live_mysql = true;
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            strncpy(client_host, argv[++i], sizeof(client_host)-1);
            strncpy(db_cfg.host, client_host, sizeof(db_cfg.host)-1);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            server_port = atoi(argv[++i]);
            db_cfg.port = server_port;
        } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            strncpy(db_cfg.user, argv[++i], sizeof(db_cfg.user)-1);
        } else if (strcmp(argv[i], "--pass") == 0 && i + 1 < argc) {
            strncpy(db_cfg.password, argv[++i], sizeof(db_cfg.password)-1);
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            strncpy(db_cfg.database, argv[++i], sizeof(db_cfg.database)-1);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    /* Initialize core subsystems */
    concurrency_init();
    db_init(&db_cfg);
    db_connect();
    train_service_init();
    booking_service_init();

    if (run_server) {
        server_start(server_port);
    } else if (run_client) {
        client_connect_and_run(client_host, server_port);
    } else if (run_simulate) {
        SimulationConfig cfg;
        cfg.num_threads = 8;
        cfg.bookings_per_thread = 5;
        cfg.train_no = 12301;
        strcpy(cfg.travel_date, "2026-11-25");
        cfg.travel_class = CLASS_3A;

        printf("=======================================================================\n");
        printf(" Starting Multi-User OS Concurrency Simulation (%d concurrent threads)...\n", cfg.num_threads);
        printf("=======================================================================\n");

        SimulationResult res;
        concurrency_run_multiuser_simulation(&cfg, &res);

        printf("\nSimulation Completed Successfully:\n");
        printf("  Total Requests:            %d\n", res.total_requests);
        printf("  Confirmed Seats:           %d\n", res.successful_confirmed);
        printf("  RAC Assigned:              %d\n", res.successful_rac);
        printf("  Waiting List:              %d\n", res.successful_wl);
        printf("  Overcapacity Rejected:     %d\n", res.rejected_overcapacity);
        printf("  Race Conditions Detected:  %d (Mutual Exclusion Verified)\n", res.race_conditions_detected);
        printf("  Execution Time:            %.4f seconds\n", res.elapsed_seconds);
        printf("  Throughput:                %.2f transactions/sec\n\n", res.transactions_per_sec);
    } else {
        ui_run_interactive_loop();
    }

    db_disconnect();
    concurrency_cleanup();
    return 0;
}
