//
//  udx_types.h
//  libudx
//
//  Public types for UDX library consumers.
//
//  Created by kejinlu on 2026/2/25.
//

#ifndef udx_types_h
#define udx_types_h

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Status Codes
// ============================================================

typedef enum {
    UDX_OK                      = 0,
    UDX_NOT_FOUND               = 1,
    UDX_ERR_INVALID_PARAM       = -1,
    UDX_ERR_IO                  = -2,
    UDX_ERR_HEADER              = -3,
    UDX_ERR_INTERNAL            = -4,
    UDX_ERR_STATE               = -5,
    UDX_ERR_OVERFLOW            = -6,
    UDX_ERR_MEMORY              = -7
} udx_status;

// ============================================================
// Basic Types
// ============================================================

// Value address: 48-bit chunk_index + 16-bit offset in chunk
typedef uint64_t udx_value_address;

// Invalid address sentinel (returned by udx_db_builder_add_value on failure)
#define UDX_INVALID_ADDRESS      UINT64_MAX

// ============================================================
// Item Types
// ============================================================

// Key entry item (one original_key + address + size)
typedef struct {
    char *original_key;
    udx_value_address value_address;
    uint32_t value_size;
} udx_key_entry_item;

// Value entry item (one original_key + data pair)
typedef struct {
    char *original_key;
    uint8_t *data;
    size_t size;
} udx_value_entry_item;

// ============================================================
// Item Arrays
// ============================================================

typedef struct {
    udx_key_entry_item *elements;
    size_t count;
    size_t capacity;
} udx_db_key_entry_item_array;

typedef struct {
    udx_value_entry_item *elements;
    size_t count;
    size_t capacity;
} udx_db_value_entry_item_array;

static inline void udx_db_key_entry_item_array_free(udx_db_key_entry_item_array *arr) {
    if (arr == NULL) return;
    free(arr->elements);
    arr->elements = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static inline void udx_db_value_entry_item_array_free(udx_db_value_entry_item_array *arr) {
    if (arr == NULL) return;
    free(arr->elements);
    arr->elements = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

// ============================================================
// Entry Types
// ============================================================

/**
 * A key entry in the UDX index.
 *
 * Each entry corresponds to one folded key, and may contain multiple
 * items if several original keys fold to the same form (e.g. "Apple"
 * and "apple" share one entry with two items).
 */
typedef struct {
    char *key;
    udx_db_key_entry_item_array items;
} udx_db_key_entry;

// Value entry: one folded key + multiple items with data content
typedef struct {
    char *key;
    udx_db_value_entry_item_array items;
} udx_db_value_entry;

// ============================================================
// Entry Operations
// ============================================================

// Free internal fields only (entry is stack-allocated or embedded)
void udx_db_key_entry_free_contents(udx_db_key_entry *entry);

// Free internal fields AND the entry itself (entry was heap-allocated)
void udx_db_key_entry_free(udx_db_key_entry *entry);
void udx_db_value_entry_free(udx_db_value_entry *entry);

// ============================================================
// Entry Array
// ============================================================

typedef struct {
    udx_db_key_entry *elements;
    size_t count;
    size_t capacity;
} udx_db_key_entry_array;

static inline void udx_db_key_entry_array_free(udx_db_key_entry_array *arr) {
    if (arr == NULL) return;
    for (size_t i = 0; i < arr->count; i++) {
        udx_db_key_entry_free_contents(&arr->elements[i]);
    }
    free(arr->elements);
    free(arr);
}

#ifdef __cplusplus
}
#endif

#endif /* udx_types_h */
