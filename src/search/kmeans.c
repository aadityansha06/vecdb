#include "../../include/search/kmeans.h"
#include <stdint.h>
#include <stdlib.h>

#include <string.h>
#include <sys/types.h>
#define epsilon 0.000001

cluster_t *kmeans_build(Record_t *record, uint64_t count, uint64_t k,
                        uint64_t dimension, uint64_t max_iterations,
                        Distance_func cal_distance) {

  cluster_t *total_cluster = (cluster_t *)malloc(k * sizeof(cluster_t));
  float *old_centroids = (float *)malloc(k * dimension * sizeof(float));

  for (int i = 0; i < k; i++) {
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
  int end = 1;
  uint64_t ittrate = 0;
  while (end != 0 && ittrate <= max_iterations) {
    /*                        Assign vectors to their specific cluster
     **/
  assign:
    ittrate++;
    for (int i = 0; i < k; i++) {
      total_cluster[i].count = 0;
    }
    for (int i = 0; i < count; i++) {
      float dis = 1e30;
      int best_cluster = 0;
      for (int j = 0; j < k; j++) {
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

    for (int i = 0; i < k; i++) {
      float *cluster_sum = (float *)calloc(dimension, sizeof(float));
      for (int j = 0; j < total_cluster[i].count; j++) {

        for (int d = 0; d < dimension; d++) {
          cluster_sum[d] += record[total_cluster[i].record_index[j]].vector[d];
        }
      }
      for (int d = 0; d < dimension; d++) {
        if (total_cluster[i].count > 0) {
          total_cluster[i].centroid_vector[d] =
              cluster_sum[d] / total_cluster[i].count;
        }
      }
      free(cluster_sum);
    }

    /*                      Compare old centroid with new updated centroid
     **/

    int size = dimension * k;
    float *diffrence = (float *)calloc(size, sizeof(float));
    for (int i = 0; i < k; i++) {
      for (int d = 0; d < dimension; d++) {
        diffrence[i * dimension + d] = old_centroids[i * dimension + d] -
                                       total_cluster[i].centroid_vector[d];
      }
    }

    for (int i = 0; i < k; i++) {
      for (int d = 0; d < dimension; d++) {
        float diff = diffrence[i * dimension + d];
        if (diff < 0) {
          diff = -diff;
        } // absolute value of diffrence
        if (diff > epsilon) {
        for (int c = 0; c < k; c++) {
                memcpy(&old_centroids[c * dimension], total_cluster[c].centroid_vector, dimension * sizeof(float));
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

  return total_cluster;
}
