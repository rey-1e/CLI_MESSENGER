/**
 * @file chat_client.h
 * @brief Chat client interface and worker thread definitions.
 */

#ifndef CHAT_CLIENT_H
#define CHAT_CLIENT_H

#include "common.h"

/**
 * @brief Context configuration for the chat client.
 */
typedef struct {
    char server_ip[INET_ADDRSTRLEN];
    int port;
    char username[MAX_USERNAME_LEN];
    int socket_fd;
    volatile bool running;
} chat_client_t;

/**
 * @brief Initializes and runs the client chat session.
 * 
 * @param client Pointer to configured chat_client_t struct.
 * @return 0 on normal exit, non-zero on error.
 */
int chat_client_run(chat_client_t *client);

/**
 * @brief Worker thread for sending user input to the chat server.
 * 
 * @param arg Pointer to chat_client_t.
 * @return NULL on completion.
 */
void *chat_client_send_worker(void *arg);

/**
 * @brief Worker thread for receiving messages from the chat server.
 * 
 * @param arg Pointer to chat_client_t.
 * @return NULL on completion.
 */
void *chat_client_recv_worker(void *arg);

#endif /* CHAT_CLIENT_H */
