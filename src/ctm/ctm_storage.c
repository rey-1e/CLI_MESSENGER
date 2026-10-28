/**
 * @file ctm_storage.c
 * @brief Persistent history storage, file locking, and configuration management.
 */

#include "ctm.h"
#include "protocol.h"
#include <pwd.h>
#include <ctype.h>

static void ctm_mkdir_p(const char *dir)
{
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len == 0) return;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0775);
            *p = '/';
        }
    }
    mkdir(tmp, 0775);
}

int ctm_resolve_peer_port(const char *peer_name)
{
    if (!peer_name) return CTM_DEFAULT_PORT;
    if (strcasecmp(peer_name, "Alice") == 0) return 8088;
    if (strcasecmp(peer_name, "Bob") == 0) return 8089;
    if (strcasecmp(peer_name, "Charlie") == 0) return 8090;
    if (strcasecmp(peer_name, "Rey") == 0) return 8091;
    if (strcasecmp(peer_name, "Admin") == 0) return 8092;

    /* Deterministic hash for any dynamic employee username */
    unsigned int hash = 5381;
    const unsigned char *p = (const unsigned char *)peer_name;
    while (*p) {
        hash = ((hash << 5) + hash) + tolower(*p++);
    }
    return 8100 + (hash % 100);
}

int ctm_get_active_user(char *out_username, size_t max_len)
{
    if (!out_username || max_len == 0) return -1;
    out_username[0] = '\0';

    const char *homedir = getenv("HOME");
    if (!homedir) {
        struct passwd *pw = getpwuid(getuid());
        homedir = pw ? pw->pw_dir : "/tmp";
    }

    char active_path[MAX_PATH_LEN];
    snprintf(active_path, sizeof(active_path), "%s/.ctm/current_user", homedir);

    FILE *fp = fopen(active_path, "r");
    if (!fp) return -1;

    if (fgets(out_username, max_len, fp)) {
        proto_strip_newline(out_username);
        fclose(fp);
        return (strlen(out_username) > 0) ? 0 : -1;
    }
    fclose(fp);
    return -1;
}

int ctm_set_active_user(const char *username)
{
    if (!username || strlen(username) == 0) return -1;

    const char *homedir = getenv("HOME");
    if (!homedir) {
        struct passwd *pw = getpwuid(getuid());
        homedir = pw ? pw->pw_dir : "/tmp";
    }

    char ctm_root[MAX_PATH_LEN];
    snprintf(ctm_root, sizeof(ctm_root), "%s/.ctm", homedir);
    ctm_mkdir_p(ctm_root);

    char active_path[MAX_PATH_LEN];
    snprintf(active_path, sizeof(active_path), "%s/.ctm/current_user", homedir);

    FILE *fp = fopen(active_path, "w");
    if (!fp) return -1;

    fprintf(fp, "%s\n", username);
    fclose(fp);
    return 0;
}

int ctm_clear_active_user(void)
{
    const char *homedir = getenv("HOME");
    if (!homedir) {
        struct passwd *pw = getpwuid(getuid());
        homedir = pw ? pw->pw_dir : "/tmp";
    }

    char active_path[MAX_PATH_LEN];
    snprintf(active_path, sizeof(active_path), "%s/.ctm/current_user", homedir);
    unlink(active_path);
    return 0;
}

