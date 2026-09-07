Not ready for production 
Work in progress 
<br>
V-0.1 soon


# OriginDB

A custom, high-performance, disk-backed Approximate Nearest Neighbor (ANN) vector database
built entirely from scratch in C.

Its main standout feature is a security-first deferred delete architecture — deletion
requests can be sent over the network, but an actual deletion can only ever be carried
out locally, by a human, on the server itself. Read the full design here:
[Deferred Delete Architecture](write-up/Delete-architecture.md).

### Read Docs to get started

- [Usage-API-Doc](write-up/usage_API_Docs.md) — how to call the API once it's running.
- [Deployment-and-Capacity](write-up/Deployment-and-Capacity.md) — how to build and
  run it, plus real load-test numbers (verified clean up to ~950 concurrent
  connections).



## Where This Fits

OriginDB is built around one trade-off: deletion requires a human, on
purpose (see [Delete-architecture](write-up/Delete-architecture.md)). That
makes it a good fit for systems that are **read-heavy, security-sensitive,
and don't delete often** — and a poor fit for anything that deletes
constantly as part of normal operation.

**Good fit:**
- A bank's transaction/document search — records get inserted and searched
  constantly, almost never deleted, and a leaked key must never be able to
  wipe history.
- A government records or compliance archive — write-once-ish, read-heavy,
  deletion is a rare, deliberate, audited event, not a routine action.
- A song/media recommendation catalog — catalog entries are inserted in
  batches and searched heavily; removing a track is infrequent and can wait
  for a human to confirm.
- An internal analytics/BI dataset — ingested periodically, queried often,
  rarely if ever needs individual records removed on demand.

**Poor fit (for now):**
- A chat app, social feed, or anything where users routinely delete their
  own content in real time — this design assumes deletion is the exception,
  not a constant background operation.
- Any system needing instant delete visibility with no review step — the
  deferred-delete model is the point of this project, not a limitation to
  route around.


# Why this exists

This started as a toy project to learn how to write a scalable C codebase and implement low level memory management and optimize performance. There wasn't anything unique I'm solving in it, and pretending otherwise would just be bluffing. There are excellent vector databases already (pgvector, Faiss, sqlite vec, Qdrant...).

While building this, I left the server's API without a delete endpoint, mostly because
I just hadn't gotten to it yet. But that gap ended up raising a real question for me,
why not make this the main feature? With most databases, if an API key ever leaks, an
attacker can delete everything instantly, no extra steps needed. So instead of treating
the missing endpoint as something to eventually patch over, I kept deletion completely
isolated from the backend and only allowed it locally, through SSH, on the server
itself. Also if anyone has any idea where we should take this, the suggestion would
be appreciated too.


## Current Features
- **Variable-Length Storage Engine:** Zero-padding disk persistence, backed by a
  memory-mapped data file for concurrent, thread-safe reads.
- **K-Means IVF Indexing:** Mathematical centroid clustering for fast approximate search.
- **Bounded Insertion Sort:** Memory-safe Exact Nearest Neighbor (ENN) tracking.
- **Byte-Offset Random Access:** `pread`-based direct record retrieval from disk, no full-table scan needed for a known offset, and safe under concurrent reads.
- **Persistent IVF Index:** trained clusters are serialized to disk and reloaded, so the index survives a restart without retraining.
- **Pluggable Distance Metrics:** cosine and euclidean, selected per table via a function pointer router.
- **Cached, Shared Tables:** each table is opened once and kept in memory across requests, rather than reloaded from disk per call, with inserts and pending deletes synced into the live table on a short background cycle.
- **Auto-Growing Record Table:** in-memory record array doubles capacity on overflow instead of a fixed cap.
- **TCP Server with JSON API:** POST /search, /insert, /train, /delete-request routes over raw sockets, parsed with cJSON, backed by a 128-thread worker pool. Load-tested clean up to ~950 concurrent connections — see [Deployment-and-Capacity](write-up/Deployment-and-Capacity.md).
- **API Key Auth:** per-table key generation and verification on server requests.
- **Deferred Delete Queue:** deletions can be requested over the network but can only ever be executed locally, with an explicit human confirmation — a leaked API key can never delete data on its own.
- **Interactive TUI + One-Shot CLI:** REPL for exploring a table, or run a single command directly from the shell.

## Design Writeups

- Storage Architecture Design for Targeted Disk Reads in ANN Vector Search in C
  [Medium](https://medium.com/@vermaadityansh/storage-architecture-design-for-targeted-disk-reads-in-ann-vector-search-7159e5b46453) | [github-read](write-up/Storage-architecture.md)
- Per Table Key Authentication Design for a Multi Tenant Vector Database in C
  [Medium](https://medium.com/@vermaadityansh/per-table-key-authentication-design-for-a-multi-tenant-vector-database-in-c-51f2b834df82) | [github-read](write-up/Auth-design.md)

- Scaling VectorDb written in C to Handle Concurrent Requests
[Medium](https://medium.com/@vermaadityansh/scaling-vectordb-written-in-c-to-handle-concurrent-requests-2fceb641e0c0?sharedUserId=vermaadityansh) | [github-read](write-up/originDb-concurrency.md)

- Deferred Delete Architecture: Why a Leaked API Key Can Never Delete Your Data
  [github-read](write-up/Delete-architecture.md)

- Deployment & Capacity: build/run instructions and real load-test results
  [github-read](write-up/Deployment-and-Capacity.md)

Feel free to contribute and connect at vermaadityansh@gmail.com or on X (https://x.com/aadityansha_06)
