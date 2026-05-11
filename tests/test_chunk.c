//
//  test_chunk.c
//  libudx tests
//
//  Chunk storage tests using Unity framework
//

#include "unity.h"
#include "udx_chunk.h"
#include "test_platform.h"
#include <stdlib.h>
#include <string.h>

// ============================================================
// Writer Tests
// ============================================================

void test_chunk_writer_open(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    FILE* file = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(file);

    udx_chunk_writer* writer = udx_chunk_writer_open(file);
    TEST_ASSERT_NOT_NULL(writer);

    udx_chunk_writer_close(writer);
    fclose(file);
    unlink(filename);
}

void test_chunk_writer_open_null_file(void) {
    udx_chunk_writer* writer = udx_chunk_writer_open(NULL);
    TEST_ASSERT_NULL(writer);
}

void test_chunk_writer_add_single_block(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    FILE* file = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(file);

    udx_chunk_writer* writer = udx_chunk_writer_open(file);
    TEST_ASSERT_NOT_NULL(writer);

    const uint8_t data[] = "Hello, World!";
    udx_value_address_t addr = udx_chunk_writer_add_block(writer, data, (uint32_t)(sizeof(data) - 1));
    TEST_ASSERT_TRUE_MESSAGE(addr != UDX_INVALID_ADDRESS, "address should be valid");

    uint64_t table_offset = udx_chunk_writer_finish(writer);
    TEST_ASSERT_TRUE_MESSAGE(table_offset != 0, "table offset should be valid");

    udx_chunk_writer_close(writer);
    fclose(file);
    unlink(filename);
}

void test_chunk_writer_add_multiple_blocks(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    FILE* file = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(file);

    udx_chunk_writer* writer = udx_chunk_writer_open(file);
    TEST_ASSERT_NOT_NULL(writer);

    // Add multiple blocks
    for (int i = 0; i < 10; i++) {
        uint8_t data[128];
        memset(data, i, sizeof(data));
        udx_value_address_t addr = udx_chunk_writer_add_block(writer, data, (uint32_t)sizeof(data));
        TEST_ASSERT_TRUE(addr != UDX_INVALID_ADDRESS);
    }

    uint64_t table_offset = udx_chunk_writer_finish(writer);
    TEST_ASSERT_TRUE(table_offset != 0);

    udx_chunk_writer_close(writer);
    fclose(file);
    unlink(filename);
}

void test_chunk_writer_add_null_data(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    FILE* file = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(file);

    udx_chunk_writer* writer = udx_chunk_writer_open(file);
    TEST_ASSERT_NOT_NULL(writer);

    udx_value_address_t addr = udx_chunk_writer_add_block(writer, NULL, 100);
    TEST_ASSERT_TRUE_MESSAGE(addr == UDX_INVALID_ADDRESS, "null data should return invalid address");

    udx_chunk_writer_close(writer);
    fclose(file);
    unlink(filename);
}

void test_chunk_writer_add_zero_size(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    FILE* file = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(file);

    udx_chunk_writer* writer = udx_chunk_writer_open(file);
    TEST_ASSERT_NOT_NULL(writer);

    uint8_t data[] = "test";
    udx_value_address_t addr = udx_chunk_writer_add_block(writer, data, 0);
    TEST_ASSERT_TRUE_MESSAGE(addr == UDX_INVALID_ADDRESS, "zero size should return invalid address");

    udx_chunk_writer_close(writer);
    fclose(file);
    unlink(filename);
}

void test_chunk_writer_large_block(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    FILE* file = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(file);

    udx_chunk_writer* writer = udx_chunk_writer_open(file);
    TEST_ASSERT_NOT_NULL(writer);

    // Add a block larger than default chunk size
    uint8_t* large_data = (uint8_t*)malloc(100000);
    TEST_ASSERT_NOT_NULL(large_data);
    memset(large_data, 0xAB, 100000);

    udx_value_address_t addr = udx_chunk_writer_add_block(writer, large_data, 100000);
    TEST_ASSERT_TRUE_MESSAGE(addr != UDX_INVALID_ADDRESS, "large block address should be valid");

    free(large_data);

    uint64_t table_offset = udx_chunk_writer_finish(writer);
    TEST_ASSERT_TRUE(table_offset != 0);

    udx_chunk_writer_close(writer);
    fclose(file);
    unlink(filename);
}

