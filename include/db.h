/*
* Copyright (c) 2026 Aadityansha Verma. All rights reserved.
* This file is licensed under the Business Source License 1.1.
* See the LICENSE file in the project root for full terms.
*/


#ifndef DB_H
#define DB_H

#include "distance.h"
// #include "storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEFAULT_CAPACITY 1024
typedef struct storage storage_t;

typedef struct cluster cluster_t;
/**
 * @file db.h
 * @brief Interface to  handel DB initialization in src/db.c
 *
 * This module provides the interface for db.c
 * for vector database operations.
 */

typedef struct Record {
  uint64_t id;
  float *vector;
  char *metadata;
  bool is_deleted;
  uint64_t byte_offset;
} Record_t;

typedef struct {
  uint64_t dimension;
  uint64_t capacity;
  uint64_t count;
  Record_t *records;
  Distance_func calculate_distance;
  storage_t *storage;

  uint64_t *pending_deletes; 
    uint64_t pending_count;
} FlatDb_t; // Master struct

typedef struct search {
  uint64_t id;
  float calculated_distance;
  char *metadata;
} SearchResult_t;
/**
 * @brief function signature to initilaize vectordb.
 *
 * @returns pointer to flatdb_t.
 * @param db_name without extension
 * @param dimension must be non-null
 * @param inital_capacity for the user-records.
 * @param metrictype to calculate similarity between two vectors.
 *
 */

FlatDb_t *db_init(const char *db_name, uint64_t dimension,
                  uint64_t initial_capacity, MetricType metric);

/**
 * @brief function signature to insert inside Db.
 *
 * @returns 0 sucess and -1 failuer.
 * @param db pointer
 * @param id of inserted value
 * @param vector of the embeedings
 * @param Mapped meta-Data alongside the vector.
 *
 */

int db_insert(FlatDb_t *db, uint32_t id, float *vector, char *metadata);

int db_ann_search(FlatDb_t *db, float *query_vector, uint64_t top_k,
                  uint64_t nprobe, cluster_t *clusters, uint64_t num_clusters,
                  SearchResult_t *out_results);

/**
 * @brief Opens an existing database. Used by the TCP server.
 * Prevents clients from creating new tables.
 */
FlatDb_t *db_open(const char *db_name);

bool validate_db_name(const char *name);
void db_close(FlatDb_t *db);


int db_delete(FlatDb_t *db, uint64_t id);

#endif
