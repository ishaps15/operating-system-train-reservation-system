#include "ui.h"
#include "auth.h"
#include "booking_service.h"
#include "train_service.h"
#include "fare_engine.h"
#include "concurrency.h"
#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static AuthSession g_session;

void ui_print_banner(void) {
    printf(UI_COLOR_CYAN);
    printf("========================================================================================\n");
    printf("  INDIAN RAILWAYS PASSENGER RESERVATION SYSTEM (PRS)  |  EASTERN RAILWAY (ER)          \n");
    printf("  Operating System-Based Modular Train Reservation & Concurrency Management Suite      \n");
    printf("  Terminal Node: Durgapur PRS Hub (DGR) | Division: Asansol (ASN), West Bengal          \n");
    printf("========================================================================================\n");
    printf(UI_COLOR_RESET);
}

void ui_print_header(const char *title) {
    printf("\n" UI_COLOR_BOLD UI_COLOR_BLUE ">>> %s <<<" UI_COLOR_RESET "\n", title);
    printf("----------------------------------------------------------------------------------------\n");
}

void ui_render_ticket(const Booking *booking) {
    if (!booking) return;

    printf("\n");
    printf(UI_COLOR_YELLOW);
    printf("+--------------------------------------------------------------------------------------+\n");
    printf("|                              INDIAN RAILWAYS E-TICKET / BOARDING PASS                |\n");
    printf("+--------------------------------------------------------------------------------------+\n");
    printf(UI_COLOR_RESET);
    printf("| PNR: " UI_COLOR_BOLD UI_COLOR_GREEN "%-14s" UI_COLOR_RESET " Train: " UI_COLOR_BOLD "%-6d" UI_COLOR_RESET " Class: %-4s  Quota: %-10s  Status: %-12s |\n",
           booking->pnr, booking->train_no, class_code_to_str(booking->travel_class),
           quota_to_str(booking->quota), status_to_str(booking->overall_status));
    printf("| Route: %-4s ---> %-4s    Date of Journey: %-10s     Passengers: %-2d             |\n",
           booking->source, booking->destination, booking->travel_date, booking->passenger_count);
    printf("+--------------------------------------------------------------------------------------+\n");
    printf("| No | Passenger Name                  | Age | G | Coach | Seat | Berth Type  | Status |\n");
    printf("+----+---------------------------------+-----+---+-------+------+-------------+--------+\n");

    for (int i = 0; i < booking->passenger_count; i++) {
        const Passenger *p = &booking->passengers[i];
        printf("| %-2d | %-31s | %-3d | %c | %-5s | %-4d | %-11s | %-6s |\n",
               p->passenger_id, p->name, p->age, p->gender,
               p->coach_code[0] ? p->coach_code : "--",
               p->seat_number,
               berth_to_str(p->allocated_berth),
               p->status == STATUS_CNF ? "CNF" : (p->status == STATUS_RAC ? "RAC" : "WL"));
    }

    printf("+----+---------------------------------+-----+---+-------+------+-------------+--------+\n");
    printf("| Fare Breakdown: Base: Rs.%.2f | Surge: Rs.%.2f | GST: Rs.%.2f | Total: Rs." UI_COLOR_BOLD "%.2f" UI_COLOR_RESET " |\n",
           booking->base_fare, booking->surge_amount, booking->gst_amount, booking->total_fare);
    printf("| [QR-CODE: ||| | | |||| || | ||||| ]  Advisory: Carry valid Govt ID during journey.   |\n");
    printf("+--------------------------------------------------------------------------------------+\n\n");
}

void ui_render_pnr_status(const Booking *booking) {
    if (!booking) return;

    ui_print_header("CURRENT PNR ENQUIRY STATUS");
    printf("PNR Number:       " UI_COLOR_BOLD UI_COLOR_CYAN "%s" UI_COLOR_RESET "\n", booking->pnr);
    printf("Train Number:     %d\n", booking->train_no);
    printf("Journey Route:    %s to %s\n", booking->source, booking->destination);
    printf("Date of Journey:  %s\n", booking->travel_date);
    printf("Travel Class:     %s (%s)\n", class_to_str(booking->travel_class), class_code_to_str(booking->travel_class));
    printf("Overall Status:   %s%s" UI_COLOR_RESET "\n",
           booking->is_cancelled ? UI_COLOR_RED : UI_COLOR_GREEN,
           booking->is_cancelled ? "CANCELLED" : status_to_str(booking->overall_status));
    printf("Total Fare:       Rs. %.2f\n\n", booking->total_fare);

    printf("+-----+---------------------------+-----+---+-------+------+-------------+----------+\n");
    printf("| Pax | Name                      | Age | G | Coach | Seat | Berth Type  | Status   |\n");
    printf("+-----+---------------------------+-----+---+-------+------+-------------+----------+\n");

    for (int i = 0; i < booking->passenger_count; i++) {
        const Passenger *p = &booking->passengers[i];
        printf("|  %-2d | %-25s | %-3d | %c | %-5s | %-4d | %-11s | %-8s |\n",
               p->passenger_id, p->name, p->age, p->gender,
               p->coach_code[0] ? p->coach_code : "--",
               p->seat_number,
               berth_to_str(p->allocated_berth),
               status_to_str(p->status));
    }
    printf("+-----+---------------------------+-----+---+-------+------+-------------+----------+\n\n");
}

