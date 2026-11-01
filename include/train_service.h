#ifndef TRAIN_SERVICE_H
#define TRAIN_SERVICE_H

#include "models.h"

int  train_service_init(void);
int  train_service_get_train(int train_no, Train *out_train);
int  train_service_list_all(Train out_trains[], int max_trains);
int  train_service_search(const char *source, const char *dest, Train out_trains[], int max_trains);
int  train_service_check_availability(int train_no, const char *travel_date, TravelClass travel_class,
                                      int *out_cnf, int *out_rac, int *out_wl, double *out_occupancy);
bool train_service_route_valid(int train_no, const char *source, const char *dest);
int  train_service_add_train(const Train *train);

#endif /* TRAIN_SERVICE_H */
