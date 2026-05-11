//
//  udx_writer.c
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#include "udx_writer.h"
#include "udx_types.h"
#include "udx_keys.h"
#include "udx_chunk.h"
#include "udx_utils.h"
#include <zlib.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ============================================================
// Internal Structures
// ============================================================

// file-level writer: manages file and db table only
struct udx_writer {
    FILE *file;
    udx_uint64_array db_offsets;   // Index block offset array
    udx_string_array db_names;     // Name array (parallel with offsets)
    bool has_db_active;            // Whether a db is currently being built
};

// db-level builder: manages single db's build state
struct udx_db_builder {
    udx_writer *writer;               // Back reference to file writer
    udx_chunk_writer *chunk_writer;
    udx_keys *keys;
    uint64_t db_offset;               // Position of this db block
    uint32_t metadata_size;           // Size of metadata (0 if not set)
    uint64_t index_root_offset;
    uint64_t index_first_leaf_offset;
    uint32_t index_bptree_height;
};

// ============================================================
// Internal Helper Functions
// ============================================================

/**
 * Write index node to file (compressed)
 * @param file File handle
 * @param data Node data to write
 * @param size Data size
 * @return File offset on success, 0 on failure
 *
 * @note Returns 0 on failure (offset 0 is reserved for file header)
 */
static uint64_t write_index_node(FILE *file, const uint8_t *data, size_t size) {
    // Check size limit (must fit in uint32_t)
    if (size > UINT32_MAX) return 0;

    int64_t offset = udx_ftell(file);
    if (offset < 0) return 0;

    uLong compressed_bound = compressBound((uLong)size);
    uint8_t *compressed = (uint8_t *)malloc(compressed_bound);
    if (compressed == NULL) return 0;

    uLong compressed_size = compressed_bound;
    int ret = compress2(compressed, &compressed_size, data, (uLong)size, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        free(compressed);
        return 0;
    }

    // Check compressed size limit
    if (compressed_size > UINT32_MAX) {
        free(compressed);
        return 0;
    }

    uint32_t uncompressed_size = (uint32_t)size;
    uint32_t comp_size = (uint32_t)compressed_size;

    if (fwrite(&uncompressed_size, sizeof(uint32_t), 1, file) != 1 ||
        fwrite(&comp_size, sizeof(uint32_t), 1, file) != 1 ||
        fwrite(compressed, 1, compressed_size, file) != compressed_size) {
        free(compressed);
        return 0;
    }

    free(compressed);
    return (uint64_t)offset;  // Safe: offset >= 0 here
}

// ============================================================
// Index B+ Tree Construction
// ============================================================

typedef struct {
    FILE *file;
    udx_keys_iter *iter;
    uint64_t last_leaf_link_offset;  // For back-patching next_leaf
    uint64_t first_leaf_offset;
    uint32_t max_elements;
} bptree_build_context;

// Forward declaration
static uint64_t build_bptree_node(bptree_build_context *ctx, size_t index_size, uint8_t *out_height);

// Serialize an index entry directly into a dynamic buffer, returning bytes written or 0 on failure.
static size_t serialize_index_entry_into(const udx_db_key_entry *entry,
                                         uint8_t **buffer, size_t *buffer_size, size_t *buffer_capacity) {
    // Format: [folded_key\0] [item_count:u16] [items...]
    //   item: [original_key\0] [address:u64] [data_size:u32]

    if (entry->items.count > UINT16_MAX) return 0;

    // Calculate required size
    size_t key_len = strlen(entry->key) + 1;
    uint16_t item_count = (uint16_t)entry->items.count;
    size_t items_size = 0;
    for (size_t i = 0; i < entry->items.count; i++) {
        items_size += strlen(entry->items.elements[i].original_key) + 1;
        items_size += sizeof(udx_value_address_t);
        items_size += sizeof(uint32_t);  // data_size
    }
    size_t total_size = key_len + sizeof(uint16_t) + items_size;

    // Ensure buffer capacity
    size_t needed = *buffer_size + total_size;
    if (needed > *buffer_capacity) {
        size_t new_capacity = *buffer_capacity * 2;
        if (new_capacity < needed) {
            new_capacity = needed + 4096;
        }
        uint8_t *new_buffer = (uint8_t *)realloc(*buffer, new_capacity);
        if (new_buffer == NULL) return 0;
        *buffer = new_buffer;
        *buffer_capacity = new_capacity;
    }

    uint8_t *ptr = *buffer + *buffer_size;

    // folded_key
    memcpy(ptr, entry->key, key_len);
    ptr += key_len;

    // item_count
    memcpy(ptr, &item_count, sizeof(uint16_t));
    ptr += sizeof(uint16_t);

    // items
    for (size_t i = 0; i < entry->items.count; i++) {
        const udx_key_entry_item *item = &entry->items.elements[i];
        size_t original_len = strlen(item->original_key) + 1;
        memcpy(ptr, item->original_key, original_len);
        ptr += original_len;
        memcpy(ptr, &item->value_address, sizeof(udx_value_address_t));
        ptr += sizeof(udx_value_address_t);
        memcpy(ptr, &item->value_size, sizeof(uint32_t));
        ptr += sizeof(uint32_t);
    }

    *buffer_size += total_size;
    return total_size;
}

// Build a leaf node
static uint64_t build_leaf_node(bptree_build_context *ctx, size_t index_size, uint8_t *out_height) {
    // Guard: entry_count must fit in uint16_t
    if (index_size > UINT16_MAX) return 0;

    FILE *file = ctx->file;

    // Layout: [node_type:u8][entry_count:u16][entries...]
    size_t buffer_capacity = 4096;
    uint8_t *buffer = (uint8_t *)malloc(buffer_capacity);
    if (buffer == NULL) return 0;

    // Write header first — both values are known upfront
    size_t buffer_size = 0;
    buffer[buffer_size++] = UDX_INDEX_NODE_TYPE_LEAF;
    uint16_t entry_count = (uint16_t)index_size;
    memcpy(buffer + buffer_size, &entry_count, sizeof(uint16_t));
    buffer_size += sizeof(uint16_t);

    // Append serialized entries
    for (size_t i = 0; i < index_size; i++) {
        const udx_db_key_entry *entry = udx_keys_iter_next(ctx->iter);
        if (entry == NULL) {
            free(buffer);
            return 0;
        }

        if (serialize_index_entry_into(entry, &buffer, &buffer_size, &buffer_capacity) == 0) {
            free(buffer);
            return 0;
        }
    }

    // Write compressed block
    uint64_t node_offset = write_index_node(file, buffer, buffer_size);
    free(buffer);
    if (node_offset == 0) return 0;

    // Write next_leaf after compressed block (initially 0)
    uint64_t next_leaf = 0;
    if (fwrite(&next_leaf, sizeof(uint64_t), 1, file) != 1) {
        return 0;
    }

    int64_t current_pos = udx_ftell(file);
    if (current_pos < 0) return 0;

    // Back-patch previous leaf's next_leaf
    if (ctx->last_leaf_link_offset != 0) {
        if (udx_fseek(file, ctx->last_leaf_link_offset, SEEK_SET) != 0 ||
            fwrite(&node_offset, sizeof(uint64_t), 1, file) != 1 ||
            udx_fseek(file, current_pos, SEEK_SET) != 0) {
            return 0;
        }
    }

    // Record current leaf's next_leaf position
    ctx->last_leaf_link_offset = (uint64_t)(current_pos - sizeof(uint64_t));

    // Record first leaf node
    if (ctx->first_leaf_offset == 0) {
        ctx->first_leaf_offset = node_offset;
    }

    *out_height = 1;
    return node_offset;
}

