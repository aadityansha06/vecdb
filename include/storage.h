#ifndef  STORAGE_H
#define STORAGE_H


#include <stdint.h>

typedef struct Record Record_t;
typedef struct storage storage_t; // contain file-pointer

storage_t *storage_init(const char* db_name);


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

#endif

