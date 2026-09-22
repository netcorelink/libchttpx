#include "cHTTPX_runtime.h"

#include "cHTTPX_event.h"
#include "cHTTPX_worker.h"
#include "cHTTPX_http.h"
#include "cHTTPX_metrics.h"
#include "cHTTPX_tls.h"
#include "cHTTPX_utils.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHTTPX_RUNTIME_EVENTS 256
#define CHTTPX_CHUNK_LINE_MAX 128

typedef enum
{
    CHTTPX_CONN_TLS,
    CHTTPX_CONN_READING_HEADERS,
    CHTTPX_CONN_READING_BODY,
    CHTTPX_CONN_PROCESSING,
    CHTTPX_CONN_WRITING,
    CHTTPX_CONN_CLOSING
} chttpx_connection_state_t;

typedef enum
{
    CHTTPX_CHUNK_SIZE,
    CHTTPX_CHUNK_SIZE_LF,
    CHTTPX_CHUNK_DATA,
    CHTTPX_CHUNK_DATA_CR,
    CHTTPX_CHUNK_DATA_LF,
    CHTTPX_CHUNK_TRAILER,
    CHTTPX_CHUNK_TRAILER_LF,
    CHTTPX_CHUNK_DONE
} chttpx_chunk_state_t;

struct chttpx_runtime;

typedef struct chttpx_connection
{
    struct chttpx_runtime* runtime;
    chttpx_serv_t* server;
    chttpx_socket_t fd;
    void* tls_session;
    chttpx_connection_state_t state;
    char* headers;
    size_t header_size;
    size_t header_capacity;
    size_t header_end;
    unsigned char* body;
    size_t body_size;
    size_t body_capacity;
    FILE* body_stream;
    size_t content_length;
    size_t body_received;
    size_t body_limit;
    bool chunked;
    bool memory_body;
    bool multipart_body;
    bool discard_body;
    chttpx_chunk_state_t chunk_state;
    char chunk_line[CHTTPX_CHUNK_LINE_MAX];
    size_t chunk_line_size;
    size_t chunk_remaining;
    size_t chunk_decoded;
    char* write_buffer;
    size_t write_size;
    size_t write_offset;
    int worker_result;
    uint64_t last_activity_ms;
    struct chttpx_connection* next;
    struct chttpx_connection* completion_next;
} chttpx_connection_t;

typedef struct chttpx_runtime
{
    chttpx_serv_t* server;
    chttpx_event_loop_t* event_loop;
    chttpx_worker_pool_t* worker_pool;
    bool stopping;
    bool shutdown_started;
    bool listener_registered;
#ifdef CHTTPX_PLATFORM_WINDOWS
    CRITICAL_SECTION completion_mutex;
#else
    pthread_mutex_t completion_mutex;
#endif
    chttpx_connection_t* completions;
    chttpx_connection_t* connections;
} chttpx_runtime_t;

int _chttpx_execute_prefetched(chttpx_serv_t* server, chttpx_socket_t client_fd, void* tls_session, char* headers, size_t header_size, unsigned char* body, size_t body_size, FILE* body_stream, size_t content_length, char** output, size_t* output_size);

static bool runtime_is_stopping(chttpx_runtime_t* runtime)
{
    return __atomic_load_n(&runtime->stopping, __ATOMIC_ACQUIRE);
}

static bool socket_valid(chttpx_socket_t fd)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    return fd != INVALID_SOCKET;
#else
    return fd >= 0;
#endif
}

static uint64_t monotonic_ms(void)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    return GetTickCount64();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
#endif
}

static void completion_lock(chttpx_runtime_t* runtime)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    EnterCriticalSection(&runtime->completion_mutex);
#else
    pthread_mutex_lock(&runtime->completion_mutex);
#endif
}

static void completion_unlock(chttpx_runtime_t* runtime)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    LeaveCriticalSection(&runtime->completion_mutex);
#else
    pthread_mutex_unlock(&runtime->completion_mutex);
#endif
}

static int sync_init(chttpx_runtime_t* runtime)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    InitializeCriticalSection(&runtime->completion_mutex);
    return 0;
#else
    return pthread_mutex_init(&runtime->completion_mutex, NULL) == 0 ? 0 : -1;
#endif
}

static void sync_destroy(chttpx_runtime_t* runtime)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    DeleteCriticalSection(&runtime->completion_mutex);
#else
    pthread_mutex_destroy(&runtime->completion_mutex);
#endif
}

static void completion_push(chttpx_runtime_t* runtime, chttpx_connection_t* connection)
{
    completion_lock(runtime);
    connection->completion_next = runtime->completions;
    runtime->completions = connection;
    completion_unlock(runtime);
    _chttpx_event_wake(runtime->event_loop);
}

