/**
 * @file client_main.c
 * @brief Main entry point for the Chat Messenger Client.
 */

#include "chat_client.h"
#include "protocol.h"

static int g_client_sock = -1;

static void handle_sigint(int sig)
{
    (void)sig;
    const char msg[] = "\nClosing Client...\n";
    ssize_t w = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)w;
    if (g_client_sock >= 0) {
        close(g_client_sock);
        g_client_sock = -1;
    }
    _exit(0);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, handle_sigint);

    chat_client_t client;
    memset(&client, 0, sizeof(client));
    client.port = DEFAULT_CHAT_PORT;
    snprintf(client.server_ip, sizeof(client.server_ip), "%s", DEFAULT_SERVER_IP);
    snprintf(client.username, sizeof(client.username), "User");

    if (argc >= 2) {
        snprintf(client.server_ip, sizeof(client.server_ip), "%.15s", argv[1]);
    }
    if (argc >= 3) {
        client.port = atoi(argv[2]);
        if (client.port <= 0 || client.port > 65535) {
            client.port = DEFAULT_CHAT_PORT;
        }
    }
    if (argc >= 4) {
        snprintf(client.username, sizeof(client.username), "%.49s", argv[3]);
    }

    /* Prompt interactively if arguments were not provided */
    if (argc < 4) {
        char input[128];

        if (argc < 2) {
            printf("Enter Server IP Address [default %s]: ", DEFAULT_SERVER_IP);
            if (fgets(input, sizeof(input), stdin)) {
                proto_strip_newline(input);
                if (strlen(input) > 0) {
                    snprintf(client.server_ip, sizeof(client.server_ip), "%.15s", input);
                }
            }
        }

        if (argc < 3) {
            printf("Enter Port Number [default %d]: ", DEFAULT_CHAT_PORT);
            if (fgets(input, sizeof(input), stdin)) {
                proto_strip_newline(input);
                if (strlen(input) > 0) {
                    int p = atoi(input);
                    if (p > 0 && p <= 65535) client.port = p;
                }
            }
        }

        if (argc < 4) {
            printf("Enter your Username: ");
            if (fgets(input, sizeof(input), stdin)) {
                proto_strip_newline(input);
                if (strlen(input) > 0) {
                    snprintf(client.username, sizeof(client.username), "%.49s", input);
                }
            }
        }
    }

    int status = chat_client_run(&client);
    return (status == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
