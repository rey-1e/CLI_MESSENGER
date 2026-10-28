/**
 * @file network.h
 * @brief Network socket abstraction and connection management.
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "common.h"

/**
 * @brief Creates, binds, and configures a TCP listening socket.
 * 
 * @param port Port number to listen on.
 * @param backlog Maximum length of the queue of pending connections.
 * @return Socket file descriptor on success, or -1 on error.
 */
int net_create_server_socket(int port, int backlog);

/**
 * @brief Connects to a remote TCP server.
 * 
 * @param ip IPv4 address string (e.g. "127.0.0.1").
 * @param port Destination port number.
 * @return Connected socket file descriptor on success, or -1 on error.
 */
int net_connect_to_server(const char *ip, int port);

/**
 * @brief Enables SO_REUSEADDR and SO_REUSEPORT on a socket descriptor.
 * 
 * @param socket_fd Socket file descriptor.
 * @return 0 on success, or -1 on error.
 */
int net_set_reuse_addr(int socket_fd);

/**
 * @brief Terminate any orphaned processes occupying the specified port.
 * 
 * @param port Port number to check and clear.
 */
void net_clean_port(int port);

/**
 * @brief Safely closes a socket and sets the descriptor to -1.
 * 
 * @param socket_fd Pointer to socket file descriptor.
 */
void net_close_socket(int *socket_fd);

#endif /* NETWORK_H */
