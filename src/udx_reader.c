//
//  ud_reader.c
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#include "udx_reader.h"
#include "udx_chunk.h"
#include "udx_utils.h"
#include <stdio.h>
#include <zlib.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Opaque Struct Definitions
// ============================================================

struct udx_reader {
    FILE *file;
    udx_header header;
    uint64_t *db_offsets;
    char **db_names;
    uint16_t db_count;
};

struct udx_db {
    udx_reader *reader;
    char *name;
    udx_db_header header;
    udx_chunk_reader *chunk_reader;
    udx_index_node *root_node;
    uint8_t *metadata;
    uint32_t metadata_size;
};

// ============================================================
// Constants
// ============================================================

#define UDX_INDEX_NODE_MAX_SIZE (1024 * 1024)  // Max 1MB for index nodes

// ============================================================
// B+ Tree Node Structure (zero-copy parsing)
// ============================================================

/**
 * Internal node view (zero-copy, points into node->data)
 * Format: [node_type:u8] [child_count:u16] [children:u64 × N] [keys\0...]
 */
typedef struct {
    const uint64_t *children;        // Pointer to children array (in data)
    const char *keys_data;           // Pointer to keys area (in data)
    uint16_t child_count;            // Number of children
} ud_index_internal_node_info;

/**
 * Leaf node view (metadata only, entry_offsets needs allocation)
 * Format: [node_type:u8] [entry_count:u16] [entries...]
 */
typedef struct {
    uint16_t entry_count;            // Number of entries
} ud_index_leaf_node_info;

// ============================================================
// Node Type Helper Functions
// ============================================================

/**
 * Check if node is an internal node
 */
static inline bool ud_index_node_is_internal(const udx_index_node *node) {
    return node->type == UDX_INDEX_NODE_TYPE_INTERNAL;
}






// ============================================================
// Helper Functions
// ============================================================

/**
 * Read and decompress a node
 * Only reads the compressed node data, does not read next_leaf
 */
static udx_index_node *read_index_node(udx_db *db, uint64_t offset) {
    if (offset == UDX_INVALID_NODE_OFFSET) return NULL;
    
    if (udx_fseek(db->reader->file, offset, SEEK_SET) != 0) return NULL;

    uint32_t uncompressed_size, compressed_size;
    if (fread(&uncompressed_size, sizeof(uint32_t), 1, db->reader->file) != 1 ||
        fread(&compressed_size, sizeof(uint32_t), 1, db->reader->file) != 1) {
        return NULL;
    }

    // Sanity check: reject obviously invalid sizes (max 1MB for index nodes)
    if (uncompressed_size == 0 || uncompressed_size > UDX_INDEX_NODE_MAX_SIZE ||
        compressed_size == 0 || compressed_size > UDX_INDEX_NODE_MAX_SIZE) {
        return NULL;
    }

    uint8_t *compressed = (uint8_t *)malloc(compressed_size);
    if (compressed == NULL) return NULL;

    if (fread(compressed, 1, compressed_size, db->reader->file) != compressed_size) {
        free(compressed);
        return NULL;
    }

    uint8_t *data = (uint8_t *)malloc(uncompressed_size);
    if (data == NULL) {
        free(compressed);
        return NULL;
    }

    uLong dest_len = uncompressed_size;
    int ret = uncompress(data, &dest_len, compressed, compressed_size);
    free(compressed);

    if (ret != Z_OK || dest_len != uncompressed_size) {
        free(data);
        return NULL;
    }

    // Create node structure
    udx_index_node *node = (udx_index_node *)malloc(sizeof(udx_index_node));
    if (node == NULL) {
        free(data);
        return NULL;
    }

    // First byte of node data is the type; validate before use
    node->type = (udx_index_node_type)data[0];
    if (node->type != UDX_INDEX_NODE_TYPE_INTERNAL &&
        node->type != UDX_INDEX_NODE_TYPE_LEAF) {
        free(data);
        free(node);
        return NULL;
    }
    node->data = data;
    node->size = uncompressed_size;

    return node;
}

/**
 * Free a node
 */
static void free_node(udx_index_node *node) {
    if (node == NULL) return;
    free(node->data);
    free(node);
}

