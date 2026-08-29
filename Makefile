CC = gcc
TARGET = origin

SRC_DIR = src
SEARCH_DIR = src/search
INC_DIR = include
BUILD_DIR = build

SRCS = $(SRC_DIR)/db.c \
       $(SRC_DIR)/storage.c \
       $(SRC_DIR)/distance.c \
       $(SRC_DIR)/main.c \
       $(SRC_DIR)/terminal.c \
       $(SRC_DIR)/server.c \
       $(SEARCH_DIR)/baseline-flat.c \
       $(SEARCH_DIR)/ivf-flat.c

OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o)

CFLAGS = -Wall -Wextra -I$(INC_DIR) -O3 -march=native -ffast-math -ftree-vectorize

LDFLAGS = -lm

all: clean $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: ./$(TARGET)"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
