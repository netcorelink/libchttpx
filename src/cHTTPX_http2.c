#include "cHTTPX_http2.h"

#include "cHTTPX_crosspltm.h"
#include "cHTTPX_headers.h"
#include "cHTTPX_http.h"
#include "cHTTPX_response.h"
#include "cHTTPX_tls.h"

#include <nghttp2/nghttp2.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef CHTTPX_PLATFORM_POSIX
#include <netdb.h>
#include <strings.h>
#endif

#define CHTTPX_H2_PROTOCOL "HTTP/2"
#define CHTTPX_H2_CALL_TIMEOUT_SEC 30
#define CHTTPX_H2_MAX_AUTHORITY 512

typedef struct chttpx_h2_server chttpx_h2_server_t;

typedef struct chttpx_h2_sse_chunk
{
    unsigned char* data;
    size_t size;
    size_t offset;
    bool completed;
    struct chttpx_h2_sse_chunk* next;
} chttpx_h2_sse_chunk_t;

/**
 * Per-stream HTTP/2 request/response state on the server.
 */
typedef struct chttpx_h2_stream
{
    int32_t id;
    char method[16];
    char path[CHTTPX_MAX_PATH];
    char authority[CHTTPX_H2_MAX_AUTHORITY];
    chttpx_header_t headers[MAX_HEADERS];
    size_t headers_count;
    unsigned char* body;
    size_t body_size;
    size_t body_capacity;
    size_t body_limit;
    bool responded;
    bool request_ready;
    bool ready_queued;
    bool handler_active;
    bool closed;
    bool sse_open;
    bool sse_closing;
    bool sse_connected;
    int error_status;
    char* response;
    size_t response_size;
    size_t response_body_offset;
    size_t response_body_sent;
    chttpx_h2_server_t* connection;
    struct chttpx_h2_stream* ready_next;
    chttpx_h2_sse_chunk_t* sse_chunks;
    chttpx_h2_sse_chunk_t* sse_chunks_tail;
} chttpx_h2_stream_t;

/**
 * Server-side HTTP/2 session bound to one connection.
 */
struct chttpx_h2_server
{
    chttpx_serv_t* server;
    chttpx_socket_t fd;
    void* tls_session;
    nghttp2_session* session;
    chttpx_h2_stream_t* ready_head;
    chttpx_h2_stream_t* ready_tail;
};

/**
 * Client-side HTTP/2 session state for outbound calls.
 */
typedef struct
{
    chttpx_socket_t fd;
    void* tls_session;
    nghttp2_session* session;
    int32_t stream_id;
    bool done;
    int status;
    char content_type[512];
    chttpx_header_t headers[MAX_HEADERS];
    size_t headers_count;
    unsigned char* body;
    size_t body_size;
    size_t body_capacity;
} chttpx_h2_client_t;

/**
 * Streaming request body cursor for nghttp2 data provider.
 */
typedef struct
{
    const unsigned char* body;
    size_t size;
    size_t offset;
} chttpx_h2_client_body_t;

/**
 * Parsed remote authority and path for HTTP/2 calls.
 */
typedef struct
{
    bool tls;
    char host[CHTTPX_H2_MAX_AUTHORITY];
    char port[16];
    char base_path[CHTTPX_MAX_PATH];
} chttpx_h2_url_t;

/**
 * Build one nghttp2 name/value pair from C strings.
 *
 * @param name Header name bytes as a null-terminated C string.
 * @param value Header value bytes as a null-terminated C string.
 * @return nghttp2 name/value pair referencing the given strings.
 */
static nghttp2_nv h2_nv(const char* name, const char* value)
{
    return (nghttp2_nv){
        .name = (uint8_t*)name,
        .value = (uint8_t*)value,
        .namelen = strlen(name),
        .valuelen = strlen(value),
        .flags = NGHTTP2_NV_FLAG_NONE,
    };
}

/**
 * Return whether a header must not be forwarded on HTTP/2.
 *
 * @param name Header field name to test.
 * @return True when the header must not be forwarded on HTTP/2.
 */
static bool h2_forbidden_header(const char* name)
{
    return strcasecmp(name, "connection") == 0 ||
           strcasecmp(name, "keep-alive") == 0 ||
           strcasecmp(name, "proxy-connection") == 0 ||
           strcasecmp(name, "transfer-encoding") == 0 ||
           strcasecmp(name, "upgrade") == 0;
}

/**
 * Fetch stream user data from an nghttp2 session.
 *
 * @param session nghttp2 session owning the stream.
 * @param stream_id HTTP/2 stream id to look up.
 * @return Stream user data for the id, or NULL when unset.
 */
static chttpx_h2_stream_t* h2_stream(nghttp2_session* session, int32_t stream_id)
{
    return (chttpx_h2_stream_t*)nghttp2_session_get_stream_user_data(session, stream_id);
}

/**
 * Release stream buffers and the stream object.
 *
 * @param stream HTTP/2 stream being decoded, buffered, or responded to.
 */
static void h2_stream_free(chttpx_h2_stream_t* stream)
{
    if (!stream)
        return;
    free(stream->body);
    free(stream->response);
    chttpx_h2_sse_chunk_t* chunk = stream->sse_chunks;
    while (chunk)
    {
        chttpx_h2_sse_chunk_t* next = chunk->next;
        free(chunk->data);
        free(chunk);
        chunk = next;
    }
    free(stream);
}

static void h2_ready_push(chttpx_h2_server_t* connection, chttpx_h2_stream_t* stream)
{
    if (!connection || !stream || stream->ready_queued)
        return;
    stream->ready_queued = true;
    stream->ready_next = NULL;
    if (connection->ready_tail)
        connection->ready_tail->ready_next = stream;
    else
        connection->ready_head = stream;
    connection->ready_tail = stream;
}

static void h2_ready_remove(chttpx_h2_server_t* connection, chttpx_h2_stream_t* stream)
{
    if (!connection || !stream || !stream->ready_queued)
        return;

    chttpx_h2_stream_t* previous = NULL;
    chttpx_h2_stream_t* current = connection->ready_head;
    while (current)
    {
        if (current == stream)
        {
            if (previous)
                previous->ready_next = current->ready_next;
            else
                connection->ready_head = current->ready_next;
            if (connection->ready_tail == current)
                connection->ready_tail = previous;
            current->ready_next = NULL;
            current->ready_queued = false;
            return;
        }
        previous = current;
        current = current->ready_next;
    }
}

/**
 * Decode one HPACK header into stream request state.
 *
 * @param stream HTTP/2 stream being decoded, buffered, or responded to.
 * @param name Header field name bytes.
 * @param namelen Header name length in bytes.
 * @param value Header field value bytes.
 * @param valuelen Header value length in bytes.
 * @return Zero on success or a negative error code.
 */
