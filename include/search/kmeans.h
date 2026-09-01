#ifndef KMEAN_H

#define KMEAN_H
#include "../db.h"


typedef struct cluster {
    float *centroid_vector;  // Array of floats (size = dimension)
    uint64_t *record_index;  // Dynamic array of vector IDs belonging to this cluster
    uint64_t count;          // How many IDs are currently in record_index
    uint64_t capacity;       // Max capacity before realloc is needed
} cluster_t;

cluster_t *kmeans_build(Record_t *record,uint64_t count,uint64_t k,uint64_t dimension,uint64_t max_iterations, Distance_func cal_distance);


#endif
