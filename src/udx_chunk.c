//
//  ud_chunk.c
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#include "udx_chunk.h"
#include "udx_types.h"
#include "udx_utils.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Address Encoding/Decoding Functions
// ============================================================

// Make address from chunk_index and offset
// chunk_index: 32-bit (0-4.29 billion), max 256 TB with 64KB chunks
// offset: 16-bit (0-65535), max 64KB per chunk
static inline udx_chunk_address udx_addr_make(uint32_t chunk_index, uint16_t offset) {
    return ((uint64_t)chunk_index << 16) | offset;
}

// Get chunk_index from address
static inline uint32_t udx_addr_get_chunk(udx_chunk_address addr) {
    return (uint32_t)(addr >> 16);
}

// Get offset from address (same for both formats)
static inline uint16_t udx_addr_get_offset(udx_chunk_address addr) {
    return (uint16_t)(addr & 0xFFFF);
}

// ============================================================
// Chunk Writer Implementation
// ============================================================

struct udx_chunk_writer {
    FILE *file;                     // Output file

    uint8_t *buffer;                // Current chunk buffer
    uint32_t buffer_size;           // Buffer used size
    uint32_t buffer_capacity;       // Buffer capacity

    uint64_t *offsets;              // Chunk offset table
    uint32_t offset_count;          // Number of saved chunks
    uint32_t offset_capacity;       // Offset table capacity

    uint32_t current_chunk_index;   // Index of the next chunk to write (32-bit limit, max 256 TB with 64KB chunks)
};

/**
 * Save current chunk (compress and write to file)
 */
static bool chunk_writer_save_current(udx_chunk_writer *writer) {
    if (writer->buffer_size == 0) {
        return true;
    }

    // Record the file offset of the current chunk
    int64_t offset = udx_ftell(writer->file);
    if (offset < 0) {
        return false;
    }

    // Expand offset table (reserve space, but don't commit yet)
    if (writer->offset_count >= writer->offset_capacity) {
        // Calculate new capacity with overflow check
        uint32_t new_capacity;
        if (writer->offset_capacity == 0) {
            new_capacity = 64;
        } else {
            // Check multiplication overflow before doing it
            if (writer->offset_capacity > UINT32_MAX / 2) {
                return false;
            }
            new_capacity = writer->offset_capacity * 2;
        }

        // Check if sizeof(uint64_t) * new_capacity would overflow size_t
        // Only relevant on 32-bit systems where size_t is 32 bits
#if UINTPTR_MAX == 0xFFFFFFFF
        if (new_capacity > SIZE_MAX / sizeof(uint64_t)) {
            return false;
        }
#endif

        uint64_t *new_offsets = (uint64_t *)realloc(writer->offsets,
                                                     sizeof(uint64_t) * new_capacity);
        if (new_offsets == NULL) {
            return false;
        }
        writer->offsets = new_offsets;
        writer->offset_capacity = new_capacity;
    }

    // Compress data
    uLong compressed_bound = compressBound((uLong)writer->buffer_size);
    uint8_t *compressed = (uint8_t *)malloc(compressed_bound);
    if (compressed == NULL) {
        return false;
    }

    uLong compressed_size = compressed_bound;
    int ret = compress2(compressed, &compressed_size,
                        writer->buffer, (uLong)writer->buffer_size,
                        Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        free(compressed);
        return false;
    }

    // Write: [uncompressed_size:u32] [compressed_size:u32] [data]
    uint32_t uncompressed_size = (uint32_t)writer->buffer_size;
    uint32_t comp_size = (uint32_t)compressed_size;

    if (fwrite(&uncompressed_size, sizeof(uint32_t), 1, writer->file) != 1 ||
        fwrite(&comp_size, sizeof(uint32_t), 1, writer->file) != 1 ||
        fwrite(compressed, 1, compressed_size, writer->file) != compressed_size) {
        free(compressed);
        return false;
    }

    free(compressed);

    // All writes succeeded — now commit the offset entry
    writer->offsets[writer->offset_count++] = (uint64_t)offset;

    // Reset buffer
    writer->buffer_size = 0;

    // Check 32-bit chunk index limit
    if (writer->current_chunk_index >= UINT32_MAX) {
        return false;  // Would overflow 32-bit limit
    }
    writer->current_chunk_index++;

    return true;
}

