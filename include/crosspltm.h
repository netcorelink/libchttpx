#ifndef CROSSPLTM_H
#define CROSSPLTM_H

#ifdef __cplusplus
extern "C"
{
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

#ifdef CHTTPX_PLATFORM_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <time.h>
#endif

#ifdef _WIN32
    typedef SOCKET chttpx_socket_t;
#else
typedef int chttpx_socket_t;
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
    static inline struct tm* localtime_r(const time_t* timep, struct tm* result)
    {
        memset(result, 0, sizeof(*result));
        localtime_s(result, timep);
        return result;
    }

    static inline struct tm* gmtime_r(const time_t* timep, struct tm* result)
    {
        memset(result, 0, sizeof(*result));
        gmtime_s(result, timep);
        return result;
    }

    static inline int chttpx_clock_gettime(int clock_id, struct timespec* value)
    {
        if (!value)
            return -1;
        if (clock_id == CLOCK_MONOTONIC)
        {
            LARGE_INTEGER frequency;
            LARGE_INTEGER counter;
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&counter);
            value->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
            value->tv_nsec = (long)(((counter.QuadPart % frequency.QuadPart) * 1000000000LL) / frequency.QuadPart);
            return 0;
        }
        FILETIME file_time;
        ULARGE_INTEGER ticks;
        GetSystemTimeAsFileTime(&file_time);
        ticks.LowPart = file_time.dwLowDateTime;
        ticks.HighPart = file_time.dwHighDateTime;
        unsigned long long unix_ticks = ticks.QuadPart - 116444736000000000ULL;
        value->tv_sec = (time_t)(unix_ticks / 10000000ULL);
        value->tv_nsec = (long)((unix_ticks % 10000000ULL) * 100ULL);
        return 0;
    }

#define clock_gettime chttpx_clock_gettime
#endif

#ifdef CHTTPX_PLATFORM_POSIX
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
#define strcasecmp _stricmp
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
    static inline void* memmem_win(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen)
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

#define memmem(haystack, haystacklen, needle, needlelen) memmem_win(haystack, haystacklen, needle, needlelen)
#endif

#ifdef __cplusplus
}
#endif

#endif
