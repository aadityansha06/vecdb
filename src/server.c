#include "../include/server.h"
#include <stdbool.h>
#define MAX_TOP_K 10000
static void handel_client(server_data_t *server);
static void send_error(server_data_t *server, Client_error err_code,
                       const char *details);

static bool verify_api_key(const char *db_name, const char *provided_key) {
  FILE *fp = fopen("origin_data/.auth_keys", "r");
  if (fp == NULL)
    return false;
  char line[256];
  char expected_match[200];

  snprintf(expected_match, sizeof(expected_match), "%s:%s", db_name,
           provided_key);

  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\r\n")] = '\0';
    if (strcmp(line, expected_match) == 0) {
      fclose(fp);
      return true;
    }
  }

  fclose(fp);
  return false;
}

static bool authenticate_request(const char *request_buffer,
                                 const char *db_name) {
  const char *auth_header = strstr(request_buffer, "Authorization: Bearer ");
  if (auth_header == NULL) {
    return false;
  }

  char api_key[65] = {0};
  auth_header += 22;
  sscanf(auth_header, "%64s", api_key);

  return verify_api_key(db_name, api_key);
}

int server_init(int PORT) {
  struct sockaddr_in serveadrr, clientadrr;
  server_data_t *server = malloc(sizeof(server_data_t));
  if (server == NULL) {
      printf("Failed to allocate server memory\n");
      exit(1);
  }
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
char header_buffer[4096] = {0};
    int header_length = 0;
    char *body_start = NULL;

    while (header_length < (int)sizeof(header_buffer) - 1) {
        int bytes = recv(server->clientfd, header_buffer + header_length, 1, 0);
        if (bytes <= 0) {
            close(server->clientfd);
            return;
        }
        header_length += bytes;
        header_buffer[header_length] = '\0';
        
        body_start = strstr(header_buffer, "\r\n\r\n");
        if (body_start != NULL) break;
    }

    if (body_start == NULL) {
        close(server->clientfd);         return;
    }

    int content_length = 0;
    char *cl_ptr = strstr(header_buffer, "Content-Length: ");
    if (cl_ptr) {
        content_length = atoi(cl_ptr + 16);
    }

    /* @Guardrail: 10MB limit to prevent memory exhaustion attacks
     */
    if (content_length == 0 || content_length > 10485760) { 
        send_error(server, INVALID_PARAMETER, "Missing or excessive Content-Length header.");
        return;
    }

    char *http_body = calloc(content_length + 1, 1);
    if (http_body == NULL) {
        send_error(server, INTERNAL_ERROR, "Out of memory allocating request body.");
        return;
    }

    int headers_size = (body_start - header_buffer) + 4;
    int leftover_body_bytes = header_length - headers_size;
    int body_bytes_read = 0;
    
    if (leftover_body_bytes > 0) {
        memcpy(http_body, body_start + 4, leftover_body_bytes);
        body_bytes_read += leftover_body_bytes;
    }

    while (body_bytes_read < content_length) {
        int bytes = recv(server->clientfd, http_body + body_bytes_read, content_length - body_bytes_read, 0);
        if (bytes <= 0) break;
        body_bytes_read += bytes;
    }
  int bytes_read =
      recv(server->clientfd, header_buffer, sizeof(header_buffer) - 1, 0);

  if (bytes_read <= 0) {
    close(server->clientfd);
    return;
  }
  header_buffer[bytes_read] = '\0';





    /*  @Route function
     *  TODO: isolate receive/header_buffer() from handel_client
     *  will do when free
     *
     */


  // Search Route
  if (strstr(header_buffer, "POST /search") != NULL) {

    

    search_req_t *req = parse_search_request(http_body);
    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      return;
    }

    // Auth Guardrail
    if (!authenticate_request(header_buffer, req->db_name)) {
            send_error(server, UNAUTHORIZED_ACCESS, "Invalid or missing API key for this table.");
            free_search_request(req);
            return;
        }


    FlatDb_t *db = db_open(req->db_name);
    if (db->dimension != req->dimension) {
            send_error(server, INVALID_PARAMETER, "Vector dimension does not match table configuration.");
            db_close(db);
            return;
        }
    if (db == NULL) {
      send_error(server, WRONG_REQUEST, "Database table not found.");
      free_search_request(req);
      return;
    }

if (req->top_k == 0 || req->top_k > MAX_TOP_K) {
        send_error(server, INVALID_PARAMETER, "top_k must be between 1 and 10000.");
        free_search_request(req);
        db_close(db); 
        return;
    }

    SearchResult_t *results = (SearchResult_t *)malloc(req->top_k * sizeof(SearchResult_t));
    if (results == NULL) {
        send_error(server, INTERNAL_ERROR, "Out of memory allocating search results.");
        free_search_request(req);
        db_close(db);
        return;
    }

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
    db_close(db);
    free(http_body);
    close(server->clientfd);

  } else if (strstr(header_buffer, "POST /insert") != NULL) {
    // insert_data();

    

    insert_req_t *req = parse_insert_request(http_body);
    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      return;
    }


    // Auth guardrail
    if (!authenticate_request(header_buffer, req->db_name)) {
            send_error(server, UNAUTHORIZED_ACCESS, "Invalid or missing API key for this table.");
            free_insert_request(req);
            return;
        }


 FlatDb_t *db = db_open(req->db_name);
    if (db->dimension != req->dimension) {
            send_error(server, INVALID_PARAMETER, "Vector dimension does not match table configuration.");
            db_close(db);
            return;
        }
    if (db == NULL) {
      send_error(server, WRONG_REQUEST, "Database table not found.");
      free_insert_request(req);
      return;
    }

    if (db_insert(db, req->id, req->vector, req->metadata) < 0) {
      send_error(server, INTERNAL_ERROR, "Failed to write record to disk.");
      free_insert_request(req);
      db_close(db);
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
    db_close(db);
    free(http_body);
    close(server->clientfd);

  } else if (strstr(header_buffer, "POST /train") != NULL) {
    // train_db();

   

    train_req_t *req = parse_train_request(http_body);
    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      return;
    }
    

    //Auth Guardrail
    if (!authenticate_request(header_buffer, req->db_name)) {
            send_error(server, UNAUTHORIZED_ACCESS, "Invalid or missing API key for this table.");
            free_train_request(req);
            return;
        }


FlatDb_t *db = db_open(req->db_name);
    if (db == NULL) {
      send_error(server, WRONG_REQUEST, "Database table not found.");
      free_train_request(req);
      db_close(db);
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
    db_close(db);
    free(http_body);
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