void test_chunk_writer_chunk_overflow(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    FILE* file = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(file);

    udx_chunk_writer* writer = udx_chunk_writer_open(file);
    TEST_ASSERT_NOT_NULL(writer);

    // Add blocks that will exceed chunk size (should auto-flush)
    uint8_t data[30000];
    memset(data, 0xAA, sizeof(data));

    for (int i = 0; i < 5; i++) {
        udx_value_address_t addr = udx_chunk_writer_add_block(writer, data, (uint32_t)sizeof(data));
        TEST_ASSERT_TRUE(addr != UDX_INVALID_ADDRESS);
    }

    uint64_t table_offset = udx_chunk_writer_finish(writer);
    TEST_ASSERT_TRUE(table_offset != 0);

    udx_chunk_writer_close(writer);
    fclose(file);
    unlink(filename);
}

void test_chunk_writer_close_null(void) {
    udx_chunk_writer_close(NULL);  // Should not crash
    TEST_PASS();
}

// ============================================================
// Reader Tests
// ============================================================

void test_chunk_reader_create(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    // Write test data
    FILE* file = fopen(filename, "wb");
    udx_chunk_writer* writer = udx_chunk_writer_open(file);
    uint8_t data[] = "test data";
    udx_chunk_writer_add_block(writer, data, (uint32_t)sizeof(data));
    uint64_t table_offset = udx_chunk_writer_finish(writer);
    udx_chunk_writer_close(writer);
    fclose(file);

    // Read it back
    file = fopen(filename, "rb");
    udx_chunk_reader* reader = udx_chunk_reader_create(file, table_offset);
    TEST_ASSERT_NOT_NULL(reader);

    udx_chunk_reader_destroy(reader);
    fclose(file);
    unlink(filename);
}

void test_chunk_reader_create_null_file(void) {
    udx_chunk_reader* reader = udx_chunk_reader_create(NULL, 0);
    TEST_ASSERT_NULL(reader);
}

void test_chunk_reader_get_block(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    // Write test data
    FILE* file = fopen(filename, "wb");
    udx_chunk_writer* writer = udx_chunk_writer_open(file);

    const char* test_str = "Hello, Chunk World!";
    udx_value_address_t addr = udx_chunk_writer_add_block(
        writer, (const uint8_t*)test_str, (uint32_t)(strlen(test_str) + 1)
    );
    uint64_t table_offset = udx_chunk_writer_finish(writer);
    udx_chunk_writer_close(writer);
    fclose(file);

    // Read it back
    file = fopen(filename, "rb");
    udx_chunk_reader* reader = udx_chunk_reader_create(file, table_offset);
    TEST_ASSERT_NOT_NULL(reader);

    uint8_t* data = udx_chunk_reader_get_block(reader, addr, (uint32_t)(strlen(test_str) + 1));
    TEST_ASSERT_NOT_NULL_MESSAGE(data, "data should be read");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_str, (const char*)data, "data should match");

    free(data);
    udx_chunk_reader_destroy(reader);
    fclose(file);
    unlink(filename);
}

void test_chunk_reader_get_chunk_count(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    // Write data that spans multiple chunks
    FILE* file = fopen(filename, "wb");
    udx_chunk_writer* writer = udx_chunk_writer_open(file);

    uint8_t data[50000];
    memset(data, 0xAA, sizeof(data));

    // Add enough data to create multiple chunks
    for (int i = 0; i < 3; i++) {
        udx_value_address_t addr = udx_chunk_writer_add_block(writer, data, (uint32_t)sizeof(data));
        TEST_ASSERT_TRUE(addr != UDX_INVALID_ADDRESS);
    }

    uint64_t table_offset = udx_chunk_writer_finish(writer);
    udx_chunk_writer_close(writer);
    fclose(file);

    // Read and verify chunk count
    file = fopen(filename, "rb");
    udx_chunk_reader* reader = udx_chunk_reader_create(file, table_offset);

    uint64_t chunk_count = udx_chunk_reader_get_chunk_count(reader);
    TEST_ASSERT_TRUE_MESSAGE(chunk_count > 1, "should have more than 1 chunk");

    udx_chunk_reader_destroy(reader);
    fclose(file);
    unlink(filename);
}

