#ifndef UTILS_H
#define UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Threads */
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

typedef HANDLE thread_t;

inline int _thread_create(thread_t *thread, void *(*func)(void*), void *arg) {
    *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return *thread ? 0 : -1;
}

inline int _thread_join(thread_t thread) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}
#else
#include <pthread.h>

typedef pthread_t thread_t;

inline int _thread_create(thread_t *thread, void *(*func)(void*), void *arg) 
{
    return pthread_create(thread, NULL, func, arg);
}

inline int _thread_join(thread_t thread) 
{
    return pthread_join(thread, NULL);
}
#endif

#ifdef __cplusplus
}
#endif

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif
