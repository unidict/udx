# udx API Documentation

This document describes the complete API reference for udx.

## Table of Contents

- [Writer APIs](#writer-apis)
  - [File-level Writer API](#file-level-writer-api)
  - [Database Builder API](#database-builder-api)
- [Reader APIs](#reader-apis)
  - [Reader Functions](#reader-functions)
  - [Database Functions](#database-functions)
  - [Index Lookup](#index-lookup)
  - [Full Data Lookup](#full-data-lookup)
  - [Iterator](#iterator)
- [Data Types](#data-types)

---

## Writer APIs

### File-level Writer API

#### `udx_writer_open()`

```c
udx_writer *udx_writer_open(const char *output_path);
```

Open a new UDX file for writing.

**Parameters:**
- `output_path` - Path to the output file (will be created or overwritten)

**Return:**
- Writer pointer, or `NULL` on failure (`UDX_ERR_MEMORY`)

**Notes:**
- Only one database can be built at a time per writer
- Call `udx_writer_close()` to finalize and close the file

---

#### `udx_writer_close()`

```c
udx_error_t udx_writer_close(udx_writer *writer);
```

Close the writer and finalize the UDX file.

**Parameters:**
- `writer` - Writer pointer

**Return:**
- `UDX_OK` on success, error code on failure:
  - `UDX_ERR_ACTIVE_DB`: a database builder is still active
  - `UDX_ERR_IO`: file I/O error

**Notes:**
- All builders must be finished before closing

---

### Database Builder API

#### `udx_db_builder_create()`

```c
udx_db_builder *udx_db_builder_create(udx_writer *writer, const char *name);
```

Create a database builder (without metadata).

**Parameters:**
- `writer` - Writer pointer
- `name` - Database name (must be unique within the file)

**Return:**
- Builder pointer, or `NULL` on failure (`UDX_ERR_MEMORY`, `UDX_ERR_ACTIVE_DB`, `UDX_ERR_DUPLICATE_NAME`)

**Notes:**
- This is equivalent to calling `udx_db_builder_create_with_metadata` with `metadata=NULL` and `metadata_size=0`

---

#### `udx_db_builder_create_with_metadata()`

```c
udx_db_builder *udx_db_builder_create_with_metadata(udx_writer *writer,
                                                    const char *name,
                                                    const uint8_t *metadata,
                                                    size_t metadata_size);
```

Create a database builder with metadata.

**Parameters:**
- `writer` - Writer pointer
- `name` - Database name (must be unique within the file)
- `metadata` - Metadata bytes (can be `NULL` if `metadata_size` is 0)
- `metadata_size` - Size of metadata in bytes (must be 0 if `metadata` is `NULL`)

**Return:**
- Builder pointer, or `NULL` on failure (`UDX_ERR_MEMORY`, `UDX_ERR_ACTIVE_DB`, `UDX_ERR_DUPLICATE_NAME`, `UDX_ERR_METADATA`)

**Constraints:**
- If `metadata` is `NULL`, `metadata_size` MUST be 0
- If `metadata_size > 0`, `metadata` MUST NOT be `NULL`

**Notes:**
- Metadata is stored immediately after the database header
- Only one builder can be active at a time per writer

---

#### `udx_db_builder_add_entry()`

```c
udx_error_t udx_db_builder_add_entry(udx_db_builder *builder,
                                     const char *key,
                                     const uint8_t *data,
                                     size_t data_size);
```

Add an entry to the database.

**Parameters:**
- `builder` - Builder pointer
- `key` - Key string (UTF-8, will be folded for case-insensitive lookup)
- `data` - Data bytes
- `data_size` - Size of data in bytes (must not exceed `UINT32_MAX`)

**Return:**
- `UDX_OK` on success, error code on failure:
  - `UDX_ERR_INVALID_PARAM`: invalid parameter
  - `UDX_ERR_CHUNK`: chunk writer failed
  - `UDX_ERR_KEYS`: keys container failed

**Notes:**
- Multiple entries can be added under the same key
- The original key case is preserved

---

#### `udx_db_builder_finalize()`

```c
udx_error_t udx_db_builder_finalize(udx_db_builder *builder);
```

Finish building the database.

**Parameters:**
- `builder` - Builder pointer

**Return:**
- `UDX_OK` on success, error code on failure:
  - `UDX_ERR_INVALID_PARAM`: invalid parameter or empty database (no entries added)
  - `UDX_ERR_CHUNK`: chunk writer failed
  - `UDX_ERR_BPTREE`: B+ tree build failed
  - `UDX_ERR_HEADER`: header write failed

**Notes:**
- After this call, the builder is freed and must not be used again
- Empty databases (with no entries) are not allowed and will return `UDX_ERR_INVALID_PARAM`

---

## Reader APIs

### Reader Functions

#### `udx_reader_open()`

```c
udx_reader *udx_reader_open(const char *path);
```

Open an existing UDX file.

**Parameters:**
- `path` - Path to the UDX file

**Return:**
- Reader pointer, or `NULL` on failure

---

#### `udx_reader_close()`

```c
void udx_reader_close(udx_reader *reader);
```

Close the reader and release resources.

**Parameters:**
- `reader` - Reader pointer

---

#### `udx_reader_get_db_count()`

```c
uint32_t udx_reader_get_db_count(const udx_reader *reader);
```

Get the number of databases in the reader.

**Parameters:**
- `reader` - Reader pointer

**Return:**
- Number of databases

---

#### `udx_reader_get_db_name()`

```c
const char *udx_reader_get_db_name(const udx_reader *reader, uint32_t index);
```

Get the name of a database by index.

**Parameters:**
- `reader` - Reader pointer
- `index` - Database index (0-based)

**Return:**
- Database name string, or `NULL` if index is invalid

---

#### `udx_reader_get_db_offset()`

```c
uint64_t udx_reader_get_db_offset(const udx_reader *reader, uint32_t index);
```

Get the file offset of a database by index.

**Parameters:**
- `reader` - Reader pointer
- `index` - Database index (0-based)

**Return:**
- File offset in bytes

---

### Database Functions

#### `udx_db_open()`

```c
udx_db *udx_db_open(udx_reader *reader, const char *name);
```

Open a database by name from reader.

**Parameters:**
- `reader` - Reader pointer
- `name` - Database name (`NULL` for first/default database)

**Return:**
- Database pointer, or `NULL` on failure (caller must `udx_db_close` it)

---

#### `udx_db_close()`

```c
void udx_db_close(udx_db *db);
```

Close a database (does not close the reader).

**Parameters:**
- `db` - Database pointer

---

#### `udx_db_get_name()`

```c
const char *udx_db_get_name(const udx_db *db);
```

Get the database name.

**Parameters:**
- `db` - Database pointer

**Return:**
- Database name string

---

#### `udx_db_get_metadata()`

```c
const uint8_t *udx_db_get_metadata(const udx_db *db, uint32_t *out_size);
```

Get the metadata.

**Parameters:**
- `db` - Database pointer
- `out_size` - If not `NULL`, receives the metadata size

**Return:**
- Metadata pointer (owned by db, do not free), or `NULL` if no metadata

---

#### `udx_db_get_key_count()`

```c
uint32_t udx_db_get_key_count(const udx_db *db);
```

Get the number of unique keys in the database.

**Parameters:**
- `db` - Database pointer

**Return:**
- Number of unique keys in the database

---

#### `udx_db_get_item_count()`

```c
uint32_t udx_db_get_item_count(const udx_db *db);
```

Get the total number of items across all keys.

**Parameters:**
- `db` - Database pointer

**Return:**
- Total number of data items

---

#### `udx_db_get_index_bptree_height()`

```c
uint32_t udx_db_get_index_bptree_height(const udx_db *db);
```

Get the B+ tree height.

**Parameters:**
- `db` - Database pointer

**Return:**
- B+ tree height (for debugging/analysis)

---

### Index Lookup

Index lookup functions return entries with addresses only, without loading the actual data. This is faster when you only need to check existence or get metadata.

#### `udx_db_index_lookup()`

```c
udx_db_key_entry *udx_db_index_lookup(udx_db *db, const char *key);
```

Look up a single key in database (index only, no data loaded).

**Parameters:**
- `db` - Database pointer
- `key` - Key to look up

**Return:**
- Index entry pointer (caller must free with `udx_key_entry_free`), or `NULL` if not found

**Notes:**
- This is faster than `udx_db_lookup` as it doesn't load data
- Use `udx_db_load_data()` to load data for specific items

---

#### `udx_db_index_prefix_match()`

```c
udx_key_entry_array udx_db_index_prefix_match(udx_db *db, const char *prefix, size_t max_results);
```

Prefix match in database (index only, no data loaded).

**Parameters:**
- `db` - Database pointer
- `prefix` - Prefix to match
- `max_results` - Maximum number of results (0 = unlimited)

**Return:**
- Array of index entries (caller must free with `udx_key_entry_array_free_contents`)

**Notes:**
- This is useful for autocomplete/suggestion features
- Returns entries with addresses only, use `udx_db_load_data()` to load data

---

#### `udx_db_load_data()`

```c
udx_db_value_entry *udx_db_load_data(udx_db *db, const udx_db_key_entry *index_entry);
```

Load data for an index entry.

**Parameters:**
- `db` - Database pointer
- `index_entry` - Index entry (with addresses) returned from index lookup

**Return:**
- Database entry with data loaded (caller must free with `udx_value_entry_free`), or `NULL` on error

**Notes:**
- This function loads data for all items in the index entry
- The `index_entry` is still valid after this call (ownership is not transferred)

---

### Full Data Lookup

Full data lookup functions return entries with all data loaded.

#### `udx_db_lookup()`

```c
udx_db_value_entry *udx_db_lookup(udx_db *db, const char *key);
```

Look up a single key in database (with data loaded).

**Parameters:**
- `db` - Database pointer
- `key` - Key to look up

**Return:**
- Database entry pointer (caller must free with `udx_value_entry_free`), or `NULL` if not found

---

### Iterator

#### `udx_db_iter`

```c
typedef struct udx_db_iter udx_db_iter;
```

Iterator type for traversing all entries in a database.

---

#### `udx_db_iter_create()`

```c
udx_db_iter *udx_db_iter_create(udx_db *db);
```

Create an iterator for a database.

**Parameters:**
- `db` - Database pointer

**Return:**
- Iterator pointer, or `NULL` on failure

---

#### `udx_db_iter_destroy()`

```c
void udx_db_iter_destroy(udx_db_iter *iter);
```

Destroy an iterator.

**Parameters:**
- `iter` - Iterator pointer

---

#### `udx_db_iter_next()`

```c
const udx_db_value_entry *udx_db_iter_next(udx_db_iter *iter);
```

Get the next entry.

**Parameters:**
- `iter` - Iterator pointer

**Return:**
- Database entry pointer, or `NULL` when traversal is complete

**Notes:**
- The returned pointer points to internal memory managed by the iterator
- Do NOT free the returned entry or its contents directly
- The data is valid only until the next call to `udx_db_iter_next()` or until `udx_db_iter_destroy()` is called
- If you need to persist the data, make a deep copy before the next iteration

---

## Data Types

See `udx_types.h` for the complete definition of data types used in the API, including:

- `udx_db_key_entry` - Index entry with addresses (no data)
- `udx_db_value_entry` - Database entry with data loaded
- `udx_key_entry_array` - Array of index entries
- `udx_value_address` - Address encoding (chunk index + offset)

Memory management functions:
- `udx_key_entry_free()` - Free an index entry
- `udx_value_entry_free()` - Free a database entry
- `udx_key_entry_array_free_contents()` - Free array contents
