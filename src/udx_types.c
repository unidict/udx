//
//  udx_types.c
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#include "udx_types.h"
#include <stdlib.h>

void udx_db_key_entry_free_contents(udx_db_key_entry *entry) {
    if (entry == NULL) return;
    free(entry->key);
    entry->key = NULL;

    // Free items array
    for (size_t i = 0; i < entry->items.size; i++) {
        free(entry->items.data[i].original_key);
    }
    udx_db_key_entry_item_array_free(&entry->items);
}

void udx_db_key_entry_free(udx_db_key_entry *entry) {
    if (entry == NULL) return;
    udx_db_key_entry_free_contents(entry);
    free(entry);
}

void udx_db_value_entry_free(udx_db_value_entry *entry) {
    if (entry == NULL) return;
    free(entry->key);
    for (size_t i = 0; i < entry->items.size; i++) {
        free(entry->items.data[i].original_key);
        free(entry->items.data[i].data);
    }
    udx_db_value_entry_item_array_free(&entry->items);
    free(entry);
}
