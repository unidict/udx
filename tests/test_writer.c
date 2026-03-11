//
//  test_writer.c
//  libudx tests
//
//  Writer API tests using Unity framework
//

#include "unity.h"
#include "udx_writer.h"
#include "test_platform.h"
#include <stdlib.h>

// ============================================================
// Test Cases
// ============================================================

void test_writer_open(void) {
    const char* test_file = "test_output.udx";
    unlink(test_file);

    udx_writer* writer = udx_writer_open(test_file);
    TEST_ASSERT_NOT_NULL_MESSAGE(writer, "writer should open successfully");

    udx_writer_close(writer);
    unlink(test_file);
}

void test_writer_open_null_path(void) {
    udx_writer* writer = udx_writer_open(NULL);
    TEST_ASSERT_NULL_MESSAGE(writer, "null path should return NULL");
}

void test_writer_create_db(void) {
    const char* test_file = "test_output.udx";
    unlink(test_file);

    udx_writer* writer = udx_writer_open(test_file);
    TEST_ASSERT_NOT_NULL(writer);

    udx_db_builder* builder = udx_db_builder_create(writer, "test_db");
    TEST_ASSERT_NOT_NULL_MESSAGE(builder, "builder should be created");

    // Add at least one entry (databases must have entries to be valid)
    udx_db_builder_add_entry(builder, "test", (const uint8_t*)"data", 4);

    udx_error_t result = udx_db_builder_finalize(builder);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UDX_OK, result, "finalize should succeed");

    udx_writer_close(writer);
    unlink(test_file);
}

void test_writer_add_entry(void) {
    const char* test_file = "test_output.udx";
    unlink(test_file);

    udx_writer* writer = udx_writer_open(test_file);
    TEST_ASSERT_NOT_NULL(writer);

    udx_db_builder* builder = udx_db_builder_create(writer, "test_db");
    TEST_ASSERT_NOT_NULL(builder);

    udx_error_t result = udx_db_builder_add_entry(builder, "hello",
        (const uint8_t*)"world", 5);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UDX_OK, result, "add_entry should succeed");

    result = udx_db_builder_finalize(builder);
    TEST_ASSERT_EQUAL_INT(UDX_OK, result);

    udx_writer_close(writer);
    unlink(test_file);
}

void test_writer_multiple_dbs(void) {
    const char* test_file = "test_output.udx";
    unlink(test_file);

    udx_writer* writer = udx_writer_open(test_file);
    TEST_ASSERT_NOT_NULL(writer);

    udx_db_builder* builder1 = udx_db_builder_create(writer, "db1");
    TEST_ASSERT_NOT_NULL(builder1);
    udx_db_builder_add_entry(builder1, "word1", (const uint8_t*)"data1", 5);
    udx_db_builder_finalize(builder1);

    udx_db_builder* builder2 = udx_db_builder_create(writer, "db2");
    TEST_ASSERT_NOT_NULL(builder2);
    udx_db_builder_add_entry(builder2, "word2", (const uint8_t*)"data2", 5);
    udx_db_builder_finalize(builder2);

    udx_writer_close(writer);

    FILE* f = fopen(test_file, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "file should be created");
    if (f) fclose(f);
    unlink(test_file);
}

void test_writer_empty_database(void) {
    const char* test_file = "test_output.udx";
    unlink(test_file);

    udx_writer* writer = udx_writer_open(test_file);
    TEST_ASSERT_NOT_NULL(writer);

    udx_db_builder* builder = udx_db_builder_create(writer, "empty_db");
    TEST_ASSERT_NOT_NULL(builder);

    udx_error_t result = udx_db_builder_finalize(builder);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UDX_ERR_INVALID_PARAM, result,
        "empty database should fail with UDX_ERR_INVALID_PARAM");

    udx_writer_close(writer);
    unlink(test_file);
}

void test_writer_duplicate_db_name(void) {
    const char* test_file = "test_output.udx";
    unlink(test_file);

    udx_writer* writer = udx_writer_open(test_file);
    TEST_ASSERT_NOT_NULL(writer);

    // Create and finalize first DB (must add entry to finalize successfully)
    udx_db_builder* builder1 = udx_db_builder_create(writer, "dup_db");
    TEST_ASSERT_NOT_NULL(builder1);
    udx_db_builder_add_entry(builder1, "word1", (const uint8_t*)"data1", 5);
    udx_db_builder_finalize(builder1);

    // Try to create duplicate DB name
    udx_db_builder* builder2 = udx_db_builder_create(writer, "dup_db");
    TEST_ASSERT_NULL_MESSAGE(builder2, "duplicate name should return NULL");

    udx_writer_close(writer);
    unlink(test_file);
}

void test_writer_with_metadata(void) {
    const char* test_file = "test_output.udx";
    unlink(test_file);

    udx_writer* writer = udx_writer_open(test_file);
    TEST_ASSERT_NOT_NULL(writer);

    const char* metadata = "Test metadata";
    udx_db_builder* builder = udx_db_builder_create_with_metadata(
        writer, "test_db", (const uint8_t*)metadata, strlen(metadata)
    );
    TEST_ASSERT_NOT_NULL(builder);

    // Add at least one entry (databases must have entries to be valid)
    udx_db_builder_add_entry(builder, "test", (const uint8_t*)"data", 4);

    udx_error_t result = udx_db_builder_finalize(builder);
    TEST_ASSERT_EQUAL_INT(UDX_OK, result);

    udx_writer_close(writer);
    unlink(test_file);
}

void test_writer_close_with_active_builder(void) {
    const char* test_file = "test_output.udx";
    unlink(test_file);

    udx_writer* writer = udx_writer_open(test_file);
    TEST_ASSERT_NOT_NULL(writer);

    udx_db_builder* builder = udx_db_builder_create(writer, "test_db");
    TEST_ASSERT_NOT_NULL(builder);
    udx_db_builder_add_entry(builder, "test", (const uint8_t*)"data", 4);

    // Try to close with active builder (should fail)
    // Note: The library will close the file and free the writer even when returning error
    // Resources will leak in this error case, but that's a library design issue
    udx_error_t result = udx_writer_close(writer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UDX_ERR_ACTIVE_DB, result,
        "close with active builder should fail");

    // Clean up the test file (resources are leaked due to library design)
    unlink(test_file);
}

// ============================================================
// Test Suite Runner
// ============================================================

void run_writer_tests(void) {
    printf("\n");
    printf("========================================\n");
    printf("  Writer Tests\n");
    printf("========================================\n");

    RUN_TEST(test_writer_open);
    RUN_TEST(test_writer_open_null_path);
    RUN_TEST(test_writer_create_db);
    RUN_TEST(test_writer_add_entry);
    RUN_TEST(test_writer_multiple_dbs);
    RUN_TEST(test_writer_empty_database);
    RUN_TEST(test_writer_duplicate_db_name);
    RUN_TEST(test_writer_with_metadata);
    RUN_TEST(test_writer_close_with_active_builder);
}
