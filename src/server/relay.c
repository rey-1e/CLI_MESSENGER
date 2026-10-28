/**
 * @file relay.c
 * @brief Enterprise Multi-Client Group Chat (GC) server engine and thread synchronization.
 */

#include "chat_server.h"
#include "protocol.h"

void chat_room_init(chat_room_t *room)
{
    memset(room, 0, sizeof(*room));
    pthread_mutex_init(&room->lock, NULL);
    room->running = true;
    room->active_count = 0;
    room->listen_fd = -1;
}

void chat_room_broadcast(chat_room_t *room, const char *msg, int exclude_fd)
{
    if (!room || !msg) return;

    pthread_mutex_lock(&room->lock);
    for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
        if (room->clients[i].active && room->clients[i].socket_fd >= 0) {
            if (room->clients[i].socket_fd != exclude_fd) {
                send(room->clients[i].socket_fd, msg, strlen(msg), MSG_NOSIGNAL);
            }
        }
    }
    pthread_mutex_unlock(&room->lock);
}

void chat_room_get_member_list(chat_room_t *room, char *buffer, size_t buf_len)
{
    if (!room || !buffer || buf_len == 0) return;
    buffer[0] = '\0';

    pthread_mutex_lock(&room->lock);
    char members[2048] = {0};
    int count = 0;

    for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
        if (room->clients[i].active && strlen(room->clients[i].username) > 0) {
            if (count > 0) {
                strncat(members, ", ", sizeof(members) - strlen(members) - 1);
            }
            strncat(members, room->clients[i].username, sizeof(members) - strlen(members) - 1);
            count++;
        }
    }

    snprintf(buffer, buf_len,
             "\n" COLOR_CYAN "*** Active Group Chat Members (%d): %s ***" COLOR_RESET "\n",
             count, (count > 0) ? members : "None");
    pthread_mutex_unlock(&room->lock);
}

void chat_room_remove_client(chat_room_t *room, int client_fd)
{
    if (!room || client_fd < 0) return;

    char username[MAX_USERNAME_LEN] = {0};
    int remaining = 0;
    bool found = false;

    pthread_mutex_lock(&room->lock);
    for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
        if (room->clients[i].active && room->clients[i].socket_fd == client_fd) {
            snprintf(username, sizeof(username), "%s", room->clients[i].username);
            room->clients[i].active = false;
            room->clients[i].socket_fd = -1;
            room->active_count--;
            remaining = room->active_count;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&room->lock);

    close(client_fd);

    if (found && strlen(username) > 0) {
        LOG_SERVER("Employee '%s' disconnected. Active members remaining: %d.", username, remaining);
        char leave_msg[512];
        snprintf(leave_msg, sizeof(leave_msg),
                 "\n" COLOR_YELLOW "*** [SYSTEM] %s has left the group chat. (Online: %d) ***" COLOR_RESET "\n",
                 username, remaining);
        chat_room_broadcast(room, leave_msg, -1);
    }
}

