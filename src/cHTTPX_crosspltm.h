#ifndef CROSSPLTM_H
#define CROSSPLTM_H

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined(__linux__)
#error "libchttpx supports Linux only"
#endif

#include <arpa/inet.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define CHTTPX_PLATFORM_POSIX
#define chttpx_close(s) close(s)

typedef int chttpx_socket_t;

/**
 * Find a byte substring within a buffer (POSIX memmem when unavailable).
 *
 * @param haystack Buffer to search.
 * @param haystacklen Length of haystack.
 * @param needle Substring to find.
 * @param needlelen Length of needle.
 * @return Pointer into haystack, or NULL if not found.
 */
static inline void* chttpx_memmem(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen)
{
    if (!needlelen)
        return (void*)haystack;
    if (needlelen > haystacklen)
        return NULL;

    const unsigned char* h = haystack;
    const unsigned char* n = needle;

    for (size_t i = 0; i <= haystacklen - needlelen; i++)
    {
        if (h[i] == n[0] && memcmp(h + i, n, needlelen) == 0)
            return (void*)(h + i);
    }

    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif
