#ifndef UI_H
#define UI_H

#include "models.h"
#include "auth.h"

/* UI Color & Styling Constants (ANSI) */
#define UI_COLOR_RESET   "\033[0m"
#define UI_COLOR_BOLD    "\033[1m"
#define UI_COLOR_RED     "\033[1;31m"
#define UI_COLOR_GREEN   "\033[1;32m"
#define UI_COLOR_YELLOW  "\033[1;33m"
#define UI_COLOR_BLUE    "\033[1;34m"
#define UI_COLOR_MAGENTA "\033[1;35m"
#define UI_COLOR_CYAN    "\033[1;36m"
#define UI_COLOR_WHITE   "\033[1;37m"

/* Screens and Menus */
void ui_print_banner(void);
void ui_print_header(const char *title);
void ui_run_interactive_loop(void);

/* View Renderers */
void ui_render_ticket(const Booking *booking);
void ui_render_pnr_status(const Booking *booking);
void ui_render_train_list(const Train trains[], int count);
void ui_render_reservation_chart(int train_no, const char *travel_date,
                                const Passenger passengers[], int count);
void ui_render_system_stats(void);

#endif /* UI_H */
