/**
 * @file ctm_ipc.c
 * @brief Unix domain socket IPC client and auto-daemonization engine.
 */

#include "ctm.h"
#include "protocol.h"
#include <sys/wait.h>

bool ctm_is_daemon_running(const ctm_config_t *cfg)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%.103s", cfg->sock_path);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }

    /* Set short 1s timeout */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    const char *ping = "PING\n";
    if (proto_send_all(sock, ping, strlen(ping)) < 0) {
        close(sock);
        return false;
    }

    char reply[64] = {0};
    ssize_t n = proto_read_line(sock, reply, sizeof(reply));
    close(sock);

    if (n > 0 && strncmp(reply, "PONG", 4) == 0) {
        return true;
    }
    return false;
}

int ctm_ensure_daemon(const ctm_config_t *cfg)
{
    if (ctm_is_daemon_running(cfg)) {
        return 0;
    }

    /* Clean stale socket file if left behind by crashed instance */
    unlink(cfg->sock_path);

    /* Fork background daemon process */
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork() failed to spawn daemon");
        return -1;
    }

    if (pid == 0) {
        /* In Child Process: daemonize */
        if (setsid() < 0) {
            _exit(1);
        }

        /* Prevent holding directory locks */
        if (chdir("/") < 0) {
            /* Non-fatal */
        }

        /* Redirect standard descriptors to log file or /dev/null */
        char log_path[MAX_PATH_LEN];
        snprintf(log_path, sizeof(log_path), "%.500s/ctmd.log", cfg->base_dir);
        int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0664);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        } else {
            int null_fd = open("/dev/null", O_RDWR);
            if (null_fd >= 0) {
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
                close(null_fd);
            }
        }

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        /* Run daemon event loop */
        ctm_config_t daemon_cfg = *cfg;
        int status = ctm_daemon_run(&daemon_cfg);
        _exit(status);
    }

    /* In Parent Process: wait up to 2 seconds for daemon to initialize */
    for (int i = 0; i < 20; i++) {
        usleep(100000); /* 100ms */
        if (ctm_is_daemon_running(cfg)) {
            return 0;
        }
    }

    LOG_WARN("ctm daemon started but did not respond to PING within 2s");
    return 0;
}

int ctm_ipc_request(const ctm_config_t *cfg, const char *cmd, char *response, size_t resp_max)
{
    if (response && resp_max > 0) response[0] = '\0';

    if (ctm_ensure_daemon(cfg) < 0) {
        return -1;
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%.103s", cfg->sock_path);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    /* Send command */
    if (proto_send_all(sock, cmd, strlen(cmd)) < 0) {
        close(sock);
        return -1;
    }
    if (cmd[strlen(cmd) - 1] != '\n') {
        proto_send_all(sock, "\n", 1);
    }

    /* Read response */
    if (response && resp_max > 0) {
        size_t total = 0;
        char buf[BUFFER_SIZE];
        ssize_t n;
        while ((n = read(sock, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            size_t copy_len = (size_t)n;
            if (total + copy_len >= resp_max - 1) {
                copy_len = resp_max - 1 - total;
            }
            memcpy(response + total, buf, copy_len);
            total += copy_len;
            if (total >= resp_max - 1) break;
        }
        response[total] = '\0';
    }

    close(sock);
    return 0;
}

int ctm_stop_daemon(const ctm_config_t *cfg)
{
    if (!ctm_is_daemon_running(cfg)) {
        printf("[ctm] Daemon is not running for user '%s'.\n", cfg->user_name);
        return 0;
    }

    char resp[128] = {0};
    ctm_ipc_request(cfg, "STOP\n", resp, sizeof(resp));
    unlink(cfg->sock_path);
    unlink(cfg->pid_path);
    printf("[ctm] Daemon stopped successfully.\n");
    return 0;
}
