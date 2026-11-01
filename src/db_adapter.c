#include "db.h"
#include "crypto_sha256.h"
#include "concurrency.h"
#include "fare_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <mysql/mysql.h>

/* Global configuration and connection state */
static DBConfig g_config;
static MYSQL   *g_mysql = NULL;
static bool     g_is_initialized = false;
static bool     g_in_transaction = false;

/* Embedded Record Store Capacities */
#define MAX_RECORDS_USERS        128
#define MAX_RECORDS_TRAINS       32
#define MAX_RECORDS_BOOKINGS     1024
#define MAX_RECORDS_PASSENGERS   4096
#define MAX_RECORDS_SEATS        4096
#define MAX_RECORDS_CANCELS      1024
#define MAX_RECORDS_TX           2048
#define MAX_RECORDS_AUDIT        4096

/* Persistent In-Memory & File Store Buffers */
static User               g_users[MAX_RECORDS_USERS];
static int                g_user_count = 0;

static Train              g_trains[MAX_RECORDS_TRAINS];
static int                g_train_count = 0;

static Booking            g_bookings[MAX_RECORDS_BOOKINGS];
static int                g_booking_count = 0;

static SeatInventory      g_seats[MAX_RECORDS_SEATS];
static int                g_seat_count = 0;

static CancellationRecord g_cancels[MAX_RECORDS_CANCELS];
static int                g_cancel_count = 0;

static TransactionLedger  g_txs[MAX_RECORDS_TX];
static int                g_tx_count = 0;

static AuditLog           g_audits[MAX_RECORDS_AUDIT];
static int                g_audit_count = 0;

/* File paths for OS persistence */
static const char *DATA_DIR          = "data";
static const char *FILE_USERS        = "data/users.dat";
static const char *FILE_TRAINS       = "data/trains.dat";
static const char *FILE_BOOKINGS     = "data/bookings.dat";
static const char *FILE_SEATS        = "data/seats.dat";
static const char *FILE_CANCELS      = "data/cancels.dat";
static const char *FILE_TX           = "data/transactions.dat";
static const char *FILE_AUDIT        = "data/audit.log";
static const char *FILE_WAL          = "data/transactions.wal";

/* Helper: Get Current ISO Timestamp String */
static void get_iso_timestamp(char *buf, size_t sz) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, sz, "%Y-%m-%d %H:%M:%S", t);
}

/* Helper: Ensure data directory exists */
static void ensure_data_directory(void) {
    struct stat st;
    if (stat(DATA_DIR, &st) == -1) {
        mkdir(DATA_DIR, 0755);
    }
}

/* ============================================================================
 * OS Low-Level File Handling & Write-Ahead Logging (WAL)
 * ============================================================================ */

int db_write_wal_entry(const char *operation, const char *payload) {
    ensure_data_directory();
    int fd = open(FILE_WAL, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return -1;

    char timestamp[32];
    get_iso_timestamp(timestamp, sizeof(timestamp));

    char line[512];
    int len = snprintf(line, sizeof(line), "[%s] OP: %s | %s\n",
                       timestamp, operation, payload ? payload : "");
    if (len > 0) {
        write(fd, line, len);
        /* fsync ensures OS commits transaction record to non-volatile storage */
        fsync(fd);
    }
    close(fd);
    return 0;
}

int db_acquire_file_lock(int fd, bool exclusive) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = exclusive ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0; /* Lock entire file */
    return fcntl(fd, F_SETLKW, &fl);
}

int db_release_file_lock(int fd) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    return fcntl(fd, F_SETLK, &fl);
}

int db_flush_to_disk(void) {
    ensure_data_directory();
    concurrency_lock_audit();

    /* Flush users */
    FILE *fp = fopen(FILE_USERS, "wb");
    if (fp) {
        fwrite(&g_user_count, sizeof(int), 1, fp);
        if (g_user_count > 0) {
            fwrite(g_users, sizeof(User), g_user_count, fp);
        }
        fclose(fp);
    }

    /* Flush trains */
    fp = fopen(FILE_TRAINS, "wb");
    if (fp) {
        fwrite(&g_train_count, sizeof(int), 1, fp);
        if (g_train_count > 0) {
            fwrite(g_trains, sizeof(Train), g_train_count, fp);
        }
        fclose(fp);
    }

    /* Flush bookings */
    fp = fopen(FILE_BOOKINGS, "wb");
    if (fp) {
        fwrite(&g_booking_count, sizeof(int), 1, fp);
        if (g_booking_count > 0) {
            fwrite(g_bookings, sizeof(Booking), g_booking_count, fp);
        }
        fclose(fp);
    }

    /* Flush seats */
    fp = fopen(FILE_SEATS, "wb");
    if (fp) {
        fwrite(&g_seat_count, sizeof(int), 1, fp);
        if (g_seat_count > 0) {
            fwrite(g_seats, sizeof(SeatInventory), g_seat_count, fp);
        }
        fclose(fp);
    }

    /* Flush cancels */
    fp = fopen(FILE_CANCELS, "wb");
    if (fp) {
        fwrite(&g_cancel_count, sizeof(int), 1, fp);
        if (g_cancel_count > 0) {
            fwrite(g_cancels, sizeof(CancellationRecord), g_cancel_count, fp);
        }
        fclose(fp);
    }

    /* Flush transactions */
    fp = fopen(FILE_TX, "wb");
    if (fp) {
        fwrite(&g_tx_count, sizeof(int), 1, fp);
        if (g_tx_count > 0) {
            fwrite(g_txs, sizeof(TransactionLedger), g_tx_count, fp);
        }
        fclose(fp);
    }

    concurrency_unlock_audit();
    return DB_SUCCESS;
}

