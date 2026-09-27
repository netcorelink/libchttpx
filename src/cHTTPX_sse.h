/**
 * Copyright (c) 2026 netcorelink
 *
 * Server-Sent Events helpers.
 */

#ifndef CHTTPX_SSE_H
#define CHTTPX_SSE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cHTTPX_request.h"
#include "cHTTPX_response.h"

    typedef struct chttpx_sse chttpx_sse_t;

    /** Open an HTTP/2 Server-Sent Events stream and send its response headers. */
    chttpx_sse_t* cHTTPX_SSEOpen(chttpx_request_t* req, chttpx_response_t* res);

    /** Send one SSE event. event and id are optional; data may contain multiple lines. */
    int cHTTPX_SSESend(chttpx_sse_t* sse, const char* event, const char* id, const char* data);

    /** Send a retry directive in milliseconds. */
    int cHTTPX_SSERetry(chttpx_sse_t* sse, uint64_t milliseconds);

    /** Send an SSE comment, splitting multiline comments correctly. */
    int cHTTPX_SSEComment(chttpx_sse_t* sse, const char* comment);

    /** Send an empty SSE comment suitable for keep-alive heartbeats. */
    int cHTTPX_SSEHeartbeat(chttpx_sse_t* sse);

    /** Return whether the underlying client stream is still writable. */
    bool cHTTPX_SSEConnected(const chttpx_sse_t* sse);

    /** Gracefully finish the SSE stream. Safe to call more than once. */
    int cHTTPX_SSEClose(chttpx_sse_t* sse);

#ifdef __cplusplus
}
#endif

#endif
