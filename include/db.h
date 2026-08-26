#ifndef DB_H 
#define DB_H

#include "distance.h"
#include <cstdint>
#include <stdbool.h>

#define DEFAULT_CAPACITY 1024

/**
 * @file db.h
 * @brief Interface to  handel DB initialization in src/db.c
 *
 * This module provides the interface for db.c 
 * for vector database operations.
 */

typedef  struct{
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
}FlatDb_t;


/**
 * @brief Function signature to initilaize vectorDb.
 *
 * @returns pointer to FlatDb_t.
 *
 * @param dimension must be non-NULL
 * @param inital_capacity for the user-records.
 * @param metrictype to calculate similarity between two vectors.
 * 
 */

FlatDb_t* db_init(int dimension, int initial_capacity, MetricType metric);


#endif