// Recursively build B+ tree node
static uint64_t build_bptree_node(bptree_build_context *ctx, size_t index_size, uint8_t *out_height) {
    uint32_t max_elements = ctx->max_elements;

    // If entry count <= max_elements, build leaf node
    if (index_size <= max_elements) {
        return build_leaf_node(ctx, index_size, out_height);
    }

    // Build internal node with max_elements + 1 children
    FILE *file = ctx->file;
    size_t child_count = max_elements + 1;

    // Guard: child_count must fit in uint16_t
    if (child_count > UINT16_MAX) return 0;

    // Allocate space for children and keys
    uint64_t *children = (uint64_t *)malloc(sizeof(uint64_t) * child_count);
    char **keys = (char **)calloc(max_elements, sizeof(char *));
    if (children == NULL || keys == NULL) {
        free(children);
        free(keys);
        return 0;
    }

    uint64_t node_offset = 0;
    size_t prev_entry = 0;
    uint8_t max_child_height = 0;

    // Build first max_elements children
    for (size_t x = 0; x < max_elements; x++) {
        size_t cur_entry = (uint64_t)index_size * (x + 1) / (max_elements + 1);
        size_t child_size = cur_entry - prev_entry;

        uint8_t child_height = 0;
        children[x] = build_bptree_node(ctx, child_size, &child_height);
        if (children[x] == 0) goto cleanup;
        if (child_height > max_child_height) max_child_height = child_height;

        // Record separator key: peek at iterator's current position
        const udx_db_key_entry *next_entry = udx_keys_iter_peek(ctx->iter);
        if (next_entry == NULL) goto cleanup;
        keys[x] = udx_str_dup(next_entry->key);
        if (keys[x] == NULL) goto cleanup;

        prev_entry = cur_entry;
    }

    // Rightmost child
    uint8_t last_child_height = 0;
    children[max_elements] = build_bptree_node(ctx, index_size - prev_entry, &last_child_height);
    if (children[max_elements] == 0) goto cleanup;
    if (last_child_height > max_child_height) max_child_height = last_child_height;

    // Build internal node data: [node_type:u8] [child_count:u16] [children:u64 × N] [keys\0...]
    size_t keys_data_size = 0;
    for (size_t i = 0; i < max_elements; i++) {
        keys_data_size += strlen(keys[i]) + 1;
    }

    size_t node_size = sizeof(uint8_t) + sizeof(uint16_t) +
                       sizeof(uint64_t) * child_count + keys_data_size;
    uint8_t *node_data = (uint8_t *)malloc(node_size);
    if (node_data == NULL) goto cleanup;

    uint8_t *ptr = node_data;
    *ptr++ = UDX_INDEX_NODE_TYPE_INTERNAL;

    uint16_t cc = (uint16_t)child_count;
    memcpy(ptr, &cc, sizeof(uint16_t));
    ptr += sizeof(uint16_t);

    for (size_t i = 0; i < child_count; i++) {
        memcpy(ptr, &children[i], sizeof(uint64_t));
        ptr += sizeof(uint64_t);
    }

    for (size_t i = 0; i < max_elements; i++) {
        size_t key_len = strlen(keys[i]) + 1;
        memcpy(ptr, keys[i], key_len);
        ptr += key_len;
    }

    node_offset = write_index_node(file, node_data, node_size);
    free(node_data);

    if (node_offset != 0) {
        *out_height = max_child_height + 1;
    }

cleanup:
    for (size_t i = 0; i < max_elements; i++) free(keys[i]);
    free(keys);
    free(children);
    return node_offset;
}

static bool build_bptree(udx_db_builder *db_builder) {
    size_t key_count = udx_keys_count(db_builder->keys);
    // Note: validations (empty, overflow) are done in udx_db_builder_finalize before calling this

    // Calculate max_elements (GoldenDict style)
    uint32_t max_elements = (uint32_t)(sqrt((double)key_count) + 1);
    if (max_elements < UDX_NODE_MIN_ELEMENTS) max_elements = UDX_NODE_MIN_ELEMENTS;
    if (max_elements > UDX_NODE_MAX_ELEMENTS) max_elements = UDX_NODE_MAX_ELEMENTS;

    // Create iterator
    udx_keys_iter *iter = udx_keys_iter_create(db_builder->keys);
    if (iter == NULL) return false;

    // Initialize build context
    bptree_build_context ctx = {0};
    ctx.file = db_builder->writer->file;
    ctx.iter = iter;
    ctx.max_elements = max_elements;

    // Build recursively
    uint8_t tree_height = 0;
    uint64_t root_offset = build_bptree_node(&ctx, key_count, &tree_height);

    udx_keys_iter_destroy(iter);

    if (root_offset == 0) return false;

    db_builder->index_root_offset = root_offset;
    db_builder->index_first_leaf_offset = ctx.first_leaf_offset;
    db_builder->index_bptree_height = tree_height;

    return true;
}

