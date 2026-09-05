# OriginDB — Deferred Delete Architecture

## 1. The problem this solves

In most vector databases (Qdrant, Milvus, pgvector, Pinecone, etc.), delete is just
another authenticated API call. Whoever holds a valid API key can delete data over
the network, the same way they can insert it. That means a single leaked or stolen
key is enough to destroy data, not just pollute it.

OriginDB closes this door at the widest level. There is no route anywhere in the
server that directly performs a deletion. Deletion only ever happens through the
local admin CLI, which requires being physically on the machine.

The earlier version of this design (immediate-only, `origin delete <id>`, no
network path at all) had real operational friction: every delete meant briefly
stopping the live server. This design keeps the same core guarantee, the network
can never delete data, while removing that friction, by splitting delete into two
separate steps that live in two separate, non-overlapping code paths.

## 2. The core idea: request and execution are different code paths

| | Can be triggered by | What it does |
|---|---|---|
| **Request** | Backend, over the network, authenticated with the table's normal API key | Appends one entry to a table's pending-deletes file. **Never opens `data.db` for writing. Never calls `db_delete`.** |
| **Execution** | A human, locally, via SSH/terminal | Reads the pending-deletes file, shows a count, asks for confirmation, then calls `db_delete` for each entry and clears the file. **This is the only code path that ever writes a deletion into `data.db`.** |

The security property this gives you: **even a fully compromised API key can never
delete a single byte of real data on its own.** The worst it can do is write junk
entries into a queue that a human reviews and approves before anything executes.
This is a stronger guarantee than "permission-gated," because there's no
misconfiguration or bypass that turns it back into direct network delete — the
code path for "network request → data gone" simply doesn't exist.

## 3. How the pending queue is stored on disk

The queue is a small, separate binary file per table, `pending.bin`, completely
distinct from the table's main data file. It has its own fixed layout:

```
[ id: 8 bytes ][ timestamp: 8 bytes ]  <- one entry, 16 bytes total
[ id: 8 bytes ][ timestamp: 8 bytes ]  <- next entry
...
```

- **Writing a request** appends exactly one 16-byte entry to the end of the file.
  The first 8 bytes are the record's id, the next 8 bytes are the Unix timestamp
  of when the request arrived. No header, no length prefix, nothing else.

  ```
  function requestDelete(tableName, recordId):
      openFileForAppend(tableName + "/pending.bin")
      writeBytes(recordId, 8 bytes)
      writeBytes(currentUnixTimestamp(), 8 bytes)
      closeFile()
  ```

- **Counting pending requests** needs no parsing at all, since every entry is the
  same fixed size, the count is just `file size ÷ 16`.

  ```
  function countPending(tableName):
      size = fileSizeInBytes(tableName + "/pending.bin")
      return size / 16
  ```

- **Loading the queue** reads that many 16-byte entries in sequence, keeping only
  the id from each one. The timestamp isn't needed for the search-time check,
  only for the admin's own reference later.

  ```
  function loadPendingIds(tableName):
      ids = []
      for each 16-byte chunk in file(tableName + "/pending.bin"):
          id = readBytes(chunk, first 8 bytes)
          ids.append(id)
      return ids
  ```

- **Sorting after load** turns the id list into something that can be checked
  quickly during a search, by binary search instead of a linear scan through
  every pending id one at a time. This is loaded once per table when it's opened,
  and reused for every search against that table.

  ```
  function isPendingDelete(recordId, sortedPendingIds):
      return binarySearch(sortedPendingIds, recordId) != notFound
  ```

- **Clearing the queue** happens only after every pending id has actually been
  processed by an administrator, and simply means discarding the file's contents
  and starting fresh.

The timestamp is not part of the security design. It exists purely so an
administrator processing a backlog can see how old each request is, which is
ordinary operational context, not something that changes what the queue can or
can't be used for.

## 4. Query-time filtering (soft, immediate exclusion)

Once an id is in `pending.bin`, it disappears from search results immediately,
before a human has ever run the execution step. This is a usability property (a
deleted-in-spirit record shouldn't keep showing up in results for hours until
someone processes the queue) as much as a security one.

- On opening a table, the pending-delete ids for that table are loaded into an
  in-memory sorted array.
- Both search paths (exact and ANN) extend the existing "skip if deleted" check
  to also skip anything found in that array. Both already have the candidate's
  id in hand at the point of comparison, so no extra bookkeeping is needed in the
  pending-delete file beyond the id itself.