static int h2_stream_add_header(chttpx_h2_stream_t* stream, const uint8_t* name, size_t namelen, const uint8_t* value, size_t valuelen)
{
    if (!stream || !name || !value)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    if (namelen == 7 && memcmp(name, ":method", 7) == 0)
    {
        if (valuelen >= sizeof(stream->method))
        {
            stream->error_status = cHTTPX_StatusBadRequest;
            return 0;
        }
        memcpy(stream->method, value, valuelen);
        stream->method[valuelen] = '\0';
        return 0;
    }

    if (namelen == 5 && memcmp(name, ":path", 5) == 0)
    {
        if (valuelen >= sizeof(stream->path))
        {
            stream->error_status = cHTTPX_StatusURITooLong;
            return 0;
        }
        memcpy(stream->path, value, valuelen);
        stream->path[valuelen] = '\0';
        return 0;
    }

    if (namelen == 10 && memcmp(name, ":authority", 10) == 0)
    {
        if (valuelen >= sizeof(stream->authority))
        {
            stream->error_status = cHTTPX_StatusRequestHeaderFieldsTooLarge;
            return 0;
        }
        memcpy(stream->authority, value, valuelen);
        stream->authority[valuelen] = '\0';
        return 0;
    }

    if (namelen > 0 && name[0] == ':')
        return 0;

    if (stream->headers_count >= MAX_HEADERS || namelen >= MAX_HEADER_NAME || valuelen >= MAX_HEADER_VALUE)
    {
        stream->error_status = cHTTPX_StatusRequestHeaderFieldsTooLarge;
        return 0;
    }

    chttpx_header_t* header = &stream->headers[stream->headers_count++];
    memcpy(header->name, name, namelen);
    header->name[namelen] = '\0';
    memcpy(header->value, value, valuelen);
    header->value[valuelen] = '\0';
    return 0;
}

/**
 * Look up a request header by case-insensitive name.
 *
 * @param stream HTTP/2 stream being decoded, buffered, or responded to.
 * @param name Header field name to match case-insensitively.
 * @return Header value string, or NULL when the name is absent.
 */
static const char* h2_request_header(const chttpx_h2_stream_t* stream, const char* name)
{
    if (!stream || !name)
        return NULL;
    for (size_t i = 0; i < stream->headers_count; i++)
        if (strcasecmp(stream->headers[i].name, name) == 0)
            return stream->headers[i].value;
    return NULL;
}

/**
 * Choose max body size based on content type.
 *
 * @param connection Server-side HTTP/2 connection state.
 * @param stream HTTP/2 stream being decoded, buffered, or responded to.
 * @return Zero on success or a negative error code.
 */
static size_t h2_body_limit(const chttpx_h2_server_t* connection, const chttpx_h2_stream_t* stream)
{
    const char* content_type = h2_request_header(stream, "content-type");
    if (content_type &&
        (strncasecmp(content_type, "application/json", 16) == 0 ||
         strncasecmp(content_type, "application/x-www-form-urlencoded", 33) == 0 ||
         strncasecmp(content_type, "text/", 5) == 0))
        return connection->server->max_body_size;
    return connection->server->max_upload_size;
}

/**
 * Append request body bytes with size enforcement.
 *
 * @param connection Server-side HTTP/2 connection state.
 * @param stream HTTP/2 stream being decoded, buffered, or responded to.
 * @param data Payload bytes for the current chunk.
 * @param len Number of payload bytes in the chunk.
 * @return Zero on success or a negative error code.
 */
static int h2_append_body(chttpx_h2_server_t* connection, chttpx_h2_stream_t* stream, const uint8_t* data, size_t len)
{
    if (!stream || (len && !data))
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    if (stream->error_status)
        return 0;

    if (stream->body_limit == 0)
        stream->body_limit = h2_body_limit(connection, stream);

    if (stream->body_size > stream->body_limit || len > stream->body_limit - stream->body_size)
    {
        stream->error_status = cHTTPX_StatusPayloadTooLarge;
        return 0;
    }

    size_t required = stream->body_size + len + 1;
    if (required > stream->body_capacity)
    {
        size_t capacity = stream->body_capacity ? stream->body_capacity : 4096;
        while (capacity < required)
        {
            size_t next = capacity * 2;
            if (next < capacity || next > stream->body_limit + 1)
                next = stream->body_limit + 1;
            capacity = next;
            if (capacity < required && capacity == stream->body_limit + 1)
                return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }
        unsigned char* resized = realloc(stream->body, capacity);
        if (!resized)
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        stream->body = resized;
        stream->body_capacity = capacity;
    }

    if (len)
        memcpy(stream->body + stream->body_size, data, len);
    stream->body_size += len;
    stream->body[stream->body_size] = '\0';
    return 0;
}

/**
 * Serialize stream state into an HTTP/1-style header block.
 *
 * @param connection Server-side HTTP/2 connection state.
 * @param stream HTTP/2 stream being decoded, buffered, or responded to.
 * @param output Output pointer receiving allocated request header text.
 * @param output_size Output length of the serialized header block.
 * @return Zero on success or a negative error code.
 */
static int h2_build_request_text(const chttpx_h2_server_t* connection, const chttpx_h2_stream_t* stream, char** output, size_t* output_size)
{
    if (!connection || !stream || !output || !output_size || !stream->method[0] || !stream->path[0])
        return cHTTPX_ERR_PROTOCOL;

    size_t capacity = 256 + strlen(stream->method) + strlen(stream->path) + strlen(stream->authority);
    for (size_t i = 0; i < stream->headers_count; i++)
    {
        size_t name_len = strlen(stream->headers[i].name);
        size_t value_len = strlen(stream->headers[i].value);
        if (capacity > SIZE_MAX - name_len - value_len - 4)
            return cHTTPX_ERR_LIMIT;
        capacity += name_len + value_len + 4;
    }
    capacity += 64;

    size_t header_limit = connection->server->max_header_size ? connection->server->max_header_size : BUFFER_SIZE - 1;
    if (capacity > header_limit + 512)
        return cHTTPX_ERR_LIMIT;

    char* buffer = malloc(capacity);
    if (!buffer)
        return cHTTPX_ERR_MEMORY;

    size_t used = 0;
    int written = snprintf(buffer, capacity, "%s %s %s\r\n", stream->method, stream->path, CHTTPX_H2_PROTOCOL);
    if (written < 0 || (size_t)written >= capacity)
        goto limit_error;
    used = (size_t)written;

    bool has_host = false;
    bool has_content_length = false;
    for (size_t i = 0; i < stream->headers_count; i++)
    {
        const chttpx_header_t* header = &stream->headers[i];
        if (h2_forbidden_header(header->name))
            continue;
        if (strcasecmp(header->name, "host") == 0)
            has_host = true;
        if (strcasecmp(header->name, "content-length") == 0)
            has_content_length = true;

        written = snprintf(buffer + used, capacity - used, "%s: %s\r\n", header->name, header->value);
        if (written < 0 || (size_t)written >= capacity - used)
            goto limit_error;
        used += (size_t)written;
    }

    if (!has_host && stream->authority[0])
    {
        written = snprintf(buffer + used, capacity - used, "Host: %s\r\n", stream->authority);
        if (written < 0 || (size_t)written >= capacity - used)
            goto limit_error;
        used += (size_t)written;
    }

    if (!has_content_length)
    {
        written = snprintf(buffer + used, capacity - used, "Content-Length: %zu\r\n", stream->body_size);
        if (written < 0 || (size_t)written >= capacity - used)
            goto limit_error;
        used += (size_t)written;
    }

    if (capacity - used < 3)
        goto limit_error;
    memcpy(buffer + used, "\r\n", 3);
    used += 2;

    if (used > header_limit)
        goto limit_error;

    *output = buffer;
    *output_size = used;
    return cHTTPX_OK;

limit_error:
    free(buffer);
    return cHTTPX_ERR_LIMIT;
}

