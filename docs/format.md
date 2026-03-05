# UDX File Format Specification

**Version:** 1.0
**Last Updated:** 2026-03-05

## Table of Contents

1. [Overview](#overview)
2. [File Structure](#file-structure)
3. [Main Header](#main-header)
4. [Database Header](#database-header)
5. [Metadata](#metadata)
6. [Chunk Storage](#chunk-storage)
7. [Chunk Table](#chunk-table)
8. [B+ Tree Index](#b-tree-index)
9. [Dictionary Database Table](#dictionary-database-table)
10. [Address Encoding](#address-encoding)
11. [String Encoding](#string-encoding)
12. [Compression](#compression)
13. [Checksum](#checksum)
14. [Limitations](#limitations)

---

## Overview

UDX (Universal Dictionary eXchange) is a binary file format for storing dictionary data with efficient indexing. The format is designed for:

- **Fast lookups** using B+ tree indexing (O(log n) complexity)
- **Space efficiency** through zlib compression
- **Case-insensitive search** while preserving original word forms
- **Multiple databases** in a single file
- **Large file support** with 64-bit file offsets

All multi-byte integers are stored in **little-endian** byte order.

---

## File Structure

A UDX file is organized as follows:

```
┌─────────────────────────────────────────────────────────┐
│ Main Header (14 bytes)                                  │
├─────────────────────────────────────────────────────────┤
│ Dictionary Database 1                                   │
│  ├─ Database Header (44 bytes)                          │
│  ├─ Metadata (optional, variable size)                  │
│  ├─ Chunks (compressed data blocks)                     │
│  ├─ Chunk Table                                         │
│  └─ B+ Tree Index                                       │
│     ├─ Leaf Nodes (compressed)                          │
│     └─ Internal Nodes (compressed)                      │
├─────────────────────────────────────────────────────────┤
│ Dictionary Database 2                                   │
│  └─ ...                                                 │
├─────────────────────────────────────────────────────────┤
│ ...                                                     │
├─────────────────────────────────────────────────────────┤
│ Dictionary Database Table                               │
│  ├─ Count (uint64_t)                                    │
│  └─ [Offset, Name] pairs                               │
└─────────────────────────────────────────────────────────┘
```

---

## Main Header

The main header is the first 14 bytes of the file.

| Offset | Size | Type   | Field                | Description                          |
|--------|------|--------|----------------------|--------------------------------------|
| 0      | 4    | char   | `magic`              | Magic bytes: "UDX\0"                 |
| 4      | 1    | uint8  | `version_major`      | Major version number (1)             |
| 5      | 1    | uint8  | `version_minor`      | Minor version number (0)             |
| 6      | 8    | uint64 | `db_table_offset`    | File offset of Dictionary DB Table   |

**Total Size:** 14 bytes

**C Structure:**
```c
typedef struct {
    char magic[4];           // "UDX\0"
    uint8_t version_major;   // 1
    uint8_t version_minor;   // 0
    uint64_t db_table_offset;
} udx_header;
```

---

## Database Header

Each dictionary database begins with a 44-byte header.

| Offset | Size | Type   | Field                      | Description                                  |
|--------|------|--------|----------------------------|----------------------------------------------|
| 0      | 8    | uint64 | `chunk_table_offset`       | File offset of Chunk Table                   |
| 8      | 8    | uint64 | `index_root_offset`        | File offset of B+ tree root node             |
| 16     | 8    | uint64 | `index_first_leaf_offset`  | File offset of first leaf node               |
| 24     | 4    | uint32 | `index_entry_count`        | Number of unique words (index entries)       |
| 28     | 4    | uint32 | `index_item_count`         | Total number of data items across all words  |
| 32     | 4    | uint32 | `index_bptree_height`      | B+ tree height (1 = single leaf node)        |
| 36     | 4    | uint32 | `metadata_size`            | Metadata size in bytes (0 = no metadata)    |
| 40     | 4    | uint32 | `checksum`                 | CRC32 checksum of first 40 bytes            |

**Total Size:** 44 bytes

**C Structure:**
```c
typedef struct {
    uint64_t chunk_table_offset;
    uint64_t index_root_offset;
    uint64_t index_first_leaf_offset;
    uint32_t index_entry_count;
    uint32_t index_item_count;
    uint32_t index_bptree_height;
    uint32_t metadata_size;
    uint32_t checksum;
} udx_db_header;
```

---

## Metadata

The metadata section immediately follows the database header. Its size is specified by `metadata_size` in the database header. If `metadata_size` is 0, no metadata is present.

Metadata can contain any application-specific data (e.g., copyright information, encoding details, creation date).

---

## Chunk Storage

Data is stored in chunks with independent zlib compression. Each chunk has a maximum size of 64KB (65536 bytes).

### Chunk Format

Each chunk is stored as:

| Offset | Size    | Type   | Field                | Description                          |
|--------|---------|--------|----------------------|--------------------------------------|
| 0      | 4       | uint32 | `uncompressed_size`  | Size after decompression             |
| 4      | 4       | uint32 | `compressed_size`    | Size of compressed data              |
| 8      | variable | uint8 | `data`               | Compressed data (zlib, default level) |

**Total Chunk Size:** 8 + `compressed_size`

### Chunk Constraints

- Maximum uncompressed size per chunk: 65536 bytes
- Each data block must fit entirely within one chunk
- Chunks are written sequentially after the metadata section

### Compression Details

- **Algorithm:** zlib (DEFLATE)
- **Compression Level:** Z_DEFAULT_COMPRESSION (typically 6)
- **Maximum Compressed Size:** `compressBound(65536)` bytes

---

## Chunk Table

The chunk table is stored after all chunks and before the B+ tree index.

| Offset | Size    | Type   | Field            | Description                          |
|--------|---------|--------|------------------|--------------------------------------|
| 0      | 8       | uint64 | `chunk_count`    | Number of chunks                     |
| 8      | 8×N     | uint64 | `offsets[]`      | File offsets of each chunk           |

**Total Size:** 8 + (8 × `chunk_count`)

Each offset points to the beginning of a chunk (the `uncompressed_size` field).

---

## B+ Tree Index

The B+ tree provides O(log n) lookup performance. The tree is built bottom-up:
- **Leaf nodes** are written first (in sorted order)
- **Internal nodes** are written last (root node is last)

### B+ Tree Parameters

| Parameter | Value               | Description                          |
|-----------|---------------------|--------------------------------------|
| Min Elements | 64              | Minimum elements per node            |
| Max Elements | 8192            | Maximum elements per node            |
| Max Node Size | ~1 MB          | Maximum compressed node size         |

All B+ tree nodes are compressed using zlib.

### Node Types

| Value | Type      | Description                          |
|-------|-----------|--------------------------------------|
| 0x01  | INTERNAL  | Internal node (navigation)           |
| 0x02  | LEAF      | Leaf node (contains data entries)    |

### Leaf Node Format

Leaf nodes contain the actual index entries.

**Compressed Block Layout:**

| Offset | Size     | Type   | Field          | Description                          |
|--------|----------|--------|----------------|--------------------------------------|
| 0      | 1        | uint8  | `node_type`    | 0x02 (LEAF)                          |
| 1      | 2        | uint16 | `entry_count`  | Number of entries in this leaf       |
| 3      | variable | -      | `entries[]`    | Index entries (see below)            |

**After Compressed Block:**

| Offset | Size | Type   | Field       | Description                          |
|--------|------|--------|-------------|--------------------------------------|
| 0      | 8    | uint64 | `next_leaf` | Offset of next leaf (0 = last leaf)  |

**Index Entry Format:**

Each entry in the leaf node:

| Offset | Size     | Type   | Field          | Description                          |
|--------|----------|--------|----------------|--------------------------------------|
| 0      | variable | char   | `folded_word`  | Folded word (null-terminated)        |
| var    | 2        | uint16 | `item_count`   | Number of items for this word        |
| var+2  | variable | -      | `items[]`      | Items (see below)                    |

**Item Format:**

Each item within an entry:

| Offset | Size     | Type   | Field            | Description                          |
|--------|----------|--------|------------------|--------------------------------------|
| 0      | variable | char   | `original_word`  | Original word (null-terminated)      |
| var    | 8        | uint64 | `address`        | Chunk address (see Address Encoding)  |
| var+8  | 4        | uint32 | `data_size`      | Data size in bytes                   |

### Internal Node Format

Internal nodes are used for navigation and contain separators and child pointers.

**Compressed Block Layout:**

| Offset | Size     | Type   | Field          | Description                          |
|--------|----------|--------|----------------|--------------------------------------|
| 0      | 1        | uint8  | `node_type`    | 0x01 (INTERNAL)                      |
| 1      | 2        | uint16 | `child_count`  | Number of children (N)               |
| 3      | 8×N      | uint64 | `children[]`   | File offsets of child nodes          |
| 3+8N   | variable | char   | `keys[]`       | Separator keys (null-terminated)     |

**Key Structure:**

- `child_count` = N (where N ≥ 1)
- Number of keys = N - 1
- Key[i] separates children[i] and children[i+1]
- All keys are folded words (null-terminated, concatenated)

**Navigation Logic:**

For a search key `K`:
1. Binary search through keys to find position
2. If K matches key[i], use children[i+1]
3. If K < key[0], use children[0]
4. If K > key[N-2], use children[N-1]

### B+ Tree Lookup Algorithm

```
function lookup(key):
    node = read_node(root_offset)
    while node.type == INTERNAL:
        child_offset = find_child(node, key)
        node = read_node(child_offset)
    // Now at leaf node
    return binary_search_leaf(node, key)
```

---

## Dictionary Database Table

The dictionary database table is stored at the end of the file (offset specified in main header).

| Offset | Size    | Type   | Field        | Description                          |
|--------|---------|--------|--------------|--------------------------------------|
| 0      | 8       | uint64 | `db_count`   | Number of databases                  |
| 8      | 8×N     | uint64 | `offsets[]`  | File offsets of each DB header       |
| 8+8N   | variable | char   | `names[]`    | Database names (null-terminated)     |

**Total Size:** 8 + (8 × N) + sum of all name lengths + N

Each name is a null-terminated UTF-8 string. Names are stored consecutively without padding.

---

## Address Encoding

Data addresses are 64-bit values encoding both chunk location and offset.

```
┌─────────────────────────────────┬─────────────────────┐
│ Chunk Index (48 bits)           │ Offset (16 bits)    │
│   Max: 281 TB                   │   Max: 64 KB        │
└─────────────────────────────────┴─────────────────────┘
```

**Encoding:**
```c
address = (chunk_index << 16) | offset
```

**Decoding:**
```c
chunk_index = address >> 16
offset = address & 0xFFFF
```

**Constraints:**
- `offset` must be ≤ 65535
- `chunk_index` must be less than chunk count
- `offset + data_size` must not exceed chunk size

---

## String Encoding

All strings in UDX files are:
- **Encoding:** UTF-8
- **Termination:** Null-terminated (`\0`)
- **Comparison:** Case-insensitive (using word folding)

### Word Folding

For indexing, words are "folded" to enable case-insensitive search:
- Convert to lowercase
- Preserves original form in data items
- Folded form is used in B+ tree keys

**Example:**
- Folded word: "hello"
- Original items: "Hello", "HELLO", "hElLo"

All three original forms map to the same folded "hello" entry.

---

## Compression

UDX uses zlib (DEFLATE algorithm) for compression:

**Compressed Sections:**
- Chunks (data blocks)
- B+ tree nodes (both leaf and internal)

**Compression Parameters:**
- Algorithm: DEFLATE
- Level: Z_DEFAULT_COMPRESSION (typically 6)
- Window: Default (15 bits)
- Memory: Default (8 MB)

**Compression Format:**

Each compressed block:
1. `uncompressed_size` (uint32) - size after decompression
2. `compressed_size` (uint32) - size of compressed data
3. `data` (uint8[]) - compressed bytes

---

## Checksum

The database header includes a CRC32 checksum for integrity checking.

**Covered Data:**
- First 40 bytes of database header
- Excludes the checksum field itself

**Algorithm:** CRC32 (ISO 3309, ITU-T V.42, PNG, MPEG-2, etc.)

**Verification:**
1. Read first 40 bytes of database header
2. Calculate CRC32
3. Compare with checksum field at offset 40

---

## Limitations

### Size Limits

| Parameter | Maximum Value      | Notes                          |
|-----------|-------------------|--------------------------------|
| Chunk size | 65536 bytes     | Uncompressed per chunk        |
| Data block | 65536 bytes     | Must fit in single chunk       |
| Chunks per DB | 2^48        | ~281 TB theoretical maximum    |
| Entries per DB | 2^32        | ~4.3 billion unique words      |
| Items per DB | 2^32         | Total data items               |
| Items per entry | 2^16      | Items for a single word        |
| B+ tree height | 255         | Practical limit ~5-6           |

### Validation Constraints

1. **Non-empty database:** Must have at least one entry
2. **Data block size:** Must fit within a single chunk
3. **Address validity:** Chunk index and offset must be within valid ranges
4. **Node decompression:** Compressed nodes must decompress to valid sizes
5. **Checksum integrity:** Database header checksum must match

### File Validation

A valid UDX file must satisfy:
1. Magic bytes: "UDX\0"
2. Supported version: 1.0
3. Valid main header offset (points within file)
4. Each database:
   - Valid checksum
   - Consistent offsets (chunk_table, index_root, index_first_leaf)
   - Non-zero entry count
   - Valid B+ tree structure
5. Chunk table offset points to valid chunk data
6. All chunk addresses resolve to valid data

---

## Example: Reading a UDX File

```c
// 1. Read main header
udx_header header;
fread(&header, sizeof(udx_header), 1, file);

// 2. Validate magic
if (memcmp(header.magic, "UDX\0", 4) != 0) {
    // Invalid file
}

// 3. Read database table
fseek(file, header.db_table_offset, SEEK_SET);
uint64_t db_count;
fread(&db_count, sizeof(uint64_t), 1, file);

// 4. Read each database
for (uint64_t i = 0; i < db_count; i++) {
    uint64_t offset;
    fread(&offset, sizeof(uint64_t), 1, file);

    // Seek to database header and load...
}
```

---

## Revision History

| Version | Date       | Changes                              |
|---------|-----------|--------------------------------------|
| 1.0     | 2026-03-05 | Initial specification                |
