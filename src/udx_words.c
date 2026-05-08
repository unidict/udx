//
//  udx_words.c
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#include "udx_words.h"
#include "udx_utils.h"
#include "udx_btree.h"
#include <stdlib.h>
#include <string.h>


/**
 * Ordered word container
 */
struct udx_words{
    struct udx_btree *tree;         // btree container
    uint32_t total_items;          // Total number of items (across all words)
};

// ============================================================
// Helper Functions
// ============================================================

/**
 * btree comparison function (compares folded words)
 */
static int entry_compare(const void *a, const void *b, void *udata) {
    (void)udata;
    const udx_index_entry *ea = (const udx_index_entry *)a;
    const udx_index_entry *eb = (const udx_index_entry *)b;
    return strcmp(ea->word, eb->word);  // Compare folded words
}

/**
 * btree item free callback
 */
static void entry_free_callback(const void *item, void *udata) {
    (void)udata;
    udx_index_entry *entry = (udx_index_entry *)item;
    udx_entry_free_contents(entry);
}

// ============================================================
// Public API Implementation
// ============================================================

udx_words *udx_words_create(void) {
    udx_words *words = (udx_words *)calloc(1, sizeof(udx_words));
    if (words == NULL) {
        return NULL;
    }

    words->tree = udx_btree_new(sizeof(udx_index_entry), 0, entry_compare, NULL);
    if (words->tree == NULL) {
        free(words);
        return NULL;
    }

    // Zero-copy: only set free callback, no clone callback
    // btree will directly store the pointer we give it
    udx_btree_set_item_callbacks(words->tree, NULL, entry_free_callback);

    return words;
}

void udx_words_destroy(udx_words *words) {
    if (words == NULL) {
        return;
    }

    if (words->tree) {
        udx_btree_free(words->tree);
    }

    free(words);
}

bool udx_words_add(udx_words *words,
                          const char *word,
                          udx_chunk_address data_address,
                          uint32_t data_size) {
    if (words == NULL || word == NULL) {
        return false;
    }

    // Check for overflow before incrementing
    if (words->total_items == UINT32_MAX) {
        return false;
    }

    // Fold word for key (case-insensitive lookup)
    char *folded = udx_fold_string(word);
    if (folded == NULL) {
        return false;
    }

    // Check if already exists (by folded word)
    udx_index_entry search_key = { .word = folded };
    udx_index_entry *existing = (udx_index_entry *)udx_btree_get_mut(words->tree, &search_key);

    if (existing) {
        // Already exists, append item
        udx_index_entry_item item;
        item.original_word = udx_str_dup(word);
        if (item.original_word == NULL) {
            free(folded);
            return false;
        }
        item.data_address = data_address;
        item.data_size = data_size;

        if (!udx_index_entry_item_array_push(&existing->items, item)) {
            free(item.original_word);
            free(folded);
            return false;
        }
        free(folded);  // Search key no longer needed
    } else {
        // Does not exist, create new entry on stack
        // btree_set will memcpy (shallow copy) the struct into internal storage
        udx_index_entry new_entry;
        new_entry.word = folded;  // ownership transferred to btree on success
        udx_index_entry_item_array_init(&new_entry.items);

        // Create first item with original word
        udx_index_entry_item item;
        item.original_word = udx_str_dup(word);
        if (item.original_word == NULL) {
            free(folded);
            return false;
        }
        item.data_address = data_address;
        item.data_size = data_size;
        if (!udx_index_entry_item_array_push(&new_entry.items, item)) {
            free(item.original_word);
            free(folded);
            return false;
        }

        // Insert into btree (shallow copy, no clone callback)
        udx_btree_set(words->tree, &new_entry);

        if (udx_btree_oom(words->tree)) {
            // btree did not take ownership, clean up everything
            udx_entry_free_contents(&new_entry);
            return false;
        }
        // Success: btree now owns the pointers (word, items.data)
    }

    words->total_items++;
    return true;
}

size_t udx_words_count(const udx_words *words) {
    if (words == NULL || words->tree == NULL) {
        return 0;
    }
    return udx_btree_count(words->tree);
}

size_t udx_words_item_count(const udx_words *words) {
    if (words == NULL) {
        return 0;
    }
    return words->total_items;
}

// ============================================================
// Iterator Implementation
// ============================================================

struct udx_words_iter {
    struct udx_btree_iter *btree_iter;
    bool started;  // true after the first successful next()
};

udx_words_iter *udx_words_iter_create(udx_words *words) {
    if (words == NULL || words->tree == NULL) return NULL;

    udx_words_iter *iter = (udx_words_iter *)calloc(1, sizeof(udx_words_iter));
    if (iter == NULL) return NULL;

    iter->btree_iter = udx_btree_iter_new(words->tree);
    if (iter->btree_iter == NULL) {
        free(iter);
        return NULL;
    }

    // started is already false due to calloc
    return iter;
}

void udx_words_iter_destroy(udx_words_iter *iter) {
    if (iter == NULL) return;
    if (iter->btree_iter) udx_btree_iter_free(iter->btree_iter);
    free(iter);
}

const udx_index_entry *udx_words_iter_next(udx_words_iter *iter) {
    if (iter == NULL || iter->btree_iter == NULL) return NULL;

    if (udx_btree_iter_next(iter->btree_iter)) {
        iter->started = true;
        return (const udx_index_entry *)udx_btree_iter_item(iter->btree_iter);
    }
    return NULL;
}

const udx_index_entry *udx_words_iter_peek(udx_words_iter *iter) {
    if (iter == NULL || iter->btree_iter == NULL || !iter->started) return NULL;
    return (const udx_index_entry *)udx_btree_iter_item(iter->btree_iter);
}
