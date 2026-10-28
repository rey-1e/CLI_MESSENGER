/**
 * @file relay.c
 * @brief Multi-client bidirectional message relay engine.
 */

#include "chat_server.h"
#include "network.h"
#include "protocol.h"

int server_accept_client_pair(int listen_fd, int *client1_fd, int *client2_fd)
{
    struct sockaddr_in addr1, addr2;
    socklen_t len1 = sizeof(addr1);
    socklen_t len2 = sizeof(addr2);

    const char *msg_wait  = "\nConnected as Client 1. Waiting for peer to join...\n";
    const char *msg_c2    = "\nConnected as Client 2. Chat starting!\n";
    const char *msg_start = "\nPeer joined! Chat starting!\n";

    LOG_SERVER("Waiting for Client 1 connection...");
    *client1_fd = accept(listen_fd, (struct sockaddr *)&addr1, &len1);
    if (*client1_fd < 0) {
        if (errno == EINTR) return -1;
        LOG_ERROR("Failed to accept Client 1");
        return -1;
    }
    char ip1[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr1.sin_addr, ip1, sizeof(ip1));
    LOG_SERVER("Client 1 connected from %s:%d", ip1, ntohs(addr1.sin_port));
    proto_send_all(*client1_fd, msg_wait, strlen(msg_wait));

    LOG_SERVER("Waiting for Client 2 connection...");
    *client2_fd = accept(listen_fd, (struct sockaddr *)&addr2, &len2);
    if (*client2_fd < 0) {
        if (errno == EINTR) {
            net_close_socket(client1_fd);
            return -1;
        }
        LOG_ERROR("Failed to accept Client 2");
        net_close_socket(client1_fd);
        return -1;
    }
    char ip2[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr2.sin_addr, ip2, sizeof(ip2));
    LOG_SERVER("Client 2 connected from %s:%d", ip2, ntohs(addr2.sin_port));

    proto_send_all(*client2_fd, msg_c2, strlen(msg_c2));
    proto_send_all(*client1_fd, msg_start, strlen(msg_start));

    return 0;
}

void *server_relay_worker(void *arg)
{
    relay_args_t *args = (relay_args_t *)arg;
    int sender = args->sender_fd;
    int receiver = args->receiver_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while (*(args->session_active)) {
        bytes_read = recv(sender, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) {
            /* Client disconnected or error occurred */
            break;
        }

        /* Forward to receiver */
        if (proto_send_all(receiver, buffer, bytes_read) < 0) {
            break;
        }
    }

    if (*(args->session_active)) {
        *(args->session_active) = false;
        LOG_SERVER("%s disconnected.", args->sender_tag);
        const char *notify = "\n[SERVER] Peer disconnected from chat.\n";
        proto_send_all(receiver, notify, strlen(notify));
        shutdown(receiver, SHUT_RDWR);
    }

    return NULL;
}

void server_start_relay_session(int client1_fd, int client2_fd)
{
    pthread_t thread1, thread2;
    volatile bool session_active = true;

    relay_args_t args1 = {
        .sender_fd = client1_fd,
        .receiver_fd = client2_fd,
        .sender_tag = "Client 1",
        .session_active = &session_active
    };

    relay_args_t args2 = {
        .sender_fd = client2_fd,
        .receiver_fd = client1_fd,
        .sender_tag = "Client 2",
        .session_active = &session_active
    };

    LOG_SERVER("Relay active. Bridging Client 1 <--> Client 2...");

    if (pthread_create(&thread1, NULL, server_relay_worker, &args1) != 0) {
        LOG_ERROR("Failed to spawn relay thread 1");
    }

    if (pthread_create(&thread2, NULL, server_relay_worker, &args2) != 0) {
        LOG_ERROR("Failed to spawn relay thread 2");
    }

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    LOG_SERVER("Session terminated. Cleaning up client sockets.\n");
    net_close_socket(&client1_fd);
    net_close_socket(&client2_fd);
}