void test_chunk_reader_destroy_null(void) {
    udx_chunk_reader_destroy(NULL);  // Should not crash
    TEST_PASS();
}

// ============================================================
// Round-trip Tests
// ============================================================

void test_chunk_roundtrip_binary_data(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    // Write binary data
    FILE* file = fopen(filename, "wb");
    udx_chunk_writer* writer = udx_chunk_writer_open(file);

    uint8_t binary_data[256];
    for (int i = 0; i < 256; i++) {
        binary_data[i] = (uint8_t)i;
    }

    udx_value_address_t addr = udx_chunk_writer_add_block(writer, binary_data, (uint32_t)sizeof(binary_data));
    uint64_t table_offset = udx_chunk_writer_finish(writer);
    udx_chunk_writer_close(writer);
    fclose(file);

    // Read and verify
    file = fopen(filename, "rb");
    udx_chunk_reader* reader = udx_chunk_reader_create(file, table_offset);

    uint8_t* read_data = udx_chunk_reader_get_block(reader, addr, (uint32_t)sizeof(binary_data));
    TEST_ASSERT_NOT_NULL(read_data);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(binary_data, read_data, (uint32_t)sizeof(binary_data), "binary data should match");

    free(read_data);
    udx_chunk_reader_destroy(reader);
    fclose(file);
    unlink(filename);
}

void test_chunk_roundtrip_unicode(void) {
    const char* filename = "test_chunk.udx";
    unlink(filename);

    // Write UTF-8 data
    FILE* file = fopen(filename, "wb");
    udx_chunk_writer* writer = udx_chunk_writer_open(file);

    const char* unicode_str = "Hello 世界 🌍 Ñoño";
    uint32_t len = (uint32_t)(strlen(unicode_str) + 1);

    udx_value_address_t addr = udx_chunk_writer_add_block(
        writer, (const uint8_t*)unicode_str, len
    );
    uint64_t table_offset = udx_chunk_writer_finish(writer);
    udx_chunk_writer_close(writer);
    fclose(file);

    // Read and verify
    file = fopen(filename, "rb");
    udx_chunk_reader* reader = udx_chunk_reader_create(file, table_offset);

    uint8_t* read_data = udx_chunk_reader_get_block(reader, addr, len);
    TEST_ASSERT_NOT_NULL(read_data);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(unicode_str, (const char*)read_data, "unicode string should match");

    free(read_data);
    udx_chunk_reader_destroy(reader);
    fclose(file);
    unlink(filename);
}

// ============================================================
// Test Suite Runner
// ============================================================

void run_chunk_tests(void) {
    printf("\n");
    printf("========================================\n");
    printf("  Chunk Tests\n");
    printf("========================================\n");

    RUN_TEST(test_chunk_writer_open);
    RUN_TEST(test_chunk_writer_open_null_file);
    RUN_TEST(test_chunk_writer_add_single_block);
    RUN_TEST(test_chunk_writer_add_multiple_blocks);
    RUN_TEST(test_chunk_writer_add_null_data);
    RUN_TEST(test_chunk_writer_add_zero_size);
    RUN_TEST(test_chunk_writer_large_block);
    RUN_TEST(test_chunk_writer_chunk_overflow);
    RUN_TEST(test_chunk_writer_close_null);

    RUN_TEST(test_chunk_reader_create);
    RUN_TEST(test_chunk_reader_create_null_file);
    RUN_TEST(test_chunk_reader_get_block);
    RUN_TEST(test_chunk_reader_get_chunk_count);
    RUN_TEST(test_chunk_reader_destroy_null);

    RUN_TEST(test_chunk_roundtrip_binary_data);
    RUN_TEST(test_chunk_roundtrip_unicode);
}
