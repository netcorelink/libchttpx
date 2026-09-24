/**
 * Copyright (c) 2026 netcorelink
 *
 * Response compression middleware.
 */

#ifndef COMPRESSION_H
#define COMPRESSION_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

    struct chttpx_serv;
    struct chttpx_route;
    struct chttpx_response;

    /**
     * Encode one complete buffered response body.
     *
     * The provider allocates *output with malloc-compatible ownership. The
     * library takes ownership on success and frees it during response cleanup.
     */
    typedef int (*chttpx_compression_encode_fn)(const unsigned char* input, size_t input_size, int level, unsigned char** output, size_t* output_size, void* user_data);

    /**
     * Compression provider descriptor.
     *
     * Providers are ordered by server preference when quality values are tied.
     * encode_buffer is intentionally buffer-oriented; future streaming hooks
     * can be added without changing response compression configuration.
     */
    typedef struct
    {
        const char* encoding;
        chttpx_compression_encode_fn encode_buffer;
        void* user_data;
    } chttpx_compression_provider_t;

    typedef struct
    {
        /* Do not compress bodies smaller than this many bytes. */
        size_t min_size;

        /* Provider-specific compression level. Built-in gzip accepts -1..9. */
        int level;

        /*
         * MIME patterns eligible for compression. '*' is supported anywhere
         * in a pattern. When the list is empty, every MIME type is eligible
         * unless excluded below.
         */
        const char** include_types;
        size_t include_types_count;

        /* MIME patterns that must never be compressed. Exclusions win. */
        const char** exclude_types;
        size_t exclude_types_count;

        /*
         * Optional custom providers. When omitted, the built-in gzip provider
         * is used.
         */
        const chttpx_compression_provider_t* providers;
        size_t providers_count;
    } chttpx_compression_config_t;

    /** Return the default gzip-oriented compression configuration. */
    chttpx_compression_config_t cHTTPX_CompressionDefault(void);

    /** Enable response compression for a server. */
    int cHTTPX_CompressionUse(struct chttpx_serv* server, const chttpx_compression_config_t* config);

    /** Enable or disable compression for one route. */
    int cHTTPX_RouteCompression(struct chttpx_route* route, bool enabled);

    /** Enable or disable compression for one response. */
    void cHTTPX_ResponseCompression(struct chttpx_response* response, bool enabled);

    /* Internal per-server compression cleanup. */
    void _chttpx_compression_server_cleanup(struct chttpx_serv* server);

#ifdef __cplusplus
}
#endif

#endif
