/**
 * @file protocol.h
 * @brief Communication protocol and I/O helper routines.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

/**
 * @brief Reads a line terminated by '\n' from a socket.
 * 
 * @param socket_fd Socket descriptor.
 * @param buffer Output buffer to store line.
 * @param max_len Maximum capacity of the output buffer.
 * @return Number of characters read (excluding null terminator), or -1 on error / 0 on EOF.
 */
ssize_t proto_read_line(int socket_fd, char *buffer, size_t max_len);

/**
 * @brief Sends an exact amount of data over a socket, handling partial writes.
 * 
 * @param socket_fd Socket descriptor.
 * @param buffer Pointer to data buffer.
 * @param length Number of bytes to send.
 * @return Total bytes sent, or -1 on error.
 */
ssize_t proto_send_all(int socket_fd, const void *buffer, size_t length);

/**
 * @brief Receives an exact amount of data from a socket, handling partial reads.
 * 
 * @param socket_fd Socket descriptor.
 * @param buffer Output data buffer.
 * @param length Exact number of bytes to receive.
 * @return Total bytes received, or -1 on error / 0 on premature EOF.
 */
ssize_t proto_recv_all(int socket_fd, void *buffer, size_t length);

/**
 * @brief Strips trailing newline (\r and \n) characters from a string.
 * 
 * @param str Null-terminated string to strip in-place.
 */
void proto_strip_newline(char *str);

#endif /* PROTOCOL_H */
