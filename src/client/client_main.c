/**
 * @file client_main.c
 * @brief Main entry point for the Chat Messenger Client.
 */

#include "chat_client.h"
#include "protocol.h"

int g_client_sock = -1;

static void handle_sigint(int sig)
{
    (void)sig;
    const char msg[] = "\nClosing Client...\n";
    ssize_t w = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)w;
    if (g_client_sock >= 0) {
        shutdown(g_client_sock, SHUT_RDWR);
        close(g_client_sock);
        g_client_sock = -1;
    }
    _exit(0);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, handle_sigint);

    chat_client_t client;
    memset(&client, 0, sizeof(client));
    client.port = DEFAULT_CHAT_PORT;
    snprintf(client.server_ip, sizeof(client.server_ip), "%s", DEFAULT_SERVER_IP);
    snprintf(client.username, sizeof(client.username), "User");

    bool need_ip_prompt = false;
    bool need_port_prompt = false;
    bool need_user_prompt = false;

    if (argc >= 4) {
        snprintf(client.server_ip, sizeof(client.server_ip), "%.15s", argv[1]);
        client.port = atoi(argv[2]);
        if (client.port <= 0 || client.port > 65535) client.port = DEFAULT_CHAT_PORT;
        snprintf(client.username, sizeof(client.username), "%.49s", argv[3]);
    } else if (argc == 3) {
        snprintf(client.server_ip, sizeof(client.server_ip), "%.15s", argv[1]);
        client.port = atoi(argv[2]);
        if (client.port <= 0 || client.port > 65535) client.port = DEFAULT_CHAT_PORT;
        need_user_prompt = true;
    } else if (argc == 2) {
        /* If argv[1] contains no dots and is not "localhost", interpret it directly as username */
        if (strchr(argv[1], '.') == NULL && strcmp(argv[1], "localhost") != 0) {
            snprintf(client.username, sizeof(client.username), "%.49s", argv[1]);
            /* Uses default IP and Port without extra prompting */
        } else {
            snprintf(client.server_ip, sizeof(client.server_ip), "%.15s", argv[1]);
            need_port_prompt = true;
            need_user_prompt = true;
        }
    } else {
        need_ip_prompt = true;
        need_port_prompt = true;
        need_user_prompt = true;
    }

    /* Prompt interactively for missing arguments */
    char input[128];
    if (need_ip_prompt) {
        printf("Enter Server IP Address [default %s]: ", DEFAULT_SERVER_IP);
        if (fgets(input, sizeof(input), stdin)) {
            proto_strip_newline(input);
            if (strlen(input) > 0) {
                snprintf(client.server_ip, sizeof(client.server_ip), "%.15s", input);
            }
        }
    }

    if (need_port_prompt) {
        printf("Enter Port Number [default %d]: ", DEFAULT_CHAT_PORT);
        if (fgets(input, sizeof(input), stdin)) {
            proto_strip_newline(input);
            if (strlen(input) > 0) {
                int p = atoi(input);
                if (p > 0 && p <= 65535) client.port = p;
            }
        }
    }

    if (need_user_prompt) {
        printf("Enter your Username [default %s]: ", client.username);
        if (fgets(input, sizeof(input), stdin)) {
            proto_strip_newline(input);
            if (strlen(input) > 0) {
                snprintf(client.username, sizeof(client.username), "%.49s", input);
            }
        }
    }

    /* Mandatory Employee Browser Authentication Gate */
    printf("\n" COLOR_BOLD "========================================================================" COLOR_RESET "\n");
    printf(COLOR_CYAN COLOR_BOLD "  CORPORATE SECURITY: EMPLOYEE AUTHENTICATION SESSION" COLOR_RESET "\n");
    printf(COLOR_BOLD "========================================================================" COLOR_RESET "\n");
    printf("  To protect corporate privacy and communication integrity, each session\n");
    printf("  requires browser-based identity confirmation with your Employee ID.\n");

    auth_session_t auth_session;
    if (auth_browser_login(client.username, &auth_session) != 0) {
        printf("\n" COLOR_RED "[SECURITY ALERT] Employee authentication failed or was cancelled." COLOR_RESET "\n");
        printf(COLOR_RED "[ACCESS DENIED] Non-employees cannot access the chat service.\n\n" COLOR_RESET);
        return EXIT_FAILURE;
    }

    /* Attach confirmed employee identity */
    snprintf(client.username, sizeof(client.username), "%.30s (%.15s)",
             auth_session.employee_name, auth_session.employee_id);
    client.auth_session = auth_session;
    LOG_INFO("Access granted for employee '%s' [%s]. Initializing relay connection...",
             client.username, auth_session.department);

    int status = chat_client_run(&client);
    return (status == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
