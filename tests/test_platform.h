#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H

// Platform-specific compatibility
#ifdef _WIN32
    #include <io.h>
    #define unlink _unlink
#else
    #include <unistd.h>
#endif

#endif // TEST_PLATFORM_H
