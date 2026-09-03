/* Regression tests for src/db.c
 *
 * Covers: db_init on a fresh table, capacity growth on insert past the
 * initial capacity, NULL metadata, persistence across a reopen (db_init
 * reloading records from disk), db_open's guardrail against opening a
 * table that was never created, and db_ann_search's disk fetch + bounded
 * insertion sort + soft-delete skipping, using a single manually-built
 * cluster covering the whole table (kmeans itself has its own test
 * suite in test_kmeans.c).
 *
 * Build & run: from tests/, `make test`
 *
 * NOTE ON LEAKS: db.c/storage.c currently expose no db_free() or
 * storage_close() at all -- every db_init()/db_open() call leaks the
 * FlatDb_t, its records array, and the storage_t (confirmed with
 * -fsanitize=address). These tests don't attempt to work around that by
 * reaching into storage_t's internals (it's intentionally opaque outside
 * storage.c); this comment exists so the gap is documented rather than
 * silently hidden. Worth adding a real destructor before this project
 * sees any long-running or repeated-use scenario.
 */

#include "../include/db.h"
#include "../include/distance.h"
#include "../include/search/kmeans.h"
#include "test_framework.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define DIM 3
#define TEST_TABLE "regression_db"

static void cleanup_table(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf origin_data/%s", TEST_TABLE);
    system(cmd);
}

static float *vec3(float x, float y, float z) {
    float *v = (float *)malloc(sizeof(float) * DIM);
    v[0] = x; v[1] = y; v[2] = z;
    return v;
}

TEST_CASE(test_db_init_fresh_table) {
    cleanup_table();
    FlatDb_t *db = db_init(TEST_TABLE, DIM, 4, METRIC_EUCLIDEAN);
    ASSERT_NOT_NULL(db);
    ASSERT_EQ_U64(db->dimension, DIM);
    ASSERT_EQ_U64(db->capacity, 4);
    ASSERT_EQ_U64(db->count, 0);
    ASSERT_NOT_NULL((void *)db->calculate_distance);
}

TEST_CASE(test_db_insert_and_capacity_growth) {
    cleanup_table();
    /* capacity 2, insert 3 records to force a realloc */
    FlatDb_t *db = db_init(TEST_TABLE, DIM, 2, METRIC_EUCLIDEAN);
    ASSERT_NOT_NULL(db);

    for (uint32_t i = 0; i < 3; i++) {
        float *v = vec3((float)i, (float)i, (float)i);
        int rc = db_insert(db, i, v, NULL);
        free(v); /* db_insert copies the vector internally */
        ASSERT_TRUE(rc == 0);
    }

    ASSERT_EQ_U64(db->count, 3);
    ASSERT_TRUE(db->capacity >= 3);
    ASSERT_EQ_U64(db->records[2].id, 2);
    ASSERT_NEAR(db->records[2].vector[0], 2.0, 1e-6);
}

TEST_CASE(test_db_insert_null_metadata) {
    cleanup_table();
    FlatDb_t *db = db_init(TEST_TABLE, DIM, 4, METRIC_EUCLIDEAN);
    float *v = vec3(1, 1, 1);
    ASSERT_TRUE(db_insert(db, 1, v, NULL) == 0);
    free(v);
    ASSERT_TRUE(db->records[0].metadata == NULL);
}

TEST_CASE(test_db_insert_copies_metadata_string) {
    cleanup_table();
    FlatDb_t *db = db_init(TEST_TABLE, DIM, 4, METRIC_EUCLIDEAN);
    char meta[] = "mutable-buffer";
    float *v = vec3(1, 1, 1);
    ASSERT_TRUE(db_insert(db, 1, v, meta) == 0);
    free(v);

    /* Mutate the caller's buffer after insert -- the stored copy must be
     * unaffected if db_insert really duplicated the string. */
    meta[0] = 'X';
    ASSERT_NOT_NULL(db->records[0].metadata);
    ASSERT_TRUE(strcmp(db->records[0].metadata, "mutable-buffer") == 0);
}

TEST_CASE(test_db_persists_and_reloads_records) {
    cleanup_table();
    FlatDb_t *db1 = db_init(TEST_TABLE, DIM, 4, METRIC_EUCLIDEAN);
    for (uint32_t i = 0; i < 5; i++) {
        float *v = vec3((float)i, (float)i * 2, (float)i * 3);
        db_insert(db1, 100 + i, v, "meta");
        free(v);
    }

    /* Re-init on the same table name simulates restarting the process;
     * db_init must replay the on-disk records back into RAM. */
    FlatDb_t *db2 = db_init(TEST_TABLE, DIM, 4, METRIC_EUCLIDEAN);
    ASSERT_NOT_NULL(db2);
    ASSERT_EQ_U64(db2->count, 5);
    for (uint32_t i = 0; i < 5; i++) {
        ASSERT_EQ_U64(db2->records[i].id, 100 + i);
        ASSERT_NEAR(db2->records[i].vector[1], (double)(i * 2), 1e-6);
    }
}

TEST_CASE(test_db_open_rejects_nonexistent_table) {
    cleanup_table(); /* make sure it really doesn't exist */
    FlatDb_t *db = db_open(TEST_TABLE, DIM, METRIC_EUCLIDEAN);
    ASSERT_TRUE(db == NULL);
}

