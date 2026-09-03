/* Regression tests for src/parser.c
 *
 * Covers: valid request parsing for all three request types, rejection
 * of malformed JSON and missing/wrong-typed fields, the nprobe default,
 * optional metadata on insert, non-numeric array elements inside a
 * vector, and serialize_search_results()'s NULL/empty/id==0 handling.
 *
 * Build & run: from tests/, `make test`
 */

#include "../include/parser.h"
#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

/* ---------- parse_search_request ---------- */

TEST_CASE(test_parse_search_request_valid) {
    const char *body =
        "{\"db_name\":\"movies\",\"top_k\":5,\"use_ann\":true,"
        "\"query_vector\":[1.0,2.5,-3.0],\"nprobe\":4}";
    search_req_t *req = parse_search_request(body);
    ASSERT_NOT_NULL(req);
    ASSERT_TRUE(strcmp(req->db_name, "movies") == 0);
    ASSERT_EQ_U64(req->top_k, 5);
    ASSERT_TRUE(req->use_ann == true);
    ASSERT_EQ_U64(req->dimension, 3);
    ASSERT_EQ_U64(req->nprobe, 4);
    ASSERT_NEAR(req->query_vector[0], 1.0, 1e-6);
    ASSERT_NEAR(req->query_vector[1], 2.5, 1e-6);
    ASSERT_NEAR(req->query_vector[2], -3.0, 1e-6);
    free_search_request(req);
}

TEST_CASE(test_parse_search_request_nprobe_defaults_to_one) {
    const char *body =
        "{\"db_name\":\"movies\",\"top_k\":5,\"use_ann\":false,"
        "\"query_vector\":[1.0]}";
    search_req_t *req = parse_search_request(body);
    ASSERT_NOT_NULL(req);
    ASSERT_EQ_U64(req->nprobe, 1);
    free_search_request(req);
}

TEST_CASE(test_parse_search_request_rejects_malformed_json) {
    search_req_t *req = parse_search_request("{not valid json");
    ASSERT_TRUE(req == NULL);
}

TEST_CASE(test_parse_search_request_rejects_missing_field) {
    /* top_k is missing entirely. */
    const char *body =
        "{\"db_name\":\"movies\",\"use_ann\":true,\"query_vector\":[1.0]}";
    ASSERT_TRUE(parse_search_request(body) == NULL);
}

TEST_CASE(test_parse_search_request_rejects_wrong_type) {
    /* top_k is a string instead of a number. */
    const char *body =
        "{\"db_name\":\"movies\",\"top_k\":\"five\",\"use_ann\":true,"
        "\"query_vector\":[1.0]}";
    ASSERT_TRUE(parse_search_request(body) == NULL);
}

TEST_CASE(test_parse_search_request_rejects_non_numeric_vector_element) {
    const char *body =
        "{\"db_name\":\"movies\",\"top_k\":5,\"use_ann\":true,"
        "\"query_vector\":[1.0,\"oops\",3.0]}";
    ASSERT_TRUE(parse_search_request(body) == NULL);
}

TEST_CASE(test_parse_search_request_rejects_empty_body) {
    ASSERT_TRUE(parse_search_request("") == NULL);
}

/* ---------- parse_insert_request ---------- */

TEST_CASE(test_parse_insert_request_valid_with_metadata) {
    const char *body =
        "{\"db_name\":\"movies\",\"id\":7,\"vector\":[1.0,2.0],"
        "\"metadata\":\"hello\"}";
    insert_req_t *req = parse_insert_request(body);
    ASSERT_NOT_NULL(req);
    ASSERT_TRUE(strcmp(req->db_name, "movies") == 0);
    ASSERT_EQ_U64(req->id, 7);
    ASSERT_EQ_U64(req->dimension, 2);
    ASSERT_NOT_NULL(req->metadata);
    ASSERT_TRUE(strcmp(req->metadata, "hello") == 0);
    free_insert_request(req);
}

TEST_CASE(test_parse_insert_request_metadata_is_optional) {
    const char *body = "{\"db_name\":\"movies\",\"id\":7,\"vector\":[1.0,2.0]}";
    insert_req_t *req = parse_insert_request(body);
    ASSERT_NOT_NULL(req);
    ASSERT_TRUE(req->metadata == NULL);
    free_insert_request(req);
}

