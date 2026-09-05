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

-[Usage-API-Doc](write-up/usage_API_Docs.md)


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
- **Variable-Length Storage Engine:** Zero-padding disk persistence.
- **K-Means IVF Indexing:** Mathematical centroid clustering for fast approximate search.
- **Bounded Insertion Sort:** Memory-safe Exact Nearest Neighbor (ENN) tracking.
- **Byte-Offset Random Access:** fseek-based direct record retrieval from disk, no full-table scan needed for a known offset.
- **Persistent IVF Index:** trained clusters are serialized to disk and reloaded, so the index survives a restart without retraining.
- **Pluggable Distance Metrics:** cosine and euclidean, selected per table via a function pointer router.
- **Auto-Growing Record Table:** in-memory record array doubles capacity on overflow instead of a fixed cap.
- **TCP Server with JSON API:** POST /search, /insert, /train, /delete-request routes over raw sockets, parsed with cJSON.
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

Feel free to contribute and connect at vermaadityansh@gmail.com or on X (https://x.com/aadityansha_06)