TEST_CASE(test_db_open_succeeds_after_init) {
    cleanup_table();
    FlatDb_t *created = db_init(TEST_TABLE, DIM, 4, METRIC_EUCLIDEAN);
    ASSERT_NOT_NULL(created);
    float *v = vec3(1, 2, 3);
    db_insert(created, 1, v, NULL);
    free(v);

    FlatDb_t *opened = db_open(TEST_TABLE, DIM, METRIC_EUCLIDEAN);
    ASSERT_NOT_NULL(opened);
    ASSERT_EQ_U64(opened->count, 1);
}

TEST_CASE(test_db_ann_search_orders_by_true_distance) {
    cleanup_table();
    FlatDb_t *db = db_init(TEST_TABLE, DIM, 8, METRIC_EUCLIDEAN);

    float *v0 = vec3(0, 0, 0);    /* id 1: closest */
    float *v1 = vec3(1, 0, 0);    /* id 2: second closest */
    float *v2 = vec3(10, 10, 10); /* id 3: far away */
    db_insert(db, 1, v0, "closest");
    db_insert(db, 2, v1, "near");
    db_insert(db, 3, v2, "far");
    free(v0); free(v1); free(v2);

    /* Build a single cluster covering the whole table so ivf_search with
     * nprobe=1,k=1 degrades to an exhaustive scan across all byte
     * offsets -- letting us test db_ann_search's disk-fetch + bounded
     * insertion sort without depending on kmeans_build's clustering
     * (covered separately in test_kmeans.c). */
    cluster_t cluster;
    cluster.centroid_vector = vec3(0, 0, 0); /* irrelevant with k=1 */
    cluster.count = 3;
    cluster.capacity = 3;
    cluster.byte_offsets = (uint64_t *)malloc(sizeof(uint64_t) * 3);
    cluster.byte_offsets[0] = db->records[0].byte_offset;
    cluster.byte_offsets[1] = db->records[1].byte_offset;
    cluster.byte_offsets[2] = db->records[2].byte_offset;

    float query[DIM] = {0, 0, 0};
    SearchResult_t results[2];
    int rc = db_ann_search(db, query, 2, 1, &cluster, 1, results);
    ASSERT_TRUE(rc == 0);

    ASSERT_EQ_U64(results[0].id, 1);
    ASSERT_NEAR(results[0].calculated_distance, 0.0, 1e-5);
    ASSERT_EQ_U64(results[1].id, 2);
    ASSERT_NEAR(results[1].calculated_distance, 1.0, 1e-5);
    ASSERT_NOT_NULL(results[0].metadata);
    ASSERT_TRUE(strcmp(results[0].metadata, "closest") == 0);

    free(results[0].metadata); /* db_ann_search strdup's these, unlike flat_search */
    free(results[1].metadata);
    free(cluster.centroid_vector);
    free(cluster.byte_offsets);
}

TEST_CASE(test_db_ann_search_ram_delete_flag_is_not_honored_by_disk_fetch) {
    /* Documents a real gap (matches the README's "Delete Operation" TODO):
     * db_ann_search fetches records straight from disk via
     * storage_fetch_by_offset() and checks is_deleted on *that* copy, not
     * on db->records[i] in RAM. Flipping the RAM flag alone (as one might
     * naively do to "soft delete") has no effect on ANN search results,
     * unlike flat_search()/db_insert() which operate on the RAM array.
     * This test pins that gap so implementing db_delete() is a visible,
     * deliberate change to this test rather than a silent behavior shift.
     */
    cleanup_table();
    FlatDb_t *db = db_init(TEST_TABLE, DIM, 8, METRIC_EUCLIDEAN);

    float *v0 = vec3(0, 0, 0);
    float *v1 = vec3(5, 5, 5);
    db_insert(db, 1, v0, NULL);
    db_insert(db, 2, v1, NULL);
    free(v0); free(v1);

    db->records[0].is_deleted = true; /* RAM-only "delete" of the closest record */

    cluster_t cluster;
    cluster.centroid_vector = vec3(0, 0, 0);
    cluster.count = 2;
    cluster.capacity = 2;
    cluster.byte_offsets = (uint64_t *)malloc(sizeof(uint64_t) * 2);
    cluster.byte_offsets[0] = db->records[0].byte_offset;
    cluster.byte_offsets[1] = db->records[1].byte_offset;

    float query[DIM] = {0, 0, 0};
    SearchResult_t results[1];
    ASSERT_TRUE(db_ann_search(db, query, 1, 1, &cluster, 1, results) == 0);

    /* The "deleted" record still wins because the on-disk copy was never
     * marked deleted. */
    ASSERT_EQ_U64(results[0].id, 1);
    if (results[0].metadata) free(results[0].metadata);

    free(cluster.centroid_vector);
    free(cluster.byte_offsets);
}

int main(void) {
    RUN_TEST(test_db_init_fresh_table);
    RUN_TEST(test_db_insert_and_capacity_growth);
    RUN_TEST(test_db_insert_null_metadata);
    RUN_TEST(test_db_insert_copies_metadata_string);
    RUN_TEST(test_db_persists_and_reloads_records);
    RUN_TEST(test_db_open_rejects_nonexistent_table);
    RUN_TEST(test_db_open_succeeds_after_init);
    RUN_TEST(test_db_ann_search_orders_by_true_distance);
    RUN_TEST(test_db_ann_search_ram_delete_flag_is_not_honored_by_disk_fetch);
    cleanup_table();
    TEST_SUMMARY();
    return g_tests_failed > 0 ? 1 : 0;
}
