/**
 * @file common.h
 * @brief Common definitions, headers, constants, and utilities for Chat Messenger & FTP.
 */

#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>

#define DEFAULT_CHAT_PORT   8080
#define DEFAULT_FTP_PORT    9000
#define DEFAULT_SERVER_IP   "127.0.0.1"

#define BUFFER_SIZE         1024
#define MAX_PAYLOAD_SIZE    1200
#define MAX_USERNAME_LEN    50
#define MAX_FILENAME_LEN    256
#define MAX_PATH_LEN        512
#define HEADER_BUFFER_SIZE  64

#define STORAGE_DIRECTORY   "storage"

/* Color formatting codes for terminal output */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

#define LOG_INFO(fmt, ...) \
    printf(COLOR_GREEN "[INFO] " COLOR_RESET fmt "\n", ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    fprintf(stderr, COLOR_YELLOW "[WARN] " COLOR_RESET fmt "\n", ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    fprintf(stderr, COLOR_RED "[ERROR] " COLOR_RESET fmt ": %s\n", ##__VA_ARGS__, strerror(errno))

#define LOG_SERVER(fmt, ...) \
    printf(COLOR_CYAN "[SERVER] " COLOR_RESET fmt "\n", ##__VA_ARGS__)

#endif /* COMMON_H */
