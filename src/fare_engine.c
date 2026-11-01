#include "fare_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

/* Predefined Eastern Railway / National Corridor Stations */
static const Station G_STATIONS[] = {
    {"HWH",  "Howrah Junction",      "Howrah",     "Eastern Railway (ER)",   23},
    {"BDC",  "Bandel Junction",      "Howrah",     "Eastern Railway (ER)",   5},
    {"BWN",  "Barddhaman Junction",  "Howrah",     "Eastern Railway (ER)",   8},
    {"DGR",  "Durgapur",             "Asansol",    "Eastern Railway (ER)",   5},
    {"ASN",  "Asansol Junction",     "Asansol",    "Eastern Railway (ER)",   8},
    {"DHN",  "Dhanbad Junction",     "Dhanbad",    "East Central Rly (ECR)", 8},
    {"GAYA", "Gaya Junction",        "Pt DDU",     "East Central Rly (ECR)", 9},
    {"DDU",  "Pt. Deen Dayal Upad.", "Pt DDU",     "East Central Rly (ECR)", 8},
    {"PRYJ", "Prayagraj Junction",   "Prayagraj",  "North Central Rly (NCR)",10},
    {"CNB",  "Kanpur Central",       "Kanpur",     "North Central Rly (NCR)",10},
    {"NDLS", "New Delhi",            "Delhi",      "Northern Railway (NR)",  16},
    {"RNC",  "Ranchi Junction",      "Ranchi",     "South Eastern Rly (SER)", 6},
    {"NJP",  "New Jalpaiguri",       "Katihar",    "Northeast Frontier (NFR)",5},
    {"DDN",  "Dehradun",             "Moradabad",  "Northern Railway (NR)",   4}
};
static const int G_STATION_COUNT = sizeof(G_STATIONS) / sizeof(G_STATIONS[0]);

/* Route Distance Along the Grand Chord / Howrah-Delhi Main Trunk (km from Howrah) */
typedef struct {
    char code[MAX_STR_XS];
    int  dist_from_hwh_km;
} StationDist;

static const StationDist G_ROUTE_DISTANCES[] = {
    {"HWH",  0},
    {"BDC",  40},
    {"BWN",  107},
    {"DGR",  171},   /* Durgapur is 171 km from Howrah */
    {"ASN",  213},   /* Asansol is 213 km from Howrah */
    {"DHN",  272},   /* Dhanbad is 272 km from Howrah */
    {"RNC",  412},   /* Ranchi via Asansol branch */
    {"GAYA", 471},   /* Gaya Junction */
    {"DDU",  676},   /* Pt. Deen Dayal Upadhyaya Junction */
    {"PRYJ", 829},   /* Prayagraj Junction */
    {"CNB",  1023},  /* Kanpur Central */
    {"NDLS", 1447},  /* New Delhi Railway Station */
    {"NJP",  560},   /* New Jalpaiguri from Howrah */
    {"DDN",  1530}   /* Dehradun from Howrah */
};
static const int G_ROUTE_COUNT = sizeof(G_ROUTE_DISTANCES) / sizeof(G_ROUTE_DISTANCES[0]);

const Station* fare_get_station(const char *station_code) {
    if (!station_code) return NULL;
    for (int i = 0; i < G_STATION_COUNT; i++) {
        if (strcasecmp(G_STATIONS[i].code, station_code) == 0) {
            return &G_STATIONS[i];
        }
    }
    return NULL;
}

int fare_get_all_stations(Station out_stations[], int max_stations) {
    int count = (G_STATION_COUNT < max_stations) ? G_STATION_COUNT : max_stations;
    for (int i = 0; i < count; i++) {
        out_stations[i] = G_STATIONS[i];
    }
    return count;
}

static int get_km_from_hwh(const char *code) {
    for (int i = 0; i < G_ROUTE_COUNT; i++) {
        if (strcasecmp(G_ROUTE_DISTANCES[i].code, code) == 0) {
            return G_ROUTE_DISTANCES[i].dist_from_hwh_km;
        }
    }
    return -1;
}

int fare_get_distance_km(const char *source_code, const char *dest_code) {
    if (!source_code || !dest_code) return 100;
    if (strcasecmp(source_code, dest_code) == 0) return 0;

    int d1 = get_km_from_hwh(source_code);
    int d2 = get_km_from_hwh(dest_code);

    if (d1 >= 0 && d2 >= 0) {
        int dist = abs(d2 - d1);
        return dist > 0 ? dist : 50;
    }
    return 250; /* Default reasonable intercity distance fallback */
}