static int load_from_disk(void) {
    ensure_data_directory();
    FILE *fp;

    /* Users */
    fp = fopen(FILE_USERS, "rb");
    if (fp) {
        if (fread(&g_user_count, sizeof(int), 1, fp) == 1 && g_user_count > 0) {
            if (g_user_count > MAX_RECORDS_USERS) g_user_count = MAX_RECORDS_USERS;
            fread(g_users, sizeof(User), g_user_count, fp);
        }
        fclose(fp);
    }

    /* Trains */
    fp = fopen(FILE_TRAINS, "rb");
    if (fp) {
        if (fread(&g_train_count, sizeof(int), 1, fp) == 1 && g_train_count > 0) {
            if (g_train_count > MAX_RECORDS_TRAINS) g_train_count = MAX_RECORDS_TRAINS;
            fread(g_trains, sizeof(Train), g_train_count, fp);
        }
        fclose(fp);
    }

    /* Bookings */
    fp = fopen(FILE_BOOKINGS, "rb");
    if (fp) {
        if (fread(&g_booking_count, sizeof(int), 1, fp) == 1 && g_booking_count > 0) {
            if (g_booking_count > MAX_RECORDS_BOOKINGS) g_booking_count = MAX_RECORDS_BOOKINGS;
            fread(g_bookings, sizeof(Booking), g_booking_count, fp);
        }
        fclose(fp);
    }

    /* Seats */
    fp = fopen(FILE_SEATS, "rb");
    if (fp) {
        if (fread(&g_seat_count, sizeof(int), 1, fp) == 1 && g_seat_count > 0) {
            if (g_seat_count > MAX_RECORDS_SEATS) g_seat_count = MAX_RECORDS_SEATS;
            fread(g_seats, sizeof(SeatInventory), g_seat_count, fp);
        }
        fclose(fp);
    }

    /* Cancels */
    fp = fopen(FILE_CANCELS, "rb");
    if (fp) {
        if (fread(&g_cancel_count, sizeof(int), 1, fp) == 1 && g_cancel_count > 0) {
            if (g_cancel_count > MAX_RECORDS_CANCELS) g_cancel_count = MAX_RECORDS_CANCELS;
            fread(g_cancels, sizeof(CancellationRecord), g_cancel_count, fp);
        }
        fclose(fp);
    }

    /* Transactions */
    fp = fopen(FILE_TX, "rb");
    if (fp) {
        if (fread(&g_tx_count, sizeof(int), 1, fp) == 1 && g_tx_count > 0) {
            if (g_tx_count > MAX_RECORDS_TX) g_tx_count = MAX_RECORDS_TX;
            fread(g_txs, sizeof(TransactionLedger), g_tx_count, fp);
        }
        fclose(fp);
    }

    return DB_SUCCESS;
}

/* ============================================================================
 * Initial Seed Population (Eastern Railway Durgapur Corridor)
 * ============================================================================ */

