/**
 * @file chat_server.h
 * @brief Multi-client Group Chat (GC) server engine and thread synchronization.
 */

#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

#include "common.h"
#include <pthread.h>

#define MAX_CHAT_CLIENTS    32

typedef struct {
    int socket_fd;
    char username[MAX_USERNAME_LEN];
    char ip[INET_ADDRSTRLEN];
    int port;
    bool active;
    pthread_t thread;
    time_t join_time;
} chat_client_node_t;

typedef struct {
    chat_client_node_t clients[MAX_CHAT_CLIENTS];
    int active_count;
    pthread_mutex_t lock;
    int listen_fd;
    volatile bool running;
} chat_room_t;

typedef struct {
    chat_room_t *room;
    int client_index;
    int client_fd;
    char ip[INET_ADDRSTRLEN];
    int port;
} client_handler_ctx_t;

/**
 * @brief Initializes the chat room state and synchronization mutex.
 */
void chat_room_init(chat_room_t *room);

/**
 * @brief Thread-safe broadcast of a message to all active clients in the room.
 * 
 * @param room Pointer to the chat room structure.
 * @param msg Message string to broadcast.
 * @param exclude_fd Socket descriptor to exclude from broadcast (e.g. sender), or -1 for all.
 */
void chat_room_broadcast(chat_room_t *room, const char *msg, int exclude_fd);

/**
 * @brief Registers and spawns an asynchronous worker thread for a newly connected client.
 * 
 * @param room Pointer to chat room.
 * @param client_fd Socket descriptor of the connected client.
 * @param ip Client IP address.
 * @param port Client port number.
 * @return 0 on success, -1 if room is full.
 */
int chat_room_add_client(chat_room_t *room, int client_fd, const char *ip, int port);

/**
 * @brief Thread-safe removal of a client, socket closure, and leave broadcast.
 */
void chat_room_remove_client(chat_room_t *room, int client_fd);

/**
 * @brief Formats a list of currently active corporate chat members.
 */
void chat_room_get_member_list(chat_room_t *room, char *buffer, size_t buf_len);

/**
 * @brief Worker thread routine handling bidirectional traffic for an individual client.
 */
void *chat_room_client_worker(void *arg);

/**
 * @brief Shuts down the chat room, notifies members, and cleanly terminates connections.
 */
void chat_room_shutdown(chat_room_t *room);

#endif /* CHAT_SERVER_H */