int fare_compute_passenger_fare(TravelClass travel_class,
                                const char *source,
                                const char *dest,
                                QuotaType quota,
                                ConcessionType concession,
                                char gender,
                                int age,
                                double current_occupancy_ratio,
                                bool is_superfast,
                                FareBreakdown *breakdown) {
    if (!breakdown) return -1;
    memset(breakdown, 0, sizeof(FareBreakdown));

    int dist = fare_get_distance_km(source, dest);
    if (dist <= 0) dist = 50;
    breakdown->distance_km = (double)dist;

    /* Base rate per km by class */
    double rate_per_km = 0.50;
    double min_fare = 50.0;
    double superfast = 0.0;
    double gst_pct = 0.0;

    switch (travel_class) {
        case CLASS_1A:
            rate_per_km = 3.60;
            min_fare = 500.0;
            superfast = is_superfast ? 75.0 : 0.0;
            gst_pct = 0.05;
            break;
        case CLASS_2A:
            rate_per_km = 2.40;
            min_fare = 350.0;
            superfast = is_superfast ? 45.0 : 0.0;
            gst_pct = 0.05;
            break;
        case CLASS_3A:
            rate_per_km = 1.65;
            min_fare = 250.0;
            superfast = is_superfast ? 45.0 : 0.0;
            gst_pct = 0.05;
            break;
        case CLASS_CC:
            rate_per_km = 1.45;
            min_fare = 200.0;
            superfast = is_superfast ? 45.0 : 0.0;
            gst_pct = 0.05;
            break;
        case CLASS_SL:
            rate_per_km = 0.65;
            min_fare = 120.0;
            superfast = is_superfast ? 30.0 : 0.0;
            gst_pct = 0.0;
            break;
        case CLASS_2S:
            rate_per_km = 0.35;
            min_fare = 50.0;
            superfast = is_superfast ? 15.0 : 0.0;
            gst_pct = 0.0;
            break;
        default:
            rate_per_km = 0.65;
            min_fare = 120.0;
            gst_pct = 0.0;
            break;
    }

    breakdown->class_multiplier = rate_per_km;
    breakdown->superfast_charge = superfast;

    /* Raw base fare */
    double raw_base = (rate_per_km * dist) + superfast;
    if (raw_base < min_fare) raw_base = min_fare;
    breakdown->base_fare = round(raw_base);

    /* Dynamic Pricing / Demand Surge:
       Simulating dynamic fare mechanism based on train occupancy */
    double surge_mult = 1.0;
    if (current_occupancy_ratio >= 0.85) {
        surge_mult = 1.35; /* 35% surge during peak high occupancy */
    } else if (current_occupancy_ratio >= 0.70) {
        surge_mult = 1.20; /* 20% surge */
    } else if (current_occupancy_ratio >= 0.50) {
        surge_mult = 1.10; /* 10% surge */
    }
    breakdown->surge_multiplier = surge_mult;
    breakdown->surge_amount = round(breakdown->base_fare * (surge_mult - 1.0));

    /* Tatkal Quota Premium */
    double tatkal = 0.0;
    if (quota == QUOTA_TATKAL) {
        switch (travel_class) {
            case CLASS_1A: tatkal = 500.0; break;
            case CLASS_2A: tatkal = 400.0; break;
            case CLASS_3A: tatkal = 300.0; break;
            case CLASS_CC: tatkal = 225.0; break;
            case CLASS_SL: tatkal = 125.0; break;
            case CLASS_2S: tatkal = 30.0;  break;
            default:       tatkal = 100.0; break;
        }
    }
    breakdown->tatkal_charge = tatkal;

    /* Concessions */
    double discount = 0.0;
    if (quota != QUOTA_TATKAL) { /* Tatkal does not allow concessions */
        if (concession == CONC_SENIOR_CITIZEN || (age >= 60 && (gender == 'M' || gender == 'm')) || (age >= 58 && (gender == 'F' || gender == 'f'))) {
            /* Senior Citizen: 40% for male, 50% for female */
            if (gender == 'F' || gender == 'f') {
                discount = breakdown->base_fare * 0.50;
            } else {
                discount = breakdown->base_fare * 0.40;
            }
        } else if (concession == CONC_CHILD || (age >= 5 && age < 12)) {
            /* Child Half-Fare: 50% discount */
            discount = breakdown->base_fare * 0.50;
        } else if (concession == CONC_STUDENT) {
            if (travel_class == CLASS_SL || travel_class == CLASS_2S) {
                discount = breakdown->base_fare * 0.40;
            }
        } else if (concession == CONC_DEFENSE) {
            discount = breakdown->base_fare * 0.25;
        }
    }
    breakdown->concession_discount = round(discount);

    /* Subtotal before tax */
    double subtotal = breakdown->base_fare + breakdown->surge_amount + breakdown->tatkal_charge - breakdown->concession_discount;
    if (subtotal < min_fare * 0.5) subtotal = min_fare * 0.5;
    breakdown->subtotal = subtotal;

    /* GST on AC Classes */
    breakdown->gst_rate = gst_pct;
    breakdown->gst_amount = round(subtotal * gst_pct);

    /* Final Fare rounded to nearest 5 rupees (Indian Railway rounding rule) */
    double total = subtotal + breakdown->gst_amount;
    int rounded_total = ((int)(total + 2.5) / 5) * 5;
    breakdown->final_fare = (double)rounded_total;

    return 0;
}

