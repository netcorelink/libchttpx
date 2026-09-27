#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef pthread_t thread_t;

/**
 * Create a worker thread (POSIX).
 *
 * @param thread Output pthread_t.
 * @param func User function accepting void* argument.
 * @param arg Argument passed to func.
 * @return pthread_create result (0 on success).
 */
static inline int _thread_create(thread_t* thread, void* (*func)(void*), void* arg)
{
    return pthread_create(thread, NULL, func, arg);
}

/**
 * Wait for a worker thread to finish (POSIX).
 *
 * @param thread Thread id from _thread_create.
 * @return pthread_join result (0 on success).
 */
static inline int _thread_join(thread_t thread)
{
    return pthread_join(thread, NULL);
}

#ifdef __cplusplus
}
#endif

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 * Case-insensitive memmem over a bounded haystack.
 *
 * @param haystack Buffer to search.
 * @param haystack_len Length of haystack.
 * @param needle Substring to find.
 * @param needle_len Length of needle.
 * @return Pointer into haystack, or NULL if not found.
 */
static inline const char* memmem_case(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len)
{
    const unsigned char* h = (const unsigned char*)haystack;
    const unsigned char* n = (const unsigned char*)needle;

    if (needle_len == 0)
        return (const char*)haystack;

    if (haystack_len < needle_len)
        return NULL;

    for (size_t i = 0; i <= haystack_len - needle_len; i++)
    {
        size_t j = 0;

        while (j < needle_len && tolower((unsigned char)h[i + j]) == tolower((unsigned char)n[j]))
        {
            j++;
        }

        if (j == needle_len)
            return (const char*)(h + i);
    }

    return NULL;
}

#endif
