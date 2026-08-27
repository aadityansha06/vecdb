#include "../include/db.h"
#include "../include/storage.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
/**
 * @file db.c
 * @brief Interface to  handel Dd func including initialization, 
 * insertation,search anddeletion in src/db.c
 *
 * This module provides the implementation of database inside the memory storage including search and deletion
 * for vector database operations.
 */


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
FlatDb_t* db_init( const char *db_name ,uint64_t dimension, uint64_t initial_capacity, MetricType metric){

    FlatDb_t *db = (FlatDb_t *)malloc(sizeof(FlatDb_t));
    if (db==NULL) {
         perror("Fatal Error: Failed to allocate database.\n");
         return NULL;
    }
    db->dimension = dimension;
    db->capacity = initial_capacity;
    db->count=0;


   db->records = (Record_t*)malloc(sizeof(Record_t)*initial_capacity);

   if (db->records == NULL) {
        perror("Fatal Error: Failed to allocate records array.\n");
        free(db); // Clean up the master struct before crashing!
        return NULL;
    }
   
        storage_t *storage;

        storage=storage_init(db_name);
        if (storage==NULL) {
              perror("Fatal Error: Failed to Open file \n");
              free(db->records);
              free(db);  
        }

        db->calculate_distance = get_distance(metric); // Returning the pointer of static function
                                                  
        return db;
}

