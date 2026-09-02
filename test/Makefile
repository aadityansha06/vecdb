CC = gcc
CFLAGS = -Wall -Wextra -I../include -g -O0
LDFLAGS = -lm

SRC_DIR = ../src
SEARCH_DIR = ../src/search

# Real project sources these tests link against directly (not the
# project's own Makefile/build dir) so a broken top-level build never
# blocks you from running the regression suite.
COMMON_SRCS = $(SRC_DIR)/storage.c $(SRC_DIR)/distance.c $(SEARCH_DIR)/kmeans.c

.PHONY: all test clean

all: test

test_storage: test_storage.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_kmeans: test_kmeans.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: test_storage test_kmeans
	@echo "== storage tests ==" && ./test_storage
	@echo ""
	@echo "== kmeans tests ==" && ./test_kmeans

clean:
	rm -f test_storage test_kmeans
	rm -rf origin_data

.PHONY: test
