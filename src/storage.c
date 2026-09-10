/*
 * Copyright (c) 2026 Aadityansha Verma. All rights reserved.
 * This file is licensed under the Business Source License 1.1.
 * See the LICENSE file in the project root for full terms.
 */

#include "../include/storage.h"

#include "../include/db.h"
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

storage_t *storage_init(const char *table_name) {
  storage_t *storage = (storage_t *)malloc(sizeof(struct storage));
  if (storage == NULL)
    return NULL;
  char db_path[512];
  char folder_path[256];

  if (mkdir("origin_data", 0777) == -1 && errno != EEXIST) {
    free(storage);
    return NULL;
  }

  snprintf(folder_path, sizeof(folder_path), "origin_data/%s", table_name);

  if (mkdir(folder_path, 0777) == -1) {
    if (errno != EEXIST) {
      free(storage);
      return NULL;
    }
  }
  snprintf(db_path, sizeof(db_path), "%s/data.db", folder_path);
  FILE *touch = fopen(db_path, "a+b");
  if (touch)
    fclose(touch);
  storage->fp = fopen(db_path, "r+b");
  if (storage->fp == NULL) {
    perror("Fatal Error: Failed to Open file \n");
    free(storage);
    return NULL;
  }
  fseek(storage->fp, 0, SEEK_END);
  long sz = ftell(storage->fp);
  storage->file_size = (sz > 0) ? (size_t)sz : 0;

  if (storage->file_size > 0) {
    storage->mmap_data = mmap(NULL, storage->file_size, PROT_READ, MAP_SHARED,
                              fileno(storage->fp), 0);
    if (storage->mmap_data == MAP_FAILED)
      storage->mmap_data = NULL;
  } else {
    storage->mmap_data = NULL;
  }
  strncpy(storage->file_name, db_path, sizeof(storage->file_name) - 1);
  storage->file_name[sizeof(storage->file_name) - 1] = '\0';
  return storage;
}

int storage_write_record(storage_t *storage, Record_t *record,
                         uint64_t dimension) {

  if (storage == NULL || record == NULL || storage->fp == NULL) {
    return -1;
  }
  fseek(storage->fp, 0, SEEK_END);
  size_t written = fwrite(&record->id, sizeof(uint64_t), 1, storage->fp);
  if (written != 1)
    return -1;

  written = fwrite(&record->is_deleted, sizeof(bool), 1, storage->fp);
  if (written != 1)
    return -1;

  written = fwrite(record->vector, sizeof(float), dimension, storage->fp);
  if (written != dimension)
    return -1;

  size_t len = 0;
  if (record->metadata != NULL) {
    len = strlen(record->metadata);
  }
  written = fwrite(&len, sizeof(size_t), 1, storage->fp);
  if (written != 1)
    return -1;
  if (len > 0) {
    written = fwrite(record->metadata, sizeof(char), len, storage->fp);
    if (written != len)
      return -1;
  }

  int x = fflush(storage->fp);
  if (x != 0)
    return -1;
  fsync(fileno(storage->fp));

  return 0;
}

/**
 * @brief Reads the next record from the disk into the provided record struct
 * for ENN.
 *
 * @returns 1 on success, 0 on End of File (EOF), or -1 on error.
 * @param storage Pointer for the file context
 * @param record Pointer to the specific record in RAM to be written.
 * @param dimension The dimensionality of the vector (needed to malloc RAM for
 * vector).
 */

int storage_load_record(storage_t *storage, Record_t *record,
                        uint64_t dimension) {
  if (storage == NULL || storage->mmap_data == NULL || record == NULL)
    return -1;

  long current_pos = ftell(storage->fp);
  if (current_pos < 0 || (size_t)current_pos >= storage->file_size)
    return 0;

  record->byte_offset = (uint64_t)current_pos;
  uint8_t *ptr = storage->mmap_data + current_pos;

  record->id = *(uint64_t *)ptr;
  ptr += sizeof(uint64_t);

  record->is_deleted = *(bool *)ptr;
  ptr += sizeof(bool);

  record->vector = (float *)ptr;
  ptr += sizeof(float) * dimension;

  size_t len = *(size_t *)ptr;
  ptr += sizeof(size_t);

  if (len > 0) {
    char *meta_copy = (char *)malloc(len + 1);
    if (meta_copy != NULL) {
      memcpy(meta_copy, ptr, len);
      meta_copy[len] = '\0';
    }
    record->metadata = meta_copy;
    ptr += len;
  } else {
    record->metadata = NULL;
  }

  record->is_mmap = true;
  long bytes_read = (long)(ptr - (storage->mmap_data + current_pos));
  fseek(storage->fp, bytes_read, SEEK_CUR);

  return 1;
}