TEST_CASE(test_parse_insert_request_rejects_missing_vector) {
    const char *body = "{\"db_name\":\"movies\",\"id\":7}";
    ASSERT_TRUE(parse_insert_request(body) == NULL);
}

TEST_CASE(test_parse_insert_request_rejects_malformed_json) {
    ASSERT_TRUE(parse_insert_request("{{{") == NULL);
}

/* ---------- parse_train_request ---------- */

TEST_CASE(test_parse_train_request_valid) {
    const char *body =
        "{\"db_name\":\"movies\",\"k\":8,\"max_iterations\":50}";
    train_req_t *req = parse_train_request(body);
    ASSERT_NOT_NULL(req);
    ASSERT_TRUE(strcmp(req->db_name, "movies") == 0);
    ASSERT_EQ_U64(req->k, 8);
    ASSERT_EQ_U64(req->max_iterations, 50);
    free_train_request(req);
}

TEST_CASE(test_parse_train_request_rejects_missing_k) {
    const char *body = "{\"db_name\":\"movies\",\"max_iterations\":50}";
    ASSERT_TRUE(parse_train_request(body) == NULL);
}

/* ---------- serialize_search_results ---------- */

TEST_CASE(test_serialize_search_results_basic) {
    SearchResult_t results[2];
    results[0].id = 1;
    results[0].calculated_distance = 0.5f;
    results[0].metadata = (char *)"alpha";
    results[1].id = 2;
    results[1].calculated_distance = 1.5f;
    results[1].metadata = NULL;

    char *json = serialize_search_results(results, 2);
    ASSERT_NOT_NULL(json);
    /* Sanity-check the structure without hardcoding exact float
     * formatting, which cJSON controls, not this test. */
    ASSERT_TRUE(strstr(json, "\"id\":1") != NULL);
    ASSERT_TRUE(strstr(json, "\"metadata\":\"alpha\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"metadata\":null") != NULL);
    free(json);
}

TEST_CASE(test_serialize_search_results_skips_id_zero_sentinel) {
    /* id == 0 marks an unfilled slot (see flat_search's sentinel
     * convention) and must not show up in the serialized output. */
    SearchResult_t results[2];
    results[0].id = 0;
    results[0].calculated_distance = 0;
    results[0].metadata = NULL;
    results[1].id = 9;
    results[1].calculated_distance = 2.0f;
    results[1].metadata = NULL;

    char *json = serialize_search_results(results, 2);
    ASSERT_NOT_NULL(json);
    ASSERT_TRUE(strstr(json, "\"id\":0") == NULL);
    ASSERT_TRUE(strstr(json, "\"id\":9") != NULL);
    free(json);
}

TEST_CASE(test_serialize_search_results_null_and_zero_count) {
    ASSERT_TRUE(serialize_search_results(NULL, 0) == NULL);
    SearchResult_t dummy;
    dummy.id = 1;
    dummy.calculated_distance = 0;
    dummy.metadata = NULL;
    ASSERT_TRUE(serialize_search_results(&dummy, 0) == NULL);
}

int main(void) {
    RUN_TEST(test_parse_search_request_valid);
    RUN_TEST(test_parse_search_request_nprobe_defaults_to_one);
    RUN_TEST(test_parse_search_request_rejects_malformed_json);
    RUN_TEST(test_parse_search_request_rejects_missing_field);
    RUN_TEST(test_parse_search_request_rejects_wrong_type);
    RUN_TEST(test_parse_search_request_rejects_non_numeric_vector_element);
    RUN_TEST(test_parse_search_request_rejects_empty_body);
    RUN_TEST(test_parse_insert_request_valid_with_metadata);
    RUN_TEST(test_parse_insert_request_metadata_is_optional);
    RUN_TEST(test_parse_insert_request_rejects_missing_vector);
    RUN_TEST(test_parse_insert_request_rejects_malformed_json);
    RUN_TEST(test_parse_train_request_valid);
    RUN_TEST(test_parse_train_request_rejects_missing_k);
    RUN_TEST(test_serialize_search_results_basic);
    RUN_TEST(test_serialize_search_results_skips_id_zero_sentinel);
    RUN_TEST(test_serialize_search_results_null_and_zero_count);
    TEST_SUMMARY();
    return g_tests_failed > 0 ? 1 : 0;
}