/**
 * Read next_leaf pointer from file
 * Should be called after reading a leaf node
 */
static bool read_next_leaf(udx_db *db, uint64_t *out_next_leaf) {
    return fread(out_next_leaf, sizeof(uint64_t), 1, db->reader->file) == 1;
}

// ============================================================
// Node Parsing Functions (zero-copy)
// ============================================================

/**
 * Parse an internal node from raw data (zero-copy)
 * Returns a view that points directly into the node data
 */
static inline ud_index_internal_node_info parse_internal_node(const udx_index_node *node) {
    ud_index_internal_node_info info = {0};

    const uint8_t *ptr = node->data + sizeof(uint8_t);  // skip node_type

    memcpy(&info.child_count, ptr, sizeof(uint16_t));
    ptr += sizeof(uint16_t);

    info.children = (const uint64_t *)ptr;
    info.keys_data = (const char *)(ptr + sizeof(uint64_t) * info.child_count);

    return info;
}

/**
 * Parse a leaf node from raw data (zero-copy)
 * Returns metadata only (entry_offsets requires separate allocation)
 */
static inline ud_index_leaf_node_info parse_leaf_node(const udx_index_node *node) {
    ud_index_leaf_node_info info = {0};

    const uint8_t *ptr = node->data + sizeof(uint8_t);  // skip node_type

    memcpy(&info.entry_count, ptr, sizeof(uint16_t));

    return info;
}

/**
 * Find child offset using binary search (GoldenDict style)
 * Uses "middle positioning" technique:
 * 1. Jump to middle of the key area
 * 2. Move backward to find the beginning of that string
 * 3. Compare and adjust window
 *
 * @return File offset of the child node to traverse next
 */
static uint64_t find_child_offset(const udx_index_node *node, const char *key) {
    ud_index_internal_node_info info = parse_internal_node(node);

    if (info.child_count == 0) return UDX_INVALID_NODE_OFFSET;  // Invalid/corrupted node

    if (info.child_count == 1) return info.children[0];

    const char *keys_start = info.keys_data;
    const char *keys_end = (const char *)node->data + node->size;

    // Binary search using middle positioning
    const char *window = keys_start;
    size_t window_size = keys_end - keys_start;

    while (window_size > 0) {
        // Jump to middle position
        const char *test_point = window + window_size / 2;

        // Move backward to find the beginning of the string
        const char *closest_key = test_point;
        while (closest_key > keys_start && closest_key[-1] != '\0') {
            closest_key--;
        }

        // Compare
        int cmp = strcmp(key, closest_key);

        if (cmp == 0) {
            // Exact match - find the entry number and return the right child
            size_t entry = 0;
            for (const char *next = keys_start; next != closest_key; next += strlen(next) + 1) {
                entry++;
            }
            return info.children[entry + 1];
        }
        else if (cmp < 0) {
            // Target is smaller, search left half
            window_size = closest_key - window;
        }
        else {
            // Target is larger, search right half
            size_t key_len = strlen(closest_key) + 1;
            window_size -= (closest_key - window) + key_len;
            window = closest_key + key_len;
        }
    }

    // If we exit the loop, find which entry we landed on
    size_t entry = 0;
    for (const char *next = keys_start; next != window; next += strlen(next) + 1) {
        entry++;
    }

    // Clamp to valid range
    if (entry >= info.child_count) {
        entry = info.child_count - 1;
    }

    return info.children[entry];
}

/**
 * Parse an entry
 * Format: [folded_key\0] [item_count:u16] [items...]
 *   item: [original_key\0] [address:u64] [data_size:u32]
 * @param out_entry If not NULL, returns parsed entry (caller must free)
 * @return Entry size in bytes
 */