uint64_t storage_current_offset(storage_t *storage) {
  if (storage == NULL || storage->fp == NULL)
    return 0;
  if (fseek(storage->fp, 0, SEEK_END) != 0)
    return 0;
  long pos = ftell(storage->fp);
  return pos < 0 ? 0 : (uint64_t)pos;
}
int save_ivf_index(const char *table_name, cluster_t *clusters, uint64_t k,
                   uint64_t dimension) {

  char index_path[256];
  snprintf(index_path, sizeof(index_path), "origin_data/%s/ivf_index.bin",
           table_name);
  FILE *fp = fopen(index_path, "wb");
  if (fp == NULL)
    return -1;

  size_t written = fwrite(&k, sizeof(uint64_t), 1, fp);
  if (written != 1) {
    fclose(fp);
    return -1;
  }
  written = fwrite(&dimension, sizeof(uint64_t), 1, fp);
  if (written != 1) {
    fclose(fp);
    return -1;
  }

  for (uint64_t i = 0; i < k; i++) {

    written = fwrite(clusters[i].centroid_vector, sizeof(float), dimension, fp);
    if (written != dimension) {
      fclose(fp);
      return -1;
    }
    written = fwrite(&clusters[i].count, sizeof(uint64_t), 1, fp);
    if (written != 1) {
      fclose(fp);
      return -1;
    }
    written = fwrite(&clusters[i].capacity, sizeof(uint64_t), 1, fp);
    if (written != 1) {
      fclose(fp);
      return -1;
    }

    written = fwrite(clusters[i].byte_offsets, sizeof(uint64_t),
                     clusters[i].count, fp);
    if (written != clusters[i].count) {
      fclose(fp);
      return -1;
    }
  }
  fclose(fp);
  return 0;
}

cluster_t *load_ivf_index(const char *table_name, uint64_t *out_k,
                          uint64_t dimension) {
  char index_path[256];
  snprintf(index_path, sizeof(index_path), "origin_data/%s/ivf_index.bin",
           table_name);
  FILE *fp = fopen(index_path, "rb");
  if (fp == NULL)
    return NULL;

  size_t read = fread(out_k, sizeof(uint64_t), 1, fp);
  if (read != 1) {
    fclose(fp);
    return NULL;
  }

  read = fread(&dimension, sizeof(uint64_t), 1, fp);
  if (read != 1) {
    fclose(fp);
    return NULL;
  }

  cluster_t *cluster = (cluster_t *)calloc(*out_k, sizeof(cluster_t));
  if (cluster == NULL) {
    fclose(fp);
    return NULL;
  }

  for (uint64_t i = 0; i < (*out_k); i++) {
    cluster[i].centroid_vector = (float *)malloc(sizeof(float) * dimension);
    read = fread(cluster[i].centroid_vector, sizeof(float), dimension, fp);
    if (read != dimension) {
      for (uint64_t j = 0; j <= i; j++) {
        if (cluster[j].centroid_vector)
          free(cluster[j].centroid_vector);
        if (cluster[j].byte_offsets)
          free(cluster[j].byte_offsets);
      }
      free(cluster);
      fclose(fp);
      return NULL;
    }

    read = fread(&cluster[i].count, sizeof(uint64_t), 1, fp);
    if (read != 1) {
      for (uint64_t j = 0; j <= i; j++) {
        if (cluster[j].centroid_vector)
          free(cluster[j].centroid_vector);
        if (cluster[j].byte_offsets)
          free(cluster[j].byte_offsets);
      }
      free(cluster);
      fclose(fp);
      return NULL;
    }
    read = fread(&cluster[i].capacity, sizeof(uint64_t), 1, fp);
    if (read != 1) {
      for (uint64_t j = 0; j <= i; j++) {
        if (cluster[j].centroid_vector)
          free(cluster[j].centroid_vector);
        if (cluster[j].byte_offsets)
          free(cluster[j].byte_offsets);
      }
      free(cluster);
      fclose(fp);
      return NULL;
    }
    cluster[i].byte_offsets = malloc(sizeof(uint64_t) * cluster[i].count);

    if (cluster[i].count > 0) {
      read = fread(cluster[i].byte_offsets, sizeof(uint64_t), cluster[i].count,
                   fp);
      if (read != cluster[i].count) {
        for (uint64_t j = 0; j <= i; j++) {
          if (cluster[j].centroid_vector)
            free(cluster[j].centroid_vector);
          if (cluster[j].byte_offsets)
            free(cluster[j].byte_offsets);
        }
        free(cluster);
        fclose(fp);
        return NULL;
      }
    }
  }
  fclose(fp);
  return cluster;
}

