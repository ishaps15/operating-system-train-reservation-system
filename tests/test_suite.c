#include "models.h"
#include "crypto_sha256.h"
#include "auth.h"
#include "fare_engine.h"
#include "train_service.h"
#include "booking_service.h"
#include "concurrency.h"
#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, name) do { \
    if (cond) { \
        printf("  [PASS] %s\n", name); \
        g_tests_passed++; \
    } else { \
        printf("  [FAIL] %s (Line %d)\n", name, __LINE__); \
        g_tests_failed++; \
    } \
} while(0)

static void test_crypto_hashing(void) {
    printf("\n=== Running Test: Cryptographic Password & Salt Hashing ===\n");

    char hash[65];
    sha256_hash_string("HelloRailway123", hash);
    TEST_ASSERT(strlen(hash) == 64, "SHA-256 output length is 64 hex characters");

    char salt1[33], salt2[33];
    generate_random_salt(salt1);
    generate_random_salt(salt2);
    TEST_ASSERT(strcmp(salt1, salt2) != 0, "Generated salts are randomized and unique");

    char hash1[65], hash2[65];
    hash_password_with_salt("Pass@123", salt1, hash1);
    hash_password_with_salt("Pass@123", salt1, hash2);
    TEST_ASSERT(strcmp(hash1, hash2) == 0, "Deterministic salted hashing for identical salt/password");
}

static void test_dynamic_fare_engine(void) {
    printf("\n=== Running Test: Dynamic Fare & Cancellation Refund Engine ===\n");

    /* Distance check */
    int dist_dgr_ndls = fare_get_distance_km("DGR", "NDLS");
    TEST_ASSERT(dist_dgr_ndls > 1200 && dist_dgr_ndls < 1350, "Distance between Durgapur and New Delhi is ~1276 km");

    int dist_hwh_dgr = fare_get_distance_km("HWH", "DGR");
    TEST_ASSERT(dist_hwh_dgr == 171, "Distance between Howrah and Durgapur is exactly 171 km");

    /* Dynamic Fare with 0% surge */
    FareBreakdown fare_normal;
    fare_compute_passenger_fare(CLASS_3A, "DGR", "NDLS", QUOTA_GENERAL, CONC_NONE, 'M', 35, 0.40, true, &fare_normal);
    TEST_ASSERT(fare_normal.surge_multiplier == 1.0, "Occupancy < 50% incurs 1.0x surge (zero surge)");
    TEST_ASSERT(fare_normal.gst_rate == 0.05, "AC 3-Tier attracts 5% GST");

    /* Dynamic Surge with 90% peak occupancy */
    FareBreakdown fare_peak;
    fare_compute_passenger_fare(CLASS_3A, "DGR", "NDLS", QUOTA_GENERAL, CONC_NONE, 'M', 35, 0.90, true, &fare_peak);
    TEST_ASSERT(fare_peak.surge_multiplier == 1.35, "Occupancy >= 85% incurs 1.35x peak surge");
    TEST_ASSERT(fare_peak.final_fare > fare_normal.final_fare, "Peak dynamic fare is strictly higher than normal fare");

    /* Senior citizen concession */
    FareBreakdown fare_senior;
    fare_compute_passenger_fare(CLASS_3A, "DGR", "NDLS", QUOTA_GENERAL, CONC_SENIOR_CITIZEN, 'M', 65, 0.40, true, &fare_senior);
    TEST_ASSERT(fare_senior.concession_discount > 0, "Senior citizen concession discount is applied");
    TEST_ASSERT(fare_senior.final_fare < fare_normal.final_fare, "Senior fare is strictly discounted");

    /* Cancellation refund rules */
    RefundBreakdown ref_48h;
    fare_compute_cancellation_refund(CLASS_3A, STATUS_CNF, 2000.0, 52.0, &ref_48h);
    TEST_ASSERT(ref_48h.cancellation_charge == 180.0, "> 48 Hours: flat clerkage fee Rs 180 for 3A");
    TEST_ASSERT(ref_48h.refund_amount == (2000.0 - 180.0), "Accurate refund amount for > 48h cancellation");

    RefundBreakdown ref_24h;
    fare_compute_cancellation_refund(CLASS_3A, STATUS_CNF, 2000.0, 24.0, &ref_24h);
    TEST_ASSERT(ref_24h.cancellation_charge == 500.0, "12 to 48 Hours: 25% cancellation penalty applied (Rs 500)");

    RefundBreakdown ref_2h;
    fare_compute_cancellation_refund(CLASS_3A, STATUS_CNF, 2000.0, 2.0, &ref_2h);
    TEST_ASSERT(ref_2h.refund_amount == 0.0, "< 4 Hours (Chart Prepared): Zero refund");
}