static size_t parse_entry(const uint8_t *data, size_t available, udx_db_key_entry **out_entry) {
    const uint8_t *ptr = data;
    const uint8_t *end = data + available;

    // folded_key
    const char *folded_key = (const char *)ptr;
    size_t folded_len = strlen(folded_key) + 1;
    if (ptr + folded_len > end) return 0;
    ptr += folded_len;

    // item_count
    uint16_t item_count;
    if (ptr + sizeof(uint16_t) > end) return 0;
    memcpy(&item_count, ptr, sizeof(uint16_t));
    ptr += sizeof(uint16_t);

    // If only calculating size, continue parsing
    if (out_entry == NULL) {
        for (uint16_t i = 0; i < item_count; i++) {
            // original_key
            size_t original_len = strlen((const char *)ptr) + 1;
            if (ptr + original_len + sizeof(udx_value_address) + sizeof(uint32_t) > end) return 0;
            ptr += original_len;
            // address
            ptr += sizeof(udx_value_address);
            // data_size
            ptr += sizeof(uint32_t);
        }
        return ptr - data;
    }

    // Allocate entry
    udx_db_key_entry *entry = (udx_db_key_entry *)calloc(1, sizeof(udx_db_key_entry));
    if (entry == NULL) return 0;

    entry->key = strdup(folded_key);
    if (entry->key == NULL) {
        free(entry);
        return 0;
    }

    udx_db_key_entry_item_array_init(&entry->items);
    if (item_count > 0) {
        if (!udx_db_key_entry_item_array_reserve(&entry->items, item_count)) {
            free(entry->key);
            free(entry);
            return 0;
        }

        for (uint16_t i = 0; i < item_count; i++) {
            // original_key
            const char *original_key = (const char *)ptr;
            size_t original_len = strlen(original_key) + 1;
            if (ptr + original_len + sizeof(udx_value_address) + sizeof(uint32_t) > end) {
                udx_db_key_entry_free(entry);
                return 0;
            }
            char *original_copy = strdup(original_key);
            if (original_copy == NULL) {
                udx_db_key_entry_free(entry);
                return 0;
            }
            ptr += original_len;

            // address
            udx_value_address address;
            memcpy(&address, ptr, sizeof(udx_value_address));
            ptr += sizeof(udx_value_address);

            // data_size
            uint32_t data_size;
            memcpy(&data_size, ptr, sizeof(uint32_t));
            ptr += sizeof(uint32_t);

            // Add item
            udx_key_entry_item item;
            item.original_key = original_copy;
            item.value_address = address;
            item.value_size = data_size;
            udx_db_key_entry_item_array_push(&entry->items, item);
        }
    }

    *out_entry = entry;
    return ptr - data;
}

/**
 * Search in leaf node using binary search (GoldenDict style)
 * Format: [node_type:u8] [entry_count:u16] [entries...]
 * Each entry: [folded_key\0] [item_count:u16] [items...]
 *   item: [original_key\0] [address:u64] [data_size:u32]
 *
 * @param node Leaf node to search
 * @param key Key to search for
 * @param exact_match true = exact match, false = prefix match
 * @return Pointer to first matching entry in node->data, or NULL if not found
 */
static const uint8_t *search_leaf_node(const udx_index_node *node,
                                       const char *key,
                                       bool exact_match) {
    ud_index_leaf_node_info leaf_node_info = parse_leaf_node(node);

    if (leaf_node_info.entry_count == 0) return NULL;

    const uint8_t *ptr = node->data + sizeof(uint8_t) + sizeof(uint16_t);

    // Build array of entry pointers (GoldenDict style)
    const uint8_t **entry_offsets = (const uint8_t **)malloc(sizeof(uint8_t *) * leaf_node_info.entry_count);
    if (entry_offsets == NULL) return NULL;

    const uint8_t *current = ptr;
    const uint8_t *node_end = node->data + node->size;
    for (uint16_t i = 0; i < leaf_node_info.entry_count; i++) {
        entry_offsets[i] = current;

        // Skip to next entry
        // folded_key
        size_t folded_len = strlen((const char *)current) + 1;
        if (current + folded_len > node_end) { free(entry_offsets); return NULL; }
        current += folded_len;

        // item_count
        if (current + sizeof(uint16_t) > node_end) { free(entry_offsets); return NULL; }
        uint16_t item_count;
        memcpy(&item_count, current, sizeof(uint16_t));
        current += sizeof(uint16_t);

        // items
        for (uint16_t j = 0; j < item_count; j++) {
            // original_key
            size_t original_len = strlen((const char *)current) + 1;
            if (current + original_len + sizeof(udx_value_address) + sizeof(uint32_t) > node_end) {
                free(entry_offsets); return NULL;
            }
            current += original_len;

            // address + data_size
            current += sizeof(udx_value_address);
            current += sizeof(uint32_t);
        }
    }

    const uint8_t *result = NULL;

    // Binary search: find lower_bound (first entry >= key)
    const uint8_t **window = entry_offsets;
    size_t window_size = leaf_node_info.entry_count;
    size_t key_len = strlen(key);

    while (window_size > 0) {
        const uint8_t **mid = window + window_size / 2;
        const char *entry_key = (const char *)*mid;
        int cmp = strcmp(key, entry_key);

        if (cmp == 0) {
            // Found exact match
            window = mid;
            break;
        }
        else if (cmp < 0) {
            // Target is smaller, search left
            window_size = mid - window;
        }
        else {
            // Target is larger, search right
            window_size -= (mid - window) + 1;
            window = mid + 1;
        }
    }

    // window points to first entry >= key (lower_bound)
    // Check if it matches
    if (window < entry_offsets + leaf_node_info.entry_count) {
        const char *entry_key = (const char *)*window;

        if (exact_match) {
            // Check for exact match
            if (strcmp(key, entry_key) == 0) {
                result = *window;
            }
        } else {
            // Check for prefix match
            if (strncmp(key, entry_key, key_len) == 0) {
                result = *window;
            }
        }
    }

    free(entry_offsets);
    return result;
}

