# OriginDB Usage & API Documentation

OriginDB enforces a strict security boundary between **Admin Operations** (schema
and destructive operations, done locally via the terminal) and **Application
Operations** (reading and writing vectors, done over the network).

> **Status:** Early/WIP. This document describes what the code actually does today,
> including its current rough edges — see [Known Limitations](#5-known-limitations--read-before-going-live)
> before pointing production traffic at it. For build/run instructions and real
> load-test capacity numbers, see
> [`Deployment-and-Capacity.md`](Deployment-and-Capacity.md).

## 1. Admin CLI (local only — never exposed over the network)

Start the engine:
```
./origin
```

| Command | Purpose |
|---|---|
| `origin init <name> <dimension> <capacity> <metric>` | Create a new table. `metric`: `0` = cosine, `1` = euclidean. Fails if the table already exists. Prints a one-time API key — save it. |
| `origin open <name>` | Open an existing table in this session. Dimension and metric are read automatically from the table's own stored config — you don't (and can't) override them here. |
| `origin insert <id> [metadata]` | Insert a vector into the open table. Prompts for the vector's floats afterward. `metadata` is a single word (no spaces). |
| `origin search <top_k>` | Exact nearest-neighbor search against the open table. Prompts for the query vector's floats. |
| `origin delete <id>` | **Admin-only, immediate.** Marks a record as deleted (tombstoned — the vector bytes stay on disk, only a flag flips) both in memory and on disk, right away. Use this for ad-hoc local cleanup. |

| `origin process-deletes <table>` | **Admin-only, batch.** Executes every delete request currently queued for `<table>` by the network's `/delete-request` endpoint (see §2.4). Shows the number of pending records and asks for `y`/`n` confirmation before touching anything. Ids that don't correspond to a real record, or were already deleted, are silently skipped and reported as ignored in the summary. Clears the queue afterward. |
| `origin set-auto-delete <table> <days_from_now>` | **Admin-only.** Schedules the table's pending-delete queue to execute automatically once the given number of days has passed — `0` means "due at the server's next check" (checked every ~2 seconds while the server is running). Calling this again before the scheduled time arrives simply replaces it, so you can push a run earlier or later at any time, e.g. reschedule from "today" to "tomorrow" by running it again with a new value. |
| `origin auto-delete-status <table>` | **Admin-only.** Shows whether auto-delete is currently scheduled for `<table>`, and roughly how long until it fires. |
| `origin cancel-auto-delete <table>` | **Admin-only.** Cancels a scheduled auto-delete for `<table>`. Manual `origin delete`/`origin process-deletes` continue to work regardless of whether a schedule is set. |
| `origin server [port]` | Start the TCP server (blocking; default port 8080). |
| `origin --help` / `origin --exit` | Help / quit. |

Deletion in OriginDB is split into two steps by design: **requesting** a deletion
can be done over the network with a normal API key, but **executing** one can only
happen here, locally, with an explicit human confirmation.
<br>

**By default, nothing executes automatically — ever.** A queued delete-request
sits untouched until an administrator explicitly runs `origin delete` or
`origin process-deletes`. Auto-delete scheduling is an optional, opt-in
convenience for tables where the queue may otherwise grow faster than a human
can review it: an administrator can set a schedule (`origin set-auto-delete`),
and only once that schedule arrives does the queue execute without further
confirmation. If you never set a schedule for a table, its pending-delete
queue will sit there indefinitely until you process it by hand — there is no
implicit timeout.
<br>

See[`Delete-architecture.md`](Delete-architecture.md) for the full reasoning — in
short, a leaked API key can queue delete requests, but can never cause an actual
deletion on its own.

Example session:
```
origin init movies 128 1000 0
# -> SUCCESS: Initialized DB 'movies', API key printed once, save it.

origin insert 1 Inception
# -> prompts for 128 floats

origin delete 1
# -> SUCCESS: Vector ID 1 marked as deleted.
```

**Important — the server blocks the terminal.** `origin server <port>` runs the
accept loop in the foreground; there is currently no way to run admin commands
(including `delete`) while the server is running in the same process. To delete a
record on a live table: stop the server, run `origin open <name>` then
`origin delete <id>` in that same terminal session, then restart
`origin server <port>`. This means every delete currently costs a brief outage —
factor that into your workflow if you plan to prune records regularly.

## 2. Network API (Application Operations)

Once the server is listening, backend applications interact with it over raw HTTP
POST requests — four routes: `/insert`, `/search`, `/train`, and
`/delete-request`. There is no route that performs an actual deletion — see §2.4.

### Authentication

```
Authorization: Bearer <YOUR_TABLE_API_KEY>
```
Keys are scoped per-table. Every request to `/insert`, `/search`, or `/train` must
include this header, or the server returns `401`.

### Vector dimension is enforced

The server reads each table's true dimension and metric from a config file written
at `origin init` time — it no longer trusts whatever a client claims. If your
insert or search vector's length doesn't match the table's actual dimension, you'll
get a `400` with a clear message, rather than a corrupted table.

### `POST /insert`
```json
{
  "db_name": "movies",
  "id": 1,
  "vector": [0.5, 0.1, -0.4],
  "metadata": "Inception"
}
```
`vector` must have exactly the table's configured dimension. `metadata` is optional.

The record is written and `fsync`'d to disk before this call returns `200` — that
part is immediate and durable. **It may take up to ~2 seconds to appear in
`/search` results**, since the in-memory table used by search is refreshed on a
short background cycle rather than on every single insert. See
[`Deployment-and-Capacity.md`](Deployment-and-Capacity.md#3-architecture-at-a-glance-whats-actually-running-under-load)
for why this trade-off exists.

### `POST /search`
```json
{
  "db_name": "movies",
  "top_k": 5,
  "use_ann": false,
  "nprobe": 1,
  "query_vector": [0.4, 0.1, -0.3]
}
```
- `top_k` must be between 1 and 10,000.
- `use_ann: false` runs an exact brute-force scan (fine for small/medium tables).
- `use_ann: true` uses the trained IVF index — requires `/train` to have been run
  at least once for this table, and `nprobe` must be a positive integer.

### `POST /train`
```json
{
  "db_name": "movies",
  "k": 10,
  "max_iterations": 50
}
```
- `k` must be a positive integer, no greater than the number of records currently
  in the table.
- Builds K-means clusters over the table's *non-deleted* records and persists the
  index to disk, so `use_ann: true` searches survive a server restart without
  retraining.
- `max_iterations` is a hard cap on the clustering loop — training stops and
  returns whatever clusters it has once that many iterations pass, even on data
  that hasn't fully converged. Safe to point at untrusted or adversarial data.

### `POST /delete-request`
```json
{
  "db_name": "movies",
  "id": 1
}
```
This does not delete anything. It queues the id for review — the actual deletion
only happens if an administrator runs `origin process-deletes movies` locally and
confirms it. See [`Delete-architecture.md`](Delete-architecture.md) for why the
endpoint is built this way.

A few things worth knowing about this route specifically:

- Only a single `id` per request. There's no way to request a range, a list, or a
  wildcard delete — the request schema simply can't express anything broader than
  one record.
- The response is identical whether or not `id` actually corresponds to a real
  record. This is deliberate: checking existence at request time and replying
  differently would let someone holding a valid key work out which ids are real
  just by watching how the server responds (or how long it takes to respond),
  without ever seeing the data itself. Existence is only resolved later, locally,
  when an administrator processes the queue.
- Queued requests take effect on search results within the same short background
  refresh window described above (up to ~2 seconds), well before anyone runs
  `process-deletes`.

### Client examples

**Python:**
```python
import requests

SERVER_URL = "http://localhost:9090"
HEADERS = {
    "Authorization": "Bearer YOUR_64_CHAR_HEX_KEY_HERE",
    "Content-Type": "application/json"
}

requests.post(f"{SERVER_URL}/insert", headers=HEADERS, json={
    "db_name": "movies", "id": 1,
    "vector": [0.5, 0.1, -0.4], "metadata": "Inception"
})

res = requests.post(f"{SERVER_URL}/search", headers=HEADERS, json={
    "db_name": "movies", "top_k": 5, "use_ann": False,
    "nprobe": 1, "query_vector": [0.4, 0.1, -0.3]
})
print(res.json())
```

**TypeScript / Node.js (e.g. from a Vercel API route):**
```typescript
const SERVER_URL = process.env.ORIGINDB_URL!;
const HEADERS = {
    "Authorization": `Bearer ${process.env.ORIGINDB_KEY}`,
    "Content-Type": "application/json"
};

export async function searchMovies(queryVector: number[]) {
    const res = await fetch(`${SERVER_URL}/search`, {
        method: "POST",
        headers: HEADERS,
        body: JSON.stringify({
            db_name: "movies",
            top_k: 5,
            use_ann: true,
            nprobe: 2,
            query_vector: queryVector
        })
    });
    return res.json();
}
```

## 3. Error Handling

| Code | Meaning |
|---|---|
| `400` | Malformed JSON, missing fields, dimension mismatch, invalid `top_k`/`nprobe`/`k`, or ANN search requested before `/train` has run. |
| `401` | Missing/invalid `Authorization` header, or the key doesn't match `db_name`. |
| `404` | Table not initialized locally, or an unrecognized route. |
| `500` | Allocation failure or unexpected disk write failure. |

## 4. Deployment notes (talking to a website backend)

- This is a **raw TCP/HTTP server you run and host yourself** — it isn't something
  you deploy *to* Vercel; rather, your Vercel (or any other) backend calls out to
  wherever *you've* deployed OriginDB (a VPS, a home server, etc.), the same way it
  might call out to any other API.
- **The server runs a fixed pool of 128 worker threads** pulling connections off a
  bounded queue (512 slots). Concurrent requests from your website are genuinely
  handled in parallel, up to the pool size, rather than queued one-at-a-time.
  If the queue fills (a burst past 512 pending connections), new connections are
  closed immediately rather than piling up unboundedly — your client should treat
  a dropped connection as "retry shortly," not a hard failure. See
  [`Deployment-and-Capacity.md`](Deployment-and-Capacity.md) for real load-test
  numbers on what this looks like in practice, including where this boundary
  actually sits.
- **Concurrency correctness:** each table is opened once and kept in memory,
  shared across every request against it, rather than reloaded from disk per
  request — reads use `pread()` against a memory-mapped copy of the data file, so
  concurrent searches can't race on a shared file position. A per-table
  read/write lock guards in-memory mutations (insert merges), a separate lock
  guards the durable on-disk write, and a read/write lock guards the IVF index
  file (`/train` takes the write side, ANN `/search` takes the read side).
  Concurrent `/insert`, `/search`, and `/train` calls — including against the
  same table — are safe to fire in parallel.
- Keep `ORIGINDB_URL` and per-table API keys in your backend's environment/secrets,
  never in client-side code — the API key is the only thing standing between a
  caller and your table's data.

## 5. Known Limitations — read before going live

- **No route performs an actual deletion, by design.** `/delete-request` only
  queues a request; only `origin process-deletes` (local, confirmed by a human)
  ever writes a real deletion to disk. Plan your cleanup workflow around running
  that command periodically rather than expecting a *permanent, on-disk* deletion
  to happen automatically — see [`Delete-architecture.md`](Delete-architecture.md).
- **Deletes are tombstones, not physical erasure.** Deleted vectors' bytes remain
  on disk; only a flag flips. There is currently no compaction step to reclaim
  space or shrink the file.
- **Inserts and deletes have a short visibility delay (~2 seconds), by design.**
  Writes are durable immediately; the in-memory table used by search picks them
  up on a short background cycle rather than instantly. See
  [`Deployment-and-Capacity.md`](Deployment-and-Capacity.md) for the reasoning.
  If your use case genuinely needs instant read-after-write visibility, this is
  worth discussing before relying on it.
- **Request bodies are capped at 10MB** as a memory-exhaustion guard; very large
  batch inserts should be chunked into multiple requests.
- **The connection queue is a fixed 512 slots** and the worker pool is a fixed
  128 threads — neither is currently configurable at runtime. Verified to handle
  up to ~950 concurrent connections cleanly in load testing; behavior right
  around 1000 concurrent becomes less predictable (still never crashes, but
  latency or rejection rates rise). See
  [`Deployment-and-Capacity.md`](Deployment-and-Capacity.md#4-load-test-results)
  for the full numbers.
- **Load testing so far covers `/search` only.** `/insert` and `/train` haven't
  been put through the same concurrency sweep yet.
