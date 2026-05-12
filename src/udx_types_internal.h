//
//  udx_types_internal.h
//  libudx
//
//  Internal types for UDX implementation files only.
//
//  Created by kejinlu on 2026/2/25.
//

#ifndef udx_types_internal_h
#define udx_types_internal_h

#include "udx_types.h"
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Internal Constants
// ============================================================

// Magic: "UDX\0" + version_major + version_minor
#define UDX_MAGIC_PREFIX   "UDX\0"
#define UDX_VERSION_MAJOR  1
#define UDX_VERSION_MINOR  0

// Chunk parameters (shared by both modes)
#define UDX_CHUNK_MAX_SIZE           65536   // 64KB per chunk

// UDX Index Static B+ tree parameters
#define UDX_NODE_MIN_ELEMENTS  64
#define UDX_NODE_MAX_ELEMENTS  8192

// Invalid B+ tree node offset (offset 0 is reserved for file header)
#define UDX_INVALID_NODE_OFFSET  0

// ============================================================
// Utility Arrays
// ============================================================

// ---- udx_uint64_array ----
typedef struct {
    uint64_t *elements;
    size_t count;
    size_t capacity;
} udx_uint64_array;

static inline bool udx_uint64_array_push(udx_uint64_array *arr, uint64_t val) {
    if (arr == NULL) return false;
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        uint64_t *new_data = (uint64_t *)realloc(arr->elements, new_cap * sizeof(uint64_t));
        if (new_data == NULL) return false;
        arr->elements = new_data;
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
    return true;
}