// ============================================================
// Public API
// ============================================================

// Forward declaration
static udx_db *load_db_by_offset(udx_reader *reader, uint64_t offset, const char *name);

// ============================================================
// Db Open/Close API
// ============================================================

/**
 * Load db by offset (internal helper)
 */
static udx_db *load_db_by_offset(udx_reader *reader, uint64_t offset, const char *name) {
    udx_db *db = (udx_db *)calloc(1, sizeof(udx_db));
    if (db == NULL) {
        return NULL;
    }

    db->reader = reader;
    db->name = name ? strdup(name) : NULL;

    // Seek to db header
    if (udx_fseek(reader->file, offset, SEEK_SET) != 0) {
        free(db->name);
        free(db);
        return NULL;
    }

    // Read db header
    uint8_t db_header_buf[UDX_DB_HEADER_SERIALIZED_SIZE];
    if (fread(db_header_buf, UDX_DB_HEADER_SERIALIZED_SIZE, 1, reader->file) != 1) {
        free(db->name);
        free(db);
        return NULL;
    }
    udx_db_header_deserialize(db_header_buf, &db->header);

    // Verify header checksum (computed on wire bytes, excluding checksum field)
    uint32_t calculated_checksum = (uint32_t)crc32(0, db_header_buf, UDX_DB_HEADER_CHECKSUM_OFFSET);
    if (db->header.checksum != calculated_checksum) {
        // Header checksum mismatch - data may be corrupted
        free(db->name);
        free(db);
        return NULL;
    }

    // Load metadata (if exists) - immediately after header
    if (db->header.metadata_size > 0) {
        db->metadata = (uint8_t *)malloc(db->header.metadata_size);
        if (db->metadata == NULL) {
            free(db->name);
            free(db);
            return NULL;
        }
        if (fread(db->metadata, 1, db->header.metadata_size, reader->file) != db->header.metadata_size) {
            free(db->metadata);
            free(db->name);
            free(db);
            return NULL;
        }
        db->metadata_size = db->header.metadata_size;
    }

    // Load chunk reader and root node
    if (db->header.chunk_table_offset > 0) {
        db->chunk_reader = udx_chunk_reader_create(reader->file, db->header.chunk_table_offset);
    }

    if (db->header.index_root_offset > 0) {
        db->root_node = read_index_node(db, db->header.index_root_offset);
        if (db->root_node == NULL) {
            udx_chunk_reader_destroy(db->chunk_reader);
            free(db->metadata);
            free(db->name);
            free(db);
            return NULL;
        }
    }

    return db;
}

/**
 * Open a db by name from reader
 */
