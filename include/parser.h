#ifndef PARSER_H
#define PARSER_H
#include "cJSON.h"
#include "db.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Represents a request to insert a new vector record into the database.
 *
 * Maps a JSON payload containing the table name, vector ID, floating-point
 * embeddings, and optional metadata string.
 */

typedef struct {
  char db_name[100];
  uint32_t id;
  uint64_t dimension;
  float *vector;
  char *metadata;
} insert_req_t;

/**
 * @brief Represents a request to search for nearest neighbors in the database.
 *
 * Maps a JSON payload to dictate whether to use Exact Nearest Neighbor (ENN)
 * or Approximate Nearest Neighbor (ANN) search, along with the query vector.
 */

typedef struct {
  char db_name[100];
  uint32_t top_k;
  uint32_t nprobe;
  bool use_ann;
  uint64_t dimension;
  float *query_vector;
} search_req_t;

/**
 * @brief Represents a request to train the K-Means IVF index.
 *
 * Maps a JSON payload containing the table name, number of clusters (k),
 * and the maximum number of iterations for the clustering algorithm.
 */
typedef struct {
  char db_name[100];
  uint64_t k;
  uint64_t max_iterations;
} train_req_t;


typedef struct {
    char db_name[100];
    uint32_t id;
} delete_req_t;



/**
 * @brief Parses a JSON string into a search request struct.
 *
 * @param json_body Raw JSON string from the HTTP body.
 * @returns Pointer to dynamically allocated search_req_t, or NULL on failure.
 */
search_req_t *parse_search_request(const char *json_body);

/**
 * @brief Frees all memory associated with a search request.
 *
 * @param req Pointer to the search_req_t to free.
 */

void free_search_request(search_req_t *req);

/**
 * @brief Parses a JSON string into an insert request struct.
 *
 * @param json_body Raw JSON string from the HTTP body.
 * @returns Pointer to dynamically allocated insert_req_t, or NULL on failure.
 */
insert_req_t *parse_insert_request(const char *json_body);

/**
 * @brief Frees all memory associated with an insert request.
 *
 * @param req Pointer to the insert_req_t to free.
 */
void free_insert_request(insert_req_t *req);

/**
 * @brief Serializes an array of search results into a JSON array string.
 *
 * @param results Array of SearchResult_t structs returned by the engine.
 * @param count Number of results to serialize (typically top_k).
 * @returns Dynamically allocated JSON string. Caller must free().
 */

char *serialize_search_results(SearchResult_t *results, uint32_t count);

/**
 * @brief Parses a JSON string into a train request struct.
 */
train_req_t *parse_train_request(const char *json_body);

/**
 * @brief Frees all memory associated with a train request.
 */
void free_train_request(train_req_t *req);


delete_req_t *parse_delete_request(const char *json_body);
void free_delete_request(delete_req_t *req);


#endif
