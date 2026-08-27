#ifndef  STORAGE_H
#define STORAGE_H

#include "db.h"

typedef struct storage storage_t;

storage_t *storage_init(const char* db_name);
#endif

