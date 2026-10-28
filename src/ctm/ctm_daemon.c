/**
 * @file ctm_daemon.c
 * @brief Background daemon implementation managing IPC and peer networking.
 */

#include "ctm.h"
#include "network.h"
#include "protocol.h"

static volatile bool g_daemon_running = true;

static void daemon_signal_handler(int sig)
{
    (void)sig;
    g_daemon_running = false;
}

static int ctm_connect_or_spawn_local(const ctm_config_t *cfg, const char *peer_name)
{
    int sock = net_connect_to_server(cfg->peer_ip, cfg->peer_port);
    if (sock < 0 && (strcmp(cfg->peer_ip, "127.0.0.1") == 0 || strcmp(cfg->peer_ip, "localhost") == 0)) {
        ctm_config_t peer_cfg;
        ctm_init_config(&peer_cfg, peer_name);
        ctm_ensure_daemon(&peer_cfg);
        sock = net_connect_to_server(cfg->peer_ip, cfg->peer_port);
    }
    return sock;
}

static void ctm_process_outbox(const ctm_config_t *cfg)
{
    int fd = open(cfg->outbox_path, O_RDWR, 0664);
    if (fd < 0) return;

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return;
    }

    FILE *fp = fdopen(dup(fd), "r+");
    if (!fp) {
        flock(fd, LOCK_UN);
        close(fd);
        return;
    }

    /* Try connecting to peer */
    int peer_sock = ctm_connect_or_spawn_local(cfg, cfg->peer_name);
    if (peer_sock < 0) {
        fclose(fp);
        flock(fd, LOCK_UN);
        close(fd);
        return;
    }

    char line[CTM_LINE_MAX];
    char remaining_lines[50][CTM_LINE_MAX];
    int remaining_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        proto_strip_newline(line);
        if (strlen(line) == 0) continue;

        char wire_hdr[128];
        snprintf(wire_hdr, sizeof(wire_hdr), "MSG %s %zu\n", cfg->user_name, strlen(line));
        if (proto_send_all(peer_sock, wire_hdr, strlen(wire_hdr)) < 0 ||
            proto_send_all(peer_sock, line, strlen(line)) < 0) {
            /* Transfer failed; keep line */
            if (remaining_count < 50) {
                snprintf(remaining_lines[remaining_count++], sizeof(remaining_lines[0]), "%s", line);
            }
        }
    }

    /* Rewrite outbox with remaining pending messages */
    rewind(fp);
    if (ftruncate(fileno(fp), 0) < 0) {
        /* Ignore error */
    }
    for (int i = 0; i < remaining_count; i++) {
        fprintf(fp, "%s\n", remaining_lines[i]);
    }
    fflush(fp);

    close(peer_sock);
    fclose(fp);
    flock(fd, LOCK_UN);
    close(fd);
}

