//
//  udx_writer.h
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#ifndef udx_writer_h
#define udx_writer_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "udx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct udx_writer udx_writer;
typedef struct udx_db_builder udx_db_builder;

// ============================================================
// File-level Writer API
// ============================================================

/**
 * Open a new UDX file for writing
 * @param output_path Path to the output file (will be created or truncated)
 * @return Writer pointer, or NULL on failure (UDX_ERR_MEMORY)
 *
 * @note Only one database can be built at a time per writer
 * @note Call udx_writer_close() to finalize and close the file
 */
udx_writer *udx_writer_open(const char *output_path);

/**
 * Close the writer and finalize the UDX file
 * @param writer Writer pointer
 * @return UDX_OK on success, error code on failure:
 *         UDX_ERR_STATE: a database builder is still active
 *         UDX_ERR_IO: file I/O error
 *
 * @note All builders must be finished before closing
 */
udx_status udx_writer_close(udx_writer *writer);

// ============================================================
// Database Builder API
// ============================================================

/**
 * Create a database builder (without metadata)
 * @param writer Writer pointer
 * @param name Database name (must be unique within the file)
 * @return Builder pointer, or NULL on failure (UDX_ERR_MEMORY, UDX_ERR_STATE)
 *
 * @note This is equivalent to calling udx_db_builder_create_with_metadata
 *       with metadata=NULL and metadata_size=0
 */
udx_db_builder *udx_db_builder_create(udx_writer *writer, const char *name);

/**
 * Create a database builder with metadata
 * @param writer Writer pointer
 * @param name Database name (must be unique within the file)
 * @param metadata Metadata bytes (can be NULL if metadata_size is 0)
 * @param metadata_size Size of metadata in bytes (must be 0 if metadata is NULL)
 * @return Builder pointer, or NULL on failure (UDX_ERR_MEMORY, UDX_ERR_STATE)
 *
 * @note Constraint: if metadata is NULL, metadata_size MUST be 0.
 *       Conversely, if metadata_size > 0, metadata MUST NOT be NULL.
 * @note Metadata is stored immediately after the database header
 * @note Only one builder can be active at a time per writer
 */
udx_db_builder *udx_db_builder_create_with_metadata(udx_writer *writer,
                                                    const char *name,
                                                    const uint8_t *metadata,
                                                    size_t metadata_size);

/**
 * Finalize building the database
 * @param builder Builder pointer
 * @return UDX_OK on success, error code on failure:
 *         UDX_ERR_INVALID_PARAM: invalid parameter or empty database (no entries added)
 *         UDX_ERR_INTERNAL: chunk writer failed
 *         UDX_ERR_INTERNAL: B+ tree build failed
 *         UDX_ERR_HEADER: header write failed
 *
 * @note After this call, the builder is freed and must not be used again
 * @note Empty databases (with no entries) are not allowed and will return UDX_ERR_INVALID_PARAM
 */
udx_status udx_db_builder_finalize(udx_db_builder *builder);

/**
 * Add an entry to the database
 * @param builder Builder pointer
 * @param key Key string (UTF-8, will be folded for case-insensitive lookup)
 * @param value Value bytes
 * @param value_size Size of value in bytes (maximum: 4 GB, recommended: < 64 KB)
 * @return UDX_OK on success, error code on failure:
 *         UDX_ERR_INVALID_PARAM: invalid parameter or value_size exceeds maximum
 *         UDX_ERR_INTERNAL: chunk writer failed
 *         UDX_ERR_INTERNAL: keys container failed
 *
 * @note Multiple entries can be added under the same key
 * @note The original key case is preserved
 * @note Data sizes < 64 KB allow multiple entries to share a chunk, improving compression
 *
 * === When to use ===
 * Use this function for simple one-key-to-one-data mappings:
 * - Each key has its own unique definition/data
 * - No need to share data between multiple keys
 * - Most common use case for dictionary building
 *
 * === When NOT to use ===
 * Do NOT use this function when:
 * - Multiple keys need to reference the same data (e.g., key alternates)
 * - You need to add multiple keys pointing to identical data
 * - In these cases, use udx_db_builder_add_value + udx_db_builder_add_key_entry instead
 *
 * Example:
 * @code
 *   udx_db_builder_add_entry(builder, "apple", data1, size1);  // Simple entry
 *   udx_db_builder_add_entry(builder, "banana", data2, size2); // Another simple entry
 * @endcode
 */
