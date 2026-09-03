Not ready for production 
Work in progress 
<br>
V-0.1 soon


# OriginDB

A custom, high-performance, disk-backed Approximate Nearest Neighbor (ANN) vector database
built entirely from scratch in C.

## Architecture

OriginDB completely bypasses high-level wrappers to interact directly with POSIX file I/O.
 It maps RAM array indices to physical disk `byte_offsets`,
allowing the engine to instantly jump around the disk using `fseek()`
to fetch targeted vectors for similarity calculations
without loading the entire dataset into memory.


# Why this exists

This started as a toy project to learn how to write a scalable C codebase and implement low level memory management and optimize performance. There isn't anything unique I'm solving in it, and pretending otherwise would just be bluffing. There are excellent vector databases already (pgvector, Faiss, sqlite vec, Qdrant...).

What I actually own is the engineering of it. I wrote a disk backed ANN search engine from scratch in C, my own binary file format, manual fseek byte offset indexing, k means clustering by hand, no libraries doing the hard parts for me.

I'll continue to work on it, implementing SIMD, CUDA, HNSW, and many more optimizations to make it more robust and scalable. If anyone is interested, they can join and work on it. Also if anyone has any idea where we should take this, the suggestion would be appreciated too.


## Current Features
- **Variable-Length Storage Engine:** Zero-padding disk persistence.
- **K-Means IVF Indexing:** Mathematical centroid clustering for fast approximate search.
- **Bounded Insertion Sort:** Memory-safe Exact Nearest Neighbor (ENN) tracking.
- **Byte-Offset Random Access:** fseek-based direct record retrieval from disk, no full-table scan needed for a known offset.
- **Persistent IVF Index:** trained clusters are serialized to disk and reloaded, so the index survives a restart without retraining.
- **Pluggable Distance Metrics:** cosine and euclidean, selected per table via a function pointer router.
- **Auto-Growing Record Table:** in-memory record array doubles capacity on overflow instead of a fixed cap.
- **TCP Server with JSON API:** POST /search, /insert, /train routes over raw sockets, parsed with cJSON.
- **API Key Auth:** per-table key generation and verification on server requests.
- **Interactive TUI + One-Shot CLI:** REPL for exploring a table, or run a single command directly from the shell.

Feel free to contribute and connect at vermaadityansh@gmail.com or on X (https://x.com/aadityansha_06)