void ctm_init_config(ctm_config_t *cfg, const char *user_override)
{
    memset(cfg, 0, sizeof(*cfg));

    /* Determine user name: Flag > Env > Active Logged-in User > Default */
    if (user_override && strlen(user_override) > 0) {
        snprintf(cfg->user_name, sizeof(cfg->user_name), "%s", user_override);
    } else {
        const char *env_user = getenv("CTM_USER");
        if (env_user && strlen(env_user) > 0) {
            snprintf(cfg->user_name, sizeof(cfg->user_name), "%s", env_user);
        } else {
            char active_user[CTM_MAX_NAME_LEN] = {0};
            if (ctm_get_active_user(active_user, sizeof(active_user)) == 0) {
                snprintf(cfg->user_name, sizeof(cfg->user_name), "%s", active_user);
            } else {
                snprintf(cfg->user_name, sizeof(cfg->user_name), "Alice");
            }
        }
    }

    /* Assign default ports and peer */
    cfg->listen_port = ctm_resolve_peer_port(cfg->user_name);

    if (strcasecmp(cfg->user_name, "Bob") == 0) {
        snprintf(cfg->peer_name, sizeof(cfg->peer_name), "Alice");
    } else {
        snprintf(cfg->peer_name, sizeof(cfg->peer_name), "Bob");
    }
    cfg->peer_port = ctm_resolve_peer_port(cfg->peer_name);
    snprintf(cfg->peer_ip, sizeof(cfg->peer_ip), "127.0.0.1");

    /* Base directory: ~/.ctm/<user_name> */
    const char *homedir = getenv("HOME");
    if (!homedir) {
        struct passwd *pw = getpwuid(getuid());
        homedir = pw ? pw->pw_dir : "/tmp";
    }

    snprintf(cfg->base_dir, sizeof(cfg->base_dir), "%s/.ctm/%s", homedir, cfg->user_name);
    ctm_mkdir_p(cfg->base_dir);

    snprintf(cfg->sock_path, sizeof(cfg->sock_path), "/tmp/ctm_%s.sock", cfg->user_name);
    snprintf(cfg->pid_path, sizeof(cfg->pid_path), "%.480s/ctmd.pid", cfg->base_dir);
    snprintf(cfg->config_path, sizeof(cfg->config_path), "%.480s/config", cfg->base_dir);
    snprintf(cfg->history_path, sizeof(cfg->history_path), "%.480s/history.log", cfg->base_dir);
    snprintf(cfg->outbox_path, sizeof(cfg->outbox_path), "%.480s/outbox.log", cfg->base_dir);
    snprintf(cfg->downloads_dir, sizeof(cfg->downloads_dir), "%.480s/downloads", cfg->base_dir);
    ctm_mkdir_p(cfg->downloads_dir);

    /* Try loading saved config */
    ctm_load_config(cfg);
}

int ctm_load_config(ctm_config_t *cfg)
{
    FILE *fp = fopen(cfg->config_path, "r");
    if (!fp) return -1;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        proto_strip_newline(line);
        if (line[0] == '#' || strlen(line) == 0) continue;

        char key[64] = {0};
        char val[192] = {0};
        if (sscanf(line, "%63[^=]=%191[^\n]", key, val) == 2) {
            if (strcmp(key, "user_name") == 0) {
                snprintf(cfg->user_name, sizeof(cfg->user_name), "%s", val);
            } else if (strcmp(key, "listen_port") == 0) {
                cfg->listen_port = atoi(val);
            } else if (strcmp(key, "peer_name") == 0) {
                snprintf(cfg->peer_name, sizeof(cfg->peer_name), "%s", val);
            } else if (strcmp(key, "peer_ip") == 0) {
                snprintf(cfg->peer_ip, sizeof(cfg->peer_ip), "%s", val);
            } else if (strcmp(key, "peer_port") == 0) {
                cfg->peer_port = atoi(val);
            }
        }
    }

    fclose(fp);
    return 0;
}

int ctm_save_config(const ctm_config_t *cfg)
{
    FILE *fp = fopen(cfg->config_path, "w");
    if (!fp) {
        LOG_ERROR("Could not open config file '%s' for writing", cfg->config_path);
        return -1;
    }

    fprintf(fp, "# CTM Configuration for %s\n", cfg->user_name);
    fprintf(fp, "user_name=%s\n", cfg->user_name);
    fprintf(fp, "listen_port=%d\n", cfg->listen_port);
    fprintf(fp, "peer_name=%s\n", cfg->peer_name);
    fprintf(fp, "peer_ip=%s\n", cfg->peer_ip);
    fprintf(fp, "peer_port=%d\n", cfg->peer_port);

    fclose(fp);
    return 0;
}

