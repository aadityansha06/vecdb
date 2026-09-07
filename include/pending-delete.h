/*
 * Copyright (c) 2026 Aadityansha Verma. All rights reserved.
 * This file is licensed under the Business Source License 1.1.
 * See the LICENSE file in the project root for full terms.
 */

#ifndef PENDING_H
#define PENDING_H

#include <stdbool.h>
#include <stdint.h>

int append_pending_delete(const char *db_name, uint64_t id);

uint64_t *load_pending_deletes(const char *db_name, uint64_t *out_count);

bool is_pending_delete(uint64_t id, uint64_t *pending_array, uint64_t count);

int clear_pending_deletes(const char *db_name);

/*@Auto-delete scheduling (admin-controlled, adjustable at any time)
 *
 */

/* Sets (or overwrites) the scheduled auto-execution time for this table's
 * pending-delete queue, as a Unix timestamp. Calling this again before the
 * scheduled time arrives simply replaces it -- an admin can push a
 * scheduled "today" out to "tomorrow" (or bring it closer) at any time. */
int set_auto_delete_schedule(const char *db_name, uint64_t unix_timestamp);

/* Returns the currently configured schedule time, or 0 if none is set
 * (auto-delete disabled for this table). */
uint64_t get_auto_delete_schedule(const char *db_name);

/* Disables auto-delete for this table (removes the schedule file). */
int clear_auto_delete_schedule(const char *db_name);

#endif
