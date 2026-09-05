CC = gcc
TARGET = origin
TEST_TARGET = run_tests

SRC_DIR = src
SEARCH_DIR = src/search
INC_DIR = include
BUILD_DIR = build
TEST_DIR = tests

CORE_SRCS = $(SRC_DIR)/db.c \
            $(SRC_DIR)/storage.c \
            $(SRC_DIR)/distance.c \
            $(SRC_DIR)/server.c \
            $(SRC_DIR)/parser.c \
            $(SRC_DIR)/cJSON.c \
            $(SRC_DIR)/api-key-generate.c \
            $(SEARCH_DIR)/baseline-flat.c \
            $(SEARCH_DIR)/kmeans.c
CORE_OBJS = $(CORE_SRCS:%.c=$(BUILD_DIR)/%.o)

MAIN_SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/terminal.c
MAIN_OBJS = $(MAIN_SRCS:%.c=$(BUILD_DIR)/%.o)

TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(TEST_SRCS:%.c=$(BUILD_DIR)/%.o)

CFLAGS = -Wall -Wextra -I$(INC_DIR) -O3 -march=native -ffast-math -ftree-vectorize -lpthread
LDFLAGS = -lm

all: clean $(TARGET)

$(TARGET): $(CORE_OBJS) $(MAIN_OBJS)
	$(CC) $(CORE_OBJS) $(MAIN_OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: ./$(TARGET)"

test: clean $(TEST_TARGET)
	@echo "Running test suite..."
	@./$(TEST_TARGET)

$(TEST_TARGET): $(CORE_OBJS) $(TEST_OBJS)
	$(CC) $(CORE_OBJS) $(TEST_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TEST_TARGET)

.PHONY: all clean test
