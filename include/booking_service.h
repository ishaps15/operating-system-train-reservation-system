#ifndef BOOKING_SERVICE_H
#define BOOKING_SERVICE_H

#include "models.h"
#include "fare_engine.h"

typedef struct {
    char name[MAX_STR_MD];
    int  age;
    char gender;
    BerthType preferred_berth;
    ConcessionType concession;
} PassengerInput;

typedef struct {
    int            user_id;
    int            train_no;
    char           source[MAX_STR_XS];
    char           dest[MAX_STR_XS];
    char           travel_date[MAX_STR_XS];
    TravelClass    travel_class;
    QuotaType      quota;
    int            passenger_count;
    PassengerInput passengers[MAX_PASSENGERS_PER_TICKET];
    char           payment_mode[MAX_STR_SM];
} BookingRequest;

int  booking_service_init(void);
void booking_generate_pnr(char out_pnr[PNR_LENGTH]);

/* Core Booking Execution */
int  booking_service_book_ticket(const BookingRequest *req, Booking *out_booking);

/* PNR Status Check */
int  booking_service_get_pnr_status(const char *pnr, Booking *out_booking);

/* Cancellation and Queue Promotion */
int  booking_service_cancel_pnr(const char *pnr, int user_id, double hours_before_dep,
                                const char *reason, CancellationRecord *out_record);
int  booking_service_cancel_passenger(const char *pnr, int passenger_id, int user_id,
                                      double hours_before_dep, const char *reason,
                                      CancellationRecord *out_record);

/* Reservation Charting (for TTE Conductor) */
int  booking_service_generate_chart(int train_no, const char *travel_date,
                                    Passenger out_chart[], int max_passengers);

/* Frequent Passenger Master List */
typedef struct {
    char name[MAX_STR_MD];
    int  age;
    char gender;
    BerthType preferred_berth;
} MasterPassenger;

int  booking_save_master_passenger(int user_id, const MasterPassenger *mp);
int  booking_get_master_passengers(int user_id, MasterPassenger out_list[], int max_list);

#endif /* BOOKING_SERVICE_H */
