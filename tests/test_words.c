//
//  test_words.c
//  libudx tests
//
//  Words container tests using Unity framework
//

#include "unity.h"
#include "udx_keys.h"
#include <stdio.h>
#include <string.h>

// ============================================================
// Creation and Destruction Tests
// ============================================================

void test_words_create(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    // Should start with zero count
    TEST_ASSERT_EQUAL_UINT32(0, udx_keys_count(words));
    TEST_ASSERT_EQUAL_UINT32(0, udx_keys_item_count(words));

    udx_keys_destroy(words);
}

void test_words_destroy_null(void) {
    udx_keys_destroy(NULL);  // Should not crash
    TEST_PASS();
}

// ============================================================
// Add Operation Tests
// ============================================================

void test_words_add_single(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    udx_value_address_t addr = 0x0001000100020003ULL;
    uint32_t data_size = 100;

    bool result = udx_keys_add(words, "hello", addr, data_size);
    TEST_ASSERT_TRUE(result);

    TEST_ASSERT_EQUAL_UINT32(1, udx_keys_count(words));
    TEST_ASSERT_EQUAL_UINT32(1, udx_keys_item_count(words));

    udx_keys_destroy(words);
}

void test_words_add_multiple_unique(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    udx_value_address_t addr = 0x0001000100020003ULL;
    uint32_t data_size = 100;

    // Add multiple unique words
    for (int i = 0; i < 10; i++) {
        char word[32];
        snprintf(word, sizeof(word), "word%d", i);

        bool result = udx_keys_add(words, word, addr + i, data_size + i);
        TEST_ASSERT_TRUE(result);
    }

    TEST_ASSERT_EQUAL_UINT32(10, udx_keys_count(words));
    TEST_ASSERT_EQUAL_UINT32(10, udx_keys_item_count(words));

    udx_keys_destroy(words);
}

void test_words_add_duplicate_words(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    udx_value_address_t addr1 = 0x0001000100020003ULL;
    udx_value_address_t addr2 = 0x0001000100020004ULL;
    uint32_t data_size = 100;

    // Add same word multiple times (case variants)
    udx_keys_add(words, "hello", addr1, data_size);
    udx_keys_add(words, "HELLO", addr2, data_size + 10);
    udx_keys_add(words, "HeLLo", addr1 + 1, data_size + 20);

    // Should have 1 unique word but 3 items
    TEST_ASSERT_EQUAL_UINT32(1, udx_keys_count(words));
    TEST_ASSERT_EQUAL_UINT32(3, udx_keys_item_count(words));

    udx_keys_destroy(words);
}

void test_words_add_null_word(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    udx_value_address_t addr = 0x0001000100020003ULL;
    uint32_t data_size = 100;

    bool result = udx_keys_add(words, NULL, addr, data_size);
    TEST_ASSERT_FALSE(result);

    udx_keys_destroy(words);
}

void test_words_add_null_container(void) {
    udx_value_address_t addr = 0x0001000100020003ULL;
    bool result = udx_keys_add(NULL, "hello", addr, 100);
    TEST_ASSERT_FALSE(result);
}

void test_words_add_empty_string(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    udx_value_address_t addr = 0x0001000100020003ULL;
    uint32_t data_size = 100;

    bool result = udx_keys_add(words, "", addr, data_size);
    TEST_ASSERT_TRUE(result);

    TEST_ASSERT_EQUAL_UINT32(1, udx_keys_count(words));

    udx_keys_destroy(words);
}

// ============================================================
// Count Tests
// ============================================================

void test_words_count_null(void) {
    size_t count = udx_keys_count(NULL);
    TEST_ASSERT_EQUAL_UINT32(0, count);
}

void test_words_item_count_null(void) {
    size_t count = udx_keys_item_count(NULL);
    TEST_ASSERT_EQUAL_UINT32(0, count);
}

// ============================================================
// Iterator Tests
// ============================================================

void test_words_iter_create(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    udx_keys_iter* iter = udx_keys_iter_create(words);
    TEST_ASSERT_NOT_NULL(iter);

    udx_keys_iter_destroy(iter);
    udx_keys_destroy(words);
}

