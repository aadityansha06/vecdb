#include "../../include/search/baseline-flat.h"
#include <stdint.h>
#include <math.h>









int flat_search(Record_t *records, uint64_t count, uint64_t dimension, float *query_vector, uint64_t top_k, Distance_func calculate_distance, SearchResult_t *out_results){



for (uint64_t i=0; i<top_k; i++) {
    out_results[i].calculated_distance=INFINITY;
    out_results[i].id = 0;
}

for (uint64_t i=0; i<count; i++) {
         if (records[i].is_deleted) continue;
   float dis = calculate_distance(dimension,records[i].vector,query_vector);

  if (dis < out_results[top_k - 1].calculated_distance) {
            
            int insert_idx = top_k - 1;
            while (insert_idx > 0 && dis < out_results[insert_idx - 1].calculated_distance) {
                insert_idx--;
            }

            for (int j = top_k - 1; j > insert_idx; j--) {
                out_results[j] = out_results[j - 1];
            }

            out_results[insert_idx].id = records[i].id;
            out_results[insert_idx].calculated_distance = dis;
            out_results[insert_idx].metadata = records[i].metadata;
 }

}
    return 0;

}
