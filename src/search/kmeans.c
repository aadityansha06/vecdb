#include "../../include/search/kmeans.h"
#include <stdint.h>
#include <stdlib.h>
// ``
#include <string.h>
#include <sys/types.h>
#define epsilon 0.000001

cluster_t *kmeans_build(Record_t *record, uint64_t count, uint64_t k,
                        uint64_t dimension, uint64_t max_iterations,
                        Distance_func cal_distance) {

  cluster_t *total_cluster = (cluster_t *)malloc(k * sizeof(cluster_t));
  float *old_centroids = (float *)malloc(k * dimension * sizeof(float));

  for (uint64_t i = 0; i < k; i++) {
    uint64_t random_idx = rand() % count;
    total_cluster[i].centroid_vector =
        (float *)malloc(dimension * sizeof(float));
    memcpy(total_cluster[i].centroid_vector, record[random_idx].vector,
           dimension * sizeof(float));
    total_cluster[i].record_index =
        (uint64_t *)malloc(count * sizeof(uint64_t));
    memcpy(&old_centroids[i * dimension], total_cluster[i].centroid_vector,
           dimension * sizeof(float));
  }
  total_cluster->count = 0;
  uint64_t end = 1;
  uint64_t ittrate = 0;
  while (end != 0 && ittrate <= max_iterations) {
    /*                        Assign vectors to their specific cluster
     **/
  assign:
    ittrate++;
    for (uint64_t i = 0; i < k; i++) {
      total_cluster[i].count = 0;
    }
    for (uint64_t i = 0; i < count; i++) {
      float dis = 1e30;
      uint64_t best_cluster = 0;
      for (uint64_t j = 0; j < k; j++) {
        float temp = cal_distance(dimension, total_cluster[j].centroid_vector,
                                  record[i].vector);
        if (dis > temp) {
          dis = temp;
          best_cluster = j;
        }
      }
      total_cluster[best_cluster]
          .record_index[total_cluster[best_cluster].count] = i;
      total_cluster[best_cluster].count++;
    }

    /*                        Mean of  vectors to re-calculate centroids
     **/

    for (uint64_t i = 0; i < k; i++) {
      float *cluster_sum = (float *)calloc(dimension, sizeof(float));
      for (uint64_t j = 0; j < total_cluster[i].count; j++) {

        for (uint64_t d = 0; d < dimension; d++) {
          cluster_sum[d] += record[total_cluster[i].record_index[j]].vector[d];
        }
      }
      for (uint64_t d = 0; d < dimension; d++) {
        if (total_cluster[i].count > 0) {
          total_cluster[i].centroid_vector[d] =
              cluster_sum[d] / total_cluster[i].count;
        }
      }
      free(cluster_sum);
    }

    /*                      Compare old centroid with new updated centroid
     **/

    uint64_t size = dimension * k;
    float *diffrence = (float *)calloc(size, sizeof(float));
    for (uint64_t i = 0; i < k; i++) {
      for (uint64_t d = 0; d < dimension; d++) {
        diffrence[i * dimension + d] = old_centroids[i * dimension + d] -
                                       total_cluster[i].centroid_vector[d];
      }
    }

    for (uint64_t i = 0; i < k; i++) {
      for (uint64_t d = 0; d < dimension; d++) {
        float diff = diffrence[i * dimension + d];
        if (diff < 0) {
          diff = -diff;
        } // absolute value of diffrence
        if (diff > epsilon) {
          for (uint64_t c = 0; c < k; c++) {
            memcpy(&old_centroids[c * dimension],
                   total_cluster[c].centroid_vector, dimension * sizeof(float));
          }
          free(diffrence);
          goto assign;
        }
      }
    }
    free(diffrence);
    free(old_centroids);
    end = 0;
  }
/*                        Map RAM indices to physical disk offsets                                                                               
 **/
for (int i = 0; i < k; i++) {
    total_cluster[i].byte_offsets = (uint64_t *)malloc(total_cluster[i].count * sizeof(uint64_t));
    
    for (uint64_t j = 0; j < total_cluster[i].count; j++) {
        uint64_t ram_idx = total_cluster[i].record_index[j];
        
        uint64_t physical_offset = record[ram_idx].byte_offset;
        
        total_cluster[i].byte_offsets[j] = physical_offset;
    }
}
  return total_cluster;
}

ivf_fetched_t *ivf_search(cluster_t *cluster, float *query_vector,
                          uint64_t nprobe, uint64_t k, uint64_t dimension,
                          Distance_func calc_distance) {
  cluster_dist_t *best_clusters =
      (cluster_dist_t *)malloc(nprobe * sizeof(cluster_dist_t));

  for (uint64_t i = 0; i < nprobe; i++) {
    best_clusters[i].dis = 1e30;
    best_clusters[i].cluster_idx = 0;
  }
  for (uint64_t i = 0; i < k; i++) {
    float tem_dis =
        calc_distance(dimension, cluster[i].centroid_vector, query_vector);

    if (tem_dis < best_clusters[nprobe - 1].dis) {
      uint64_t insert_pos = nprobe - 1;
      while (insert_pos > 0 && tem_dis < best_clusters[insert_pos - 1].dis) {
        best_clusters[insert_pos] = best_clusters[insert_pos - 1];
        insert_pos--;
      }
      best_clusters[insert_pos].dis = tem_dis;
      best_clusters[insert_pos].cluster_idx = i;
    }
  }

  uint64_t total_records = 0;
  for (uint64_t i = 0; i < nprobe; i++) {
    uint64_t c_idx = best_clusters[i].cluster_idx;
    total_records += cluster[c_idx].count;
  }

  ivf_fetched_t *ivf_fetched = (ivf_fetched_t *)malloc(sizeof(ivf_fetched_t));
  ivf_fetched->count = total_records;

  if (total_records > 0) {
    ivf_fetched->ids = (uint64_t *)malloc(total_records * sizeof(uint64_t));

    uint64_t offset = 0;
    for (uint64_t i = 0; i < nprobe; i++) {
      uint64_t c_idx = best_clusters[i].cluster_idx;
      uint64_t c_count = cluster[c_idx].count;

      memcpy(ivf_fetched->ids + offset, cluster[c_idx].byte_offsets,
             c_count * sizeof(uint64_t));
      offset += c_count;
    }
  } else {
    ivf_fetched->ids = NULL;
  }

  free(best_clusters);
  return ivf_fetched;
}