// ============================================================
// Public API Implementation
// ============================================================

udx_writer *udx_writer_open(const char *output_path) {
    if (output_path == NULL) {
        return NULL;
    }

    udx_writer *writer = (udx_writer *)calloc(1, sizeof(udx_writer));
    if (writer == NULL) {
        return NULL;
    }

    writer->file = fopen(output_path, "wb+");
    if (writer->file == NULL) {
        free(writer);
        return NULL;
    }

    // Initialize arrays (calloc zeroed memory, has_db_active already false)
    udx_uint64_array_init(&writer->db_offsets);
    udx_string_array_init(&writer->db_names);

    // Write main file header at the beginning of the file
    udx_header main_header = {0};
    memcpy(main_header.magic, UDX_MAGIC_PREFIX, 4);
    main_header.version_major = UDX_VERSION_MAJOR;
    main_header.version_minor = UDX_VERSION_MINOR;
    main_header.db_count = 0;  // Will be updated at final end
    main_header.db_table_offset = 0;  // Will be updated at final end

    uint8_t header_buf[UDX_HEADER_SERIALIZED_SIZE];
    udx_header_serialize(&main_header, header_buf);
    if (fwrite(header_buf, UDX_HEADER_SERIALIZED_SIZE, 1, writer->file) != 1) {
        fclose(writer->file);
        free(writer);
        return NULL;
    }

    return writer;
}

udx_status_t udx_writer_close(udx_writer *writer) {
    if (writer == NULL) return UDX_OK;

    udx_status_t result = UDX_OK;

    // Caller must finish all builders before closing
    if (writer->has_db_active) {
        result = UDX_ERR_STATE;
        goto cleanup;
    }

    // Write db table at current position (end of file)
    // Format: [(offset:u64, name: null-terminated string)] × db_count
    uint16_t db_count = writer->db_offsets.count;

    int64_t db_table_offset = udx_ftell(writer->file);
    if (db_table_offset < 0) {
        result = UDX_ERR_IO;
        goto cleanup;
    }

    for (uint16_t i = 0; i < db_count; i++) {
        // Write offset
        if (fwrite(&writer->db_offsets.elements[i], sizeof(uint64_t), 1, writer->file) != 1) {
            result = UDX_ERR_IO;
            goto cleanup;
        }

        // Write name (including null terminator)
        const char *name = writer->db_names.elements[i];
        if (name != NULL) {
            size_t len = strlen(name) + 1;
            if (fwrite(name, 1, len, writer->file) != len) {
                result = UDX_ERR_IO;
                goto cleanup;
            }
        } else {
            const char empty = '\0';
            if (fwrite(&empty, 1, 1, writer->file) != 1) {
                result = UDX_ERR_IO;
                goto cleanup;
            }
        }
    }

    // Back-patch main header's db_count and db_table_offset fields
    if (udx_fseek(writer->file, UDX_HEADER_DB_COUNT_POS, SEEK_SET) != 0) {
        result = UDX_ERR_IO;
        goto cleanup;
    }

    if (fwrite(&db_count, sizeof(uint16_t), 1, writer->file) != 1) {
        result = UDX_ERR_IO;
        goto cleanup;
    }

    uint64_t db_table_offset_uint64 = (uint64_t)db_table_offset;
    if (fwrite(&db_table_offset_uint64, sizeof(uint64_t), 1, writer->file) != 1) {
        result = UDX_ERR_IO;
    }

cleanup:
    if (writer->file) fclose(writer->file);

    for (size_t i = 0; i < writer->db_names.count; i++) {
        free(writer->db_names.elements[i]);
    }
    udx_string_array_free(&writer->db_names);
    udx_uint64_array_free(&writer->db_offsets);
    free(writer);

    return result;
}

udx_db_builder *udx_db_builder_create(udx_writer *writer, const char *name) {
    return udx_db_builder_create_with_metadata(writer, name, NULL, 0);
}

