//
//  udx_types.h
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#ifndef udx_types_h
#define udx_types_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Constants
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

// ============================================================
// Error Codes
// ============================================================

typedef enum {
    UDX_OK                      = 0,    // Success
    UDX_ERR_INVALID_PARAM       = -1,   // Invalid parameter (NULL pointer, etc.)
    UDX_ERR_IO                  = -2,   // File I/O error
    UDX_ERR_BPTREE              = -3,   // B+ tree operation failed
    UDX_ERR_HEADER              = -4,   // Header write/read failed
    UDX_ERR_CHUNK               = -5,   // Chunk writer/reader failed
    UDX_ERR_KEYS               = -6,   // Keys container operation failed
    UDX_ERR_ACTIVE_DB           = -7,   // A database builder is still active
    UDX_ERR_DUPLICATE_NAME      = -8,   // Duplicate database name
    UDX_ERR_METADATA            = -9,   // Invalid metadata parameters
    UDX_ERR_OVERFLOW            = -10,  // Integer/size overflow
    UDX_ERR_MEMORY              = -11   // Memory allocation failed
} udx_error_t;

// ============================================================
// Basic Types
// ============================================================

// Value address: 48-bit chunk_index + 16-bit offset in chunk (chunk_index limited to 32 bits)
typedef uint64_t udx_value_address;

// Invalid address sentinel (used to indicate errors)
#define UDX_INVALID_ADDRESS      UINT64_MAX

// Invalid B+ tree node offset (offset 0 is reserved for file header)
#define UDX_INVALID_NODE_OFFSET  0

// ============================================================
// Item Types
// ============================================================

// Key entry item (one original_key + address + data_size)
typedef struct {
    char *original_key;       // Original key (preserves case)
    udx_value_address value_address; // Value address
    uint32_t data_size;        // Data size in bytes
} udx_key_entry_item;

// Data entry item (one original_key + data pair)
typedef struct {
    char *original_key;  // Original key (preserves case)
    uint8_t *data;        // Data content
    size_t size;          // Data size
} udx_value_entry_item;

// ============================================================
// Item Arrays (used by Entry Types)
// ============================================================

// ---- udx_db_key_entry_item_array ----
typedef struct {
    udx_key_entry_item *data;
    size_t size;
    size_t capacity;
} udx_db_key_entry_item_array;

