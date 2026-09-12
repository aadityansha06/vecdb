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

The server is an **epoll-based event loop**, not a thread-per-connection
model — a small, fixed pool of worker threads (bounded by CPU core count, 4
to 32) services every open connection via `epoll_wait()`, rather than
committing one OS thread per connection for its entire lifetime. This
decouples "connections currently open" from "threads consumed," which is
what makes the numbers in §4 below possible.

- **HTTP keep-alive is implemented.** Responses no longer force
  `Connection: close` on every request — a client can reuse one TCP
  connection across many requests instead of paying a full reconnect (and,
  over the public internet, a DNS lookup) on every single call. This was the
  single largest cost identified during load testing; see §4 for the
  measured impact.
- **Tables are opened once and cached in memory** (`get_or_open_table`),
  shared across all requests for that table rather than re-read from disk
  each time.
- **The trained IVF index is cached in memory per table**
  (`open_table_t.ivf_index` / `index_loaded`), not reloaded from disk on
  every `/search` call the way it originally was. A `/train` call replaces
  the cached copy.
- **API keys are cached in memory** (`auth_cache`), not re-read by scanning
  `origin_data/.auth_keys` line-by-line on every request.
- **Reads are safe under concurrency**: record fetches use `pread()` against
  a memory-mapped copy of the data file, so multiple threads can search the
  same table at once without racing on a shared file cursor.
- **Inserts are durable immediately, visible with a short delay.** Every
  insert is written and `fsync`'d to disk synchronously before the server
  responds `200` — that part is not deferred. Making the new record visible
  to the in-memory table used by search is handled by a background sync
  thread that runs every ~2 seconds, the same "near real-time" trade-off
  Elasticsearch makes by default. In practice: **a freshly inserted vector
  may take up to ~2 seconds to appear in `/search` results**, even though it
  was safely written to disk the moment `/insert` returned success.
- **Pending deletes follow the same pattern** — a `/delete-request` is
  reflected in search results within the same ~2 second window, via the same
  background sync, rather than needing a search to reload the whole table to
  notice it.
- **Connection queue: 512 slots**, accept-queue depth up to 100,000 at the
  OS level. A sustained burst beyond what the event loop can service is
  refused rather than queued unboundedly — see §5.

## 4. Load test results

Tested against a 2 vCPU / 4GB VM, over the public internet (not localhost —
these numbers include real network latency, not just server processing
time). Test data: 100 records, dimension 5, IVF trained with k=5, `top_k=5`,
`nprobe=2`, using [`hey`](https://github.com/rakyll/hey).

### Before vs. after keep-alive + in-memory caching

The single biggest performance change in this project's history so far. The
"before" numbers are the original thread-per-connection server; "after" is
the epoll rewrite with keep-alive, cached tables, cached IVF index, and
cached API keys all in place.

| Concurrency | Req/sec (before) | Req/sec (after) | p99 (before) | p99 (after) | Failures (before) | Failures (after) |
|---|---|---|---|---|---|---|
| 256 | 242 | **806** | 1.27s | **0.57s** | 0/1024 | 0/1024 |
| 500 | 540 | **1099** | 1.08s | **0.60s** | 0/1000 | 0/1000 |
| 950 | 585 | 502* | 1.48s | 1.87s* | 0/950 | 0/950 |
| 1000 | varies | **608** | 1.7–3.0s | **0.95s** | 0–113/1000 | **0/1000** |

\* *The `c=950` "after" run is an outlier — its average connection-setup time
was 1.29s (vs. under 0.35s in every adjacent run), pointing to a transient
network condition during that specific test rather than a real regression.
Every other data point at higher and lower concurrency improved. Flagged
here rather than smoothed over; worth re-running to confirm.*

**The headline result:** at `c=1000` — previously the point where behavior
became unpredictable (anywhere from 0 to 113 dropped connections depending
on timing) — the server now completes **1000/1000 requests cleanly**, with
p99 latency under 1 second. Keep-alive removing the cost of a fresh TCP
connection (and DNS lookup) per request appears to have been the direct
cause of that instability; eliminating it stabilized the exact zone that was
previously the server's weakest point.

**Practical takeaway:** the ~950-1000 concurrent ceiling documented in
earlier testing was real, but it was substantially a *connection-churn*
problem, not a hard architectural limit. With that removed, this
configuration now handles at least 1000 concurrent connections cleanly. The
new ceiling, if one exists at this scale, hasn't been found yet — see §6.

## 5. What "server overloaded, dropping connection" means if you see it

This is expected, designed behavior, not a bug report. It means the
connection queue was full at that instant. The client's connection was
closed immediately rather than left to hang — treat this the same way you'd
treat a `503`: retry shortly, don't treat it as a permanent failure.

## 6. Known gaps not yet covered by load testing

- **A small memory leak remains, now much smaller than originally found.**
  Confirmed via sustained RSS monitoring: the original leak was ~340
  bytes/request; two fixes (freeing per-result metadata strings, and
  initializing the results array before every early-exit path) brought it
  down to roughly ~75 bytes/request as of the last measurement. At current
  scale this would take millions of sustained requests to meaningfully
  matter, but it hasn't been fully eliminated — a `valgrind` run against a
  live server found no further leak in a short (300-request) local test,
  suggesting the remaining growth may need a longer, larger reproduction to
  isolate. Treat this as open until a `valgrind` run under real sustained
  load pinpoints the exact allocation.
- **The concurrency ceiling above `c=1000` hasn't been found.** Every test
  through `c=1000` now passes cleanly; the point where the server actually
  starts shedding load under the new architecture is unknown and worth
  testing (`c=2000`, `c=5000`, etc.) as a next step.
- **These numbers are for `/search` only.** `/insert` and `/train` haven't
  been put through the same concurrency sweep since the keep-alive/caching
  rewrite — `/insert` in particular is worth retesting, since it exercises
  the disk-write lock and background sync merge in ways a read-only search
  benchmark doesn't.
- **All testing used a single small table (100 records).** Behavior at much
  larger table sizes hasn't been separately measured.
