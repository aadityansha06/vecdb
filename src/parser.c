#include "../include/parser.h"
#include <stdlib.h>
#include <string.h>

search_req_t *parse_search_request(const char *json_body) {
  cJSON *json = cJSON_Parse(json_body);
  if (json == NULL)
    return NULL;
  cJSON *db_name = cJSON_GetObjectItemCaseSensitive(json, "db_name");
  cJSON *top_k = cJSON_GetObjectItemCaseSensitive(json, "top_k");
  cJSON *use_ann = cJSON_GetObjectItemCaseSensitive(json, "use_ann");
  cJSON *vec_array = cJSON_GetObjectItemCaseSensitive(json, "query_vector");

  if (!cJSON_IsString(db_name) || !cJSON_IsNumber(top_k) ||
      !cJSON_IsBool(use_ann) || !cJSON_IsArray(vec_array)) {
    cJSON_Delete(json);
    return NULL;
  }

  search_req_t *req = calloc(1, sizeof(search_req_t));
  strncpy(req->db_name, db_name->valuestring, sizeof(req->db_name) - 1);
  req->db_name[sizeof(req->db_name) - 1] = '\0';
  req->top_k = top_k->valueint;
  req->use_ann = cJSON_IsTrue(use_ann);
  req->dimension = cJSON_GetArraySize(vec_array);

  cJSON *nprobe = cJSON_GetObjectItemCaseSensitive(json, "nprobe");
  req->nprobe = cJSON_IsNumber(nprobe) ? nprobe->valueint : 1;

  req->query_vector = malloc(req->dimension * sizeof(float));
  for (uint64_t i = 0; i < req->dimension; i++) {
    cJSON *item = cJSON_GetArrayItem(vec_array, i);
    if (!cJSON_IsNumber(item)) {
      free(req->query_vector);
      free(req);
      cJSON_Delete(json);
      return NULL;
    }
    req->query_vector[i] = (float)item->valuedouble;
  }

  cJSON_Delete(json);
  return req;
}

void free_search_request(search_req_t *req) {
  if (req) {
    if (req->query_vector)
      free(req->query_vector);
    free(req);
  }
}

char *serialize_search_results(SearchResult_t *results, uint32_t count) {
  if (results == NULL || count == 0)
    return NULL;

  cJSON *json_response = cJSON_CreateArray();

  for (uint32_t i = 0; i < count; i++) {
    if (results[i].id != 0) {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddNumberToObject(item, "id", results[i].id);
      cJSON_AddNumberToObject(item, "distance", results[i].calculated_distance);

      if (results[i].metadata != NULL) {
        cJSON_AddStringToObject(item, "metadata", results[i].metadata);
      } else {
        cJSON_AddNullToObject(item, "metadata");
      }

      cJSON_AddItemToArray(json_response, item);
    }
  }

  char *json_string = cJSON_PrintUnformatted(json_response);

  cJSON_Delete(json_response);

  return json_string;
}

insert_req_t *parse_insert_request(const char *json_body) {
  cJSON *json = cJSON_Parse(json_body);
  if (json == NULL)
    return NULL;

  cJSON *db_name = cJSON_GetObjectItemCaseSensitive(json, "db_name");
  cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");
  cJSON *vec_array = cJSON_GetObjectItemCaseSensitive(json, "vector");
  cJSON *metadata =
      cJSON_GetObjectItemCaseSensitive(json, "metadata"); // Optional

  if (!cJSON_IsString(db_name) || !cJSON_IsNumber(id) ||
      !cJSON_IsArray(vec_array)) {
    cJSON_Delete(json);
    return NULL;
  }

insert_req_t *req = calloc(1, sizeof(insert_req_t));
  strncpy(req->db_name, db_name->valuestring, sizeof(req->db_name) - 1);
  req->db_name[sizeof(req->db_name) - 1] = '\0';

  strncpy(req->db_name, db_name->valuestring, sizeof(req->db_name) - 1);
  req->id = (uint32_t)id->valuedouble;
  req->dimension = cJSON_GetArraySize(vec_array);

  if (cJSON_IsString(metadata) && metadata->valuestring != NULL) {
    req->metadata = strdup(metadata->valuestring);
  } else {
    req->metadata = NULL;
  }

  req->vector = malloc(req->dimension * sizeof(float));
  for (uint64_t i = 0; i < req->dimension; i++) {
    cJSON *item = cJSON_GetArrayItem(vec_array, i);
    if (!cJSON_IsNumber(item)) {
      free(req->vector);
      if (req->metadata)
        free(req->metadata);
      free(req);
      cJSON_Delete(json);
      return NULL;
    }
    req->vector[i] = (float)item->valuedouble;
  }

  cJSON_Delete(json);
  return req;
}

void free_insert_request(insert_req_t *req) {
  if (req) {
    if (req->vector)
      free(req->vector);
    if (req->metadata)
      free(req->metadata);
    free(req);
  }
}

train_req_t *parse_train_request(const char *json_body) {
  cJSON *json = cJSON_Parse(json_body);
  if (json == NULL)
    return NULL;

  cJSON *db_name = cJSON_GetObjectItemCaseSensitive(json, "db_name");
  cJSON *k = cJSON_GetObjectItemCaseSensitive(json, "k");
  cJSON *max_iterations =
      cJSON_GetObjectItemCaseSensitive(json, "max_iterations");

  if (!cJSON_IsString(db_name) || !cJSON_IsNumber(k) ||
      !cJSON_IsNumber(max_iterations)) {
    cJSON_Delete(json);
    return NULL;
  }

train_req_t *req = calloc(1, sizeof(train_req_t));
  strncpy(req->db_name, db_name->valuestring, sizeof(req->db_name) - 1);
  req->db_name[sizeof(req->db_name) - 1] = '\0';
  strncpy(req->db_name, db_name->valuestring, sizeof(req->db_name) - 1);
  req->k = (uint64_t)k->valuedouble;
  req->max_iterations = (uint64_t)max_iterations->valuedouble;

  cJSON_Delete(json);
  return req;
}


void free_train_request(train_req_t *req) {
  if (req) {
    free(req);
  }
}

delete_req_t *parse_delete_request(const char *json_body) {
    cJSON *json = cJSON_Parse(json_body);
    if (json == NULL) return NULL; 

    cJSON *db_name = cJSON_GetObjectItemCaseSensitive(json, "db_name");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");

    if (!cJSON_IsString(db_name) || !cJSON_IsNumber(id)) {
        cJSON_Delete(json);
        return NULL;
    }

    delete_req_t *req = calloc(1, sizeof(delete_req_t));
    strncpy(req->db_name, db_name->valuestring, sizeof(req->db_name) - 1);
    req->db_name[sizeof(req->db_name) - 1] = '\0';
    req->id = (uint32_t)id->valuedouble;

    cJSON_Delete(json);
    return req;
}

void free_delete_request(delete_req_t *req) {
    if (req) free(req);
}


