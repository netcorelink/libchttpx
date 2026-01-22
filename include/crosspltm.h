#ifndef CROSSPLTM_H
#define CROSSPLTM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
    #define CHTTPX_PLATFORM_WINDOWS
#else
    #define CHTTPX_PLATFORM_POSIX
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
    #define strdup _strdup
#else
    #define strdup strdup
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
    #define chttpx_close(s) closesocket(s)
#else
    #define chttpx_close(s) close(s)
#endif

#ifdef _WIN32
    typedef SOCKET chttpx_socket_t;
#else
    typedef int chttpx_socket_t;
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
    #include <time.h>
    static struct tm *localtime_r(const time_t *timep, struct tm *result) {
        /* WIN32: localtime_s */
        localtime_s(result, timep);
        return result;
    }
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <windows.h>
    #include <ws2tcpip.h>
#else
    #include <unistd.h>
    #include <sys/time.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
    #define strcasecmp _stricmp
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
    static void* memmem_win(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen) {
        if (!needlelen) return (void *)haystack;
        if (needlelen > haystacklen) return NULL;

        const unsigned char *h = haystack;
        const unsigned char *n = needle;

        for (size_t i = 0; i <= haystacklen - needlelen; i++) {
            if (h[i] == n[0] && memcmp(h + i, n, needlelen) == 0) return (void *)(h + i);
        }

        return NULL;
    }

    #define memmem(haystack, haystacklen, needle, needlelen) memmem_win(haystack, haystacklen, needle, needlelen)
#endif

#ifdef __cplusplus
}
#endif

#endif
