# OriginDB Usage & API Documentation

OriginDB enforces a strict security boundary between Admin Operations (provisioning tables and keys via the terminal) and Application Operations (reading and writing data via the TCP API).

1. Admin Provisioning (Terminal / TUI)

Tables cannot be created over the network. You must initialize them locally to generate a secure, table-specific API key.

Start the engine:
```
./origin
```

Commands:

Initialize a new table: origin init <db_name> <dimension> <capacity> <metric>

Example: origin init movies 128 1000 0

Note: Metric 0 = L2 Distance, 1 = Inner Product.

Edge Case: If the table already exists, this command will fail to prevent overwriting data.

Output: This generates and displays your 256-bit API_KEY for this specific table. Save it.

Open an existing table: origin open <db_name> <dimension> <metric>

Example: origin open movies 128 0

Start the TCP Server: origin server <port>

Example: origin server 9090

2. Network API (Application Operations)

Once the server is listening, backend applications can interact with the engine using raw HTTP POST requests.

Authentication:
Every request must include the API key generated during origin init in the headers:
Authorization: Bearer <YOUR_TABLE_API_KEY>

Python Client Example (requests)

import requests

SERVER_URL = "http://localhost:9090"
HEADERS = {
    "Authorization": "Bearer YOUR_64_CHAR_HEX_KEY_HERE",
    "Content-Type": "application/json"
}

# 1. Insert a Vector
insert_payload = {
    "db_name": "movies",
    "id": 1,
    "vector": [0.5, 0.1, -0.4], # Must match the dimension set in TUI
    "metadata": "Inception"
}
response = requests.post(f"{SERVER_URL}/insert", json=insert_payload, headers=HEADERS)
print("Insert:", response.json())

# 2. Exact Nearest Neighbor (ENN) Search
search_payload = {
    "db_name": "movies",
    "top_k": 5,
    "use_ann": False, # Forces exact brute-force search
    "nprobe": 1,
    "query_vector": [0.4, 0.1, -0.3]
}
response = requests.post(f"{SERVER_URL}/search", json=search_payload, headers=HEADERS)
print("Search Results:", response.json())


TypeScript / Node.js Client Example (fetch)

const SERVER_URL = "http://localhost:9090";
const HEADERS = {
    "Authorization": "Bearer YOUR_64_CHAR_HEX_KEY_HERE",
    "Content-Type": "application/json"
};

// 1. Train the IVF Index (K-Means)
async function trainIndex() {
    const payload = {
        db_name: "movies",
        k: 10,             // Number of clusters
        max_iterations: 50 // Algorithm iterations
    };

    const res = await fetch(`${SERVER_URL}/train`, {
        method: "POST",
        headers: HEADERS,
        body: JSON.stringify(payload)
    });
    console.log(await res.json());
}

// 2. Approximate Nearest Neighbor (ANN) Search
async function searchANN(queryVector: number[]) {
    const payload = {
        db_name: "movies",
        top_k: 5,
        use_ann: true, // Uses the trained IVF index
        nprobe: 2,     // Searches the 2 closest clusters
        query_vector: queryVector
    };

    const res = await fetch(`${SERVER_URL}/search`, {
        method: "POST",
        headers: HEADERS,
        body: JSON.stringify(payload)
    });
    console.log(await res.json());
}


3. Error Handling & Edge Cases

The server strictly routes all failures through a unified JSON error handler. Your backend should be prepared to handle the following HTTP status codes and edge cases:

400 Bad Request (Invalid Parameter)

Malformed JSON: The request body is missing required fields (e.g., omitting top_k in a search).

Dimension Mismatch: The client sends an array of 100 floats, but the table was initialized with 128.

Untrained ANN: The client requests /search with "use_ann": true, but /train has never been executed for that table.

401 Unauthorized (Security Deflection)

Missing Key: The Authorization header is omitted.

Cross-Table Violation: The client provides a valid key, but it belongs to the music table while attempting to query the movies table.

404 Not Found (Wrong Request)

Missing Table: The client attempts to insert or search a table name that has not been initialized locally via the TUI.

Invalid Route: The client sends a request to a route other than /insert, /search, or /train.
