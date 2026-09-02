#include <stdio.h>
#include <stdlib.h>
#include "../include/db.h"
#include "../include/storage.h"
#include "../include/search/kmeans.h"
#define METRIC_L2 1 

int main() {
    printf("--- Initializing OriginDB ---\n");
    
    FlatDb_t *db = db_init("colors", 3, 10, METRIC_L2);
    if (!db) {
        printf("Failed to initialize database.\n");
        return -1;
    }

    printf("--- Inserting Data ---\n");
     // dummy RGB vectors
    float v1[] = {1.0, 0.0, 0.0}; db_insert(db, 101, v1, "Pure Red");
    float v2[] = {0.0, 1.0, 0.0}; db_insert(db, 102, v2, "Pure Green");
    float v3[] = {0.0, 0.0, 1.0}; db_insert(db, 103, v3, "Pure Blue");
    float v4[] = {0.9, 0.1, 0.0}; db_insert(db, 104, v4, "Dark Red");
    float v5[] = {0.1, 0.8, 0.1}; db_insert(db, 105, v5, "Forest Green");

    printf("--- Training K-Means IVF Index ---\n");
    uint64_t k = 2;
    cluster_t *trained_clusters = kmeans_build(db->records, db->count, k, 3, 10, db->calculate_distance);

    printf("--- Saving Index to Disk ---\n");
    save_ivf_index("colors", trained_clusters, k, 3);

    printf("--- Loading Index from Disk ---\n");
    uint64_t loaded_k;
    cluster_t *disk_clusters = load_ivf_index("colors", &loaded_k, 3);

    if (disk_clusters == NULL) {
        printf("Failed to load clusters from disk.\n");
        return -1;
    }

    printf("--- Executing ANN Search ---\n");
    float query[] = {0.95, 0.05, 0.0}; 
    uint64_t top_k = 2;
    
    uint64_t nprobe = 1; 

    SearchResult_t results[2];
    db_ann_search(db, query, top_k, nprobe, disk_clusters, loaded_k, results);

    printf("\nSearch Results for Query [0.95, 0.05, 0.0]:\n");
    for (uint64_t i = 0; i < top_k; i++) {
        if (results[i].id != 0) {
            printf("Rank %lu | ID: %lu | Distance: %f | Metadata: %s\n", 
                   i + 1, 
                   results[i].id, 
                   results[i].calculated_distance, 
                   results[i].metadata ? results[i].metadata : "NULL");
            
            if (results[i].metadata) free(results[i].metadata);
        }
    }

    printf("\nEngine test complete.\n");
    return 0;
}
