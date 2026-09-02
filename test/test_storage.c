/* Regression tests for src/storage.c
 *
 * Covers: directory creation, write/read roundtrips, NULL metadata,
 * multi-record ordering, the read/write offset-drift bug we fixed
 * earlier (storage_current_offset), out-of-range fetches, and the
 * ivf_index save/load roundtrip including an empty cluster.
 *
 * Build & run: from tests/, `make test`
 */

#include "../include/db.h"
#include "../include/storage.h"
#include "test_framework.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DIM 3
#define TEST_TABLE "regression_storage"

static void cleanup_table(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf origin_data/%s", TEST_TABLE);
    system(cmd);
}

static Record_t make_record(uint64_t id, float x, float y, float z, const char *meta) {
    Record_t r;
    r.id = id;
    r.vector = (float *)malloc(sizeof(float) * DIM);
    r.vector[0] = x; r.vector[1] = y; r.vector[2] = z;
    r.metadata = meta ? strdup(meta) : NULL;
    r.is_deleted = false;
    r.byte_offset = 0;
    return r;
}

static void free_record(Record_t *r) {
    free(r->vector);
    if (r->metadata) free(r->metadata);
}

TEST_CASE(test_init_creates_directory) {
    cleanup_table();
    storage_t *s = storage_init(TEST_TABLE);
    ASSERT_NOT_NULL(s);

    struct stat st;
    char path[256];
    snprintf(path, sizeof(path), "origin_data/%s/data.db", TEST_TABLE);
    ASSERT_TRUE(stat(path, &st) == 0);
}

TEST_CASE(test_write_read_roundtrip_single_record) {
    cleanup_table();
    storage_t *w = storage_init(TEST_TABLE);
    ASSERT_NOT_NULL(w);

    Record_t rec = make_record(42, 1.5f, -2.25f, 3.0f, "hello world");
    ASSERT_TRUE(storage_write_record(w, &rec, DIM) == 0);
    free_record(&rec);

    /* Reopening gives a fresh handle whose read cursor starts at 0. */
    storage_t *r = storage_init(TEST_TABLE);
    ASSERT_NOT_NULL(r);

    Record_t loaded;
    ASSERT_TRUE(storage_load_record(r, &loaded, DIM) == 1);
    ASSERT_EQ_U64(loaded.id, 42);
    ASSERT_TRUE(loaded.is_deleted == false);
    ASSERT_NEAR(loaded.vector[0], 1.5, 1e-6);
    ASSERT_NEAR(loaded.vector[1], -2.25, 1e-6);
    ASSERT_NEAR(loaded.vector[2], 3.0, 1e-6);
    ASSERT_NOT_NULL(loaded.metadata);
    ASSERT_TRUE(strcmp(loaded.metadata, "hello world") == 0);
    free(loaded.vector);
    free(loaded.metadata);

    /* Nothing left to read. */
    Record_t loaded2;
    ASSERT_TRUE(storage_load_record(r, &loaded2, DIM) == 0);
}

TEST_CASE(test_null_metadata_roundtrip) {
    cleanup_table();
    storage_t *w = storage_init(TEST_TABLE);
    Record_t rec = make_record(7, 0, 0, 0, NULL);
    ASSERT_TRUE(storage_write_record(w, &rec, DIM) == 0);
    free_record(&rec);

    storage_t *r = storage_init(TEST_TABLE);
    Record_t loaded;
    ASSERT_TRUE(storage_load_record(r, &loaded, DIM) == 1);
    ASSERT_TRUE(loaded.metadata == NULL);
    free(loaded.vector);
}

TEST_CASE(test_multiple_records_preserve_order) {
    cleanup_table();
    storage_t *w = storage_init(TEST_TABLE);
    for (uint64_t i = 0; i < 5; i++) {
        Record_t rec = make_record(100 + i, (float)i, (float)i * 2, (float)i * 3, "meta");
        ASSERT_TRUE(storage_write_record(w, &rec, DIM) == 0);
        free_record(&rec);
    }

    storage_t *r = storage_init(TEST_TABLE);
    for (uint64_t i = 0; i < 5; i++) {
        Record_t loaded;
        ASSERT_TRUE(storage_load_record(r, &loaded, DIM) == 1);
        ASSERT_EQ_U64(loaded.id, 100 + i);
        free(loaded.vector);
        if (loaded.metadata) free(loaded.metadata);
    }
}

