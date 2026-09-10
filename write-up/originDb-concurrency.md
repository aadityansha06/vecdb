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

### 4. Why No `epoll` (Yet)

Everything in §1 is still fundamentally a **thread-per-connection** model, not an event loop. The 128 worker threads (the number grew from the 32 in the original design as the thread pool matured) each pull one file descriptor off the queue and then block on it end to end: `recv()` the headers, `recv()` the body, do the work, `send()` the response, `close()`. For the entire lifetime of that connection, one OS thread is committed to it — even during the parts where the thread is doing nothing but waiting on the network, which for a slow client or a large request body can be the majority of that connection's lifetime.

That's the direct cause of the concurrency ceiling documented in [`Deployment-and-Capacity.md`](Deployment-and-Capacity.md): with exactly 128 threads, at most 128 connections can be *actively being served* at any instant, no matter how little CPU work each one actually needs. The 512-slot queue absorbs bursts past that, but a connection sitting in the queue isn't being served at all yet — it's just waiting for a thread to free up. Load testing shows this holds up cleanly to ~950 concurrent connections and gets unpredictable right around 1000, which lines up with "queue capacity plus in-flight threads" being the real limiting resource, not raw CPU.

An `epoll`-based design (or `kqueue`/IOCP on other platforms) would decouple "connections currently open" from "OS threads consumed." A small number of threads register every open socket with the kernel and block on a single `epoll_wait()` call instead of one `recv()` per thread; the kernel only wakes a thread when a specific socket actually has data ready. Thousands of idle-but-open connections would cost file descriptors and a little kernel bookkeeping, not one committed thread apiece. The 512/128 numbers in the current design would stop being the same kind of hard ceiling.

I haven't built this yet, on purpose, not because it wouldn't help. Two reasons:

- **Straight-line blocking code is easier to get right first.** Right now, the routing logic in `handel_client` reads like a normal function: receive the whole request, parse it, touch the database, send a response, return. An `epoll` reactor turns that into a state machine per connection — you can be interrupted after a partial header, a partial body, or mid-`send()` on a slow client, and have to resume correctly next time that fd becomes readable/writable. Getting that state machine right, on top of the locking work in §2–§3 and the compaction work in §5, is a meaningfully bigger project than the thread pool was, and this is still an early/WIP database — correctness under the simpler model came first.
- **The current ceiling hasn't actually been hit by a real workload yet.** ~950 clean concurrent connections is a lot of simultaneous *test* traffic; it hasn't yet been the bottleneck in an actual deployment. Rewriting the I/O layer before it's the limiting factor would be solving a problem this project doesn't have yet, at the cost of the every-request-is-easy-to-follow code that currently exists.

**What wouldn't change if this happens later:** everything in §1–§3 and §5 is about correctness once a worker is executing — the locks, the atomic appends, the index rwlock, the compaction lock discipline. Moving from a blocking thread pool to an `epoll` front end only changes how a socket gets handed to a worker in the first place; it doesn't touch what that worker does with the database once it has the request in hand. It's a front-door change, not a rewrite of the concurrency-correctness work already done.

### 5. The Compaction Layer: Rewriting Under the Same Locks

Deletion becoming physical (see `Delete-architecture.md`, §11) adds a third kind of critical section beyond appends and index rebuilds: replacing the entire data file out from under a table that other threads might be mid-search against.

Compaction reuses the exact same per-table `pthread_rwlock_t` that guards insert-merges — it takes the **write** side, the same lock a `/search` request holds the **read** side of. That's deliberate: compaction frees and replaces both the in-memory record array and the memory-mapped view of `data.db`, so any thread holding even a read lock on the table at that moment would be holding a pointer about to go invalid. Taking the write lock guarantees no reader is mid-flight when the swap happens — the same property `/insert`'s background merge into the live table already relied on.

The IVF index is invalidated as a separate step, under the existing `index_lock` rwlock from §3, not the table lock. `save_ivf_index`, `load_ivf_index`, and the index removal that follows a compaction all contend for that same lock, so a `/train` or ANN `/search` in flight can't observe a half-removed index file — the identical dirty-read problem §3 was built to prevent, just for one more writer.

The admin CLI's own `origin delete` and `process-deletes` compact without taking either lock at all. That's not an oversight — the CLI only ever runs while the server process is stopped, so there's no other thread in the same address space to race against; the two processes only ever share the on-disk files, never these in-memory locks.

> **Architectural Note:** By building the thread pool, condition variables, and RW-locks from scratch using raw POSIX primitives, the concurrency model mirrors the underlying C architecture of production web servers like Nginx — Nginx's own worker processes, notably, are `epoll`-based rather than thread-per-connection, which is exactly the gap described in §4.