void *chat_room_client_worker(void *arg)
{
    client_handler_ctx_t *ctx = (client_handler_ctx_t *)arg;
    chat_room_t *room = ctx->room;
    int client_fd = ctx->client_fd;
    int slot = ctx->client_index;
    char client_ip[INET_ADDRSTRLEN];
    snprintf(client_ip, sizeof(client_ip), "%s", ctx->ip);
    int client_port = ctx->port;
    free(ctx);

    char line[BUFFER_SIZE];

    /* Step 1: Read client handshake (e.g. "JOIN Alice (EMP001)") */
    ssize_t n = proto_read_line(client_fd, line, sizeof(line));
    if (n <= 0) {
        chat_room_remove_client(room, client_fd);
        return NULL;
    }
    proto_strip_newline(line);

    char username[MAX_USERNAME_LEN] = "Anonymous Employee";
    if (strncmp(line, "JOIN ", 5) == 0 && strlen(line + 5) > 0) {
        snprintf(username, sizeof(username), "%.49s", line + 5);
    } else if (strlen(line) > 0) {
        snprintf(username, sizeof(username), "%.49s", line);
    }

    pthread_mutex_lock(&room->lock);
    snprintf(room->clients[slot].username, sizeof(room->clients[slot].username), "%s", username);
    int current_online = room->active_count;
    pthread_mutex_unlock(&room->lock);

    LOG_SERVER("Verified Employee '%s' joined group chat from %s:%d. Total online: %d.",
               username, client_ip, client_port, current_online);

    /* Step 2: Send welcome message & active member list to the new user */
    char welcome_buf[4096];
    char member_list[2048];
    chat_room_get_member_list(room, member_list, sizeof(member_list));

    snprintf(welcome_buf, sizeof(welcome_buf),
             "\n" COLOR_BOLD "========================================================================" COLOR_RESET "\n"
             COLOR_CYAN COLOR_BOLD "  CORPORATE GROUP CHAT ROOM &mdash; ENTERPRISE MESSENGER" COLOR_RESET "\n"
             COLOR_BOLD "========================================================================" COLOR_RESET "\n"
             "  Authenticated as: " COLOR_GREEN COLOR_BOLD "%s" COLOR_RESET "\n"
             "%s"
             "  Type your message and press ENTER to broadcast to all members.\n"
             "  Commands: " COLOR_BOLD "/who" COLOR_RESET " (list members), " COLOR_BOLD "/exit" COLOR_RESET " (disconnect)\n"
             COLOR_BOLD "========================================================================" COLOR_RESET "\n\n",
             username, member_list);
    proto_send_all(client_fd, welcome_buf, strlen(welcome_buf));

    /* Step 3: Broadcast join announcement to all other connected employees */
    char join_announcement[512];
    snprintf(join_announcement, sizeof(join_announcement),
             "\n" COLOR_GREEN "*** [SYSTEM] %s joined the corporate group chat. (Online: %d) ***" COLOR_RESET "\n",
             username, current_online);
    chat_room_broadcast(room, join_announcement, client_fd);

    /* Step 4: Bidirectional group message loop */
    while (room->running) {
        memset(line, 0, sizeof(line));
        n = proto_read_line(client_fd, line, sizeof(line));
        if (n <= 0) break;

        proto_strip_newline(line);
        if (strlen(line) == 0) continue;

        /* Check for commands */
        if (strcmp(line, "/exit") == 0 || strcmp(line, "/quit") == 0) {
            break;
        } else if (strcmp(line, "/who") == 0 || strcmp(line, "/users") == 0 || strcmp(line, "/list") == 0) {
            char members[2048];
            chat_room_get_member_list(room, members, sizeof(members));
            proto_send_all(client_fd, members, strlen(members));
            continue;
        }

        /* Broadcast message to all other group chat members */
        char broadcast[MAX_PAYLOAD_SIZE];
        /* Check if client payload already includes username */
        if (strstr(line, ": ") != NULL) {
            snprintf(broadcast, sizeof(broadcast), "%s\n", line);
        } else {
            snprintf(broadcast, sizeof(broadcast), "%s: %s\n", username, line);
        }

        chat_room_broadcast(room, broadcast, client_fd);
    }

    chat_room_remove_client(room, client_fd);
    return NULL;
}

int chat_room_add_client(chat_room_t *room, int client_fd, const char *ip, int port)
{
    if (!room || client_fd < 0) return -1;

    pthread_mutex_lock(&room->lock);
    if (room->active_count >= MAX_CHAT_CLIENTS) {
        pthread_mutex_unlock(&room->lock);
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
        if (!room->clients[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&room->lock);
        return -1;
    }

    room->clients[slot].socket_fd = client_fd;
    snprintf(room->clients[slot].ip, sizeof(room->clients[slot].ip), "%s", ip);
    room->clients[slot].port = port;
    room->clients[slot].active = true;
    room->clients[slot].join_time = time(NULL);
    room->clients[slot].username[0] = '\0';
    room->active_count++;

    client_handler_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        room->clients[slot].active = false;
        room->active_count--;
        pthread_mutex_unlock(&room->lock);
        return -1;
    }
    ctx->room = room;
    ctx->client_index = slot;
    ctx->client_fd = client_fd;
    snprintf(ctx->ip, sizeof(ctx->ip), "%s", ip);
    ctx->port = port;

    pthread_t thread;
    if (pthread_create(&thread, NULL, chat_room_client_worker, ctx) != 0) {
        free(ctx);
        room->clients[slot].active = false;
        room->active_count--;
        pthread_mutex_unlock(&room->lock);
        return -1;
    }

    room->clients[slot].thread = thread;
    pthread_detach(thread);

    pthread_mutex_unlock(&room->lock);
    return 0;
}

void chat_room_shutdown(chat_room_t *room)
{
    if (!room) return;

    const char *msg = "\n" COLOR_RED "*** [SYSTEM] Corporate Group Chat server is shutting down. Goodbye! ***" COLOR_RESET "\n";

    pthread_mutex_lock(&room->lock);
    room->running = false;

    for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
        if (room->clients[i].active && room->clients[i].socket_fd >= 0) {
            send(room->clients[i].socket_fd, msg, strlen(msg), MSG_NOSIGNAL);
            shutdown(room->clients[i].socket_fd, SHUT_RDWR);
            close(room->clients[i].socket_fd);
            room->clients[i].active = false;
            room->clients[i].socket_fd = -1;
        }
    }
    room->active_count = 0;
    pthread_mutex_unlock(&room->lock);

    pthread_mutex_destroy(&room->lock);
}
