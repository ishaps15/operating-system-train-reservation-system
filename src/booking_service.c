#include "booking_service.h"
#include "concurrency.h"
#include "db.h"
#include "train_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_MASTER_PASSENGERS 64
static MasterPassenger g_master_passengers[MAX_MASTER_PASSENGERS];
static int             g_master_count = 0;

int booking_service_init(void) {
    return 0;
}

void booking_generate_pnr(char out_pnr[PNR_LENGTH]) {
    concurrency_lock_pnr();
    static int sequence = 1000;
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL) ^ 0x5F3759DF);
        seeded = 1;
    }

    /* 3-digit zone prefix (234 for Eastern Railway / Durgapur PRS) + 7-digit unique sequence */
    int prefix = 230 + (rand() % 10);
    sequence = (sequence + 1 + (rand() % 97)) % 10000000;
    if (sequence < 1000000) sequence += 1000000;

    snprintf(out_pnr, PNR_LENGTH, "%03d-%07d", prefix, sequence);
    concurrency_unlock_pnr();
}

int booking_service_book_ticket(const BookingRequest *req, Booking *out_booking) {
    if (!req || !out_booking || req->passenger_count <= 0 || req->passenger_count > MAX_PASSENGERS_PER_TICKET) {
        return -1;
    }

    Train train;
    if (db_get_train(req->train_no, &train) != DB_SUCCESS) {
        return -2; /* Train not found */
    }

    if (!train_service_route_valid(req->train_no, req->source, req->dest)) {
        return -3; /* Invalid route or reverse travel */
    }

    /* Check seat availability & occupancy ratio for dynamic fare calculation */
    int cnf_left = 0, rac_left = 0, wl_count = 0;
    double occupancy = 0.0;
    train_service_check_availability(req->train_no, req->travel_date, req->travel_class,
                                     &cnf_left, &rac_left, &wl_count, &occupancy);

    /* Check overall quota bounds */
    int total_avail = cnf_left + rac_left + (train.wl_limit - wl_count);
    if (req->passenger_count > total_avail) {
        return -4; /* Overcapacity: REGRET */
    }

    /* Prepare booking record */
    memset(out_booking, 0, sizeof(Booking));
    booking_generate_pnr(out_booking->pnr);
    out_booking->user_id = req->user_id;
    out_booking->train_no = req->train_no;
    strncpy(out_booking->source, req->source, sizeof(out_booking->source)-1);
    strncpy(out_booking->destination, req->dest, sizeof(out_booking->destination)-1);
    strncpy(out_booking->travel_date, req->travel_date, sizeof(out_booking->travel_date)-1);
    out_booking->travel_class = req->travel_class;
    out_booking->quota = req->quota;
    out_booking->passenger_count = req->passenger_count;

    bool has_rac = false;
    bool has_wl = false;

    /* Process each passenger */
    for (int i = 0; i < req->passenger_count; i++) {
        const PassengerInput *pin = &req->passengers[i];
        Passenger *pout = &out_booking->passengers[i];

        pout->passenger_id = i + 1;
        strncpy(pout->pnr, out_booking->pnr, PNR_LENGTH-1);
        strncpy(pout->name, pin->name, sizeof(pout->name)-1);
        pout->age = pin->age;
        pout->gender = pin->gender;
        pout->preferred_berth = pin->preferred_berth;
        pout->concession = pin->concession;

        /* Senior Citizen preference for lower berth */
        if (pout->age >= 60 && pout->preferred_berth == 0) {
            pout->preferred_berth = BERTH_LOWER;
        }

        /* Dynamic Fare Calculation for this passenger */
        FareBreakdown fare;
        fare_compute_passenger_fare(req->travel_class, req->source, req->dest,
                                    req->quota, pin->concession, pin->gender,
                                    pin->age, occupancy, train.is_superfast, &fare);

        pout->individual_fare = fare.final_fare;
        out_booking->base_fare += fare.base_fare;
        out_booking->surge_amount += fare.surge_amount;
        out_booking->tatkal_charge += fare.tatkal_charge;
        out_booking->discount_amount += fare.concession_discount;
        out_booking->gst_amount += fare.gst_amount;
        out_booking->total_fare += fare.final_fare;

        /* Seat Allocation */
        char coach[MAX_STR_XS] = "";
        int seat_no = 0;
        BerthType alloc_berth = BERTH_LOWER;

        int alloc_rc = db_allocate_seat(req->train_no, req->travel_date, req->travel_class,
                                        pout->preferred_berth, coach, &seat_no, &alloc_berth);

        if (alloc_rc == DB_SUCCESS) {
            pout->status = STATUS_CNF;
            strncpy(pout->coach_code, coach, sizeof(pout->coach_code)-1);
            pout->seat_number = seat_no;
            pout->allocated_berth = alloc_berth;
            pout->waitlist_number = 0;
        } else {
            /* Try RAC */
            if (rac_left > 0) {
                pout->status = STATUS_RAC;
                pout->allocated_berth = BERTH_SIDE_LOWER;
                pout->waitlist_number = train.rac_limit - rac_left + 1;
                rac_left--;
                has_rac = true;
            } else if (wl_count < train.wl_limit) {
                /* Waiting List */
                pout->status = STATUS_WL;
                pout->allocated_berth = BERTH_LOWER;
                pout->waitlist_number = wl_count + 1;
                wl_count++;
                has_wl = true;
            } else {
                return -4; /* Capacity full */
            }
        }
    }

    if (has_wl) {
        out_booking->overall_status = STATUS_WL;
    } else if (has_rac) {
        out_booking->overall_status = STATUS_RAC;
    } else {
        out_booking->overall_status = STATUS_CNF;
    }

    /* Persist to database */
    if (db_create_booking(out_booking) != DB_SUCCESS) {
        return -5;
    }

    /* Record Financial Payment Transaction */
    TransactionLedger tx;
    memset(&tx, 0, sizeof(TransactionLedger));
    strncpy(tx.pnr, out_booking->pnr, PNR_LENGTH-1);
    tx.user_id = req->user_id;
    tx.tx_type = (req->quota == QUOTA_TATKAL) ? TX_TATKAL_SURCHARGE : TX_BOOKING;
    tx.amount = out_booking->total_fare;
    tx.timestamp = time(NULL);
    strncpy(tx.payment_mode, req->payment_mode[0] ? req->payment_mode : "COUNTER_CASH", sizeof(tx.payment_mode)-1);
    snprintf(tx.remarks, sizeof(tx.remarks), "Booking for %d passenger(s)", req->passenger_count);
    db_record_transaction(&tx);

    /* Audit Log */
    char audit_buf[256];
    snprintf(audit_buf, sizeof(audit_buf),
             "Booked PNR %s: Train %d (%s -> %s), Class %s, Status %s, Fare Rs.%.2f",
             out_booking->pnr, req->train_no, req->source, req->dest,
             class_code_to_str(req->travel_class),
             status_to_str(out_booking->overall_status), out_booking->total_fare);
    db_write_audit_log(req->user_id, "BOOK_TICKET", out_booking->pnr, "PRS_COUNTER", audit_buf);

    return 0;
}

