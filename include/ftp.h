/**
 * @file ftp.h
 * @brief Concurrent FTP Server and Client interfaces.
 */

#ifndef FTP_H
#define FTP_H

#include "common.h"

#define FTP_ERR_MSG "ERR\n"
#define FTP_OK_FMT  "OK %ld\n"

/**
 * @brief Handles an incoming client request by serving the requested file.
 * 
 * @param client_fd Connected client socket descriptor.
 * @param storage_dir Base directory where files are located.
 * @return 0 on success, or -1 on error.
 */
int ftp_server_serve_file(int client_fd, const char *storage_dir);

/**
 * @brief Requests and downloads a file from the FTP server.
 * 
 * @param server_fd Connected server socket descriptor.
 * @param remote_filename Name of the file on the server.
 * @param local_filename Path where downloaded file should be saved.
 * @return 0 on success, or -1 on failure.
 */
int ftp_client_download_file(int server_fd, const char *remote_filename, const char *local_filename);

#endif /* FTP_H */
