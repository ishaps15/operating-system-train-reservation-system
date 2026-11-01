#ifndef DB_H
#define DB_H

#include "models.h"
#include <stdbool.h>

#define DB_SUCCESS  0
#define DB_ERROR   -1
#define DB_NOTFOUND 1

typedef struct {
    char host[MAX_STR_SM];
    int  port;
    char user[MAX_STR_SM];
    char password[MAX_STR_SM];
    char database[MAX_STR_SM];
    bool use_live_mysql;
    void *mysql_conn; /* void* to abstract MYSQL* without forcing header order */
} DBConfig;

typedef struct {
    int total_users;
    int total_trains;
    int total_bookings;
    int total_passengers;
    int confirmed_bookings;
    int rac_bookings;
    int wl_bookings;
    int cancelled_bookings;
    double total_revenue;
    double total_refunded;
} DBSystemStats;

/* Database Lifecycle */
int  db_init(DBConfig *config);
int  db_connect(void);
void db_disconnect(void);
bool db_is_connected(void);
int  db_seed_initial_data(void);

/* OS File Handling & Persistence */
int  db_flush_to_disk(void);
int  db_write_wal_entry(const char *operation, const char *payload);
int  db_acquire_file_lock(int fd, bool exclusive);
int  db_release_file_lock(int fd);

/* Transaction Control */
int  db_begin_transaction(void);
int  db_commit(void);
int  db_rollback(void);

/* User Management Queries */
int  db_find_user_by_username(const char *username, User *out_user);
int  db_find_user_by_id(int user_id, User *out_user);
int  db_create_user(const User *user);
int  db_update_user_password(int user_id, const char *new_hash, const char *new_salt);
int  db_get_all_users(User out_users[], int max_users);

/* Train & Schedule Queries */
int  db_get_train(int train_no, Train *out_train);
int  db_get_all_trains(Train out_trains[], int max_trains);
int  db_search_trains(const char *source, const char *dest, Train out_trains[], int max_trains);
int  db_create_train(const Train *train);

/* Seat Inventory & Availability */
int  db_get_class_capacity(int train_no, TravelClass travel_class);
int  db_get_inventory_status(int train_no, const char *travel_date, TravelClass travel_class,
                             int *out_confirmed_left, int *out_rac_left, int *out_wl_count);
int  db_allocate_seat(int train_no, const char *travel_date, TravelClass travel_class,
                      BerthType preferred_berth, char out_coach[MAX_STR_XS],
                      int *out_seat_no, BerthType *out_allocated_berth);
int  db_release_seat(int train_no, const char *travel_date, const char *coach, int seat_no);

/* Booking Operations */
int  db_create_booking(Booking *booking);
int  db_get_booking_by_pnr(const char *pnr, Booking *out_booking);
int  db_get_bookings_by_user(int user_id, Booking out_bookings[], int max_bookings);
int  db_get_all_bookings(Booking out_bookings[], int max_bookings);
int  db_cancel_booking(const char *pnr, int user_id, const char *reason,
                       CancellationRecord *out_record);
int  db_cancel_single_passenger(const char *pnr, int passenger_id, int user_id,
                                const char *reason, CancellationRecord *out_record);

/* Queue Promotion Engine (RAC -> CNF, WL -> RAC) */
int  db_promote_rac_and_wl(int train_no, const char *travel_date, TravelClass travel_class,
                           const char *freed_coach, int freed_seat_no, BerthType freed_berth);

/* Finance & Transactions */
int  db_record_transaction(const TransactionLedger *tx);
int  db_get_transactions_by_pnr(const char *pnr, TransactionLedger out_tx[], int max_tx);

/* System Audit & Charting */
int  db_write_audit_log(int user_id, const char *action, const char *pnr,
                        const char *terminal, const char *details);
int  db_get_audit_logs(AuditLog out_logs[], int max_logs);
int  db_get_reservation_chart(int train_no, const char *travel_date,
                              Passenger out_passengers[], int max_passengers);
int  db_get_system_stats(DBSystemStats *stats);

#endif /* DB_H */
