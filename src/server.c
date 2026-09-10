/*
 * Copyright (c) 2026 Aadityansha Verma. All rights reserved.
 * This file is licensed under the Business Source License 1.1.
 * See the LICENSE file in the project root for full terms.
 */
#define _GNU_SOURCE
#include "../include/server.h"
#include "../include/pending-delete.h"
#include "../include/storage.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <unistd.h>

#define MAX_TOP_K 10000
#define QUEUE_SIZE 512
#define MAX_OPEN_TABLES 32
#define MAX_EVENTS 1024


typedef struct {
  uint64_t id;
  float *vector;
  char *metadata;
  uint64_t byte_offset;
} pending_insert_t;

typedef struct {
  char name[65];
  FlatDb_t *db;
  pthread_rwlock_t lock;
  pthread_mutex_t pending_dirty_lock;
  bool pending_delete_dirty;
  pthread_mutex_t insert_queue_lock;
  pending_insert_t *insert_queue;
  uint64_t insert_queue_count;
  uint64_t insert_queue_capacity;
  pthread_mutex_t disk_write_lock;
} open_table_t;

static pthread_rwlock_t index_lock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t pending_delete_lock = PTHREAD_MUTEX_INITIALIZER;
static open_table_t open_tables[MAX_OPEN_TABLES];
static int open_table_count = 0;
static pthread_mutex_t open_tables_lock = PTHREAD_MUTEX_INITIALIZER;

static void handel_client(server_data_t *server, char *header_buffer,
                          char *http_body);
static void send_error(server_data_t *server, Client_error err_code,
                       const char *details);
static int send_all(int fd, const char *buf, size_t len);

static open_table_t *get_or_open_table(const char *db_name) {
  pthread_mutex_lock(&open_tables_lock);

  for (int i = 0; i < open_table_count; i++) {
    if (strcmp(open_tables[i].name, db_name) == 0) {
      pthread_mutex_unlock(&open_tables_lock);
      return &open_tables[i];
    }
  }

  if (open_table_count >= MAX_OPEN_TABLES) {
    pthread_mutex_unlock(&open_tables_lock);
    return NULL;
  }

  FlatDb_t *db = db_open(db_name);
  if (db == NULL) {
    pthread_mutex_unlock(&open_tables_lock);
    return NULL;
  }

  open_table_t *slot = &open_tables[open_table_count++];
  strncpy(slot->name, db_name, sizeof(slot->name) - 1);
  slot->db = db;
  pthread_rwlock_init(&slot->lock, NULL);

  pthread_mutex_init(&slot->pending_dirty_lock, NULL);
  slot->pending_delete_dirty = false;

  pthread_mutex_init(&slot->insert_queue_lock, NULL);
  slot->insert_queue_capacity = 1024;
  slot->insert_queue =
      malloc(sizeof(pending_insert_t) * slot->insert_queue_capacity);
  slot->insert_queue_count = 0;

  pthread_mutex_init(&slot->disk_write_lock, NULL);

  pthread_mutex_unlock(&open_tables_lock);
  return slot;
}
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

static void mark_table_pending_dirty(const char *db_name) {
  pthread_mutex_lock(&open_tables_lock);
  for (int i = 0; i < open_table_count; i++) {
    if (strcmp(open_tables[i].name, db_name) == 0) {
      pthread_mutex_lock(&open_tables[i].pending_dirty_lock);
      open_tables[i].pending_delete_dirty = true;
      pthread_mutex_unlock(&open_tables[i].pending_dirty_lock);
      break;
    }
  }
  pthread_mutex_unlock(&open_tables_lock);
}

