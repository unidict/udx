//
//  udx_reader.h
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#ifndef udx_reader_h
#define udx_reader_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "udx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque types
typedef struct udx_reader udx_reader;
typedef struct udx_db udx_db;

// ============================================================
// Reader
// ============================================================

udx_reader *udx_reader_open(const char *path);
void udx_reader_close(udx_reader *reader);

/**
 * Get the number of dbs in the reader
 */
uint32_t udx_reader_get_db_count(const udx_reader *reader);

/**
 * Get the name of a db by index
 */
const char *udx_reader_get_db_name(const udx_reader *reader, uint32_t index);

/**
 * Get the file offset of a db by index
 */
uint64_t udx_reader_get_db_offset(const udx_reader *reader, uint32_t index);

// ============================================================
// Db
// ============================================================

/**
 * Open a db by name from reader
 * @param reader Reader pointer
 * @param name Db name (NULL for first/default db)
 * @return Db pointer, or NULL on failure (caller must udx_db_close it)
 */
udx_db *udx_db_open(udx_reader *reader, const char *name);

/**
 * Close a db (does not close the reader)
 * @param db Db pointer
 */
void udx_db_close(udx_db *db);

/**
 * Get the db name
 */
const char *udx_db_get_name(const udx_db *db);

/**
 * Get the metadata
 * @param db Db pointer
 * @param out_size If not NULL, receives the metadata size
 * @return Metadata pointer (owned by db, do not free), or NULL if no metadata
 */
const uint8_t *udx_db_get_metadata(const udx_db *db, uint32_t *out_size);

/**
 * Get the number of unique keys
 */
uint32_t udx_db_get_key_count(const udx_db *db);

/**
 * Get the total number of items across all keys
 */
uint32_t udx_db_get_item_count(const udx_db *db);

/**
 * Get the B+ tree height
 */
uint32_t udx_db_get_index_bptree_height(const udx_db *db);

// ============================================================
// Key Entry Lookup (returns key entries with addresses only, no data loaded)
// ============================================================

/**
 * Look up a single key in db (index only, no data loaded)
 * @param db Db pointer
 * @param key Key to look up
 * @return Index entry pointer (caller must free with udx_key_entry_free), or NULL if not found
 *
 * @note This is faster than udx_db_lookup as it doesn't load data
 * @note Use udx_db_lookup_by_key_entry() to load data for specific items
 */
udx_db_key_entry *udx_db_key_entry_lookup(udx_db *db, const char *key);

/**
 * Prefix match in db (index only, no data loaded)
 * @param db Db pointer
 * @param prefix Prefix to match
 * @param limit Maximum number of results (0 = unlimited)
 * @return Array of index entries (caller must free with udx_db_key_entry_array_free_contents)
 *
 * @note This is useful for autocomplete/suggestion features
 * @note Returns entries with addresses only, use udx_db_lookup_by_key_entry() to load data
 */
udx_db_key_entry_array udx_db_key_entry_prefix_match(udx_db *db, const char *prefix, size_t limit);

// ============================================================
// Full Data Lookup (returns entries with data loaded)
// ============================================================

/**
 * Look up a single key in db (with data loaded)
 * @param db Db pointer
 * @param key Key to look up
 * @return Db entry pointer (caller must free with udx_value_entry_free), or NULL if not found
 */
udx_db_value_entry *udx_db_lookup(udx_db *db, const char *key);

/**
 * Look up by key entry (with data loaded)
 * @param db Db pointer
 * @param key_entry Key entry (with addresses) returned from index lookup
 * @return Db entry with data loaded (caller must free with udx_value_entry_free), or NULL on error
 *
 * @note This function loads data for all items in the key entry
 * @note The key_entry is still valid after this call (ownership is not transferred)
 */
udx_db_value_entry *udx_db_lookup_by_key_entry(udx_db *db, const udx_db_key_entry *key_entry);

// ============================================================
// Iterator
// ============================================================

/**
 * Iterator type
 */
typedef struct udx_db_iter udx_db_iter;

/**
 * Create an iterator for a db
 * @param db Db pointer
 * @return Iterator pointer, or NULL on failure
 */
udx_db_iter *udx_db_iter_create(udx_db *db);

/**
 * Destroy an iterator
 * @param iter Iterator pointer
 */
void udx_db_iter_destroy(udx_db_iter *iter);

/**
 * Get the next entry
 * @param iter Iterator pointer
 * @return Db entry pointer, or NULL when traversal is complete
 *
 * @note The returned pointer points to internal memory managed by the iterator.
 *       Do NOT free the returned entry or its contents directly.
 *       The data is valid only until the next call to udx_db_iter_next()
 *       or until udx_db_iter_destroy() is called.
 *       If you need to persist the data, make a deep copy before the next iteration.
 */
const udx_db_value_entry *udx_db_iter_next(udx_db_iter *iter);

#ifdef __cplusplus
}
#endif

#endif /* udx_reader_h */
