# udx Architecture

This document describes the internal architecture and design decisions of udx.

## Key Design Points

- **Chunk Storage**: Data is stored in chunks with independent compression, block offset limited to 16-bit (0-65535)
- **Address Encoding**: 48-bit chunk index + 16-bit offset in chunk
- **Index Structure**: Static B+ tree for efficient O(log n) lookups
- **Checksum**: CRC32 checksum on database header for corruption detection

## Architecture

```
udx/
└── src/
    ├── udx_writer.h/c    # High-level writer API
    ├── udx_reader.h/c    # High-level reader API
    ├── udx_chunk.h/c     # Chunk-based storage with compression
    ├── udx_words.h/c     # Ordered word container (B-tree wrapper for building)
    ├── udx_types.h/c     # Core data types and serialization
    ├── udx_utils.h/c     # Utility functions (string folding, I/O)
    └── udx_btree.h/c     # B-tree implementation (Joshua J Baker)
```

### Module Architecture Diagram

The library is organized into three distinct layers:

- **Public API Layer**: High-level interfaces (`udx_writer` and `udx_reader`) that provide simple, intuitive functions for creating and reading UDX files. These APIs hide implementation details and handle resource management automatically.

- **Core Modules**: The foundational components that implement the UDX format specification. `udx_chunk` manages compressed data storage, `udx_words` provides ordered word container functionality during database building, and `udx_types` defines all core data structures and serialization routines.

- **Utility Modules**: Supporting components that provide common functionality. The `udx_btree` module offers a dynamic B-tree implementation (courtesy of Joshua J Baker) used during the build phase, while `udx_utils` provides cross-cutting utilities like string folding for case-insensitive indexing and platform-independent I/O operations.

```mermaid
%%{init: {"flowchart": {"diagramPadding": 150}}}%%
graph TB
    subgraph API["Public API Layer"]
        Writer["udx_writer.h<br/>High-level Writer API"]
        Reader["udx_reader.h<br/>High-level Reader API"]
    end

    subgraph Core["Core Modules"]
        Chunk["udx_chunk.h/c<br/>Chunk Storage<br/>(Compression)"]
        Words["udx_words.h/c<br/>Words Container<br/>(B-tree Wrapper)"]
        Types["udx_types.h/c<br/>Core Data Types<br/>& Serialization"]
    end

    subgraph Util["Utility Modules"]
        BTree["udx_btree.h/c<br/>B-tree Implementation<br/>(Joshua J Baker)"]
        Helpers["udx_utils.h/c<br/>Utility Functions<br/>(String Folding, I/O)"]
    end

    %% Writer flow
    Writer --> Words
    Writer --> Chunk
    Writer --> Types
    Words --> BTree
    Words --> Helpers
    Chunk --> Helpers
    Types --> Helpers

    %% Reader flow
    Reader --> Chunk
    Reader --> Types
    Chunk --> Helpers
    Types --> Helpers

    %% Styling
    classDef apiStyle fill:#e1f5fe,stroke:#01579b,stroke-width:2px
    classDef coreStyle fill:#f3e5f5,stroke:#4a148c,stroke-width:2px
    classDef utilStyle fill:#e8f5e9,stroke:#1b5e20,stroke-width:2px

    class Writer,Reader apiStyle
    class Chunk,Words,Types coreStyle
    class BTree,Helpers utilStyle
```

### Write Flow

The write process transforms user data into an optimized UDX file through a pipeline:

1. **Writer Initiation**: `udx_writer` creates the file header and manages the overall write operation.
2. **Database Building**: `udx_db_builder` collects entries, storing data in chunks via `udx_chunk_writer` and indexing words via `udx_words`.
3. **Index Construction**: During the build phase, `udx_words` uses a dynamic B-tree (`udx_btree`) to efficiently collect and sort entries out-of-order.
4. **B+ Tree Generation**: At finalization, the sorted entries are converted into a static B+ tree and written to the file for O(log n) lookup performance.
5. **File Finalization**: Chunks, chunk table, and B+ tree index are written to disk, with back-patched headers for optimal file layout.

```mermaid
%%{init: {"flowchart": {"diagramPadding": 150}}}%%
graph LR
    A[udx_writer] --> B[udx_db_builder]
    B --> C[udx_words]
    C --> D[udx_btree]
    B --> E[udx_chunk_writer]
    E --> F[UDX File]
    D --> F
```

### Read Flow

The read process provides efficient access to dictionary data:

1. **File Opening**: `udx_reader` parses the file header and validates the format, including checksums and version compatibility.
2. **Database Access**: `udx_db` represents an opened dictionary within the file, providing access to metadata and index structures.
3. **Index Lookup**: The B+ tree index enables fast O(log n) word lookups with prefix matching support, returning chunk addresses without loading data.
4. **Data Retrieval**: When data is needed, `udx_chunk_reader` decompresses the relevant chunk on-demand and returns the full data block.
5. **Lazy Loading**: Data is loaded only when requested, minimizing memory usage and enabling efficient random access to individual entries.

```mermaid
%%{init: {"flowchart": {"diagramPadding": 150}}}%%
graph LR
    A[UDX File] --> B[udx_reader]
    B --> C[udx_db]
    C --> D[udx_chunk_reader]
    C --> E[B+ Tree Index]
```

## Design Decisions

### Why B+ Trees?

B+ trees provide predictable O(log n) performance for lookups and naturally support range queries and prefix matching. The static layout (pre-built during write) enables efficient zero-copy parsing during read.

### Why Two Different Tree Structures?

udx uses **two different tree structures** for different purposes:

| Tree Type | Used By | Purpose |
|-----------|---------|---------|
| **B-tree** | `udx_words` (internal) | Memory storage during **building phase** - dynamic, auto-sorted, supports insertions |
| **B+ tree** | UDX file index | Disk storage in **file format** - static, optimized for reading |

During **write**, entries are added to a dynamic B-tree (`udx_words`) which efficiently handles out-of-order insertions. At the end, this B-tree is traversed in sorted order and converted to a **static B+ tree** that's written to disk. The static B+ tree format enables zero-copy parsing and efficient lookups when reading UDX files.

### Why Chunks?

A **chunk** is a compressed storage unit containing one or more **data blocks**. Each data block represents the actual content (e.g., dictionary definition) for a specific word or entry.

Storing data in chunks with independent compression provides:
- Better compression ratios (similar data compresses together)
- Random access to individual data blocks
- Memory-efficient reading (only decompress what's needed)

**Limitation**: Block offset in chunk is limited to 16-bit (0-65535). Each block's start position is guaranteed to be within this range.

### Case-Insensitive Search

The library uses **word folding** (converting to lowercase) for indexing while preserving the original word case. This enables:
- Case-insensitive lookups ("Hello", "hello", "HELLO" all match)
- Preserved original forms for display
- Efficient B+ tree traversal (sorted by folded form)
