# udx API Documentation

This document describes the complete API reference for udx.

## Table of Contents

- [Writer APIs](#writer-apis)
  - [File-level Writer API](#file-level-writer-api)
  - [Database Builder API](#database-builder-api)
- [Reader APIs](#reader-apis)
  - [Reader Functions](#reader-functions)
  - [Database Functions](#database-functions)
  - [Key Entry Lookup](#key-entry-lookup)
  - [Value Entry Lookup](#value-entry-lookup)
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
udx_status udx_writer_close(udx_writer *writer);
```

Close the writer and finalize the UDX file.

**Parameters:**
- `writer` - Writer pointer

**Return:**
- `UDX_OK` on success, error code on failure:
  - `UDX_ERR_STATE`: a database builder is still active
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
- Builder pointer, or `NULL` on failure (`UDX_ERR_MEMORY`, `UDX_ERR_STATE`)

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
- Builder pointer, or `NULL` on failure (`UDX_ERR_MEMORY`, `UDX_ERR_STATE`)

**Constraints:**
- If `metadata` is `NULL`, `metadata_size` MUST be 0
- If `metadata_size > 0`, `metadata` MUST NOT be `NULL`

**Notes:**
- Metadata is stored immediately after the database header
- Only one builder can be active at a time per writer

---

#### `udx_db_builder_add_entry()`

```c
udx_status udx_db_builder_add_entry(udx_db_builder *builder,
                                     const char *key,
                                     const uint8_t *value,
                                     uint32_t value_size);
```

Add an entry to the database (simple one-key-to-one-value mapping).

**Parameters:**
- `builder` - Builder pointer
- `key` - Key string (UTF-8, will be folded for case-insensitive lookup)
- `value` - Value bytes
- `value_size` - Size of value in bytes

**Return:**
- `UDX_OK` on success, error code on failure:
  - `UDX_ERR_INVALID_PARAM`: invalid parameter
  - `UDX_ERR_INTERNAL`: chunk writer or keys container failed

**Notes:**
- Multiple entries can be added under the same key
- The original key case is preserved
- For cases where multiple keys reference the same data, use `udx_db_builder_add_value()` + `udx_db_builder_add_key_entry()` instead

---

#### `udx_db_builder_add_value()`

```c
udx_value_address udx_db_builder_add_value(udx_db_builder *builder,
                                            const uint8_t *value,
                                            uint32_t value_size);
```

Store value in chunk storage (returns address for key entries).

**Parameters:**
- `builder` - Builder pointer
- `value` - Value bytes
- `value_size` - Size of value in bytes

**Return:**
- Value address on success, `UDX_INVALID_ADDRESS` on failure

**Notes:**
- This function only writes data to chunk storage
- Use `udx_db_builder_add_key_entry()` to add key references to this address
- Multiple keys can reference the same address (for alternates)

---

#### `udx_db_builder_add_key_entry()`

```c
udx_status udx_db_builder_add_key_entry(udx_db_builder *builder,
                                         const char *key,
                                         udx_value_address value_address,
                                         uint32_t value_size);
```

Add key entry referencing stored value.

**Parameters:**
- `builder` - Builder pointer
- `key` - Key string (UTF-8, will be folded for case-insensitive lookup)
- `value_address` - Address of data in chunk storage (from `udx_db_builder_add_value`)
- `value_size` - Size of value in bytes

**Return:**
- `UDX_OK` on success, error code on failure:
  - `UDX_ERR_INVALID_PARAM`: invalid parameter
  - `UDX_ERR_INTERNAL`: keys container failed

**Notes:**
- This function only adds key-to-address mapping to index
- Use `udx_db_builder_add_value()` first to store data
- Multiple keys can reference the same `value_address`

---

#### `udx_db_builder_finalize()`

```c
udx_status udx_db_builder_finalize(udx_db_builder *builder);
```

Finish building the database.

**Parameters:**
- `builder` - Builder pointer

**Return:**
- `UDX_OK` on success, error code on failure:
  - `UDX_ERR_INVALID_PARAM`: invalid parameter or empty database (no entries added)
  - `UDX_ERR_INTERNAL`: chunk writer or B+ tree build failed
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

### Key Entry Lookup

Key entry lookup functions return entries with addresses only, without loading the actual data. This is faster when you only need to check existence or get metadata.

#### `udx_db_key_entry_lookup()`

```c
udx_status udx_db_key_entry_lookup(udx_db *db, const char *key, udx_db_key_entry **out_entry);
```

Look up a single key in database (index only, no data loaded).

**Parameters:**
- `db` - Database pointer
- `key` - Key to look up
- `out_entry` - Output: key entry (caller must free with `udx_db_key_entry_free`)

**Return:**
- `UDX_OK` on success, `UDX_NOT_FOUND` if key not found, `UDX_ERR_*` on error

**Notes:**
- This is faster than `udx_db_value_entry_lookup()` as it doesn't load data
- Use `udx_db_value_entry_load()` to load data for specific key entries

---

#### `udx_db_key_entry_prefix_match()`

```c
udx_status udx_db_key_entry_prefix_match(udx_db *db, const char *prefix, size_t limit, udx_db_key_entry_array **out_entries);
```

Prefix match in database (index only, no data loaded).

**Parameters:**
- `db` - Database pointer
- `prefix` - Prefix to match
- `limit` - Maximum number of results (0 = unlimited)
- `out_entries` - Output: array of key entries (caller must free with `udx_db_key_entry_array_free`)

**Return:**
- `UDX_OK` on success, `UDX_NOT_FOUND` if no matches, `UDX_ERR_*` on error

**Notes:**
- This is useful for autocomplete/suggestion features
- Returns entries with addresses only, use `udx_db_value_entry_load()` to load data

---

### Value Entry Lookup

Value entry lookup functions return entries with all data loaded.

#### `udx_db_value_entry_lookup()`

```c
udx_status udx_db_value_entry_lookup(udx_db *db, const char *key, udx_db_value_entry **out_entry);
```

Look up a single key in database (with data loaded).

**Parameters:**
- `db` - Database pointer
- `key` - Key to look up
- `out_entry` - Output: value entry (caller must free with `udx_db_value_entry_free`)

**Return:**
- `UDX_OK` on success, `UDX_NOT_FOUND` if key not found, `UDX_ERR_*` on error

---

#### `udx_db_value_entry_load()`

```c
udx_status udx_db_value_entry_load(udx_db *db, const udx_db_key_entry *key_entry, udx_db_value_entry **out_entry);
```

Load data for all items in a key entry.

**Parameters:**
- `db` - Database pointer
- `key_entry` - Key entry (with addresses) returned from key entry lookup
- `out_entry` - Output: value entry (caller must free with `udx_db_value_entry_free`)

**Return:**
- `UDX_OK` on success, `UDX_NOT_FOUND` if key not found, `UDX_ERR_*` on error

**Notes:**
- This function loads data for all items in the key entry
- The `key_entry` is still valid after this call (ownership is not transferred)

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
udx_status udx_db_iter_next(udx_db_iter *iter, const udx_db_key_entry **out_entry);
```

Get the next key entry from the iterator.

**Parameters:**
- `iter` - Iterator pointer
- `out_entry` - Output: pointer to internal entry (valid until next call or destroy)

**Return:**
- `UDX_OK` on success, `UDX_NOT_FOUND` if no more entries, `UDX_ERR_*` on error

**Notes:**
- The returned pointer points to internal memory managed by the iterator
- Do NOT free the returned entry or its contents directly
- The data is valid only until the next call to `udx_db_iter_next()` or until `udx_db_iter_destroy()` is called
- If you need to persist the data, make a deep copy before the next iteration

---

## Data Types

See `udx_types.h` for the complete definition of data types used in the API, including:

- `udx_db_key_entry` - Key entry with addresses (no data)
- `udx_db_value_entry` - Value entry with data loaded
- `udx_db_key_entry_array` - Array of key entries
- `udx_value_address` - Address encoding (chunk index + offset)

Memory management functions:
- `udx_db_key_entry_free()` - Free a key entry
- `udx_db_value_entry_free()` - Free a value entry
- `udx_db_key_entry_array_free()` - Free a key entry array
