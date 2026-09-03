/* Regression tests for src/search/baseline-flat.c (flat_search)
 *
 * Covers: correct top-k ordering against euclidean distance, skipping
 * soft-deleted records, top_k larger than the number of live records
 * (unfilled slots must stay at the INFINITY/id=0 sentinel, not read
 * garbage), top_k equal to count, an empty database, and that returned
 * metadata pointers alias the original records rather than being newly
 * allocated (so callers must not free them -- see terminal.c).
 *
 * Build & run: from tests/, `make test`
 */

#include "../include/db.h"
#include "../include/distance.h"
#include "../include/search/baseline-flat.h"
#include "test_framework.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define DIM 2

static Record_t make_record(uint64_t id, float x, float y, const char *meta,
                             bool deleted) {
    Record_t r;
    r.id = id;
    r.vector = (float *)malloc(sizeof(float) * DIM);
    r.vector[0] = x;
    r.vector[1] = y;
    r.metadata = meta ? strdup(meta) : NULL;
    r.is_deleted = deleted;
    r.byte_offset = 0;
    return r;
}

static void free_records(Record_t *recs, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        free(recs[i].vector);
        if (recs[i].metadata) free(recs[i].metadata);
    }
    free(recs);
}

TEST_CASE(test_returns_closest_k_in_order) {
    Record_t *recs = (Record_t *)malloc(sizeof(Record_t) * 4);
    recs[0] = make_record(1, 10, 10, "far", false);
    recs[1] = make_record(2, 0, 1, "near", false);
    recs[2] = make_record(3, 5, 5, "mid", false);
    recs[3] = make_record(4, 0, 0, "closest", false);

    Distance_func dist = get_distance(METRIC_EUCLIDEAN);
    float query[DIM] = {0, 0};
    SearchResult_t results[2];

    ASSERT_TRUE(flat_search(recs, 4, DIM, query, 2, dist, results) == 0);

    ASSERT_EQ_U64(results[0].id, 4); /* (0,0), distance 0 */
    ASSERT_NEAR(results[0].calculated_distance, 0.0, 1e-5);
    ASSERT_EQ_U64(results[1].id, 2); /* (0,1), distance 1 */
    ASSERT_NEAR(results[1].calculated_distance, 1.0, 1e-5);

    free_records(recs, 4);
}

TEST_CASE(test_skips_deleted_records) {
    Record_t *recs = (Record_t *)malloc(sizeof(Record_t) * 2);
    recs[0] = make_record(1, 0, 0, "deleted-but-closest", true);
    recs[1] = make_record(2, 10, 10, "alive", false);

    Distance_func dist = get_distance(METRIC_EUCLIDEAN);
    float query[DIM] = {0, 0};
    SearchResult_t results[1];

    ASSERT_TRUE(flat_search(recs, 2, DIM, query, 1, dist, results) == 0);

    /* The deleted record is the geometrically closer one; it must be
     * skipped so the only live record wins instead. */
    ASSERT_EQ_U64(results[0].id, 2);

    free_records(recs, 2);
}

TEST_CASE(test_top_k_larger_than_live_records_leaves_sentinels) {
    Record_t *recs = (Record_t *)malloc(sizeof(Record_t) * 1);
    recs[0] = make_record(1, 1, 1, "only", false);

    Distance_func dist = get_distance(METRIC_EUCLIDEAN);
    float query[DIM] = {0, 0};
    SearchResult_t results[3];

    ASSERT_TRUE(flat_search(recs, 1, DIM, query, 3, dist, results) == 0);

    ASSERT_EQ_U64(results[0].id, 1);
    /* Unfilled slots must remain at the documented sentinel values
     * (id 0, distance +INFINITY), never uninitialized/garbage data. */
    ASSERT_EQ_U64(results[1].id, 0);
    ASSERT_TRUE(isinf(results[1].calculated_distance));
    ASSERT_EQ_U64(results[2].id, 0);
    ASSERT_TRUE(isinf(results[2].calculated_distance));

    free_records(recs, 1);
}

TEST_CASE(test_top_k_equal_to_count) {
    Record_t *recs = (Record_t *)malloc(sizeof(Record_t) * 3);
    recs[0] = make_record(1, 3, 0, NULL, false);
    recs[1] = make_record(2, 1, 0, NULL, false);
    recs[2] = make_record(3, 2, 0, NULL, false);

    Distance_func dist = get_distance(METRIC_EUCLIDEAN);
    float query[DIM] = {0, 0};
    SearchResult_t results[3];

    ASSERT_TRUE(flat_search(recs, 3, DIM, query, 3, dist, results) == 0);

    ASSERT_EQ_U64(results[0].id, 2); /* distance 1 */
    ASSERT_EQ_U64(results[1].id, 3); /* distance 2 */
    ASSERT_EQ_U64(results[2].id, 1); /* distance 3 */

    free_records(recs, 3);
}

TEST_CASE(test_empty_database) {
    Distance_func dist = get_distance(METRIC_EUCLIDEAN);
    float query[DIM] = {0, 0};
    SearchResult_t results[2];

    /* count == 0: every slot must stay a sentinel, and the function must
     * not dereference a NULL/empty records array. */
    ASSERT_TRUE(flat_search(NULL, 0, DIM, query, 2, dist, results) == 0);
    ASSERT_EQ_U64(results[0].id, 0);
    ASSERT_TRUE(isinf(results[0].calculated_distance));
    ASSERT_EQ_U64(results[1].id, 0);
    ASSERT_TRUE(isinf(results[1].calculated_distance));
}

TEST_CASE(test_metadata_pointer_aliases_original_record) {
    /* flat_search assigns out_results[i].metadata = records[i].metadata
     * directly (no strdup). Callers (see terminal.c) rely on this: they
     * print but never free results[i].metadata. Pin the aliasing so a
     * future change to strdup-by-default doesn't introduce a silent
     * double-free at every call site. */
    Record_t *recs = (Record_t *)malloc(sizeof(Record_t) * 1);
    recs[0] = make_record(1, 0, 0, "hello", false);

    Distance_func dist = get_distance(METRIC_EUCLIDEAN);
    float query[DIM] = {0, 0};
    SearchResult_t results[1];

    ASSERT_TRUE(flat_search(recs, 1, DIM, query, 1, dist, results) == 0);
    ASSERT_TRUE(results[0].metadata == recs[0].metadata);

    free_records(recs, 1);
}

int main(void) {
    RUN_TEST(test_returns_closest_k_in_order);
    RUN_TEST(test_skips_deleted_records);
    RUN_TEST(test_top_k_larger_than_live_records_leaves_sentinels);
    RUN_TEST(test_top_k_equal_to_count);
    RUN_TEST(test_empty_database);
    RUN_TEST(test_metadata_pointer_aliases_original_record);
    TEST_SUMMARY();
    return g_tests_failed > 0 ? 1 : 0;
}
