/**
 * @file ftp_client_main.c
 * @brief FTP Client main entry point for requesting and downloading files.
 */

#include "ftp.h"
#include "network.h"
#include "protocol.h"

static void print_usage(const char *prog_name)
{
    printf(COLOR_BOLD "Concurrent FTP Client" COLOR_RESET "\n");
    printf("Usage:\n");
    printf("  %s <server_ip> <port> <remote_filename> [local_filename]\n", prog_name);
    printf("  %s <remote_filename>\n\n", prog_name);
    printf("Arguments:\n");
    printf("  server_ip        IP address of FTP server (default: 127.0.0.1)\n");
    printf("  port             Port number where FTP server is listening (default: 9000)\n");
    printf("  remote_filename  Name of the target file hosted on server\n");
    printf("  local_filename   Destination file path (default: downloaded_<remote_filename>)\n\n");
    printf("Examples:\n");
    printf("  %s 127.0.0.1 9000 demo.txt downloaded_demo.txt\n", prog_name);
    printf("  %s 127.0.0.1 9000 demo.txt\n", prog_name);
    printf("  %s demo.txt\n", prog_name);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    char server_ip[INET_ADDRSTRLEN] = DEFAULT_SERVER_IP;
    int port = DEFAULT_FTP_PORT;
    char remote_file[MAX_FILENAME_LEN] = {0};
    char local_file[MAX_PATH_LEN] = {0};

    if (argc >= 5) {
        snprintf(server_ip, sizeof(server_ip), "%.15s", argv[1]);
        port = atoi(argv[2]);
        snprintf(remote_file, sizeof(remote_file), "%s", argv[3]);
        snprintf(local_file, sizeof(local_file), "%s", argv[4]);
    } else if (argc == 4) {
        snprintf(server_ip, sizeof(server_ip), "%.15s", argv[1]);
        port = atoi(argv[2]);
        snprintf(remote_file, sizeof(remote_file), "%s", argv[3]);
        snprintf(local_file, sizeof(local_file), "downloaded_%s", remote_file);
    } else if (argc == 2) {
        /* Single argument convenience: ./bin/ftp_client <filename> */
        snprintf(remote_file, sizeof(remote_file), "%s", argv[1]);
        snprintf(local_file, sizeof(local_file), "downloaded_%s", remote_file);
    } else {
        /* Interactive prompt */
        char input[256];
        printf(COLOR_BOLD "Interactive FTP Download" COLOR_RESET "\n");

        printf("Enter Server IP Address [default %s]: ", DEFAULT_SERVER_IP);
        if (fgets(input, sizeof(input), stdin)) {
            proto_strip_newline(input);
            if (strlen(input) > 0) snprintf(server_ip, sizeof(server_ip), "%.15s", input);
        }

        printf("Enter Server Port [default %d]: ", DEFAULT_FTP_PORT);
        if (fgets(input, sizeof(input), stdin)) {
            proto_strip_newline(input);
            if (strlen(input) > 0) {
                int p = atoi(input);
                if (p > 0 && p <= 65535) port = p;
            }
        }

        printf("Enter remote filename to download (e.g. demo.txt, image.jpg): ");
        if (fgets(input, sizeof(input), stdin)) {
            proto_strip_newline(input);
            if (strlen(input) > 0) snprintf(remote_file, sizeof(remote_file), "%s", input);
        }

        if (strlen(remote_file) == 0) {
            LOG_ERROR("No filename provided");
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }

        printf("Enter destination file path [default downloaded_%s]: ", remote_file);
        if (fgets(input, sizeof(input), stdin)) {
            proto_strip_newline(input);
            if (strlen(input) > 0) snprintf(local_file, sizeof(local_file), "%s", input);
            else snprintf(local_file, sizeof(local_file), "downloaded_%s", remote_file);
        }
    }

    if (port <= 0 || port > 65535) {
        LOG_ERROR("Invalid port number: %d", port);
        return EXIT_FAILURE;
    }

    LOG_INFO("Connecting to FTP Server at %s:%d...", server_ip, port);
    int sock_fd = net_connect_to_server(server_ip, port);
    if (sock_fd < 0) {
        LOG_ERROR("Failed to connect to FTP Server at %s:%d", server_ip, port);
        return EXIT_FAILURE;
    }

    int ret = ftp_client_download_file(sock_fd, remote_file, local_file);
    net_close_socket(&sock_fd);

    return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