static chttpx_connection_t* completion_take_all(chttpx_runtime_t* runtime)
{
    completion_lock(runtime);
    chttpx_connection_t* list = runtime->completions;
    runtime->completions = NULL;
    completion_unlock(runtime);
    return list;
}

static void runtime_worker_execute(void* job, void* context)
{
    chttpx_connection_t* connection = job;
    chttpx_runtime_t* runtime = context;

    unsigned char* body = connection->body;
    FILE* stream = connection->body_stream;
    connection->body = NULL;
    connection->body_stream = NULL;

    if (runtime_is_stopping(runtime))
    {
        free(body);

        if (stream)
            fclose(stream);

        connection->worker_result = cHTTPX_ERR_STATE;
    }
    else
    {
        connection->worker_result = _chttpx_execute_prefetched(connection->server, connection->fd, connection->tls_session, connection->headers, connection->header_end, body, connection->body_size, stream, connection->content_length, &connection->write_buffer, &connection->write_size);
    }

    free(connection->headers);
    connection->headers = NULL;
    connection->header_size = 0;
    connection->header_capacity = 0;

    completion_push(runtime, connection);
}

static void connection_unlink(chttpx_runtime_t* runtime, chttpx_connection_t* connection)
{
    chttpx_connection_t** current = &runtime->connections;
    while (*current)
    {
        if (*current == connection)
        {
            *current = connection->next;
            return;
        }
        current = &(*current)->next;
    }
}

static void connection_close(chttpx_runtime_t* runtime, chttpx_connection_t* connection)
{
    if (!runtime || !connection)
        return;

    connection->state = CHTTPX_CONN_CLOSING;
    _chttpx_event_del(runtime->event_loop, connection->fd);
    connection_unlink(runtime, connection);

    _chttpx_tls_session_close(connection->tls_session);
    connection->tls_session = NULL;

    if (socket_valid(connection->fd))
        chttpx_close(connection->fd);

    free(connection->headers);
    free(connection->body);

    if (connection->body_stream)
        fclose(connection->body_stream);

    free(connection->write_buffer);

    _chttpx_metrics_connection_closed(connection->server);
    __atomic_fetch_sub(&connection->server->current_clients, 1, __ATOMIC_SEQ_CST);

    free(connection);
}

static void connection_touch(chttpx_connection_t* connection)
{
    connection->last_activity_ms = monotonic_ms();
}

static bool span_equal_ci(const char* value, size_t value_size, const char* expected)
{
    size_t expected_size = strlen(expected);
    return value_size == expected_size && strncasecmp(value, expected, value_size) == 0;
}

static bool content_type_matches(const char* value, const char* expected)
{
    if (!value || !expected)
        return false;
    size_t expected_size = strlen(expected);
    if (strncasecmp(value, expected, expected_size) != 0)
        return false;
    char suffix = value[expected_size];
    return suffix == '\0' || suffix == ';' || suffix == ' ' || suffix == '\t';
}

static int parse_size_value(const char* value, size_t value_size, size_t* output)
{
    if (!value || !value_size || !output || value_size >= 32)
        return 0;

    char buffer[32];
    memcpy(buffer, value, value_size);
    buffer[value_size] = '\0';

    if (buffer[0] == '-')
        return 0;

    errno = 0;
    char* end = NULL;
    unsigned long long parsed = strtoull(buffer, &end, 10);

    if (errno == ERANGE || !end || *end || parsed > SIZE_MAX)
        return 0;

    *output = (size_t)parsed;
    return 1;
}