udx_db_builder *udx_db_builder_create_with_metadata(udx_writer *writer,
                                                    const char *name,
                                                    const uint8_t *metadata,
                                                    size_t metadata_size) {
    if (writer == NULL || name == NULL) {
        return NULL;
    }

    // Validate metadata parameters
    if (metadata == NULL && metadata_size > 0) {
        return NULL;
    }
    if (metadata_size > UINT32_MAX) {
        return NULL;
    }

    if (writer->has_db_active) {
        return NULL;
    }

    // Check db count limit (uint16_t in header)
    if (writer->db_offsets.count >= UINT16_MAX) {
        return NULL;
    }

    // Check for duplicate name
    for (size_t i = 0; i < writer->db_names.count; i++) {
        if (strcmp(writer->db_names.elements[i], name) == 0) {
            return NULL;  // Duplicate name
        }
    }

    udx_db_builder *db_builder = (udx_db_builder *)calloc(1, sizeof(udx_db_builder));
    if (db_builder == NULL) {
        return NULL;
    }

    db_builder->writer = writer;

    int64_t offset = udx_ftell(writer->file);
    if (offset < 0) {
        goto error;
    }
    db_builder->db_offset = (uint64_t)offset;

    db_builder->keys = udx_keys_create();
    if (db_builder->keys == NULL) {
        goto error;
    }

    db_builder->chunk_writer = udx_chunk_writer_open(writer->file);
    if (db_builder->chunk_writer == NULL) {
        goto error;
    }

    // Reserve header space (write zeros)
    uint8_t zero_buf[UDX_DB_HEADER_SERIALIZED_SIZE] = {0};
    if (fwrite(zero_buf, UDX_DB_HEADER_SERIALIZED_SIZE, 1, writer->file) != 1) {
        goto error;
    }

    // Write metadata immediately after header (if provided)
    if (metadata != NULL && metadata_size > 0) {
        db_builder->metadata_size = (uint32_t)metadata_size;

        if (fwrite(metadata, 1, metadata_size, writer->file) != metadata_size) {
            goto error;
        }
    }

    // Record name and offset in writer
    char *name_copy = udx_str_dup(name);
    if (name_copy == NULL) {
        goto error;
    }
    if (!udx_uint64_array_push(&writer->db_offsets, db_builder->db_offset)) {
        free(name_copy);
        goto error;
    }
    if (!udx_string_array_push(&writer->db_names, name_copy)) {
        // Rollback the offset push to keep arrays in sync
        writer->db_offsets.count--;
        free(name_copy);
        goto error;
    }

    writer->has_db_active = true;
    return db_builder;

error:
    if (db_builder->chunk_writer) udx_chunk_writer_close(db_builder->chunk_writer);
    if (db_builder->keys) udx_keys_destroy(db_builder->keys);
    // Seek file pointer back to the position before this db, so that
    // subsequent operations start from a clean position.
    if (db_builder->db_offset > 0) {
        udx_fseek(writer->file, db_builder->db_offset, SEEK_SET);
    }
    free(db_builder);
    return NULL;
}