This means a record can be in one of three states: **live**, **pending delete**
(excluded from search, bytes untouched, flag not yet flipped), or **deleted**
(tombstoned on disk). Only a human executing the queue moves a record from the
middle state to the last one.

## 5. `POST /delete-request` (network route)

- Authenticated the same way as `/insert`, `/search`, `/train`, table-scoped API
  key required.
- Request body: `{ "db_name": "movies", "id": 42 }`, a single concrete id only.
  No wildcards, no arrays, no "delete all", the schema itself makes a mass-delete
  request structurally impossible to express, not just disallowed by convention.
- Does not check whether the id exists. See §7 below, this is deliberate, not an
  oversight.
- Guarded by a dedicated lock so two concurrent requests don't race on the
  append.
- Response is identical regardless of whether the id turns out to be real, see §7.

## 6. `origin process-deletes <table>` (admin CLI command)

- Terminal-only, same trust boundary as `origin init` / `origin delete`.
- Reads `pending.bin` for the table.
- Shows a count and asks for confirmation before doing anything:
  ```
  14 records marked for deletion. Proceed? (y/n)
  ```
  This is the single most important control in the whole design, the point where
  a human notices "wait, that's a lot more than I expected" before any damage
  happens. It turns a compromised key spamming fake delete requests into
  something a person catches by eye, rather than something that silently
  executes.
- On confirmation, calls `db_delete(id)` for each pending id. Ids that don't
  correspond to a real record are looked up here for the first time, and simply
  no-op (this is where existence gets resolved, not at request time). Ids already
  deleted in a previous run also just no-op.
- Clears `pending.bin` afterward.

## 7. Why existence is never checked at request time (the oracle problem)

An early version of this design considered having `/delete-request` check
whether an id exists and respond differently depending on the answer. That would
be a mistake, and it's worth explaining why, since it's a subtle but well-known
category of vulnerability, an **oracle**: a system that answers a yes/no question
for an attacker, even if it doesn't take direct action based on the answer.

Say the check happened. A request naming a real record would take slightly
longer to process, since confirming existence means looking the record up. A
request naming a typo would skip that lookup and finish a hair faster. Even if
both requests get the exact same reply, word for word, the time each one took to
answer would differ. An attacker holding a leaked key doesn't need the reply to
say anything different, they only need to measure how long each reply took.
Sending many guesses and timing each response lets them work out, purely from the
clock, which ids are real, even though every reply looked identical on the page.
This is generally called a timing side channel.

If `/delete-request` for a real id and a typo'd id ever produce any
distinguishable signal, different response text, different status code,
different response size, or a measurable timing difference, an attacker can
enumerate every real id in a table by sending guesses and watching which ones get
a different answer. That's real information leakage even though no data was ever
deleted directly.

The fix, and the one this design commits to:

- **The endpoint never checks existence.** It accepts any well-formed id
  unconditionally, queues it, and returns the same response either way. A real id
  and a typo do exactly the same amount of work, appending one entry to a file,
  so there is nothing for a timing measurement to detect a difference in. The
  side channel closes as a side effect of this decision rather than needing a
  fix of its own.
- **Existence is only ever resolved later, locally, by a human**, during
  `process-deletes`, when the count is already being reviewed anyway. A typo'd id
  just quietly does nothing when its turn comes up, nothing about that moment is
  visible to whoever originally sent the request.

Practical guardrails worth keeping in mind so this property doesn't get
accidentally reintroduced later:

- If rate limiting or abuse detection is ever added to this endpoint, "suspicious"
  must never be defined in terms of how many ids didn't exist, that reintroduces
  the same oracle through the back door.
- Keep the HTTP response (status, body, `Content-Length`) byte-identical for a
  real id versus a nonexistent one, even a differing response size is technically
  a distinguishable signal to a sufficiently determined attacker.
- Anything from `process-deletes` (the count, which ids were real) must stay
  local/terminal-side only, never echoed to a network-reachable log or dashboard,
  or the same distinction leaks back out through a different door.

## 8. What this design does and doesn't claim

**Does claim:** an API key, however compromised, can never cause a single byte of
real data to be deleted on its own. The most it can do is queue a request that a
human must knowingly approve before anything happens.

**Does not claim:** this makes OriginDB more secure than mature engines in
general, or that it's a complete access-control system. It's a narrow, specific
guarantee about one operation, deletion, achieved by removing the network's
ability to directly execute it, not a general security hardening claim.