static int connection_prepare_body(chttpx_connection_t* connection)
{
    const char* start = connection->headers;
    const char* end = connection->headers + connection->header_end;
    const char* line = chttpx_memmem(start, connection->header_end, "\r\n", 2);
    if (!line)
        return cHTTPX_StatusBadRequest;
    line += 2;

    bool has_content_length = false;
    bool has_transfer_encoding = false;
    size_t content_length = 0;
    char content_type[512] = {0};

    while (line < end)
    {
        const char* line_end = chttpx_memmem(line, (size_t)(end - line), "\r\n", 2);
        if (!line_end)
            return cHTTPX_StatusBadRequest;
        if (line_end == line)
            break;

        const char* colon = memchr(line, ':', (size_t)(line_end - line));
        if (!colon)
            return cHTTPX_StatusBadRequest;

        const char* name_start = line;
        const char* name_end = colon;
        while (name_end > name_start && (name_end[-1] == ' ' || name_end[-1] == '\t'))
            name_end--;

        const char* value_start = colon + 1;
        while (value_start < line_end && (*value_start == ' ' || *value_start == '\t'))
            value_start++;
        const char* value_end = line_end;
        while (value_end > value_start && (value_end[-1] == ' ' || value_end[-1] == '\t'))
            value_end--;

        size_t name_size = (size_t)(name_end - name_start);
        size_t value_size = (size_t)(value_end - value_start);

        if (span_equal_ci(name_start, name_size, "Content-Length"))
        {
            size_t parsed = 0;
            if (!parse_size_value(value_start, value_size, &parsed))
                return cHTTPX_StatusBadRequest;
            if (has_content_length && parsed != content_length)
                return cHTTPX_StatusBadRequest;
            has_content_length = true;
            content_length = parsed;
        }
        else if (span_equal_ci(name_start, name_size, "Transfer-Encoding"))
        {
            if (has_transfer_encoding || !span_equal_ci(value_start, value_size, "chunked"))
                return cHTTPX_StatusBadRequest;
            has_transfer_encoding = true;
        }
        else if (span_equal_ci(name_start, name_size, "Content-Type"))
        {
            size_t copy = value_size < sizeof(content_type) - 1 ? value_size : sizeof(content_type) - 1;
            memcpy(content_type, value_start, copy);
            content_type[copy] = '\0';
        }

        line = line_end + 2;
    }

    if (has_content_length && has_transfer_encoding)
        return cHTTPX_StatusBadRequest;

    connection->memory_body = content_type_matches(content_type, cHTTPX_CTYPE_JSON) || content_type_matches(content_type, cHTTPX_CTYPE_FORM) || strncasecmp(content_type, "text/", 5) == 0;
    connection->multipart_body = content_type_matches(content_type, cHTTPX_CTYPE_MULTI);
    connection->body_limit = connection->memory_body ? connection->server->max_body_size : connection->server->max_upload_size;
    connection->chunked = has_transfer_encoding;
    connection->content_length = has_content_length ? content_length : 0;

    if (connection->chunked)
    {
        connection->chunk_state = CHTTPX_CHUNK_SIZE;
        if (!connection->memory_body)
        {
            connection->body_stream = tmpfile();
            if (!connection->body_stream)
                return cHTTPX_StatusInternalServerError;
        }
        connection->state = CHTTPX_CONN_READING_BODY;
        return 0;
    }

    if (connection->content_length > connection->body_limit)
        return cHTTPX_StatusPayloadTooLarge;
    if (connection->content_length == 0)
        return 0;

    if (connection->memory_body)
    {
        if (connection->content_length == SIZE_MAX)
            return cHTTPX_StatusPayloadTooLarge;
        connection->body = malloc(connection->content_length + 1);
        if (!connection->body)
            return cHTTPX_StatusInternalServerError;
        connection->body_capacity = connection->content_length + 1;
    }
    else if (connection->multipart_body)
    {
        connection->body_stream = tmpfile();
        if (!connection->body_stream)
            return cHTTPX_StatusInternalServerError;
    }
    else
        connection->discard_body = true;

    connection->state = CHTTPX_CONN_READING_BODY;
    return 0;
}

static int connection_store_decoded(chttpx_connection_t* connection, const unsigned char* data, size_t size)
{
    if (size == 0)
        return 1;
    if (connection->chunk_decoded > connection->body_limit || size > connection->body_limit - connection->chunk_decoded)
        return 0;

    if (connection->memory_body)
    {
        size_t required = connection->chunk_decoded + size + 1;
        if (required > connection->body_capacity)
        {
            size_t capacity = connection->body_capacity ? connection->body_capacity : 4096;
            while (capacity < required)
            {
                if (capacity > (connection->body_limit + 1) / 2)
                {
                    capacity = connection->body_limit + 1;
                    break;
                }
                capacity *= 2;
            }
            if (capacity < required)
                return 0;
            unsigned char* resized = realloc(connection->body, capacity);
            if (!resized)
                return 0;
            connection->body = resized;
            connection->body_capacity = capacity;
        }
        memcpy(connection->body + connection->chunk_decoded, data, size);
        connection->body[connection->chunk_decoded + size] = '\0';
    }
    else if (connection->body_stream)
    {
        if (fwrite(data, 1, size, connection->body_stream) != size)
            return 0;
    }

    connection->chunk_decoded += size;
    return 1;
}

