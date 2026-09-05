# Deferred Delete Architecture in OriginDB

**In one sentence:** an API key, however completely compromised, can queue a
deletion request but can never cause a real deletion to happen on its own. Only a
human, confirming locally on the machine, can turn a request into an actual change
on disk.

## The starting point

Most vector databases treat deletion as an ordinary authenticated API call.
Whatever holds a valid key can insert data, and the same key can typically delete
it. This is true of the large managed platforms as much as it is of self hosted
engines built on top of Postgres or SQLite. The credential is trusted completely.
If it leaks, gets stolen, or ends up in a compromised client, whoever holds it can
remove data from the system directly, immediately, over the network.

Traditional relational databases handle this risk the same way they have for
decades. A leaked credential with delete or drop privileges executes exactly what
it asks for. The safety net sits after the fact, in the form of write ahead logs
and point in time recovery, which let an operator roll a database back to a moment
before the damage occurred. This is a mature and well tested strategy, but it is
fundamentally a strategy of detection and reversal rather than prevention. The
database itself does not refuse the command. It assumes the credential presented
to it is legitimate, because that is the only signal it has to go on.

OriginDB starts from a different premise for this one operation specifically.
Rather than asking whether a credential is trustworthy enough to be allowed to
delete, the design removes the network's ability to execute a deletion in the
first place, for any credential, trusted or not.

## The split between requesting and executing

The mechanism is a separation between two actions that most systems treat as one.

**Requesting a deletion**, over the network, with a valid table scoped API key,
does not delete anything. It writes a small entry into a per table file, noting
the record id and the time the request arrived. Nothing about handling this
request opens the table's actual data file for writing. The code path that would
perform a real deletion is never reached from the network at all.

**Executing the queue**, reachable only from the local terminal on the machine
itself, reads that file of pending requests. Before doing anything with them, it
prints the count of records queued for deletion and waits for a human to confirm.
Only after that confirmation does it call the function that actually flips a
record's status on disk. Afterward the queue is cleared, ready to collect the next
batch of requests.

The result is that a compromised API key, however completely compromised, can
never cause a single byte of real data to disappear on its own. The most damage it
can do is write entries into a queue that a person must knowingly approve. There
is no configuration flag standing between the network and this guarantee, because
there is no code connecting them to disable.

## What happens to a record between request and execution

A record that has been requested for deletion but not yet processed sits in a
middle state. Its bytes remain untouched on disk, and the underlying deleted flag
has not been flipped. It has, however, already stopped appearing in search
results. When a table is opened, the list of pending ids for that table is loaded
into memory, and every search path checks a candidate record's id against that
list before it is returned. This means a record effectively disappears from the
application's point of view the moment a delete is requested, long before an
administrator ever reviews and approves the batch. The gap between requesting a
deletion and it becoming permanent is invisible to whoever is using the system day
to day. It only matters to the person running the periodic cleanup.

## Why a delete request can only ever name one record

The request itself is deliberately narrow. It accepts a single table name and a
single record id, nothing else. There is no way to phrase a request that asks for
every record in a table, or for a range of ids, or for anything matching a
pattern. This is not a rule enforced by checking the request afterward and
rejecting broad ones. The shape of the request simply cannot express anything
broader than one record, so there is nothing to check for and nothing to bypass.

## Why the system never checks whether an id exists at request time

An earlier version of this design considered checking, at the moment a delete
request arrives, whether the record actually exists, and replying differently
depending on the answer. This turns out to create a subtle problem worth
explaining in some detail, because it is easy to build by accident and easy to
miss.

Suppose the check happened. A request naming a real record would take a certain
amount of time to process, since confirming the record exists means looking it
up. A request naming a typo, one that does not correspond to any real record,
would skip that lookup and finish faster. Even if both requests are given the
exact same reply, word for word, the amount of time each one took to answer would
differ, if only by a small amount. An attacker holding a leaked key does not need
the reply to say anything different. They only need to measure how long each
reply took to arrive. Sending many guesses at ids and timing each response lets
them work out, purely from the clock, which ids are real and which are not, even
though every reply they received looked identical on the page. This is generally
called a timing side channel, and it is a well known way that supposedly
identical responses can still leak information through a channel nobody thought
to hide.

OriginDB avoids this not by adding a special defense against timing measurement,
but by removing the reason for one to exist. The decision to never check whether
an id exists at request time means a real id and a typo do exactly the same
amount of work, which is to say, they both do the single, fixed task of writing
one line to a file. There is no extra lookup happening for real ids that a typo
skips, so there is nothing for a timing measurement to detect a difference in.
The side channel closes as a consequence of a decision made for an entirely
different reason, rather than needing its own fix bolted on afterward.

Existence is only ever resolved later, locally, when an administrator runs the
processing step and the queue is already being reviewed by a person. A request
naming a real id proceeds to an actual deletion. A request naming a typo quietly
does nothing when its turn comes up. Neither outcome is visible to whoever sent
the original request, because by the time it is decided, that request has long
since returned its one, identical reply.

## What this guarantees, and what it does not

The guarantee this architecture provides is narrow and specific. An API key, no
matter how it was compromised, cannot cause a real deletion to happen on its own.
The most it can do is add entries to a list that a person must actively choose to
act on. That is the whole claim.

It is worth being equally clear about what this is not. It is not a general
security hardening of the system, and it does not make OriginDB more secure than
mature, extensively audited databases across the board. Those systems have
decades of production hardening behind them that this project does not. What is
different here is one specific decision about one specific operation, made
deliberately, and carried through consistently enough that there is no path
around it.