/**
 * nghttp2 send callback writing to the connection socket.
 *
 * @param session Active nghttp2 session for the callback.
 * @param data Payload bytes for the current chunk.
 * @param length Buffer capacity or number of bytes to transfer.
 * @param flags Frame or DATA flags supplied by nghttp2.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Bytes transferred or a negative nghttp2 status.
 */
static ssize_t h2_server_send(nghttp2_session* session, const uint8_t* data, size_t length, int flags, void* user_data)
{
    (void)session;
    (void)flags;
    chttpx_h2_server_t* connection = user_data;
    if (!connection)
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    return _chttpx_io_send_all(connection->fd, connection->tls_session, data, length) == cHTTPX_OK ? (ssize_t)length
               : NGHTTP2_ERR_CALLBACK_FAILURE;
}

static void h2_sse_reap_chunks(chttpx_h2_stream_t* stream)
{
    while (stream && stream->sse_chunks && stream->sse_chunks->completed)
    {
        chttpx_h2_sse_chunk_t* chunk = stream->sse_chunks;
        stream->sse_chunks = chunk->next;
        if (!stream->sse_chunks)
            stream->sse_chunks_tail = NULL;
        free(chunk->data);
        free(chunk);
    }
}

static ssize_t h2_sse_read(nghttp2_session* session, int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags, nghttp2_data_source* source, void* user_data)
{
    (void)session;
    (void)stream_id;
    (void)user_data;
    chttpx_h2_sse_chunk_t* chunk = source ? source->ptr : NULL;
    if (!chunk || !data_flags)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    size_t remaining = chunk->size - chunk->offset;
    size_t take = remaining < length ? remaining : length;
    if (take)
        memcpy(buf, chunk->data + chunk->offset, take);
    chunk->offset += take;

    if (chunk->offset >= chunk->size)
    {
        chunk->completed = true;
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return (ssize_t)take;
}

static int h2_sse_queue(chttpx_h2_stream_t* stream, const void* data, size_t size, uint8_t flags)
{
    if (!stream || !stream->connection || !stream->connection->session || stream->closed)
        return cHTTPX_ERR_STATE;

    chttpx_h2_sse_chunk_t* chunk = calloc(1, sizeof(*chunk));
    if (!chunk)
        return cHTTPX_ERR_MEMORY;

    if (size)
    {
        chunk->data = malloc(size);
        if (!chunk->data)
        {
            free(chunk);
            return cHTTPX_ERR_MEMORY;
        }
        memcpy(chunk->data, data, size);
    }
    chunk->size = size;

    if (stream->sse_chunks_tail)
        stream->sse_chunks_tail->next = chunk;
    else
        stream->sse_chunks = chunk;
    stream->sse_chunks_tail = chunk;

    nghttp2_data_provider provider = {
        .source = {.ptr = chunk},
        .read_callback = h2_sse_read,
    };

    int rv = nghttp2_submit_data(stream->connection->session, flags, stream->id, &provider);
    if (rv != 0)
    {
        if (stream->sse_chunks == chunk)
            stream->sse_chunks = NULL;
        else
        {
            chttpx_h2_sse_chunk_t* previous = stream->sse_chunks;
            while (previous && previous->next != chunk)
                previous = previous->next;
            if (previous)
                previous->next = NULL;
        }
        stream->sse_chunks_tail = stream->sse_chunks;
        while (stream->sse_chunks_tail && stream->sse_chunks_tail->next)
            stream->sse_chunks_tail = stream->sse_chunks_tail->next;
        free(chunk->data);
        free(chunk);
        stream->sse_connected = false;
        return cHTTPX_ERR_PROTOCOL;
    }

    if (nghttp2_session_send(stream->connection->session) != 0)
    {
        stream->sse_connected = false;
        return cHTTPX_ERR_IO;
    }

    while (!chunk->completed && !stream->closed &&
           !__atomic_load_n(&stream->connection->server->shutdown_requested, __ATOMIC_ACQUIRE))
    {
        unsigned char input[BUFFER_SIZE];
        int received = _chttpx_io_recv(stream->connection->fd, stream->connection->tls_session, input, sizeof(input));
        if (received <= 0)
        {
            stream->sse_connected = false;
            return received == cHTTPX_ERR_TIMEOUT ? cHTTPX_ERR_TIMEOUT : cHTTPX_ERR_IO;
        }

        size_t offset = 0;
        while (offset < (size_t)received)
        {
            ssize_t consumed = nghttp2_session_mem_recv(stream->connection->session, input + offset, (size_t)received - offset);
            if (consumed <= 0)
            {
                stream->sse_connected = false;
                return cHTTPX_ERR_PROTOCOL;
            }
            offset += (size_t)consumed;
        }

        if (nghttp2_session_send(stream->connection->session) != 0)
        {
            stream->sse_connected = false;
            return cHTTPX_ERR_IO;
        }
    }

    bool closed = stream->closed;
    h2_sse_reap_chunks(stream);
    if (closed && !(flags & NGHTTP2_FLAG_END_STREAM))
        return cHTTPX_ERR_STATE;
    if (__atomic_load_n(&stream->connection->server->shutdown_requested, __ATOMIC_ACQUIRE) && !(flags & NGHTTP2_FLAG_END_STREAM))
        return cHTTPX_ERR_STATE;
    return cHTTPX_OK;
}

static int h2_sse_open(void* context, const struct chttpx_response* response)
{
    chttpx_h2_stream_t* stream = context;
    if (!stream || !stream->connection || !response || stream->closed || stream->sse_open)
        return cHTTPX_ERR_STATE;

    char status_text[4];
    snprintf(status_text, sizeof(status_text), "%03d", response->status);

    nghttp2_nv headers[MAX_HEADERS + 2];
    char names[MAX_HEADERS][MAX_HEADER_NAME];
    size_t count = 0;
    headers[count++] = h2_nv(":status", status_text);
    headers[count++] = h2_nv("content-type", response->content_type ? response->content_type : cHTTPX_CTYPE_SSE);

    for (size_t i = 0; i < response->headers_count && count < CHTTPX_ARRAY_LEN(headers); i++)
    {
        if (h2_forbidden_header(response->headers[i].name) ||
            strcasecmp(response->headers[i].name, "content-length") == 0 ||
            strcasecmp(response->headers[i].name, "content-type") == 0)
            continue;

        size_t name_size = strlen(response->headers[i].name);
        if (name_size >= sizeof(names[0]))
            return cHTTPX_ERR_LIMIT;
        for (size_t j = 0; j <= name_size; j++)
            names[i][j] = (char)tolower((unsigned char)response->headers[i].name[j]);
        headers[count++] = h2_nv(names[i], response->headers[i].value);
    }

    int rv = nghttp2_submit_headers(stream->connection->session, NGHTTP2_FLAG_NONE, stream->id, NULL, headers, count, NULL);
    if (rv != 0)
        return cHTTPX_ERR_PROTOCOL;

    stream->responded = true;
    stream->sse_open = true;
    stream->sse_connected = true;

    if (nghttp2_session_send(stream->connection->session) != 0)
    {
        stream->sse_connected = false;
        return cHTTPX_ERR_IO;
    }
    return cHTTPX_OK;
}

static int h2_sse_write(void* context, const void* data, size_t size)
{
    chttpx_h2_stream_t* stream = context;
    if (!stream || !data || !size || !stream->sse_open || stream->sse_closing || stream->closed ||
        !stream->connection || __atomic_load_n(&stream->connection->server->shutdown_requested, __ATOMIC_ACQUIRE))
        return cHTTPX_ERR_STATE;
    return h2_sse_queue(stream, data, size, NGHTTP2_FLAG_NONE);
}

static int h2_sse_close(void* context)
{
    chttpx_h2_stream_t* stream = context;
    if (!stream || !stream->sse_open || stream->closed)
        return cHTTPX_OK;
    if (stream->sse_closing)
        return cHTTPX_OK;

    stream->sse_closing = true;
    stream->sse_connected = false;
    return h2_sse_queue(stream, NULL, 0, NGHTTP2_FLAG_END_STREAM);
}

static bool h2_sse_connected(void* context)
{
    chttpx_h2_stream_t* stream = context;
    if (!stream || !stream->connection || !stream->sse_open || stream->sse_closing || stream->closed || !stream->sse_connected)
        return false;
    return !__atomic_load_n(&stream->connection->server->shutdown_requested, __ATOMIC_ACQUIRE);
}

/**
 * nghttp2 data provider for serialized response bodies.
 *
 * @param session Active nghttp2 session for the callback.
 * @param stream_id HTTP/2 stream identifier.
 * @param buf Output buffer filled by the nghttp2 data provider.
 * @param length Buffer capacity or number of bytes to transfer.
 * @param data_flags Out flags telling nghttp2 when body transmission ends.
 * @param source Originating request for inherited metadata.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Bytes transferred or a negative nghttp2 status.
 */
static ssize_t h2_response_read(nghttp2_session* session, int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags, nghttp2_data_source* source, void* user_data)
{
    (void)session;
    (void)stream_id;
    (void)user_data;
    chttpx_h2_stream_t* stream = source ? source->ptr : NULL;
    if (!stream || !data_flags)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    size_t body_size = stream->response_size - stream->response_body_offset;
    size_t remaining = body_size - stream->response_body_sent;
    size_t take = remaining < length ? remaining : length;
    if (take)
        memcpy(buf, stream->response + stream->response_body_offset + stream->response_body_sent, take);
    stream->response_body_sent += take;
    if (stream->response_body_sent >= body_size)
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    return (ssize_t)take;
}

/**
 * Submit a minimal HTTP/2 response without a body.
 *
 * @param connection Server-side HTTP/2 connection state.
 * @param stream HTTP/2 stream being decoded, buffered, or responded to.
 * @param status HTTP status code for the response.
 * @param content_type Content-Type header value.
 * @return Zero on success or a negative error code.
 */
static int h2_submit_text_response(chttpx_h2_server_t* connection, chttpx_h2_stream_t* stream, int status, const char* content_type)
{
    char status_text[4];
    snprintf(status_text, sizeof(status_text), "%03d", status);

    nghttp2_nv headers[3];
    size_t count = 0;
    headers[count++] = h2_nv(":status", status_text);
    headers[count++] = h2_nv("content-length", "0");
    if (content_type)
        headers[count++] = h2_nv("content-type", content_type);

    int rv = nghttp2_submit_response(connection->session, stream->id, headers, count, NULL);
    if (rv == 0)
        stream->responded = true;
    return rv;
}

/**
 * Submit a full response parsed from HTTP/1 text.
 *
 * @param connection Server-side HTTP/2 connection state.
 * @param stream HTTP/2 stream being decoded, buffered, or responded to.
 * @return Zero on success or a negative error code.
 */
static int h2_submit_serialized_response(chttpx_h2_server_t* connection, chttpx_h2_stream_t* stream)
{
    if (!connection || !stream || !stream->response)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    char* delimiter = strstr(stream->response, "\r\n\r\n");
    if (!delimiter)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    stream->response_body_offset = (size_t)(delimiter - stream->response) + 4;
    if (stream->response_body_offset > stream->response_size)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    char* line_end = strstr(stream->response, "\r\n");
    if (!line_end)
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    *line_end = '\0';

    int status = 0;
    if (sscanf(stream->response, CHTTPX_H2_PROTOCOL " %d", &status) != 1 || status < 100 || status > 599)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    char status_text[4];
    snprintf(status_text, sizeof(status_text), "%03d", status);

    nghttp2_nv headers[MAX_HEADERS + 4];
    size_t count = 0;
    headers[count++] = h2_nv(":status", status_text);

    char* cursor = line_end + 2;
    while (cursor < delimiter && *cursor)
    {
        char* end = strstr(cursor, "\r\n");
        if (!end || end > delimiter)
            break;
        *end = '\0';

        char* colon = strchr(cursor, ':');
        if (colon)
        {
            *colon = '\0';
            char* value = colon + 1;
            while (*value == ' ' || *value == '\t')
                value++;
            for (char* p = cursor; *p; p++)
                *p = (char)tolower((unsigned char)*p);

            if (!h2_forbidden_header(cursor) && count < CHTTPX_ARRAY_LEN(headers))
                headers[count++] = h2_nv(cursor, value);
        }
        cursor = end + 2;
    }

    size_t body_size = stream->response_size - stream->response_body_offset;
    nghttp2_data_provider provider = {
        .source = {.ptr = stream},
        .read_callback = h2_response_read,
    };

    int rv = nghttp2_submit_response(connection->session, stream->id, headers, count, body_size ? &provider : NULL);
    if (rv == 0)
        stream->responded = true;
    return rv;
}

/**
 * Dispatch one complete HTTP/2 request to the server core.
 *
 * @param connection Server-side HTTP/2 connection state.
 * @param stream HTTP/2 stream being decoded, buffered, or responded to.
 * @return Zero on success or a negative error code.
 */
static int h2_process_request(chttpx_h2_server_t* connection, chttpx_h2_stream_t* stream)
{
    if (!connection || !stream || stream->responded)
        return 0;

    if (stream->error_status)
        return h2_submit_text_response(connection, stream, stream->error_status, cHTTPX_CTYPE_TEXT);

    char* headers = NULL;
    size_t header_size = 0;
    int build_result = h2_build_request_text(connection, stream, &headers, &header_size);
    if (build_result != cHTTPX_OK)
        return h2_submit_text_response(connection, stream,
            build_result == cHTTPX_ERR_LIMIT ? cHTTPX_StatusRequestHeaderFieldsTooLarge : cHTTPX_StatusBadRequest,
            cHTTPX_CTYPE_TEXT);

    unsigned char* body = stream->body;
    size_t body_size = stream->body_size;
    stream->body = NULL;
    stream->body_size = 0;
    stream->body_capacity = 0;

    char* response = NULL;
    size_t response_size = 0;
    chttpx_stream_transport_t stream_transport = {
        .context = stream,
        .open = h2_sse_open,
        .write = h2_sse_write,
        .close = h2_sse_close,
        .connected = h2_sse_connected,
    };

    stream->handler_active = true;
    int execute_result = _chttpx_execute_prefetched(connection->server, connection->fd, connection->tls_session,
        headers, header_size, body, body_size, NULL, body_size, &stream_transport, &response, &response_size);
    free(headers);

    if (execute_result == cHTTPX_OK && !response && response_size == SIZE_MAX)
    {
        stream->handler_active = false;
        if (stream->closed)
            h2_stream_free(stream);
        return 0;
    }

    stream->handler_active = false;
    if (stream->closed)
    {
        free(response);
        h2_stream_free(stream);
        return 0;
    }

    if (execute_result != cHTTPX_OK || !response)
    {
        free(response);
        return h2_submit_text_response(connection, stream, cHTTPX_StatusInternalServerError, cHTTPX_CTYPE_TEXT);
    }

    stream->response = response;
    stream->response_size = response_size;
    return h2_submit_serialized_response(connection, stream);
}

static int h2_process_ready(chttpx_h2_server_t* connection)
{
    while (connection && connection->ready_head)
    {
        chttpx_h2_stream_t* stream = connection->ready_head;
        connection->ready_head = stream->ready_next;
        if (!connection->ready_head)
            connection->ready_tail = NULL;
        stream->ready_next = NULL;
        stream->ready_queued = false;

        int rv = h2_process_request(connection, stream);
        if (rv != 0)
            return rv;
    }
    return 0;
}

/**
 * Allocate stream state when request headers begin.
 *
 * @param session Active nghttp2 session for the callback.
 * @param frame Incoming nghttp2 frame metadata.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Zero on success or a negative error code.
 */
static int h2_server_begin_headers(nghttp2_session* session, const nghttp2_frame* frame, void* user_data)
{
    chttpx_h2_server_t* connection = user_data;
    if (!connection || !frame || frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST)
        return 0;

    chttpx_h2_stream_t* stream = calloc(1, sizeof(*stream));
    if (!stream)
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    stream->id = frame->hd.stream_id;
    stream->connection = connection;

    int rv = nghttp2_session_set_stream_user_data(session, stream->id, stream);
    if (rv != 0)
    {
        free(stream);
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return 0;
}

/**
 * Handle one request header field on the server session.
 *
 * @param session Active nghttp2 session for the callback.
 * @param frame Incoming nghttp2 frame metadata.
 * @param name Header field name bytes.
 * @param namelen Header name length in bytes.
 * @param value Header field value bytes.
 * @param valuelen Header value length in bytes.
 * @param flags Frame or DATA flags supplied by nghttp2.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Zero on success or a negative error code.
 */
static int h2_server_header(nghttp2_session* session, const nghttp2_frame* frame, const uint8_t* name, size_t namelen, const uint8_t* value, size_t valuelen, uint8_t flags, void* user_data)
{
    (void)flags;
    (void)user_data;
    if (!frame || frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST)
        return 0;
    chttpx_h2_stream_t* stream = h2_stream(session, frame->hd.stream_id);
    return h2_stream_add_header(stream, name, namelen, value, valuelen);
}

/**
 * Handle one request DATA chunk on the server session.
 *
 * @param session Active nghttp2 session for the callback.
 * @param flags Frame or DATA flags supplied by nghttp2.
 * @param stream_id HTTP/2 stream identifier.
 * @param data Payload bytes for the current chunk.
 * @param len Number of payload bytes in the chunk.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Zero on success or a negative error code.
 */
static int h2_server_data(nghttp2_session* session, uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len, void* user_data)
{
    (void)flags;
    chttpx_h2_server_t* connection = user_data;
    chttpx_h2_stream_t* stream = h2_stream(session, stream_id);
    return h2_append_body(connection, stream, data, len);
}

/**
 * Finalize request handling when END_STREAM is seen.
 *
 * @param session Active nghttp2 session for the callback.
 * @param frame Incoming nghttp2 frame metadata.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Zero on success or a negative error code.
 */
static int h2_server_frame_recv(nghttp2_session* session, const nghttp2_frame* frame, void* user_data)
{
    chttpx_h2_server_t* connection = user_data;
    if (!connection || !frame)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    if (frame->hd.stream_id <= 0 || !(frame->hd.flags & NGHTTP2_FLAG_END_STREAM))
        return 0;

    if (frame->hd.type != NGHTTP2_HEADERS && frame->hd.type != NGHTTP2_DATA)
        return 0;

    chttpx_h2_stream_t* stream = h2_stream(session, frame->hd.stream_id);
    if (!stream)
        return 0;

    if (!stream->request_ready)
    {
        stream->request_ready = true;
        h2_ready_push(connection, stream);
    }
    return 0;
}

/**
 * Free stream state when nghttp2 closes a stream.
 *
 * @param session Active nghttp2 session for the callback.
 * @param stream_id HTTP/2 stream identifier.
 * @param error_code nghttp2 stream error code reported on close.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Zero on success or a negative error code.
 */
static int h2_server_stream_close(nghttp2_session* session, int32_t stream_id, uint32_t error_code, void* user_data)
{
    (void)error_code;
    chttpx_h2_server_t* connection = user_data;
    chttpx_h2_stream_t* stream = h2_stream(session, stream_id);
    if (stream)
    {
        if (connection)
            h2_ready_remove(connection, stream);
        stream->closed = true;
        stream->sse_connected = false;
        nghttp2_session_set_stream_user_data(session, stream_id, NULL);
        if (!stream->handler_active)
            h2_stream_free(stream);
    }
    return 0;
}

/**
 * Serve one HTTP/2 connection until shutdown or I/O failure.
 *
 * @param server Server instance.
 * @param client_fd Connected client socket.
 * @param tls_session OpenSSL session for TLS I/O, or NULL for cleartext.
 * @return Zero on success or a negative error code.
 */
int _chttpx_http2_serve(chttpx_serv_t* server, chttpx_socket_t client_fd, void* tls_session)
{
    if (!server)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_h2_server_t connection = {
        .server = server,
        .fd = client_fd,
        .tls_session = tls_session,
    };

    nghttp2_session_callbacks* callbacks = NULL;
    if (nghttp2_session_callbacks_new(&callbacks) != 0)
        return cHTTPX_ERR_MEMORY;

    nghttp2_session_callbacks_set_send_callback(callbacks, h2_server_send);
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, h2_server_begin_headers);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, h2_server_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, h2_server_data);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, h2_server_frame_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, h2_server_stream_close);

    int rv = nghttp2_session_server_new(&connection.session, callbacks, &connection);
    nghttp2_session_callbacks_del(callbacks);
    if (rv != 0)
        return cHTTPX_ERR_MEMORY;

    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 128},
    };
    if (nghttp2_submit_settings(connection.session, NGHTTP2_FLAG_NONE, settings, CHTTPX_ARRAY_LEN(settings)) != 0 ||
        nghttp2_session_send(connection.session) != 0)
    {
        nghttp2_session_del(connection.session);
        return cHTTPX_ERR_PROTOCOL;
    }

    unsigned char buffer[BUFFER_SIZE];
    int result = cHTTPX_OK;

    while (!__atomic_load_n(&server->shutdown_requested, __ATOMIC_ACQUIRE) && (nghttp2_session_want_read(connection.session) || nghttp2_session_want_write(connection.session)))
    {
        int received = _chttpx_io_recv(client_fd, tls_session, buffer, sizeof(buffer));
        if (received == 0)
            break;
        if (received < 0)
        {
            result = received == cHTTPX_ERR_TIMEOUT ? cHTTPX_ERR_TIMEOUT : cHTTPX_ERR_IO;
            break;
        }

        size_t offset = 0;
        while (offset < (size_t)received)
        {
            ssize_t consumed = nghttp2_session_mem_recv(connection.session, buffer + offset, (size_t)received - offset);
            if (consumed < 0)
            {
                result = cHTTPX_ERR_PROTOCOL;
                goto done;
            }
            if (consumed == 0)
            {
                result = cHTTPX_ERR_PROTOCOL;
                goto done;
            }
            offset += (size_t)consumed;
        }

        if (h2_process_ready(&connection) != 0)
        {
            result = cHTTPX_ERR_PROTOCOL;
            break;
        }

        if (nghttp2_session_send(connection.session) != 0)
        {
            result = cHTTPX_ERR_IO;
            break;
        }
    }