int storage_fetch_by_offset(storage_t *storage, uint64_t byte_offset,
                            Record_t *record, uint64_t dimension) {
  if (!storage || !storage->mmap_data || byte_offset >= storage->file_size)
    return -1;

  uint8_t *ptr = storage->mmap_data + byte_offset;

  record->id = *(uint64_t *)ptr;
  ptr += sizeof(uint64_t);

  record->is_deleted = *(bool *)ptr;
  ptr += sizeof(bool);

  record->vector = (float *)ptr;
  ptr += sizeof(float) * dimension;
  size_t len = *(size_t *)ptr;
  ptr += sizeof(size_t);

  if (len > 0) {
    char *meta_copy = (char *)malloc(len + 1);
    if (meta_copy != NULL) {
      memcpy(meta_copy, ptr, len);
      meta_copy[len] = '\0';
    }
    record->metadata = meta_copy;
  } else {
    record->metadata = NULL;
  }
  return 1;
}

void storage_close(storage_t *storage) {
  if (storage == NULL)
    return;
  if (storage->mmap_data && storage->mmap_data != MAP_FAILED) {
    munmap(storage->mmap_data, storage->file_size);
  }
  if (storage->fp != NULL) {
    fclose(storage->fp);
  }

  free(storage);
}

int storage_remap(storage_t *storage) {
  if (storage == NULL || storage->fp == NULL)
    return -1;
  fseek(storage->fp, 0, SEEK_END);
  long sz = ftell(storage->fp);
  size_t new_size = (sz > 0) ? (size_t)sz : 0;

  if (new_size == storage->file_size)
    return 0;
  if (storage->mmap_data && storage->mmap_data != MAP_FAILED) {
    munmap(storage->mmap_data, storage->file_size);
  }

  storage->file_size = new_size;
  if (new_size > 0) {
    storage->mmap_data =
        mmap(NULL, new_size, PROT_READ, MAP_SHARED, fileno(storage->fp), 0);
    if (storage->mmap_data == MAP_FAILED)
      storage->mmap_data = NULL;
  } else {
    storage->mmap_data = NULL;
  }
  return 0;
}


