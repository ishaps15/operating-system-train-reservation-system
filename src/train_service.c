#include "train_service.h"
#include "db.h"
#include <string.h>
#include <strings.h>

int train_service_init(void) {
    return 0;
}

int train_service_get_train(int train_no, Train *out_train) {
    return db_get_train(train_no, out_train);
}

int train_service_list_all(Train out_trains[], int max_trains) {
    return db_get_all_trains(out_trains, max_trains);
}

int train_service_search(const char *source, const char *dest, Train out_trains[], int max_trains) {
    if (!source || !dest) return 0;
    return db_search_trains(source, dest, out_trains, max_trains);
}

int train_service_check_availability(int train_no, const char *travel_date, TravelClass travel_class,
                                      int *out_cnf, int *out_rac, int *out_wl, double *out_occupancy) {
    int cnf_left = 0, rac_left = 0, wl_count = 0;
    int rc = db_get_inventory_status(train_no, travel_date, travel_class,
                                     &cnf_left, &rac_left, &wl_count);
    if (rc != DB_SUCCESS) return -1;

    int total_capacity = db_get_class_capacity(train_no, travel_class);
    double occ = 0.0;
    if (total_capacity > 0) {
        int booked = total_capacity - cnf_left;
        occ = (double)booked / (double)total_capacity;
        if (occ < 0.0) occ = 0.0;
        if (occ > 1.0) occ = 1.0;
    }

    if (out_cnf) *out_cnf = cnf_left;
    if (out_rac) *out_rac = rac_left;
    if (out_wl) *out_wl = wl_count;
    if (out_occupancy) *out_occupancy = occ;

    return 0;
}

bool train_service_route_valid(int train_no, const char *source, const char *dest) {
    Train train;
    if (db_get_train(train_no, &train) != DB_SUCCESS) return false;

    if (strcasecmp(train.source, source) == 0 && strcasecmp(train.destination, dest) == 0) {
        return true;
    }

    int src_seq = -1, dst_seq = -1;
    if (strcasecmp(train.source, source) == 0) src_seq = 0;
    if (strcasecmp(train.destination, dest) == 0) dst_seq = 999;

    for (int s = 0; s < train.stop_count; s++) {
        int seq = train.stops[s].stop_sequence > 0 ? train.stops[s].stop_sequence : (s + 1);
        if (strcasecmp(train.stops[s].station_code, source) == 0) {
            src_seq = seq;
        }
        if (strcasecmp(train.stops[s].station_code, dest) == 0) {
            dst_seq = seq;
        }
    }

    return (src_seq >= 0 && dst_seq >= 0 && src_seq < dst_seq);
}

int train_service_add_train(const Train *train) {
    return db_create_train(train);
}
