# libudx Architecture

This document describes the internal architecture and design decisions of libudx.

## File Format

UDX files are organized as follows:

```
┌─────────────────────────────────────────────────────────┐
│ Main Header                                             │
│  - Magic: "UDX\0"                                       │
│  - Version: 1.0                                         │
│  - Dictionary DB Table Offset                           │
├─────────────────────────────────────────────────────────┤
│ Dictionary DB 1                                         │
│  - DB Header (metadata size, offsets, counts, checksum) │
│  - Metadata (optional)                                  │
│  - Chunks (compressed data blocks)                      │
│  - Chunk Table                                          │
│  - B+ Tree Index                                        │
│    - Leaf Nodes (compressed)                            │
│    - Internal Nodes (compressed)                        │
├─────────────────────────────────────────────────────────┤
│ Dictionary DB 2                                         │
│  - ...                                                  │
├─────────────────────────────────────────────────────────┤
│ ...                                                     │
├─────────────────────────────────────────────────────────┤
│ Dictionary DB Table                                     │
│  - Count                                                │
│  - [Offset, Name] pairs                                 │
└─────────────────────────────────────────────────────────┘
```

### Key Design Points

- **Chunk Storage**: Data is stored in chunks with a maximum size of 64KB, each compressed independently
- **Address Encoding**: 48-bit chunk index + 16-bit offset in chunk
- **Index Structure**: Static B+ tree for efficient O(log n) lookups
- **Checksum**: CRC32 checksum on database header for corruption detection

## Architecture

```
libudx/
├── src/
│   ├── udx_writer.h/c    # High-level writer API
│   ├── udx_reader.h/c    # High-level reader API
│   ├── udx_chunk.h/c     # Chunk-based storage with compression
│   ├── udx_words.h/c     # Ordered word container (B-tree wrapper for building)
│   ├── udx_types.h/c     # Core data types and serialization
│   ├── udx_utils.h/c     # Utility functions (string folding, I/O)
│   └── udx_btree.h/c     # B-tree implementation (Joshua J Baker)
└── include/              # Public headers
```

## Design Decisions

### Why B+ Trees?

B+ trees provide predictable O(log n) performance for lookups and naturally support range queries and prefix matching. The static layout (pre-built during write) enables efficient zero-copy parsing during read.

### Why Two Different Tree Structures?

libudx uses **two different tree structures** for different purposes:

| Tree Type | Used By | Purpose |
|-----------|---------|---------|
| **B-tree** | `udx_words` (internal) | Memory storage during **building phase** - dynamic, auto-sorted, supports insertions |
| **B+ tree** | UDX file index | Disk storage in **file format** - static, optimized for reading |

During **write**, entries are added to a dynamic B-tree (`udx_words`) which efficiently handles out-of-order insertions. At the end, this B-tree is traversed in sorted order and converted to a **static B+ tree** that's written to disk. The static B+ tree format enables zero-copy parsing and efficient lookups when reading UDX files.

### Why Chunks?

Storing data in chunks (up to 64KB each) with independent compression provides:
- Better compression ratios (similar data compresses together)
- Random access to individual data blocks
- Memory-efficient reading (only decompress what's needed)

**Limitation**: Individual data blocks must fit within a single chunk (max 64KB). For larger data, split into multiple blocks.

### Case-Insensitive Search

The library uses **word folding** (converting to lowercase) for indexing while preserving the original word case. This enables:
- Case-insensitive lookups ("Hello", "hello", "HELLO" all match)
- Preserved original forms for display
- Efficient B+ tree traversal (sorted by folded form)