int storage_compact(storage_t *storage, Record_t **records_ptr,
                    uint64_t *count, uint64_t dimension) {
  if (storage == NULL || records_ptr == NULL || count == NULL)
    return -1;

  Record_t *records = *records_ptr;
  char tmp_path[600];
  snprintf(tmp_path, sizeof(tmp_path), "%s.compact.tmp", storage->file_name);

  FILE *out = fopen(tmp_path, "w+b");
  if (out == NULL) return -1;

  uint64_t buf_cap = (*count > 0) ? *count : 1;
  Record_t *new_records = (Record_t *)malloc(sizeof(Record_t) * buf_cap);
  uint64_t *new_offsets = (uint64_t *)malloc(sizeof(uint64_t) * buf_cap);
  bool *was_mmap = (bool *)malloc(sizeof(bool) * buf_cap);

  /* Pointers to free ONLY after compaction succeeds */
  void **to_free_meta = (void **)malloc(sizeof(void *) * buf_cap);
  void **to_free_vec = (void **)malloc(sizeof(void *) * buf_cap);

  if (!new_records || !new_offsets || !was_mmap || !to_free_meta || !to_free_vec) {
    fclose(out);
    remove(tmp_path);
    free(new_records); free(new_offsets); free(was_mmap);
    free(to_free_meta); free(to_free_vec);
    return -1;
  }

  uint64_t new_count = 0;
  uint64_t to_free_count = 0;
  int failed = 0;

  for (uint64_t i = 0; i < *count; i++) {
    if (records[i].is_deleted) {
      to_free_meta[to_free_count] = records[i].metadata;
      to_free_vec[to_free_count] = records[i].is_mmap ? NULL : records[i].vector;
      to_free_count++;
      continue;
    }

    uint64_t new_offset = (uint64_t)ftell(out);
    if (fwrite(&records[i].id, sizeof(uint64_t), 1, out) != 1) { failed = 1; break; }
    if (fwrite(&records[i].is_deleted, sizeof(bool), 1, out) != 1) { failed = 1; break; }
    if (fwrite(records[i].vector, sizeof(float), dimension, out) != dimension) { failed = 1; break; }
    
    size_t len = records[i].metadata ? strlen(records[i].metadata) : 0;
    if (fwrite(&len, sizeof(size_t), 1, out) != 1) { failed = 1; break; }
    if (len > 0 && fwrite(records[i].metadata, sizeof(char), len, out) != len) { failed = 1; break; }
    
    new_records[new_count] = records[i];
    new_offsets[new_count] = new_offset;
    was_mmap[new_count] = records[i].is_mmap;
    new_count++;
  }

  if (failed || fflush(out) != 0) {
    fclose(out);
    remove(tmp_path);
    free(new_records); free(new_offsets); free(was_mmap);
    free(to_free_meta); free(to_free_vec);
    return -1; /* Original array remains 100% valid */
  }

  fsync(fileno(out));
  fclose(out);

  if (storage->mmap_data && storage->mmap_data != MAP_FAILED) {
    munmap(storage->mmap_data, storage->file_size);
    storage->mmap_data = NULL;
  }

  if (storage->fp != NULL) {
    fclose(storage->fp);
    storage->fp = NULL;
  }

  if (rename(tmp_path, storage->file_name) != 0) {
    perror("Fatal Error: Failed to swap compacted file");
    storage->fp = fopen(storage->file_name, "r+b");
    if (storage->fp != NULL && storage->file_size > 0) {
      storage->mmap_data = mmap(NULL, storage->file_size, PROT_READ,
                               MAP_SHARED, fileno(storage->fp), 0);
      if (storage->mmap_data == MAP_FAILED) storage->mmap_data = NULL;
    }
    free(new_records); free(new_offsets); free(was_mmap);
    free(to_free_meta); free(to_free_vec);
    return -1;
  }

  storage->fp = fopen(storage->file_name, "r+b");
  if (storage->fp == NULL) {
    free(new_records); free(new_offsets); free(was_mmap);
    free(to_free_meta); free(to_free_vec);
    return -1;
  }

  fseek(storage->fp, 0, SEEK_END);
  long sz = ftell(storage->fp);
  storage->file_size = (sz > 0) ? (size_t)sz : 0;

  if (storage->file_size > 0) {
    storage->mmap_data = mmap(NULL, storage->file_size, PROT_READ, MAP_SHARED,
                              fileno(storage->fp), 0);
    if (storage->mmap_data == MAP_FAILED) storage->mmap_data = NULL;
  }

  for (uint64_t i = 0; i < new_count; i++) {
    new_records[i].byte_offset = new_offsets[i];
    if (was_mmap[i] && storage->mmap_data != NULL) {
      uint8_t *ptr = (uint8_t *)storage->mmap_data + new_offsets[i];
      ptr += sizeof(uint64_t) + sizeof(bool);
      new_records[i].vector = (float *)ptr;
      new_records[i].is_mmap = true;
    } else {
      new_records[i].is_mmap = false;
    }
  }

  for (uint64_t i = 0; i < to_free_count; i++) {
    if (to_free_meta[i]) free(to_free_meta[i]);
    if (to_free_vec[i]) free(to_free_vec[i]);
  }

  free(records);
  free(new_offsets);
  free(was_mmap);
  free(to_free_meta);
  free(to_free_vec);

  *records_ptr = new_records;
  *count = new_count;
  return 0;
}

