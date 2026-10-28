/**
 * @file chat_client.c
 * @brief Multithreaded chat client implementation for concurrent sending and receiving.
 */

#include "chat_client.h"
#include "network.h"
#include "protocol.h"

void *chat_client_send_worker(void *arg)
{
    chat_client_t *client = (chat_client_t *)arg;
    char message[BUFFER_SIZE];
    char payload[MAX_PAYLOAD_SIZE];

    while (client->running) {
        if (fgets(message, sizeof(message), stdin) == NULL) {
            /* User closed stdin (Ctrl+D) */
            break;
        }

        proto_strip_newline(message);
        if (strlen(message) == 0) {
            continue;
        }

        /* Check for exit command */
        if (strcmp(message, "/exit") == 0 || strcmp(message, "/quit") == 0) {
            printf("\nExiting chat session...\n");
            client->running = false;
            shutdown(client->socket_fd, SHUT_RDWR);
            break;
        }

        snprintf(payload, sizeof(payload), "%s: %s\n", client->username, message);

        if (proto_send_all(client->socket_fd, payload, strlen(payload)) < 0) {
            LOG_ERROR("Failed to send message");
            break;
        }
    }

    client->running = false;
    return NULL;
}

void *chat_client_recv_worker(void *arg)
{
    chat_client_t *client = (chat_client_t *)arg;
    char recvbuf[MAX_PAYLOAD_SIZE];
    ssize_t bytes_read;

    while (client->running) {
        memset(recvbuf, 0, sizeof(recvbuf));
        bytes_read = recv(client->socket_fd, recvbuf, sizeof(recvbuf) - 1, 0);

        if (bytes_read <= 0) {
            if (client->running) {
                printf("\n" COLOR_YELLOW "[NOTICE] Disconnected from server." COLOR_RESET "\n");
                client->running = false;
                /* Quick exit on server termination */
                _exit(0);
            }
            break;
        }

        recvbuf[bytes_read] = '\0';
        printf("%s", recvbuf);
        fflush(stdout);
    }

    return NULL;
}

int chat_client_run(chat_client_t *client)
{
    client->socket_fd = net_connect_to_server(client->server_ip, client->port);
    if (client->socket_fd < 0) {
        LOG_ERROR("Unable to establish connection to %s:%d", client->server_ip, client->port);
        return -1;
    }

    client->running = true;
    LOG_INFO("Connected to server! Starting interactive chat session as '%s'.", client->username);
    LOG_INFO("Type your message and press ENTER. Type /exit to quit.");

    pthread_t send_thread, recv_thread;

    if (pthread_create(&send_thread, NULL, chat_client_send_worker, client) != 0) {
        LOG_ERROR("Failed to create send worker thread");
        net_close_socket(&client->socket_fd);
        return -1;
    }

    if (pthread_create(&recv_thread, NULL, chat_client_recv_worker, client) != 0) {
        LOG_ERROR("Failed to create receive worker thread");
        client->running = false;
        pthread_cancel(send_thread);
        net_close_socket(&client->socket_fd);
        return -1;
    }

    pthread_join(send_thread, NULL);
    pthread_join(recv_thread, NULL);

    net_close_socket(&client->socket_fd);
    LOG_INFO("Chat session closed.");
    return 0;
}
