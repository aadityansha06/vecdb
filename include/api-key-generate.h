#ifndef  API_H
#define API_H

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../include/db.h"


void generate_api_key(char *out_key);
void save_api_key(const char *db_name, const char *api_key);
void prune_api_key(const char *db_name);

#endif
