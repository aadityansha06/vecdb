#ifndef DB_H 
#define DB_H

#include "distance.h"
#include "storage.h"

#include <stdint.h>
#include <stdbool.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_CAPACITY 1024

/**
 * @file db.h
 * @brief Interface to  handel DB initialization in src/db.c
 *
 * This module provides the interface for db.c 
 * for vector database operations.
 */

typedef  struct Record{
    uint64_t id;
    float *vector;
    char *metadata;
    bool is_deleted;
}Record_t;


typedef struct{
    uint64_t dimension;
    uint64_t capacity;
    uint64_t count;
    Record_t *records;
    Distance_func calculate_distance;
storage_t *storage;
}FlatDb_t; //Master struct


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

FlatDb_t* db_init(const char *db_name ,uint64_t dimension, uint64_t initial_capacity, MetricType metric);



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



#endif