udx_db *udx_db_open(udx_reader *reader, const char *name) {
    if (reader == NULL) {
        return NULL;
    }

    // Default to first db if name is NULL
    if (name == NULL) {
        if (reader->db_count == 0) {
            return NULL;
        }
        return load_db_by_offset(reader, reader->db_offsets[0], reader->db_names[0]);
    }

    // Find by name - search in section_names array
    for (uint16_t i = 0; i < reader->db_count; i++) {
        if (strcmp(reader->db_names[i], name) == 0) {
            return load_db_by_offset(reader, reader->db_offsets[i], name);
        }
    }

    return NULL;
}

/**
 * Close a db (does not close the reader)
 */
void udx_db_close(udx_db *db) {
    if (db == NULL) return;
    free(db->metadata);
    udx_chunk_reader_destroy(db->chunk_reader);
    free_node(db->root_node);
    free(db->name);
    // Don't close file - it's owned by reader
    free(db);
}

// ============================================================
// Internal Lookup Functions (static)
// ============================================================

udx_db_key_entry *udx_db_key_entry_lookup(udx_db *db, const char *key) {
    if (db == NULL || key == NULL) return NULL;

    if (db->header.entry_count == 0 || db->root_node == NULL) return NULL;

    char *folded_key = udx_fold_string(key);
    if (folded_key == NULL) return NULL;

    udx_index_node *current_node = db->root_node;
    bool is_root = true;

    while (ud_index_node_is_internal(current_node)) {
        uint64_t child_offset = find_child_offset(current_node, folded_key);
        if (!is_root) free_node(current_node);
        is_root = false;
        current_node = read_index_node(db, child_offset);
        if (current_node == NULL) {
            free(folded_key);
            return NULL;
        }
    }

    // Search for exact match
    const uint8_t *entry_ptr = search_leaf_node(current_node, folded_key, true);
    free(folded_key);

    if (entry_ptr == NULL) {
        if (!is_root) free_node(current_node);
        return NULL;
    }

    // Parse the found entry BEFORE freeing the node (fix use-after-free)
    udx_db_key_entry *entry = NULL;
    parse_entry(entry_ptr, current_node->data + current_node->size - entry_ptr, &entry);

    if (!is_root) free_node(current_node);
    return entry;
}

udx_db_key_entry_array *udx_db_key_entry_prefix_match(udx_db *db, const char *prefix,
                                                         size_t limit) {
    if (db == NULL || prefix == NULL) return NULL;

    if (db->header.entry_count == 0 || db->root_node == NULL) return NULL;

    char *key = udx_fold_string(prefix);
    if (key == NULL) return NULL;

    udx_index_node *current_node = db->root_node;
    bool is_root = true;

    while (ud_index_node_is_internal(current_node)) {
        uint64_t child_offset = find_child_offset(current_node, key);
        if (!is_root) free_node(current_node);
        is_root = false;
        current_node = read_index_node(db, child_offset);
        if (current_node == NULL) {
            free(key);
            return NULL;
        }
    }

    // Now current_node is a leaf node
    uint64_t next_leaf = 0;

    if (!is_root) {
        if (!read_next_leaf(db, &next_leaf)) {
            free_node(current_node);
            free(key);
            return NULL;
        }
    }

    const uint8_t *first_entry = search_leaf_node(current_node, key, false);

    if (first_entry == NULL) {
        if (!is_root) free_node(current_node);
        free(key);
        return NULL;
    }

    udx_db_key_entry_array *result = calloc(1, sizeof(udx_db_key_entry_array));
    if (!result) {
        if (!is_root) free_node(current_node);
        free(key);
        return NULL;
    }
    const uint8_t *current = first_entry;
    const uint8_t *node_end = current_node->data + current_node->size;
    size_t key_len = strlen(key);

    size_t initial_capacity = (limit > 0 && limit < 64) ? limit : 64;
    if (!udx_db_key_entry_array_reserve(result, initial_capacity)) {
        if (!is_root) free_node(current_node);
        free(key);
        free(result);
        return NULL;
    }

    while (true) {
        while (current < node_end) {
            const char *entry_key = (const char *)current;

            if (strncmp(key, entry_key, key_len) != 0) {
                goto cleanup;
            }

            udx_db_key_entry *entry = NULL;
            size_t entry_size = parse_entry(current, node_end - current, &entry);
            if (entry_size == 0 || entry == NULL) {
                if (!is_root) free_node(current_node);
                udx_db_key_entry_array_free(result);
                free(key);
                return NULL;
            }

            // Add entry to result
            udx_db_key_entry_array_push(result, *entry);
            free(entry);

            if (limit > 0 && result->size >= limit) {
                // Collected enough, stop completely
                goto cleanup;
            }

            // Move to next entry
            current += entry_size;
        }

        // Check if we exhausted current leaf
        if (current < node_end) {
            // Didn't reach end, stopped early due to no match
            goto cleanup;
        }

        // Try to continue to next leaf
        if (next_leaf == 0) break;
        if (limit > 0 && result->size >= limit) break;

        // Read next leaf
        udx_index_node *next_node = read_index_node(db, next_leaf);
        if (next_node == NULL) break;

        // Read next_leaf pointer for this node
        if (!read_next_leaf(db, &next_leaf)) {
            free_node(next_node);
            break;
        }

        // Free current node (if not root)
        if (!is_root) free_node(current_node);
        is_root = false;
        current_node = next_node;

        // Setup for new leaf — entries start right after header
        node_end = current_node->data + current_node->size;
        current = current_node->data + sizeof(uint8_t) + sizeof(uint16_t);
    }

cleanup:
    if (!is_root) free_node(current_node);

    free(key);
    return result;
}

