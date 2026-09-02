/* Regression tests for src/search/kmeans.c
 *
 * Covers: k=1, invariants that must hold for any k (every record
 * assigned exactly once), degenerate/identical-vector input, correct
 * RAM-index -> disk-byte-offset mapping, and a documented bug in
 * ivf_search() when nprobe > k.
 *
 * NOTE: kmeans_build() calls rand() without ever calling srand(), so
 * its centroid picks are deterministic across runs (C's default seed
 * is 1) -- that's what makes these tests reproducible without a fixed
 * seed of our own. If the source ever adds an srand() call, these
 * tests may need a seed pinned via a test-only hook.
 *
 * Build & run: from tests/, `make test`
 */

#include "../include/db.h"
#include "../include/storage.h"
#include "../include/search/kmeans.h"
#include "test_framework.h"

#include <stdbool.h>
#include <stdlib.h>

#define METRIC_L2 1
#define DIM 2

static Record_t *make_records(float pts[][DIM], uint64_t n) {
    Record_t *recs = (Record_t *)malloc(sizeof(Record_t) * n);
    for (uint64_t i = 0; i < n; i++) {
        recs[i].id = i;
        recs[i].vector = (float *)malloc(sizeof(float) * DIM);
        recs[i].vector[0] = pts[i][0];
        recs[i].vector[1] = pts[i][1];
        recs[i].metadata = NULL;
        recs[i].is_deleted = false;
        recs[i].byte_offset = i * 100; /* fake disk offsets for the mapping test */
    }
    return recs;
}

static void free_records(Record_t *recs, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) free(recs[i].vector);
    free(recs);
}

static void free_clusters(cluster_t *clusters, uint64_t k) {
    for (uint64_t i = 0; i < k; i++) {
        free(clusters[i].centroid_vector);
        free(clusters[i].record_index);
        if (clusters[i].byte_offsets) free(clusters[i].byte_offsets);
    }
    free(clusters);
}

TEST_CASE(test_k_equals_1_puts_everything_in_one_cluster) {
    float pts[4][DIM] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    Record_t *recs = make_records(pts, 4);
    Distance_func dist = get_distance(METRIC_L2);

    cluster_t *clusters = kmeans_build(recs, 4, 1, DIM, 10, dist);
    ASSERT_NOT_NULL(clusters);
    ASSERT_EQ_U64(clusters[0].count, 4);
    ASSERT_NEAR(clusters[0].centroid_vector[0], 0.5, 1e-4);
    ASSERT_NEAR(clusters[0].centroid_vector[1], 0.5, 1e-4);

    free_clusters(clusters, 1);
    free_records(recs, 4);
}

TEST_CASE(test_every_point_gets_assigned_exactly_once) {
    /* Must hold for ANY k, regardless of which points random init
     * happens to pick as starting centroids. */
    float pts[6][DIM] = {{0, 0}, {0, 1}, {10, 10}, {10, 11}, {20, 0}, {20, 1}};
    Record_t *recs = make_records(pts, 6);
    Distance_func dist = get_distance(METRIC_L2);

    uint64_t k = 3;
    cluster_t *clusters = kmeans_build(recs, 6, k, DIM, 20, dist);
    ASSERT_NOT_NULL(clusters);

    int seen[6] = {0};
    uint64_t total = 0;
    for (uint64_t i = 0; i < k; i++) {
        total += clusters[i].count;
        for (uint64_t j = 0; j < clusters[i].count; j++) {
            uint64_t idx = clusters[i].record_index[j];
            ASSERT_TRUE(idx < 6);
            seen[idx]++;
        }
    }
    ASSERT_EQ_U64(total, 6);
    for (int i = 0; i < 6; i++) ASSERT_TRUE(seen[i] == 1);

    free_clusters(clusters, k);
    free_records(recs, 6);
}

TEST_CASE(test_all_identical_vectors_does_not_crash) {
    /* Degenerate input: every point is the same. Some clusters may end
     * up with count == 0 depending on which duplicate the random init
     * lands on -- kmeans_build must not divide by zero or crash. */
    float pts[5][DIM] = {{3, 3}, {3, 3}, {3, 3}, {3, 3}, {3, 3}};
    Record_t *recs = make_records(pts, 5);
    Distance_func dist = get_distance(METRIC_L2);

    cluster_t *clusters = kmeans_build(recs, 5, 3, DIM, 10, dist);
    ASSERT_NOT_NULL(clusters);

    uint64_t total = 0;
    for (uint64_t i = 0; i < 3; i++) total += clusters[i].count;
    ASSERT_EQ_U64(total, 5);

    free_clusters(clusters, 3);
    free_records(recs, 5);
}