udx_chunk_writer *udx_chunk_writer_open(FILE *file) {
    if (file == NULL) {
        return NULL;
    }

    udx_chunk_writer *writer = (udx_chunk_writer *)calloc(1, sizeof(udx_chunk_writer));
    if (writer == NULL) {
        return NULL;
    }

    writer->file = file;
    writer->buffer_capacity = UDX_CHUNK_MAX_SIZE;
    writer->buffer = (uint8_t *)malloc(writer->buffer_capacity);
    if (writer->buffer == NULL) {
        free(writer);
        return NULL;
    }

    return writer;
}

udx_chunk_address udx_chunk_writer_add_block(udx_chunk_writer *writer,
                                              const uint8_t *data,
                                              uint32_t size) {
    if (writer == NULL || data == NULL || size == 0) {
        return UDX_INVALID_ADDRESS;
    }

    // Check for addition overflow before calculation
    if (size > UINT32_MAX - writer->buffer_size) {
        return UDX_INVALID_ADDRESS;  // buffer_size + size would overflow uint32
    }

    // Flush if current offset would exceed uint16 range
    if (writer->buffer_size >= UDX_CHUNK_MAX_SIZE) {
        if (!chunk_writer_save_current(writer)) {
            return UDX_INVALID_ADDRESS;
        }
    }

    // Expand buffer if needed (for blocks larger than default capacity)
    uint32_t required = writer->buffer_size + size;
    if (required > writer->buffer_capacity) {
        uint8_t *new_buffer = (uint8_t *)realloc(writer->buffer, required);
        if (new_buffer == NULL) {
            return UDX_INVALID_ADDRESS;
        }
        writer->buffer = new_buffer;
        writer->buffer_capacity = required;
    }

    // Record address before appending
    // buffer_size is guaranteed < UDX_CHUNK_MAX_SIZE after flush check
    udx_chunk_address address = udx_addr_make(writer->current_chunk_index,
                                               (uint16_t)writer->buffer_size);

    // Append data
    memcpy(writer->buffer + writer->buffer_size, data, size);
    writer->buffer_size += size;

    return address;
}

uint64_t udx_chunk_writer_finish(udx_chunk_writer *writer) {
    if (writer == NULL) {
        return 0;
    }

    // Save the last chunk
    if (writer->buffer_size > 0) {
        if (!chunk_writer_save_current(writer)) {
            return 0;
        }
    }

    // Record chunk table position
    int64_t table_offset = udx_ftell(writer->file);
    if (table_offset < 0) {
        return 0;
    }

    // Write chunk table: [count:u64] [offset_0:u64] [offset_1:u64] ...
    uint64_t count = writer->offset_count;
    if (fwrite(&count, sizeof(uint64_t), 1, writer->file) != 1) {
        return 0;
    }

    if (count > 0) {
        if (fwrite(writer->offsets, sizeof(uint64_t), count, writer->file) != count) {
            return 0;
        }
    }

    return (uint64_t)table_offset;
}

void udx_chunk_writer_close(udx_chunk_writer *writer) {
    if (writer == NULL) {
        return;
    }
    free(writer->buffer);
    free(writer->offsets);
    free(writer);
}

// ============================================================
// Chunk Reader Implementation
// ============================================================

struct udx_chunk_reader {
    FILE *file;                 // Input file
    uint64_t *offsets;          // Chunk offset table
    uint64_t chunk_count;       // Number of chunks

    // Cache for the most recently read chunk
    uint8_t *cached_data;       // Decompressed data
    uint32_t cached_size;       // Decompressed size
    uint64_t cached_index;      // Cached chunk index
    bool has_cache;             // Whether cache is valid
};