int db_seed_initial_data(void) {
    if (g_user_count > 0 && g_train_count > 0) {
        return DB_SUCCESS; /* Already seeded */
    }

    /* 1. Seed Users */
    User seed_users[] = {
        {1, "admin", "", "", "Chief Commercial Manager", ROLE_ADMIN, "+91-343-255011", "ccm@er.railnet.gov.in", "DGR", time(NULL), true},
        {2, "dgr_counter1", "", "", "Durgapur PRS Counter Clerk 1", ROLE_AGENT, "+91-9832100111", "dgr_prs1@er.railnet.gov.in", "DGR", time(NULL), true},
        {3, "hwh_counter1", "", "", "Howrah PRS Counter Clerk 1", ROLE_AGENT, "+91-9832200222", "hwh_prs1@er.railnet.gov.in", "HWH", time(NULL), true},
        {4, "passenger1", "", "", "Amitava Mukherjee", ROLE_PASSENGER, "+91-9876543210", "amitava.m@gmail.com", "DGR", time(NULL), true},
        {5, "sourav", "", "", "Sourav Ganguly", ROLE_PASSENGER, "+91-9830012345", "sourav.g@outlook.com", "HWH", time(NULL), true},
        {6, "tte_dgr", "", "", "Rajesh Sharma (TTE)", ROLE_TTE, "+91-9434056789", "tte.dgr@er.railnet.gov.in", "DGR", time(NULL), true}
    };
    const char *passwords[] = {"Admin@123", "Counter@123", "Counter@123", "Pass@123", "Sourav@123", "Tte@123"};

    for (size_t i = 0; i < sizeof(seed_users)/sizeof(seed_users[0]); i++) {
        generate_random_salt(seed_users[i].salt);
        hash_password_with_salt(passwords[i], seed_users[i].salt, seed_users[i].password_hash);
        g_users[g_user_count++] = seed_users[i];
    }

    /* 2. Seed Trains */
    Train rajdhani;
    memset(&rajdhani, 0, sizeof(Train));
    rajdhani.train_no = 12301;
    strncpy(rajdhani.train_name, "Howrah - New Delhi Rajdhani Express", sizeof(rajdhani.train_name)-1);
    strncpy(rajdhani.source, "HWH", sizeof(rajdhani.source)-1);
    strncpy(rajdhani.destination, "NDLS", sizeof(rajdhani.destination)-1);
    rajdhani.total_distance_km = 1447;
    strncpy(rajdhani.departure_time, "16:50", sizeof(rajdhani.departure_time)-1);
    strncpy(rajdhani.arrival_time, "10:05", sizeof(rajdhani.arrival_time)-1);
    rajdhani.is_superfast = true;
    rajdhani.base_rate_per_km = 2.80;
    rajdhani.capacity_1a = 18;
    rajdhani.capacity_2a = 36;
    rajdhani.capacity_3a = 64;
    rajdhani.capacity_cc = 0;
    rajdhani.capacity_sl = 0;
    rajdhani.capacity_2s = 0;
    rajdhani.rac_limit = 10;
    rajdhani.wl_limit = 20;
    rajdhani.active = true;

    /* Stops for Rajdhani */
    rajdhani.stop_count = 5;
    strncpy(rajdhani.stops[0].station_code, "HWH", MAX_STR_XS-1); rajdhani.stops[0].distance_from_source_km = 0;
    strncpy(rajdhani.stops[1].station_code, "BWN", MAX_STR_XS-1); rajdhani.stops[1].distance_from_source_km = 107;
    strncpy(rajdhani.stops[2].station_code, "DGR", MAX_STR_XS-1); rajdhani.stops[2].distance_from_source_km = 171;
    strncpy(rajdhani.stops[3].station_code, "ASN", MAX_STR_XS-1); rajdhani.stops[3].distance_from_source_km = 213;
    strncpy(rajdhani.stops[4].station_code, "NDLS", MAX_STR_XS-1); rajdhani.stops[4].distance_from_source_km = 1447;
    for (int s = 0; s < rajdhani.stop_count; s++) rajdhani.stops[s].stop_sequence = s + 1;
    g_trains[g_train_count++] = rajdhani;

    Train poorva;
    memset(&poorva, 0, sizeof(Train));
    poorva.train_no = 12303;
    strncpy(poorva.train_name, "Poorva Express (via Durgapur)", sizeof(poorva.train_name)-1);
    strncpy(poorva.source, "HWH", sizeof(poorva.source)-1);
    strncpy(poorva.destination, "NDLS", sizeof(poorva.destination)-1);
    poorva.total_distance_km = 1530;
    strncpy(poorva.departure_time, "08:00", sizeof(poorva.departure_time)-1);
    strncpy(poorva.arrival_time, "06:05", sizeof(poorva.arrival_time)-1);
    poorva.is_superfast = true;
    poorva.base_rate_per_km = 0.75;
    poorva.capacity_1a = 12;
    poorva.capacity_2a = 24;
    poorva.capacity_3a = 48;
    poorva.capacity_cc = 0;
    poorva.capacity_sl = 72;
    poorva.capacity_2s = 72;
    poorva.rac_limit = 20;
    poorva.wl_limit = 40;
    poorva.active = true;
    poorva.stop_count = 5;
    strncpy(poorva.stops[0].station_code, "HWH", MAX_STR_XS-1); poorva.stops[0].distance_from_source_km = 0;
    strncpy(poorva.stops[1].station_code, "BWN", MAX_STR_XS-1); poorva.stops[1].distance_from_source_km = 107;
    strncpy(poorva.stops[2].station_code, "DGR", MAX_STR_XS-1); poorva.stops[2].distance_from_source_km = 171;
    strncpy(poorva.stops[3].station_code, "ASN", MAX_STR_XS-1); poorva.stops[3].distance_from_source_km = 213;
    strncpy(poorva.stops[4].station_code, "NDLS", MAX_STR_XS-1); poorva.stops[4].distance_from_source_km = 1530;
    for (int s = 0; s < poorva.stop_count; s++) poorva.stops[s].stop_sequence = s + 1;
    g_trains[g_train_count++] = poorva;

    Train shatabdi;
    memset(&shatabdi, 0, sizeof(Train));
    shatabdi.train_no = 12019;
    strncpy(shatabdi.train_name, "Howrah - Ranchi Shatabdi Express", sizeof(shatabdi.train_name)-1);
    strncpy(shatabdi.source, "HWH", sizeof(shatabdi.source)-1);
    strncpy(shatabdi.destination, "RNC", sizeof(shatabdi.destination)-1);
    shatabdi.total_distance_km = 412;
    strncpy(shatabdi.departure_time, "06:05", sizeof(shatabdi.departure_time)-1);
    strncpy(shatabdi.arrival_time, "13:15", sizeof(shatabdi.arrival_time)-1);
    shatabdi.is_superfast = true;
    shatabdi.base_rate_per_km = 1.60;
    shatabdi.capacity_1a = 0;
    shatabdi.capacity_2a = 0;
    shatabdi.capacity_3a = 0;
    shatabdi.capacity_cc = 72;
    shatabdi.capacity_sl = 0;
    shatabdi.capacity_2s = 72;
    shatabdi.rac_limit = 10;
    shatabdi.wl_limit = 20;
    shatabdi.active = true;
    shatabdi.stop_count = 4;
    strncpy(shatabdi.stops[0].station_code, "HWH", MAX_STR_XS-1); shatabdi.stops[0].distance_from_source_km = 0;
    strncpy(shatabdi.stops[1].station_code, "DGR", MAX_STR_XS-1); shatabdi.stops[1].distance_from_source_km = 171;
    strncpy(shatabdi.stops[2].station_code, "ASN", MAX_STR_XS-1); shatabdi.stops[2].distance_from_source_km = 213;
    strncpy(shatabdi.stops[3].station_code, "RNC", MAX_STR_XS-1); shatabdi.stops[3].distance_from_source_km = 412;
    for (int s = 0; s < shatabdi.stop_count; s++) shatabdi.stops[s].stop_sequence = s + 1;
    g_trains[g_train_count++] = shatabdi;

    Train vande_bharat;
    memset(&vande_bharat, 0, sizeof(Train));
    vande_bharat.train_no = 22301;
    strncpy(vande_bharat.train_name, "Howrah - NJP Vande Bharat Express", sizeof(vande_bharat.train_name)-1);
    strncpy(vande_bharat.source, "HWH", sizeof(vande_bharat.source)-1);
    strncpy(vande_bharat.destination, "NJP", sizeof(vande_bharat.destination)-1);
    vande_bharat.total_distance_km = 560;
    strncpy(vande_bharat.departure_time, "05:55", sizeof(vande_bharat.departure_time)-1);
    strncpy(vande_bharat.arrival_time, "13:25", sizeof(vande_bharat.arrival_time)-1);
    vande_bharat.is_superfast = true;
    vande_bharat.base_rate_per_km = 2.20;
    vande_bharat.capacity_1a = 0;
    vande_bharat.capacity_2a = 0;
    vande_bharat.capacity_3a = 0;
    vande_bharat.capacity_cc = 72;
    vande_bharat.capacity_sl = 0;
    vande_bharat.capacity_2s = 0;
    vande_bharat.rac_limit = 8;
    vande_bharat.wl_limit = 16;
    vande_bharat.active = true;
    g_trains[g_train_count++] = vande_bharat;

    /* Write initial audit log */
    db_write_audit_log(1, "SYSTEM_INIT", NULL, "SYSTEM_BOOT",
                       "Initialized Railway System Database with Durgapur Corridor seed records.");
    db_write_wal_entry("BOOTSTRAP", "System seed initialized successfully");

    db_flush_to_disk();
    return DB_SUCCESS;
}