udx_status_t udx_db_builder_finalize(udx_db_builder *builder) {
    if (builder == NULL) {
        return UDX_ERR_INVALID_PARAM;
    }

    udx_writer *writer = builder->writer;
    udx_status_t result = UDX_OK;

    // Validate key count
    size_t key_count = udx_keys_count(builder->keys);
    size_t item_count = udx_keys_item_count(builder->keys);

    if (key_count == 0) {
        result = UDX_ERR_INVALID_PARAM;  // Empty databases not allowed
        goto cleanup;
    }
    if (key_count > UINT32_MAX) {
        result = UDX_ERR_OVERFLOW;  // Too many keys for header field
        goto cleanup;
    }
    if (item_count > UINT32_MAX) {
        result = UDX_ERR_OVERFLOW;  // Too many items for header field
        goto cleanup;
    }

    // 1. Finish chunk table (writes chunk table at current position)
    uint64_t chunks_offset = udx_chunk_writer_finish(builder->chunk_writer);
    if (chunks_offset == 0) {
        result = UDX_ERR_INTERNAL;
        goto cleanup;
    }

    // 2. Build B+ tree
    if (!build_bptree(builder)) {
        result = UDX_ERR_INTERNAL;
        goto cleanup;
    }

    // 3. Build complete header with correct offsets
    udx_db_header header = {0};
    header.metadata_size = builder->metadata_size;
    header.chunk_table_offset = chunks_offset;
    header.index_root_offset = builder->index_root_offset;
    header.index_first_leaf_offset = builder->index_first_leaf_offset;
    header.entry_count = (uint32_t)key_count;
    header.item_count = (uint32_t)item_count;
    header.index_bptree_height = builder->index_bptree_height;

    // Calculate checksum on wire bytes (exclude the checksum field itself)
    uint8_t db_header_buf[UDX_DB_HEADER_SERIALIZED_SIZE];
    udx_db_header_serialize(&header, db_header_buf);
    header.checksum = (uint32_t)crc32(0, db_header_buf, UDX_DB_HEADER_CHECKSUM_OFFSET);
    memcpy(db_header_buf + UDX_DB_HEADER_CHECKSUM_OFFSET, &header.checksum, sizeof(uint32_t));

    // 4. Write header back to reserved position
    int64_t current_pos = udx_ftell(writer->file);
    if (current_pos < 0) {
        result = UDX_ERR_HEADER;
        goto cleanup;
    }
    if (udx_fseek(writer->file, builder->db_offset, SEEK_SET) != 0) {
        result = UDX_ERR_HEADER;
        goto cleanup;
    }
    if (fwrite(db_header_buf, UDX_DB_HEADER_SERIALIZED_SIZE, 1, writer->file) != 1) {
        result = UDX_ERR_HEADER;
        goto cleanup;
    }
    if (udx_fseek(writer->file, current_pos, SEEK_SET) != 0) {
        result = UDX_ERR_HEADER;
        goto cleanup;
    }

    fflush(writer->file);

cleanup:
    // Always reset state and free builder
    writer->has_db_active = false;

    if (result != 0) {
        // Rollback: remove the offset/name pushed during create
        if (writer->db_names.count > 0) {
            free(writer->db_names.elements[writer->db_names.count - 1]);
            writer->db_names.count--;
        }
        if (writer->db_offsets.count > 0) {
            writer->db_offsets.count--;
        }
        // Seek file pointer back so subsequent writes start clean
        udx_fseek(writer->file, builder->db_offset, SEEK_SET);
    }

    udx_chunk_writer_close(builder->chunk_writer);
    udx_keys_destroy(builder->keys);
    free(builder);

    return result;
}

udx_status_t udx_db_builder_add_entry(udx_db_builder *builder,
                           const char *key,
                           const uint8_t *value,
                           uint32_t value_size) {
    if (builder == NULL || key == NULL || value == NULL) {
        return UDX_ERR_INVALID_PARAM;
    }

    udx_value_address_t address = udx_chunk_writer_add_block(builder->chunk_writer, value, value_size);
    if (address == UDX_INVALID_ADDRESS) return UDX_ERR_INTERNAL;

    return udx_keys_add(builder->keys, key, address, value_size) ? UDX_OK : UDX_ERR_INTERNAL;
}

udx_value_address_t udx_db_builder_add_value(udx_db_builder *builder,
                                                  const uint8_t *value,
                                                  uint32_t value_size) {
    if (builder == NULL || value == NULL) {
        return UDX_INVALID_ADDRESS;
    }

    return udx_chunk_writer_add_block(builder->chunk_writer, value, value_size);
}

udx_status_t udx_db_builder_add_key_entry(udx_db_builder *builder,
                                          const char *key,
                                          udx_value_address_t value_address,
                                          uint32_t value_size) {
    if (builder == NULL || key == NULL) {
        return UDX_ERR_INVALID_PARAM;
    }

    if (value_address == UDX_INVALID_ADDRESS) {
        return UDX_ERR_INVALID_PARAM;
    }

    return udx_keys_add(builder->keys, key, value_address, value_size) ? UDX_OK : UDX_ERR_INTERNAL;
}

