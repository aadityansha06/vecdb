#ifndef KMEAN_H

#define KMEAN_H
#include "../db.h"
#include <stdint.h>

typedef struct cluster {
  float *centroid_vector;
  uint64_t *record_index;
  uint64_t count;
  uint64_t capacity;
  uint64_t *byte_offsets;
} cluster_t;

typedef struct {
  uint64_t *ids;
  uint64_t count;
} ivf_fetched_t;

typedef struct {
  float dis;
  uint64_t cluster_idx;
} cluster_dist_t;



cluster_t *kmeans_build(Record_t *record, uint64_t count, uint64_t k,
                        uint64_t dimension, uint64_t max_iterations,
                        Distance_func cal_distance);


ivf_fetched_t *ivf_search(cluster_t *cluster, float *query_vector,
                          uint64_t nprobe, uint64_t k, uint64_t dimension,
                          Distance_func calc_distance);


#endif
