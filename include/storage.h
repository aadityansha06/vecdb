#ifndef  STORAGE_H
#define STORAGE_H
#include "search/kmeans.h"
#include <stdint.h>
typedef struct Record Record_t;
typedef struct storage storage_t; // contain file-pointer

storage_t *storage_init(const char* table_name);


uint64_t storage_current_offset(storage_t *storage);


/**
 * @brief Appends a single vector record to the binary storage file on disk.
 *
 * @returns 0 on success, or -1 if the disk write fails.
 * @param storage Pointer to the opaque storage context.
 * @param record Pointer to the specific record in RAM to be written.
 * @param dimension The dimensionality of the vector (needed to calculate byte size).
 * 
 */
int storage_write_record(storage_t *storage,Record_t *record,uint64_t dimension);



/**
 * @brief Reads the next record from the disk into the provided record struct.
 *
 * @returns 1 on success, 0 on End of File (EOF), or -1 on error.
 * @param storage Pointer for the file context
 * @param record Pointer to the specific record in RAM to be written.
 * @param dimension The dimensionality of the vector (needed to malloc RAM for vector).
*/
int storage_load_record(storage_t *storage,Record_t *record, uint64_t dimension);

/**
 * @brief Serializes the trained IVF clusters to disk.
 * 
 * @param table_name The name of the table/folder (e.g., "movies").
 * @param clusters The pointer to the array of trained clusters.
 * @param k The number of clusters.
 * @param dimension The vector dimensionality.
 * @return 0 on success, -1 on failure.
 */
int save_ivf_index(const char *table_name, cluster_t *clusters, uint64_t k, uint64_t dimension);

/**
 * @brief Deserializes the IVF clusters from disk into RAM.
 * 
 * @param table_name The name of the table/folder.
 * @param out_k Pointer to store the loaded 'k' value.
 * @param dimension The vector dimensionality.
 * @return A heap-allocated array of cluster_t, or NULL on failure.
 */
cluster_t *load_ivf_index(const char *table_name, uint64_t *out_k, uint64_t dimension);


/**
 * @brief Fetches a single record from the disk using its exact byte offset.
 *
 * @returns 1 on success, 0 on End of File (EOF), or -1 on error.
 * @param storage Pointer for the file context.
 * @param byte_offset The exact byte location of the record in the file.
 * @param record Pointer to the specific record in RAM to be populated.
 * @param dimension The dimensionality of the vector.
 */
int storage_fetch_by_offset(storage_t *storage, uint64_t byte_offset, Record_t *record, uint64_t dimension);



void storage_close(storage_t *storage);

#endif