static int connection_feed_chunked(chttpx_connection_t* connection, const unsigned char* data, size_t size)
{
    size_t offset = 0;
    while (offset < size && connection->chunk_state != CHTTPX_CHUNK_DONE)
    {
        switch (connection->chunk_state)
        {
        case CHTTPX_CHUNK_SIZE:
            if (data[offset] == '\r')
                connection->chunk_state = CHTTPX_CHUNK_SIZE_LF;
            else
            {
                if (connection->chunk_line_size + 1 >= sizeof(connection->chunk_line))
                    return cHTTPX_StatusBadRequest;
                connection->chunk_line[connection->chunk_line_size++] = (char)data[offset];
            }
            offset++;
            break;

        case CHTTPX_CHUNK_SIZE_LF:
        {
            if (data[offset++] != '\n')
                return cHTTPX_StatusBadRequest;
            connection->chunk_line[connection->chunk_line_size] = '\0';
            char* extension = strchr(connection->chunk_line, ';');
            if (extension)
                *extension = '\0';
            if (!connection->chunk_line[0])
                return cHTTPX_StatusBadRequest;
            errno = 0;
            char* end = NULL;
            unsigned long long parsed = strtoull(connection->chunk_line, &end, 16);
            if (errno == ERANGE || !end || *end || parsed > SIZE_MAX)
                return cHTTPX_StatusBadRequest;
            if ((size_t)parsed > connection->body_limit - connection->chunk_decoded)
                return cHTTPX_StatusPayloadTooLarge;
            connection->chunk_line_size = 0;
            connection->chunk_remaining = (size_t)parsed;
            connection->chunk_state = connection->chunk_remaining ? CHTTPX_CHUNK_DATA : CHTTPX_CHUNK_TRAILER;
            break;
        }

        case CHTTPX_CHUNK_DATA:
        {
            size_t available = size - offset;
            size_t take = connection->chunk_remaining < available ? connection->chunk_remaining : available;
            if (!connection_store_decoded(connection, data + offset, take))
                return cHTTPX_StatusInternalServerError;
            offset += take;
            connection->chunk_remaining -= take;
            if (connection->chunk_remaining == 0)
                connection->chunk_state = CHTTPX_CHUNK_DATA_CR;
            break;
        }

        case CHTTPX_CHUNK_DATA_CR:
            if (data[offset++] != '\r')
                return cHTTPX_StatusBadRequest;
            connection->chunk_state = CHTTPX_CHUNK_DATA_LF;
            break;

        case CHTTPX_CHUNK_DATA_LF:
            if (data[offset++] != '\n')
                return cHTTPX_StatusBadRequest;
            connection->chunk_state = CHTTPX_CHUNK_SIZE;
            break;

        case CHTTPX_CHUNK_TRAILER:
            if (data[offset] == '\r')
                connection->chunk_state = CHTTPX_CHUNK_TRAILER_LF;
            else
            {
                if (connection->chunk_line_size + 1 >= sizeof(connection->chunk_line))
                    return cHTTPX_StatusBadRequest;
                connection->chunk_line[connection->chunk_line_size++] = (char)data[offset];
            }
            offset++;
            break;

        case CHTTPX_CHUNK_TRAILER_LF:
            if (data[offset++] != '\n')
                return cHTTPX_StatusBadRequest;
            if (connection->chunk_line_size == 0)
                connection->chunk_state = CHTTPX_CHUNK_DONE;
            else
            {
                connection->chunk_line_size = 0;
                connection->chunk_state = CHTTPX_CHUNK_TRAILER;
            }
            break;

        case CHTTPX_CHUNK_DONE:
            break;
        }
    }

    if (connection->chunk_state == CHTTPX_CHUNK_DONE)
    {
        connection->content_length = connection->chunk_decoded;
        connection->body_size = connection->memory_body ? connection->chunk_decoded : 0;
    }
    return 0;
}

static int connection_feed_body(chttpx_connection_t* connection, const unsigned char* data, size_t size)
{
    if (connection->chunked)
        return connection_feed_chunked(connection, data, size);

    if (connection->body_received >= connection->content_length)
        return 0;

    size_t remaining = connection->content_length - connection->body_received;
    size_t take = size < remaining ? size : remaining;

    if (connection->memory_body && take)
        memcpy(connection->body + connection->body_received, data, take);
    else if (connection->body_stream && take && fwrite(data, 1, take, connection->body_stream) != take)
        return cHTTPX_StatusInternalServerError;

    connection->body_received += take;
    if (connection->memory_body && connection->body_received == connection->content_length)
    {
        connection->body[connection->body_received] = '\0';
        connection->body_size = connection->body_received;
    }
    return 0;
}

static bool connection_body_complete(chttpx_connection_t* connection)
{
    return connection->chunked ? connection->chunk_state == CHTTPX_CHUNK_DONE : connection->body_received >= connection->content_length;
}

