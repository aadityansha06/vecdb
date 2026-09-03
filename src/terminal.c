#include "../include/terminal.h"
#include "../include/db.h"
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "../include/api-key-generate.h"
#include "../include/search/baseline-flat.h"
#include <unistd.h>
#include <stdlib.h>
#include "../include/server.h"
int run_tui(int argc, char *argv[]) {
  FlatDb_t *master_db = NULL;
  if (argc == 1) {
    printf("Welcome to originDB\n");
    printf("Enter origin --help for help\n");
    printf("Enter origin --exit to exit\n");

    int exit_request = 0;
    while (exit_request != 1) {
      printf("\norigin> ");

      char command[256];
      if (fgets(command, sizeof(command), stdin) == NULL) break;
      command[strcspn(command, "\n")] = '\0';

      char cmd[20], arg[20], db_name[200];
      uint64_t dimension = 0, initial_capacity = 0;
      int metric_int = 0;

      int ret = sscanf(command, "%s %s %s %" SCNu64 " %" SCNu64 " %d", cmd, arg,
                       db_name, &dimension, &initial_capacity, &metric_int);

      // --- 1. NEW SERVER ROUTE ---
      if (ret >= 3 && strcmp(cmd, "origin") == 0 && strcmp(arg, "server") == 0) {
          int port = atoi(db_name);
          if (port <= 0) port = 8080; // Fallback to 8080 if parsing fails
          
          printf("Launching OriginDB TCP Server on port %d...\n", port);
          server_init(port); // This will block and run the server loop
          continue;
      }

      // --- 2. FIXED INIT ROUTE ---
      else if (ret == 6 && strcmp(cmd, "origin") == 0 && strcmp(arg, "init") == 0) {
        
        // FIX: Declare and format the folder path before checking it!
        char folder_path[256];
        snprintf(folder_path, sizeof(folder_path), "origin_data/%s", db_name);
        
        if (access(folder_path, F_OK) == 0) {
            printf("Error: Table '%s' already exists. Use 'origin open' instead.\n", db_name);
            continue;
        }

        if (master_db != NULL) {
          printf("Database already initialized in this session!\n");
          continue;
        }

        master_db = db_init(db_name, dimension, initial_capacity, (MetricType)metric_int);

        if (master_db != NULL) {
          char new_api_key[65];
          generate_api_key(new_api_key);
          save_api_key(db_name, new_api_key);

          printf("\n================================================================\n");
          printf("SUCCESS: Initialized DB '%s'\n", db_name);
          printf("Your API Key for '%s' is:\n%s\n", db_name, new_api_key);
          printf("================================================================\n\n");
        }   
      }// --- THE MISSING OPEN COMMAND ---
      // Expected usage: origin open <db_name> <dimension> <metric>
      else if (ret >= 5 && strcmp(cmd, "origin") == 0 && strcmp(arg, "open") == 0) {
          
          if (master_db != NULL) {
              printf("Error: A database is already open in this session!\n");
              continue;
          }

          // Use the db_open guardrail we wrote for the server!
          master_db = db_open(db_name, dimension, (MetricType)metric_int);
          
          if (master_db != NULL) {
              printf("\n================================================================\n");
              printf("SUCCESS: Opened existing DB '%s' (Dim: %" PRIu64 ")\n", db_name, dimension);
              printf("================================================================\n\n");
          } else {
              printf("Error: Table '%s' does not exist. Use 'origin init' first.\n", db_name);
          }
      }else if (ret >= 3 && strcmp(cmd, "origin") == 0 &&
               strcmp(arg, "insert") == 0) {
        if (master_db == NULL) {
          printf("Error: Initialize DB first using 'origin init ...'\n");
          continue;
        }

        uint32_t id;
        char metadata[256] = {0};

       int parsed = sscanf(command, "%*s %*s %u %255s", &id, metadata);
        if (parsed < 1) {
          printf("Usage: origin insert <id> [metadata_string_no_spaces]\n");
          continue;
        }

      
        float *vec = (float *)malloc(master_db->dimension * sizeof(float));

              printf("Enter %" PRIu64 " float values separated by spaces: ",
               master_db->dimension);
        for (uint64_t i = 0; i < master_db->dimension; i++) {
          scanf("%f", &vec[i]);
        }

       int c;
        while ((c = getchar()) != '\n' && c != EOF)
          ;

              char *meta_ptr = (parsed == 2) ? metadata : NULL;
        if (db_insert(master_db, id, vec, meta_ptr) == 0) {
          printf("Success: Inserted vector ID %u\n", id);
        }
        free(vec);
      } else if (ret >= 3 && strcmp(cmd, "origin") == 0 &&
                 strcmp(arg, "search") == 0) {
        if (master_db == NULL) {
          printf("Error: Initialize DB first!\n");
          continue;
        }

        uint32_t top_k;
        if (sscanf(command, "%*s %*s %u", &top_k) < 1) {
          printf("Usage: origin search <top_k>\n");
          continue;
        }

        float *q_vec = (float *)malloc(master_db->dimension * sizeof(float));
        SearchResult_t *results =
            (SearchResult_t *)malloc(top_k * sizeof(SearchResult_t));

        printf("Enter %" PRIu64 " floats for query vector: ",
               master_db->dimension);
        for (uint64_t i = 0; i < master_db->dimension; i++) {
          scanf("%f", &q_vec[i]);
        }

        int c;
        while ((c = getchar()) != '\n' && c != EOF)
          ;

        flat_search(master_db->records, master_db->count, master_db->dimension,
                    q_vec, top_k, master_db->calculate_distance, results);

          printf("\n--- Top %u Results ---\n", top_k);
          for (uint32_t i = 0; i < top_k; i++) {
              printf("Rank %u | ID: %" PRIu64 " | Distance: %f | Meta: %s\n", 
                     i + 1, 
                     results[i].id, 
                     results[i].calculated_distance,
                     results[i].metadata ? results[i].metadata : "NULL");
          }

        free(q_vec);
        free(results);
      }

      else if (strcmp(command, "origin --exit") == 0) {
        exit_request = 1;
      }
    }
  }

  return 0;
}
