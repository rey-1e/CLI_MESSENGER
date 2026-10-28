/**
 * @file ftp_server.c
 * @brief Concurrent FTP Server file-serving implementation.
 */

#include "ftp.h"
#include "protocol.h"

int ftp_server_serve_file(int client_fd, const char *storage_dir)
{
    char filename[MAX_FILENAME_LEN] = {0};
    char full_path[MAX_PATH_LEN] = {0};
    char header[HEADER_BUFFER_SIZE] = {0};
    char buffer[BUFFER_SIZE];
    struct stat st;

    /* Step 1: Read requested file name from client */
    ssize_t n = proto_read_line(client_fd, filename, sizeof(filename));
    if (n <= 0) {
        LOG_WARN("FTP: Failed to read filename from client");
        return -1;
    }
    proto_strip_newline(filename);

    /* Basic security check: prevent directory traversal */
    if (strstr(filename, "..") != NULL || filename[0] == '/') {
        LOG_WARN("FTP: Blocked security violation / path traversal attempt: '%s'", filename);
        proto_send_all(client_fd, FTP_ERR_MSG, strlen(FTP_ERR_MSG));
        return -1;
    }

    if (storage_dir && strlen(storage_dir) > 0) {
        snprintf(full_path, sizeof(full_path), "%s/%s", storage_dir, filename);
    } else {
        snprintf(full_path, sizeof(full_path), "%s", filename);
    }

    /* Fallback search if launched from bin/ or a sibling directory */
    if (stat(full_path, &st) < 0 || !S_ISREG(st.st_mode)) {
        char alt_path[MAX_PATH_LEN];
        snprintf(alt_path, sizeof(alt_path), "../%s/%s",
                 (storage_dir && strlen(storage_dir) > 0) ? storage_dir : "storage", filename);
        if (stat(alt_path, &st) == 0 && S_ISREG(st.st_mode)) {
            snprintf(full_path, sizeof(full_path), "%s", alt_path);
        } else {
            snprintf(alt_path, sizeof(alt_path), "%s", filename);
            if (stat(alt_path, &st) == 0 && S_ISREG(st.st_mode)) {
                snprintf(full_path, sizeof(full_path), "%s", alt_path);
            }
        }
    }

    LOG_INFO("FTP: Client requested file: '%s' (Resolved: '%s')", filename, full_path);

    /* Step 2: Check if file exists and get size */
    if (stat(full_path, &st) < 0 || !S_ISREG(st.st_mode)) {
        LOG_WARN("FTP: File '%s' not found or is not a regular file", full_path);
        proto_send_all(client_fd, FTP_ERR_MSG, strlen(FTP_ERR_MSG));
        return -1;
    }

    int fd = open(full_path, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("FTP: Failed to open file '%s'", full_path);
        proto_send_all(client_fd, FTP_ERR_MSG, strlen(FTP_ERR_MSG));
        return -1;
    }

    /* Step 3: Send header with file size */
    snprintf(header, sizeof(header), FTP_OK_FMT, (long)st.st_size);
    if (proto_send_all(client_fd, header, strlen(header)) < 0) {
        LOG_ERROR("FTP: Failed to send header to client");
        close(fd);
        return -1;
    }

    /* Step 4: Stream file contents in chunks */
    long total_sent = 0;
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        if (proto_send_all(client_fd, buffer, (size_t)bytes_read) < 0) {
            LOG_ERROR("FTP: Connection interrupted during transmission");
            close(fd);
            return -1;
        }
        total_sent += bytes_read;
    }

    close(fd);
    LOG_INFO("FTP: Successfully transferred %ld bytes of '%s'.", total_sent, filename);
    return 0;
}
