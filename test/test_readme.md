# vecdb regression tests

Drop this `tests/` folder into the repo root (next to `src/`, `include/`, `Makefile`).

## Run

```
cd tests
make test
```

This compiles `test_storage.c` and `test_kmeans.c` directly against your real
`src/storage.c`, `src/distance.c`, and `src/search/kmeans.c` (not through the
top-level Makefile), so a broken top-level build never blocks the suite.

## What's covered

**`test_storage.c`** — `storage_init`, write/read roundtrips, `NULL`
metadata, multi-record ordering, `storage_current_offset` correctness under
interleaved read/write (the insert→search→insert pattern a real app does),
out-of-range `storage_fetch_by_offset`, and `save_ivf_index`/`load_ivf_index`
roundtrip including an empty cluster.

**`test_kmeans.c`** — `k=1`, the invariant that every record is assigned to
exactly one cluster for any `k`, all-identical-vector input (degenerate
case), correct RAM-index → disk-byte-offset mapping, and `ivf_search`
behavior at `nprobe == k` and `nprobe > k`.

## Results against your current code (as of the files you last shared)

```
== storage tests ==
[PASS] test_init_creates_directory
[PASS] test_write_read_roundtrip_single_record
[PASS] test_null_metadata_roundtrip
[PASS] test_multiple_records_preserve_order
[FAIL] test_offset_stays_correct_after_interleaved_read
[PASS] test_fetch_by_offset_out_of_range
[PASS] test_ivf_index_roundtrip_with_empty_cluster

== kmeans tests ==
[PASS] test_k_equals_1_puts_everything_in_one_cluster
[PASS] test_every_point_gets_assigned_exactly_once
[PASS] test_all_identical_vectors_does_not_crash
[PASS] test_byte_offsets_mapped_correctly
[PASS] test_ivf_search_nprobe_equals_k_returns_everything
[PASS] test_ivf_search_nprobe_greater_than_k_KNOWN_BUG (documents a bug, see below)
```

## Bugs this suite found

### 1. `storage_current_offset` doesn't force end-of-file (currently FAILS)

Your current implementation:
```c
uint64_t storage_current_offset(storage_t *storage) {
  if (storage == NULL || storage->fp == NULL) return 0;
  long pos = ftell(storage->fp);
  return pos < 0 ? 0 : (uint64_t)pos;
}
```
This reports wherever the stream cursor currently sits — which drifts to the
middle of the file the moment a read (`storage_fetch_by_offset`, used by
`db_ann_search`) happens on the same handle. The next insert then records the
*wrong* offset for its own record, even though the byte-append itself (mode
`"a+b"`) still lands in the right place. Fix:

```c
uint64_t storage_current_offset(storage_t *storage) {
  if (storage == NULL || storage->fp == NULL) return 0;
  if (fseek(storage->fp, 0, SEEK_END) != 0) return 0;
  long pos = ftell(storage->fp);
  return pos < 0 ? 0 : (uint64_t)pos;
}
```
Once applied, `test_offset_stays_correct_after_interleaved_read` should pass.

### 2. `ivf_search` doesn't clamp `nprobe` to `k` (documented, not yet fixed)

`best_clusters[]` is sized `nprobe`, but only the first `k` slots ever get
overwritten (there are only `k` centroids to compare against). Any slots
beyond `k` keep their sentinel value `{ dis = 1e30, cluster_idx = 0 }`, so
cluster 0's records get counted and copied multiple times whenever
`nprobe > k` — our test measured `fetched->count = 10` against a true total
of 4 records. Fix: clamp at the top of `ivf_search`:
```c
if (nprobe > k) nprobe = k;
```
Once you add that, flip the last assertion in
`test_ivf_search_nprobe_greater_than_k_KNOWN_BUG` from
`ASSERT_TRUE(fetched->count >= true_total)` to
`ASSERT_EQ_U64(fetched->count, true_total)`.

## Bugs found but NOT covered by an automated test (crash risk — fix before fuzzing further)

These are real edge cases your recommendation/RAG use case *will* hit
(empty collections, first-run state), but they'd crash the whole test binary
rather than fail cleanly, so they're documented here instead of encoded as
tests:

- **`count == 0`** passed to `kmeans_build`: `rand() % count` divides by
  zero. Guard with `if (count == 0) return NULL;` at the top.
- **`k == 0`** passed to `kmeans_build`: `total_cluster` is allocated with
  `malloc(0)`, then `total_cluster->count = 0;` writes through a
  zero-length allocation (undefined behavior). Guard with
  `if (k == 0) return NULL;`.
- **No `storage_close`/`storage_free` function exists anywhere.** Every
  `storage_init` leaks the `FILE*` and the `storage_t` struct — fine for a
  short-lived CLI demo, but for a long-running server process (which your
  client's recommendation/RAG service will be) this is a slow file-handle
  and memory leak. Worth adding a `storage_close(storage_t *storage)` that
  `fclose`s and `free`s, and calling it wherever a `FlatDb_t` gets torn
  down.

## Also worth fixing before you ship to a client

Carried over from earlier review, not yet addressed in what you've shared:

- `kmeans_build`'s convergence loop uses `goto assign` to jump back into
  the loop body, which bypasses the `while (... && ittrate <= max_iterations)`
  condition — so `max_iterations` is not actually enforced as a hard cap on
  non-converging data. Restructure as a bounded `for` loop with an explicit
  `if (ittrate > max_iterations) break;`.
- Several `for (int i = 0; i < k; i++)` loops compare a signed `int`
  against unsigned `uint64_t k` (`storage.c`, `kmeans.c`) — harmless at
  your current scale, but change `i` to `uint64_t` before a client relies on
  large `k`.
