//
//  udx_utils.h
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#ifndef udx_utils_h
#define udx_utils_h

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// String Utilities
// ============================================================

/**
 * Duplicate a string (ISO C11 compatible alternative to strdup)
 * @param str Input string (NULL returns NULL)
 * @return Newly allocated copy (caller must free), or NULL on failure
 */
char *udx_str_dup(const char *str);

/**
 * Fold string to lowercase for case-insensitive comparison
 * @param str Input string (NULL returns NULL)
 * @return Newly allocated lowercase string (caller must free), or NULL on failure
 */
char *udx_fold_string(const char *str);

// ============================================================
// File I/O Utilities (64-bit safe)
// ============================================================

/**
 * Get current file position (64-bit safe)
 * @param file File handle
 * @return Current file offset on success, -1 on failure (check errno for details)
 */
static inline int64_t udx_ftell(FILE *file) {
#if defined(_MSC_VER)
    return _ftelli64(file);
#elif defined(__MINGW32__) || defined(__MINGW64__)
    return ftello64(file);
#else
    return ftello(file);
#endif
}

/**
 * Set file position (64-bit safe)
 * @param file File handle
 * @param offset File offset
 * @param whence Seek origin (SEEK_SET, SEEK_CUR, SEEK_END)
 * @return 0 on success, -1 on failure (check errno for details)
 */
static inline int udx_fseek(FILE *file, int64_t offset, int whence) {
#if defined(_MSC_VER)
    return _fseeki64(file, offset, whence);
#elif defined(__MINGW32__) || defined(__MINGW64__)
    return fseeko64(file, offset, whence);
#else
    return fseeko(file, offset, whence);
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* udx_utils_h */
