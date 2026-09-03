# Storage Architecture Design for Targeted Disk Reads in ANN Vector Search in C

I've been building a VectorDB from scratch in C, and while working on the storage layer, I started thinking more about how the database should actually be organized once tables and authentication are introduced.

The first part of the architecture was mainly about separating the data used for lookup from the actual records. The ANN index should be optimized for searching, while the actual records should remain in their own storage. I also wanted the storage layer to be disk backed instead of simply loading everything into RAM. The idea is to keep track of where a vector lives on disk using its byte offset, so once the ANN search tells us which IDs we actually need, we can use fseek() to jump directly to those locations and load only the required vectors. This becomes especially useful with IVF, where we only need to read vectors belonging to the selected nprobe clusters instead of scanning the entire dataset.

```
/origin_data/movies/
├── data.db        (raw Record_t structs)
└── ivf_index.bin  (cluster_t data: centroids + ID lists)
```

Inside that folder, I split the storage into two separate files, one for the raw data records and another just for the K means cluster metadata.

I also made it so the build phase doesn't just run on its own. It only triggers when the user explicitly tells the database to train. When it does, it calculates all the centroids, figures out which records belong to which cluster and saves that whole mapping directly into the metadata file.

The real magic happens when you actually run a search. In C we use fseek() for reading specific data instead of the whole file. If someone wants to do an IVF search instead of brute forcing the whole dataset, the database doesn't even touch the main data file at first. It just opens the cluster metadata file, checks the query vector against the centroids, and figures out the top nprobe clusters. Since that metadata tells us the exact record IDs that belong to those specific clusters, it acts like a lookup table. We can then jump straight into the main data file and load only those specific vectors into RAM, completely ignoring the rest of the dataset. It lets me extract exactly what is needed without blowing up the memory, making the whole architecture incredibly fast.

Full writeup on Medium: [Storage Architecture Design for Targeted Disk Reads in ANN Vector Search in C](https://medium.com/@vermaadityansh/storage-architecture-design-for-targeted-disk-reads-in-ann-vector-search-7159e5b46453)
