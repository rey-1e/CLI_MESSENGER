/**
 * @file ftp_client_main.c
 * @brief FTP Client main entry point for requesting and downloading files.
 */

#include "ftp.h"
#include "network.h"

static void print_usage(const char *prog_name)
{
    printf(COLOR_BOLD "Concurrent FTP Client" COLOR_RESET "\n");
    printf("Usage:\n");
    printf("  %s <server_ip> <port> <remote_filename> <local_filename>\n\n", prog_name);
    printf("Arguments:\n");
    printf("  server_ip        IP address of FTP server (e.g. 127.0.0.1)\n");
    printf("  port             Port number where FTP server is listening\n");
    printf("  remote_filename  Name of the target file hosted on server\n");
    printf("  local_filename   Destination file path to save locally\n\n");
    printf("Example:\n");
    printf("  %s 127.0.0.1 9000 demo.txt downloaded_demo.txt\n", prog_name);
}

int main(int argc, char *argv[])
{
    if (argc != 5) {
        LOG_WARN("Invalid number of arguments.");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *remote_file = argv[3];
    const char *local_file = argv[4];

    if (port <= 0 || port > 65535) {
        LOG_ERROR("Invalid port number: %s", argv[2]);
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
