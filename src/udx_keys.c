//
//  udx_keys.c
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#include "udx_keys.h"
#include "udx_utils.h"
#include "udx_btree.h"
#include <stdlib.h>
#include <string.h>


/**
 * Ordered key container
 */
struct udx_keys{
    struct udx_btree *tree;         // btree container
    uint32_t total_items;          // Total number of items (across all keys)
};

// ============================================================
// Helper Functions
// ============================================================

/**
 * btree comparison function (compares folded keys)
 */
static int entry_compare(const void *a, const void *b, void *udata) {
    (void)udata;
    const udx_db_key_entry *ea = (const udx_db_key_entry *)a;
    const udx_db_key_entry *eb = (const udx_db_key_entry *)b;
    return strcmp(ea->key, eb->key);  // Compare folded keys
}

/**
 * btree item free callback
 */
static void entry_free_callback(const void *item, void *udata) {
    (void)udata;
    udx_db_key_entry *entry = (udx_db_key_entry *)item;
    udx_db_key_entry_free_contents(entry);
}

// ============================================================
// Public API Implementation
// ============================================================

udx_keys *udx_keys_create(void) {
    udx_keys *keys = (udx_keys *)calloc(1, sizeof(udx_keys));
    if (keys == NULL) {
        return NULL;
    }

    keys->tree = udx_btree_new(sizeof(udx_db_key_entry), 0, entry_compare, NULL);
    if (keys->tree == NULL) {
        free(keys);
        return NULL;
    }

    // Zero-copy: only set free callback, no clone callback
    // btree will directly store the pointer we give it
    udx_btree_set_item_callbacks(keys->tree, NULL, entry_free_callback);

    return keys;
}

void udx_keys_destroy(udx_keys *keys) {
    if (keys == NULL) {
        return;
    }

    if (keys->tree) {
        udx_btree_free(keys->tree);
    }

    free(keys);
}

bool udx_keys_add(udx_keys *keys,
                          const char *key,
                          udx_value_address value_address,
                          uint32_t data_size) {
    if (keys == NULL || key == NULL) {
        return false;
    }

    // Check for overflow before incrementing
    if (keys->total_items == UINT32_MAX) {
        return false;
    }

    // Fold key for case-insensitive lookup
    char *folded = udx_fold_string(key);
    if (folded == NULL) {
        return false;
    }

    // Check if already exists (by folded key)
    udx_db_key_entry search_key = { .key = folded };
    udx_db_key_entry *existing = (udx_db_key_entry *)udx_btree_get_mut(keys->tree, &search_key);

    if (existing) {
        // Already exists, append item
        udx_key_entry_item item;
        item.original_key = udx_str_dup(key);
        if (item.original_key == NULL) {
            free(folded);
            return false;
        }
        item.value_address = value_address;
        item.data_size = data_size;

        if (!udx_db_key_entry_item_array_push(&existing->items, item)) {
            free(item.original_key);
            free(folded);
            return false;
        }
        free(folded);  // Search key no longer needed
    } else {
        // Does not exist, create new entry on stack
        // btree_set will memcpy (shallow copy) the struct into internal storage
        udx_db_key_entry new_entry;
        new_entry.key = folded;  // ownership transferred to btree on success
        udx_db_key_entry_item_array_init(&new_entry.items);

        // Create first item with original key
        udx_key_entry_item item;
        item.original_key = udx_str_dup(key);
        if (item.original_key == NULL) {
            free(folded);
            return false;
        }
        item.value_address = value_address;
        item.data_size = data_size;
        if (!udx_db_key_entry_item_array_push(&new_entry.items, item)) {
            free(item.original_key);
            free(folded);
            return false;
        }

        // Insert into btree (shallow copy, no clone callback)
        udx_btree_set(keys->tree, &new_entry);

        if (udx_btree_oom(keys->tree)) {
            // btree did not take ownership, clean up everything
            udx_db_key_entry_free_contents(&new_entry);
            return false;
        }
        // Success: btree now owns the pointers (key, items.data)
    }

    keys->total_items++;
    return true;
}

size_t udx_keys_count(const udx_keys *keys) {
    if (keys == NULL || keys->tree == NULL) {
        return 0;
    }
    return udx_btree_count(keys->tree);
}

size_t udx_keys_item_count(const udx_keys *keys) {
    if (keys == NULL) {
        return 0;
    }
    return keys->total_items;
}

// ============================================================
// Iterator Implementation
// ============================================================

struct udx_keys_iter {
    struct udx_btree_iter *btree_iter;
    bool started;  // true after the first successful next()
};

udx_keys_iter *udx_keys_iter_create(udx_keys *keys) {
    if (keys == NULL || keys->tree == NULL) return NULL;

    udx_keys_iter *iter = (udx_keys_iter *)calloc(1, sizeof(udx_keys_iter));
    if (iter == NULL) return NULL;

    iter->btree_iter = udx_btree_iter_new(keys->tree);
    if (iter->btree_iter == NULL) {
        free(iter);
        return NULL;
    }

    // started is already false due to calloc
    return iter;
}

void udx_keys_iter_destroy(udx_keys_iter *iter) {
    if (iter == NULL) return;
    if (iter->btree_iter) udx_btree_iter_free(iter->btree_iter);
    free(iter);
}

const udx_db_key_entry *udx_keys_iter_next(udx_keys_iter *iter) {
    if (iter == NULL || iter->btree_iter == NULL) return NULL;

    if (udx_btree_iter_next(iter->btree_iter)) {
        iter->started = true;
        return (const udx_db_key_entry *)udx_btree_iter_item(iter->btree_iter);
    }
    return NULL;
}

const udx_db_key_entry *udx_keys_iter_peek(udx_keys_iter *iter) {
    if (iter == NULL || iter->btree_iter == NULL || !iter->started) return NULL;
    return (const udx_db_key_entry *)udx_btree_iter_item(iter->btree_iter);
}