// ============================================================
// Public API
// ============================================================

/**
 * Internal helper: get data by address and size
 */
static uint8_t *udx_db_get_data(udx_db *db, udx_value_address address, uint32_t data_size) {
    if (db == NULL || db->chunk_reader == NULL) return NULL;
    return udx_chunk_reader_get_block(db->chunk_reader, address, data_size);
}

/**
 * Convert udx_db_key_entry to udx_db_value_entry
 * Loads all items for the key
 */
udx_db_value_entry *udx_db_value_entry_load(udx_db *db, const udx_db_key_entry *key_entry) {
    if (key_entry == NULL || key_entry->items.size == 0) {
        return NULL;
    }

    udx_db_value_entry *value_entry = (udx_db_value_entry *)calloc(1, sizeof(udx_db_value_entry));
    if (value_entry == NULL) {
        return NULL;
    }

    value_entry->key = strdup(key_entry->key);
    if (value_entry->key == NULL) {
        free(value_entry);
        return NULL;
    }

    udx_db_value_entry_item_array_init(&value_entry->items);

    // Reserve space for all items
    if (!udx_db_value_entry_item_array_reserve(&value_entry->items, key_entry->items.size)) {
        udx_db_value_entry_free(value_entry);
        return NULL;
    }

    // Load data for each item
    for (size_t i = 0; i < key_entry->items.size; i++) {
        udx_key_entry_item *src = &key_entry->items.data[i];

        udx_value_entry_item item;
        item.original_key = strdup(src->original_key);
        if (item.original_key == NULL) {
            udx_db_value_entry_free(value_entry);
            return NULL;
        }

        item.data = udx_db_get_data(db, src->value_address, src->value_size);
        item.size = src->value_size;

        if (item.data == NULL) {
            free(item.original_key);
            udx_db_value_entry_free(value_entry);
            return NULL;
        }

        // Push the item (ownership transferred)
        value_entry->items.data[i] = item;
        value_entry->items.size++;
    }

    return value_entry;
}

udx_db_value_entry *udx_db_value_entry_lookup(udx_db *db, const char *key) {
    if (db == NULL || key == NULL) {
        return NULL;
    }

    // Use internal index lookup
    udx_db_key_entry *key_entry = udx_db_key_entry_lookup(db, key);
    if (key_entry == NULL) {
        return NULL;
    }

    // Convert to data entry
    udx_db_value_entry *value_entry = udx_db_value_entry_load(db, key_entry);

    // Free key_entry
    udx_db_key_entry_free(key_entry);

    return value_entry;
}

// ============================================================
// Reader Accessor Functions
// ============================================================

uint32_t udx_reader_get_db_count(const udx_reader *reader) {
    return reader ? reader->db_count : 0;
}

const char *udx_reader_get_db_name(const udx_reader *reader, uint32_t index) {
    if (!reader || index >= reader->db_count) return NULL;
    return reader->db_names[index];
}