udx_chunk_reader *udx_chunk_reader_create(FILE *file, uint64_t table_offset) {
    if (file == NULL) {
        return NULL;
    }

    udx_chunk_reader *reader = (udx_chunk_reader *)calloc(1, sizeof(udx_chunk_reader));
    if (reader == NULL) {
        return NULL;
    }

    reader->file = file;

    // Read chunk table
    if (udx_fseek(file, table_offset, SEEK_SET) != 0) {
        free(reader);
        return NULL;
    }

    if (fread(&reader->chunk_count, sizeof(uint64_t), 1, file) != 1) {
        free(reader);
        return NULL;
    }

    if (reader->chunk_count > 0) {
        reader->offsets = (uint64_t *)malloc(sizeof(uint64_t) * reader->chunk_count);
        if (reader->offsets == NULL) {
            free(reader);
            return NULL;
        }

        if (fread(reader->offsets, sizeof(uint64_t), reader->chunk_count, file) != reader->chunk_count) {
            free(reader->offsets);
            free(reader);
            return NULL;
        }
    }

    return reader;
}

void udx_chunk_reader_destroy(udx_chunk_reader *reader) {
    if (reader == NULL) {
        return;
    }

    free(reader->offsets);
    free(reader->cached_data);
    free(reader);
}

/**
 * Read and decompress the specified chunk
 */
static bool chunk_reader_load_chunk(udx_chunk_reader *reader, uint64_t chunk_index) {
    if (chunk_index >= reader->chunk_count) {
        return false;
    }
    
    // Check cache
    if (reader->has_cache && reader->cached_index == chunk_index) {
        return true;
    }

    // Seek to chunk
    if (udx_fseek(reader->file, reader->offsets[chunk_index], SEEK_SET) != 0) {
        return false;
    }

    // Invalidate cache before loading new chunk
    reader->has_cache = false;

    // Read header
    uint32_t uncompressed_size, compressed_size;
    if (fread(&uncompressed_size, sizeof(uint32_t), 1, reader->file) != 1 ||
        fread(&compressed_size, sizeof(uint32_t), 1, reader->file) != 1) {
        return false;
    }

    // Sanity check: reject obviously invalid sizes
    if (uncompressed_size == 0 || compressed_size == 0) {
        return false;
    }

    // Read compressed data
    uint8_t *compressed = (uint8_t *)malloc(compressed_size);
    if (compressed == NULL) {
        return false;
    }
    
    if (fread(compressed, 1, compressed_size, reader->file) != compressed_size) {
        free(compressed);
        return false;
    }
    
    // Decompress
    free(reader->cached_data);
    reader->cached_data = (uint8_t *)malloc(uncompressed_size);
    if (reader->cached_data == NULL) {
        free(compressed);
        return false;
    }
    
    uLong dest_len = uncompressed_size;
    int ret = uncompress(reader->cached_data, &dest_len, compressed, compressed_size);
    free(compressed);
    
    if (ret != Z_OK || dest_len != uncompressed_size) {
        free(reader->cached_data);
        reader->cached_data = NULL;
        return false;
    }
    
    reader->cached_size = uncompressed_size;
    reader->cached_index = chunk_index;
    reader->has_cache = true;
    
    return true;
}

uint8_t *udx_chunk_reader_get_block(udx_chunk_reader *reader,
                                    udx_chunk_address address,
                                    uint32_t data_size) {
    if (reader == NULL || data_size == 0) {
        return NULL;
    }

    uint64_t chunk_index = udx_addr_get_chunk(address);
    uint16_t offset = udx_addr_get_offset(address);

    // Load chunk
    if (!chunk_reader_load_chunk(reader, chunk_index)) {
        return NULL;
    }

    // Validate offset + data_size within chunk bounds
    // Cast to uint64_t first to avoid uint16_t + uint32_t wrapping
    if ((uint64_t)offset + data_size > reader->cached_size) {
        return NULL;
    }

    // Copy exact data
    uint8_t *data = (uint8_t *)malloc(data_size);
    if (data == NULL) {
        return NULL;
    }

    memcpy(data, reader->cached_data + offset, data_size);

    return data;
}

uint64_t udx_chunk_reader_get_chunk_count(const udx_chunk_reader *reader) {
    if (reader == NULL) {
        return 0;
    }
    return reader->chunk_count;
}