static int connection_error_response(chttpx_runtime_t* runtime, chttpx_connection_t* connection, int status)
{
    const char* reason = cHTTPX_StatusReason((uint16_t)status);
    char buffer[256];
    int size = snprintf(buffer, sizeof(buffer), "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status, reason);
    if (size <= 0 || (size_t)size >= sizeof(buffer))
    {
        connection_close(runtime, connection);
        return 0;
    }
    connection->write_buffer = malloc((size_t)size);
    if (!connection->write_buffer)
    {
        connection_close(runtime, connection);
        return 0;
    }
    memcpy(connection->write_buffer, buffer, (size_t)size);
    connection->write_size = (size_t)size;
    connection->write_offset = 0;
    connection->state = CHTTPX_CONN_WRITING;
    connection_touch(connection);
    if (_chttpx_event_mod(runtime->event_loop, connection->fd, CHTTPX_EVENT_WRITE, connection) != 0)
    {
        connection_close(runtime, connection);
        return 0;
    }
    return 1;
}

static int connection_submit(chttpx_runtime_t* runtime, chttpx_connection_t* connection)
{
    if (connection->body_stream)
    {
        if (fflush(connection->body_stream) != 0 || fseek(connection->body_stream, 0, SEEK_SET) != 0)
        {
            connection_error_response(runtime, connection, cHTTPX_StatusInternalServerError);
            return 0;
        }
    }
    _chttpx_event_del(runtime->event_loop, connection->fd);
    connection->state = CHTTPX_CONN_PROCESSING;
    if (!_chttpx_worker_submit(runtime->worker_pool, connection))
    {
        connection_close(runtime, connection);
        return 0;
    }
    return 1;
}

static int connection_process_bytes(chttpx_runtime_t* runtime, chttpx_connection_t* connection, const unsigned char* data, size_t size)
{
    if (connection->state == CHTTPX_CONN_READING_HEADERS)
    {
        size_t header_limit = connection->server->max_header_size ? connection->server->max_header_size : BUFFER_SIZE - 1;
        if (header_limit > SIZE_MAX - BUFFER_SIZE - 1)
        {
            connection_close(runtime, connection);
            return 0;
        }
        size_t maximum = header_limit + BUFFER_SIZE + 1;
        if (size > maximum - connection->header_size)
        {
            _chttpx_metrics_parser_failure(connection->server);
            return connection_error_response(runtime, connection, cHTTPX_StatusRequestHeaderFieldsTooLarge);
        }

        size_t required = connection->header_size + size + 1;
        if (required > connection->header_capacity)
        {
            size_t capacity = connection->header_capacity ? connection->header_capacity : 4096;
            while (capacity < required)
            {
                size_t next = capacity * 2;
                if (next < capacity || next > maximum)
                    next = maximum;
                capacity = next;
                if (capacity < required && capacity == maximum)
                    break;
            }
            if (capacity < required)
            {
                _chttpx_metrics_parser_failure(connection->server);
                return connection_error_response(runtime, connection, cHTTPX_StatusRequestHeaderFieldsTooLarge);
            }
            char* resized = realloc(connection->headers, capacity);
            if (!resized)
                return connection_error_response(runtime, connection, cHTTPX_StatusInternalServerError);
            connection->headers = resized;
            connection->header_capacity = capacity;
        }

        memcpy(connection->headers + connection->header_size, data, size);
        connection->header_size += size;
        connection->headers[connection->header_size] = '\0';

        char* delimiter = chttpx_memmem(connection->headers, connection->header_size, "\r\n\r\n", 4);
        if (!delimiter)
        {
            if (connection->header_size > header_limit)
            {
                _chttpx_metrics_parser_failure(connection->server);
                return connection_error_response(runtime, connection, cHTTPX_StatusRequestHeaderFieldsTooLarge);
            }
            return 1;
        }

        connection->header_end = (size_t)(delimiter - connection->headers) + 4;
        if (connection->header_end > header_limit)
        {
            _chttpx_metrics_parser_failure(connection->server);
            return connection_error_response(runtime, connection, cHTTPX_StatusRequestHeaderFieldsTooLarge);
        }

        int body_status = connection_prepare_body(connection);
        if (body_status)
        {
            _chttpx_metrics_parser_failure(connection->server);
            return connection_error_response(runtime, connection, body_status);
        }

        size_t initial_size = connection->header_size - connection->header_end;
        if (initial_size)
        {
            int body_result = connection_feed_body(connection, (unsigned char*)connection->headers + connection->header_end, initial_size);
            if (body_result)
            {
                _chttpx_metrics_parser_failure(connection->server);
                return connection_error_response(runtime, connection, body_result);
            }
        }

        connection->header_size = connection->header_end;
        connection->headers[connection->header_end] = '\0';

        if (connection_body_complete(connection))
            return connection_submit(runtime, connection);
        return 1;
    }

    if (connection->state == CHTTPX_CONN_READING_BODY)
    {
        int body_result = connection_feed_body(connection, data, size);
        if (body_result)
        {
            _chttpx_metrics_parser_failure(connection->server);
            return connection_error_response(runtime, connection, body_result);
        }
        if (connection_body_complete(connection))
            return connection_submit(runtime, connection);
    }
    return 1;
}

static int connection_tls_step(chttpx_runtime_t* runtime, chttpx_connection_t* connection)
{
    int result = _chttpx_tls_accept_step(connection->tls_session);
    if (result == cHTTPX_OK)
    {
        connection->state = CHTTPX_CONN_READING_HEADERS;
        connection_touch(connection);
        if (_chttpx_event_mod(runtime->event_loop, connection->fd, CHTTPX_EVENT_READ, connection) != 0)
        {
            connection_close(runtime, connection);
            return 0;
        }
        return 1;
    }
    if (result == CHTTPX_IO_WANT_READ)
    {
        if (_chttpx_event_mod(runtime->event_loop, connection->fd, CHTTPX_EVENT_READ, connection) != 0)
        {
            connection_close(runtime, connection);
            return 0;
        }
        return 1;
    }
    if (result == CHTTPX_IO_WANT_WRITE)
    {
        if (_chttpx_event_mod(runtime->event_loop, connection->fd, CHTTPX_EVENT_WRITE, connection) != 0)
        {
            connection_close(runtime, connection);
            return 0;
        }
        return 1;
    }
    _chttpx_metrics_connection_rejected(connection->server);
    _chttpx_tls_log_error(connection->server, "-", "TLS handshake failed");
    connection_close(runtime, connection);
    return 0;
}

static int connection_read_ready(chttpx_runtime_t* runtime, chttpx_connection_t* connection)
{
    unsigned char buffer[BUFFER_SIZE];
    for (;;)
    {
        int result = _chttpx_io_recv_nonblocking(connection->fd, connection->tls_session, buffer, sizeof(buffer));
        if (result > 0)
        {
            connection_touch(connection);
            if (!connection_process_bytes(runtime, connection, buffer, (size_t)result))
                return 0;
            if (connection->state == CHTTPX_CONN_PROCESSING || connection->state == CHTTPX_CONN_WRITING)
                return 1;
            continue;
        }
        if (result == 0)
        {
            connection_close(runtime, connection);
            return 0;
        }
        if (result == CHTTPX_IO_WANT_READ)
        {
            if (_chttpx_event_mod(runtime->event_loop, connection->fd, CHTTPX_EVENT_READ, connection) != 0)
            {
                connection_close(runtime, connection);
                return 0;
            }
            return 1;
        }
        if (result == CHTTPX_IO_WANT_WRITE)
        {
            if (_chttpx_event_mod(runtime->event_loop, connection->fd, CHTTPX_EVENT_WRITE, connection) != 0)
            {
                connection_close(runtime, connection);
                return 0;
            }
            return 1;
        }
        if (result == cHTTPX_ERR_TLS)
            _chttpx_tls_log_error(connection->server, "-", "TLS request read failed");
        connection_close(runtime, connection);
        return 0;
    }
}

static int connection_write_ready(chttpx_runtime_t* runtime, chttpx_connection_t* connection)
{
    while (connection->write_offset < connection->write_size)
    {
        int result = _chttpx_io_send_nonblocking(connection->fd, connection->tls_session, connection->write_buffer + connection->write_offset, connection->write_size - connection->write_offset);
        if (result > 0)
        {
            connection->write_offset += (size_t)result;
            connection_touch(connection);
            continue;
        }
        if (result == CHTTPX_IO_WANT_WRITE)
        {
            if (_chttpx_event_mod(runtime->event_loop, connection->fd, CHTTPX_EVENT_WRITE, connection) != 0)
            {
                connection_close(runtime, connection);
                return 0;
            }
            return 1;
        }
        if (result == CHTTPX_IO_WANT_READ)
        {
            if (_chttpx_event_mod(runtime->event_loop, connection->fd, CHTTPX_EVENT_READ, connection) != 0)
            {
                connection_close(runtime, connection);
                return 0;
            }
            return 1;
        }
        if (result == cHTTPX_ERR_TLS)
            _chttpx_tls_log_error(connection->server, "-", "TLS response write failed");
        connection_close(runtime, connection);
        return 0;
    }
    connection_close(runtime, connection);
    return 0;
}

static void drain_completions(chttpx_runtime_t* runtime)
{
    chttpx_connection_t* connection = completion_take_all(runtime);
    while (connection)
    {
        chttpx_connection_t* next = connection->completion_next;
        connection->completion_next = NULL;
        if (runtime_is_stopping(runtime) || connection->worker_result != cHTTPX_OK || !connection->write_buffer)
            connection_close(runtime, connection);
        else
        {
            connection->state = CHTTPX_CONN_WRITING;
            connection->write_offset = 0;
            connection_touch(connection);
            if (_chttpx_event_add(runtime->event_loop, connection->fd, CHTTPX_EVENT_WRITE, connection) != 0)
                connection_close(runtime, connection);
        }
        connection = next;
    }
}

static void accept_connections(chttpx_runtime_t* runtime)
{
    chttpx_serv_t* server = runtime->server;
    for (;;)
    {
        chttpx_socket_t fd = accept(server->server_fd, NULL, NULL);
        if (!socket_valid(fd))
        {
#ifdef CHTTPX_PLATFORM_WINDOWS
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK || error == WSAEINTR)
                return;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return;
#endif
            return;
        }

        _chttpx_metrics_connection_accepted(server);

        if (runtime_is_stopping(runtime) || __atomic_load_n(&server->current_clients, __ATOMIC_SEQ_CST) >= server->max_clients)
        {
            if (!server->tls.enabled)
            {
                static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(fd, busy, (int)(sizeof(busy) - 1), 0);
            }
            _chttpx_metrics_connection_rejected(server);
            chttpx_close(fd);
            continue;
        }

        if (_chttpx_socket_set_nonblocking(fd) != 0)
        {
            _chttpx_metrics_connection_rejected(server);
            chttpx_close(fd);
            continue;
        }

        chttpx_connection_t* connection = calloc(1, sizeof(*connection));
        if (!connection)
        {
            _chttpx_metrics_connection_rejected(server);
            chttpx_close(fd);
            continue;
        }

        connection->runtime = runtime;
        connection->server = server;
        connection->fd = fd;
        connection->state = CHTTPX_CONN_TLS;
        connection_touch(connection);
        connection->next = runtime->connections;
        runtime->connections = connection;
        __atomic_fetch_add(&server->current_clients, 1, __ATOMIC_SEQ_CST);
        _chttpx_metrics_connection_opened(server);

        int tls_result = _chttpx_tls_accept_begin(server, fd, &connection->tls_session);
        if (tls_result != cHTTPX_OK)
        {
            _chttpx_metrics_connection_rejected(server);
            connection_close(runtime, connection);
            continue;
        }

        uint32_t interest = CHTTPX_EVENT_READ;
        if (server->tls.enabled)
        {
            int step = _chttpx_tls_accept_step(connection->tls_session);
            if (step == cHTTPX_OK)
                connection->state = CHTTPX_CONN_READING_HEADERS;
            else if (step == CHTTPX_IO_WANT_READ)
                interest = CHTTPX_EVENT_READ;
            else if (step == CHTTPX_IO_WANT_WRITE)
                interest = CHTTPX_EVENT_WRITE;
            else
            {
                _chttpx_metrics_connection_rejected(server);
                _chttpx_tls_log_error(server, "-", "TLS handshake failed");
                connection_close(runtime, connection);
                continue;
            }
        }
        else
            connection->state = CHTTPX_CONN_READING_HEADERS;

        if (_chttpx_event_add(runtime->event_loop, fd, interest, connection) != 0)
        {
            connection_close(runtime, connection);
            continue;
        }
    }
}

