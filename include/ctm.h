/**
 * @file ctm.h
 * @brief Core definitions, data structures, and APIs for CTM (Chat Messenger CLI).
 */

#ifndef CTM_H
#define CTM_H

#include "common.h"
#include <sys/un.h>
#include <sys/file.h>
#include <dirent.h>
#include <time.h>

#define CTM_VERSION         "1.0.0"
#define CTM_DEFAULT_PORT    8088
#define CTM_DEFAULT_PEER_PORT 8088
#define CTM_MAX_MSG_LEN     2048
#define CTM_MAX_NAME_LEN    64
#define CTM_LINE_MAX        4096

typedef enum {
    CTM_MSG_TEXT = 1,
    CTM_MSG_FILE = 2
} ctm_msg_type_t;

typedef struct {
    time_t timestamp;
    bool is_read;
    char sender[CTM_MAX_NAME_LEN];
    char receiver[CTM_MAX_NAME_LEN];
    ctm_msg_type_t type;
    char content[CTM_MAX_MSG_LEN];
    long file_size;
} ctm_message_t;

typedef struct {
    char user_name[CTM_MAX_NAME_LEN];
    int listen_port;
    char peer_name[CTM_MAX_NAME_LEN];
    char peer_ip[INET_ADDRSTRLEN];
    int peer_port;

    char base_dir[MAX_PATH_LEN];
    char sock_path[MAX_PATH_LEN];
    char pid_path[MAX_PATH_LEN];
    char config_path[MAX_PATH_LEN];
    char history_path[MAX_PATH_LEN];
    char downloads_dir[MAX_PATH_LEN];
    char outbox_path[MAX_PATH_LEN];
} ctm_config_t;

/* ========================================================================= */
/* Storage & Configuration API (ctm_storage.c)                              */
/* ========================================================================= */

/**
 * @brief Initializes paths and default settings for a given user profile.
 */
void ctm_init_config(ctm_config_t *cfg, const char *user_override);

/**
 * @brief Loads user configuration from ~/.ctm/<user>/config.
 */
int ctm_load_config(ctm_config_t *cfg);

/**
 * @brief Saves user configuration to ~/.ctm/<user>/config.
 */
int ctm_save_config(const ctm_config_t *cfg);

/**
 * @brief Appends a message entry to history.log using flock() for thread/process safety.
 */
int ctm_history_append(const ctm_config_t *cfg, const ctm_message_t *msg);

/**
 * @brief Reads history entries, formats them, and optionally marks unread entries as read.
 */
int ctm_history_read(const ctm_config_t *cfg, int count, bool mark_read, char *output, size_t max_out);

/**
 * @brief Counts unread incoming messages and returns the latest unread message text.
 */
int ctm_get_unread_count(const ctm_config_t *cfg, char *last_summary, size_t sum_len);

/**
 * @brief Clears or archives chat history.
 */
int ctm_history_clear(const ctm_config_t *cfg);

/* ========================================================================= */
/* IPC & Daemon Management API (ctm_ipc.c)                                   */
/* ========================================================================= */

/**
 * @brief Checks if the background daemon is alive via Unix domain socket ping.
 */
bool ctm_is_daemon_running(const ctm_config_t *cfg);

/**
 * @brief Ensures daemon is running; automatically forks and starts it if not.
 */
int ctm_ensure_daemon(const ctm_config_t *cfg);

/**
 * @brief Sends an IPC command string to the daemon and captures the response.
 */
int ctm_ipc_request(const ctm_config_t *cfg, const char *cmd, char *response, size_t resp_max);

/**
 * @brief Stops running daemon via IPC command.
 */
int ctm_stop_daemon(const ctm_config_t *cfg);

/* ========================================================================= */
/* Daemon Core API (ctm_daemon.c)                                            */
/* ========================================================================= */

/**
 * @brief Main execution function for the background daemon.
 */
int ctm_daemon_run(ctm_config_t *cfg);

#endif /* CTM_H */