static void *sync_worker(void *arg) {
  (void)arg;
  while (1) {
    sleep(2);

    pthread_mutex_lock(&open_tables_lock);
    for (int i = 0; i < open_table_count; i++) {
      open_table_t *t = &open_tables[i];

      pthread_mutex_lock(&t->insert_queue_lock);
      uint64_t n = t->insert_queue_count;
      if (n > 0) {
        pthread_rwlock_wrlock(&t->lock);
        if (t->db->count + n > t->db->capacity) {
          uint64_t new_cap = t->db->capacity;
          while (new_cap < t->db->count + n)
            new_cap *= 2;
          t->db->records = realloc(t->db->records, sizeof(Record_t) * new_cap);
          t->db->capacity = new_cap;
        }

        for (uint64_t j = 0; j < n; j++) {
          t->db->records[t->db->count].id = t->insert_queue[j].id;
          t->db->records[t->db->count].vector = t->insert_queue[j].vector;
          t->db->records[t->db->count].metadata = t->insert_queue[j].metadata;
          t->db->records[t->db->count].byte_offset =
              t->insert_queue[j].byte_offset;
          t->db->records[t->db->count].is_deleted = false;
          t->db->records[t->db->count].is_mmap = false;
          t->db->count++;
        }
        storage_remap(t->db->storage);
        pthread_rwlock_unlock(&t->lock);
        t->insert_queue_count = 0;
      }
      pthread_mutex_unlock(&t->insert_queue_lock);

      pthread_mutex_lock(&t->pending_dirty_lock);
      if (t->pending_delete_dirty) {
        uint64_t new_count;
        uint64_t *fresh = load_pending_deletes(t->name, &new_count);

        pthread_rwlock_wrlock(&t->lock);
        uint64_t *old = t->db->pending_deletes;
        t->db->pending_deletes = fresh;
        t->db->pending_count = new_count;
        pthread_rwlock_unlock(&t->lock);

        if (old)
          free(old);
        t->pending_delete_dirty = false;
      }
      pthread_mutex_unlock(&t->pending_dirty_lock);

      pthread_rwlock_wrlock(&t->lock);
      int compacted = check_and_run_auto_delete(t->db, t->name);
      pthread_rwlock_unlock(&t->lock);

      if (compacted > 0) {
        char index_path[300];
        snprintf(index_path, sizeof(index_path),
                 "origin_data/%s/ivf_index.bin", t->name);
        pthread_rwlock_wrlock(&index_lock);
        remove(index_path);
        pthread_rwlock_unlock(&index_lock);
      }
    }
    pthread_mutex_unlock(&open_tables_lock);
  }
  return NULL;
}



typedef enum { CONN_READING_HEADERS, CONN_READING_BODY, CONN_DONE } conn_phase_t;

typedef struct {
  int fd;
  conn_phase_t phase;
  char header_buffer[4096];
  int header_length;
  int content_length;
  char *http_body;
  int body_bytes_read;
} conn_state_t;

static int g_epoll_fd = -1;


static int compute_epoll_thread_count(void) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n < 4)
    n = 4;
  if (n > 32)
    n = 32;
  return (int)n;
}

static void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags != -1)
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


static void raise_fd_limit(void) {
  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
    return;

  rlim_t previous = rl.rlim_cur;
  rl.rlim_cur = rl.rlim_max;

  if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
    printf("Warning: could not raise open-file limit (soft=%llu hard=%llu). "
           "For 100k+ connections, raise 'ulimit -n' (or LimitNOFILE= in "
           "your systemd unit) before starting the server.\n",
           (unsigned long long)previous, (unsigned long long)rl.rlim_max);
    return;
  }

  printf("Open-file limit raised from %llu to %llu.\n",
         (unsigned long long)previous, (unsigned long long)rl.rlim_cur);

  if (rl.rlim_cur < 100000) {
    printf("Warning: open-file limit is %llu, below 100000. The kernel's "
           "hard limit itself needs raising externally to actually reach "
           "100k concurrent connections.\n",
           (unsigned long long)rl.rlim_cur);
  }
}

static void close_conn(conn_state_t *conn) {
  if (conn->http_body)
    free(conn->http_body);
  close(conn->fd);
  free(conn);
}

