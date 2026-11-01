#ifndef AUTH_H
#define AUTH_H

#include "models.h"

typedef struct {
    bool     is_logged_in;
    User     current_user;
    char     session_token[MAX_STR_SM];
    time_t   login_time;
    char     terminal_id[MAX_STR_SM]; /* e.g. "DGR_TERM_01" */
} AuthSession;

/* Session Management */
void auth_init_session(AuthSession *session);
bool auth_is_authenticated(const AuthSession *session);
const User* auth_get_current_user(const AuthSession *session);
bool auth_has_role(const AuthSession *session, UserRole required_role);

/* Authentication Operations */
int  auth_login(AuthSession *session, const char *username, const char *password);
void auth_logout(AuthSession *session);
int  auth_register_passenger(const char *username, const char *password, 
                             const char *full_name, const char *phone, const char *email);
int  auth_create_agent(const char *username, const char *password, 
                       const char *full_name, const char *phone, 
                       const char *email, const char *station_code);
int  auth_change_password(int user_id, const char *old_password, const char *new_password);

#endif /* AUTH_H */