static void scan_timeouts(chttpx_runtime_t* runtime)
{
    uint64_t now = monotonic_ms();
    chttpx_connection_t* connection = runtime->connections;
    while (connection)
    {
        chttpx_connection_t* next = connection->next;
        if (connection->state != CHTTPX_CONN_PROCESSING)
        {
            uint16_t timeout = connection->state == CHTTPX_CONN_WRITING ? connection->server->write_timeout_sec : connection->server->read_timeout_sec;
            if (connection->server->idle_timeout_sec && (!timeout || connection->server->idle_timeout_sec < timeout))
                timeout = connection->server->idle_timeout_sec;
            if (timeout && now - connection->last_activity_ms >= (uint64_t)timeout * 1000ULL)
            {
                _chttpx_metrics_timeout_failure(connection->server);
                connection_close(runtime, connection);
            }
        }
        connection = next;
    }
}

static void begin_shutdown(chttpx_runtime_t* runtime)
{
    if (runtime->shutdown_started)
        return;
    runtime->shutdown_started = true;
    if (runtime->listener_registered)
    {
        _chttpx_event_del(runtime->event_loop, runtime->server->server_fd);
        runtime->listener_registered = false;
    }

    chttpx_connection_t* connection = runtime->connections;
    while (connection)
    {
        chttpx_connection_t* next = connection->next;
        if (connection->state != CHTTPX_CONN_PROCESSING)
            connection_close(runtime, connection);
        connection = next;
    }
}