static void rearm(conn_state_t *conn) {
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLONESHOT;
  ev.data.ptr = conn;
  if (epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev) != 0) {
    close_conn(conn);
  }
}


static void handle_conn_event(conn_state_t *conn) {
  for (;;) {
    char *dest;
    int dest_cap;

    if (conn->phase == CONN_READING_HEADERS) {
      dest = conn->header_buffer + conn->header_length;
      dest_cap = (int)sizeof(conn->header_buffer) - 1 - conn->header_length;
      if (dest_cap <= 0) {
       
        close_conn(conn);
        return;
      }
    } else {
      dest = conn->http_body + conn->body_bytes_read;
      dest_cap = conn->content_length - conn->body_bytes_read;
    }

    int n = recv(conn->fd, dest, dest_cap, 0);

    if (n == 0) {
      close_conn(conn);
      return;
    }
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        rearm(conn);
        return;
      }
      close_conn(conn);
    }

    if (conn->phase == CONN_READING_HEADERS) {
      conn->header_length += n;
      conn->header_buffer[conn->header_length] = '\0';

      char *body_start = strstr(conn->header_buffer, "\r\n\r\n");
      if (body_start == NULL)
        continue; 

      int content_length = 0;
      char *cl_ptr = strcasestr(conn->header_buffer, "Content-Length:");
      if (cl_ptr) {
        cl_ptr += strlen("Content-Length:");
        content_length = atoi(cl_ptr);
      }

      if (content_length == 0 || content_length > 10485760) {
        server_data_t s;
        s.clientfd = conn->fd;
        send_error(&s, INVALID_PARAMETER,
                   "Missing or excessive Content-Length header.");
        free(conn);
        return;
      }

      conn->http_body = calloc(content_length + 1, 1);
      if (conn->http_body == NULL) {
        server_data_t s;
        s.clientfd = conn->fd;
        send_error(&s, INTERNAL_ERROR,
                   "Out of memory allocating request body.");
        free(conn);
        return;
      }
      conn->content_length = content_length;

      int headers_size = (int)(body_start - conn->header_buffer) + 4;
      int leftover = conn->header_length - headers_size;
      if (leftover > 0) {
        int take = leftover > content_length ? content_length : leftover;
        memcpy(conn->http_body, body_start + 4, take);
        conn->body_bytes_read = take;
      }

      conn->phase = CONN_READING_BODY;

      if (conn->body_bytes_read >= conn->content_length) {
        conn->phase = CONN_DONE;
        break;
      }
      continue;

    } else { /* CONN_READING_BODY */
      conn->body_bytes_read += n;
      if (conn->body_bytes_read >= conn->content_length) {
        conn->phase = CONN_DONE;
        break;
      }
      continue;
    }
  }

  
  server_data_t server;
  server.clientfd = conn->fd;
  handel_client(&server, conn->header_buffer, conn->http_body);
  free(conn);
}

static void *epoll_worker_loop(void *arg) {
  (void)arg;
  struct epoll_event events[MAX_EVENTS];
  for (;;) {
    int n = epoll_wait(g_epoll_fd, events, MAX_EVENTS, -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      continue;
    }
    for (int i = 0; i < n; i++) {
      conn_state_t *conn = (conn_state_t *)events[i].data.ptr;
      if (conn != NULL)
        handle_conn_event(conn);
    }
  }
  return NULL;
}

typedef struct {
  int listen_fd;
} acceptor_arg_t;

