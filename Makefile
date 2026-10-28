# ==============================================================================
# Makefile for Chat Messenger & Concurrent FTP System
# ==============================================================================

CC         ?= gcc
CFLAGS     ?= -Wall -Wextra -pedantic -pthread -Iinclude -O2
LDFLAGS    ?= -pthread

BIN_DIR    := bin
BUILD_DIR  := build
SRC_DIR    := src
INC_DIR    := include

# Common Object Files
COMMON_SRCS := $(wildcard $(SRC_DIR)/common/*.c)
COMMON_OBJS := $(patsubst $(SRC_DIR)/common/%.c, $(BUILD_DIR)/common/%.o, $(COMMON_SRCS))

# Chat Server Objects
SERVER_SRCS := $(wildcard $(SRC_DIR)/server/*.c)
SERVER_OBJS := $(patsubst $(SRC_DIR)/server/%.c, $(BUILD_DIR)/server/%.o, $(SERVER_SRCS))

# Chat Client Objects
CLIENT_SRCS := $(wildcard $(SRC_DIR)/client/*.c)
CLIENT_OBJS := $(patsubst $(SRC_DIR)/client/%.c, $(BUILD_DIR)/client/%.o, $(CLIENT_SRCS))

# FTP Server Objects
FTP_SERVER_SRCS := $(SRC_DIR)/ftp/ftp_server.c $(SRC_DIR)/ftp/ftp_server_main.c
FTP_SERVER_OBJS := $(BUILD_DIR)/ftp/ftp_server.o $(BUILD_DIR)/ftp/ftp_server_main.o

# FTP Client Objects
FTP_CLIENT_SRCS := $(SRC_DIR)/ftp/ftp_client.c $(SRC_DIR)/ftp/ftp_client_main.c
FTP_CLIENT_OBJS := $(BUILD_DIR)/ftp/ftp_client.o $(BUILD_DIR)/ftp/ftp_client_main.o

# CTM CLI & Daemon Objects
CTM_SRCS := $(wildcard $(SRC_DIR)/ctm/*.c)
CTM_OBJS := $(patsubst $(SRC_DIR)/ctm/%.c, $(BUILD_DIR)/ctm/%.o, $(CTM_SRCS))

# Output Binaries
CHAT_SERVER_BIN := $(BIN_DIR)/chat_server
CHAT_CLIENT_BIN := $(BIN_DIR)/chat_client
FTP_SERVER_BIN  := $(BIN_DIR)/ftp_server
FTP_CLIENT_BIN  := $(BIN_DIR)/ftp_client
CTM_BIN         := $(BIN_DIR)/ctm

TARGETS := $(CHAT_SERVER_BIN) $(CHAT_CLIENT_BIN) $(FTP_SERVER_BIN) $(FTP_CLIENT_BIN) $(CTM_BIN)

.PHONY: all chat ftp ctm clean help directories install

all: directories $(TARGETS)
	@echo "Build complete. All binaries available in '$(BIN_DIR)/'."

chat: directories $(CHAT_SERVER_BIN) $(CHAT_CLIENT_BIN)
	@echo "Chat Messenger build complete."

ftp: directories $(FTP_SERVER_BIN) $(FTP_CLIENT_BIN)
	@echo "Concurrent FTP System build complete."

ctm: directories $(CTM_BIN)
	@echo "CTM CLI build complete."

directories:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(BUILD_DIR)/common $(BUILD_DIR)/server $(BUILD_DIR)/client $(BUILD_DIR)/ftp $(BUILD_DIR)/ctm
	@mkdir -p downloads storage

# Compilation rules for object files
$(BUILD_DIR)/common/%.o: $(SRC_DIR)/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/server/%.o: $(SRC_DIR)/server/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/client/%.o: $(SRC_DIR)/client/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ftp/%.o: $(SRC_DIR)/ftp/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ctm/%.o: $(SRC_DIR)/ctm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Linking rules for binaries
$(CHAT_SERVER_BIN): $(SERVER_OBJS) $(COMMON_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@
	@echo "Built: $@"

$(CHAT_CLIENT_BIN): $(CLIENT_OBJS) $(COMMON_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@
	@echo "Built: $@"

$(FTP_SERVER_BIN): $(FTP_SERVER_OBJS) $(COMMON_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@
	@echo "Built: $@"

$(FTP_CLIENT_BIN): $(FTP_CLIENT_OBJS) $(COMMON_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@
	@echo "Built: $@"

$(CTM_BIN): $(CTM_OBJS) $(COMMON_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@
	@echo "Built: $@"

install: $(CTM_BIN)
	@mkdir -p $(HOME)/.local/bin
	@install -m 755 $(CTM_BIN) $(HOME)/.local/bin/ctm
	@echo "Installed 'ctm' to $(HOME)/.local/bin/ctm."

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Cleaned build artifacts and binaries."

help:
	@echo "Usage:"
	@echo "  make          Build all binaries (chat, ftp, and ctm)"
	@echo "  make ctm      Build CTM Git-style CLI tool"
	@echo "  make chat     Build Chat Messenger server and client"
	@echo "  make ftp      Build Concurrent FTP server and client"
	@echo "  make install  Install 'ctm' to ~/.local/bin/ctm"
	@echo "  make clean    Remove object files and binaries"
	@echo "  make help     Show this help message"