/* ============================================================================
 * DB Lifecycle
 * ============================================================================ */

int db_init(DBConfig *config) {
    if (config) {
        g_config = *config;
    } else {
        memset(&g_config, 0, sizeof(DBConfig));
        strncpy(g_config.host, "127.0.0.1", sizeof(g_config.host)-1);
        g_config.port = 3306;
        strncpy(g_config.user, "root", sizeof(g_config.user)-1);
        strncpy(g_config.database, "train_reservation_db", sizeof(g_config.database)-1);
        g_config.use_live_mysql = false;
    }

    load_from_disk();
    db_seed_initial_data();
    g_is_initialized = true;
    return DB_SUCCESS;
}

int db_connect(void) {
    if (g_config.use_live_mysql) {
        g_mysql = mysql_init(NULL);
        if (!g_mysql) {
            fprintf(stderr, "[DB Adapter] mysql_init failed. Falling back to persistent binary store.\n");
            g_config.use_live_mysql = false;
            return DB_SUCCESS;
        }

        unsigned int timeout = 3;
        mysql_options(g_mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

        if (!mysql_real_connect(g_mysql, g_config.host, g_config.user,
                                g_config.password, g_config.database,
                                g_config.port, NULL, 0)) {
            fprintf(stderr, "[DB Adapter] Live MySQL connection failed (%s). Continuing with file/relational store.\n",
                    mysql_error(g_mysql));
            mysql_close(g_mysql);
            g_mysql = NULL;
            g_config.use_live_mysql = false;
        } else {
            printf("[DB Adapter] Connected to live MySQL/MariaDB database '%s' at %s:%d\n",
                   g_config.database, g_config.host, g_config.port);
        }
    }
    return DB_SUCCESS;
}

void db_disconnect(void) {
    db_flush_to_disk();
    if (g_mysql) {
        mysql_close(g_mysql);
        g_mysql = NULL;
    }
}

bool db_is_connected(void) {
    return g_is_initialized;
}

int db_begin_transaction(void) {
    g_in_transaction = true;
    db_write_wal_entry("BEGIN_TX", NULL);
    if (g_mysql) {
        mysql_query(g_mysql, "START TRANSACTION");
    }
    return DB_SUCCESS;
}

int db_commit(void) {
    g_in_transaction = false;
    db_write_wal_entry("COMMIT_TX", NULL);
    if (g_mysql) {
        mysql_query(g_mysql, "COMMIT");
    }
    db_flush_to_disk();
    return DB_SUCCESS;
}

int db_rollback(void) {
    g_in_transaction = false;
    db_write_wal_entry("ROLLBACK_TX", NULL);
    if (g_mysql) {
        mysql_query(g_mysql, "ROLLBACK");
    }
    load_from_disk(); /* Revert to persistent snapshot */
    return DB_SUCCESS;
}

/* ============================================================================
 * User Queries
 * ============================================================================ */

int db_find_user_by_username(const char *username, User *out_user) {
    if (!username || !out_user) return DB_ERROR;
    for (int i = 0; i < g_user_count; i++) {
        if (strcasecmp(g_users[i].username, username) == 0 && g_users[i].is_active) {
            *out_user = g_users[i];
            return DB_SUCCESS;
        }
    }
    return DB_NOTFOUND;
}

int db_find_user_by_id(int user_id, User *out_user) {
    if (!out_user) return DB_ERROR;
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i].user_id == user_id && g_users[i].is_active) {
            *out_user = g_users[i];
            return DB_SUCCESS;
        }
    }
    return DB_NOTFOUND;
}

int db_create_user(const User *user) {
    if (!user) return DB_ERROR;
    if (g_user_count >= MAX_RECORDS_USERS) return DB_ERROR;

    /* Check duplicate username */
    for (int i = 0; i < g_user_count; i++) {
        if (strcasecmp(g_users[i].username, user->username) == 0) {
            return DB_ERROR; /* Duplicate */
        }
    }

    User new_user = *user;
    new_user.user_id = g_user_count + 1;
    new_user.created_at = time(NULL);
    new_user.is_active = true;

    g_users[g_user_count++] = new_user;

    char wal_buf[128];
    snprintf(wal_buf, sizeof(wal_buf), "user_id=%d, username=%s, role=%d",
             new_user.user_id, new_user.username, new_user.role);
    db_write_wal_entry("CREATE_USER", wal_buf);
    db_flush_to_disk();
    return DB_SUCCESS;
}

int db_update_user_password(int user_id, const char *new_hash, const char *new_salt) {
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i].user_id == user_id) {
            strncpy(g_users[i].password_hash, new_hash, HASH_HEX_LEN-1);
            strncpy(g_users[i].salt, new_salt, SALT_HEX_LEN-1);
            db_write_wal_entry("UPDATE_PASSWORD", g_users[i].username);
            db_flush_to_disk();
            return DB_SUCCESS;
        }
    }
    return DB_NOTFOUND;
}

int db_get_all_users(User out_users[], int max_users) {
    int count = (g_user_count < max_users) ? g_user_count : max_users;
    for (int i = 0; i < count; i++) {
        out_users[i] = g_users[i];
    }
    return count;
}

/* ============================================================================
 * Train and Schedule Queries
 * ============================================================================ */

