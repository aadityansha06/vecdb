#include "../include/distance.h"
#include <math.h>



static float cosine(int dim,float vec[dim], float query[dim]){
    float mod_a=0.0 , mod_b=0.0, dot_product=0.0;
    for (int i=0; i<dim; i++) {
        dot_product+= vec[i] * query[i];
        mod_a += vec[i]*vec[i];
        mod_b += query[i]*query[i];

    }
      float similarity = dot_product/(sqrtf(mod_a)*sqrtf(mod_b));
       return 1.0f - similarity; //lower = closer, same direction as euclidean
    
}

static float euclidean(int dim,float vec[dim], float query[dim]){
    float final_sum=0.0;
    for (int i=0;i<dim; i++) {
        final_sum += (query[i]-vec[i]) *( query[i]-vec[i]) ;
    }
    float result = sqrtf(final_sum);
    return result;
 }   
Distance_func get_distance(MetricType metric){
    switch (metric) {
        case METRIC_COSINE:
            return cosine; // Returning the pointer of static function
        case METRIC_EUCLIDEAN:
            return euclidean; 
        default:
            return cosine; // Fallback 
    }
}