int _chttpx_runtime_init(chttpx_serv_t* server)
{
    if (!server || !socket_valid(server->server_fd) || server->max_clients == 0)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_runtime_t* runtime = calloc(1, sizeof(*runtime));
    if (!runtime)
        return cHTTPX_ERR_MEMORY;
    runtime->server = server;

    if (sync_init(runtime) != 0)
    {
        free(runtime);
        return cHTTPX_ERR_IO;
    }

    runtime->event_loop = _chttpx_event_create();
    if (!runtime->event_loop || _chttpx_socket_set_nonblocking(server->server_fd) != 0 || _chttpx_event_add(runtime->event_loop, server->server_fd, CHTTPX_EVENT_READ, runtime) != 0)
        goto error;
    runtime->listener_registered = true;

    if (_chttpx_worker_pool_create(&runtime->worker_pool, server->max_clients, runtime_worker_execute, runtime) != 0)
        goto error;

    server->runtime_state = runtime;
    return cHTTPX_OK;

error:
    _chttpx_worker_pool_destroy(runtime->worker_pool);

    if (runtime->event_loop)
        _chttpx_event_destroy(runtime->event_loop);

    sync_destroy(runtime);
    free(runtime);
    return cHTTPX_ERR_IO;
}

void _chttpx_runtime_listen(chttpx_serv_t* server)
{
    chttpx_runtime_t* runtime = server ? server->runtime_state : NULL;
    if (!runtime)
        return;

    chttpx_event_t events[CHTTPX_RUNTIME_EVENTS];
    while (true)
    {
        if (runtime_is_stopping(runtime) || __atomic_load_n(&server->shutdown_requested, __ATOMIC_ACQUIRE))
            begin_shutdown(runtime);

        drain_completions(runtime);

        if (runtime->shutdown_started && __atomic_load_n(&server->current_clients, __ATOMIC_SEQ_CST) == 0)
            break;

        int count = _chttpx_event_wait(runtime->event_loop, events, CHTTPX_RUNTIME_EVENTS, 100);
        if (count < 0)
            continue;

        for (int i = 0; i < count; i++)
        {
            if (events[i].events & CHTTPX_EVENT_WAKE)
            {
                drain_completions(runtime);
                continue;
            }

            if (events[i].data == runtime)
            {
                if (!runtime_is_stopping(runtime))
                    accept_connections(runtime);
                continue;
            }

            chttpx_connection_t* connection = events[i].data;
            if (!connection || connection->state == CHTTPX_CONN_PROCESSING)
                continue;

            int alive = 1;
            if (connection->state == CHTTPX_CONN_TLS)
                alive = connection_tls_step(runtime, connection);
            else if (connection->state == CHTTPX_CONN_READING_HEADERS || connection->state == CHTTPX_CONN_READING_BODY)
                alive = connection_read_ready(runtime, connection);
            else if (connection->state == CHTTPX_CONN_WRITING)
                alive = connection_write_ready(runtime, connection);

            if (alive && (events[i].events & CHTTPX_EVENT_ERROR) && connection->state != CHTTPX_CONN_PROCESSING && connection->state != CHTTPX_CONN_WRITING)
                connection_close(runtime, connection);
        }

        if (!runtime->shutdown_started)
            scan_timeouts(runtime);
    }

    drain_completions(runtime);
}