int db_get_train(int train_no, Train *out_train) {
    if (!out_train) return DB_ERROR;
    for (int i = 0; i < g_train_count; i++) {
        if (g_trains[i].train_no == train_no && g_trains[i].active) {
            *out_train = g_trains[i];
            return DB_SUCCESS;
        }
    }
    return DB_NOTFOUND;
}

int db_get_all_trains(Train out_trains[], int max_trains) {
    int count = (g_train_count < max_trains) ? g_train_count : max_trains;
    for (int i = 0; i < count; i++) {
        out_trains[i] = g_trains[i];
    }
    return count;
}

int db_search_trains(const char *source, const char *dest, Train out_trains[], int max_trains) {
    int matched = 0;
    for (int i = 0; i < g_train_count && matched < max_trains; i++) {
        if (!g_trains[i].active) continue;

        /* Check direct match */
        if (strcasecmp(g_trains[i].source, source) == 0 &&
            strcasecmp(g_trains[i].destination, dest) == 0) {
            out_trains[matched++] = g_trains[i];
            continue;
        }

        /* Check intermediate stops */
        int src_seq = -1, dst_seq = -1;
        if (strcasecmp(g_trains[i].source, source) == 0) src_seq = 0;
        if (strcasecmp(g_trains[i].destination, dest) == 0) dst_seq = 999;

        for (int s = 0; s < g_trains[i].stop_count; s++) {
            int seq = g_trains[i].stops[s].stop_sequence > 0 ? g_trains[i].stops[s].stop_sequence : (s + 1);
            if (strcasecmp(g_trains[i].stops[s].station_code, source) == 0) {
                src_seq = seq;
            }
            if (strcasecmp(g_trains[i].stops[s].station_code, dest) == 0) {
                dst_seq = seq;
            }
        }

        if (src_seq >= 0 && dst_seq >= 0 && src_seq < dst_seq) {
            out_trains[matched++] = g_trains[i];
        }
    }
    return matched;
}

int db_create_train(const Train *train) {
    if (!train || g_train_count >= MAX_RECORDS_TRAINS) return DB_ERROR;
    g_trains[g_train_count++] = *train;
    db_flush_to_disk();
    return DB_SUCCESS;
}

/* ============================================================================
 * Inventory, Seat Allocation & Concurrency Control
 * ============================================================================ */

int db_get_class_capacity(int train_no, TravelClass travel_class) {
    Train train;
    if (db_get_train(train_no, &train) != DB_SUCCESS) return 0;
    switch (travel_class) {
        case CLASS_1A: return train.capacity_1a;
        case CLASS_2A: return train.capacity_2a;
        case CLASS_3A: return train.capacity_3a;
        case CLASS_CC: return train.capacity_cc;
        case CLASS_SL: return train.capacity_sl;
        case CLASS_2S: return train.capacity_2s;
        default:       return 0;
    }
}

int db_get_inventory_status(int train_no, const char *travel_date, TravelClass travel_class,
                             int *out_confirmed_left, int *out_rac_left, int *out_wl_count) {
    Train train;
    if (db_get_train(train_no, &train) != DB_SUCCESS) return DB_ERROR;

    int total_cnf = db_get_class_capacity(train_no, travel_class);
    int max_rac = train.rac_limit;
    int max_wl = train.wl_limit;

    /* Count active confirmed, RAC, and WL passengers for this train/date/class */
    int booked_cnf = 0;
    int booked_rac = 0;
    int booked_wl = 0;

    for (int i = 0; i < g_booking_count; i++) {
        if (g_bookings[i].train_no == train_no &&
            strcmp(g_bookings[i].travel_date, travel_date) == 0 &&
            g_bookings[i].travel_class == travel_class &&
            !g_bookings[i].is_cancelled) {

            for (int p = 0; p < g_bookings[i].passenger_count; p++) {
                Passenger *pass = &g_bookings[i].passengers[p];
                if (pass->status == STATUS_CNF) booked_cnf++;
                else if (pass->status == STATUS_RAC) booked_rac++;
                else if (pass->status == STATUS_WL) booked_wl++;
            }
        }
    }

    int cnf_left = total_cnf - booked_cnf;
    if (cnf_left < 0) cnf_left = 0;

    int rac_left = max_rac - booked_rac;
    if (rac_left < 0) rac_left = 0;

    if (out_confirmed_left) *out_confirmed_left = cnf_left;
    if (out_rac_left) *out_rac_left = rac_left;
    if (out_wl_count) *out_wl_count = booked_wl;

    return DB_SUCCESS;
}

/* Helper to map seat number to berth type in Indian Railway coaches */
static BerthType calculate_berth_type(TravelClass travel_class, int seat_num) {
    if (travel_class == CLASS_CC || travel_class == CLASS_2S) {
        int rem = seat_num % 5;
        if (rem == 1 || rem == 0) return BERTH_WINDOW;
        return BERTH_AISLE;
    }
    if (travel_class == CLASS_1A) {
        return BERTH_CABIN;
    }
    if (travel_class == CLASS_2A) {
        int mod = seat_num % 6;
        if (mod == 1 || mod == 2) return BERTH_LOWER;
        if (mod == 3 || mod == 4) return BERTH_UPPER;
        if (mod == 5) return BERTH_SIDE_LOWER;
        return BERTH_SIDE_UPPER;
    }
    /* Sleeper / 3A standard 8-berth bay */
    int mod = seat_num % 8;
    if (mod == 1 || mod == 4) return BERTH_LOWER;
    if (mod == 2 || mod == 5) return BERTH_MIDDLE;
    if (mod == 3 || mod == 6) return BERTH_UPPER;
    if (mod == 7) return BERTH_SIDE_LOWER;
    return BERTH_SIDE_UPPER;
}