uint64_t udx_reader_get_db_offset(const udx_reader *reader, uint32_t index) {
    if (!reader || index >= reader->db_count) return 0;
    return reader->db_offsets[index];
}

// ============================================================
// Db Accessor Functions
// ============================================================

const char *udx_db_get_name(const udx_db *db) {
    return db ? db->name : NULL;
}

const uint8_t *udx_db_get_metadata(const udx_db *db, uint32_t *out_size) {
    if (!db) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) *out_size = db->metadata_size;
    return db->metadata;
}

uint32_t udx_db_get_key_count(const udx_db *db) {
    return db ? db->header.entry_count : 0;
}

uint32_t udx_db_get_item_count(const udx_db *db) {
    return db ? db->header.item_count : 0;
}

uint32_t udx_db_get_index_bptree_height(const udx_db *db) {
    return db ? db->header.index_bptree_height : 0;
}

/**
 * Open UDX file and read main header and db table
 */
udx_reader *udx_reader_open(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    udx_reader *reader = (udx_reader *)calloc(1, sizeof(udx_reader));
    if (reader == NULL) {
        return NULL;
    }

    reader->file = fopen(path, "rb");
    if (reader->file == NULL) {
        free(reader);
        return NULL;
    }

    // Read main header
    uint8_t header_buf[UDX_HEADER_SERIALIZED_SIZE];
    if (udx_fseek(reader->file, 0, SEEK_SET) != 0 ||
        fread(header_buf, UDX_HEADER_SERIALIZED_SIZE, 1, reader->file) != 1) {
        fclose(reader->file);
        free(reader);
        return NULL;
    }
    udx_header_deserialize(header_buf, &reader->header);

    // Validate magic
    if (memcmp(reader->header.magic, "UDX\0", 4) != 0) {
        fclose(reader->file);
        free(reader);
        return NULL;
    }

    // Validate version: major must match exactly; minor is backward-compatible
    if (reader->header.version_major != UDX_VERSION_MAJOR) {
        fclose(reader->file);
        free(reader);
        return NULL;
    }

    // Read db table (offsets and names)
    // Format: [count:u32] [(offset:u64, name: null-terminated string)] × count
    uint64_t table_offset = reader->header.db_table_offset;
    if (table_offset == 0) {
        fclose(reader->file);
        free(reader);
        return NULL;
    }

    if (udx_fseek(reader->file, table_offset, SEEK_SET) != 0) {
        fclose(reader->file);
        free(reader);
        return NULL;
    }

    // Get db_count from header
    uint16_t db_count = reader->header.db_count;

    // Allocate arrays
    uint64_t *offsets = (uint64_t *)malloc(sizeof(uint64_t) * db_count);
    char **names = (char **)malloc(sizeof(char *) * db_count);
    if (offsets == NULL || names == NULL) {
        free(offsets);
        free(names);
        fclose(reader->file);
        free(reader);
        return NULL;
    }

    // Read entries: (offset, name) for each db
    bool success = true;
    for (uint16_t i = 0; i < db_count; i++) {
        // Read offset
        if (fread(&offsets[i], sizeof(uint64_t), 1, reader->file) != 1) {
            success = false;
            break;
        }

        // Read name (null-terminated string) with dynamic buffer
        size_t cap = 64, pos = 0;
        char *name_buf = (char *)malloc(cap);
        if (name_buf == NULL) {
            success = false;
            break;
        }
        int ch;
        while ((ch = fgetc(reader->file)) != EOF && ch != '\0') {
            if (pos + 1 >= cap) {
                cap *= 2;
                char *tmp = (char *)realloc(name_buf, cap);
                if (tmp == NULL) {
                    free(name_buf);
                    success = false;
                    break;
                }
                name_buf = tmp;
            }
            name_buf[pos++] = (char)ch;
        }
        if (!success) break;
        name_buf[pos] = '\0';
        names[i] = name_buf;
    }

    if (!success) {
        for (uint16_t i = 0; i < db_count; i++) {
            if (names[i] != NULL) free(names[i]);
        }
        free(names);
        free(offsets);
        fclose(reader->file);
        free(reader);
        return NULL;
    }

    reader->db_offsets = offsets;
    reader->db_names = names;
    reader->db_count = db_count;

    return reader;
}

