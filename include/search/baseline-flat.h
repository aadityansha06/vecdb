#ifndef BASE_LINE_H
#define BASE_LINE_H
/*
 * implementation of ENN to fid exact near value
 * */

#include "../db.h"


/**
 * @brief Performs an exact nearest neighbor (Flat) search across all records.
 *
 * @param records The array of all database records
 * @param count The total number of active records to search
 * @param dimension The dimensionality of the vectors
 * @param query_vector The vector the user is searching for
 * @param top_k How many results to return (e.g., top 5 closest)
 * @param calculate_distance The function pointer to your distance math
 * @param out_results An array to store the winning IDs and distances
 *
 * @returns 0 on success, -1 on failure
 */
int flat_search(Record_t *records, uint64_t count, uint64_t dimension, float *query_vector, uint64_t top_k, Distance_func calculate_distance, SearchResult_t *out_results);

#endif