int db_allocate_seat(int train_no, const char *travel_date, TravelClass travel_class,
                      BerthType preferred_berth, char out_coach[MAX_STR_XS],
                      int *out_seat_no, BerthType *out_allocated_berth) {
    concurrency_lock_seats();

    int total_capacity = db_get_class_capacity(train_no, travel_class);
    if (total_capacity <= 0) {
        concurrency_unlock_seats();
        return DB_ERROR;
    }

    /* Prefix coach code based on class */
    char coach_prefix[4] = "S";
    if (travel_class == CLASS_1A) strcpy(coach_prefix, "H");
    else if (travel_class == CLASS_2A) strcpy(coach_prefix, "A");
    else if (travel_class == CLASS_3A) strcpy(coach_prefix, "B");
    else if (travel_class == CLASS_CC) strcpy(coach_prefix, "C");
    else if (travel_class == CLASS_2S) strcpy(coach_prefix, "D");

    /* Track occupied seat numbers */
    bool occupied[MAX_SEATS_PER_COACH + 1];
    memset(occupied, 0, sizeof(occupied));

    for (int i = 0; i < g_booking_count; i++) {
        if (g_bookings[i].train_no == train_no &&
            strcmp(g_bookings[i].travel_date, travel_date) == 0 &&
            g_bookings[i].travel_class == travel_class &&
            !g_bookings[i].is_cancelled) {

            for (int p = 0; p < g_bookings[i].passenger_count; p++) {
                if (g_bookings[i].passengers[p].status == STATUS_CNF) {
                    int s = g_bookings[i].passengers[p].seat_number;
                    if (s > 0 && s <= MAX_SEATS_PER_COACH) {
                        occupied[s] = true;
                    }
                }
            }
        }
    }

    /* Attempt 1: Match preferred berth */
    int assigned_seat = 0;
    BerthType assigned_berth = BERTH_LOWER;

    for (int s = 1; s <= total_capacity && s <= MAX_SEATS_PER_COACH; s++) {
        if (!occupied[s]) {
            BerthType bt = calculate_berth_type(travel_class, s);
            if (bt == preferred_berth) {
                assigned_seat = s;
                assigned_berth = bt;
                break;
            }
        }
    }

    /* Attempt 2: If preferred unavailable, pick first available */
    if (assigned_seat == 0) {
        for (int s = 1; s <= total_capacity && s <= MAX_SEATS_PER_COACH; s++) {
            if (!occupied[s]) {
                assigned_seat = s;
                assigned_berth = calculate_berth_type(travel_class, s);
                break;
            }
        }
    }

    if (assigned_seat == 0) {
        concurrency_unlock_seats();
        return DB_NOTFOUND; /* No CNF seats left in this class */
    }

    snprintf(out_coach, MAX_STR_XS, "%s1", coach_prefix);
    *out_seat_no = assigned_seat;
    *out_allocated_berth = assigned_berth;

    concurrency_unlock_seats();
    return DB_SUCCESS;
}

int db_release_seat(int train_no, const char *travel_date, const char *coach, int seat_no) {
    (void)train_no;
    (void)travel_date;
    (void)coach;
    (void)seat_no;
    /* Releasing seat is handled via booking cancellation / promotion */
    return DB_SUCCESS;
}

/* ============================================================================
 * Booking Operations & Queue Promotion State Machine
 * ============================================================================ */

int db_create_booking(Booking *booking) {
    if (!booking) return DB_ERROR;
    if (g_booking_count >= MAX_RECORDS_BOOKINGS) return DB_ERROR;

    booking->booking_id = g_booking_count + 1;
    booking->booking_timestamp = time(NULL);
    booking->is_cancelled = false;

    for (int i = 0; i < booking->passenger_count; i++) {
        booking->passengers[i].passenger_id = i + 1;
        booking->passengers[i].booking_id = booking->booking_id;
        strncpy(booking->passengers[i].pnr, booking->pnr, PNR_LENGTH-1);
    }

    g_bookings[g_booking_count++] = *booking;

    char wal_buf[256];
    snprintf(wal_buf, sizeof(wal_buf), "PNR=%s, train=%d, date=%s, fare=%.2f, status=%d",
             booking->pnr, booking->train_no, booking->travel_date,
             booking->total_fare, booking->overall_status);
    db_write_wal_entry("CREATE_BOOKING", wal_buf);

    db_flush_to_disk();
    return DB_SUCCESS;
}

int db_get_booking_by_pnr(const char *pnr, Booking *out_booking) {
    if (!pnr || !out_booking) return DB_ERROR;
    for (int i = 0; i < g_booking_count; i++) {
        if (strcasecmp(g_bookings[i].pnr, pnr) == 0) {
            *out_booking = g_bookings[i];
            return DB_SUCCESS;
        }
    }
    return DB_NOTFOUND;
}

int db_get_bookings_by_user(int user_id, Booking out_bookings[], int max_bookings) {
    int matched = 0;
    for (int i = 0; i < g_booking_count && matched < max_bookings; i++) {
        if (g_bookings[i].user_id == user_id) {
            out_bookings[matched++] = g_bookings[i];
        }
    }
    return matched;
}

int db_get_all_bookings(Booking out_bookings[], int max_bookings) {
    int count = (g_booking_count < max_bookings) ? g_booking_count : max_bookings;
    for (int i = 0; i < count; i++) {
        out_bookings[i] = g_bookings[i];
    }
    return count;
}

/* ============================================================================
 * Cascade Promotion Engine (RAC -> CNF, WL -> RAC)
 * Operating System Queue & Mutual Exclusion Logic
 * ============================================================================ */

