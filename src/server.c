#include "server.h"
#include "booking_service.h"
#include "train_service.h"
#include "auth.h"
#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int  g_server_fd = -1;
static bool g_server_running = false;

typedef struct {
    int                client_fd;
    struct sockaddr_in client_addr;
    AuthSession        session;
} ClientThreadContext;

static void handle_client_command(ClientThreadContext *ctx, const char *cmd_line, char *response, size_t resp_max) {
    char cmd[32] = "";
    char arg1[64] = "";
    char arg2[64] = "";
    char arg3[64] = "";
    char arg4[64] = "";

    sscanf(cmd_line, "%31s %63s %63s %63s %63s", cmd, arg1, arg2, arg3, arg4);

    if (strcasecmp(cmd, "PING") == 0) {
        snprintf(response, resp_max, "PONG Indian Railways PRS Central Server Ready\n");
    } else if (strcasecmp(cmd, "LOGIN") == 0) {
        if (auth_login(&ctx->session, arg1, arg2) == 0) {
            snprintf(response, resp_max, "OK LOGIN_SUCCESS user=%s role=%s token=%s\n",
                     ctx->session.current_user.username,
                     role_to_str(ctx->session.current_user.role),
                     ctx->session.session_token);
        } else {
            snprintf(response, resp_max, "ERR INVALID_CREDENTIALS\n");
        }
    } else if (strcasecmp(cmd, "SEARCH") == 0) {
        Train trains[10];
        int count = train_service_search(arg1, arg2, trains, 10);
        int offset = snprintf(response, resp_max, "FOUND %d trains\n", count);
        for (int i = 0; i < count && offset < (int)resp_max - 128; i++) {
            offset += snprintf(response + offset, resp_max - offset,
                               "TRAIN %d | %s | %s -> %s | Dep: %s Arr: %s\n",
                               trains[i].train_no, trains[i].train_name,
                               trains[i].source, trains[i].destination,
                               trains[i].departure_time, trains[i].arrival_time);
        }
    } else if (strcasecmp(cmd, "AVAIL") == 0) {
        int train_no = atoi(arg1);
        const char *date = arg2;
        TravelClass tc = parse_class_code(arg3);
        int cnf = 0, rac = 0, wl = 0;
        double occ = 0.0;
        if (train_service_check_availability(train_no, date, tc, &cnf, &rac, &wl, &occ) == 0) {
            snprintf(response, resp_max, "AVAIL train=%d date=%s class=%s CNF=%d RAC=%d WL=%d OCC=%.1f%%\n",
                     train_no, date, class_code_to_str(tc), cnf, rac, wl, occ * 100.0);
        } else {
            snprintf(response, resp_max, "ERR TRAIN_NOT_FOUND\n");
        }
    } else if (strcasecmp(cmd, "PNR") == 0) {
        Booking b;
        if (booking_service_get_pnr_status(arg1, &b) == 0) {
            snprintf(response, resp_max,
                     "PNR_STATUS pnr=%s train=%d date=%s class=%s status=%s total_fare=%.2f pax=%d\n",
                     b.pnr, b.train_no, b.travel_date, class_code_to_str(b.travel_class),
                     status_to_str(b.overall_status), b.total_fare, b.passenger_count);
        } else {
            snprintf(response, resp_max, "ERR PNR_NOT_FOUND\n");
        }
    } else if (strcasecmp(cmd, "CANCEL") == 0) {
        if (!auth_is_authenticated(&ctx->session)) {
            snprintf(response, resp_max, "ERR AUTH_REQUIRED\n");
            return;
        }
        CancellationRecord rec;
        if (booking_service_cancel_pnr(arg1, ctx->session.current_user.user_id, 48.0, "Terminal cancellation", &rec) == 0) {
            snprintf(response, resp_max, "OK CANCELLED pnr=%s refund=%.2f penalty=%.2f\n",
                     rec.pnr, rec.refund_amount, rec.cancellation_penalty);
        } else {
            snprintf(response, resp_max, "ERR CANCELLATION_FAILED\n");
        }
    } else if (strcasecmp(cmd, "STATS") == 0) {
        DBSystemStats stats;
        db_get_system_stats(&stats);
        snprintf(response, resp_max, "STATS users=%d trains=%d bookings=%d rev=%.2f ref=%.2f\n",
                 stats.total_users, stats.total_trains, stats.total_bookings,
                 stats.total_revenue, stats.total_refunded);
    } else if (strcasecmp(cmd, "QUIT") == 0) {
        snprintf(response, resp_max, "BYE Connection closed\n");
    } else {
        snprintf(response, resp_max, "ERR UNKNOWN_COMMAND (Use: PING, LOGIN, SEARCH, AVAIL, PNR, CANCEL, STATS, QUIT)\n");
    }
}

static void* client_worker(void *arg) {
    ClientThreadContext *ctx = (ClientThreadContext*)arg;
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    auth_init_session(&ctx->session);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ctx->client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
    snprintf(ctx->session.terminal_id, sizeof(ctx->session.terminal_id), "%s:%d",
             ip_str, ntohs(ctx->client_addr.sin_port));

    const char *welcome = "220 Eastern Railway PRS Daemon (Durgapur Hub) Ready\n";
    write(ctx->client_fd, welcome, strlen(welcome));

    while (g_server_running) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(ctx->client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) break;

        /* Strip trailing newlines */
        while (bytes_read > 0 && (buffer[bytes_read - 1] == '\r' || buffer[bytes_read - 1] == '\n')) {
            buffer[--bytes_read] = '\0';
        }
        if (bytes_read == 0) continue;

        if (strcasecmp(buffer, "QUIT") == 0) {
            const char *bye = "221 Goodbye\n";
            write(ctx->client_fd, bye, strlen(bye));
            break;
        }

        memset(response, 0, sizeof(response));
        handle_client_command(ctx, buffer, response, sizeof(response));
        write(ctx->client_fd, response, strlen(response));
    }

    close(ctx->client_fd);
    free(ctx);
    return NULL;
}

int server_start(int port) {
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(g_server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(g_server_fd);
        return -1;
    }

    if (listen(g_server_fd, 10) < 0) {
        perror("listen");
        close(g_server_fd);
        return -1;
    }

    g_server_running = true;
    printf("[Server] Eastern Railway PRS Server running on port %d (PID %d)\n", port, getpid());
    printf("[Server] Multi-threaded POSIX client handler initialized. Awaiting terminal connections...\n");

    while (g_server_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(g_server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (!g_server_running) break;
            continue;
        }

        ClientThreadContext *ctx = malloc(sizeof(ClientThreadContext));
        if (!ctx) {
            close(client_fd);
            continue;
        }
        ctx->client_fd = client_fd;
        ctx->client_addr = client_addr;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_worker, ctx) != 0) {
            close(client_fd);
            free(ctx);
        } else {
            pthread_detach(tid);
        }
    }

    close(g_server_fd);
    return 0;
}

void server_stop(void) {
    g_server_running = false;
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}

bool server_is_running(void) {
    return g_server_running;
}
