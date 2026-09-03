#include "../include/db.h"
#include "../include/storage.h"
// #include <cstddef>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/**
 * @file db.c
 * @brief Interface to  handel Dd func including initialization,
 * insertation,search anddeletion in src/db.c
 *
 * This module provides the implementation of database inside the memory storage
 * including search and deletion for vector database operations.
 */


/**
 * @brief Opens an existing database. Used by the TCP server. 
 * Prevents clients from creating new tables.
 */

FlatDb_t *db_open(const char *db_name, uint64_t dimension, MetricType metric) {
    char folder_path[256];
    snprintf(folder_path, sizeof(folder_path), "origin_data/%s", db_name);

    if (access(folder_path, F_OK) == -1) {
        printf("Server Error: Client attempted to open non-existent table '%s'.\n", db_name);
        return NULL;  
    }

    return db_init(db_name, dimension, 10, metric); 
}





/**
 * @brief function signature to initilaize vectordb.
 *
 * @returns pointer to flatdb_t on sucess or NULL. If memory allocation or
 * storage initialization fails.
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
  Record_t temp_record;

  while (storage_load_record(db->storage, &temp_record, db->dimension) == 1) {
    if (db->count == db->capacity) {

      uint64_t new_capacity = db->capacity * 2;
      Record_t *resize_main_record =
          (Record_t *)realloc(db->records, sizeof(Record_t) * new_capacity);
      if (resize_main_record == NULL) {
        perror("Fatal Error: Failed to Load data due to out of memory \n");
        free(db->records);
        free(db->storage);
        free(db);
        return NULL;

      } else {
        db->records = resize_main_record;
        db->capacity = new_capacity;
      }
    }

    db->records[db->count] = temp_record;
    db->count++;
  }

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
db->records[db->count].byte_offset = storage_current_offset(db->storage);  
  /**
   * @brief Appends a single vector record to the binary storage file on disk.
   *
   * @returns 0 on success, or -1 if the disk write fails.
   * @param storage Pointer to the opaque storage context.
   * @param record Pointer to the specific record in RAM to be written.
   * @param dimension The dimensionality of the vector (needed to calculate byte
   * size).
   *
   */

  int db_write =
      storage_write_record(db->storage, &db->records[db->count], db->dimension);
  if (db_write < 0) {

    perror("Fatal Error: Failed to write record\n");
    free(vec_cpy);
    if (meta_cpy != NULL)
      free(meta_cpy);
    return -1;
  }

  db->count++;

  return 0;
}

int db_ann_search(FlatDb_t *db, float *query_vector, uint64_t top_k, uint64_t nprobe, cluster_t *clusters, uint64_t num_clusters, SearchResult_t *out_results) {
    
    // 1. Initialize out_results (Your exact logic)
    for (uint64_t i = 0; i < top_k; i++) {
        out_results[i].calculated_distance = 1e30; // Using 1e30 as a safe infinity
        out_results[i].id = 0;
        out_results[i].metadata = NULL;
    }

    // 2. Coarse Search: Get the flattened array of disk offsets
    ivf_fetched_t *fetched = ivf_search(clusters, query_vector, nprobe, num_clusters, db->dimension, db->calculate_distance);
    if (fetched == NULL) return -1;

    // 3. Fine Search: Fetch from disk and sort
    for (uint64_t i = 0; i < fetched->count; i++) {
        uint64_t byte_offset = fetched->ids[i]; 
        
        Record_t temp_record;
        
        // Use the fseek function we built to pull ONLY this record into RAM
        if (storage_fetch_by_offset(db->storage, byte_offset, &temp_record, db->dimension) == 1) {
            
            if (temp_record.is_deleted) {
                free(temp_record.vector);
                if (temp_record.metadata) free(temp_record.metadata);
                continue;
            }

            // Your exact distance math
            float dis = db->calculate_distance(db->dimension, temp_record.vector, query_vector);

            // Your exact bounded insertion sort
            if (dis < out_results[top_k - 1].calculated_distance) {
                
                int insert_idx = top_k - 1;
                while (insert_idx > 0 && dis < out_results[insert_idx - 1].calculated_distance) {
                    insert_idx--;
                }

                for (int j = top_k - 1; j > insert_idx; j--) {
                    // Free the metadata we are about to overwrite to prevent leaks
                    if (out_results[j].metadata != NULL) {
                        free(out_results[j].metadata); 
                    }
                    out_results[j] = out_results[j - 1];
                }

                out_results[insert_idx].id = temp_record.id;
                out_results[insert_idx].calculated_distance = dis;
                
                // CRITICAL FIX: Duplicate the string so it survives the temp_record free
                if (temp_record.metadata != NULL) {
                    out_results[insert_idx].metadata = strdup(temp_record.metadata);
                } else {
                    out_results[insert_idx].metadata = NULL;
                }
            } else {
                // If it didn't make the top K, we don't need its metadata
            }

            // Instantly free the temporary record so RAM usage stays perfectly flat
            free(temp_record.vector);
            if (temp_record.metadata) free(temp_record.metadata);
        }
    }
    
    // Clean up the index array
    free(fetched->ids);
    free(fetched);
    
    return 0;
}