int ctm_history_append(const ctm_config_t *cfg, const ctm_message_t *msg)
{
    int fd = open(cfg->history_path, O_CREAT | O_WRONLY | O_APPEND, 0664);
    if (fd < 0) {
        LOG_ERROR("Failed to open history log '%s'", cfg->history_path);
        return -1;
    }

    /* Acquire POSIX advisory kernel lock */
    if (flock(fd, LOCK_EX) < 0) {
        LOG_ERROR("Failed to lock history log");
        close(fd);
        return -1;
    }

    char record[CTM_LINE_MAX];
    int len = snprintf(record, sizeof(record),
                       "%ld|%d|%s|%s|%d|%s|%ld\n",
                       (long)(msg->timestamp > 0 ? msg->timestamp : time(NULL)),
                       msg->is_read ? 1 : 0,
                       msg->sender,
                       msg->receiver,
                       (int)msg->type,
                       msg->content,
                       msg->file_size);

    ssize_t w = write(fd, record, (size_t)len);
    (void)w;
    fsync(fd);

    flock(fd, LOCK_UN);
    close(fd);
    return 0;
}

int ctm_history_read(const ctm_config_t *cfg, int count, bool mark_read, char *output, size_t max_out)
{
    if (!output || max_out == 0) return 0;
    output[0] = '\0';

    int fd = open(cfg->history_path, mark_read ? O_RDWR : O_RDONLY, 0664);
    if (fd < 0) {
        snprintf(output, max_out, "No conversation history found.\n");
        return 0;
    }

    if (flock(fd, LOCK_EX) < 0) {
        close(fd);
        return -1;
    }

    FILE *fp = fdopen(dup(fd), mark_read ? "r+" : "r");
    if (!fp) {
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    /* Read all messages into memory */
    typedef struct {
        long ts;
        int is_read;
        char sender[CTM_MAX_NAME_LEN];
        char receiver[CTM_MAX_NAME_LEN];
        int type;
        char content[CTM_MAX_MSG_LEN];
        long file_size;
    } record_t;

    record_t records[500];
    int total_records = 0;
    char line[CTM_LINE_MAX];

    while (fgets(line, sizeof(line), fp) && total_records < 500) {
        proto_strip_newline(line);
        if (strlen(line) == 0) continue;

        record_t *r = &records[total_records];
        char *token = strtok(line, "|");
        if (!token) continue;
        r->ts = atol(token);

        token = strtok(NULL, "|");
        if (!token) continue;
        r->is_read = atoi(token);

        token = strtok(NULL, "|");
        if (!token) continue;
        snprintf(r->sender, sizeof(r->sender), "%s", token);

        token = strtok(NULL, "|");
        if (!token) continue;
        snprintf(r->receiver, sizeof(r->receiver), "%s", token);

        token = strtok(NULL, "|");
        if (!token) continue;
        r->type = atoi(token);

        token = strtok(NULL, "|");
        if (!token) continue;
        snprintf(r->content, sizeof(r->content), "%s", token);

        token = strtok(NULL, "|");
        r->file_size = token ? atol(token) : 0;

        total_records++;
    }

    /* Determine starting index */
    int start_idx = 0;
    if (count > 0 && total_records > count) {
        start_idx = total_records - count;
    }

    /* Format history output */
    size_t out_len = 0;
    out_len += snprintf(output + out_len, max_out - out_len,
                        COLOR_BOLD "--- Conversation History (%s & %s) ---" COLOR_RESET "\n",
                        cfg->user_name, cfg->peer_name);

    for (int i = start_idx; i < total_records; i++) {
        record_t *r = &records[i];
        struct tm tm_info;
        localtime_r(&r->ts, &tm_info);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%H:%M", &tm_info);

        const char *user_color = (strcmp(r->sender, cfg->user_name) == 0) ? COLOR_CYAN : COLOR_GREEN;

        if (r->type == CTM_MSG_FILE) {
            out_len += snprintf(output + out_len, max_out - out_len,
                                "[%s] %s%s" COLOR_RESET " : " COLOR_YELLOW "[ATTACHMENT] %s (%ld bytes)" COLOR_RESET "\n",
                                time_str, user_color, r->sender, r->content, r->file_size);
        } else {
            out_len += snprintf(output + out_len, max_out - out_len,
                                "[%s] %s%s" COLOR_RESET " : %s\n",
                                time_str, user_color, r->sender, r->content);
        }

        if (mark_read && strcmp(r->receiver, cfg->user_name) == 0) {
            r->is_read = 1;
        }

        if (out_len >= max_out - 128) break;
    }

    if (total_records == 0) {
        out_len += snprintf(output + out_len, max_out - out_len, "No messages yet.\n");
    }

    /* Rewrite history file if mark_read was requested */
    if (mark_read && total_records > 0) {
        rewind(fp);
        if (ftruncate(fileno(fp), 0) < 0) {
            /* Non-fatal */
        }
        for (int i = 0; i < total_records; i++) {
            record_t *r = &records[i];
            fprintf(fp, "%ld|%d|%s|%s|%d|%s|%ld\n",
                    r->ts, r->is_read, r->sender, r->receiver, r->type, r->content, r->file_size);
        }
        fflush(fp);
    }

    fclose(fp);
    flock(fd, LOCK_UN);
    close(fd);
    return total_records;
}

int ctm_get_unread_count(const ctm_config_t *cfg, char *last_summary, size_t sum_len)
{
    if (last_summary && sum_len > 0) last_summary[0] = '\0';

    int fd = open(cfg->history_path, O_RDONLY, 0664);
    if (fd < 0) return 0;

    if (flock(fd, LOCK_SH) < 0) {
        close(fd);
        return 0;
    }

    FILE *fp = fdopen(dup(fd), "r");
    if (!fp) {
        flock(fd, LOCK_UN);
        close(fd);
        return 0;
    }

    int unread = 0;
    char line[CTM_LINE_MAX];
    time_t last_time = 0;

    while (fgets(line, sizeof(line), fp)) {
        proto_strip_newline(line);
        if (strlen(line) == 0) continue;

        char *token = strtok(line, "|");
        if (!token) continue;
        long ts = atol(token);

        token = strtok(NULL, "|");
        if (!token) continue;
        int is_read = atoi(token);

        token = strtok(NULL, "|");
        if (!token) continue;
        char sender[CTM_MAX_NAME_LEN];
        snprintf(sender, sizeof(sender), "%s", token);

        token = strtok(NULL, "|");
        if (!token) continue;
        char receiver[CTM_MAX_NAME_LEN];
        snprintf(receiver, sizeof(receiver), "%s", token);

        token = strtok(NULL, "|");
        if (!token) continue;
        int type = atoi(token);

        token = strtok(NULL, "|");
        if (!token) continue;
        char content[CTM_MAX_MSG_LEN];
        snprintf(content, sizeof(content), "%s", token);

        /* Only count incoming unread messages */
        if (strcmp(receiver, cfg->user_name) == 0 && is_read == 0) {
            unread++;
            last_time = (time_t)ts;
            if (last_summary && sum_len > 0) {
                if (type == CTM_MSG_FILE) {
                    snprintf(last_summary, sum_len, "[File Attachment] %s", content);
                } else {
                    snprintf(last_summary, sum_len, "\"%s\"", content);
                }
            }
        }
    }

    fclose(fp);
    flock(fd, LOCK_UN);
    close(fd);
    (void)last_time;
    return unread;
}

int ctm_history_clear(const ctm_config_t *cfg)
{
    int fd = open(cfg->history_path, O_WRONLY | O_TRUNC | O_CREAT, 0664);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}