void ui_render_train_list(const Train trains[], int count) {
    ui_print_header("TRAIN TIMETABLE & DIRECTORY");
    printf("+-------+---------------------------------------+---------+---------+-------+-------+---------+\n");
    printf("| Train | Train Name                            | Source  | Dest    | Dep   | Arr   | Dist(km)|\n");
    printf("+-------+---------------------------------------+---------+---------+-------+-------+---------+\n");

    for (int i = 0; i < count; i++) {
        printf("| %-5d | %-37s | %-7s | %-7s | %-5s | %-5s | %-7d |\n",
               trains[i].train_no, trains[i].train_name,
               trains[i].source, trains[i].destination,
               trains[i].departure_time, trains[i].arrival_time,
               trains[i].total_distance_km);
    }
    printf("+-------+---------------------------------------+---------+---------+-------+-------+---------+\n\n");
}

void ui_render_reservation_chart(int train_no, const char *travel_date,
                                const Passenger passengers[], int count) {
    char header_buf[128];
    snprintf(header_buf, sizeof(header_buf), "RESERVATION CHART (TTE MANIFEST) - TRAIN %d [%s]", train_no, travel_date);
    ui_print_header(header_buf);

    if (count == 0) {
        printf("No booked passengers found for this schedule.\n\n");
        return;
    }

    printf("+----+--------------+---------------------------+-----+---+-------+------+-------------+--------+\n");
    printf("| No | PNR          | Passenger Name            | Age | G | Coach | Seat | Berth Type  | Status |\n");
    printf("+----+--------------+---------------------------+-----+---+-------+------+-------------+--------+\n");

    for (int i = 0; i < count; i++) {
        const Passenger *p = &passengers[i];
        printf("| %-2d | %-12s | %-25s | %-3d | %c | %-5s | %-4d | %-11s | %-6s |\n",
               i + 1, p->pnr, p->name, p->age, p->gender,
               p->coach_code[0] ? p->coach_code : "--",
               p->seat_number,
               berth_to_str(p->allocated_berth),
               p->status == STATUS_CNF ? "CNF" : "RAC");
    }
    printf("+----+--------------+---------------------------+-----+---+-------+------+-------------+--------+\n\n");
}

void ui_render_system_stats(void) {
    DBSystemStats stats;
    db_get_system_stats(&stats);

    ui_print_header("OPERATIONAL METRICS & SYSTEM LEDGER");
    printf("Registered System Users:        %d\n", stats.total_users);
    printf("Active Trains in Catalog:       %d\n", stats.total_trains);
    printf("Total Bookings Processed:       %d\n", stats.total_bookings);
    printf("Total Passengers Transported:   %d\n", stats.total_passengers);
    printf("Confirmed (CNF) Bookings:       " UI_COLOR_GREEN "%d" UI_COLOR_RESET "\n", stats.confirmed_bookings);
    printf("RAC Reservations:               " UI_COLOR_YELLOW "%d" UI_COLOR_RESET "\n", stats.rac_bookings);
    printf("Waiting List (WL) Entries:      " UI_COLOR_RED "%d" UI_COLOR_RESET "\n", stats.wl_bookings);
    printf("Cancelled Bookings:             %d\n", stats.cancelled_bookings);
    printf("Total Revenue Generated:        Rs. " UI_COLOR_BOLD "%.2f" UI_COLOR_RESET "\n", stats.total_revenue);
    printf("Total Refunds Processed:        Rs. %.2f\n\n", stats.total_refunded);
}

/* ============================================================================
 * Interactive Sub-Menus
 * ============================================================================ */