int fare_compute_cancellation_refund(TravelClass travel_class,
                                     BookingStatus status,
                                     double total_paid_fare,
                                     double hours_before_departure,
                                     RefundBreakdown *refund) {
    if (!refund) return -1;
    memset(refund, 0, sizeof(RefundBreakdown));
    refund->original_fare = total_paid_fare;

    /* RAC or Waiting List Tickets */
    if (status == STATUS_RAC || status == STATUS_WL) {
        double clerkage = (travel_class == CLASS_SL || travel_class == CLASS_2S) ? 60.0 : 80.0;
        if (clerkage > total_paid_fare) clerkage = total_paid_fare;
        refund->clerkage_fee = clerkage;
        refund->cancellation_charge = clerkage;
        refund->refund_amount = total_paid_fare - clerkage;
        refund->refund_percentage = (refund->refund_amount / total_paid_fare) * 100.0;
        snprintf(refund->rule_applied, sizeof(refund->rule_applied),
                 "RAC/WL Flat Clerkage Deduction (Rs. %.0f)", clerkage);
        return 0;
    }

    /* Flat minimum clerkage charges for Confirmed (CNF) Tickets */
    double min_clerkage = 60.0;
    switch (travel_class) {
        case CLASS_1A: min_clerkage = 240.0; break;
        case CLASS_2A: min_clerkage = 200.0; break;
        case CLASS_3A: min_clerkage = 180.0; break;
        case CLASS_CC: min_clerkage = 180.0; break;
        case CLASS_SL: min_clerkage = 120.0; break;
        case CLASS_2S: min_clerkage = 60.0;  break;
        default:       min_clerkage = 120.0; break;
    }
    refund->clerkage_fee = min_clerkage;

    /* Railway Refund Rules based on cancellation time */
    if (hours_before_departure >= 48.0) {
        /* More than 48 hours: Flat clerkage per passenger */
        double charge = min_clerkage;
        if (charge > total_paid_fare) charge = total_paid_fare;
        refund->cancellation_charge = charge;
        refund->refund_amount = total_paid_fare - charge;
        snprintf(refund->rule_applied, sizeof(refund->rule_applied),
                 "> 48 Hours Before Departure: Flat Clerkage (Rs. %.0f)", charge);
    } else if (hours_before_departure >= 12.0) {
        /* 48 hours to 12 hours: 25% of fare, subject to minimum clerkage */
        double charge = total_paid_fare * 0.25;
        if (charge < min_clerkage) charge = min_clerkage;
        if (charge > total_paid_fare) charge = total_paid_fare;
        refund->cancellation_charge = round(charge);
        refund->refund_amount = total_paid_fare - refund->cancellation_charge;
        snprintf(refund->rule_applied, sizeof(refund->rule_applied),
                 "12 to 48 Hours: 25%% Deduction (Min Rs. %.0f)", min_clerkage);
    } else if (hours_before_departure >= 4.0) {
        /* 12 hours to 4 hours: 50% of fare, subject to minimum clerkage */
        double charge = total_paid_fare * 0.50;
        if (charge < min_clerkage) charge = min_clerkage;
        if (charge > total_paid_fare) charge = total_paid_fare;
        refund->cancellation_charge = round(charge);
        refund->refund_amount = total_paid_fare - refund->cancellation_charge;
        snprintf(refund->rule_applied, sizeof(refund->rule_applied),
                 "4 to 12 Hours: 50%% Deduction");
    } else {
        /* Less than 4 hours / chart prepared: Zero refund */
        refund->cancellation_charge = total_paid_fare;
        refund->refund_amount = 0.0;
        snprintf(refund->rule_applied, sizeof(refund->rule_applied),
                 "< 4 Hours (Chart Prepared): No Refund Allowed");
    }

    if (total_paid_fare > 0) {
        refund->refund_percentage = (refund->refund_amount / total_paid_fare) * 100.0;
    }

    return 0;
}
