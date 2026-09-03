# Per Table Key Authentication Design for a Multi Tenant Vector Database in C

After getting the storage architecture in place, I started thinking about how the API and table initialization should work.

One thing I wanted was to have a separate key for each table. So if the database has a movies table and a music table, they would each have their own key. I didn't want to put these keys inside the actual record metadata because that metadata belongs to the user's data, while authentication is part of the database's system level information.

My idea was to keep the table keys somewhere separately in the database's system metadata. When a user initializes a table for the first time, something like origin init movies, the engine checks whether the table exists. If it doesn't, it creates the table, generates a key for it and stores that key along with the table information. The generated key is then given to the user so they can use it from their backend.

For generating the key, I was looking at the Linux dev urandom interface since it provides cryptographically secure random bytes. A 256 bit value can be generated from 32 random bytes. Eventually this can be handled directly inside the C implementation rather than relying on a shell command, which is exactly what I ended up doing, reading straight from dev urandom in C and hex encoding the bytes myself.

I also thought about what should happen if someone creates a table outside the CLI. For example, they could manually create the directory from the terminal, or the backend could try to create a table through the API. The important thing here is that the existence of a folder shouldn't itself mean that a valid table exists. The engine should still validate the table against its own system metadata and structure.

For the API, I wanted to keep the distinction between database administration and normal application operations clear. Initialization and table management are administrative operations, while the backend mainly needs operations such as insert, search and retrieval. So the flow I had in mind was that the database owner initializes the table and gets its key, and then the backend uses that key when communicating with the VectorDB.

This also avoids exposing an unauthenticated initialization endpoint to the network. Otherwise, anyone who can reach the server could potentially keep creating tables.

So the overall flow I designed was basically CLI initializes the table, generates the table key, stores the system metadata, then returns the key to the owner. Then the backend sends the table name and key, gets authenticated, and performs the database operation.

This part of the architecture is still closely tied to the storage design because the table itself is not just a directory on disk. It has its own metadata, key and storage structure that the engine needs to understand before it can operate on it.

Full writeup on Medium: [Per Table Key Authentication Design for a Multi Tenant Vector Database in C](https://medium.com/@vermaadityansh/per-table-key-authentication-design-for-a-multi-tenant-vector-database-in-c-51f2b834df82)
