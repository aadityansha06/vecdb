#include "../include/terminal.h"
#include "../include/db.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "../include/api-key-generate.h"
#include "../include/search/baseline-flat.h"
#include <unistd.h>
#include <stdlib.h>
#include "../include/server.h"

/* Return codes for handle_command(): whether the REPL should keep going. */
#define TUI_CONTINUE 1
#define TUI_EXIT 0

static void print_help(void) {
  printf("\n");
  printf("OriginDB - a disk-backed vector database (toy/WIP)\n\n");
  printf("Usage inside the prompt: origin <command> [args...]\n\n");
  printf("Commands:\n");
  printf("  origin init <name> <dimension> <capacity> <metric>\n");
  printf("      Create a brand new table.\n");
  printf("      metric: 0 = cosine, 1 = euclidean\n");
  printf("      example: origin init movies 128 1000 0\n\n");
  printf("  origin open <name> <dimension> <metric>\n");
  printf("      Open a table that was already created with 'origin init'.\n");
  printf("      example: origin open movies 128 0\n\n");
  printf("  origin insert <id> [metadata]\n");
  printf("      Insert a vector into the currently open/initialized table.\n");
  printf("      <id> must be a non-negative integer. [metadata] is an\n");
  printf("      optional single word (no spaces) attached to the record.\n");
  printf("      You'll be prompted to type the vector's float values next.\n");
  printf("      example: origin insert 1 my-note\n\n");
  printf("  origin search <top_k>\n");
  printf("      Find the <top_k> nearest neighbours to a query vector.\n");
  printf("      You'll be prompted to type the query vector's floats next.\n");
  printf("      example: origin search 5\n\n");
  printf("  origin server [port]\n");
  printf("      Launch the TCP server (blocking). Defaults to port 8080.\n");
  printf("      example: origin server 9090\n\n");
  printf("  origin --help, origin help\n");
  printf("      Show this message.\n\n");
  printf("  origin --exit, origin exit\n");
  printf("      Quit OriginDB.\n\n");
}

static int count_words(const char *s) {
  int n = 0;
  int in_word = 0;
  for (int i = 0; s[i] != '\0'; i++) {
    if (!isspace((unsigned char)s[i])) {
      if (!in_word) {
        n++;
        in_word = 1;
      }
    } else {
      in_word = 0;
    }
  }
  return n;
}

static int read_vector(uint64_t dimension, float *out) {
  for (uint64_t i = 0; i < dimension; i++) {
    if (scanf("%f", &out[i]) != 1) {
      printf("Error: expected a number for value %" PRIu64
             " of %" PRIu64 ", got something else. Aborting this command.\n",
             i + 1, dimension);
      int c;
      while ((c = getchar()) != '\n' && c != EOF)
        ;
      return 0;
    }
  }
  int c;
  while ((c = getchar()) != '\n' && c != EOF)
    ;
  return 1;
}

