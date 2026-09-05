# OriginDB Usage & API Documentation

OriginDB enforces a strict security boundary between **Admin Operations** (schema
and destructive operations, done locally via the terminal) and **Application
Operations** (reading and writing vectors, done over the network).

> **Status:** Early/WIP. This document describes what the code actually does today,
> including its current rough edges — see [Known Limitations](#5-known-limitations--read-before-going-live)
> before pointing production traffic at it.

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
| `origin delete <id>` | **Admin-only, by design.** Marks a record as deleted (tombstoned — the vector bytes stay on disk, only a flag flips) both in memory and on disk. There is intentionally no network route for this: a leaked or compromised API key can pollute a table via `/insert`, but can never delete data from it. |
| `origin server [port]` | Start the TCP server (blocking; default port 8080). |
| `origin --help` / `origin --exit` | Help / quit. |

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
POST requests — three routes only: `/insert`, `/search`, `/train`. There is
deliberately no `/delete` route.

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
- Note: `max_iterations` is currently a soft cap in the underlying clustering loop
  — pathological input that never converges could in principle run past it. In
  practice this is rare, but if you're training on adversarial or untrusted data,
  keep an eye on it.

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
- **The server runs a fixed pool of 32 worker threads** pulling connections off a
  bounded queue (256 slots). Concurrent requests from your website are genuinely
  handled in parallel, up to the pool size, rather than queued one-at-a-time.
  If the queue fills (a burst past 256 pending connections), new connections are
  closed immediately rather than piling up unboundedly — your client should treat
  a dropped connection as "retry shortly," not a hard failure.
- **Concurrency correctness:** every request opens its own private handle onto a
  table (no shared in-memory state between requests), so the only genuinely shared
  resources are the on-disk files. Those are protected explicitly: a mutex guards
  each record write to a table's `data.db`, and a read/write lock guards the IVF
  index file (`/train` takes the write side, ANN `/search` takes the read side).
  Concurrent `/insert`, `/search`, and `/train` calls — including against the same
  table — are safe to fire in parallel.
- Keep `ORIGINDB_URL` and per-table API keys in your backend's environment/secrets,
  never in client-side code — the API key is the only thing standing between a
  caller and your table's data.

## 5. Known Limitations — read before going live

- **No `/delete` over the network, by design.** Deletion is admin-CLI only, and
  requires briefly stopping the server (see §1). Plan around this if you need to
  prune records on a schedule.
- **Deletes are tombstones, not physical erasure.** Deleted vectors' bytes remain
  on disk; only a flag flips. There is currently no compaction step to reclaim
  space or shrink the file.
- **Every request reloads the full table into memory.** `/insert` and `/search`
  each open a fresh copy of the table's entire record history rather than sharing
  one long-lived in-memory table across requests. This is what makes the current
  locking scheme sufficient (see §4), but it means concurrent requests to a large,
  busy table each pay a full reload cost rather than sharing one warm copy. Fine
  for small-to-medium tables; the natural next step if a table gets large and hot
  is a shared, long-lived table guarded by a read/write lock instead.
- **`/train`'s `max_iterations` may not be a hard ceiling in all cases** — the
  underlying clustering loop's iteration cap has an edge case that wasn't
  reconfirmed as fixed as of this writing. Low risk in normal use; worth a look if
  untrusted or adversarial data ever reaches this endpoint. A stuck `/train` now
  only ties up one of the 32 worker threads rather than freezing the whole server,
  so the blast radius is contained either way.
- **Request bodies are capped at 10MB** as a memory-exhaustion guard; very large
  batch inserts should be chunked into multiple requests.
- **The connection queue is a fixed 256 slots** and the worker pool is a fixed 32
  threads — neither is currently configurable at runtime. Fine for a single-server
  deployment; revisit if you ever need to tune these under real load.