udx_status udx_db_builder_add_entry(udx_db_builder *builder,
                           const char *key,
                           const uint8_t *value,
                           uint32_t value_size);

/**
 * @brief Store value in chunk storage (returns address for key entries)
 * @param builder Builder pointer
 * @param value Value bytes
 * @param value_size Size of value in bytes (maximum: 4 GB, recommended: < 64 KB)
 * @return Value address on success, UDX_INVALID_ADDRESS on failure
 *
 * @note This function only writes data to chunk storage
 * @note Use udx_db_builder_add_key_entry to add key references to this chunk
 * @note Multiple keys can reference the same chunk address (for alternates)
 *
 * === When to use ===
 * Use this function with udx_db_builder_add_key_entry for advanced scenarios:
 * - Multiple keys need to reference the same data (e.g., key alternates, synonyms)
 * - You want to avoid duplicating identical data in the database
 * - Building dictionary with key variants that share definitions
 *
 * === When NOT to use ===
 * Do NOT use this function alone when:
 * - You have a simple one-key-to-one-data mapping
 * - In these cases, use udx_db_builder_add_entry instead for simplicity
 *
 * Example (adding key with alternates):
 * @code
 *   // Store definition data once
 *   udx_value_address_t addr = udx_db_builder_add_value(builder, def_data, def_size);
 *
 *   // Add main key
 *   udx_db_builder_add_key_entry(builder, "colour", addr, def_size);
 *
 *   // Add alternate spellings pointing to same data
 *   udx_db_builder_add_key_entry(builder, "color", addr, def_size);
 * @endcode
 */
udx_value_address udx_db_builder_add_value(udx_db_builder *builder,
                                                  const uint8_t *value,
                                                  uint32_t value_size);

/**
 * @brief Add key entry referencing stored value
 * @param builder Builder pointer
 * @param key Key string (UTF-8, will be folded for case-insensitive lookup)
 * @param value_address Address of data in chunk storage (from udx_db_builder_add_value)
 * @param value_size Size of value in bytes
 * @return UDX_OK on success, error code on failure:
 *         UDX_ERR_INVALID_PARAM: invalid parameter
 *         UDX_ERR_INTERNAL: keys container failed
 *
 * @note This function only adds key->address mapping to index
 * @note Use udx_db_builder_add_value first to store data
 * @note Multiple keys can reference the same value_address (for alternates)
 *
 * === When to use ===
 * This function must be used together with udx_db_builder_add_value:
 * - After calling udx_db_builder_add_value to store data
 * - To add multiple keys referencing the same data
 * - To implement key alternates, synonyms, or spelling variants
 *
 * === When NOT to use ===
 * Do NOT use this function when:
 * - You haven't called udx_db_builder_add_value first
 * - You have a simple one-key-to-one-data mapping (use udx_db_builder_add_entry instead)
 * - The value_address is invalid or UDX_INVALID_ADDRESS
 *
 * Example (BGL dictionary with alternates):
 * @code
 *   // In BGL, a key has multiple alternates that share the same definition
 *   ud_bgl_entry entry;
 *   ud_bgl_parse_entry(reader, block, &entry);
 *
 *   // Store definition once
 *   udx_value_address_t addr = udx_db_builder_add_value(builder,
 *                                                           entry.definition,
 *                                                           entry.def_len);
 *
 *   // Add main key
 *   udx_db_builder_add_key_entry(builder, entry.key, addr, entry.def_len);
 *
 *   // Add all alternates pointing to same definition
 *   for (int i = 0; i < entry.alternate_count; i++) {
 *       udx_db_builder_add_key_entry(builder, entry.alternates[i], addr, entry.def_len);
 *   }
 * @endcode
 */
udx_status udx_db_builder_add_key_entry(udx_db_builder *builder,
                                          const char *key,
                                          udx_value_address value_address,
                                          uint32_t value_size);


#ifdef __cplusplus
}
#endif

#endif /* udx_writer_h */
