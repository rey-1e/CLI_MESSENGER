/**
 * @file server_main.c
 * @brief Main entry point for the Corporate Group Chat (GC) Relay Server.
 */

#include "chat_server.h"
#include "network.h"

static int g_listen_fd = -1;
static chat_room_t g_room;

static void handle_sigint(int sig)
{
    (void)sig;
    const char msg[] = "\nShutting down Corporate Group Chat Server...\n";
    ssize_t w = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)w;

    chat_room_shutdown(&g_room);

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
    /* Ignore SIGPIPE so write to disconnected client doesn't crash the server */
    signal(SIGPIPE, SIG_IGN);

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

    LOG_INFO("Initializing Corporate Group Chat Server on port %d...", port);
    net_clean_port(port);

    g_listen_fd = net_create_server_socket(port, 20);
    if (g_listen_fd < 0) {
        LOG_ERROR("Could not start server on port %d", port);
        return EXIT_FAILURE;
    }

    chat_room_init(&g_room);
    g_room.listen_fd = g_listen_fd;

    LOG_INFO("Corporate Group Chat Server active on port %d (Max Capacity: %d employees).",
             port, MAX_CHAT_CLIENTS);

    while (g_room.running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("accept() failed: %s", strerror(errno));
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        if (chat_room_add_client(&g_room, client_fd, client_ip, client_port) < 0) {
            LOG_WARN("Connection from %s:%d rejected: room capacity reached (%d).",
                     client_ip, client_port, MAX_CHAT_CLIENTS);
            const char *full_msg = "\n" COLOR_RED "*** [SYSTEM] Corporate group chat room is full. Try again later. ***" COLOR_RESET "\n";
            send(client_fd, full_msg, strlen(full_msg), MSG_NOSIGNAL);
            close(client_fd);
        }
    }

    chat_room_shutdown(&g_room);
    net_close_socket(&g_listen_fd);
    return EXIT_SUCCESS;
}