done:
    nghttp2_session_del(connection.session);
    return result;
}

/**
 * Parse http(s) base URL into host, port, and path.
 *
 * @param base_url Remote http(s) base URL.
 * @param parsed Output structure receiving parsed URL fields.
 * @return Zero on success or a negative error code.
 */
static int h2_parse_url(const char* base_url, chttpx_h2_url_t* parsed)
{
    if (!base_url || !parsed)
        return 0;

    memset(parsed, 0, sizeof(*parsed));
    const char* cursor = NULL;
    if (strncmp(base_url, "https://", 8) == 0)
    {
        parsed->tls = true;
        cursor = base_url + 8;
    }
    else if (strncmp(base_url, "http://", 7) == 0)
        cursor = base_url + 7;
    else
        return 0;

    const char* slash = strchr(cursor, '/');
    size_t authority_len = slash ? (size_t)(slash - cursor) : strlen(cursor);
    if (!authority_len || authority_len >= CHTTPX_H2_MAX_AUTHORITY)
        return 0;

    char authority[CHTTPX_H2_MAX_AUTHORITY];
    memcpy(authority, cursor, authority_len);
    authority[authority_len] = '\0';

    if (authority[0] == '[')
    {
        char* close = strchr(authority, ']');
        if (!close)
            return 0;
        *close = '\0';
        snprintf(parsed->host, sizeof(parsed->host), "%s", authority + 1);
        if (close[1] == ':')
            snprintf(parsed->port, sizeof(parsed->port), "%s", close + 2);
    }
    else
    {
        char* colon = strrchr(authority, ':');
        if (colon && strchr(authority, ':') == colon)
        {
            *colon = '\0';
            snprintf(parsed->port, sizeof(parsed->port), "%s", colon + 1);
        }
        snprintf(parsed->host, sizeof(parsed->host), "%s", authority);
    }

    if (!parsed->host[0])
        return 0;
    if (!parsed->port[0])
        snprintf(parsed->port, sizeof(parsed->port), "%s", parsed->tls ? "443" : "80");
    if (slash)
        snprintf(parsed->base_path, sizeof(parsed->base_path), "%s", slash);
    return 1;
}

