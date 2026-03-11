//
//  test_reader.c
//  libudx tests
//
//  Reader API tests using Unity framework
//

#include "unity.h"
#include "udx_reader.h"
#include "udx_writer.h"
#include "test_platform.h"
#include <stdlib.h>

// Helper to create a simple test file
static void create_test_file(void) {
    udx_writer* writer = udx_writer_open("test_reader.udx");
    udx_db_builder* builder = udx_db_builder_create(writer, "test_db");
    udx_db_builder_add_entry(builder, "hello", (const uint8_t*)"world", 5);
    udx_db_builder_add_entry(builder, "test", (const uint8_t*)"data", 4);
    udx_db_builder_add_entry(builder, "Hello", (const uint8_t*)"WORLD", 5);
    udx_db_builder_finalize(builder);
    udx_writer_close(writer);
}

// ============================================================
// Test Cases
// ============================================================

void test_reader_open(void) {
    create_test_file();

    udx_reader* reader = udx_reader_open("test_reader.udx");
    TEST_ASSERT_NOT_NULL_MESSAGE(reader, "reader should open successfully");

    udx_reader_close(reader);
    unlink("test_reader.udx");
}

void test_reader_open_null_path(void) {
    udx_reader* reader = udx_reader_open(NULL);
    TEST_ASSERT_NULL_MESSAGE(reader, "null path should return NULL");
}

void test_reader_get_db_count(void) {
    create_test_file();

    udx_reader* reader = udx_reader_open("test_reader.udx");
    TEST_ASSERT_NOT_NULL(reader);

    uint32_t count = udx_reader_get_db_count(reader);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, count, "should have 1 database");

    udx_reader_close(reader);
    unlink("test_reader.udx");
}

void test_reader_db_open(void) {
    create_test_file();

    udx_reader* reader = udx_reader_open("test_reader.udx");
    TEST_ASSERT_NOT_NULL(reader);

    udx_db* db = udx_db_open(reader, "test_db");
    TEST_ASSERT_NOT_NULL_MESSAGE(db, "database should open");

    udx_db_close(db);
    udx_reader_close(reader);
    unlink("test_reader.udx");
}

void test_reader_db_open_first(void) {
    create_test_file();

    udx_reader* reader = udx_reader_open("test_reader.udx");
    TEST_ASSERT_NOT_NULL(reader);

    udx_db* db = udx_db_open(reader, NULL);  // Open first db
    TEST_ASSERT_NOT_NULL_MESSAGE(db, "first database should open");

    udx_db_close(db);
    udx_reader_close(reader);
    unlink("test_reader.udx");
}

void test_reader_lookup(void) {
    create_test_file();

    udx_reader* reader = udx_reader_open("test_reader.udx");
    TEST_ASSERT_NOT_NULL(reader);

    udx_db* db = udx_db_open(reader, "test_db");
    TEST_ASSERT_NOT_NULL(db);

    // Case-insensitive lookup
    udx_db_entry* entry = udx_db_lookup(db, "hello");
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "entry should be found");
    udx_db_entry_free(entry);

    udx_db_close(db);
    udx_reader_close(reader);
    unlink("test_reader.udx");
}

void test_reader_prefix_match(void) {
    create_test_file();

    udx_reader* reader = udx_reader_open("test_reader.udx");
    TEST_ASSERT_NOT_NULL(reader);

    udx_db* db = udx_db_open(reader, "test_db");
    TEST_ASSERT_NOT_NULL(db);

    // Prefix match should find both "hello" and "Hello"
    udx_index_entry_array results = udx_db_index_prefix_match(db, "he", 10);
    TEST_ASSERT_TRUE_MESSAGE(results.size >= 1, "should find at least 1 entry");

    udx_index_entry_array_free_contents(&results);
    udx_db_close(db);
    udx_reader_close(reader);
    unlink("test_reader.udx");
}

void test_reader_iterator(void) {
    create_test_file();

    udx_reader* reader = udx_reader_open("test_reader.udx");
    TEST_ASSERT_NOT_NULL(reader);

    udx_db* db = udx_db_open(reader, "test_db");
    TEST_ASSERT_NOT_NULL(db);

    udx_db_iter* iter = udx_db_iter_create(db);
    TEST_ASSERT_NOT_NULL(iter);

    int count = 0;
    const udx_db_entry* entry;
    while ((entry = udx_db_iter_next(iter)) != NULL) {
        count++;
    }
    TEST_ASSERT_TRUE_MESSAGE(count >= 2, "should have at least 2 entries");

    udx_db_iter_destroy(iter);
    udx_db_close(db);
    udx_reader_close(reader);
    unlink("test_reader.udx");
}

void test_reader_case_insensitive(void) {
    create_test_file();

    udx_reader* reader = udx_reader_open("test_reader.udx");
    TEST_ASSERT_NOT_NULL(reader);

    udx_db* db = udx_db_open(reader, "test_db");
    TEST_ASSERT_NOT_NULL(db);

    // All variations should work
    udx_db_entry* e1 = udx_db_lookup(db, "hello");
    udx_db_entry* e2 = udx_db_lookup(db, "HELLO");
    udx_db_entry* e3 = udx_db_lookup(db, "HeLLo");

    TEST_ASSERT_NOT_NULL(e1);
    TEST_ASSERT_NOT_NULL(e2);
    TEST_ASSERT_NOT_NULL(e3);

    // All should return data
    TEST_ASSERT_TRUE(e1->items.size >= 1);
    TEST_ASSERT_TRUE(e2->items.size >= 1);
    TEST_ASSERT_TRUE(e3->items.size >= 1);

    udx_db_entry_free(e1);
    udx_db_entry_free(e2);
    udx_db_entry_free(e3);
    udx_db_close(db);
    udx_reader_close(reader);
    unlink("test_reader.udx");
}

// ============================================================
// Test Suite Runner
// ============================================================

void run_reader_tests(void) {
    printf("\n");
    printf("========================================\n");
    printf("  Reader Tests\n");
    printf("========================================\n");

    RUN_TEST(test_reader_open);
    RUN_TEST(test_reader_open_null_path);
    RUN_TEST(test_reader_get_db_count);
    RUN_TEST(test_reader_db_open);
    RUN_TEST(test_reader_db_open_first);
    RUN_TEST(test_reader_lookup);
    RUN_TEST(test_reader_prefix_match);
    RUN_TEST(test_reader_iterator);
    RUN_TEST(test_reader_case_insensitive);
}
