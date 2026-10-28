/**
 * @file protocol.c
 * @brief Implementation of communication protocol and robust I/O helpers.
 */

#include "protocol.h"

ssize_t proto_read_line(int socket_fd, char *buffer, size_t max_len)
{
    if (!buffer || max_len == 0) return -1;

    size_t total_read = 0;
    char ch = '\0';

    while (total_read < max_len - 1) {
        ssize_t n = read(socket_fd, &ch, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            break; /* EOF */
        }

        buffer[total_read++] = ch;
        if (ch == '\n') {
            break;
        }
    }

    buffer[total_read] = '\0';
    return (ssize_t)total_read;
}

ssize_t proto_send_all(int socket_fd, const void *buffer, size_t length)
{
    const char *ptr = (const char *)buffer;
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(socket_fd, ptr + total_sent, length - total_sent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) {
            break;
        }
        total_sent += (size_t)sent;
    }

    return (ssize_t)total_sent;
}

ssize_t proto_recv_all(int socket_fd, void *buffer, size_t length)
{
    char *ptr = (char *)buffer;
    size_t total_received = 0;

    while (total_received < length) {
        ssize_t n = recv(socket_fd, ptr + total_received, length - total_received, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            break; /* EOF */
        }
        total_received += (size_t)n;
    }

    return (ssize_t)total_received;
}

void proto_strip_newline(char *str)
{
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len--;
    }
}
