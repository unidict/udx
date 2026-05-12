//
//  udx_types.c
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#include "udx_types_internal.h"
#include <stdlib.h>

void udx_db_key_entry_cleanup(udx_db_key_entry *entry) {
    if (entry == NULL) return;
    free(entry->key);
    entry->key = NULL;
    udx_db_key_entry_item_array_cleanup(&entry->items);
}

void udx_db_key_entry_free(udx_db_key_entry *entry) {
    if (entry == NULL) return;
    udx_db_key_entry_cleanup(entry);
    free(entry);
}

void udx_db_value_entry_free(udx_db_value_entry *entry) {
    if (entry == NULL) return;
    free(entry->key);
    udx_db_value_entry_item_array_cleanup(&entry->items);
    free(entry);
}

void udx_db_key_entry_item_array_cleanup(udx_db_key_entry_item_array *arr) {
    if (arr == NULL) return;
    for (size_t i = 0; i < arr->count; i++) {
        free(arr->elements[i].original_key);
    }
    free(arr->elements);
    arr->elements = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void udx_db_value_entry_item_array_cleanup(udx_db_value_entry_item_array *arr) {
    if (arr == NULL) return;
    for (size_t i = 0; i < arr->count; i++) {
        free(arr->elements[i].original_key);
        free(arr->elements[i].data);
    }
    free(arr->elements);
    arr->elements = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void udx_db_key_entry_array_free(udx_db_key_entry_array *arr) {
    if (arr == NULL) return;
    for (size_t i = 0; i < arr->count; i++) {
        udx_db_key_entry_cleanup(&arr->elements[i]);
    }
    free(arr->elements);
    free(arr);
}
