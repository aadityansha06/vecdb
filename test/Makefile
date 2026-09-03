CC = gcc
CFLAGS = -Wall -Wextra -I../include -g -O0
LDFLAGS = -lm

SRC_DIR = ../src
SEARCH_DIR = ../src/search

# Real project sources these tests link against directly (not the
# project's own Makefile/build dir) so a broken top-level build never
# blocks you from running the regression suite.
COMMON_SRCS = $(SRC_DIR)/storage.c $(SRC_DIR)/distance.c $(SEARCH_DIR)/kmeans.c
DB_SRCS = $(COMMON_SRCS) $(SRC_DIR)/db.c $(SEARCH_DIR)/baseline-flat.c
PARSER_SRCS = $(SRC_DIR)/parser.c $(SRC_DIR)/cJSON.c

.PHONY: all test clean

all: test

test_storage: test_storage.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_kmeans: test_kmeans.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_distance: test_distance.c $(SRC_DIR)/distance.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_flat_search: test_flat_search.c $(SRC_DIR)/distance.c $(SEARCH_DIR)/baseline-flat.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_parser: test_parser.c $(PARSER_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_db: test_db.c $(DB_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: test_storage test_kmeans test_distance test_flat_search test_parser test_db
	@echo "== storage tests ==" && ./test_storage
	@echo ""
	@echo "== kmeans tests ==" && ./test_kmeans
	@echo ""
	@echo "== distance tests ==" && ./test_distance
	@echo ""
	@echo "== flat_search tests ==" && ./test_flat_search
	@echo ""
	@echo "== parser tests ==" && ./test_parser
	@echo ""
	@echo "== db tests ==" && ./test_db

clean:
	rm -f test_storage test_kmeans test_distance test_flat_search test_parser test_db
	rm -rf origin_data

.PHONY: test
