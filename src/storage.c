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

  if (storage == NULL || storage->fp == NULL || record == NULL) {
    return -1;
  }

  long current_pos = ftell(storage->fp);
  if (current_pos == -1)
    return -1;
  record->byte_offset = (uint64_t)current_pos;

  if (fread(&record->id, sizeof(uint64_t), 1, storage->fp) != 1)
    return 0; // EOF no record found

  if (fread(&record->is_deleted, sizeof(bool), 1, storage->fp) != 1)
    return -1;

  record->vector = (float *)malloc(sizeof(float) * dimension);
  if (record->vector == NULL)
    return -1;
  if (fread(record->vector, sizeof(float), dimension, storage->fp) != dimension)
    return -1;

  size_t len = 0;
  if (fread(&len, sizeof(size_t), 1, storage->fp) != 1) {
    free(record->vector);
    return -1;
  }

  if (len > 0) {
    record->metadata = (char *)malloc(sizeof(char) * (len + 1));
    if (record->metadata == NULL) {
      free(record->vector);

      return -1;
    }

    if (fread(record->metadata, sizeof(char), len, storage->fp) != len) {
      free(record->vector);
      free(record->metadata);

      return -1;
    }
    record->metadata[len] = '\0';
  } else {
    record->metadata = NULL;
  }

  int x = fflush(storage->fp);
  if (x != 0) {
    free(record->vector);
    free(record->metadata);

    return -1;
  }

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
    cluster[i].byte_offsets = malloc(sizeof(uint64_t) * cluster[i].capacity);

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
  if (storage == NULL || storage->fp == NULL || record == NULL) {
    return -1;
  }

  if (fseek(storage->fp, byte_offset, SEEK_SET) != 0) {
    return -1;
  }

  if (fread(&record->id, sizeof(uint64_t), 1, storage->fp) != 1)
    return 0; // EOF

  if (fread(&record->is_deleted, sizeof(bool), 1, storage->fp) != 1)
    return -1;

  record->vector = (float *)malloc(sizeof(float) * dimension);
  if (record->vector == NULL)
    return -1;

  if (fread(record->vector, sizeof(float), dimension, storage->fp) !=
      dimension) {
    free(record->vector);
    return -1;
  }

  size_t len = 0;
  if (fread(&len, sizeof(size_t), 1, storage->fp) != 1) {
    free(record->vector);
    return -1;
  }

  if (len > 0) {
    record->metadata = (char *)malloc(sizeof(char) * (len + 1));
    if (record->metadata == NULL) {
      free(record->vector);
      return -1;
    }

    if (fread(record->metadata, sizeof(char), len, storage->fp) != len) {
      free(record->vector);
      free(record->metadata);
      return -1;
    }
    record->metadata[len] = '\0';
  } else {
    record->metadata = NULL;
  }

  return 1;
}

void storage_close(storage_t *storage) {
  if (storage == NULL)
    return;

  if (storage->fp != NULL) {
    fclose(storage->fp);
  }

  free(storage);
}