static void test_authentication_and_rbac(void) {
    printf("\n=== Running Test: Authentication & Role-Based Access Control (RBAC) ===\n");

    AuthSession session;
    auth_init_session(&session);

    /* Test invalid login */
    int login_err = auth_login(&session, "non_existent_user", "WrongPass");
    TEST_ASSERT(login_err != 0, "Invalid user login rejected");
    TEST_ASSERT(!auth_is_authenticated(&session), "Session remains unauthenticated");

    /* Test valid admin login */
    int login_ok = auth_login(&session, "admin", "Admin@123");
    TEST_ASSERT(login_ok == 0, "Admin credentials authenticated successfully");
    TEST_ASSERT(auth_is_authenticated(&session), "Session is active");
    TEST_ASSERT(auth_has_role(&session, ROLE_ADMIN), "Admin role verified");

    /* Logout */
    auth_logout(&session);
    TEST_ASSERT(!auth_is_authenticated(&session), "Session successfully invalidated on logout");

    /* Register new passenger with unique test username */
    char test_username[32];
    snprintf(test_username, sizeof(test_username), "test_user_%ld_%d", time(NULL), rand() % 10000);
    int reg_rc = auth_register_passenger(test_username, "UnitPass@999", "Test Passenger", "9998887776", "test@test.com");
    TEST_ASSERT(reg_rc == DB_SUCCESS, "New passenger account registered");

    int user_login = auth_login(&session, test_username, "UnitPass@999");
    TEST_ASSERT(user_login == 0, "Newly registered passenger logged in successfully");
    TEST_ASSERT(auth_has_role(&session, ROLE_PASSENGER), "Passenger role verified");
}

static void test_pnr_and_booking_workflow(void) {
    printf("\n=== Running Test: PNR Generation & Ticket Booking Workflow ===\n");

    char pnr1[PNR_LENGTH], pnr2[PNR_LENGTH];
    booking_generate_pnr(pnr1);
    booking_generate_pnr(pnr2);
    TEST_ASSERT(strlen(pnr1) == 11, "PNR format is 10 digits with separator (XXX-XXXXXXX)");
    TEST_ASSERT(strcmp(pnr1, pnr2) != 0, "Consecutive PNRs are unique");

    BookingRequest req;
    memset(&req, 0, sizeof(BookingRequest));
    req.user_id = 4;
    req.train_no = 12301;
    strcpy(req.source, "DGR");
    strcpy(req.dest, "NDLS");
    strcpy(req.travel_date, "2026-12-01");
    req.travel_class = CLASS_3A;
    req.quota = QUOTA_GENERAL;
    req.passenger_count = 2;

    strcpy(req.passengers[0].name, "Ramesh Roy");
    req.passengers[0].age = 65; /* Senior citizen -> should be allocated Lower berth */
    req.passengers[0].gender = 'M';
    req.passengers[0].preferred_berth = BERTH_LOWER;

    strcpy(req.passengers[1].name, "Priya Roy");
    req.passengers[1].age = 30;
    req.passengers[1].gender = 'F';
    req.passengers[1].preferred_berth = BERTH_UPPER;

    Booking booking;
    int rc = booking_service_book_ticket(&req, &booking);
    TEST_ASSERT(rc == 0, "Ticket reservation executed successfully");
    TEST_ASSERT(booking.overall_status == STATUS_CNF, "Reservation confirmed (CNF)");
    TEST_ASSERT(booking.passengers[0].status == STATUS_CNF, "Passenger 1 is CNF");
    TEST_ASSERT(booking.passengers[0].seat_number > 0, "Passenger 1 assigned valid seat number");
    TEST_ASSERT(booking.passengers[0].allocated_berth == BERTH_LOWER, "Senior passenger granted preferred Lower berth");
}