static void *acceptor_loop(void *arg) {
  int listen_fd = ((acceptor_arg_t *)arg)->listen_fd;
  struct sockaddr_in clientadrr;
  socklen_t client_len = sizeof(clientadrr);

  for (;;) {
    int new_clientfd =
        accept(listen_fd, (struct sockaddr *)&clientadrr, &client_len);
    if (new_clientfd < 0)
      continue;

    set_nonblocking(new_clientfd);

    conn_state_t *conn = (conn_state_t *)calloc(1, sizeof(conn_state_t));
    if (conn == NULL) {
      close(new_clientfd);
      continue;
    }
    conn->fd = new_clientfd;
    conn->phase = CONN_READING_HEADERS;

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.ptr = conn;
    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, new_clientfd, &ev) != 0) {
      close(new_clientfd);
      free(conn);
    }
  }
  return NULL;
}

int server_init(int PORT) {
  struct sockaddr_in serveadrr;

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    printf("Socket failed ");
    exit(1);
  }

  int opt = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  serveadrr.sin_family = AF_INET;
  serveadrr.sin_port = htons(PORT);
  serveadrr.sin_addr.s_addr = INADDR_ANY;

  socklen_t len = sizeof(serveadrr);
  if (bind(sockfd, (struct sockaddr *)&serveadrr, len) < 0) {
    printf("Bind failed ");
    close(sockfd);
    exit(1);
  }

  if (listen(sockfd, 100000) < 0) {
    printf("Listen failed ");
    close(sockfd);
    exit(1);
  }

  raise_fd_limit();

  g_epoll_fd = epoll_create1(0);
  if (g_epoll_fd == -1) {
    printf("Failed to create epoll file descriptor\n");
    exit(1);
  }

  static acceptor_arg_t accept_arg;
  accept_arg.listen_fd = sockfd;
  pthread_t acceptor_thread;
  pthread_create(&acceptor_thread, NULL, acceptor_loop, &accept_arg);
  pthread_detach(acceptor_thread);

  int epoll_thread_count = compute_epoll_thread_count();
  pthread_t *epoll_threads =
      malloc(sizeof(pthread_t) * (size_t)epoll_thread_count);
  for (int i = 0; i < epoll_thread_count; i++) {
    pthread_create(&epoll_threads[i], NULL, epoll_worker_loop, NULL);
    pthread_detach(epoll_threads[i]);
  }

  pthread_t sync_thread;
  pthread_create(&sync_thread, NULL, sync_worker, NULL);
  pthread_detach(sync_thread);

  printf("\nOriginDB epoll event loop listening on port %d (%d worker "
         "thread(s), accept queue depth 100000)...\n",
         PORT, epoll_thread_count);


  for (;;) {
    pause();
  }

  return 1;
}


static int send_all(int fd, const char *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, buf + sent, len - sent, 0);
    if (n > 0) {
      sent += (size_t)n;
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLOUT;
      poll(&pfd, 1, 5000);
      continue;
    }
    return -1;
  }
  return 0;
}



