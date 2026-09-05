
# Scaling OriginDB to Handle Concurrent Requests

### 1. The Server Layer: Producer-Consumer Thread Pool

Previously, the server was a sequential bottleneck. It could only `accept()` one connection, process the JSON, read the database, and send a response before it was allowed to even look at the next incoming client. I replaced this with a **Producer-Consumer Thread Pool**.

The **Producer (Main Thread)** main loop is now infinitely fast. Its only job is to `accept()` incoming network sockets and immediately push the file descriptor (FD) integer into a 256-slot ring buffer array.

The **Consumers (Worker Threads)** are 32 detached POSIX threads that run entirely in the background.

Using **Condition Variables (`pthread_cond_t`)**, instead of wildly spinning and burning CPU cycles, worker threads sleep. When the main thread pushes a socket into the buffer, it fires a signal. A single worker wakes up, pops the FD off the queue, and executes the router logic independently on its own stack.

A **Queue Mutex (`pthread_mutex_t`)** guarantees that two workers cannot accidentally pop the same network socket off the ring buffer simultaneously.

### 2. The Storage Layer: Surgical Critical Sections

Because every worker thread acts independently, multiple threads could attempt to write to `data.db` at the exact same millisecond. If Thread A and Thread B both calculate the end-of-file offset simultaneously, their bytes will intertwine and permanently corrupt the vectors on the disk.

I isolated the exact moment the file size is calculated and the bytes are written via `fwrite()` using a **Global Mutex**.

For **Atomic Appends**, by locking an `insert_lock` mutex right before `storage_current_offset`, I force concurrent threads into a single-file line for disk I/O. Thread A locks the door, writes safely, and unlocks it. Thread B immediately follows.

**Thread-Local RAM** ensures that parsing the JSON, calculating distances, and returning the HTTP response all happen *outside* the lock. This ensures the CPU-heavy tasks remain fully parallel across all 32 threads.

### 3. The Index Layer: Preventing Dirty Reads

Beyond raw vector appending, the K-Means clustering index (`ivf_index.bin`) required a different concurrency strategy. If a background `/train` request was rewriting the index file while a concurrent `/search` tried to load it, the reader would hit half-written bytes. This "dirty read" would result in the parser reading garbage capacity sizes, attempting to `malloc()` massive amounts of memory, and instantly crashing the process.

However, wrapping a standard Mutex around the index would bottleneck read operations, destroying the server's search throughput. Instead, I implemented a **Reader-Writer Lock (`pthread_rwlock_t`)**.

During a `/search`, threads acquire a shared read lock (`rdlock`). This allows an infinite number of concurrent clients to load and query the index simultaneously without blocking each other.

When a `/train` request arrives to rebuild the clusters, it requests a write lock (`wrlock`). The OS waits for active readers to finish, then gives the training thread exclusive access to rewrite the binary file, fully insulating the data structure from race conditions.

> **Architectural Note:** By building the thread pool, condition variables, and RW-locks from scratch using raw POSIX primitives, the concurrency model mirrors the underlying C architecture of production web servers like Nginx.
