#ifndef distance
#define distance

/* Ultimate client request would have Json paylod like this during Db
 * initilaization
 * {"dimension": 128, "metric": "cosine"}
 * */

/*
 * Define Metric type for every new fuction
 */
typedef enum {
  METRIC_COSINE,
  METRIC_EUCLIDEAN
} MetricType;

/**
 * @brief Calculate cosine similarity between two vectors.
 *
 * Returns a value in [-1, 1] where 1 means identical direction.
 *
 *  @param dimension must be non-NULL
 * @param vec First vector; must be non-NULL with matching dimension.
 * @param query Second vector; must be non-NULL with matching dimension.
 * @return Distance value in [-1, 1].
 */

typedef float (*Distance_func)(int dim, float vec[dim], float query[dim]);

// Router Function

Distance_func get_distance(MetricType metric);

#endif