static void handle_search_trains(void) {
    char src[MAX_STR_XS] = "DGR";
    char dst[MAX_STR_XS] = "NDLS";

    printf("\nEnter Source Station Code [default: DGR]: ");
    char input[64];
    if (fgets(input, sizeof(input), stdin)) {
        if (input[0] != '\n') {
            sscanf(input, "%s", src);
        }
    }

    printf("Enter Destination Station Code [default: NDLS]: ");
    if (fgets(input, sizeof(input), stdin)) {
        if (input[0] != '\n') {
            sscanf(input, "%s", dst);
        }
    }

    Train trains[16];
    int count = train_service_search(src, dst, trains, 16);
    if (count == 0) {
        printf(UI_COLOR_RED "No direct trains found between %s and %s.\n" UI_COLOR_RESET, src, dst);
    } else {
        ui_render_train_list(trains, count);
    }
}

static void handle_check_availability(void) {
    int train_no = 12301;
    char date[MAX_STR_XS] = "2026-11-15";
    char class_str[MAX_STR_XS] = "3A";

    printf("\nEnter Train Number [default: 12301]: ");
    char input[64];
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') {
        train_no = atoi(input);
    }

    printf("Enter Travel Date (YYYY-MM-DD) [default: 2026-11-15]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') {
        sscanf(input, "%s", date);
    }

    printf("Enter Class (1A, 2A, 3A, CC, SL, 2S) [default: 3A]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') {
        sscanf(input, "%s", class_str);
    }

    TravelClass tc = parse_class_code(class_str);
    int cnf = 0, rac = 0, wl = 0;
    double occ = 0.0;

    if (train_service_check_availability(train_no, date, tc, &cnf, &rac, &wl, &occ) == 0) {
        printf("\n" UI_COLOR_BOLD "--- LIVE AVAILABILITY STATUS FOR TRAIN %d (%s) ---" UI_COLOR_RESET "\n", train_no, date);
        printf("Class:                  %s\n", class_to_str(tc));
        printf("Confirmed (CNF) Left:   " UI_COLOR_GREEN "%d" UI_COLOR_RESET "\n", cnf);
        printf("RAC Available:          " UI_COLOR_YELLOW "%d" UI_COLOR_RESET "\n", rac);
        printf("Waiting List Count:     " UI_COLOR_RED "%d" UI_COLOR_RESET "\n", wl);
        printf("Current Occupancy:      %.1f%%\n\n", occ * 100.0);
    } else {
        printf(UI_COLOR_RED "Failed to retrieve train availability.\n" UI_COLOR_RESET);
    }
}