void test_words_iter_create_null(void) {
    udx_keys_iter* iter = udx_keys_iter_create(NULL);
    TEST_ASSERT_NULL(iter);
}

void test_words_iter_empty(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    udx_keys_iter* iter = udx_keys_iter_create(words);
    TEST_ASSERT_NOT_NULL(iter);

    const udx_db_key_entry* entry = udx_keys_iter_next(iter);
    TEST_ASSERT_NULL(entry);

    udx_keys_iter_destroy(iter);
    udx_keys_destroy(words);
}

void test_words_iter_traverse_all(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    // Add words in unsorted order
    udx_keys_add(words, "zebra", 1, 10);
    udx_keys_add(words, "apple", 2, 20);
    udx_keys_add(words, "banana", 3, 30);
    udx_keys_add(words, "cherry", 4, 40);
    udx_keys_add(words, "date", 5, 50);

    udx_keys_iter* iter = udx_keys_iter_create(words);
    TEST_ASSERT_NOT_NULL(iter);

    // Words should be returned in sorted order
    const char* expected_order[] = {"apple", "banana", "cherry", "date", "zebra"};
    int index = 0;

    const udx_db_key_entry* entry;
    while ((entry = udx_keys_iter_next(iter)) != NULL) {
        TEST_ASSERT_EQUAL_STRING(expected_order[index], entry->key);
        TEST_ASSERT_EQUAL_UINT32(1, entry->items.count);
        index++;
    }

    TEST_ASSERT_EQUAL_INT(5, index);

    udx_keys_iter_destroy(iter);
    udx_keys_destroy(words);
}

void test_words_iter_peek(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    udx_keys_add(words, "test", 1, 10);

    udx_keys_iter* iter = udx_keys_iter_create(words);
    TEST_ASSERT_NOT_NULL(iter);

    // Peek before next should return NULL
    const udx_db_key_entry* entry = udx_keys_iter_peek(iter);
    TEST_ASSERT_NULL(entry);

    // Get first entry
    entry = udx_keys_iter_next(iter);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("test", entry->key);

    // Peek should now return the same entry
    entry = udx_keys_iter_peek(iter);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("test", entry->key);

    // Next should return NULL (end of iteration)
    entry = udx_keys_iter_next(iter);
    TEST_ASSERT_NULL(entry);

    udx_keys_iter_destroy(iter);
    udx_keys_destroy(words);
}

void test_words_iter_peek_null(void) {
    const udx_db_key_entry* entry = udx_keys_iter_peek(NULL);
    TEST_ASSERT_NULL(entry);
}

void test_words_iter_destroy_null(void) {
    udx_keys_iter_destroy(NULL);  // Should not crash
    TEST_PASS();
}

// ============================================================
// Case Insensitivity Tests
// ============================================================

void test_words_case_insensitive_add(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    // Add same word in different cases
    udx_keys_add(words, "Apple", 1, 10);
    udx_keys_add(words, "APPLE", 2, 20);
    udx_keys_add(words, "apple", 3, 30);
    udx_keys_add(words, "aPpLe", 4, 40);

    // Should have 1 unique word but 4 items
    TEST_ASSERT_EQUAL_UINT32(1, udx_keys_count(words));
    TEST_ASSERT_EQUAL_UINT32(4, udx_keys_item_count(words));

    // Check the entry
    udx_keys_iter* iter = udx_keys_iter_create(words);
    const udx_db_key_entry* entry = udx_keys_iter_next(iter);

    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("apple", entry->key);  // Folded form
    TEST_ASSERT_EQUAL_UINT32(4, entry->items.count);

    // Check original words are preserved
    TEST_ASSERT_EQUAL_STRING("Apple", entry->items.elements[0].original_key);
    TEST_ASSERT_EQUAL_STRING("APPLE", entry->items.elements[1].original_key);
    TEST_ASSERT_EQUAL_STRING("apple", entry->items.elements[2].original_key);
    TEST_ASSERT_EQUAL_STRING("aPpLe", entry->items.elements[3].original_key);

    udx_keys_iter_destroy(iter);
    udx_keys_destroy(words);
}