/**
 * Apply outbound call read/write timeouts on a socket.
 *
 * @param fd Socket descriptor.
 * @return Zero on success or a negative error code.
 */
static int h2_set_call_timeouts(chttpx_socket_t fd)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    DWORD timeout_ms = CHTTPX_H2_CALL_TIMEOUT_SEC * 1000U;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) != 0)
        return cHTTPX_ERR_UNAVAILABLE;
#else
    struct timeval timeout = {.tv_sec = CHTTPX_H2_CALL_TIMEOUT_SEC, .tv_usec = 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
        return cHTTPX_ERR_UNAVAILABLE;
#endif
    return cHTTPX_OK;
}

/**
 * TCP-connect to a parsed remote URL.
 *
 * @param remote Parsed remote host, port, and base path.
 * @param connected Output socket set when TCP connect succeeds.
 * @return Zero on success or a negative error code.
 */
static int h2_connect(const chttpx_h2_url_t* remote, chttpx_socket_t* connected)
{
    if (!remote || !connected)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    struct addrinfo hints;
    struct addrinfo* result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(remote->host, remote->port, &hints, &result) != 0)
        return cHTTPX_ERR_UNAVAILABLE;

    int final_result = cHTTPX_ERR_UNAVAILABLE;
    for (struct addrinfo* current = result; current; current = current->ai_next)
    {
        chttpx_socket_t fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
#ifdef CHTTPX_PLATFORM_WINDOWS
        if (fd == INVALID_SOCKET)
#else
        if (fd < 0)
#endif
            continue;

        if (h2_set_call_timeouts(fd) != cHTTPX_OK)
        {
            chttpx_close(fd);
            continue;
        }

        if (connect(fd, current->ai_addr, (int)current->ai_addrlen) == 0)
        {
            *connected = fd;
            final_result = cHTTPX_OK;
            break;
        }
        chttpx_close(fd);
    }

    freeaddrinfo(result);
    return final_result;
}