static void handle_ipc_client(const ctm_config_t *cfg, int client_fd)
{
    char line[CTM_LINE_MAX];
    ssize_t n = proto_read_line(client_fd, line, sizeof(line));
    if (n <= 0) {
        close(client_fd);
        return;
    }
    proto_strip_newline(line);

    if (strcmp(line, "PING") == 0) {
        const char *reply = "PONG\n";
        proto_send_all(client_fd, reply, strlen(reply));
    } else if (strcmp(line, "STOP") == 0) {
        const char *reply = "OK Stopped\n";
        proto_send_all(client_fd, reply, strlen(reply));
        g_daemon_running = false;
    } else if (strcmp(line, "STATUS") == 0) {
        char summary[CTM_MAX_MSG_LEN] = {0};
        int unread = ctm_get_unread_count(cfg, summary, sizeof(summary));
        char reply[CTM_LINE_MAX];
        if (unread > 0) {
            snprintf(reply, sizeof(reply),
                     COLOR_GREEN "[ctm]" COLOR_RESET " %d unread message%s from %s\n"
                     COLOR_BOLD "Last:" COLOR_RESET " %s\n",
                     unread, (unread > 1 ? "s" : ""), cfg->peer_name, summary);
        } else {
            snprintf(reply, sizeof(reply),
                     COLOR_GREEN "[ctm]" COLOR_RESET " No unread messages. All caught up!\n");
        }
        proto_send_all(client_fd, reply, strlen(reply));
    } else if (strncmp(line, "LOG", 3) == 0) {
        int count = 20;
        sscanf(line + 3, "%d", &count);
        char history_buf[8192];
        ctm_history_read(cfg, count, true, history_buf, sizeof(history_buf));
        proto_send_all(client_fd, history_buf, strlen(history_buf));
    } else if (strncmp(line, "TEXT ", 5) == 0) {
        /* Format: TEXT <peer> <message> */
        char target_peer[CTM_MAX_NAME_LEN] = {0};
        char *msg_ptr = NULL;
        char *first_space = strchr(line + 5, ' ');
        if (first_space) {
            *first_space = '\0';
            snprintf(target_peer, sizeof(target_peer), "%.63s", line + 5);
            msg_ptr = first_space + 1;
        } else {
            snprintf(target_peer, sizeof(target_peer), "%.63s", cfg->peer_name);
            msg_ptr = line + 5;
        }

        /* 1. Append to local persistent history */
        ctm_message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.timestamp = time(NULL);
        msg.is_read = true;
        snprintf(msg.sender, sizeof(msg.sender), "%.63s", cfg->user_name);
        snprintf(msg.receiver, sizeof(msg.receiver), "%.63s", target_peer);
        msg.type = CTM_MSG_TEXT;
        snprintf(msg.content, sizeof(msg.content), "%.2047s", msg_ptr);
        ctm_history_append(cfg, &msg);

        /* 2. Attempt delivery to peer via TCP */
        int peer_sock = ctm_connect_or_spawn_local(cfg, target_peer);
        if (peer_sock >= 0) {
            char wire_hdr[128];
            snprintf(wire_hdr, sizeof(wire_hdr), "MSG %s %zu\n", cfg->user_name, strlen(msg_ptr));
            proto_send_all(peer_sock, wire_hdr, strlen(wire_hdr));
            proto_send_all(peer_sock, msg_ptr, strlen(msg_ptr));
            close(peer_sock);

            const char *ack = COLOR_GREEN "[ctm]" COLOR_RESET " Message delivered to " COLOR_BOLD "%s" COLOR_RESET ".\n";
            char reply[128];
            snprintf(reply, sizeof(reply), ack, target_peer);
            proto_send_all(client_fd, reply, strlen(reply));
        } else {
            /* Queue into outbox for future delivery */
            int out_fd = open(cfg->outbox_path, O_CREAT | O_WRONLY | O_APPEND, 0664);
            if (out_fd >= 0) {
                flock(out_fd, LOCK_EX);
                ssize_t w1 = write(out_fd, msg_ptr, strlen(msg_ptr));
                ssize_t w2 = write(out_fd, "\n", 1);
                (void)w1;
                (void)w2;
                flock(out_fd, LOCK_UN);
                close(out_fd);
            }
            const char *ack = COLOR_YELLOW "[ctm]" COLOR_RESET " Peer '%s' is offline. Message saved to outbox for retry.\n";
            char reply[128];
            snprintf(reply, sizeof(reply), ack, target_peer);
            proto_send_all(client_fd, reply, strlen(reply));
        }
    } else if (strncmp(line, "FILE ", 5) == 0) {
        /* Format: FILE <peer> <filepath> */
        char target_peer[CTM_MAX_NAME_LEN] = {0};
        char *path_ptr = NULL;
        char *first_space = strchr(line + 5, ' ');
        if (first_space) {
            *first_space = '\0';
            snprintf(target_peer, sizeof(target_peer), "%.63s", line + 5);
            path_ptr = first_space + 1;
        } else {
            snprintf(target_peer, sizeof(target_peer), "%.63s", cfg->peer_name);
            path_ptr = line + 5;
        }

        struct stat st;
        if (stat(path_ptr, &st) < 0 || !S_ISREG(st.st_mode)) {
            const char *err = COLOR_RED "[ctm ERROR]" COLOR_RESET " File does not exist or is not a regular file.\n";
            proto_send_all(client_fd, err, strlen(err));
            close(client_fd);
            return;
        }

        int file_fd = open(path_ptr, O_RDONLY);
        if (file_fd < 0) {
            const char *err = COLOR_RED "[ctm ERROR]" COLOR_RESET " Cannot read target file.\n";
            proto_send_all(client_fd, err, strlen(err));
            close(client_fd);
            return;
        }

        /* Extract basename */
        const char *base = strrchr(path_ptr, '/');
        base = base ? base + 1 : path_ptr;

        /* Try connecting to peer */
        int peer_sock = ctm_connect_or_spawn_local(cfg, target_peer);
        if (peer_sock < 0) {
            close(file_fd);
            const char *err = COLOR_YELLOW "[ctm]" COLOR_RESET " Cannot transfer file: Peer is offline.\n";
            proto_send_all(client_fd, err, strlen(err));
            close(client_fd);
            return;
        }

        /* Log in persistent history */
        ctm_message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.timestamp = time(NULL);
        msg.is_read = true;
        snprintf(msg.sender, sizeof(msg.sender), "%.63s", cfg->user_name);
        snprintf(msg.receiver, sizeof(msg.receiver), "%.63s", target_peer);
        msg.type = CTM_MSG_FILE;
        snprintf(msg.content, sizeof(msg.content), "%.2047s", base);
        msg.file_size = (long)st.st_size;
        ctm_history_append(cfg, &msg);

        /* Send file header */
        char hdr[512];
        snprintf(hdr, sizeof(hdr), "FILE %s %.200s %ld\n", cfg->user_name, base, (long)st.st_size);
        proto_send_all(peer_sock, hdr, strlen(hdr));

        /* Stream file chunks */
        char chunk[BUFFER_SIZE];
        ssize_t r;
        while ((r = read(file_fd, chunk, sizeof(chunk))) > 0) {
            proto_send_all(peer_sock, chunk, (size_t)r);
        }
        close(file_fd);
        close(peer_sock);

        char reply[512];
        snprintf(reply, sizeof(reply),
                 COLOR_GREEN "[ctm]" COLOR_RESET " Sent file " COLOR_BOLD "'%.100s'" COLOR_RESET " (%ld bytes) to %s.\n",
                 base, (long)st.st_size, target_peer);
        proto_send_all(client_fd, reply, strlen(reply));
    } else if (strcmp(line, "CLEAR") == 0) {
        ctm_history_clear(cfg);
        const char *reply = COLOR_GREEN "[ctm]" COLOR_RESET " Chat history cleared.\n";
        proto_send_all(client_fd, reply, strlen(reply));
    } else {
        const char *reply = "ERR Unknown command\n";
        proto_send_all(client_fd, reply, strlen(reply));
    }

    close(client_fd);
}

