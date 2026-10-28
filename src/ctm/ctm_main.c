/**
 * @file ctm_main.c
 * @brief Command-line interface frontend for CTM (Chat Messenger CLI).
 */

#include "ctm.h"
#include "protocol.h"
#include <getopt.h>

static void print_help(const char *prog)
{
    printf(COLOR_BOLD "CTM — Chat Messenger CLI (v%s)" COLOR_RESET "\n", CTM_VERSION);
    printf("A fast, asynchronous Git-style command-line messenger for Linux.\n\n");
    printf(COLOR_BOLD "USAGE:" COLOR_RESET "\n");
    printf("  %s [--user <name>] <command> [arguments]\n\n", prog);
    printf(COLOR_BOLD "COMMANDS:" COLOR_RESET "\n");
    printf("  " COLOR_GREEN "text" COLOR_RESET " [peer] -m \"<msg>\"      Send a message to peer\n");
    printf("  " COLOR_GREEN "updates" COLOR_RESET "                     Check unread message count and preview\n");
    printf("  " COLOR_GREEN "log" COLOR_RESET " [-n <count>]             View chat history (marks unread as read)\n");
    printf("  " COLOR_GREEN "send-file" COLOR_RESET " [peer] <filepath>   Send a file to peer\n");
    printf("  " COLOR_GREEN "fetch" COLOR_RESET "                       List received files in downloads/\n");
    printf("  " COLOR_GREEN "config" COLOR_RESET "                      View or modify user and network configuration\n");
    printf("  " COLOR_GREEN "daemon" COLOR_RESET " [start|stop|status]   Manage background daemon process\n");
    printf("  " COLOR_GREEN "clear" COLOR_RESET "                      Clear local chat history\n\n");
    printf(COLOR_BOLD "GLOBAL OPTIONS:" COLOR_RESET "\n");
    printf("  -u, --user <name>          Select user profile (default: $CTM_USER or 'Alice')\n");
    printf("  -h, --help                 Display this help message\n");
    printf("  -v, --version              Display version\n\n");
    printf(COLOR_BOLD "EXAMPLES:" COLOR_RESET "\n");
    printf("  %s text Bob -m \"I'm working on the project!\"\n", prog);
    printf("  %s updates\n", prog);
    printf("  %s log -n 10\n", prog);
    printf("  %s send-file Bob storage/demo.txt\n", prog);
    printf("  %s config --peer-ip 192.168.1.25\n", prog);
}

static int cmd_config(ctm_config_t *cfg, int argc, char *argv[])
{
    if (argc == 0) {
        printf(COLOR_BOLD "CTM Configuration for '%s':" COLOR_RESET "\n", cfg->user_name);
        printf("  Profile Name     : %s\n", cfg->user_name);
        printf("  Local TCP Port   : %d\n", cfg->listen_port);
        printf("  Peer Name        : %s\n", cfg->peer_name);
        printf("  Peer IP Address  : %s\n", cfg->peer_ip);
        printf("  Peer TCP Port    : %d\n", cfg->peer_port);
        printf("  Socket Path      : %s\n", cfg->sock_path);
        printf("  History File     : %s\n", cfg->history_path);
        printf("  Downloads Dir    : %s\n", cfg->downloads_dir);
        return 0;
    }

    bool modified = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            snprintf(cfg->user_name, sizeof(cfg->user_name), "%s", argv[++i]);
            modified = true;
        } else if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc) {
            snprintf(cfg->peer_name, sizeof(cfg->peer_name), "%s", argv[++i]);
            modified = true;
        } else if (strcmp(argv[i], "--peer-ip") == 0 && i + 1 < argc) {
            snprintf(cfg->peer_ip, sizeof(cfg->peer_ip), "%s", argv[++i]);
            modified = true;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg->listen_port = atoi(argv[++i]);
            modified = true;
        } else if (strcmp(argv[i], "--peer-port") == 0 && i + 1 < argc) {
            cfg->peer_port = atoi(argv[++i]);
            modified = true;
        }
    }

    if (modified) {
        ctm_save_config(cfg);
        printf(COLOR_GREEN "[ctm]" COLOR_RESET " Configuration updated successfully.\n");
        if (ctm_is_daemon_running(cfg)) {
            printf("[ctm] Restarting daemon to apply new configuration...\n");
            ctm_stop_daemon(cfg);
            ctm_ensure_daemon(cfg);
        }
    } else {
        printf("Usage: ctm config [--name <name>] [--peer <peer>] [--peer-ip <ip>] [--port <port>] [--peer-port <port>]\n");
    }

    return 0;
}

