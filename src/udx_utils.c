//
//  udx_utils.c
//  libudx
//
//  Created by kejinlu on 2026/2/25.
//

#include "udx_utils.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *udx_str_dup(const char *str) {
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str) + 1;
    char *dup = (char *)malloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

char *udx_fold_string(const char *str) {
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str);
    char *folded = (char *)malloc(len + 1);
    if (folded == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < len; i++) {
        folded[i] = (char)tolower((unsigned char)str[i]);
    }
    folded[len] = '\0';

    return folded;
}