static void handle_dynamic_fare_calculator(void) {
    char src[MAX_STR_XS] = "DGR";
    char dst[MAX_STR_XS] = "NDLS";
    char class_str[MAX_STR_XS] = "3A";
    int age = 30;
    char gender = 'M';
    double occ_pct = 75.0;

    printf("\n--- DYNAMIC FARE ESTIMATION ENGINE ---\n");
    printf("Source Station [default: DGR]: ");
    char input[64];
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') sscanf(input, "%s", src);

    printf("Destination Station [default: NDLS]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') sscanf(input, "%s", dst);

    printf("Travel Class (1A, 2A, 3A, CC, SL, 2S) [default: 3A]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') sscanf(input, "%s", class_str);

    printf("Passenger Age [default: 30]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') age = atoi(input);

    printf("Passenger Gender (M/F) [default: M]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') sscanf(input, " %c", &gender);

    printf("Current Train Occupancy %% (e.g. 50, 75, 90) [default: 75]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') occ_pct = atof(input);

    TravelClass tc = parse_class_code(class_str);
    FareBreakdown fare;
    fare_compute_passenger_fare(tc, src, dst, QUOTA_GENERAL, CONC_NONE, gender, age, occ_pct / 100.0, true, &fare);

    printf("\n" UI_COLOR_BOLD "--- DYNAMIC FARE COMPUTATION BREAKDOWN ---" UI_COLOR_RESET "\n");
    printf("Calculated Distance:       %.0f km\n", fare.distance_km);
    printf("Base Ticket Fare:          Rs. %.2f\n", fare.base_fare);
    printf("Superfast Surcharge:       Rs. %.2f\n", fare.superfast_charge);
    printf("Dynamic Surge (%.0f%% occ): Rs. %.2f (%.2fx multiplier)\n",
           occ_pct, fare.surge_amount, fare.surge_multiplier);
    printf("Concession Discount:       Rs. -%.2f\n", fare.concession_discount);
    printf("GST (%.0f%%):                Rs. %.2f\n", fare.gst_rate * 100.0, fare.gst_amount);
    printf("----------------------------------------\n");
    printf(UI_COLOR_GREEN UI_COLOR_BOLD "Final Calculated Fare:     Rs. %.2f\n" UI_COLOR_RESET, fare.final_fare);
    printf("----------------------------------------\n\n");
}

static void handle_book_ticket(void) {
    if (!auth_is_authenticated(&g_session)) {
        printf(UI_COLOR_RED "Please login to book tickets.\n" UI_COLOR_RESET);
        return;
    }

    BookingRequest req;
    memset(&req, 0, sizeof(BookingRequest));
    req.user_id = g_session.current_user.user_id;

    printf("\n--- NEW TICKET RESERVATION ---\n");
    printf("Train Number [e.g. 12301]: ");
    char input[64];
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') req.train_no = atoi(input);
    else req.train_no = 12301;

    printf("Source Station [default: DGR]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') sscanf(input, "%s", req.source);
    else strcpy(req.source, "DGR");

    printf("Destination Station [default: NDLS]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') sscanf(input, "%s", req.dest);
    else strcpy(req.dest, "NDLS");

    printf("Travel Date (YYYY-MM-DD) [default: 2026-11-20]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') sscanf(input, "%s", req.travel_date);
    else strcpy(req.travel_date, "2026-11-20");

    char class_str[MAX_STR_XS] = "3A";
    printf("Travel Class (1A, 2A, 3A, CC, SL, 2S) [default: 3A]: ");
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') sscanf(input, "%s", class_str);
    req.travel_class = parse_class_code(class_str);

    printf("Quota (1: General, 2: Tatkal, 3: Ladies, 4: Senior) [default: 1]: ");
    int q_choice = 1;
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') q_choice = atoi(input);
    req.quota = (QuotaType)q_choice;

    printf("Number of Passengers (1-6) [default: 1]: ");
    req.passenger_count = 1;
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') req.passenger_count = atoi(input);
    if (req.passenger_count > 6) req.passenger_count = 6;

    for (int i = 0; i < req.passenger_count; i++) {
        printf("\nPassenger #%d:\n", i + 1);
        printf("  Name: ");
        if (fgets(input, sizeof(input), stdin)) {
            input[strcspn(input, "\n")] = 0;
            strncpy(req.passengers[i].name, input, MAX_STR_MD-1);
        }
        printf("  Age: ");
        if (fgets(input, sizeof(input), stdin)) req.passengers[i].age = atoi(input);

        printf("  Gender (M/F/O): ");
        if (fgets(input, sizeof(input), stdin)) sscanf(input, " %c", &req.passengers[i].gender);

        printf("  Berth Preference (1: Lower, 2: Middle, 3: Upper, 4: Side Lower, 5: Side Upper, 6: Window) [default: 1]: ");
        int bp = 1;
        if (fgets(input, sizeof(input), stdin) && input[0] != '\n') bp = atoi(input);
        req.passengers[i].preferred_berth = (BerthType)bp;
        req.passengers[i].concession = CONC_NONE;
    }

    strcpy(req.payment_mode, "CASH_COUNTER");

    Booking booking;
    int rc = booking_service_book_ticket(&req, &booking);
    if (rc == 0) {
        printf(UI_COLOR_GREEN UI_COLOR_BOLD "\n>>> TICKET BOOKED SUCCESSFULLY! <<<" UI_COLOR_RESET "\n");
        ui_render_ticket(&booking);
    } else {
        printf(UI_COLOR_RED "\nBooking failed with error code %d (Seats full or invalid route).\n" UI_COLOR_RESET, rc);
    }
}

static void handle_pnr_enquiry(void) {
    char pnr[PNR_LENGTH];
    printf("\nEnter 10-Digit PNR Number: ");
    char input[64];
    if (fgets(input, sizeof(input), stdin)) {
        sscanf(input, "%s", pnr);
        Booking booking;
        if (booking_service_get_pnr_status(pnr, &booking) == 0) {
            ui_render_pnr_status(&booking);
        } else {
            printf(UI_COLOR_RED "PNR '%s' not found.\n" UI_COLOR_RESET, pnr);
        }
    }
}

static void handle_cancellation(void) {
    if (!auth_is_authenticated(&g_session)) {
        printf(UI_COLOR_RED "Please login to cancel tickets.\n" UI_COLOR_RESET);
        return;
    }

    char pnr[PNR_LENGTH];
    printf("\nEnter PNR to Cancel: ");
    char input[64];
    if (fgets(input, sizeof(input), stdin)) {
        sscanf(input, "%s", pnr);

        Booking booking;
        if (booking_service_get_pnr_status(pnr, &booking) != 0) {
            printf(UI_COLOR_RED "PNR not found.\n" UI_COLOR_RESET);
            return;
        }

        ui_render_pnr_status(&booking);

        printf("Confirm Full Ticket Cancellation? (y/N): ");
        if (fgets(input, sizeof(input), stdin) && (input[0] == 'y' || input[0] == 'Y')) {
            CancellationRecord rec;
            int rc = booking_service_cancel_pnr(pnr, g_session.current_user.user_id, 48.0, "Passenger Request", &rec);
            if (rc == 0) {
                printf(UI_COLOR_GREEN UI_COLOR_BOLD "\n>>> TICKET CANCELLED SUCCESSFULLY <<<" UI_COLOR_RESET "\n");
                printf("Original Paid Fare:     Rs. %.2f\n", rec.original_fare);
                printf("Cancellation Deduction: Rs. %.2f\n", rec.cancellation_penalty);
                printf("Refund Amount:          Rs. " UI_COLOR_BOLD "%.2f" UI_COLOR_RESET "\n", rec.refund_amount);
                printf(UI_COLOR_CYAN "Note: RAC and Waiting List queues have been automatically promoted.\n\n" UI_COLOR_RESET);
            } else {
                printf(UI_COLOR_RED "Cancellation failed.\n" UI_COLOR_RESET);
            }
        }
    }
}

static void handle_view_my_bookings(void) {
    if (!auth_is_authenticated(&g_session)) return;

    Booking bookings[32];
    int count = db_get_bookings_by_user(g_session.current_user.user_id, bookings, 32);

    ui_print_header("MY RESERVATION HISTORY");
    if (count == 0) {
        printf("No active or historical bookings found.\n\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        ui_render_ticket(&bookings[i]);
    }
}

static void handle_run_concurrency_simulation(void) {
    ui_print_header("MULTI-USER OS CONCURRENCY & RACE-CONDITION BENCHMARK");
    printf("Simulating concurrent booking counters (Durgapur PRS, Howrah PRS, Asansol, IRCTC)...\n");

    SimulationConfig cfg;
    cfg.num_threads = 8;
    cfg.bookings_per_thread = 5;
    cfg.train_no = 12301;
    strcpy(cfg.travel_date, "2026-11-25");
    cfg.travel_class = CLASS_3A;

    printf("Launching %d POSIX worker threads booking the SAME train/date simultaneously...\n", cfg.num_threads);

    SimulationResult res;
    concurrency_run_multiuser_simulation(&cfg, &res);

    printf("\n" UI_COLOR_BOLD UI_COLOR_GREEN "=== CONCURRENCY BENCHMARK RESULTS ===" UI_COLOR_RESET "\n");
    printf("Total Booking Requests:       %d\n", res.total_requests);
    printf("Confirmed Seats Allocated:    %d\n", res.successful_confirmed);
    printf("RAC Reservations Granted:     %d\n", res.successful_rac);
    printf("Waiting List Entries:         %d\n", res.successful_wl);
    printf("Rejected Overcapacity:        %d\n", res.rejected_overcapacity);
    printf("Double-Booking Violations:    " UI_COLOR_BOLD "%s%d" UI_COLOR_RESET "\n",
           res.race_conditions_detected == 0 ? UI_COLOR_GREEN : UI_COLOR_RED,
           res.race_conditions_detected);
    printf("Elapsed Execution Time:       %.4f seconds\n", res.elapsed_seconds);
    printf("Throughput:                   " UI_COLOR_BOLD "%.2f transactions/sec" UI_COLOR_RESET "\n\n",
           res.transactions_per_sec);
}

static void handle_login(void) {
    char username[32];
    char password[32];

    printf("\n--- USER LOGIN ---\n");
    printf("Username [e.g. admin, dgr_counter1, passenger1]: ");
    char input[64];
    if (fgets(input, sizeof(input), stdin)) sscanf(input, "%s", username);

    printf("Password [e.g. Admin@123, Counter@123, Pass@123]: ");
    if (fgets(input, sizeof(input), stdin)) sscanf(input, "%s", password);

    if (auth_login(&g_session, username, password) == 0) {
        printf(UI_COLOR_GREEN UI_COLOR_BOLD "\nLogin Successful! Welcome, %s (%s)\n\n" UI_COLOR_RESET,
               g_session.current_user.full_name, role_to_str(g_session.current_user.role));
    } else {
        printf(UI_COLOR_RED "\nInvalid username or password.\n\n" UI_COLOR_RESET);
    }
}

static void handle_register(void) {
    char username[32], password[32], name[64], phone[32], email[64];

    printf("\n--- PASSENGER REGISTRATION ---\n");
    printf("Choose Username: ");
    char input[64];
    if (fgets(input, sizeof(input), stdin)) sscanf(input, "%s", username);

    printf("Choose Password: ");
    if (fgets(input, sizeof(input), stdin)) sscanf(input, "%s", password);

    printf("Full Name: ");
    if (fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = 0;
        strcpy(name, input);
    }

    printf("Phone Number: ");
    if (fgets(input, sizeof(input), stdin)) sscanf(input, "%s", phone);

    printf("Email Address: ");
    if (fgets(input, sizeof(input), stdin)) sscanf(input, "%s", email);

    if (auth_register_passenger(username, password, name, phone, email) == 0) {
        printf(UI_COLOR_GREEN "\nRegistration successful! You can now log in.\n\n" UI_COLOR_RESET);
    } else {
        printf(UI_COLOR_RED "\nRegistration failed (Username may already exist).\n\n" UI_COLOR_RESET);
    }
}

void ui_run_interactive_loop(void) {
    auth_init_session(&g_session);
    ui_print_banner();

    while (1) {
        printf(UI_COLOR_BOLD "----------------------------- MAIN PRS MENU -----------------------------" UI_COLOR_RESET "\n");
        if (auth_is_authenticated(&g_session)) {
            printf("Active User: " UI_COLOR_GREEN "%s" UI_COLOR_RESET " | Role: " UI_COLOR_CYAN "%s" UI_COLOR_RESET " | Station: %s\n",
                   g_session.current_user.username,
                   role_to_str(g_session.current_user.role),
                   g_session.current_user.station_code);
        } else {
            printf("Status: " UI_COLOR_YELLOW "GUEST (Not logged in)" UI_COLOR_RESET "\n");
        }
        printf("1. Search Trains & Timetable\n");
        printf("2. Check Live Seat Availability\n");
        printf("3. Dynamic Fare Estimation Calculator\n");
        printf("4. Book Train Ticket\n");
        printf("5. PNR Status Enquiry\n");
        printf("6. Cancel Ticket & Dynamic Refund\n");
        printf("7. View My Booking History\n");
        printf("8. Multi-User OS Concurrency Simulation\n");
        printf("9. View System Statistics & Operational Ledger\n");

        if (!auth_is_authenticated(&g_session)) {
            printf("10. User Login\n");
            printf("11. Register New Passenger Account\n");
        } else {
            printf("10. User Logout\n");
            if (auth_has_role(&g_session, ROLE_ADMIN) || auth_has_role(&g_session, ROLE_TTE)) {
                printf("12. Generate Reservation Chart (TTE Manifest)\n");
            }
        }
        printf("0. Exit\n");
        printf("Select Option: ");

        char input[64];
        if (!fgets(input, sizeof(input), stdin)) break;
        int opt = atoi(input);

        switch (opt) {
            case 1:  handle_search_trains(); break;
            case 2:  handle_check_availability(); break;
            case 3:  handle_dynamic_fare_calculator(); break;
            case 4:  handle_book_ticket(); break;
            case 5:  handle_pnr_enquiry(); break;
            case 6:  handle_cancellation(); break;
            case 7:  handle_view_my_bookings(); break;
            case 8:  handle_run_concurrency_simulation(); break;
            case 9:  ui_render_system_stats(); break;
            case 10:
                if (auth_is_authenticated(&g_session)) {
                    auth_logout(&g_session);
                    printf("Logged out successfully.\n\n");
                } else {
                    handle_login();
                }
                break;
            case 11:
                if (!auth_is_authenticated(&g_session)) handle_register();
                break;
            case 12:
                if (auth_is_authenticated(&g_session) &&
                    (auth_has_role(&g_session, ROLE_ADMIN) || auth_has_role(&g_session, ROLE_TTE))) {
                    Passenger chart[64];
                    int count = booking_service_generate_chart(12301, "2026-11-15", chart, 64);
                    ui_render_reservation_chart(12301, "2026-11-15", chart, count);
                }
                break;
            case 0:
                printf("\nThank you for using Eastern Railway PRS. Have a safe journey!\n");
                return;
            default:
                printf("Invalid selection.\n");
                break;
        }
    }
}