static int cmd_text(ctm_config_t *cfg, int argc, char *argv[])
{
    const char *target_peer = cfg->peer_name;
    const char *message = NULL;

    for (int i = 0; i < argc; i++) {
        if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--message") == 0) && i + 1 < argc) {
            message = argv[++i];
        } else if (argv[i][0] != '-') {
            target_peer = argv[i];
        }
    }

    if (!message || strlen(message) == 0) {
        fprintf(stderr, "Error: Message cannot be empty. Use -m \"<message>\".\n");
        return 1;
    }

    char req[CTM_LINE_MAX];
    snprintf(req, sizeof(req), "TEXT %s %s\n", target_peer, message);

    char reply[CTM_LINE_MAX] = {0};
    if (ctm_ipc_request(cfg, req, reply, sizeof(reply)) == 0) {
        printf("%s", reply);
    } else {
        fprintf(stderr, COLOR_RED "[ctm ERROR]" COLOR_RESET " Failed to contact daemon.\n");
        return 1;
    }
    return 0;
}

static int cmd_updates(ctm_config_t *cfg)
{
    char reply[CTM_LINE_MAX] = {0};
    if (ctm_ipc_request(cfg, "STATUS\n", reply, sizeof(reply)) == 0) {
        printf("%s", reply);
    } else {
        /* Fallback: read directly from history if daemon offline */
        char summary[CTM_MAX_MSG_LEN] = {0};
        int unread = ctm_get_unread_count(cfg, summary, sizeof(summary));
        if (unread > 0) {
            printf(COLOR_GREEN "[ctm]" COLOR_RESET " %d unread message%s from %s\n" COLOR_BOLD "Last:" COLOR_RESET " %s\n",
                   unread, (unread > 1 ? "s" : ""), cfg->peer_name, summary);
        } else {
            printf(COLOR_GREEN "[ctm]" COLOR_RESET " No unread messages.\n");
        }
    }
    return 0;
}

static int cmd_log(ctm_config_t *cfg, int argc, char *argv[])
{
    int count = 20;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
        }
    }

    char req[64];
    snprintf(req, sizeof(req), "LOG %d\n", count);

    char reply[8192] = {0};
    if (ctm_ipc_request(cfg, req, reply, sizeof(reply)) == 0) {
        printf("%s", reply);
    } else {
        /* Fallback direct read */
        char direct_buf[8192];
        ctm_history_read(cfg, count, true, direct_buf, sizeof(direct_buf));
        printf("%s", direct_buf);
    }
    return 0;
}

static int cmd_send_file(ctm_config_t *cfg, int argc, char *argv[])
{
    const char *target_peer = cfg->peer_name;
    const char *filepath = NULL;

    if (argc == 1) {
        filepath = argv[0];
    } else if (argc >= 2) {
        target_peer = argv[0];
        filepath = argv[1];
    } else {
        fprintf(stderr, "Usage: ctm send-file [peer] <filepath>\n");
        return 1;
    }

    char *abs_path = realpath(filepath, NULL);
    if (!abs_path) {
        fprintf(stderr, COLOR_RED "[ctm ERROR]" COLOR_RESET " File '%s' not found.\n", filepath);
        return 1;
    }

    char req[CTM_LINE_MAX];
    snprintf(req, sizeof(req), "FILE %s %s\n", target_peer, abs_path);
    free(abs_path);

    char reply[CTM_LINE_MAX] = {0};
    if (ctm_ipc_request(cfg, req, reply, sizeof(reply)) == 0) {
        printf("%s", reply);
    } else {
        fprintf(stderr, COLOR_RED "[ctm ERROR]" COLOR_RESET " Failed to initiate file transfer.\n");
        return 1;
    }
    return 0;
}

static int cmd_fetch(ctm_config_t *cfg)
{
    DIR *d = opendir(cfg->downloads_dir);
    if (!d) {
        printf(COLOR_YELLOW "[ctm]" COLOR_RESET " Downloads directory '%s' does not exist.\n", cfg->downloads_dir);
        return 0;
    }

    struct dirent *dir;
    int count = 0;
    printf(COLOR_BOLD "Received Files in '%s/':" COLOR_RESET "\n", cfg->downloads_dir);
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_name[0] == '.') continue;
        char fullpath[MAX_PATH_LEN];
        snprintf(fullpath, sizeof(fullpath), "%.255s/%.255s", cfg->downloads_dir, dir->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0 && S_ISREG(st.st_mode)) {
            printf("  • %-30s (%ld bytes)\n", dir->d_name, (long)st.st_size);
            count++;
        }
    }
    closedir(d);

    if (count == 0) {
        printf("  (No files received yet)\n");
    }
    return 0;
}