/**
 * Close file reader
 */
void udx_reader_close(udx_reader *reader) {
    if (reader == NULL) return;

    // Free db table (names and offsets)
    for (uint16_t i = 0; i < reader->db_count; i++) {
        free(reader->db_names[i]);
    }
    free(reader->db_names);
    free(reader->db_offsets);

    // Close file
    if (reader->file != NULL) {
        fclose(reader->file);
    }

    free(reader);
}

// ============================================================
// Iterator Implementation
// ============================================================

struct udx_db_iter {
    udx_db *db;
    udx_index_node *leaf_node;
    uint64_t next_leaf_offset;
    uint16_t current_index;       // Current entry index in leaf
    uint16_t entry_count;         // Total entries in current leaf
    const uint8_t *current_ptr;   // Current entry data pointer

    // Current entry (returned to caller)
    udx_db_key_entry current_entry;
};

udx_db_iter *udx_db_iter_create(udx_db *db) {
    if (db == NULL || db->header.entry_count == 0) return NULL;

    udx_db_iter *iter = (udx_db_iter *)calloc(1, sizeof(udx_db_iter));
    if (iter == NULL) return NULL;

    iter->db = db;

    // Read first leaf node
    iter->leaf_node = read_index_node(db, db->header.index_first_leaf_offset);
    if (iter->leaf_node == NULL) {
        free(iter);
        return NULL;
    }

    // Read next_leaf pointer
    if (!read_next_leaf(db, &iter->next_leaf_offset)) {
        free_node(iter->leaf_node);
        free(iter);
        return NULL;
    }

    // Parse leaf node header
    ud_index_leaf_node_info info = parse_leaf_node(iter->leaf_node);
    iter->entry_count = info.entry_count;

    const uint8_t *ptr = iter->leaf_node->data + sizeof(uint8_t) + sizeof(uint16_t);
    iter->current_index = 0;
    iter->current_ptr = ptr;

    return iter;
}

void udx_db_iter_destroy(udx_db_iter *iter) {
    if (iter == NULL) return;

    udx_db_key_entry_free_contents(&iter->current_entry);
    free_node(iter->leaf_node);
    free(iter);
}

const udx_db_key_entry *udx_db_iter_next(udx_db_iter *iter) {
    if (iter == NULL) return NULL;

    // Free previous entry
    udx_db_key_entry_free_contents(&iter->current_entry);

    // Traverse until we find the next entry
    while (true) {
        // Check if current leaf has more entries
        if (iter->current_index < iter->entry_count) {
            // Parse current entry using parse_entry
            udx_db_key_entry *key_entry = NULL;
            size_t entry_size = parse_entry(iter->current_ptr,
                                           iter->leaf_node->data + iter->leaf_node->size - iter->current_ptr,
                                           &key_entry);

            if (entry_size == 0 || key_entry == NULL) {
                return NULL;
            }

            // Advance pointer
            iter->current_ptr += entry_size;
            iter->current_index++;

            // Transfer ownership of key_entry internals to current_entry
            iter->current_entry = *key_entry;
            free(key_entry);  // Free wrapper only, contents now owned by current_entry

            return &iter->current_entry;
        }

        // Current leaf exhausted, try to read next leaf
        if (iter->next_leaf_offset == 0) {
            // No more leaves
            return NULL;
        }

        // Read next leaf node
        udx_index_node *new_leaf = read_index_node(iter->db, iter->next_leaf_offset);
        if (new_leaf == NULL) {
            return NULL;
        }

        // Read next_leaf pointer
        uint64_t next_leaf;
        if (!read_next_leaf(iter->db, &next_leaf)) {
            free_node(new_leaf);
            return NULL;
        }

        free_node(iter->leaf_node);
        iter->leaf_node = new_leaf;
        iter->next_leaf_offset = next_leaf;

        // Parse new leaf node header
        ud_index_leaf_node_info info = parse_leaf_node(iter->leaf_node);
        iter->entry_count = info.entry_count;

        const uint8_t *ptr = iter->leaf_node->data + sizeof(uint8_t) + sizeof(uint16_t);
        iter->current_index = 0;
        iter->current_ptr = ptr;

        // Continue loop to read first entry
    }
}
