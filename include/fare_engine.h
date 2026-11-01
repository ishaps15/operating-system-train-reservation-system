#ifndef FARE_ENGINE_H
#define FARE_ENGINE_H

#include "models.h"

typedef struct {
    double base_fare;
    double class_multiplier;
    double distance_km;
    double superfast_charge;
    double surge_multiplier;
    double surge_amount;
    double tatkal_charge;
    double concession_discount;
    double subtotal;
    double gst_rate;
    double gst_amount;
    double final_fare;
} FareBreakdown;

typedef struct {
    double original_fare;
    double cancellation_charge;
    double clerkage_fee;
    double refund_amount;
    double refund_percentage;
    char   rule_applied[MAX_STR_LG];
} RefundBreakdown;

/* Distance calculation between stations along route */
int fare_get_distance_km(const char *source_code, const char *dest_code);

/* Dynamic Fare Calculation for a passenger */
int fare_compute_passenger_fare(TravelClass travel_class,
                                const char *source,
                                const char *dest,
                                QuotaType quota,
                                ConcessionType concession,
                                char gender,
                                int age,
                                double current_occupancy_ratio,
                                bool is_superfast,
                                FareBreakdown *breakdown);

/* Dynamic Refund Calculation on cancellation */
int fare_compute_cancellation_refund(TravelClass travel_class,
                                     BookingStatus status,
                                     double total_paid_fare,
                                     double hours_before_departure,
                                     RefundBreakdown *refund);

/* Station coordinate & distance matrix lookup */
const Station* fare_get_station(const char *station_code);
int fare_get_all_stations(Station out_stations[], int max_stations);

#endif /* FARE_ENGINE_H */