/**
 * Map arbitrary content types to library constants.
 *
 * @param content_type Content-Type value from the remote response.
 * @return Stable library Content-Type constant for the given value.
 */
static const char* h2_stable_content_type(const char* content_type)
{
    if (!content_type || !*content_type)
        return cHTTPX_CTYPE_OCTET;
#define CHTTPX_H2_MATCH_CTYPE(value) if (strcasecmp(content_type, value) == 0) return value
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_HTML);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_TEXT);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_XML);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_CSS);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_CSV);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_JSON);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_FORM);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_MULTI);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_OCTET);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_JS);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_PNG);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_JPEG);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_GIF);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_WEBP);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_SVG);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_MP3);
    CHTTPX_H2_MATCH_CTYPE(cHTTPX_CTYPE_MP4);
#undef CHTTPX_H2_MATCH_CTYPE
    return cHTTPX_CTYPE_OCTET;
}

/**
 * nghttp2 send callback for outbound client sessions.
 *
 * @param session Active nghttp2 session for the callback.
 * @param data Payload bytes for the current chunk.
 * @param length Buffer capacity or number of bytes to transfer.
 * @param flags Frame or DATA flags supplied by nghttp2.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Bytes transferred or a negative nghttp2 status.
 */
static ssize_t h2_client_send(nghttp2_session* session, const uint8_t* data, size_t length, int flags, void* user_data)
{
    (void)session;
    (void)flags;
    chttpx_h2_client_t* client = user_data;
    if (!client)
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    return _chttpx_io_send_all(client->fd, client->tls_session, data, length) == cHTTPX_OK ? (ssize_t)length
               : NGHTTP2_ERR_CALLBACK_FAILURE;
}