void test_words_case_insensitive_ordering(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    // Add words in mixed case
    udx_keys_add(words, "Zebra", 1, 10);
    udx_keys_add(words, "apple", 2, 20);
    udx_keys_add(words, "BANANA", 3, 30);

    udx_keys_iter* iter = udx_keys_iter_create(words);

    // Should be sorted by folded form
    const udx_db_key_entry* entry1 = udx_keys_iter_next(iter);
    TEST_ASSERT_EQUAL_STRING("apple", entry1->key);

    const udx_db_key_entry* entry2 = udx_keys_iter_next(iter);
    TEST_ASSERT_EQUAL_STRING("banana", entry2->key);

    const udx_db_key_entry* entry3 = udx_keys_iter_next(iter);
    TEST_ASSERT_EQUAL_STRING("zebra", entry3->key);

    udx_keys_iter_destroy(iter);
    udx_keys_destroy(words);
}

// ============================================================
// Large Dataset Tests
// ============================================================

void test_words_large_dataset(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    // Add many words
    int count = 1000;
    for (int i = 0; i < count; i++) {
        char word[32];
        snprintf(word, sizeof(word), "word%d", i);

        bool result = udx_keys_add(words, word, i, i * 10);
        TEST_ASSERT_TRUE(result);
    }

    TEST_ASSERT_EQUAL_UINT32(count, udx_keys_count(words));
    TEST_ASSERT_EQUAL_UINT32(count, udx_keys_item_count(words));

    // Verify iteration
    udx_keys_iter* iter = udx_keys_iter_create(words);
    int iter_count = 0;

    const udx_db_key_entry* entry;
    while ((entry = udx_keys_iter_next(iter)) != NULL) {
        iter_count++;
    }

    TEST_ASSERT_EQUAL_INT(count, iter_count);

    udx_keys_iter_destroy(iter);
    udx_keys_destroy(words);
}

void test_words_many_duplicates(void) {
    udx_keys* words = udx_keys_create();
    TEST_ASSERT_NOT_NULL(words);

    // Add same word many times
    const char* word = "test";
    int count = 100;

    for (int i = 0; i < count; i++) {
        bool result = udx_keys_add(words, word, i, i * 10);
        TEST_ASSERT_TRUE(result);
    }

    // Should have 1 unique word but many items
    TEST_ASSERT_EQUAL_UINT32(1, udx_keys_count(words));
    TEST_ASSERT_EQUAL_UINT32(count, udx_keys_item_count(words));

    // Verify the entry has all items
    udx_keys_iter* iter = udx_keys_iter_create(words);
    const udx_db_key_entry* entry = udx_keys_iter_next(iter);

    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT32(count, entry->items.count);

    udx_keys_iter_destroy(iter);
    udx_keys_destroy(words);
}

// ============================================================
// Test Suite Runner
// ============================================================

void run_words_tests(void) {
    printf("\n");
    printf("========================================\n");
    printf("  Words Tests\n");
    printf("========================================\n");

    RUN_TEST(test_words_create);
    RUN_TEST(test_words_destroy_null);

    RUN_TEST(test_words_add_single);
    RUN_TEST(test_words_add_multiple_unique);
    RUN_TEST(test_words_add_duplicate_words);
    RUN_TEST(test_words_add_null_word);
    RUN_TEST(test_words_add_null_container);
    RUN_TEST(test_words_add_empty_string);

    RUN_TEST(test_words_count_null);
    RUN_TEST(test_words_item_count_null);

    RUN_TEST(test_words_iter_create);
    RUN_TEST(test_words_iter_create_null);
    RUN_TEST(test_words_iter_empty);
    RUN_TEST(test_words_iter_traverse_all);
    RUN_TEST(test_words_iter_peek);
    RUN_TEST(test_words_iter_peek_null);
    RUN_TEST(test_words_iter_destroy_null);

    RUN_TEST(test_words_case_insensitive_add);
    RUN_TEST(test_words_case_insensitive_ordering);

    RUN_TEST(test_words_large_dataset);
    RUN_TEST(test_words_many_duplicates);
}