static void handle_peer_connection(const ctm_config_t *cfg, int peer_sock)
{
    char line[CTM_LINE_MAX];
    ssize_t n = proto_read_line(peer_sock, line, sizeof(line));
    if (n <= 0) {
        close(peer_sock);
        return;
    }
    proto_strip_newline(line);

    if (strncmp(line, "MSG ", 4) == 0) {
        /* Format: MSG <sender> <length> */
        char sender[CTM_MAX_NAME_LEN] = {0};
        size_t msg_len = 0;
        if (sscanf(line + 4, "%63s %zu", sender, &msg_len) == 2 && msg_len > 0) {
            if (msg_len >= CTM_MAX_MSG_LEN) msg_len = CTM_MAX_MSG_LEN - 1;
            char msg_body[CTM_MAX_MSG_LEN] = {0};
            ssize_t received = proto_recv_all(peer_sock, msg_body, msg_len);
            if (received > 0) {
                msg_body[received] = '\0';

                ctm_message_t msg;
                memset(&msg, 0, sizeof(msg));
                msg.timestamp = time(NULL);
                msg.is_read = false;
                snprintf(msg.sender, sizeof(msg.sender), "%s", sender);
                snprintf(msg.receiver, sizeof(msg.receiver), "%s", cfg->user_name);
                msg.type = CTM_MSG_TEXT;
                snprintf(msg.content, sizeof(msg.content), "%s", msg_body);
                ctm_history_append(cfg, &msg);
            }
        }
    } else if (strncmp(line, "FILE ", 5) == 0) {
        /* Format: FILE <sender> <filename> <size> */
        char sender[CTM_MAX_NAME_LEN] = {0};
        char filename[MAX_FILENAME_LEN] = {0};
        long file_size = 0;

        if (sscanf(line + 5, "%63s %255s %ld", sender, filename, &file_size) == 3 && file_size >= 0) {
            /* Guard against path traversal */
            if (strstr(filename, "..") == NULL && filename[0] != '/') {
                char local_path[MAX_PATH_LEN];
                snprintf(local_path, sizeof(local_path), "%.255s/%.255s", cfg->downloads_dir, filename);
                int out_fd = open(local_path, O_CREAT | O_WRONLY | O_TRUNC, 0664);
                if (out_fd >= 0) {
                    char chunk[BUFFER_SIZE];
                    long total = 0;
                    while (total < file_size) {
                        size_t to_read = sizeof(chunk);
                        if (file_size - total < (long)to_read) to_read = (size_t)(file_size - total);
                        ssize_t r = read(peer_sock, chunk, to_read);
                        if (r <= 0) break;
                        ssize_t w = write(out_fd, chunk, (size_t)r);
                        (void)w;
                        total += r;
                    }
                    close(out_fd);

                    ctm_message_t msg;
                    memset(&msg, 0, sizeof(msg));
                    msg.timestamp = time(NULL);
                    msg.is_read = false;
                    snprintf(msg.sender, sizeof(msg.sender), "%.63s", sender);
                    snprintf(msg.receiver, sizeof(msg.receiver), "%.63s", cfg->user_name);
                    msg.type = CTM_MSG_FILE;
                    snprintf(msg.content, sizeof(msg.content), "%.2047s", filename);
                    msg.file_size = file_size;
                    ctm_history_append(cfg, &msg);
                }
            }
        }
    }

    close(peer_sock);
}

