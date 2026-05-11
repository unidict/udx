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

    // Case-insensitive lookup: "hello" should match both "hello" and "Hello"
    udx_db_value_entry* entry = NULL;
    udx_status_t status = udx_db_value_entry_lookup(db, "hello", &entry);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UDX_OK, status, "lookup should succeed");
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "entry should be found");

    // Should have 2 items (one for "hello"->"world", one for "Hello"->"WORLD")
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, entry->items.size, "should have 2 items (hello and Hello)");

    // First item: "hello" -> "world"
    TEST_ASSERT_EQUAL_STRING_MESSAGE("hello", entry->items.data[0].original_key, "first original word should be 'hello'");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, entry->items.data[0].size, "first data should be 5 bytes");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("world", (char*)entry->items.data[0].data, 5, "first data should be 'world'");

    // Second item: "Hello" -> "WORLD"
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hello", entry->items.data[1].original_key, "second original word should be 'Hello'");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, entry->items.data[1].size, "second data should be 5 bytes");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("WORLD", (char*)entry->items.data[1].data, 5, "second data should be 'WORLD'");

    udx_db_value_entry_free(entry);
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
    udx_db_key_entry_array *results = NULL;
    udx_status_t status = udx_db_key_entry_prefix_match(db, "he", 10, &results);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UDX_OK, status, "prefix match should succeed");
    TEST_ASSERT_NOT_NULL_MESSAGE(results, "prefix match should return results");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, results->size, "should find 1 entry (both hello/Hello map to same folded word)");

    // Verify the entry
    TEST_ASSERT_EQUAL_STRING_MESSAGE("hello", results->entries[0].key, "folded word should be 'hello'");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, results->entries[0].items.size, "should have 2 items");

    // Verify first item
    TEST_ASSERT_EQUAL_STRING_MESSAGE("hello", results->entries[0].items.data[0].original_key, "first original word should be 'hello'");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, results->entries[0].items.data[0].value_size, "first data should be 5 bytes");

    // Verify second item
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hello", results->entries[0].items.data[1].original_key, "second original word should be 'Hello'");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, results->entries[0].items.data[1].value_size, "second data should be 5 bytes");

    udx_db_key_entry_array_free(results);
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

    // Should iterate through all 2 unique key entries: "hello" (with 2 items) and "test" (with 1 item)
    const udx_db_key_entry* entry;
    udx_status_t status;

    // First entry: "hello" with 2 items
    status = udx_db_iter_next(iter, &entry);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UDX_OK, status, "first iter_next should succeed");
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "should have first entry");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("hello", entry->key, "first word should be 'hello'");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, entry->items.size, "hello should have 2 items");

    // Second entry: "test" with 1 item
    status = udx_db_iter_next(iter, &entry);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UDX_OK, status, "second iter_next should succeed");
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "should have second entry");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test", entry->key, "second word should be 'test'");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, entry->items.size, "test should have 1 item");

    // No more entries
    status = udx_db_iter_next(iter, &entry);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UDX_NOT_FOUND, status, "should have no more entries");

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

    // All case variations should return the same results (both "hello" and "Hello" items)
    udx_db_value_entry* e1 = NULL;
    udx_db_value_entry* e2 = NULL;
    udx_db_value_entry* e3 = NULL;
    udx_status_t s1 = udx_db_value_entry_lookup(db, "hello", &e1);
    udx_status_t s2 = udx_db_value_entry_lookup(db, "HELLO", &e2);
    udx_status_t s3 = udx_db_value_entry_lookup(db, "HeLLo", &e3);

    TEST_ASSERT_EQUAL_INT(UDX_OK, s1);
    TEST_ASSERT_EQUAL_INT(UDX_OK, s2);
    TEST_ASSERT_EQUAL_INT(UDX_OK, s3);
    TEST_ASSERT_NOT_NULL(e1);
    TEST_ASSERT_NOT_NULL(e2);
    TEST_ASSERT_NOT_NULL(e3);

    // All should return 2 items (both "hello"->"world" and "Hello"->"WORLD")
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, e1->items.size, "hello lookup should return 2 items");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, e2->items.size, "HELLO lookup should return 2 items");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, e3->items.size, "HeLLo lookup should return 2 items");

    // Verify first item data
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("world", (char*)e1->items.data[0].data, 5, "first item data should be 'world'");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("world", (char*)e2->items.data[0].data, 5, "first item data should be 'world'");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("world", (char*)e3->items.data[0].data, 5, "first item data should be 'world'");

    // Verify second item data
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("WORLD", (char*)e1->items.data[1].data, 5, "second item data should be 'WORLD'");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("WORLD", (char*)e2->items.data[1].data, 5, "second item data should be 'WORLD'");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("WORLD", (char*)e3->items.data[1].data, 5, "second item data should be 'WORLD'");

    udx_db_value_entry_free(e1);
    udx_db_value_entry_free(e2);
    udx_db_value_entry_free(e3);
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
