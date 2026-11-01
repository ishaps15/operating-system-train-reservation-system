#include "auth.h"
#include "crypto_sha256.h"
#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void auth_init_session(AuthSession *session) {
    if (!session) return;
    memset(session, 0, sizeof(AuthSession));
    session->is_logged_in = false;
    strncpy(session->terminal_id, "LOCAL_TERM", sizeof(session->terminal_id)-1);
}

bool auth_is_authenticated(const AuthSession *session) {
    return session && session->is_logged_in;
}

const User* auth_get_current_user(const AuthSession *session) {
    if (!session || !session->is_logged_in) return NULL;
    return &session->current_user;
}

bool auth_has_role(const AuthSession *session, UserRole required_role) {
    if (!session || !session->is_logged_in) return false;
    if (session->current_user.role == ROLE_ADMIN) return true; /* Admin has all privileges */
    return session->current_user.role == required_role;
}

int auth_login(AuthSession *session, const char *username, const char *password) {
    if (!session || !username || !password) return -1;

    User user;
    if (db_find_user_by_username(username, &user) != DB_SUCCESS) {
        db_write_audit_log(0, "LOGIN_FAILED", NULL, session->terminal_id, "User not found");
        return -1;
    }

    /* Compute hash using user's stored salt */
    char computed_hash[HASH_HEX_LEN];
    hash_password_with_salt(password, user.salt, computed_hash);

    if (strcmp(computed_hash, user.password_hash) != 0) {
        db_write_audit_log(user.user_id, "LOGIN_FAILED", NULL, session->terminal_id, "Invalid credentials");
        return -1;
    }

    session->is_logged_in = true;
    session->current_user = user;
    session->login_time = time(NULL);

    /* Generate a random session token */
    char token_source[64];
    char token_hex[65];
    snprintf(token_source, sizeof(token_source), "%s_%ld_%d", username, session->login_time, rand());
    sha256_hash_string(token_source, token_hex);
    strncpy(session->session_token, token_hex, MAX_STR_SM - 1);
    session->session_token[MAX_STR_SM - 1] = '\0';

    char details[128];
    snprintf(details, sizeof(details), "User %s logged in as %s",
             user.username, role_to_str(user.role));
    db_write_audit_log(user.user_id, "LOGIN_SUCCESS", NULL, session->terminal_id, details);

    return 0;
}

void auth_logout(AuthSession *session) {
    if (!session || !session->is_logged_in) return;

    db_write_audit_log(session->current_user.user_id, "LOGOUT", NULL,
                       session->terminal_id, "User logged out");

    session->is_logged_in = false;
    memset(&session->current_user, 0, sizeof(User));
    memset(session->session_token, 0, sizeof(session->session_token));
}

int auth_register_passenger(const char *username, const char *password, 
                             const char *full_name, const char *phone, const char *email) {
    if (!username || !password || !full_name) return -1;

    User new_user;
    memset(&new_user, 0, sizeof(User));
    strncpy(new_user.username, username, sizeof(new_user.username)-1);
    strncpy(new_user.full_name, full_name, sizeof(new_user.full_name)-1);
    strncpy(new_user.phone, phone ? phone : "", sizeof(new_user.phone)-1);
    strncpy(new_user.email, email ? email : "", sizeof(new_user.email)-1);
    strncpy(new_user.station_code, "DGR", sizeof(new_user.station_code)-1);
    new_user.role = ROLE_PASSENGER;

    generate_random_salt(new_user.salt);
    hash_password_with_salt(password, new_user.salt, new_user.password_hash);

    return db_create_user(&new_user);
}

int auth_create_agent(const char *username, const char *password, 
                       const char *full_name, const char *phone, 
                       const char *email, const char *station_code) {
    if (!username || !password || !full_name) return -1;

    User new_user;
    memset(&new_user, 0, sizeof(User));
    strncpy(new_user.username, username, sizeof(new_user.username)-1);
    strncpy(new_user.full_name, full_name, sizeof(new_user.full_name)-1);
    strncpy(new_user.phone, phone ? phone : "", sizeof(new_user.phone)-1);
    strncpy(new_user.email, email ? email : "", sizeof(new_user.email)-1);
    strncpy(new_user.station_code, station_code ? station_code : "DGR", sizeof(new_user.station_code)-1);
    new_user.role = ROLE_AGENT;

    generate_random_salt(new_user.salt);
    hash_password_with_salt(password, new_user.salt, new_user.password_hash);

    return db_create_user(&new_user);
}

int auth_change_password(int user_id, const char *old_password, const char *new_password) {
    User user;
    if (db_find_user_by_id(user_id, &user) != DB_SUCCESS) return -1;

    char old_hash[HASH_HEX_LEN];
    hash_password_with_salt(old_password, user.salt, old_hash);
    if (strcmp(old_hash, user.password_hash) != 0) return -1;

    char new_salt[SALT_HEX_LEN];
    char new_hash[HASH_HEX_LEN];
    generate_random_salt(new_salt);
    hash_password_with_salt(new_password, new_salt, new_hash);

    return db_update_user_password(user_id, new_hash, new_salt);
}
