#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Threads */
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

    typedef HANDLE thread_t;

    typedef struct
    {
        void* (*function)(void*);
        void* argument;
    } chttpx_thread_start_t;

    static DWORD WINAPI _thread_start(LPVOID value)
    {
        chttpx_thread_start_t* start = value;
        void* (*function)(void*) = start->function;
        void* argument = start->argument;
        free(start);
        function(argument);
        return 0;
    }

    static inline int _thread_create(thread_t* thread, void* (*func)(void*), void* arg)
    {
        chttpx_thread_start_t* start = malloc(sizeof(*start));
        if (!start)
            return -1;
        start->function = func;
        start->argument = arg;
        *thread = CreateThread(NULL, 0, _thread_start, start, 0, NULL);
        if (!*thread)
            free(start);
        return *thread ? 0 : -1;
    }

    static inline int _thread_join(thread_t thread)
    {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        return 0;
    }
#else
#include <pthread.h>

typedef pthread_t thread_t;

static inline int _thread_create(thread_t* thread, void* (*func)(void*), void* arg)
{
    return pthread_create(thread, NULL, func, arg);
}

static inline int _thread_join(thread_t thread)
{
    return pthread_join(thread, NULL);
}
#endif

#ifdef __cplusplus
}
#endif

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

#include <ctype.h>

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
