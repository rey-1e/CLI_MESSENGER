/**
 * @file chat_server.h
 * @brief Relay server interface for multi-client chat sessions.
 */

#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

#include "common.h"

/**
 * @brief Arguments passed to individual relay worker threads.
 */
typedef struct {
    int sender_fd;
    int receiver_fd;
    const char *sender_tag;
    volatile bool *session_active;
} relay_args_t;

/**
 * @brief Waits for and accepts a pair of chat clients, sending initial handshake greetings.
 * 
 * @param listen_fd Server listening socket descriptor.
 * @param client1_fd Pointer to store Client 1 socket descriptor.
 * @param client2_fd Pointer to store Client 2 socket descriptor.
 * @return 0 on success, or -1 on failure.
 */
int server_accept_client_pair(int listen_fd, int *client1_fd, int *client2_fd);

/**
 * @brief Starts bidirectional message relaying between two connected clients.
 * 
 * @param client1_fd Socket descriptor of Client 1.
 * @param client2_fd Socket descriptor of Client 2.
 */
void server_start_relay_session(int client1_fd, int client2_fd);

/**
 * @brief Worker thread routine that forwards incoming messages from sender to receiver.
 * 
 * @param arg Pointer to relay_args_t structure.
 * @return NULL on thread termination.
 */
void *server_relay_worker(void *arg);

#endif /* CHAT_SERVER_H */