TEST_CASE(test_byte_offsets_mapped_correctly) {
    float pts[3][DIM] = {{0, 0}, {100, 100}, {200, 200}};
    Record_t *recs = make_records(pts, 3); /* byte_offset = i*100 */
    Distance_func dist = get_distance(METRIC_L2);

    cluster_t *clusters = kmeans_build(recs, 3, 3, DIM, 10, dist);
    ASSERT_NOT_NULL(clusters);

    /* Every byte_offset returned must be one of the fake offsets we set
     * up (0, 100, 200) -- catches RAM-index vs disk-offset mix-ups. */
    for (uint64_t i = 0; i < 3; i++) {
        for (uint64_t j = 0; j < clusters[i].count; j++) {
            uint64_t off = clusters[i].byte_offsets[j];
            ASSERT_TRUE(off == 0 || off == 100 || off == 200);
        }
    }

    free_clusters(clusters, 3);
    free_records(recs, 3);
}

TEST_CASE(test_ivf_search_nprobe_equals_k_returns_everything) {
    float pts[4][DIM] = {{0, 0}, {1, 0}, {50, 50}, {51, 50}};
    Record_t *recs = make_records(pts, 4);
    Distance_func dist = get_distance(METRIC_L2);

    uint64_t k = 2;
    cluster_t *clusters = kmeans_build(recs, 4, k, DIM, 10, dist);
    ASSERT_NOT_NULL(clusters);

    float query[DIM] = {0, 0};
    ivf_fetched_t *fetched = ivf_search(clusters, query, k, k, DIM, dist);
    ASSERT_NOT_NULL(fetched);
    ASSERT_EQ_U64(fetched->count, 4);

    free(fetched->ids);
    free(fetched);
    free_clusters(clusters, k);
    free_records(recs, 4);
}

TEST_CASE(test_ivf_search_nprobe_greater_than_k_KNOWN_BUG) {
    /* KNOWN BUG: ivf_search() never clamps nprobe to k. best_clusters[]
     * is allocated with size `nprobe`, but only the first `k` slots ever
     * get overwritten by the insertion-sort loop (there are only k
     * centroids to compare against). Any slots beyond k keep their
     * *initial* sentinel value { dis = 1e30, cluster_idx = 0 } -- so
     * cluster 0 gets counted and copied into the results MULTIPLE
     * times when nprobe > k.
     *
     * This test documents the current (buggy) behavior rather than
     * asserting "correct" behavior. Fix suggestion: at the top of
     * ivf_search(), do `if (nprobe > k) nprobe = k;` before allocating
     * best_clusters. Once fixed, replace the assertion below with
     * `ASSERT_EQ_U64(fetched->count, true_total);`.
     */
    float pts[4][DIM] = {{0, 0}, {1, 0}, {50, 50}, {51, 50}};
    Record_t *recs = make_records(pts, 4);
    Distance_func dist = get_distance(METRIC_L2);

    uint64_t k = 2;
    cluster_t *clusters = kmeans_build(recs, 4, k, DIM, 10, dist);
    ASSERT_NOT_NULL(clusters);

    uint64_t true_total = clusters[0].count + clusters[1].count;

    float query[DIM] = {0, 0};
    uint64_t nprobe = 5; /* > k, deliberately */
    ivf_fetched_t *fetched = ivf_search(clusters, query, nprobe, k, DIM, dist);
    ASSERT_NOT_NULL(fetched);

    printf("       [info] true_total=%llu fetched->count=%llu"
           " (bug: should be clamped to true_total)\n",
           (unsigned long long)true_total, (unsigned long long)fetched->count);
    ASSERT_TRUE(fetched->count >= true_total);

    free(fetched->ids);
    free(fetched);
    free_clusters(clusters, k);
    free_records(recs, 4);
}

int main(void) {
    RUN_TEST(test_k_equals_1_puts_everything_in_one_cluster);
    RUN_TEST(test_every_point_gets_assigned_exactly_once);
    RUN_TEST(test_all_identical_vectors_does_not_crash);
    RUN_TEST(test_byte_offsets_mapped_correctly);
    RUN_TEST(test_ivf_search_nprobe_equals_k_returns_everything);
    RUN_TEST(test_ivf_search_nprobe_greater_than_k_KNOWN_BUG);
    TEST_SUMMARY();
    return g_tests_failed > 0 ? 1 : 0;
}