/**
 * Collect response headers for an outbound HTTP/2 call.
 *
 * @param session Active nghttp2 session for the callback.
 * @param frame Incoming nghttp2 frame metadata.
 * @param name Header field name bytes.
 * @param namelen Header name length in bytes.
 * @param value Header field value bytes.
 * @param valuelen Header value length in bytes.
 * @param flags Frame or DATA flags supplied by nghttp2.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Zero on success or a negative error code.
 */
static int h2_client_header(nghttp2_session* session, const nghttp2_frame* frame, const uint8_t* name, size_t namelen, const uint8_t* value, size_t valuelen, uint8_t flags, void* user_data)
{
    (void)session;
    (void)flags;
    chttpx_h2_client_t* client = user_data;
    if (!client || !frame || frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_RESPONSE)
        return 0;

    if (namelen == 7 && memcmp(name, ":status", 7) == 0)
    {
        char status[4] = {0};
        if (valuelen != 3)
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        memcpy(status, value, 3);
        client->status = atoi(status);
    }
    else if (namelen > 0 && name[0] != ':')
    {
        if (namelen == 12 && memcmp(name, "content-type", 12) == 0)
        {
            size_t copy = valuelen < sizeof(client->content_type) - 1 ? valuelen : sizeof(client->content_type) - 1;
            memcpy(client->content_type, value, copy);
            client->content_type[copy] = '\0';
            return 0;
        }

        /*
         * chttpx_response_t owns representation metadata separately from the
         * generic header array. Copying Content-Length here would make a
         * proxied response emit two content-length fields when it is serialized
         * again by the receiving server.
         */
        if (namelen == 14 && memcmp(name, "content-length", 14) == 0)
            return 0;

        if (client->headers_count < MAX_HEADERS && namelen < MAX_HEADER_NAME && valuelen < MAX_HEADER_VALUE)
        {
            chttpx_header_t* header = &client->headers[client->headers_count++];
            memcpy(header->name, name, namelen);
            header->name[namelen] = '\0';
            memcpy(header->value, value, valuelen);
            header->value[valuelen] = '\0';
        }
    }
    return 0;
}

/**
 * Buffer response body bytes for an outbound HTTP/2 call.
 *
 * @param session Active nghttp2 session for the callback.
 * @param flags Frame or DATA flags supplied by nghttp2.
 * @param stream_id HTTP/2 stream identifier.
 * @param data Payload bytes for the current chunk.
 * @param len Number of payload bytes in the chunk.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Zero on success or a negative error code.
 */
static int h2_client_data(nghttp2_session* session, uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len, void* user_data)
{
    (void)session;
    (void)flags;
    chttpx_h2_client_t* client = user_data;
    if (!client || stream_id != client->stream_id)
        return 0;

    if (client->body_size > MAX_BUFFER_BODY || len > MAX_BUFFER_BODY - client->body_size)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    size_t required = client->body_size + len + 1;
    if (required > client->body_capacity)
    {
        size_t capacity = client->body_capacity ? client->body_capacity : 4096;
        while (capacity < required)
        {
            if (capacity > MAX_BUFFER_BODY / 2)
            {
                capacity = MAX_BUFFER_BODY + 1;
                break;
            }
            capacity *= 2;
        }
        unsigned char* resized = realloc(client->body, capacity);
        if (!resized)
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        client->body = resized;
        client->body_capacity = capacity;
    }

    if (len)
        memcpy(client->body + client->body_size, data, len);
    client->body_size += len;
    client->body[client->body_size] = '\0';
    return 0;
}

/**
 * Mark client stream complete when nghttp2 closes it.
 *
 * @param session Active nghttp2 session for the callback.
 * @param stream_id HTTP/2 stream identifier.
 * @param error_code nghttp2 stream error code reported on close.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Zero on success or a negative error code.
 */
static int h2_client_stream_close(nghttp2_session* session, int32_t stream_id, uint32_t error_code, void* user_data)
{
    (void)session;
    (void)error_code;
    chttpx_h2_client_t* client = user_data;
    if (client && stream_id == client->stream_id)
        client->done = true;
    return 0;
}

/**
 * nghttp2 data provider for outbound request bodies.
 *
 * @param session Active nghttp2 session for the callback.
 * @param stream_id HTTP/2 stream identifier.
 * @param buf Output buffer filled by the nghttp2 data provider.
 * @param length Buffer capacity or number of bytes to transfer.
 * @param data_flags Out flags telling nghttp2 when body transmission ends.
 * @param source Originating request for inherited metadata.
 * @param user_data User pointer registered with the nghttp2 callback.
 * @return Bytes transferred or a negative nghttp2 status.
 */
static ssize_t h2_client_body_read(nghttp2_session* session, int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags, nghttp2_data_source* source, void* user_data)
{
    (void)session;
    (void)stream_id;
    (void)user_data;
    chttpx_h2_client_body_t* body = source ? source->ptr : NULL;
    if (!body || !data_flags)
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    size_t remaining = body->size - body->offset;
    size_t take = remaining < length ? remaining : length;
    if (take)
        memcpy(buf, body->body + body->offset, take);
    body->offset += take;
    if (body->offset >= body->size)
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    return (ssize_t)take;
}

