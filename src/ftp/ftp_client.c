/**
 * @file ftp_client.c
 * @brief FTP client file downloading implementation.
 */

#include "ftp.h"
#include "protocol.h"

int ftp_client_download_file(int server_fd, const char *remote_filename, const char *local_filename)
{
    char request_buf[MAX_FILENAME_LEN + 2];
    char header_buf[HEADER_BUFFER_SIZE] = {0};
    char buffer[BUFFER_SIZE];
    long file_size = 0;

    /* Step 1: Send requested file name */
    snprintf(request_buf, sizeof(request_buf), "%s\n", remote_filename);
    if (proto_send_all(server_fd, request_buf, strlen(request_buf)) < 0) {
        LOG_ERROR("Failed to send file request to server");
        return -1;
    }

    /* Step 2: Read response header */
    ssize_t n = proto_read_line(server_fd, header_buf, sizeof(header_buf));
    if (n <= 0) {
        LOG_ERROR("Server closed connection unexpectedly or failed to respond");
        return -1;
    }
    proto_strip_newline(header_buf);

    if (strncmp(header_buf, "ERR", 3) == 0) {
        LOG_WARN("Server error: File '%s' does not exist or cannot be read.", remote_filename);
        return -1;
    }

    if (sscanf(header_buf, "OK %ld", &file_size) != 1) {
        LOG_WARN("Malformed server response: '%s'", header_buf);
        return -1;
    }

    LOG_INFO("Server confirmed file '%s' (Size: %ld bytes). Starting download...",
             remote_filename, file_size);

    /* Step 3: Open destination file */
    int out_fd = open(local_filename, O_CREAT | O_WRONLY | O_TRUNC, 0664);
    if (out_fd < 0) {
        LOG_ERROR("Failed to create destination file '%s'", local_filename);
        return -1;
    }

    /* Step 4: Stream incoming bytes to disk */
    long received = 0;
    while (received < file_size) {
        size_t to_read = sizeof(buffer);
        long remaining = file_size - received;
        if (remaining < (long)to_read) {
            to_read = (size_t)remaining;
        }

        ssize_t bytes_in = read(server_fd, buffer, to_read);
        if (bytes_in < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("Network read error during download");
            close(out_fd);
            return -1;
        }
        if (bytes_in == 0) {
            LOG_WARN("Connection ended prematurely before download completed");
            break;
        }

        ssize_t bytes_out = write(out_fd, buffer, (size_t)bytes_in);
        if (bytes_out != bytes_in) {
            LOG_ERROR("Disk write error");
            close(out_fd);
            return -1;
        }

        received += bytes_in;
    }

    close(out_fd);

    if (received == file_size) {
        LOG_INFO("File download completed successfully: '%s' (%ld bytes saved)",
                 local_filename, received);
        return 0;
    } else {
        LOG_WARN("Download incomplete: received %ld of %ld bytes", received, file_size);
        return -1;
    }
}
