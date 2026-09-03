/* Regression tests for src/distance.c
 *
 * Covers: correctness of both metrics against hand-computed values,
 * self-distance (identical vectors), the router function get_distance(),
 * and a documented bug in cosine() -- division by zero (NaN) when either
 * input vector is the zero vector, which is a realistic input (e.g. an
 * all-zero embedding from a failed model call).
 *
 * Build & run: from tests/, `make test`
 */

#include "../include/distance.h"
#include "test_framework.h"

#include <math.h>

#define DIM 3

TEST_CASE(test_euclidean_known_value) {
    /* (3,0,0) vs (0,4,0) -> classic 3-4-5 triangle, distance 5 */
    float a[DIM] = {3, 0, 0};
    float b[DIM] = {0, 4, 0};
    Distance_func dist = get_distance(METRIC_EUCLIDEAN);
    ASSERT_NOT_NULL((void *)dist);
    ASSERT_NEAR(dist(DIM, a, b), 5.0, 1e-5);
}

TEST_CASE(test_euclidean_identical_vectors_is_zero) {
    float a[DIM] = {1.5f, -2.25f, 3.0f};
    Distance_func dist = get_distance(METRIC_EUCLIDEAN);
    ASSERT_NEAR(dist(DIM, a, a), 0.0, 1e-6);
}

TEST_CASE(test_euclidean_symmetric) {
    float a[DIM] = {1, 2, 3};
    float b[DIM] = {4, -1, 0.5f};
    Distance_func dist = get_distance(METRIC_EUCLIDEAN);
    ASSERT_NEAR(dist(DIM, a, b), dist(DIM, b, a), 1e-6);
}

TEST_CASE(test_cosine_identical_direction_is_zero) {
    /* Same direction (even different magnitude) -> cosine distance 0. */
    float a[DIM] = {1, 2, 3};
    float b[DIM] = {2, 4, 6};
    Distance_func dist = get_distance(METRIC_COSINE);
    ASSERT_NEAR(dist(DIM, a, b), 0.0, 1e-5);
}

TEST_CASE(test_cosine_orthogonal_is_one) {
    /* Orthogonal vectors -> similarity 0 -> distance 1 - 0 = 1. */
    float a[DIM] = {1, 0, 0};
    float b[DIM] = {0, 1, 0};
    Distance_func dist = get_distance(METRIC_COSINE);
    ASSERT_NEAR(dist(DIM, a, b), 1.0, 1e-5);
}

TEST_CASE(test_cosine_opposite_direction_is_two) {
    float a[DIM] = {1, 0, 0};
    float b[DIM] = {-1, 0, 0};
    Distance_func dist = get_distance(METRIC_COSINE);
    ASSERT_NEAR(dist(DIM, a, b), 2.0, 1e-5);
}

TEST_CASE(test_cosine_zero_vector_produces_nan) {
    /* BUG (documented, not fixed here): cosine() divides by
     * sqrtf(mod_a)*sqrtf(mod_b) with no guard, so an all-zero vector -- a
     * realistic input, e.g. a failed/empty embedding -- produces 0/0 =
     * NaN instead of a defined error value or a sentinel distance. Any
     * caller that puts this straight into the bounded-insertion sort in
     * flat_search()/db_ann_search() will have `dis < x` comparisons
     * silently evaluate to false for NaN, so the record is neither
     * ranked nor rejected predictably. This test pins the current
     * (undesirable) behaviour so a future fix is a visible, deliberate
     * change to this test, not a silent regression.
     */
    float zero[DIM] = {0, 0, 0};
    float b[DIM] = {1, 2, 3};
    Distance_func dist = get_distance(METRIC_COSINE);
    float result = dist(DIM, zero, b);
    ASSERT_TRUE(isnan(result));
}

TEST_CASE(test_get_distance_router) {
    ASSERT_TRUE(get_distance(METRIC_COSINE) != get_distance(METRIC_EUCLIDEAN));
}

TEST_CASE(test_get_distance_unknown_metric_falls_back_to_cosine) {
    /* get_distance()'s default case falls back to cosine for any value
     * outside the enum -- pin that documented fallback behaviour. */
    Distance_func fallback = get_distance((MetricType)999);
    ASSERT_TRUE(fallback == get_distance(METRIC_COSINE));
}

int main(void) {
    RUN_TEST(test_euclidean_known_value);
    RUN_TEST(test_euclidean_identical_vectors_is_zero);
    RUN_TEST(test_euclidean_symmetric);
    RUN_TEST(test_cosine_identical_direction_is_zero);
    RUN_TEST(test_cosine_orthogonal_is_one);
    RUN_TEST(test_cosine_opposite_direction_is_two);
    RUN_TEST(test_cosine_zero_vector_produces_nan);
    RUN_TEST(test_get_distance_router);
    RUN_TEST(test_get_distance_unknown_metric_falls_back_to_cosine);
    TEST_SUMMARY();
    return g_tests_failed > 0 ? 1 : 0;
}
