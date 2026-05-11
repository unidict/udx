//
//  udx_reader.h
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#ifndef udx_reader_h
#define udx_reader_h

#include "udx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Opaque Types
// ============================================================

typedef struct udx_reader udx_reader;
typedef struct udx_db udx_db;
typedef struct udx_db_iter udx_db_iter;

// ============================================================
// Reader Open/Close
// ============================================================

/**
 * Open a UDX file for reading
 * @param path File path to open
 * @return Reader handle, or NULL on failure
 */
udx_reader *udx_reader_open(const char *path);

/**
 * Close reader and release resources
 */
void udx_reader_close(udx_reader *reader);

// ============================================================
// Reader Accessors
// ============================================================

uint32_t udx_reader_get_db_count(const udx_reader *reader);
const char *udx_reader_get_db_name(const udx_reader *reader, uint32_t index);
uint64_t udx_reader_get_db_offset(const udx_reader *reader, uint32_t index);

// ============================================================
// Db Open/Close
// ============================================================

/**
 * Open a database by name (NULL = first database)
 */
udx_db *udx_db_open(udx_reader *reader, const char *name);

/**
 * Close a database
 */
void udx_db_close(udx_db *db);

// ============================================================
// Db Accessors
// ============================================================

const char *udx_db_get_name(const udx_db *db);
const uint8_t *udx_db_get_metadata(const udx_db *db, uint32_t *out_size);
uint32_t udx_db_get_key_count(const udx_db *db);
uint32_t udx_db_get_item_count(const udx_db *db);

// ============================================================
// Key Entry Lookup
// ============================================================

/**
 * Look up a key entry by key (case-insensitive)
 * @param out_entry Output: key entry (caller must free with udx_db_key_entry_free)
 * @return UDX_OK on success, UDX_NOT_FOUND if key not found, UDX_ERR_* on error
 */
udx_status_t udx_db_key_entry_lookup(udx_db *db, const char *key, udx_db_key_entry **out_entry);

/**
 * Find all key entries matching a prefix (case-insensitive)
 * @param prefix Prefix to match
 * @param limit Maximum number of results (0 = unlimited)
 * @param out_entries Output: array of key entries (caller must free with udx_db_key_entry_array_free)
 * @return UDX_OK on success, UDX_NOT_FOUND if no matches, UDX_ERR_* on error
 */
udx_status_t udx_db_key_entry_prefix_match(udx_db *db, const char *prefix, size_t limit, udx_db_key_entry_array **out_entries);

// ============================================================
// Value Entry Lookup
// ============================================================

/**
 * Look up a value entry by key (case-insensitive)
 * @param out_entry Output: value entry (caller must free with udx_db_value_entry_free)
 * @return UDX_OK on success, UDX_NOT_FOUND if key not found, UDX_ERR_* on error
 */
udx_status_t udx_db_value_entry_lookup(udx_db *db, const char *key, udx_db_value_entry **out_entry);

/**
 * Load data for all items in a key entry
 * @param out_entry Output: value entry (caller must free with udx_db_value_entry_free)
 * @return UDX_OK on success, UDX_NOT_FOUND if key not found, UDX_ERR_* on error
 */
udx_status_t udx_db_value_entry_load(udx_db *db, const udx_db_key_entry *key_entry, udx_db_value_entry **out_entry);

// ============================================================
// Iterator
// ============================================================

udx_db_iter *udx_db_iter_create(udx_db *db);
void udx_db_iter_destroy(udx_db_iter *iter);

/**
 * Get the next key entry from the iterator
 * @param out_entry Output: pointer to internal entry (valid until next call or destroy)
 * @return UDX_OK on success, UDX_NOT_FOUND if no more entries, UDX_ERR_* on error
 */
udx_status_t udx_db_iter_next(udx_db_iter *iter, const udx_db_key_entry **out_entry);

#ifdef __cplusplus
}
#endif

#endif /* udx_reader_h */