static int cmd_daemon(ctm_config_t *cfg, int argc, char *argv[])
{
    const char *action = (argc > 0) ? argv[0] : "status";

    if (strcmp(action, "start") == 0) {
        if (ctm_is_daemon_running(cfg)) {
            printf("[ctm] Daemon is already running for user '%s'.\n", cfg->user_name);
        } else {
            ctm_ensure_daemon(cfg);
            printf("[ctm] Daemon started successfully.\n");
        }
    } else if (strcmp(action, "stop") == 0) {
        ctm_stop_daemon(cfg);
    } else if (strcmp(action, "status") == 0) {
        if (ctm_is_daemon_running(cfg)) {
            printf(COLOR_GREEN "[ctm]" COLOR_RESET " Daemon is RUNNING for user '%s'.\n", cfg->user_name);
            FILE *fp = fopen(cfg->pid_path, "r");
            if (fp) {
                char pid_str[32] = {0};
                if (fgets(pid_str, sizeof(pid_str), fp)) {
                    proto_strip_newline(pid_str);
                    printf("  PID: %s | Socket: %s\n", pid_str, cfg->sock_path);
                }
                fclose(fp);
            }
        } else {
            printf(COLOR_YELLOW "[ctm]" COLOR_RESET " Daemon is STOPPED for user '%s'.\n", cfg->user_name);
        }
    } else {
        printf("Usage: ctm daemon [start|stop|status]\n");
    }
    return 0;
}

static int cmd_clear(ctm_config_t *cfg)
{
    char reply[128] = {0};
    if (ctm_ipc_request(cfg, "CLEAR\n", reply, sizeof(reply)) == 0) {
        printf("%s", reply);
    } else {
        ctm_history_clear(cfg);
        printf(COLOR_GREEN "[ctm]" COLOR_RESET " Chat history cleared.\n");
    }
    return 0;
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    const char *user_override = NULL;
    int opt_idx = 1;

    /* Parse global flags before subcommands */
    while (opt_idx < argc) {
        if (strcmp(argv[opt_idx], "-u") == 0 || strcmp(argv[opt_idx], "--user") == 0) {
            if (opt_idx + 1 < argc) {
                user_override = argv[++opt_idx];
                opt_idx++;
            } else {
                fprintf(stderr, "Option %s requires an argument.\n", argv[opt_idx]);
                return 1;
            }
        } else if (strcmp(argv[opt_idx], "-h") == 0 || strcmp(argv[opt_idx], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[opt_idx], "-v") == 0 || strcmp(argv[opt_idx], "--version") == 0) {
            printf("ctm version %s\n", CTM_VERSION);
            return 0;
        } else if (argv[opt_idx][0] == '-') {
            fprintf(stderr, "Unknown global option: %s\n", argv[opt_idx]);
            print_help(argv[0]);
            return 1;
        } else {
            /* First positional argument: the subcommand */
            break;
        }
    }

    ctm_config_t cfg;
    ctm_init_config(&cfg, user_override);

    if (opt_idx >= argc) {
        print_help(argv[0]);
        return 0;
    }

    const char *command = argv[opt_idx++];
    int sub_argc = argc - opt_idx;
    char **sub_argv = &argv[opt_idx];

    if (strcmp(command, "text") == 0 || strcmp(command, "send") == 0) {
        return cmd_text(&cfg, sub_argc, sub_argv);
    } else if (strcmp(command, "updates") == 0 || strcmp(command, "status") == 0) {
        return cmd_updates(&cfg);
    } else if (strcmp(command, "log") == 0 || strcmp(command, "history") == 0 || strcmp(command, "read") == 0) {
        return cmd_log(&cfg, sub_argc, sub_argv);
    } else if (strcmp(command, "send-file") == 0 || strcmp(command, "file") == 0 || strcmp(command, "share") == 0) {
        return cmd_send_file(&cfg, sub_argc, sub_argv);
    } else if (strcmp(command, "fetch") == 0) {
        return cmd_fetch(&cfg);
    } else if (strcmp(command, "config") == 0) {
        return cmd_config(&cfg, sub_argc, sub_argv);
    } else if (strcmp(command, "daemon") == 0) {
        return cmd_daemon(&cfg, sub_argc, sub_argv);
    } else if (strcmp(command, "clear") == 0) {
        return cmd_clear(&cfg);
    } else {
        fprintf(stderr, "Unknown command '%s'. Run '%s --help' for usage.\n", command, argv[0]);
        return 1;
    }
}