static void test_cancellation_and_cascade_promotion(void) {
    printf("\n=== Running Test: Atomic Cancellation & Cascade Promotion (RAC -> CNF) ===\n");

    const char *test_date = "2026-12-10";

    /* Fill confirmed capacity of a class or create a CNF and an RAC booking */
    /* Book Passenger A (Confirmed) */
    BookingRequest req_cnf;
    memset(&req_cnf, 0, sizeof(BookingRequest));
    req_cnf.user_id = 4;
    req_cnf.train_no = 12301;
    strcpy(req_cnf.source, "HWH");
    strcpy(req_cnf.dest, "NDLS");
    strcpy(req_cnf.travel_date, test_date);
    req_cnf.travel_class = CLASS_2A;
    req_cnf.quota = QUOTA_GENERAL;
    req_cnf.passenger_count = 1;
    strcpy(req_cnf.passengers[0].name, "Original CNF Holder");
    req_cnf.passengers[0].age = 40;
    req_cnf.passengers[0].gender = 'M';

    Booking book_cnf;
    int rc1 = booking_service_book_ticket(&req_cnf, &book_cnf);
    TEST_ASSERT(rc1 == 0, "Booking 1 created");
    TEST_ASSERT(book_cnf.passengers[0].status == STATUS_CNF, "Passenger A is CNF");
    int freed_seat = book_cnf.passengers[0].seat_number;

    /* Simulate an RAC candidate waiting for promotion */
    /* Test calling db_promote_rac_and_wl directly */
    Booking book_rac;
    memset(&book_rac, 0, sizeof(Booking));
    booking_generate_pnr(book_rac.pnr);
    book_rac.train_no = 12301;
    strcpy(book_rac.travel_date, test_date);
    book_rac.travel_class = CLASS_2A;
    book_rac.passenger_count = 1;
    book_rac.user_id = 5;
    book_rac.overall_status = STATUS_RAC;
    strcpy(book_rac.passengers[0].name, "Waiting RAC Passenger");
    book_rac.passengers[0].status = STATUS_RAC;
    book_rac.passengers[0].waitlist_number = 1;
    db_create_booking(&book_rac);

    /* Cancel Passenger A's booking */
    CancellationRecord rec;
    int cancel_rc = booking_service_cancel_pnr(book_cnf.pnr, 4, 48.0, "Test cancellation", &rec);
    TEST_ASSERT(cancel_rc == 0, "Passenger A booking cancelled successfully");
    TEST_ASSERT(rec.refund_amount > 0, "Refund issued to cancelled passenger");

    /* Check that the RAC passenger was promoted to CNF with the freed seat */
    Booking updated_rac;
    booking_service_get_pnr_status(book_rac.pnr, &updated_rac);
    TEST_ASSERT(updated_rac.passengers[0].status == STATUS_CNF, "RAC Passenger successfully promoted to CNF");
    TEST_ASSERT(updated_rac.passengers[0].seat_number == freed_seat, "Promoted passenger assigned the freed seat");
}

static void test_concurrency_race_condition_protection(void) {
    printf("\n=== Running Test: POSIX Mutexes & Concurrency Race Condition Safety ===\n");

    SimulationConfig cfg;
    cfg.num_threads = 6;
    cfg.bookings_per_thread = 4;
    cfg.train_no = 12303;
    strcpy(cfg.travel_date, "2026-12-20");
    cfg.travel_class = CLASS_3A;

    SimulationResult res;
    int sim_rc = concurrency_run_multiuser_simulation(&cfg, &res);
    TEST_ASSERT(sim_rc == 0, "Multi-threaded concurrency simulation executed without errors");
    TEST_ASSERT(res.total_requests == 24, "All 24 concurrent requests processed");
    TEST_ASSERT(res.race_conditions_detected == 0, "Zero race conditions / double bookings detected (Mutual exclusion verified!)");
    TEST_ASSERT(res.transactions_per_sec > 50.0, "High concurrency throughput verified");
}

static void test_wal_and_persistence(void) {
    printf("\n=== Running Test: OS Write-Ahead Logging (WAL) & File Persistence ===\n");

    int wal_rc = db_write_wal_entry("TEST_OP", "Unit test WAL entry payload");
    TEST_ASSERT(wal_rc == 0, "WAL entry written with low-level open/write/fsync");

    int flush_rc = db_flush_to_disk();
    TEST_ASSERT(flush_rc == DB_SUCCESS, "State successfully flushed to persistent disk records");

    FILE *wal_fp = fopen("data/transactions.wal", "r");
    TEST_ASSERT(wal_fp != NULL, "transactions.wal file exists on filesystem");
    if (wal_fp) fclose(wal_fp);

    FILE *audit_fp = fopen("data/audit.log", "r");
    TEST_ASSERT(audit_fp != NULL, "audit.log file exists on filesystem");
    if (audit_fp) fclose(audit_fp);
}

int main(void) {
    printf("=======================================================================\n");
    printf(" Running Eastern Railway PRS System Test Suite\n");
    printf("=======================================================================\n");

    concurrency_init();
    db_init(NULL);
    train_service_init();
    booking_service_init();

    test_crypto_hashing();
    test_dynamic_fare_engine();
    test_authentication_and_rbac();
    test_pnr_and_booking_workflow();
    test_cancellation_and_cascade_promotion();
    test_concurrency_race_condition_protection();
    test_wal_and_persistence();

    db_disconnect();
    concurrency_cleanup();

    printf("\n=======================================================================\n");
    printf(" TEST SUITE SUMMARY: %d PASSED, %d FAILED\n", g_tests_passed, g_tests_failed);
    printf("=======================================================================\n");

    return g_tests_failed == 0 ? 0 : 1;
}
