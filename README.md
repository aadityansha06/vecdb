Not ready for production 
Work in progress 
<br>
V-0.1 soon


```md
# OriginDB

A custom, high-performance, disk-backed Approximate Nearest Neighbor (ANN) vector database built entirely from scratch in C.

## Architecture

OriginDB completely bypasses high-level wrappers to interact directly with POSIX file I/O. It maps RAM array indices to physical disk `byte_offsets`, allowing the engine to instantly jump around the disk using `fseek()` to fetch targeted vectors for similarity calculations without loading the entire dataset into memory.

## Current Features

- **Variable-Length Storage Engine:** Zero-padding disk persistence.
- **K-Means IVF Indexing:** Mathematical centroid clustering for fast approximate search.
- **Bounded Insertion Sort:** Memory-safe Exact Nearest Neighbor (ENN) tracking.

## TODOs

- [ ] **Authentication Layer:** Expand the basic API key validation into robust endpoint security.
- [ ] **Delete Operation:** Implement `db_delete` to flag `is_deleted` in RAM and rewrite the specific flag on disk.
- [ ] **Concurrency & Locking:** Introduce read/write mutexes to safely handle simultaneous admin writes and client searches.
- [ ] **Training Lifecycle:** Automate background re-training of the IVF index based on continuous insertion thresholds.
- [ ] **API Wrappers:** Build thin HTTP clients in Python/TS to interface with the core C engine.
```