static void handel_client(server_data_t *server, char *header_buffer,
                          char *http_body) {
  // Search Route
  if (strstr(header_buffer, "POST /search") != NULL) {
    search_req_t *req = NULL;
    FlatDb_t *db = NULL;
    SearchResult_t *results = NULL;
    char *json_payload = NULL;
    open_table_t *table = NULL;

    req = parse_search_request(http_body);
    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      goto search_cleanup;
    }

    /*
     *@Auth Guardrail
     */
    if (!authenticate_request(header_buffer, req->db_name)) {
      send_error(server, UNAUTHORIZED_ACCESS,
                 "Invalid or missing API key for this table.");
      goto search_cleanup;
    }
    table = get_or_open_table(req->db_name);
    if (table == NULL) {
      send_error(server, WRONG_REQUEST, "Database not found");
      goto search_cleanup;
    }
    pthread_rwlock_rdlock(&table->lock);
    db = table->db;
    if (db->dimension != req->dimension) {
      send_error(server, INVALID_PARAMETER,
                 "Vector dimension does not match table configuration.");
      goto search_cleanup;
    }

    if (req->top_k == 0 || req->top_k > MAX_TOP_K) {
      send_error(server, INVALID_PARAMETER,
                 "top_k must be between 1 and 10000.");
      goto search_cleanup;
    }
    if (req->use_ann && (req->nprobe == 0 || req->nprobe > MAX_TOP_K)) {
      send_error(server, INVALID_PARAMETER,
                 "nprobe must be a positive integer.");
      goto search_cleanup;
    }

    results = (SearchResult_t *)malloc(req->top_k * sizeof(SearchResult_t));
    if (results == NULL) {
      send_error(server, INTERNAL_ERROR,
                 "Out of memory allocating search results.");
      goto search_cleanup;
    }

    if (req->use_ann) {
      uint64_t loaded_k;
      pthread_rwlock_rdlock(&index_lock);
      cluster_t *clusters =
          load_ivf_index(req->db_name, &loaded_k, req->dimension);
      pthread_rwlock_unlock(&index_lock);

      if (clusters == NULL) {
        send_error(server, INVALID_PARAMETER,
                   "Index not trained. Call /train or use ENN.");
        goto search_cleanup;
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
                  req->top_k, db->calculate_distance, results,
                  db->pending_deletes, db->pending_count);
    }

    json_payload = serialize_search_results(results, req->top_k);

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

      send_all(server->clientfd, http_response, strlen(http_response));
    } else {
      send_error(server, INTERNAL_ERROR, "Failed to serialize search results.");
    }

  search_cleanup:
    if (table)
      pthread_rwlock_unlock(&table->lock);
    if (json_payload)
      free(json_payload);
    if (results)
      free(results);

    if (req)
      free_search_request(req);
    if (http_body)
      free(http_body);

    close(server->clientfd);
  } else if (strstr(header_buffer, "POST /insert") != NULL) {
    // insert_data();
    insert_req_t *req = NULL;
    FlatDb_t *db = NULL;
    open_table_t *table = NULL;

    req = parse_insert_request(http_body);
    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      goto insert_cleanup;
    }
    /*@Auth Guardrail
     *
     */
    if (!authenticate_request(header_buffer, req->db_name)) {
      send_error(server, UNAUTHORIZED_ACCESS,
                 "Invalid or missing API key for this table.");
      goto insert_cleanup;
    }
    /* @writing insert to table i.e RAM or cache
     */
    table = get_or_open_table(req->db_name);
    if (table == NULL) {
      send_error(server, WRONG_REQUEST, "Database table not found.");
      goto insert_cleanup;
    }
    pthread_rwlock_wrlock(&table->lock);
    db = table->db;

    if (db->dimension != req->dimension) {
      send_error(server, INVALID_PARAMETER,
                 "Vector dimension does not match table configuration.");
      goto insert_cleanup;
    }

    float *vec_cpy = (float *)malloc(db->dimension * sizeof(float));
    memcpy(vec_cpy, req->vector, db->dimension * sizeof(float));
    char *meta_cpy = req->metadata ? strdup(req->metadata) : NULL;

    Record_t temp_rec;
    temp_rec.id = req->id;
    temp_rec.vector = vec_cpy;
    temp_rec.metadata = meta_cpy;
    temp_rec.is_deleted = false;

    pthread_mutex_lock(&table->disk_write_lock);
    temp_rec.byte_offset = storage_current_offset(db->storage);
    int db_write = storage_write_record(db->storage, &temp_rec, db->dimension);
    pthread_mutex_unlock(&table->disk_write_lock);

    if (db_write < 0) {
      free(vec_cpy);
      if (meta_cpy)
        free(meta_cpy);
      send_error(server, INTERNAL_ERROR, "Failed to write record to disk.");
      goto insert_cleanup;
    }

    pthread_mutex_lock(&table->insert_queue_lock);
    if (table->insert_queue_count == table->insert_queue_capacity) {
      table->insert_queue_capacity *= 2;
      table->insert_queue =
          realloc(table->insert_queue,
                  sizeof(pending_insert_t) * table->insert_queue_capacity);
    }
    table->insert_queue[table->insert_queue_count].id = req->id;
    table->insert_queue[table->insert_queue_count].vector = vec_cpy;
    table->insert_queue[table->insert_queue_count].metadata = meta_cpy;
    table->insert_queue[table->insert_queue_count].byte_offset =
        temp_rec.byte_offset;
    table->insert_queue_count++;
    pthread_mutex_unlock(&table->insert_queue_lock);
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

    send_all(server->clientfd, response, strlen(response));

  insert_cleanup:
    if (table)
      pthread_rwlock_unlock(&table->lock);
    if (req)
      free_insert_request(req);

    if (http_body)
      free(http_body);
    close(server->clientfd);

  } else if (strstr(header_buffer, "POST /delete-request") != NULL) {

    // @temp-delete

    delete_req_t *req = NULL;
    req = parse_delete_request(http_body);

    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      goto pending_cleanup;
    }

    if (!authenticate_request(header_buffer, req->db_name)) {
      send_error(server, UNAUTHORIZED_ACCESS, "Invalid API key.");
      goto pending_cleanup;
    }

    pthread_mutex_lock(&pending_delete_lock);
    append_pending_delete(req->db_name, req->id);
    pthread_mutex_unlock(&pending_delete_lock);
    mark_table_pending_dirty(req->db_name);
    /* @Constant-time Oracle defense: Always say success.
     */
    char response[1024];
    char json_response[] =
        "{\"status\": \"success\", \"message\": \"Delete request queued.\"}";

    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             strlen(json_response), json_response);

    send_all(server->clientfd, response, strlen(response));

  pending_cleanup:
    if (req)
      free_delete_request(req);
    if (http_body)
      free(http_body);
    close(server->clientfd);
    return;
  } else if (strstr(header_buffer, "POST /train") != NULL) {

    // train_db();
    train_req_t *req = NULL;
    FlatDb_t *db = NULL;
    cluster_t *trained_clusters = NULL;
    open_table_t *table = NULL;

    req = parse_train_request(http_body);
    if (req == NULL) {
      send_error(server, INVALID_PARAMETER, "Malformed JSON payload.");
      goto train_cleanup;
    }

    if (!authenticate_request(header_buffer, req->db_name)) {
      send_error(server, UNAUTHORIZED_ACCESS,
                 "Invalid or missing API key for this table.");
      goto train_cleanup;
    }

    table = get_or_open_table(req->db_name);
    if (table == NULL) {
      send_error(server, WRONG_REQUEST, "Database table not found.");
      goto train_cleanup;
    }
    pthread_rwlock_rdlock(&table->lock);
    db = table->db;

    if (req->k == 0 || db->count == 0 || db->count < req->k) {
      send_error(server, INVALID_PARAMETER,
                 "Not enough records to train the requested clusters.");
      goto train_cleanup;
    }

    trained_clusters =
        kmeans_build(db->records, db->count, req->k, db->dimension,
                     req->max_iterations, db->calculate_distance);

    pthread_rwlock_wrlock(&index_lock);
    int save_status =
        save_ivf_index(req->db_name, trained_clusters, req->k, db->dimension);
    pthread_rwlock_unlock(&index_lock);
    if (save_status < 0) {
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

      send_all(server->clientfd, response, strlen(response));
    }

  train_cleanup:
    if (trained_clusters && req) {
      for (uint64_t i = 0; i < req->k; i++) {
        if (trained_clusters[i].centroid_vector)
          free(trained_clusters[i].centroid_vector);
        if (trained_clusters[i].record_index)
          free(trained_clusters[i].record_index);
        if (trained_clusters[i].byte_offsets)
          free(trained_clusters[i].byte_offsets);
      }
      free(trained_clusters);
    }
    if (req)
      free_train_request(req);
    if (table)
      pthread_rwlock_unlock(&table->lock);
    if (http_body)
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

  send_all(server->clientfd, response, strlen(response));
  close(server->clientfd);
}
