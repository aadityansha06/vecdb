#include "../include/server.h"
static void handel_client(server_data_t *server);
static void send_error(server_data_t *server, Client_error err_code,
                       const char *details);
int server_init(int PORT) {
  struct sockaddr_in serveadrr, clientadrr;
  server_data_t *server;
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    printf("Socket failed ");
    close(sockfd);
    exit(0);
  }

  serveadrr.sin_family = AF_INET;         /* AF_INET */
  serveadrr.sin_port = htons(PORT);       /* Port number */
  serveadrr.sin_addr.s_addr = INADDR_ANY; // Accept any
  socklen_t len = sizeof(serveadrr);
  if (bind(sockfd, (struct sockaddr *)&serveadrr, len) < 0) {
    printf("Bind failed ");
    close(sockfd);
    exit(0);
  }

  if (listen(sockfd, 3) < 0) {
    printf("Listen failed ");
    close(sockfd);
    exit(0);
  }
  printf("\n Listing on Port %d...", PORT);
  socklen_t client_len = sizeof(clientadrr);
  while (1) {
    server->clientfd =
        accept(sockfd, (struct sockaddr *)&clientadrr, &client_len);
    if (server->clientfd < 0) {
      printf("\n connection failed");
      continue;
    }
    handel_client(server);
  }

  return 1;
}

