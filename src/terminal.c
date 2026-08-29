#include "../include/terminal.h"
#include "../include/db.h"
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "../include/search/baseline-flat.h"
#include <stdlib.h>

int run_tui(int argc, char *argv[]) {
  int exit_request = 0;
  FlatDb_t *master_db = NULL;
  if (argc == 1) {
    printf("Welcome to originDB\n");
    printf("Enter origin --help for help\n");
    printf("Enter origin --exit to exit\n");

    int exit_request = 0;
    while (exit_request != 1) {
      printf("\norigin> ");

      char command[256];
      if (fgets(command, sizeof(command), stdin) == NULL)
        break;
      command[strcspn(command, "\n")] = '\0';

      char cmd[20], arg[20], db_name[200];
      uint64_t dimension, initial_capacity;
      int metric_int;

      int ret = sscanf(command, "%s %s %s %" SCNu64 " %" SCNu64 " %d", cmd, arg,
                       db_name, &dimension, &initial_capacity, &metric_int);

      if (ret == 6 && strcmp(cmd, "origin") == 0 && strcmp(arg, "init") == 0) {

        if (master_db != NULL) {
          printf("Database already initialized!\n");
          continue;
        }

        master_db = db_init(db_name, dimension, initial_capacity,
                            (MetricType)metric_int);

        if (master_db != NULL) {
          printf("Success: Initialized DB '%s' (Dim: %" PRIu64 ", Cap: %" PRIu64
                 ")\n",
                 db_name, dimension, initial_capacity);
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