TEST_CASE(test_offset_stays_correct_after_interleaved_read) {
    /* Regression test for the offset-drift bug: storage_current_offset()
     * must always report the true end-of-file position, even after a
     * read (storage_fetch_by_offset) has moved the stream cursor to the
     * middle of the file first -- which is exactly what a real
     * insert-then-search-then-insert workload does. */
    cleanup_table();
    storage_t *s = storage_init(TEST_TABLE);

    Record_t rec1 = make_record(1, 1, 1, 1, "first");
    uint64_t off1 = storage_current_offset(s);
    ASSERT_TRUE(storage_write_record(s, &rec1, DIM) == 0);
    free_record(&rec1);

    Record_t rec2 = make_record(2, 2, 2, 2, "second");
    uint64_t off2 = storage_current_offset(s);
    ASSERT_TRUE(storage_write_record(s, &rec2, DIM) == 0);
    free_record(&rec2);

    /* Simulate a search: fetch record 1 by its earlier offset. This
     * fseeks the stream to the middle of the file. */
    Record_t fetched;
    ASSERT_TRUE(storage_fetch_by_offset(s, off1, &fetched, DIM) == 1);
    ASSERT_EQ_U64(fetched.id, 1);
    free(fetched.vector);
    if (fetched.metadata) free(fetched.metadata);

    /* Now insert again right after that read repositioned the cursor. */
    uint64_t off3 = storage_current_offset(s);
    Record_t rec3 = make_record(3, 3, 3, 3, "third");
    ASSERT_TRUE(storage_write_record(s, &rec3, DIM) == 0);
    free_record(&rec3);

    /* off3 must be past off2 (true end of file), NOT equal to off1
     * (the middle-of-file position the read left the cursor at). */
    ASSERT_TRUE(off3 != off1);
    ASSERT_TRUE(off3 > off2);

    Record_t fetched3;
    ASSERT_TRUE(storage_fetch_by_offset(s, off3, &fetched3, DIM) == 1);
    ASSERT_EQ_U64(fetched3.id, 3);
    free(fetched3.vector);
    if (fetched3.metadata) free(fetched3.metadata);
}

TEST_CASE(test_fetch_by_offset_out_of_range) {
    cleanup_table();
    storage_t *s = storage_init(TEST_TABLE);
    Record_t rec = make_record(1, 1, 1, 1, "x");
    storage_write_record(s, &rec, DIM);
    free_record(&rec);

    Record_t out;
    int status = storage_fetch_by_offset(s, 999999, &out, DIM);
    /* Must fail gracefully, not crash or report a bogus success. */
    ASSERT_TRUE(status == 0 || status == -1);
}

TEST_CASE(test_ivf_index_roundtrip_with_empty_cluster) {
    cleanup_table();
    storage_t *s = storage_init(TEST_TABLE); /* just to create the folder */
    (void)s;

    uint64_t k = 2;
    cluster_t clusters[2];

    clusters[0].centroid_vector = (float *)malloc(sizeof(float) * DIM);
    clusters[0].centroid_vector[0] = 1; clusters[0].centroid_vector[1] = 2; clusters[0].centroid_vector[2] = 3;
    clusters[0].count = 2;
    clusters[0].capacity = 2;
    clusters[0].byte_offsets = (uint64_t *)malloc(sizeof(uint64_t) * 2);
    clusters[0].byte_offsets[0] = 0;
    clusters[0].byte_offsets[1] = 40;

    /* Deliberately test the empty-cluster edge case -- realistic when k
     * is picked larger than the natural number of groups in the data. */
    clusters[1].centroid_vector = (float *)malloc(sizeof(float) * DIM);
    clusters[1].centroid_vector[0] = 9; clusters[1].centroid_vector[1] = 9; clusters[1].centroid_vector[2] = 9;
    clusters[1].count = 0;
    clusters[1].capacity = 0;
    clusters[1].byte_offsets = NULL;

    ASSERT_TRUE(save_ivf_index(TEST_TABLE, clusters, k, DIM) == 0);

    free(clusters[0].centroid_vector);
    free(clusters[0].byte_offsets);
    free(clusters[1].centroid_vector);

    uint64_t loaded_k;
    cluster_t *loaded = load_ivf_index(TEST_TABLE, &loaded_k, DIM);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_U64(loaded_k, 2);
    ASSERT_EQ_U64(loaded[0].count, 2);
    ASSERT_NEAR(loaded[0].centroid_vector[0], 1.0, 1e-6);
    ASSERT_EQ_U64(loaded[0].byte_offsets[1], 40);
    ASSERT_EQ_U64(loaded[1].count, 0);

    for (uint64_t i = 0; i < loaded_k; i++) {
        free(loaded[i].centroid_vector);
        if (loaded[i].byte_offsets) free(loaded[i].byte_offsets);
    }
    free(loaded);
}

int main(void) {
    RUN_TEST(test_init_creates_directory);
    RUN_TEST(test_write_read_roundtrip_single_record);
    RUN_TEST(test_null_metadata_roundtrip);
    RUN_TEST(test_multiple_records_preserve_order);
    RUN_TEST(test_offset_stays_correct_after_interleaved_read);
    RUN_TEST(test_fetch_by_offset_out_of_range);
    RUN_TEST(test_ivf_index_roundtrip_with_empty_cluster);
    cleanup_table();
    TEST_SUMMARY();
    return g_tests_failed > 0 ? 1 : 0;
}
