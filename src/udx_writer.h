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
 *         UDX_ERR_ACTIVE_DB: a database builder is still active
 *         UDX_ERR_IO: file I/O error
 *
 * @note All builders must be finished before closing
 */
udx_error_t udx_writer_close(udx_writer *writer);

// ============================================================
// Database Builder API
// ============================================================

/**
 * Create a database builder (without metadata)
 * @param writer Writer pointer
 * @param name Database name (must be unique within the file)
 * @return Builder pointer, or NULL on failure (UDX_ERR_MEMORY, UDX_ERR_ACTIVE_DB, UDX_ERR_DUPLICATE_NAME)
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
 * @return Builder pointer, or NULL on failure (UDX_ERR_MEMORY, UDX_ERR_ACTIVE_DB,
 *         UDX_ERR_DUPLICATE_NAME, UDX_ERR_METADATA)
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
 *         UDX_ERR_CHUNK: chunk writer failed
 *         UDX_ERR_BPTREE: B+ tree build failed
 *         UDX_ERR_HEADER: header write failed
 *
 * @note After this call, the builder is freed and must not be used again
 * @note Empty databases (with no entries) are not allowed and will return UDX_ERR_INVALID_PARAM
 */
udx_error_t udx_db_builder_finalize(udx_db_builder *builder);

/**
 * Add an entry to the database
 * @param builder Builder pointer
 * @param word Word string (UTF-8, will be folded for case-insensitive lookup)
 * @param data Data bytes
 * @param data_size Size of data in bytes (maximum: 4 GB, recommended: < 64 KB)
 * @return UDX_OK on success, error code on failure:
 *         UDX_ERR_INVALID_PARAM: invalid parameter or data_size exceeds maximum
 *         UDX_ERR_CHUNK: chunk writer failed
 *         UDX_ERR_WORDS: words container failed
 *
 * @note Multiple entries can be added under the same word
 * @note The original word case is preserved
 * @note Data sizes < 64 KB allow multiple entries to share a chunk, improving compression
 *
 * === When to use ===
 * Use this function for simple one-word-to-one-data mappings:
 * - Each word has its own unique definition/data
 * - No need to share data between multiple words
 * - Most common use case for dictionary building
 *
 * === When NOT to use ===
 * Do NOT use this function when:
 * - Multiple words need to reference the same data (e.g., word alternates)
 * - You need to add multiple words pointing to identical data
 * - In these cases, use udx_db_builder_add_chunk_block + udx_db_builder_add_word_entry instead
 *
 * Example:
 * @code
 *   udx_db_builder_add_entry(builder, "apple", data1, size1);  // Simple entry
 *   udx_db_builder_add_entry(builder, "banana", data2, size2); // Another simple entry
 * @endcode
 */
udx_error_t udx_db_builder_add_entry(udx_db_builder *builder,
                           const char *word,
                           const uint8_t *data,
                           uint32_t data_size);

/**
 * @brief Add data chunk to storage (returns address for word entries)
 * @param builder Builder pointer
 * @param data Data bytes
 * @param data_size Size of data in bytes (maximum: 4 GB, recommended: < 64 KB)
 * @return Chunk address on success, UDX_INVALID_ADDRESS on failure
 *
 * @note This function only writes data to chunk storage
 * @note Use udx_db_builder_add_word_entry to add word references to this chunk
 * @note Multiple words can reference the same chunk address (for alternates)
 *
 * === When to use ===
 * Use this function with udx_db_builder_add_word_entry for advanced scenarios:
 * - Multiple words need to reference the same data (e.g., word alternates, synonyms)
 * - You want to avoid duplicating identical data in the database
 * - Building dictionary with word variants that share definitions
 *
 * === When NOT to use ===
 * Do NOT use this function alone when:
 * - You have a simple one-word-to-one-data mapping
 * - In these cases, use udx_db_builder_add_entry instead for simplicity
 *
 * Example (adding word with alternates):
 * @code
 *   // Store definition data once
 *   udx_chunk_address addr = udx_db_builder_add_chunk_block(builder, def_data, def_size);
 *
 *   // Add main word
 *   udx_db_builder_add_word_entry(builder, "colour", addr, def_size);
 *
 *   // Add alternate spellings pointing to same data
 *   udx_db_builder_add_word_entry(builder, "color", addr, def_size);
 * @endcode
 */
udx_chunk_address udx_db_builder_add_chunk_block(udx_db_builder *builder,
                                                  const uint8_t *data,
                                                  uint32_t data_size);

/**
 * @brief Add word entry referencing existing chunk data
 * @param builder Builder pointer
 * @param word Word string (UTF-8, will be folded for case-insensitive lookup)
 * @param data_address Address of data in chunk storage (from udx_db_builder_add_chunk_block)
 * @param data_size Size of data in bytes
 * @return UDX_OK on success, error code on failure:
 *         UDX_ERR_INVALID_PARAM: invalid parameter
 *         UDX_ERR_WORDS: words container failed
 *
 * @note This function only adds word->address mapping to index
 * @note Use udx_db_builder_add_chunk_block first to store data
 * @note Multiple words can reference the same data_address (for alternates)
 *
 * === When to use ===
 * This function must be used together with udx_db_builder_add_chunk_block:
 * - After calling udx_db_builder_add_chunk_block to store data
 * - To add multiple words referencing the same data
 * - To implement word alternates, synonyms, or spelling variants
 *
 * === When NOT to use ===
 * Do NOT use this function when:
 * - You haven't called udx_db_builder_add_chunk_block first
 * - You have a simple one-word-to-one-data mapping (use udx_db_builder_add_entry instead)
 * - The data_address is invalid or UDX_INVALID_ADDRESS
 *
 * Example (BGL dictionary with alternates):
 * @code
 *   // In BGL, a word has multiple alternates that share the same definition
 *   ud_bgl_entry entry;
 *   ud_bgl_parse_entry(reader, block, &entry);
 *
 *   // Store definition once
 *   udx_chunk_address addr = udx_db_builder_add_chunk_block(builder,
 *                                                           entry.definition,
 *                                                           entry.def_len);
 *
 *   // Add main word
 *   udx_db_builder_add_word_entry(builder, entry.word, addr, entry.def_len);
 *
 *   // Add all alternates pointing to same definition
 *   for (int i = 0; i < entry.alternate_count; i++) {
 *       udx_db_builder_add_word_entry(builder, entry.alternates[i], addr, entry.def_len);
 *   }
 * @endcode
 */
udx_error_t udx_db_builder_add_word_entry(udx_db_builder *builder,
                                          const char *word,
                                          udx_chunk_address data_address,
                                          uint32_t data_size);


#ifdef __cplusplus
}
#endif

#endif /* udx_writer_h */
