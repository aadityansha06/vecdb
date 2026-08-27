#include "../include/db.h"
#include "../include/storage.h"
// #include <cstddef>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/**
 * @file db.c
 * @brief Interface to  handel Dd func including initialization,
 * insertation,search anddeletion in src/db.c
 *
 * This module provides the implementation of database inside the memory storage
 * including search and deletion for vector database operations.
 */

/**
 * @brief function signature to initilaize vectordb.
 *
 * @returns pointer to flatdb_t on sucess or NULL. If memory allocation or storage initialization fails.
 * @param db_name without extension
 * @param dimension must be non-null
 * @param inital_capacity for the user-records.
 * @param metrictype to calculate similarity between two vectors.
 *
 */
FlatDb_t *db_init(const char *db_name, uint64_t dimension,
                  uint64_t initial_capacity, MetricType metric) {

  FlatDb_t *db = (FlatDb_t *)malloc(sizeof(FlatDb_t));
  if (db == NULL) {
    perror("Fatal Error: Failed to allocate database.\n");
    return NULL;
  }
  db->dimension = dimension;
  db->capacity = initial_capacity;
  db->count = 0;

  db->records = (Record_t *)malloc(sizeof(Record_t) * initial_capacity);

  if (db->records == NULL) {
    perror("Fatal Error: Failed to allocate records array.\n");
    free(db); // Clean up the master struct before crashing!
    return NULL;
  }

  storage_t *storage;

  storage = storage_init(db_name);
  if (storage == NULL) {
    perror("Fatal Error: Failed to Open file \n");
    free(db->records);
    free(db);
    return NULL;
  }
    db->storage = storage;
  db->calculate_distance =
      get_distance(metric); // Returning the pointer of static function

  return db;
}

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

int db_insert(FlatDb_t *db, uint32_t id, float *vector, char *metadata) {

  if (db->count == db->capacity) {
    // Handel Out of memory failure
    uint64_t new_capacity = db->capacity * 2;
    Record_t *temp_record =
        (Record_t *)realloc(db->records, sizeof(Record_t) * new_capacity);
    if (temp_record == NULL) {
      perror("Fatal Error: Failed to insert data due to out of memory \n");
      return -1;

    } else {
      db->records = temp_record;
      db->capacity = new_capacity;
    }
  }

  float *vec_cpy = (float *)malloc(db->dimension * sizeof(float));

  if (vec_cpy == NULL) {
    perror("Fatal Error: Failed to allocate vector copy\n");
    return -1;
  }

  memcpy(vec_cpy, vector, sizeof(float) * db->dimension);

  char *meta_cpy = NULL;
  if (metadata != NULL) {
    meta_cpy = strdup(metadata);
    if (meta_cpy == NULL) {
      free(vec_cpy);
      perror("Fatal Error: Failed to allocate metadata copy\n");
      return -1;
    }
  }

  db->records[db->count].id = id;
  db->records[db->count].vector = vec_cpy;
  db->records[db->count].metadata = meta_cpy;
  db->records[db->count].is_deleted = false;


/**
 * @brief Appends a single vector record to the binary storage file on disk.
 *
 * @returns 0 on success, or -1 if the disk write fails.
 * @param storage Pointer to the opaque storage context.
 * @param record Pointer to the specific record in RAM to be written.
 * @param dimension The dimensionality of the vector (needed to calculate byte size).
 * 
 */

   int db_write =storage_write_record(db->storage, &db->records[db->count], db->dimension);


    db->count++;

  return 0;
}