static inline void udx_uint64_array_free(udx_uint64_array *arr) {
    if (arr == NULL) return;
    free(arr->elements);
    arr->elements = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

// ---- udx_string_array ----
typedef struct {
    char **elements;
    size_t count;
    size_t capacity;
} udx_string_array;

static inline bool udx_string_array_push(udx_string_array *arr, char *val) {
    if (arr == NULL) return false;
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        char **new_data = (char **)realloc(arr->elements, new_cap * sizeof(char *));
        if (new_data == NULL) return false;
        arr->elements = new_data;
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
    return true;
}

static inline void udx_string_array_free(udx_string_array *arr) {
    if (arr == NULL) return;
    for (size_t i = 0; i < arr->count; i++) {
        free(arr->elements[i]);
    }
    free(arr->elements);
    arr->elements = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

// ============================================================
// Index B+ Tree Node Types
// ============================================================

typedef enum {
    UDX_INDEX_NODE_TYPE_INTERNAL = 0x01,
    UDX_INDEX_NODE_TYPE_LEAF     = 0x02
} udx_index_node_type;

typedef struct {
    udx_index_node_type type;
    uint8_t *data;
    size_t size;
} udx_index_node;

// ============================================================
// Entry Cleanup (internal use only)
// ============================================================

// Free internal fields only (entry is stack-allocated or embedded)
void udx_db_key_entry_cleanup(udx_db_key_entry *entry);

// ============================================================
// Item Array Builders (internal use only)
// ============================================================

// ---- udx_db_key_entry_item_array ----

void udx_db_key_entry_item_array_cleanup(udx_db_key_entry_item_array *arr);

static inline bool udx_db_key_entry_item_array_push(udx_db_key_entry_item_array *arr, udx_key_entry_item val) {
    if (arr == NULL) return false;
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        udx_key_entry_item *new_data = (udx_key_entry_item *)realloc(arr->elements, new_cap * sizeof(udx_key_entry_item));
        if (new_data == NULL) return false;
        arr->elements = new_data;
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
    return true;
}

static inline bool udx_db_key_entry_item_array_reserve(udx_db_key_entry_item_array *arr, size_t new_cap) {
    if (arr == NULL) return false;
    if (new_cap <= arr->capacity) return true;
    udx_key_entry_item *new_data = (udx_key_entry_item *)realloc(arr->elements, new_cap * sizeof(udx_key_entry_item));
    if (new_data == NULL) return false;
    arr->elements = new_data;
    arr->capacity = new_cap;
    return true;
}

// ---- udx_db_key_entry_array ----

static inline bool udx_db_key_entry_array_reserve(udx_db_key_entry_array *arr, size_t new_cap) {
    if (arr == NULL) return false;
    if (new_cap <= arr->capacity) return true;
    udx_db_key_entry *new_data = (udx_db_key_entry *)realloc(arr->elements, new_cap * sizeof(udx_db_key_entry));
    if (new_data == NULL) return false;
    arr->elements = new_data;
    arr->capacity = new_cap;
    return true;
}

static inline bool udx_db_key_entry_array_push(udx_db_key_entry_array *arr, udx_db_key_entry val) {
    if (arr == NULL) return false;
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        udx_db_key_entry *new_data = (udx_db_key_entry *)realloc(arr->elements, new_cap * sizeof(udx_db_key_entry));
        if (new_data == NULL) return false;
        arr->elements = new_data;
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
    return true;
}

// ---- udx_db_value_entry_item_array ----

void udx_db_value_entry_item_array_cleanup(udx_db_value_entry_item_array *arr);

static inline bool udx_db_value_entry_item_array_push(udx_db_value_entry_item_array *arr, udx_value_entry_item val) {
    if (arr == NULL) return false;
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        udx_value_entry_item *new_data = (udx_value_entry_item *)realloc(arr->elements, new_cap * sizeof(udx_value_entry_item));
        if (new_data == NULL) return false;
        arr->elements = new_data;
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
    return true;
}

static inline bool udx_db_value_entry_item_array_reserve(udx_db_value_entry_item_array *arr, size_t new_cap) {
    if (arr == NULL) return false;
    if (new_cap <= arr->capacity) return true;
    udx_value_entry_item *new_data = (udx_value_entry_item *)realloc(arr->elements, new_cap * sizeof(udx_value_entry_item));
    if (new_data == NULL) return false;
    arr->elements = new_data;
    arr->capacity = new_cap;
    return true;
}

// ============================================================
// File Header Structures
// ============================================================

typedef struct {
    char magic[4];                    // "UDX\0"
    uint8_t version_major;
    uint8_t version_minor;
    uint16_t db_count;
    uint64_t db_table_offset;
} udx_header;

// Serialized size: magic(4) + version(2) + db_count(2) + db_table_offset(8) = 16 bytes
#define UDX_HEADER_SERIALIZED_SIZE              16
#define UDX_HEADER_DB_COUNT_POS                  6
#define UDX_HEADER_DB_TABLE_OFFSET_POS          8

// chunk_count is stored in chunk table data header (not here) so that
// the chunk table can be parsed self-contained without external parameters.
typedef struct {
    uint64_t chunk_table_offset;
    uint64_t index_root_offset;
    uint64_t index_first_leaf_offset;
    uint32_t index_bptree_height;
    uint32_t entry_count;
    uint32_t item_count;
    uint32_t metadata_size;
    uint32_t checksum;                // CRC32 of preceding fields
} udx_db_header;

// Serialized size: 8*3 + 4*5 = 44 bytes (no struct padding)
#define UDX_DB_HEADER_SERIALIZED_SIZE        44
#define UDX_DB_HEADER_CHECKSUM_OFFSET        40

// ============================================================
// Header Serialization
// ============================================================

static inline void udx_header_serialize(const udx_header *h, uint8_t buf[UDX_HEADER_SERIALIZED_SIZE]) {
    uint8_t *p = buf;
    memcpy(p, h->magic, 4);
    p += 4;
    *p++ = h->version_major;
    *p++ = h->version_minor;
    memcpy(p, &h->db_count, 2);
    p += 2;
    memcpy(p, &h->db_table_offset, 8);
    p += 8;
}

static inline void udx_header_deserialize(const uint8_t buf[UDX_HEADER_SERIALIZED_SIZE], udx_header *h) {
    const uint8_t *p = buf;
    memcpy(h->magic, p, 4);
    p += 4;
    h->version_major = *p++;
    h->version_minor = *p++;
    memcpy(&h->db_count, p, 2);
    p += 2;
    memcpy(&h->db_table_offset, p, 8);
    p += 8;
}

static inline void udx_db_header_serialize(const udx_db_header *h, uint8_t buf[UDX_DB_HEADER_SERIALIZED_SIZE]) {
    uint8_t *p = buf;
    memcpy(p, &h->chunk_table_offset, 8);
    p += 8;
    memcpy(p, &h->index_root_offset, 8);
    p += 8;
    memcpy(p, &h->index_first_leaf_offset, 8);
    p += 8;
    memcpy(p, &h->index_bptree_height, 4);
    p += 4;
    memcpy(p, &h->entry_count, 4);
    p += 4;
    memcpy(p, &h->item_count, 4);
    p += 4;
    memcpy(p, &h->metadata_size, 4);
    p += 4;
    memcpy(p, &h->checksum, 4);
    p += 4;
}

static inline void udx_db_header_deserialize(const uint8_t buf[UDX_DB_HEADER_SERIALIZED_SIZE], udx_db_header *h) {
    const uint8_t *p = buf;
    memcpy(&h->chunk_table_offset, p, 8);
    p += 8;
    memcpy(&h->index_root_offset, p, 8);
    p += 8;
    memcpy(&h->index_first_leaf_offset, p, 8);
    p += 8;
    memcpy(&h->index_bptree_height, p, 4);
    p += 4;
    memcpy(&h->entry_count, p, 4);
    p += 4;
    memcpy(&h->item_count, p, 4);
    p += 4;
    memcpy(&h->metadata_size, p, 4);
    p += 4;
    memcpy(&h->checksum, p, 4);
    p += 4;
}

#ifdef __cplusplus
}
#endif

#endif /* udx_types_internal_h */