int db_promote_rac_and_wl(int train_no, const char *travel_date, TravelClass travel_class,
                           const char *freed_coach, int freed_seat_no, BerthType freed_berth) {
    concurrency_lock_seats();

    /* Step 1: Find the earliest RAC passenger for this train/date/class */
    Passenger *rac_candidate = NULL;
    Booking   *rac_booking = NULL;

    for (int i = 0; i < g_booking_count; i++) {
        Booking *b = &g_bookings[i];
        if (b->train_no == train_no &&
            strcmp(b->travel_date, travel_date) == 0 &&
            b->travel_class == travel_class &&
            !b->is_cancelled) {

            for (int p = 0; p < b->passenger_count; p++) {
                if (b->passengers[p].status == STATUS_RAC) {
                    rac_candidate = &b->passengers[p];
                    rac_booking = b;
                    break;
                }
            }
            if (rac_candidate) break;
        }
    }

    /* If RAC candidate found, promote to CNF with the freed seat */
    if (rac_candidate && rac_booking) {
        rac_candidate->status = STATUS_CNF;
        strncpy(rac_candidate->coach_code, freed_coach, MAX_STR_XS-1);
        rac_candidate->seat_number = freed_seat_no;
        rac_candidate->allocated_berth = freed_berth;
        rac_candidate->waitlist_number = 0;

        char audit_msg[256];
        snprintf(audit_msg, sizeof(audit_msg),
                 "PROMOTED RAC to CNF: PNR %s, Passenger %s -> Coach %s, Seat %d (%s)",
                 rac_candidate->pnr, rac_candidate->name,
                 rac_candidate->coach_code, rac_candidate->seat_number,
                 berth_to_str(freed_berth));
        db_write_audit_log(rac_booking->user_id, "RAC_PROMOTION",
                           rac_candidate->pnr, "SYSTEM_QUEUE", audit_msg);

        /* Step 2: Now that an RAC slot is vacated, find the earliest WL passenger to promote to RAC */
        Passenger *wl_candidate = NULL;
        Booking   *wl_booking = NULL;

        for (int i = 0; i < g_booking_count; i++) {
            Booking *b = &g_bookings[i];
            if (b->train_no == train_no &&
                strcmp(b->travel_date, travel_date) == 0 &&
                b->travel_class == travel_class &&
                !b->is_cancelled) {

                for (int p = 0; p < b->passenger_count; p++) {
                    if (b->passengers[p].status == STATUS_WL) {
                        wl_candidate = &b->passengers[p];
                        wl_booking = b;
                        break;
                    }
                }
                if (wl_candidate) break;
            }
        }

        if (wl_candidate && wl_booking) {
            wl_candidate->status = STATUS_RAC;
            wl_candidate->waitlist_number = 1; /* First RAC */
            snprintf(audit_msg, sizeof(audit_msg),
                     "PROMOTED WL to RAC: PNR %s, Passenger %s",
                     wl_candidate->pnr, wl_candidate->name);
            db_write_audit_log(wl_booking->user_id, "WL_PROMOTION",
                               wl_candidate->pnr, "SYSTEM_QUEUE", audit_msg);
        }
    }

    concurrency_unlock_seats();
    return DB_SUCCESS;
}

int db_cancel_booking(const char *pnr, int user_id, const char *reason,
                       CancellationRecord *out_record) {
    if (!pnr) return DB_ERROR;

    for (int i = 0; i < g_booking_count; i++) {
        Booking *b = &g_bookings[i];
        if (strcasecmp(b->pnr, pnr) == 0 && !b->is_cancelled) {
            b->is_cancelled = true;
            b->cancelled_timestamp = time(NULL);
            b->overall_status = STATUS_CANCELLED;

            /* Compute Dynamic Refund */
            RefundBreakdown refund;
            fare_compute_cancellation_refund(b->travel_class, STATUS_CNF, b->total_fare, 50.0, &refund);

            if (out_record) {
                out_record->cancel_id = g_cancel_count + 1;
                strncpy(out_record->pnr, b->pnr, PNR_LENGTH-1);
                out_record->passenger_id = 0; /* All passengers */
                out_record->user_id = user_id;
                out_record->cancelled_at = time(NULL);
                out_record->original_fare = b->total_fare;
                out_record->cancellation_penalty = refund.cancellation_charge;
                out_record->refund_amount = refund.refund_amount;
                strncpy(out_record->reason, reason ? reason : "Full ticket cancellation", MAX_STR_MD-1);
                if (g_cancel_count < MAX_RECORDS_CANCELS) {
                    g_cancels[g_cancel_count++] = *out_record;
                }
            }

            /* Record financial refund transaction */
            TransactionLedger tx;
            tx.tx_id = g_tx_count + 1;
            strncpy(tx.pnr, b->pnr, PNR_LENGTH-1);
            tx.user_id = user_id;
            tx.tx_type = TX_REFUND;
            tx.amount = refund.refund_amount;
            tx.timestamp = time(NULL);
            strncpy(tx.payment_mode, "ORIGINAL_SOURCE", sizeof(tx.payment_mode)-1);
            strncpy(tx.remarks, "Booking cancellation refund", sizeof(tx.remarks)-1);
            db_record_transaction(&tx);

            /* Promote queue for each released CNF berth */
            for (int p = 0; p < b->passenger_count; p++) {
                if (b->passengers[p].status == STATUS_CNF && b->passengers[p].seat_number > 0) {
                    char freed_coach[MAX_STR_XS];
                    strncpy(freed_coach, b->passengers[p].coach_code, sizeof(freed_coach)-1);
                    int freed_seat = b->passengers[p].seat_number;
                    BerthType freed_berth = b->passengers[p].allocated_berth;

                    b->passengers[p].status = STATUS_CANCELLED;
                    db_promote_rac_and_wl(b->train_no, b->travel_date, b->travel_class,
                                          freed_coach, freed_seat, freed_berth);
                } else {
                    b->passengers[p].status = STATUS_CANCELLED;
                }
            }

            char wal_buf[128];
            snprintf(wal_buf, sizeof(wal_buf), "PNR=%s cancelled, refund=%.2f", b->pnr, refund.refund_amount);
            db_write_wal_entry("CANCEL_PNR", wal_buf);
            db_flush_to_disk();
            return DB_SUCCESS;
        }
    }
    return DB_NOTFOUND;
}