static inline void udx_db_key_entry_item_array_init(udx_db_key_entry_item_array *arr) {
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

static inline bool udx_db_key_entry_item_array_push(udx_db_key_entry_item_array *arr, udx_key_entry_item val) {
    if (arr == NULL) return false;
    if (arr->size >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        udx_key_entry_item *new_data = (udx_key_entry_item *)realloc(arr->data, new_cap * sizeof(udx_key_entry_item));
        if (new_data == NULL) return false;
        arr->data = new_data;
        arr->capacity = new_cap;
    }
    arr->data[arr->size++] = val;
    return true;
}

static inline bool udx_db_key_entry_item_array_reserve(udx_db_key_entry_item_array *arr, size_t new_cap) {
    if (arr == NULL) return false;
    if (new_cap <= arr->capacity) return true;
    udx_key_entry_item *new_data = (udx_key_entry_item *)realloc(arr->data, new_cap * sizeof(udx_key_entry_item));
    if (new_data == NULL) return false;
    arr->data = new_data;
    arr->capacity = new_cap;
    return true;
}

static inline void udx_db_key_entry_item_array_free(udx_db_key_entry_item_array *arr) {
    if (arr == NULL) return;
    free(arr->data);
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

// ---- udx_db_value_entry_item_array ----
typedef struct {
    udx_value_entry_item *data;
    size_t size;
    size_t capacity;
} udx_db_value_entry_item_array;

static inline void udx_db_value_entry_item_array_init(udx_db_value_entry_item_array *arr) {
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

static inline bool udx_db_value_entry_item_array_push(udx_db_value_entry_item_array *arr, udx_value_entry_item val) {
    if (arr == NULL) return false;
    if (arr->size >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        udx_value_entry_item *new_data = (udx_value_entry_item *)realloc(arr->data, new_cap * sizeof(udx_value_entry_item));
        if (new_data == NULL) return false;
        arr->data = new_data;
        arr->capacity = new_cap;
    }
    arr->data[arr->size++] = val;
    return true;
}

static inline bool udx_db_value_entry_item_array_reserve(udx_db_value_entry_item_array *arr, size_t new_cap) {
    if (arr == NULL) return false;
    if (new_cap <= arr->capacity) return true;
    udx_value_entry_item *new_data = (udx_value_entry_item *)realloc(arr->data, new_cap * sizeof(udx_value_entry_item));
    if (new_data == NULL) return false;
    arr->data = new_data;
    arr->capacity = new_cap;
    return true;
}

static inline void udx_db_value_entry_item_array_free(udx_db_value_entry_item_array *arr) {
    if (arr == NULL) return;
    free(arr->data);
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

// ============================================================
// Entry Types
// ============================================================

// Key entry (folded key + items with addresses)
typedef struct {
    char *key;                      // Key (folded for sorting and lookup)
    udx_db_key_entry_item_array items; // Original keys and addresses
} udx_db_key_entry;

// Data entry (folded key + items with data content)
typedef struct {
    char *key;                     // Key (folded for sorting and lookup)
    udx_db_value_entry_item_array items;  // Original keys and data
} udx_db_value_entry;

// ============================================================
// Entry Arrays
// ============================================================

// ---- udx_db_key_entry_array ----
typedef struct {
    udx_db_key_entry *data;
    size_t size;
    size_t capacity;
} udx_db_key_entry_array;

static inline void udx_db_key_entry_array_init(udx_db_key_entry_array *arr) {
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

static inline void udx_db_key_entry_array_free(udx_db_key_entry_array *arr) {
    if (arr == NULL) return;
    free(arr->data);
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

static inline bool udx_db_key_entry_array_reserve(udx_db_key_entry_array *arr, size_t new_cap) {
    if (arr == NULL) return false;
    if (new_cap <= arr->capacity) return true;
    udx_db_key_entry *new_data = (udx_db_key_entry *)realloc(arr->data, new_cap * sizeof(udx_db_key_entry));
    if (new_data == NULL) return false;
    arr->data = new_data;
    arr->capacity = new_cap;
    return true;
}

static inline bool udx_db_key_entry_array_push(udx_db_key_entry_array *arr, udx_db_key_entry val) {
    if (arr == NULL) return false;
    if (arr->size >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        udx_db_key_entry *new_data = (udx_db_key_entry *)realloc(arr->data, new_cap * sizeof(udx_db_key_entry));
        if (new_data == NULL) return false;
        arr->data = new_data;
        arr->capacity = new_cap;
    }
    arr->data[arr->size++] = val;
    return true;
}

// Forward declaration (defined in udx_types.c)
void udx_key_entry_free_contents(udx_db_key_entry *entry);

static inline void udx_db_key_entry_array_free_contents(udx_db_key_entry_array *arr) {
    if (arr == NULL) return;
    for (size_t i = 0; i < arr->size; i++) {
        udx_key_entry_free_contents(&arr->data[i]);
    }
    udx_db_key_entry_array_free(arr);
}

// ============================================================
// Entry Operations
// ============================================================

void udx_key_entry_free(udx_db_key_entry *entry);
void udx_value_entry_free_contents(udx_db_value_entry *entry);
void udx_value_entry_free(udx_db_value_entry *entry);

// ============================================================
// Other Dynamic Arrays
// ============================================================

// ---- udx_uint64_array ----
typedef struct {
    uint64_t *data;
    size_t size;
    size_t capacity;
} udx_uint64_array;

static inline void udx_uint64_array_init(udx_uint64_array *arr) {
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

static inline bool udx_uint64_array_push(udx_uint64_array *arr, uint64_t val) {
    if (arr == NULL) return false;
    if (arr->size >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        uint64_t *new_data = (uint64_t *)realloc(arr->data, new_cap * sizeof(uint64_t));
        if (new_data == NULL) return false;
        arr->data = new_data;
        arr->capacity = new_cap;
    }
    arr->data[arr->size++] = val;
    return true;
}

static inline void udx_uint64_array_free(udx_uint64_array *arr) {
    if (arr == NULL) return;
    free(arr->data);
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

// ---- udx_string_array ----
typedef struct {
    char **data;
    size_t size;
    size_t capacity;
} udx_string_array;

static inline void udx_string_array_init(udx_string_array *arr) {
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

static inline bool udx_string_array_push(udx_string_array *arr, char *val) {
    if (arr == NULL) return false;
    if (arr->size >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        char **new_data = (char **)realloc(arr->data, new_cap * sizeof(char *));
        if (new_data == NULL) return false;
        arr->data = new_data;
        arr->capacity = new_cap;
    }
    arr->data[arr->size++] = val;
    return true;
}

static inline void udx_string_array_free(udx_string_array *arr) {
    if (arr == NULL) return;
    free(arr->data);
    arr->data = NULL;
    arr->size = 0;
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
    udx_index_node_type type;         // Node type (INTERNAL or LEAF)
    uint8_t *data;                   // Decompressed node data
    size_t size;                     // Data size
} udx_index_node;

// ============================================================
// File Header Structures
// ============================================================

typedef struct {
    char magic[4];                    // "UDX\0"
    uint8_t version_major;            // Major version number
    uint8_t version_minor;            // Minor version number
    uint16_t db_count;                // number of databases
    uint64_t db_table_offset;         // db offsets table
} udx_header;

// Serialized size: magic(4) + version_major(1) + version_minor(1) + db_count(2) + db_table_offset(8) = 16 bytes
#define UDX_HEADER_SERIALIZED_SIZE              16
// Serialized offset of db_count field: magic(4) + version(1+1) = 6
#define UDX_HEADER_DB_COUNT_POS                  6
// Serialized offset of db_table_offset field: magic(4) + version(1+1) + db_count(2) = 8
#define UDX_HEADER_DB_TABLE_OFFSET_POS          8

typedef struct {
    uint64_t chunk_table_offset;      // Chunk table offset
    uint64_t index_root_offset;       // B+ tree root node offset
    uint64_t index_first_leaf_offset; // B+ tree first leaf node offset
    uint32_t index_entry_count;       // Total number of entries (unique keys)
    uint32_t index_item_count;        // Total number of addresses (all keys)
    uint32_t index_bptree_height;     // B+ tree height
    uint32_t metadata_size;           // Metadata size (0 = no metadata)
    uint32_t checksum;                // CRC32 checksum of header (excluding this field)
} udx_db_header;

// Serialized size: 8*3 + 4*5 = 44 bytes (no struct padding)
#define UDX_DB_HEADER_SERIALIZED_SIZE        44
// Checksum covers the first 40 bytes (everything before the checksum field)
#define UDX_DB_HEADER_CHECKSUM_OFFSET        40

// ============================================================
// Header Serialization Helpers
// ============================================================

// ---- udx_header ----

static inline void udx_header_serialize(const udx_header *h, uint8_t buf[UDX_HEADER_SERIALIZED_SIZE]) {
    uint8_t *p = buf;
    memcpy(p, h->magic, 4);             p += 4;
    *p++ = h->version_major;
    *p++ = h->version_minor;
    memcpy(p, &h->db_count, 2);        p += 2;
    memcpy(p, &h->db_table_offset, 8);  p += 8;
}

static inline void udx_header_deserialize(const uint8_t buf[UDX_HEADER_SERIALIZED_SIZE], udx_header *h) {
    const uint8_t *p = buf;
    memcpy(h->magic, p, 4);             p += 4;
    h->version_major = *p++;
    h->version_minor = *p++;
    memcpy(&h->db_count, p, 2);        p += 2;
    memcpy(&h->db_table_offset, p, 8);  p += 8;
}

// ---- udx_db_header ----

static inline void udx_db_header_serialize(const udx_db_header *h, uint8_t buf[UDX_DB_HEADER_SERIALIZED_SIZE]) {
    uint8_t *p = buf;
    memcpy(p, &h->chunk_table_offset,     8); p += 8;
    memcpy(p, &h->index_root_offset,      8); p += 8;
    memcpy(p, &h->index_first_leaf_offset,8); p += 8;
    memcpy(p, &h->index_entry_count,      4); p += 4;
    memcpy(p, &h->index_item_count,       4); p += 4;
    memcpy(p, &h->index_bptree_height,    4); p += 4;
    memcpy(p, &h->metadata_size,          4); p += 4;
    memcpy(p, &h->checksum,              4); p += 4;
}

static inline void udx_db_header_deserialize(const uint8_t buf[UDX_DB_HEADER_SERIALIZED_SIZE], udx_db_header *h) {
    const uint8_t *p = buf;
    memcpy(&h->chunk_table_offset,     p, 8); p += 8;
    memcpy(&h->index_root_offset,      p, 8); p += 8;
    memcpy(&h->index_first_leaf_offset,p, 8); p += 8;
    memcpy(&h->index_entry_count,      p, 4); p += 4;
    memcpy(&h->index_item_count,       p, 4); p += 4;
    memcpy(&h->index_bptree_height,    p, 4); p += 4;
    memcpy(&h->metadata_size,          p, 4); p += 4;
    memcpy(&h->checksum,              p, 4); p += 4;
}

#ifdef __cplusplus
}
#endif

#endif /* udx_types_h */
