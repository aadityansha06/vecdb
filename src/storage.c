#include "../include/storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef  struct storage{
    char file_name[203];
    FILE *fp;
}storage_t;



storage_t *storage_init(const char* db_name){
    storage_t *storage = (storage_t *)malloc(sizeof(struct storage));
    if (storage==NULL) return NULL;
   char db_path[200];
    snprintf(db_path, sizeof(db_path), "%s.db", db_name);
     storage->fp  =fopen(db_path,"a+b"); // TODO: load existing records from file on init
    if (storage->fp==NULL) {
     perror("Fatal Error: Failed to Open file \n");
        free(storage);
        return NULL;
    }

    return storage;
    
}
