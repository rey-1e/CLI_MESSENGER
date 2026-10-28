/**
 * @file network.c
 * @brief Implementation of network socket and connection helpers.
 */

#include "network.h"

int net_set_reuse_addr(int socket_fd)
{
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARN("Failed to set SO_REUSEADDR: %s", strerror(errno));
        return -1;
    }
#ifdef SO_REUSEPORT
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        /* SO_REUSEPORT might not be available or fail on some kernels; ignore error */
    }
#endif
    return 0;
}

void net_clean_port(int port)
{
    char command[256];
    snprintf(command, sizeof(command),
             "lsof -ti tcp:%d 2>/dev/null | grep -v %d | xargs kill -9 2>/dev/null",
             port, getpid());
    int ret = system(command);
    (void)ret;
    usleep(200000); /* 0.2s pause for kernel socket recycling */
}

int net_create_server_socket(int port, int backlog)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("socket() failed");
        return -1;
    }

    if (net_set_reuse_addr(fd) < 0) {
        /* Non-fatal, continue */
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("bind() failed on port %d", port);
        close(fd);
        return -1;
    }

    if (listen(fd, backlog) < 0) {
        LOG_ERROR("listen() failed");
        close(fd);
        return -1;
    }

    return fd;
}

int net_connect_to_server(const char *ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("socket() failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        LOG_WARN("Invalid IP address: %s", ip);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("connect() to %s:%d failed", ip, port);
        close(fd);
        return -1;
    }

    return fd;
}

void net_close_socket(int *socket_fd)
{
    if (socket_fd && *socket_fd >= 0) {
        close(*socket_fd);
        *socket_fd = -1;
    }
}