int _chttpx_http2_call(chttpx_request_t* source, const char* base_url, const chttpx_tls_client_config_t* tls_config,
                       const char* method, const char* path, const void* body, size_t body_size, const char* content_type,
                       chttpx_response_t* res)
{
    if (!source || !base_url || !method || !path || !res || (body_size && !body))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_h2_url_t remote;
    if (!h2_parse_url(base_url, &remote))
        return cHTTPX_ERR_PROTOCOL;

#ifdef CHTTPX_PLATFORM_WINDOWS
    chttpx_socket_t fd = INVALID_SOCKET;
#else
    chttpx_socket_t fd = -1;
#endif
    int result = h2_connect(&remote, &fd);
    if (result != cHTTPX_OK)
        return result;

    void* tls_context = NULL;
    void* tls_session = NULL;
    if (remote.tls)
    {
        result = _chttpx_tls_client_connect(fd, remote.host, tls_config, &tls_context, &tls_session);
        if (result != cHTTPX_OK)
        {
            chttpx_close(fd);
            return result;
        }
    }

    chttpx_h2_client_t client = {
        .fd = fd,
        .tls_session = tls_session,
    };

    nghttp2_session_callbacks* callbacks = NULL;
    if (nghttp2_session_callbacks_new(&callbacks) != 0)
    {
        result = cHTTPX_ERR_MEMORY;
        goto done;
    }

    nghttp2_session_callbacks_set_send_callback(callbacks, h2_client_send);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, h2_client_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, h2_client_data);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, h2_client_stream_close);

    if (nghttp2_session_client_new(&client.session, callbacks, &client) != 0)
    {
        nghttp2_session_callbacks_del(callbacks);
        result = cHTTPX_ERR_MEMORY;
        goto done;
    }
    nghttp2_session_callbacks_del(callbacks);

    if (nghttp2_submit_settings(client.session, NGHTTP2_FLAG_NONE, NULL, 0) != 0)
    {
        result = cHTTPX_ERR_PROTOCOL;
        goto done;
    }

    char full_path[CHTTPX_MAX_PATH];
    if (remote.base_path[0] && strcmp(remote.base_path, "/") != 0)
    {
        int n = snprintf(full_path, sizeof(full_path), "%s%s%s",
                         remote.base_path,
                         remote.base_path[strlen(remote.base_path) - 1] == '/' || path[0] == '/' ? "" : "/",
                         path);
        if (n < 0 || (size_t)n >= sizeof(full_path))
        {
            result = cHTTPX_ERR_LIMIT;
            goto done;
        }
    }
    else
        snprintf(full_path, sizeof(full_path), "%s", path);

    char authority[CHTTPX_H2_MAX_AUTHORITY + 24];
    bool default_port = (remote.tls && strcmp(remote.port, "443") == 0) || (!remote.tls && strcmp(remote.port, "80") == 0);
    if (default_port)
        snprintf(authority, sizeof(authority), "%s", remote.host);
    else
        snprintf(authority, sizeof(authority), "%s:%s", remote.host, remote.port);

    char content_length[32];
    snprintf(content_length, sizeof(content_length), "%zu", body_size);

    const char* selected_content_type = content_type && *content_type ? content_type
        : (source->content_type[0] ? source->content_type : cHTTPX_CTYPE_JSON);

    nghttp2_nv headers[MAX_HEADERS + 8];
    char lowercase_names[MAX_HEADERS][MAX_HEADER_NAME];
    size_t count = 0;
    headers[count++] = h2_nv(":method", method);
    headers[count++] = h2_nv(":scheme", remote.tls ? "https" : "http");
    headers[count++] = h2_nv(":authority", authority);
    headers[count++] = h2_nv(":path", full_path);
    headers[count++] = h2_nv("content-type", selected_content_type);
    headers[count++] = h2_nv("content-length", content_length);

    if (source->request_id[0] && !cHTTPX_HeaderGet(source, "X-Request-ID"))
        headers[count++] = h2_nv("x-request-id", source->request_id);
    if (source->language[0] && !cHTTPX_HeaderGet(source, "Accept-Language"))
        headers[count++] = h2_nv("accept-language", source->language);

    size_t copied_names = 0;
    for (size_t i = 0; i < source->headers_count && count < CHTTPX_ARRAY_LEN(headers); i++)
    {
        const char* name = source->headers[i].name;
        const char* value = source->headers[i].value;
        if (!name[0] || name[0] == ':' || h2_forbidden_header(name) ||
            strcasecmp(name, "host") == 0 ||
            strcasecmp(name, "content-length") == 0 ||
            strcasecmp(name, "content-type") == 0)
            continue;

        size_t name_len = strlen(name);
        if (name_len >= MAX_HEADER_NAME || copied_names >= MAX_HEADERS)
            continue;
        for (size_t j = 0; j < name_len; j++)
            lowercase_names[copied_names][j] = (char)tolower((unsigned char)name[j]);
        lowercase_names[copied_names][name_len] = '\0';
        headers[count++] = h2_nv(lowercase_names[copied_names], value);
        copied_names++;
    }

    chttpx_h2_client_body_t request_body = {
        .body = body,
        .size = body_size,
    };
    nghttp2_data_provider provider = {
        .source = {.ptr = &request_body},
        .read_callback = h2_client_body_read,
    };

    client.stream_id = nghttp2_submit_request(client.session, NULL, headers, count, body_size ? &provider : NULL, NULL);
    if (client.stream_id < 0 || nghttp2_session_send(client.session) != 0)
    {
        result = cHTTPX_ERR_PROTOCOL;
        goto done;
    }

    unsigned char buffer[BUFFER_SIZE];
    while (!client.done)
    {
        int received = _chttpx_io_recv(fd, tls_session, buffer, sizeof(buffer));
        if (received <= 0)
        {
            result = received == cHTTPX_ERR_TIMEOUT ? cHTTPX_ERR_TIMEOUT : cHTTPX_ERR_IO;
            goto done;
        }

        size_t offset = 0;
        while (offset < (size_t)received)
        {
            ssize_t consumed = nghttp2_session_mem_recv(client.session, buffer + offset, (size_t)received - offset);
            if (consumed <= 0)
            {
                result = cHTTPX_ERR_PROTOCOL;
                goto done;
            }
            offset += (size_t)consumed;
        }

        if (nghttp2_session_send(client.session) != 0)
        {
            result = cHTTPX_ERR_IO;
            goto done;
        }
    }

    if (client.status < 100 || client.status > 599)
    {
        result = cHTTPX_ERR_PROTOCOL;
        goto done;
    }

    *res = cHTTPX_ResBinary((uint16_t)client.status, h2_stable_content_type(client.content_type), client.body, client.body_size);
    if (!res->status)
    {
        result = cHTTPX_ERR_MEMORY;
        goto done;
    }

    for (size_t i = 0; i < client.headers_count; i++)
    {
        if (cHTTPX_HeaderAdd(res, client.headers[i].name, client.headers[i].value) != 0)
        {
            cHTTPX_ResponseCleanup(res);
            memset(res, 0, sizeof(*res));
            result = cHTTPX_ERR_LIMIT;
            goto done;
        }
    }
    result = cHTTPX_OK;

done:
    if (client.session)
        nghttp2_session_del(client.session);
    free(client.body);
    _chttpx_tls_client_close(tls_context, tls_session);
    chttpx_close(fd);
    return result;
}