int _chttpx_runtime_worker_stats(chttpx_serv_t* server, chttpx_worker_stats_t* stats)
{
    if (!server || !stats)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_runtime_t* runtime = server->runtime_state;
    if (!runtime || !runtime->worker_pool)
        return cHTTPX_ERR_UNAVAILABLE;

    _chttpx_worker_stats(runtime->worker_pool, stats);
    return cHTTPX_OK;
}

void _chttpx_runtime_request_stop(chttpx_serv_t* server)
{
    chttpx_runtime_t* runtime = server ? server->runtime_state : NULL;
    if (!runtime)
        return;

    __atomic_store_n(&runtime->stopping, true, __ATOMIC_RELEASE);
    _chttpx_event_wake(runtime->event_loop);
}

void _chttpx_runtime_cleanup(chttpx_serv_t* server)
{
    chttpx_runtime_t* runtime = server ? server->runtime_state : NULL;
    if (!runtime)
        return;

    __atomic_store_n(&runtime->stopping, true, __ATOMIC_RELEASE);
    begin_shutdown(runtime);

    _chttpx_worker_pool_destroy(runtime->worker_pool);
    runtime->worker_pool = NULL;

    drain_completions(runtime);

    while (runtime->connections)
        connection_close(runtime, runtime->connections);

    _chttpx_event_destroy(runtime->event_loop);
    sync_destroy(runtime);
    free(runtime);
    server->runtime_state = NULL;
}