int booking_service_get_pnr_status(const char *pnr, Booking *out_booking) {
    if (!pnr || !out_booking) return -1;
    return db_get_booking_by_pnr(pnr, out_booking);
}

int booking_service_cancel_pnr(const char *pnr, int user_id, double hours_before_dep,
                                const char *reason, CancellationRecord *out_record) {
    (void)hours_before_dep;
    if (!pnr) return -1;
    return db_cancel_booking(pnr, user_id, reason, out_record);
}

int booking_service_cancel_passenger(const char *pnr, int passenger_id, int user_id,
                                      double hours_before_dep, const char *reason,
                                      CancellationRecord *out_record) {
    (void)hours_before_dep;
    if (!pnr) return -1;
    return db_cancel_single_passenger(pnr, passenger_id, user_id, reason, out_record);
}

int booking_service_generate_chart(int train_no, const char *travel_date,
                                    Passenger out_chart[], int max_passengers) {
    return db_get_reservation_chart(train_no, travel_date, out_chart, max_passengers);
}

int booking_save_master_passenger(int user_id, const MasterPassenger *mp) {
    (void)user_id;
    if (!mp || g_master_count >= MAX_MASTER_PASSENGERS) return -1;
    g_master_passengers[g_master_count++] = *mp;
    return 0;
}

int booking_get_master_passengers(int user_id, MasterPassenger out_list[], int max_list) {
    (void)user_id;
    int count = (g_master_count < max_list) ? g_master_count : max_list;
    for (int i = 0; i < count; i++) {
        out_list[i] = g_master_passengers[i];
    }
    return count;
}
