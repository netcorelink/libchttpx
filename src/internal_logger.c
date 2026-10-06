/*
 * Copyright (c) 2026 netcorelink
 *
 * Distributed under the BSD 3-Clause License. See LICENSE for details.
 */

#include "internal_logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum
{
    LOG_ERROR = 0,
    LOG_INFO = 1,
    LOG_DEBUG = 2
};

static int g_log_level = LOG_DEBUG;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_day_dir[512];
static char g_day_stamp[16];
static int g_ready = 0;

#if defined(__APPLE__)
#define CHTTPX_LOG_FALLBACK_BASE "/tmp/libchttpx"
#else
#define CHTTPX_LOG_PRIMARY_BASE "/var/log/libchttpx"
#define CHTTPX_LOG_FALLBACK_BASE "/tmp/libchttpx"
#endif

/**
 * Create one directory if missing.
 */
static int ensure_dir(const char* path, mode_t mode)
{
    if (mkdir(path, mode) == 0 || errno == EEXIST)
        return 0;

    return -1;
}

/**
 * Create nested directories (best-effort, absolute paths only).
 */
static int ensure_dirs(char* path)
{
    if (!path || path[0] != '/')
        return -1;

    for (char* p = path + 1; *p; p++)
    {
        if (*p != '/')
            continue;

        *p = '\0';

        if (ensure_dir(path, 0755) != 0)
        {
            *p = '/';
            return -1;
        }

        *p = '/';
    }

    return ensure_dir(path, 0755);
}

/**
 * Resolve platform log root into out (without trailing day folder).
 */
static int resolve_base_dir(char* out, size_t out_size)
{
#if defined(__APPLE__)
    const char* home = getenv("HOME");
    if (home && *home)
    {
        int n = snprintf(out, out_size, "%s/Library/Logs/libchttpx", home);
        if (n > 0 && (size_t)n < out_size && ensure_dirs(out) == 0)
            return 0;
    }

    if (snprintf(out, out_size, "%s", CHTTPX_LOG_FALLBACK_BASE) >= (int)out_size)
        return -1;

    return ensure_dirs(out);
#else
    if (snprintf(out, out_size, "%s", CHTTPX_LOG_PRIMARY_BASE) < (int)out_size && ensure_dirs(out) == 0)
        return 0;
    if (snprintf(out, out_size, "%s", CHTTPX_LOG_FALLBACK_BASE) >= (int)out_size)
        return -1;

    return ensure_dirs(out);
#endif
}

/**
 * Refresh dated directory path when the calendar day changes.
 */
static int refresh_day_dir_locked(void)
{
    time_t now = time(NULL);
    struct tm local_tm;
    if (!localtime_r(&now, &local_tm))
        return -1;

    char stamp[16];
    if (strftime(stamp, sizeof(stamp), "%d%m%Y", &local_tm) == 0)
        return -1;

    if (g_ready && strcmp(g_day_stamp, stamp) == 0)
        return 0;

    char base[384];
    if (resolve_base_dir(base, sizeof(base)) != 0)
        return -1;

    int n = snprintf(g_day_dir, sizeof(g_day_dir), "%s/log_%s", base, stamp);
    if (n <= 0 || (size_t)n >= sizeof(g_day_dir))
        return -1;

    if (ensure_dirs(g_day_dir) != 0)
        return -1;

    snprintf(g_day_stamp, sizeof(g_day_stamp), "%s", stamp);
    if (!g_ready)
    {
#if defined(__APPLE__)
        const char* platform = "macos";
#else
        const char* platform = "linux";
#endif
        /* Mark ready before recursive write from init message. */
        g_ready = 1;
        FILE* bootstrap = NULL;

        char path[576];
        snprintf(path, sizeof(path), "%s/info-dev.log", g_day_dir);

        bootstrap = fopen(path, "a");
        if (bootstrap)
        {
            char timebuf[32];
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &local_tm);

            fprintf(bootstrap, "%s [STDINFO] internal logger ready platform=%s dir=%s\n", timebuf, platform, g_day_dir);

            fclose(bootstrap);
        }
        return 0;
    }

    g_ready = 1;
    return 0;
}

/**
 * Map level to file name and label.
 */
static const char* level_file(int level)
{
    switch (level)
    {
    case LOG_ERROR:
        return "err-dev.log";
    case LOG_INFO:
        return "info-dev.log";
    default:
        return "debug-dev.log";
    }
}

static const char* level_name(int level)
{
    switch (level)
    {
    case LOG_ERROR:
        return "STDERROR";
    case LOG_INFO:
        return "STDINFO";
    default:
        return "STDDEBUG";
    }
}

static void log_write(int level, const char* fmt, va_list args)
{
    if (!fmt || level > g_log_level)
        return;

    pthread_mutex_lock(&g_log_mutex);

    if (refresh_day_dir_locked() != 0)
    {
        pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    char path[576];
    snprintf(path, sizeof(path), "%s/%s", g_day_dir, level_file(level));

    FILE* fp = fopen(path, "a");
    if (!fp)
    {
        pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    time_t now = time(NULL);
    struct tm local_tm;

    char timebuf[32];
    
    if (localtime_r(&now, &local_tm))
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &local_tm);
    else
        snprintf(timebuf, sizeof(timebuf), "unknown-time");

    fprintf(fp, "%s [%s] ", timebuf, level_name(level));
    vfprintf(fp, fmt, args);
    fputc('\n', fp);
    fclose(fp);

    pthread_mutex_unlock(&g_log_mutex);
}

void _chttpx_sys_log_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_write(LOG_ERROR, fmt, args);
    va_end(args);
}

void _chttpx_sys_log_info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_write(LOG_INFO, fmt, args);
    va_end(args);
}

void _chttpx_sys_log_debug(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_write(LOG_DEBUG, fmt, args);
    va_end(args);
}
