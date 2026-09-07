/*
* Copyright (c) 2026 Aadityansha Verma. All rights reserved.
* This file is licensed under the Business Source License 1.1.
* See the LICENSE file in the project root for full terms.
*/


#include "../include/pending-delete.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

static pthread_mutex_t pending_append_lock = PTHREAD_MUTEX_INITIALIZER;

static int cmp_uint32(const void *a, const void *b) {
    uint64_t arg1 = *(const uint64_t *)a;
    uint64_t arg2 = *(const uint64_t *)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

int append_pending_delete(const char *db_name, uint64_t id) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "origin_data/%s/pending.bin", db_name);
    pthread_mutex_lock(&pending_append_lock);
    FILE *fp = fopen(filepath, "a+b");
    if (!fp) {
    pthread_mutex_unlock(&pending_append_lock);
}return -1;

    uint64_t timestamp = (uint64_t)time(NULL);
    
    fwrite(&id, sizeof(uint64_t), 1, fp);
    fwrite(&timestamp, sizeof(uint64_t), 1, fp);
    
    fflush(fp);
    fclose(fp);
    pthread_mutex_unlock(&pending_append_lock);
    return 0;
}

uint64_t* load_pending_deletes(const char *db_name, uint64_t *out_count) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "origin_data/%s/pending.bin", db_name);
    
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        *out_count = 0;
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    rewind(fp);

    size_t record_size = sizeof(uint64_t) + sizeof(uint64_t);
    *out_count = filesize / record_size;

    if (*out_count == 0) {
        fclose(fp);
        return NULL;
    }

    uint64_t *ids = malloc(*out_count * sizeof(uint64_t));
    for (uint64_t i = 0; i < *out_count; i++) {
        fread(&ids[i], sizeof(uint64_t), 1, fp);
        fseek(fp, sizeof(uint64_t), SEEK_CUR); // Skip timestamp to save RAM
    }
    fclose(fp);

    qsort(ids, *out_count, sizeof(uint64_t), cmp_uint32);
    
    return ids;
}

bool is_pending_delete(uint64_t id, uint64_t *pending_array, uint64_t count) {
    if (!pending_array || count == 0) return false;
    void *result = bsearch(&id, pending_array, count, sizeof(uint64_t), cmp_uint32);
    return result != NULL; 
}

int clear_pending_deletes(const char *db_name) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "origin_data/%s/pending.bin", db_name);
    FILE *fp = fopen(filepath, "wb");
    if (fp) {
        fclose(fp);
        return 0;
    }
    return -1;
}



/* 0 on success; -1 if it didn't exist(don't panic bro then have to delet manual)
 * */

int set_auto_delete_schedule(const char *db_name, uint64_t unix_timestamp) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "origin_data/%s/auto_delete_schedule.bin", db_name);
    FILE *fp = fopen(filepath, "wb");
    if (!fp) return -1;
    size_t written = fwrite(&unix_timestamp, sizeof(uint64_t), 1, fp);
    fclose(fp);
    return (written == 1) ? 0 : -1;
}

uint64_t get_auto_delete_schedule(const char *db_name) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "origin_data/%s/auto_delete_schedule.bin", db_name);
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return 0;
    uint64_t ts = 0;
    if (fread(&ts, sizeof(uint64_t), 1, fp) != 1) ts = 0;
    fclose(fp);
    return ts;
}

int clear_auto_delete_schedule(const char *db_name) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "origin_data/%s/auto_delete_schedule.bin", db_name);
    return remove(filepath); }
