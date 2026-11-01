#ifndef MODELS_H
#define MODELS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define MAX_STR_XS     16
#define MAX_STR_SM     32
#define MAX_STR_MD     64
#define MAX_STR_LG     128
#define MAX_STR_XL     256

#define MAX_PASSENGERS_PER_TICKET 6
#define MAX_COACHES_PER_TRAIN     16
#define MAX_SEATS_PER_COACH       72
#define MAX_STATIONS_PER_ROUTE    20

#define PNR_LENGTH               16  /* e.g. "234-8910231" */
#define MAX_CHART_ENTRIES        512
#define HASH_HEX_LEN             65  /* 64 hex chars + null terminator */
#define SALT_HEX_LEN             33  /* 32 hex chars + null terminator */

/* User Roles */
typedef enum {
    ROLE_PASSENGER = 1,
    ROLE_AGENT     = 2,  /* Booking clerk at Durgapur / Howrah counter */
    ROLE_ADMIN     = 3,  /* Railway Administrator */
    ROLE_TTE       = 4   /* Travelling Ticket Examiner / Train Conductor */
} UserRole;

/* Train Travel Classes */
typedef enum {
    CLASS_1A = 1,  /* AC First Class */
    CLASS_2A = 2,  /* AC 2-Tier */
    CLASS_3A = 3,  /* AC 3-Tier */
    CLASS_CC = 4,  /* AC Chair Car */
    CLASS_SL = 5,  /* Sleeper Class */
    CLASS_2S = 6   /* Second Sitting */
} TravelClass;

/* Berth / Seat Types */
typedef enum {
    BERTH_LOWER       = 1,
    BERTH_MIDDLE      = 2,
    BERTH_UPPER       = 3,
    BERTH_SIDE_LOWER  = 4,
    BERTH_SIDE_UPPER  = 5,
    BERTH_WINDOW      = 6,
    BERTH_AISLE       = 7,
    BERTH_CABIN       = 8
} BerthType;

/* Reservation Quota Types */
typedef enum {
    QUOTA_GENERAL    = 1,
    QUOTA_TATKAL     = 2,
    QUOTA_LADIES     = 3,
    QUOTA_SENIOR     = 4,
    QUOTA_DEFENSE    = 5
} QuotaType;

/* Booking Status */
typedef enum {
    STATUS_CNF       = 1,  /* Confirmed */
    STATUS_RAC       = 2,  /* Reservation Against Cancellation */
    STATUS_WL        = 3,  /* Waiting List */
    STATUS_CANCELLED = 4   /* Cancelled */
} BookingStatus;

/* Concession Types */
typedef enum {
    CONC_NONE            = 0,
    CONC_SENIOR_CITIZEN  = 1,  /* 40% (Male) / 50% (Female) */
    CONC_CHILD           = 2,  /* 50% for child under 12 */
    CONC_STUDENT         = 3,  /* 40% discount for students */
    CONC_DEFENSE         = 4   /* 25% discount for armed forces */
} ConcessionType;

/* Transaction Types */
typedef enum {
    TX_BOOKING           = 1,
    TX_CANCELLATION      = 2,
    TX_TATKAL_SURCHARGE  = 3,
    TX_REFUND            = 4
} TransactionType;

/* --- Domain Data Structures --- */

/* User Account */
typedef struct {
    int      user_id;
    char     username[MAX_STR_SM];
    char     password_hash[HASH_HEX_LEN];
    char     salt[SALT_HEX_LEN];
    char     full_name[MAX_STR_MD];
    UserRole role;
    char     phone[MAX_STR_SM];
    char     email[MAX_STR_MD];
    char     station_code[MAX_STR_XS]; /* e.g. "DGR" for Durgapur agent counter */
    time_t   created_at;
    bool     is_active;
} User;

/* Railway Station */
typedef struct {
    char     code[MAX_STR_XS];     /* e.g. "DGR", "HWH", "NDLS" */
    char     name[MAX_STR_MD];     /* "Durgapur", "Howrah", "New Delhi" */
    char     division[MAX_STR_SM]; /* e.g. "Asansol (ASN)", "Howrah" */
    char     zone[MAX_STR_SM];     /* e.g. "Eastern Railway (ER)" */
    int      platform_count;
} Station;

/* Station on a Train Route */
typedef struct {
    char     station_code[MAX_STR_XS];
    int      stop_sequence;
    int      distance_from_source_km;
    char     arr_time[MAX_STR_XS]; /* "HH:MM" */
    char     dep_time[MAX_STR_XS]; /* "HH:MM" */
    int      halt_minutes;
} RouteStop;