int ctm_daemon_run(ctm_config_t *cfg)
{
    signal(SIGTERM, daemon_signal_handler);
    signal(SIGINT, daemon_signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Write PID file */
    FILE *pid_fp = fopen(cfg->pid_path, "w");
    if (pid_fp) {
        fprintf(pid_fp, "%d\n", getpid());
        fclose(pid_fp);
    }

    /* 1. Setup Unix Domain Socket for local IPC */
    unlink(cfg->sock_path);
    int uds_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (uds_fd < 0) {
        LOG_ERROR("Failed to create Unix domain socket");
        return -1;
    }

    struct sockaddr_un uds_addr;
    memset(&uds_addr, 0, sizeof(uds_addr));
    uds_addr.sun_family = AF_UNIX;
    snprintf(uds_addr.sun_path, sizeof(uds_addr.sun_path), "%.103s", cfg->sock_path);

    if (bind(uds_fd, (struct sockaddr *)&uds_addr, sizeof(uds_addr)) < 0) {
        LOG_ERROR("Failed to bind Unix domain socket '%s'", cfg->sock_path);
        close(uds_fd);
        return -1;
    }

    if (listen(uds_fd, 10) < 0) {
        LOG_ERROR("Failed to listen on Unix domain socket");
        close(uds_fd);
        return -1;
    }

    /* 2. Setup TCP Server Socket for peer communication */
    net_clean_port(cfg->listen_port);
    int tcp_fd = net_create_server_socket(cfg->listen_port, 10);
    if (tcp_fd < 0) {
        LOG_ERROR("Failed to bind daemon TCP socket on port %d", cfg->listen_port);
    }

    LOG_INFO("ctm daemon started [User: %s, Port: %d, Socket: %s]",
             cfg->user_name, cfg->listen_port, cfg->sock_path);

    time_t last_outbox_check = 0;

    /* 3. Event Loop with poll() */
    while (g_daemon_running) {
        struct pollfd fds[2];
        int num_fds = 0;

        fds[0].fd = uds_fd;
        fds[0].events = POLLIN;
        num_fds = 1;

        if (tcp_fd >= 0) {
            fds[1].fd = tcp_fd;
            fds[1].events = POLLIN;
            num_fds = 2;
        }

        int poll_ret = poll(fds, num_fds, 1000); /* 1s timeout for periodic tasks */
        if (poll_ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Periodic outbox retry */
        time_t now = time(NULL);
        if (now - last_outbox_check >= 5) {
            ctm_process_outbox(cfg);
            last_outbox_check = now;
        }

        /* Handle local CLI IPC request */
        if (fds[0].revents & POLLIN) {
            int client_fd = accept(uds_fd, NULL, NULL);
            if (client_fd >= 0) {
                handle_ipc_client(cfg, client_fd);
            }
        }

        /* Handle incoming peer TCP connection */
        if (num_fds == 2 && (fds[1].revents & POLLIN)) {
            struct sockaddr_in peer_addr;
            socklen_t peer_len = sizeof(peer_addr);
            int peer_sock = accept(tcp_fd, (struct sockaddr *)&peer_addr, &peer_len);
            if (peer_sock >= 0) {
                handle_peer_connection(cfg, peer_sock);
            }
        }
    }

    /* Clean shutdown */
    close(uds_fd);
    if (tcp_fd >= 0) close(tcp_fd);
    unlink(cfg->sock_path);
    unlink(cfg->pid_path);
    LOG_INFO("ctm daemon stopped.");
    return 0;
}
