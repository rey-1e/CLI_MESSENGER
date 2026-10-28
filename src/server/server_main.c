/**
 * @file server_main.c
 * @brief Main entry point for the Chat Messenger Relay Server.
 */

#include "chat_server.h"
#include "network.h"

static int g_listen_fd = -1;

static void handle_sigint(int sig)
{
    (void)sig;
    const char msg[] = "\nShutting down Chat Server...\n";
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

    int port = DEFAULT_CHAT_PORT;

    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            LOG_WARN("Invalid port provided '%s'. Defaulting to %d.", argv[1], DEFAULT_CHAT_PORT);
            port = DEFAULT_CHAT_PORT;
        }
    } else {
        printf("Enter the port number [default %d]: ", DEFAULT_CHAT_PORT);
        char input[32];
        if (fgets(input, sizeof(input), stdin) && input[0] != '\n') {
            int parsed = atoi(input);
            if (parsed > 0 && parsed <= 65535) {
                port = parsed;
            }
        }
    }

    LOG_INFO("Initializing Chat Server on port %d...", port);
    net_clean_port(port);

    g_listen_fd = net_create_server_socket(port, 10);
    if (g_listen_fd < 0) {
        LOG_ERROR("Could not start server on port %d", port);
        return EXIT_FAILURE;
    }

    LOG_INFO("Chat Server is listening on port %d. Waiting for peer pairs...", port);

    while (1) {
        int client1_fd = -1;
        int client2_fd = -1;

        if (server_accept_client_pair(g_listen_fd, &client1_fd, &client2_fd) == 0) {
            server_start_relay_session(client1_fd, client2_fd);
        }
    }

    net_close_socket(&g_listen_fd);
    return EXIT_SUCCESS;
}
