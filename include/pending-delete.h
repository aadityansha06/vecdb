/*
* Copyright (c) 2026 Aadityansha Verma. All rights reserved.
* This file is licensed under the Business Source License 1.1.
* See the LICENSE file in the project root for full terms.
*/

#ifndef PENDING_H
#define PENDING_H

#include <stdint.h>
#include <stdbool.h>

int append_pending_delete(const char *db_name, uint64_t id);

uint64_t* load_pending_deletes(const char *db_name, uint64_t *out_count);

bool is_pending_delete(uint64_t id, uint64_t *pending_array, uint64_t count);

int clear_pending_deletes(const char *db_name); 

#endif
