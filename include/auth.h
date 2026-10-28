/**
 * @file auth.h
 * @brief Enterprise browser-based employee authentication interface.
 */

#ifndef AUTH_H
#define AUTH_H

#include "common.h"

#define MAX_EMP_ID_LEN      32
#define MAX_EMP_PASS_LEN    64
#define MAX_EMP_NAME_LEN    64
#define MAX_EMP_DEPT_LEN    64
#define MAX_TOKEN_LEN       64
#define AUTH_DEFAULT_PORT   8585
#define AUTH_TIMEOUT_SEC    120

/**
 * @brief Employee record representing an authorized corporate identity.
 */
typedef struct {
    char employee_id[MAX_EMP_ID_LEN];
    char password[MAX_EMP_PASS_LEN];
    char name[MAX_EMP_NAME_LEN];
    char department[MAX_EMP_DEPT_LEN];
} auth_employee_t;

/**
 * @brief Authenticated session state for an active employee.
 */
typedef struct {
    char employee_id[MAX_EMP_ID_LEN];
    char employee_name[MAX_EMP_NAME_LEN];
    char department[MAX_EMP_DEPT_LEN];
    char token[MAX_TOKEN_LEN];
    bool authenticated;
    time_t login_time;
} auth_session_t;

/**
 * @brief Verifies input employee ID and password against the credential store.
 * 
 * @param emp_id Input Employee ID (e.g. EMP001).
 * @param password Input Password.
 * @param out_emp Pointer to store matched employee record (optional, can be NULL).
 * @return 0 on success, -1 if credentials are invalid.
 */
int auth_verify_credentials(const char *emp_id, const char *password, auth_employee_t *out_emp);

/**
 * @brief Initiates a browser-based login session. Spawns an HTTP loopback server,
 *        opens the default web browser to the authentication portal, and waits
 *        for employee credential confirmation.
 * 
 * @param suggested_user Suggested username or profile name (optional hint).
 * @param out_session Pointer to struct receiving authenticated session details.
 * @return 0 on successful authentication, -1 on rejection/failure/timeout.
 */
int auth_browser_login(const char *suggested_user, auth_session_t *out_session);

/**
 * @brief Saves an authenticated session to a local file.
 * 
 * @param filepath Destination file path.
 * @param session Pointer to session state to serialize.
 * @return 0 on success, -1 on I/O error.
 */
int auth_save_session(const char *filepath, const auth_session_t *session);

/**
 * @brief Loads an existing session from file.
 * 
 * @param filepath Source file path.
 * @param session Pointer to session struct to populate.
 * @return 0 on success, -1 if expired or invalid.
 */
int auth_load_session(const char *filepath, auth_session_t *session);

#endif /* AUTH_H */
