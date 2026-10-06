/*
 * Copyright (c) 2026 netcorelink
 *
 * Distributed under the BSD 3-Clause License. See LICENSE for details.
 */

#ifndef CHTTPX_INTERNAL_LOGGER_H
#define CHTTPX_INTERNAL_LOGGER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Internal library logger (not the public request logger).
 *
 * Writes to dated directories:
 *   <base>/log_DDMMYYYY/err-dev.log
 *   <base>/log_DDMMYYYY/info-dev.log
 *   <base>/log_DDMMYYYY/debug-dev.log
 *
 * Default bases:
 *   Linux:  /var/log/libchttpx  (fallback /tmp/libchttpx)
 *   macOS:  ~/Library/Logs/libchttpx  (fallback /tmp/libchttpx)
 */

void _chttpx_sys_log_error(const char* fmt, ...);
void _chttpx_sys_log_info(const char* fmt, ...);
void _chttpx_sys_log_debug(const char* fmt, ...);

#define STDERROR(...) _chttpx_sys_log_error(__VA_ARGS__)
#define STDINFO(...) _chttpx_sys_log_info(__VA_ARGS__)
#define STDDEBUG(...) _chttpx_sys_log_debug(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