static void handel_client(server_data_t *server) {
  char request_buffer[2048];

  int bytes_read =
      recv(server->clientfd, request_buffer, sizeof(request_buffer) - 1, 0);

  if (bytes_read <= 0) {
    close(server->clientfd);
    return;
  }
  request_buffer[bytes_read] = '\0';

  //  AUTHENTICATION GUARDRAIL
  char *system_key = getenv("ORIGIN_API_KEY");

  if (system_key == NULL || strstr(request_buffer, system_key) == NULL) {
    send_error(server, UNAUTHORIZED_ACCESS, "Missing or Invalid API Key.");
    return;
  }

  // Search Route
  if (strstr(request_buffer, "POST /search") != NULL) {

    char *http_body = strstr(request_buffer, "\r\n\r\n");
    if (http_body != NULL) {
      http_body += 4;
    } else {
      send_error(server, INVALID_PARAMETER, "Missing HTTP body.");
      return;
    }

    search_req_t *req = parse_search_request(http_body);
    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      return;
    }

    FlatDb_t *db = db_open(req->db_name, req->dimension, 0);
    if (db == NULL) {
      send_error(server, WRONG_REQUEST, "Database table not found.");
      free_search_request(req);
      return;
    }

    SearchResult_t *results =
        (SearchResult_t *)malloc(req->top_k * sizeof(SearchResult_t));

    if (req->use_ann) {
      uint64_t loaded_k;
      cluster_t *clusters =
          load_ivf_index(req->db_name, &loaded_k, req->dimension);

      if (clusters == NULL) {
        send_error(server, INVALID_PARAMETER,
                   "Index not trained. Call /train or use ENN.");
        free(results);
        free_search_request(req);
        return;
      }

      db_ann_search(db, req->query_vector, req->top_k, req->nprobe, clusters,
                    loaded_k, results);

      for (uint64_t i = 0; i < loaded_k; i++) {
        free(clusters[i].centroid_vector);
        free(clusters[i].byte_offsets);
      }
      free(clusters);

    } else {
      flat_search(db->records, db->count, req->dimension, req->query_vector,
                  req->top_k, db->calculate_distance, results);
    }

    char *json_payload = serialize_search_results(results, req->top_k);

    if (json_payload != NULL) {
      char http_response[4096];
      snprintf(http_response, sizeof(http_response),
               "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Content-Length: %zu\r\n"
               "Connection: close\r\n"
               "\r\n"
               "%s",
               strlen(json_payload), json_payload);

      send(server->clientfd, http_response, strlen(http_response), 0);

      free(json_payload);
    } else {
      send_error(server, INTERNAL_ERROR, "Failed to serialize search results.");
    }

    free(results);
    free_search_request(req);
    close(server->clientfd);

  } else if (strstr(request_buffer, "POST /insert") != NULL) {
    // insert_data();

    char *http_body = strstr(request_buffer, "\r\n\r\n");
    if (http_body != NULL) {
      http_body += 4;
    } else {
      send_error(server, INVALID_PARAMETER, "Missing HTTP body.");
      return;
    }

    insert_req_t *req = parse_insert_request(http_body);
    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      return;
    }

    FlatDb_t *db = db_open(req->db_name, req->dimension, 0);
    if (db == NULL) {
      send_error(server, WRONG_REQUEST, "Database table not found.");
      free_insert_request(req);
      return;
    }

    if (db_insert(db, req->id, req->vector, req->metadata) < 0) {
      send_error(server, INTERNAL_ERROR, "Failed to write record to disk.");
      free_insert_request(req);
      return;
    }

    char response[1024];
    char json_body[] = "{\"status\": \"success\", \"message\": \"Vector "
                       "inserted successfully.\"}";

    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             strlen(json_body), json_body);

    send(server->clientfd, response, strlen(response), 0);

    free_insert_request(req);
    close(server->clientfd);

  } else if (strstr(request_buffer, "POST /train") != NULL) {
    // train_db();

    char *http_body = strstr(request_buffer, "\r\n\r\n");
    if (http_body != NULL) {
      http_body += 4;
    } else {
      send_error(server, INVALID_PARAMETER, "Missing HTTP body.");
      return;
    }

    train_req_t *req = parse_train_request(http_body);
    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      return;
    }

    FlatDb_t *db = db_open(req->db_name, 0, 0);
    if (db == NULL) {
      send_error(server, WRONG_REQUEST, "Database table not found.");
      free_train_request(req);
      return;
    }

    if (db->count == 0 || db->count < req->k) {
      send_error(server, INVALID_PARAMETER,
                 "Not enough records to train the requested clusters.");
      free_train_request(req);
      return;
    }

    cluster_t *trained_clusters =
        kmeans_build(db->records, db->count, req->k, db->dimension,
                     req->max_iterations, db->calculate_distance);

    if (save_ivf_index(req->db_name, trained_clusters, req->k, db->dimension) <
        0) {
      send_error(server, INTERNAL_ERROR,
                 "Failed to save trained index to disk.");
    } else {
      char response[1024];
      char json_body[] = "{\"status\": \"success\", \"message\": \"Index "
                         "trained and saved successfully.\"}";

      snprintf(response, sizeof(response),
               "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Content-Length: %zu\r\n"
               "Connection: close\r\n"
               "\r\n"
               "%s",
               strlen(json_body), json_body);

      send(server->clientfd, response, strlen(response), 0);
    }

    for (uint64_t i = 0; i < req->k; i++) {
      free(trained_clusters[i].centroid_vector);
      free(trained_clusters[i].record_index);
      free(trained_clusters[i].byte_offsets);
    }
    free(trained_clusters);
    free_train_request(req);
    close(server->clientfd);

  } else {

    send_error(server, WRONG_REQUEST, "Invalid API route.");
  }
}
static void send_error(server_data_t *server, Client_error err_code,
                       const char *details) {
  char response[1024];
  const char *status_str;

  switch (err_code) {
  case INVALID_PARAMETER:
    status_str = "400 Bad Request";
    break;
  case UNAUTHORIZED_ACCESS:
    status_str = "401 Unauthorized";
    break;
  case WRONG_REQUEST:
    status_str = "404 Not Found";
    break;
  default:
    status_str = "500 Internal Server Error";
    break;
  }

  char json_body[256];
  snprintf(json_body, sizeof(json_body), "{\"error\": \"%s\"}", details);

  snprintf(response, sizeof(response),
           "HTTP/1.1 %s\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: %zu\r\n"
           "Connection: close\r\n"
           "\r\n"
           "%s",
           status_str, strlen(json_body), json_body);

  send(server->clientfd, response, strlen(response), 0);
  close(server->clientfd);
}