int db_cancel_single_passenger(const char *pnr, int passenger_id, int user_id,
                                const char *reason, CancellationRecord *out_record) {
    if (!pnr) return DB_ERROR;

    for (int i = 0; i < g_booking_count; i++) {
        Booking *b = &g_bookings[i];
        if (strcasecmp(b->pnr, pnr) == 0 && !b->is_cancelled) {
            for (int p = 0; p < b->passenger_count; p++) {
                Passenger *pass = &b->passengers[p];
                if (pass->passenger_id == passenger_id && pass->status != STATUS_CANCELLED) {
                    BookingStatus old_status = pass->status;
                    char freed_coach[MAX_STR_XS];
                    strncpy(freed_coach, pass->coach_code, sizeof(freed_coach)-1);
                    int freed_seat = pass->seat_number;
                    BerthType freed_berth = pass->allocated_berth;

                    pass->status = STATUS_CANCELLED;

                    RefundBreakdown refund;
                    fare_compute_cancellation_refund(b->travel_class, old_status, pass->individual_fare, 50.0, &refund);

                    if (out_record) {
                        out_record->cancel_id = g_cancel_count + 1;
                        strncpy(out_record->pnr, b->pnr, PNR_LENGTH-1);
                        out_record->passenger_id = passenger_id;
                        out_record->user_id = user_id;
                        out_record->cancelled_at = time(NULL);
                        out_record->original_fare = pass->individual_fare;
                        out_record->cancellation_penalty = refund.cancellation_charge;
                        out_record->refund_amount = refund.refund_amount;
                        strncpy(out_record->reason, reason ? reason : "Passenger cancellation", MAX_STR_MD-1);
                        if (g_cancel_count < MAX_RECORDS_CANCELS) {
                            g_cancels[g_cancel_count++] = *out_record;
                        }
                    }

                    if (old_status == STATUS_CNF && freed_seat > 0) {
                        db_promote_rac_and_wl(b->train_no, b->travel_date, b->travel_class,
                                              freed_coach, freed_seat, freed_berth);
                    }

                    db_flush_to_disk();
                    return DB_SUCCESS;
                }
            }
        }
    }
    return DB_NOTFOUND;
}

/* ============================================================================
 * Financial Transactions and Auditing
 * ============================================================================ */

int db_record_transaction(const TransactionLedger *tx) {
    if (!tx || g_tx_count >= MAX_RECORDS_TX) return DB_ERROR;
    g_txs[g_tx_count++] = *tx;
    db_flush_to_disk();
    return DB_SUCCESS;
}

int db_get_transactions_by_pnr(const char *pnr, TransactionLedger out_tx[], int max_tx) {
    int matched = 0;
    for (int i = 0; i < g_tx_count && matched < max_tx; i++) {
        if (strcasecmp(g_txs[i].pnr, pnr) == 0) {
            out_tx[matched++] = g_txs[i];
        }
    }
    return matched;
}

int db_write_audit_log(int user_id, const char *action, const char *pnr,
                        const char *terminal, const char *details) {
    concurrency_lock_audit();

    if (g_audit_count < MAX_RECORDS_AUDIT) {
        AuditLog *log = &g_audits[g_audit_count++];
        log->log_id = g_audit_count;
        log->timestamp = time(NULL);
        log->user_id = user_id;
        strncpy(log->action, action ? action : "ACTION", MAX_STR_SM-1);
        strncpy(log->pnr, pnr ? pnr : "", PNR_LENGTH-1);
        strncpy(log->ip_or_terminal, terminal ? terminal : "TERMINAL", MAX_STR_SM-1);
        strncpy(log->details, details ? details : "", MAX_STR_XL-1);
    }

    /* Write to persistent audit log file */
    FILE *fp = fopen(FILE_AUDIT, "a");
    if (fp) {
        char ts[32];
        get_iso_timestamp(ts, sizeof(ts));
        fprintf(fp, "[%s] USER:%d | ACTION:%-16s | PNR:%-12s | TERM:%-12s | %s\n",
                ts, user_id, action ? action : "", pnr ? pnr : "-",
                terminal ? terminal : "-", details ? details : "");
        fclose(fp);
    }

    concurrency_unlock_audit();
    return DB_SUCCESS;
}

int db_get_audit_logs(AuditLog out_logs[], int max_logs) {
    int count = (g_audit_count < max_logs) ? g_audit_count : max_logs;
    for (int i = 0; i < count; i++) {
        out_logs[i] = g_audits[i];
    }
    return count;
}

int db_get_reservation_chart(int train_no, const char *travel_date,
                              Passenger out_passengers[], int max_passengers) {
    int count = 0;
    for (int i = 0; i < g_booking_count && count < max_passengers; i++) {
        Booking *b = &g_bookings[i];
        if (b->train_no == train_no &&
            strcmp(b->travel_date, travel_date) == 0 &&
            !b->is_cancelled) {

            for (int p = 0; p < b->passenger_count && count < max_passengers; p++) {
                if (b->passengers[p].status == STATUS_CNF || b->passengers[p].status == STATUS_RAC) {
                    out_passengers[count++] = b->passengers[p];
                }
            }
        }
    }
    return count;
}

int db_get_system_stats(DBSystemStats *stats) {
    if (!stats) return DB_ERROR;
    memset(stats, 0, sizeof(DBSystemStats));

    stats->total_users = g_user_count;
    stats->total_trains = g_train_count;
    stats->total_bookings = g_booking_count;

    for (int i = 0; i < g_booking_count; i++) {
        Booking *b = &g_bookings[i];
        stats->total_passengers += b->passenger_count;
        if (b->is_cancelled) {
            stats->cancelled_bookings++;
        } else {
            if (b->overall_status == STATUS_CNF) stats->confirmed_bookings++;
            else if (b->overall_status == STATUS_RAC) stats->rac_bookings++;
            else if (b->overall_status == STATUS_WL) stats->wl_bookings++;
            stats->total_revenue += b->total_fare;
        }
    }

    for (int i = 0; i < g_cancel_count; i++) {
        stats->total_refunded += g_cancels[i].refund_amount;
    }

    return DB_SUCCESS;
}