/* Train Information */
typedef struct {
    int       train_no;                    /* e.g. 12301 */
    char      train_name[MAX_STR_MD];      /* "Rajdhani Express" */
    char      source[MAX_STR_XS];          /* "HWH" */
    char      destination[MAX_STR_XS];     /* "NDLS" */
    int       total_distance_km;
    char      departure_time[MAX_STR_XS];  /* "16:50" */
    char      arrival_time[MAX_STR_XS];    /* "10:05" */
    int       stop_count;
    RouteStop stops[MAX_STATIONS_PER_ROUTE];
    
    /* Capacity by Class */
    int       capacity_1a;
    int       capacity_2a;
    int       capacity_3a;
    int       capacity_cc;
    int       capacity_sl;
    int       capacity_2s;

    /* Max RAC / WL limit */
    int       rac_limit;
    int       wl_limit;

    double    base_rate_per_km;            /* Standard multiplier */
    bool      is_superfast;
    bool      active;
} Train;

/* Seat Inventory Record (File & Memory Store) */
typedef struct {
    int         inventory_id;
    int         train_no;
    char        travel_date[MAX_STR_XS];   /* "YYYY-MM-DD" */
    TravelClass travel_class;
    char        coach_code[MAX_STR_XS];    /* e.g. "B1", "A1", "S3" */
    int         seat_number;               /* 1 to 72 */
    BerthType   berth_type;
    bool        is_booked;
    int         booking_id;                /* Reference to booking */
    char        pnr[PNR_LENGTH];
} SeatInventory;

/* Individual Passenger within a Booking */
typedef struct {
    int            passenger_id;
    int            booking_id;
    char           pnr[PNR_LENGTH];
    char           name[MAX_STR_MD];
    int            age;
    char           gender;                 /* 'M', 'F', 'O' */
    BerthType      preferred_berth;
    BerthType      allocated_berth;
    char           coach_code[MAX_STR_XS];
    int            seat_number;
    BookingStatus  status;                 /* CNF, RAC, WL, CANCELLED */
    int            waitlist_number;        /* if status is WL or RAC */
    ConcessionType concession;
    double         individual_fare;
} Passenger;

/* Ticket Booking Record */
typedef struct {
    int             booking_id;
    char            pnr[PNR_LENGTH];
    int             user_id;               /* Booked by */
    int             train_no;
    char            source[MAX_STR_XS];
    char            destination[MAX_STR_XS];
    char            travel_date[MAX_STR_XS];/* "YYYY-MM-DD" */
    TravelClass     travel_class;
    QuotaType       quota;
    int             passenger_count;
    Passenger       passengers[MAX_PASSENGERS_PER_TICKET];
    double          base_fare;
    double          surge_amount;
    double          tatkal_charge;
    double          discount_amount;
    double          gst_amount;
    double          total_fare;
    BookingStatus   overall_status;
    time_t          booking_timestamp;
    bool            is_cancelled;
    time_t          cancelled_timestamp;
} Booking;

/* Cancellation & Refund Record */
typedef struct {
    int      cancel_id;
    char     pnr[PNR_LENGTH];
    int      passenger_id;
    int      user_id;
    time_t   cancelled_at;
    double   original_fare;
    double   cancellation_penalty;
    double   refund_amount;
    char     reason[MAX_STR_MD];
} CancellationRecord;

/* Financial Transaction Ledger */
typedef struct {
    int             tx_id;
    char            pnr[PNR_LENGTH];
    int             user_id;
    TransactionType tx_type;
    double          amount;
    time_t          timestamp;
    char            payment_mode[MAX_STR_SM]; /* e.g. "CASH_COUNTER", "UPI", "CARD" */
    char            remarks[MAX_STR_MD];
} TransactionLedger;

/* System Audit Log for Operating System Security & Traceability */
typedef struct {
    int      log_id;
    time_t   timestamp;
    int      user_id;
    char     action[MAX_STR_SM];   /* "LOGIN", "BOOK_TICKET", "CANCEL", "CHART_PREP" */
    char     pnr[PNR_LENGTH];
    char     ip_or_terminal[MAX_STR_SM];
    char     details[MAX_STR_XL];
} AuditLog;

/* Helper string conversion utilities */
const char* role_to_str(UserRole role);
const char* class_to_str(TravelClass tc);
const char* class_code_to_str(TravelClass tc);
TravelClass parse_class_code(const char *code);
const char* berth_to_str(BerthType bt);
const char* status_to_str(BookingStatus status);
const char* quota_to_str(QuotaType q);
const char* concession_to_str(ConcessionType c);

#endif /* MODELS_H */
