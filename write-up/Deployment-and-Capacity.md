# OriginDB — Build, Run & Capacity

This is the operator-facing companion to [`usage_API_Docs.md`](usage_API_Docs.md).
That document explains how to *call* the API once it's running; this one covers
getting it running in the first place, and what it can actually handle once it is —
backed by real load testing, not estimates.

## 1. Build

```
make clean && make
```
Produces the `origin` binary in the project root.

## 2. Run

Create a table (one-time, local, prints your API key):
```
./origin
origin init movies 128 1000 0
```

Start the server (blocking; runs in the foreground):
```
./origin server 8000
```
See [`usage_API_Docs.md`](usage_API_Docs.md) for the full admin CLI and network
API reference once it's up.

## 3. Architecture at a glance (what's actually running under load)

Since v0.1, this differs from a naive "reopen the table on every request" model:

- **Tables are opened once and cached in memory** (`get_or_open_table`), shared
  across all requests for that table rather than re-read from disk each time.
- **Reads are safe under concurrency**: record fetches use `pread()` against a
  memory-mapped copy of the data file, so multiple threads can search the same
  table at once without racing on a shared file cursor.
- **Inserts are durable immediately, visible with a short delay.** Every insert
  is written and `fsync`'d to disk synchronously before the server responds
  `200` — that part is not deferred. Making the new record visible to the
  in-memory table used by search is handled by a background sync thread that
  runs every ~2 seconds, the same "near real-time" trade-off Elasticsearch makes
  by default. In practice: **a freshly inserted vector may take up to ~2 seconds
  to appear in `/search` results**, even though it was safely written to disk
  the moment `/insert` returned success.
- **Pending deletes follow the same pattern** — a `/delete-request` is reflected
  in search results within the same ~2 second window, via the same background
  sync, rather than needing a search to reload the whole table to notice it.
- **Thread pool: 128 workers, pulling from a 512-slot connection queue.** A
  burst beyond 512 pending connections is refused immediately (closed, not
  queued unboundedly) — see §5 for what this looks like in practice.

## 4. Load test results

Tested against a 2 vCPU / 4GB VM, over the public internet (not localhost —
these numbers include real network latency, not just server processing time).
Test data: 100 records, dimension 5, IVF trained with k=5, `top_k=5`,
`nprobe=2`, using [`hey`](https://github.com/rakyll/hey). Every test below was
run after fixing a lock-handling bug and a stale-mmap bug found during this
testing process — see the project's own commit history / dev notes for that
debugging trail if you're curious how these numbers were arrived at.

| Concurrency | Requests/sec | p50 latency | p99 latency | Failures |
|---|---|---|---|---|
| 64 | 136 | 0.43s | 0.66s | 0/1024 |
| 256 | 242 | 0.96s | 1.27s | 0/1024 |
| 500 | 540 | 0.75s | 1.08s | 0/1000 |
| 800 | 991 | — | — | 0/800 |
| 900 | 604 | 0.78s | 1.01s | 0/900 |
| 950 | 585 | 0.73s | 1.48s | 0/950 |
| 1000 | varies | — | 1.7s–3.0s | 0–113/1000 |

**Read this as:** the server handles **up to ~950 simultaneous connections
cleanly** — zero failures, latency staying in the sub-1.5-second range even at
the high end. **Right around 1000 concurrent, behavior becomes unpredictable**:
depending on the exact timing of the burst, you'll either see a clean,
immediate rejection of the excess (the 512-slot queue's designed overload
behavior — a `close()`, not a crash) or, in other runs, every request
eventually succeeds but a small number sit in the queue for several seconds
before a thread becomes free. Neither outcome is a crash, a hang, or data
corruption — the server recovers instantly on the next request either way, no
restart needed — but neither is a good experience for the unlucky requests
involved, either.

**Practical takeaway:** if you expect sustained traffic with bursts near or
above ~1000 simultaneous requests, plan for either a larger thread
pool/queue (a config change, not an architecture change) or a retry-with-backoff
strategy client-side for the rare rejected/slow request in that range. Below
that, this has been verified to just work.

## 5. What "server overloaded, dropping connection" means if you see it

This is expected, designed behavior, not a bug report:
```
Server overloaded, dropping connection.
```
It means the 512-slot connection queue was full at that instant. The client's
connection was closed immediately rather than left to hang — treat this the
same way you'd treat a `503`: retry shortly, don't treat it as a permanent
failure.

## 6. Known gaps not yet covered by load testing

- These numbers are for `/search` only. `/insert` and `/train` haven't been
  put through the same concurrency sweep yet — `/insert` in particular is
  worth testing separately, since sustained heavy insert traffic exercises the
  disk-write lock and the background sync merge in ways a read-only search
  benchmark doesn't.
- All testing used a single small table (100 records). Behavior at much larger
  table sizes (the point where the in-memory record array and its periodic
  `realloc` growth become relevant) hasn't been separately measured.
