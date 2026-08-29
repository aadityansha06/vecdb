#include "../include/storage.h"

#include "../include/db.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct storage {
  char file_name[203];
  FILE *fp;

} storage_t;

storage_t *storage_init(const char *db_name) {
  storage_t *storage = (storage_t *)malloc(sizeof(struct storage));
  if (storage == NULL)
    return NULL;
  char db_path[200];
  snprintf(db_path, sizeof(db_path), "%s.db", db_name);
  storage->fp =
      fopen(db_path, "a+b"); // TODO: load existing records from file on init
  if (storage->fp == NULL) {
    perror("Fatal Error: Failed to Open file \n");
    free(storage);
    return NULL;
  }

  return storage;
}

int storage_write_record(storage_t *storage, Record_t *record,
                         uint64_t dimension) {

  if (storage == NULL || record == NULL || storage->fp == NULL) {
    return -1;
  }

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
 * @brief Reads the next record from the disk into the provided record struct.
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
