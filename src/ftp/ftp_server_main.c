/**
 * @file ftp_server_main.c
 * @brief Concurrent FTP Server daemon main entry point.
 */

#include "ftp.h"
#include "network.h"

static int g_listen_fd = -1;

static void handle_sigint(int sig)
{
    (void)sig;
    const char msg[] = "\nShutting down FTP Server...\n";
    ssize_t w = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)w;
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    _exit(0);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, handle_sigint);
    /* Prevent zombie child processes by auto-reaping */
    signal(SIGCHLD, SIG_IGN);

    /* Ensure storage directory exists */
    mkdir(STORAGE_DIRECTORY, 0775);

    int port = DEFAULT_FTP_PORT;

    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            LOG_WARN("Invalid port provided '%s'. Defaulting to %d.", argv[1], DEFAULT_FTP_PORT);
            port = DEFAULT_FTP_PORT;
        }
    } else {
        printf("Enter the FTP port number [default %d]: ", DEFAULT_FTP_PORT);
        char input[32];
        if (fgets(input, sizeof(input), stdin) && input[0] != '\n') {
            int parsed = atoi(input);
            if (parsed > 0 && parsed <= 65535) {
                port = parsed;
            }
        }
    }

    LOG_INFO("Starting Concurrent FTP Server on port %d...", port);
    net_clean_port(port);

    g_listen_fd = net_create_server_socket(port, 20);
    if (g_listen_fd < 0) {
        LOG_ERROR("Could not bind and listen on port %d", port);
        return EXIT_FAILURE;
    }

    LOG_INFO("Concurrent FTP Server running on port %d. Serving storage directory: '%s'.",
             port, STORAGE_DIRECTORY);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("FTP accept() failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        LOG_INFO("FTP: Incoming connection from %s:%d", client_ip, ntohs(client_addr.sin_port));

        pid_t pid = fork();
        if (pid < 0) {
            LOG_ERROR("fork() failed to create worker process");
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            /* Child process: handle the client request */
            close(g_listen_fd);
            ftp_server_serve_file(client_fd, STORAGE_DIRECTORY);
            close(client_fd);
            _exit(0);
        } else {
            /* Parent process: close client descriptor and continue accepting */
            close(client_fd);
        }
    }

    net_close_socket(&g_listen_fd);
    return EXIT_SUCCESS;
}
