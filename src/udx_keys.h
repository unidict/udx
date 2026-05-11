//
//  udx_keys.h
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#ifndef udx_keys_h
#define udx_keys_h

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "udx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Ordered Key Container
// ============================================================
typedef struct udx_keys udx_keys;

// ============================================================
// Creation and Destruction
// ============================================================

/**
 * Create an ordered key container
 * @return Container pointer, or NULL on failure
 */
udx_keys *udx_keys_create(void);

/**
 * Destroy an ordered key container
 * @param keys Container pointer
 */
void udx_keys_destroy(udx_keys *keys);

// ============================================================
// Key Operations
// ============================================================

/**
 * Add a key
 * @param keys Container pointer
 * @param key Key (UTF-8)
 * @param value_address Value address
 * @param value_size Value size in bytes
 * @return true on success, false on failure
 *
 * @note The key will be folded (lowercased, etc.) for sorting and lookup
 * @note Multiple addresses can be stored under the same key
 */
bool udx_keys_add(udx_keys *keys,
                          const char *key,
                          udx_value_address value_address,
                          uint32_t value_size);

/**
 * Get the number of unique keys
 * @param keys Container pointer
 * @return Number of unique keys
 */
size_t udx_keys_count(const udx_keys *keys);

/**
 * Get the total number of items
 * @param keys Container pointer
 * @return Total number of items across all keys
 */
size_t udx_keys_item_count(const udx_keys *keys);

// ============================================================
// Iterator
// ============================================================

/**
 * Iterator type
 */
typedef struct udx_keys_iter udx_keys_iter;

/**
 * Create an iterator
 * @param keys Container pointer
 * @return Iterator pointer, or NULL on failure
 */
udx_keys_iter *udx_keys_iter_create(udx_keys *keys);

/**
 * Destroy an iterator
 * @param iter Iterator pointer
 */
void udx_keys_iter_destroy(udx_keys_iter *iter);

/**
 * Get the next entry
 * @param iter Iterator pointer
 * @return Entry pointer, or NULL when traversal is complete
 */
const udx_db_key_entry *udx_keys_iter_next(udx_keys_iter *iter);

/**
 * Peek at the current entry (without advancing the iterator)
 * @param iter Iterator pointer
 * @return Entry pointer, or NULL when traversal is complete
 */
const udx_db_key_entry *udx_keys_iter_peek(udx_keys_iter *iter);

#ifdef __cplusplus
}
#endif

#endif /* udx_keys_h */