static int handle_command(char *command, FlatDb_t **master_db) {
  if (count_words(command) == 0) {
    return TUI_CONTINUE; 
  }

  char cmd[20] = {0}, arg[20] = {0}, db_name[200] = {0};
  uint64_t dimension = 0, initial_capacity = 0;
  int metric_int = 0;

  int ret = sscanf(command, "%19s %19s %199s %" SCNu64 " %" SCNu64 " %d", cmd,
                    arg, db_name, &dimension, &initial_capacity, &metric_int);
  int words = count_words(command);

  if (strcmp(cmd, "origin") != 0) {
    printf("Unknown command '%s'. Every command starts with 'origin'. Try "
           "'origin --help'.\n",
           cmd);
    return TUI_CONTINUE;
  }

  if (ret < 2) {
    printf("Missing command after 'origin'. Try 'origin --help' for the "
           "list of commands.\n");
    return TUI_CONTINUE;
  }

  /* --- help / exit --- */
  if (strcmp(arg, "--help") == 0 || strcmp(arg, "help") == 0 ||
      strcmp(arg, "-h") == 0) {
    print_help();
    return TUI_CONTINUE;
  }

  if (strcmp(arg, "--exit") == 0 || strcmp(arg, "exit") == 0) {
    printf("Goodbye.\n");
    return TUI_EXIT;
  }

  /* --- server --- */
  if (strcmp(arg, "server") == 0) {
    if (words > 3) {
      printf("Usage: origin server [port]\n");
      return TUI_CONTINUE;
    }
    int port = 8080;
    if (words == 3) {
      port = atoi(db_name);
      if (port <= 0) {
        printf("Error: '%s' is not a valid port number. Usage: origin "
               "server [port]\n",
               db_name);
        return TUI_CONTINUE;
      }
    }
    printf("Launching OriginDB TCP Server on port %d...\n", port);
    server_init(port); /* blocks and runs the server loop */
    return TUI_CONTINUE;
  }

  /* --- init --- */
  if (strcmp(arg, "init") == 0) {
    if (words != 6) {
      printf("Usage: origin init <name> <dimension> <capacity> <metric>\n");
      printf("  got %d argument(s), need exactly 4: name, dimension, "
             "capacity, metric (0=cosine, 1=euclidean)\n",
             words - 2);
      printf("  example: origin init movies 128 1000 0\n");
      return TUI_CONTINUE;
    }
    if (dimension == 0) {
      printf("Error: dimension must be a positive integer.\n");
      return TUI_CONTINUE;
    }
    if (initial_capacity == 0) {
      printf("Error: capacity must be a positive integer.\n");
      return TUI_CONTINUE;
    }
    if (metric_int != 0 && metric_int != 1) {
      printf("Error: metric must be 0 (cosine) or 1 (euclidean), got %d.\n",
             metric_int);
      return TUI_CONTINUE;
    }

    char folder_path[256];
    snprintf(folder_path, sizeof(folder_path), "origin_data/%s", db_name);

    if (access(folder_path, F_OK) == 0) {
      printf("Error: Table '%s' already exists. Use 'origin open' "
             "instead.\n",
             db_name);
      return TUI_CONTINUE;
    }

    if (*master_db != NULL) {
      printf("Error: A database is already open in this session. Restart "
             "OriginDB to work with a different table.\n");
      return TUI_CONTINUE;
    }

    *master_db =
        db_init(db_name, dimension, initial_capacity, (MetricType)metric_int);

    if (*master_db != NULL) {
      char new_api_key[65];
      generate_api_key(new_api_key);
      save_api_key(db_name, new_api_key);

      printf("\n================================================================\n");
      printf("SUCCESS: Initialized DB '%s'\n", db_name);
      printf("Your API Key for '%s' is:\n%s\n", db_name, new_api_key);
      printf("================================================================\n\n");
    } else {
      printf("Error: Failed to initialize table '%s'. Check permissions on "
             "./origin_data and try again.\n",
             db_name);
    }
    return TUI_CONTINUE;
  }

  /* --- open --- */
  if (strcmp(arg, "open") == 0) {
    if (words != 5) {
      printf("Usage: origin open <name> <dimension> <metric>\n");
      printf("  got %d argument(s), need exactly 3: name, dimension, "
             "metric (0=cosine, 1=euclidean)\n",
             words - 2);
      printf("  example: origin open movies 128 0\n");
      return TUI_CONTINUE;
    }
    if (dimension == 0) {
      printf("Error: dimension must be a positive integer.\n");
      return TUI_CONTINUE;
    }
    if (metric_int != 0 && metric_int != 1) {
      printf("Error: metric must be 0 (cosine) or 1 (euclidean), got %d.\n",
             metric_int);
      return TUI_CONTINUE;
    }
    if (*master_db != NULL) {
      printf("Error: A database is already open in this session. Restart "
             "OriginDB to work with a different table.\n");
      return TUI_CONTINUE;
    }

    *master_db = db_open(db_name);

    if (*master_db != NULL) {
      printf("\n================================================================\n");
      printf("SUCCESS: Opened existing DB '%s' (Dim: %" PRIu64 ")\n", db_name,
             dimension);
      printf("================================================================\n\n");
    } else {
      printf("Error: Table '%s' does not exist. Use 'origin init' first.\n",
             db_name);
    }
    return TUI_CONTINUE;
  }

  /* --- insert --- */
  if (strcmp(arg, "insert") == 0) {
    if (*master_db == NULL) {
      printf("Error: No database open. Run 'origin init ...' or 'origin "
             "open ...' first.\n");
      return TUI_CONTINUE;
    }
    if (words < 3 || words > 4) {
      printf("Usage: origin insert <id> [metadata]\n");
      printf("  got %d argument(s), need 1 or 2: id, and optionally one "
             "metadata word (no spaces)\n",
             words - 2);
      printf("  example: origin insert 1 my-note\n");
      return TUI_CONTINUE;
    }

    uint32_t id;
    char metadata[256] = {0};
    int parsed = sscanf(command, "%*s %*s %u %255s", &id, metadata);
    if (parsed < 1) {
      printf("Error: <id> must be a non-negative integer.\n");
      printf("Usage: origin insert <id> [metadata]\n");
      return TUI_CONTINUE;
    }

    float *vec = (float *)malloc((*master_db)->dimension * sizeof(float));
    if (vec == NULL) {
      printf("Error: out of memory while allocating the vector buffer.\n");
      return TUI_CONTINUE;
    }

    printf("Enter %" PRIu64 " float values separated by spaces: ",
           (*master_db)->dimension);
    if (!read_vector((*master_db)->dimension, vec)) {
      free(vec);
      return TUI_CONTINUE;
    }

    char *meta_ptr = (parsed == 2) ? metadata : NULL;
    if (db_insert(*master_db, id, vec, meta_ptr) == 0) {
      printf("Success: Inserted vector ID %u\n", id);
    } else {
      printf("Error: Insert failed (see message above).\n");
    }
    free(vec);
    return TUI_CONTINUE;
  }

  /* --- search --- */
  if (strcmp(arg, "search") == 0) {
    if (*master_db == NULL) {
      printf("Error: No database open. Run 'origin init ...' or 'origin "
             "open ...' first.\n");
      return TUI_CONTINUE;
    }
    if (words != 3) {
      printf("Usage: origin search <top_k>\n");
      printf("  got %d argument(s), need exactly 1: top_k\n", words - 2);
      printf("  example: origin search 5\n");
      return TUI_CONTINUE;
    }

    uint32_t top_k;
    if (sscanf(command, "%*s %*s %u", &top_k) < 1 || top_k == 0) {
      printf("Error: <top_k> must be a positive integer.\n");
      printf("Usage: origin search <top_k>\n");
      return TUI_CONTINUE;
    }
    if (top_k > (*master_db)->count && (*master_db)->count > 0) {
      printf("Note: top_k (%u) is larger than the number of stored records "
             "(%" PRIu64 "); some results will be empty.\n",
             top_k, (*master_db)->count);
    }

    float *q_vec = (float *)malloc((*master_db)->dimension * sizeof(float));
    SearchResult_t *results =
        (SearchResult_t *)malloc(top_k * sizeof(SearchResult_t));
    if (q_vec == NULL || results == NULL) {
      printf("Error: out of memory while allocating search buffers.\n");
      free(q_vec);
      free(results);
      return TUI_CONTINUE;
    }

    printf("Enter %" PRIu64 " floats for query vector: ",
           (*master_db)->dimension);
    if (!read_vector((*master_db)->dimension, q_vec)) {
      free(q_vec);
      free(results);
      return TUI_CONTINUE;
    }

    flat_search((*master_db)->records, (*master_db)->count,
                (*master_db)->dimension, q_vec, top_k,
                (*master_db)->calculate_distance, results);

    printf("\n--- Top %u Results ---\n", top_k);
    for (uint32_t i = 0; i < top_k; i++) {
      printf("Rank %u | ID: %" PRIu64 " | Distance: %f | Meta: %s\n", i + 1,
             results[i].id, results[i].calculated_distance,
             results[i].metadata ? results[i].metadata : "NULL");
    }

    free(q_vec);
    free(results);
    return TUI_CONTINUE;
  }
  /* --- delete --- */
  if (strcmp(arg, "delete") == 0) {
    if (*master_db == NULL) {
      printf("Error: No database open. Run 'origin init ...' or 'origin open ...' first.\n");
      return TUI_CONTINUE;
    }
    
    if (words != 3) {
      printf("Usage: origin delete <id>\n");
      printf("  example: origin delete 42\n");
      return TUI_CONTINUE;
    }

    uint64_t id_to_delete; 
    
    if (sscanf(command, "%*s %*s  %" PRIu64"\n", &id_to_delete) < 1) {
      printf("Error: <id> must be a valid positive integer.\n");
      printf("Usage: origin delete <id>\n");
      return TUI_CONTINUE;
    }

    if (db_delete(*master_db, id_to_delete) == 0) {
      printf("SUCCESS: Vector ID  %" PRIu64"  has been permanently marked as deleted on disk.\n", id_to_delete);
    } else {
      printf("Error: Vector ID  %" PRIu64" not found or has already been deleted.\n", id_to_delete);
    }
    
    return TUI_CONTINUE;
  }

  printf("Unknown command 'origin %s'. Try 'origin --help' for the list of "
         "commands.\n",
         arg);
  return TUI_CONTINUE;
}

int run_tui(int argc, char *argv[]) {
  FlatDb_t *master_db = NULL;

  if (argc > 1) {
    char command[1024] = "origin";
    for (int i = 1; i < argc; i++) {
      strncat(command, " ", sizeof(command) - strlen(command) - 1);
      strncat(command, argv[i], sizeof(command) - strlen(command) - 1);
    }
    handle_command(command, &master_db);
    return 0;
  }

  printf("Welcome to originDB\n");
  printf("Enter origin --help for help\n");
  printf("Enter origin --exit to exit\n");

  int exit_request = 0;
  while (!exit_request) {
    printf("\norigin> ");

    char command[256];
    if (fgets(command, sizeof(command), stdin) == NULL) {
      printf("\nGoodbye.\n");
      break;
    }
    command[strcspn(command, "\n")] = '\0';

    if (handle_command(command, &master_db) == TUI_EXIT) {
      exit_request = 1;
    }
  }

  return 0;
}
