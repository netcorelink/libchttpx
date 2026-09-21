/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software.
 */

#include "cHTTPX_compression.h"

#include "cHTTPX_crosspltm.h"
#include "cHTTPX_headers.h"
#include "cHTTPX_http.h"
#include "cHTTPX_middlewares.h"
#include "cHTTPX_request.h"
#include "cHTTPX_response.h"
#include "cHTTPX_serv.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

typedef struct
{
    chttpx_compression_config_t config;
    char** include_types;
    char** exclude_types;
    chttpx_compression_provider_t* providers;
    char** provider_encodings;
} chttpx_compression_state_t;

static const char* default_include_types[] = {
    "text/*",
    "application/json",
    "application/*+json",
    "application/javascript",
    "application/x-javascript",
    "application/xml",
    "application/*+xml",
    "application/graphql",
    "application/sql",
    "image/svg+xml",
};

static const char* default_exclude_types[] = {
    "image/jpeg",
    "image/png",
    "image/gif",
    "image/webp",
    "image/avif",
    "image/heic",
    "image/heif",
    "audio/*",
    "video/*",
    "font/*",
    "application/pdf",
    "application/zip",
    "application/gzip",
    "application/x-gzip",
    "application/x-7z-compressed",
    "application/x-rar-compressed",
    "application/octet-stream",
    "application/wasm",
};

static int compression_middleware_registered(const chttpx_serv_t* server, chttpx_middleware_t middleware)
{
    if (!server || !middleware)
        return 0;

    for (size_t i = 0; i < server->middleware.after_middleware_count; i++)
        if (server->middleware.after_middlewares[i] == middleware)
            return 1;

    return 0;
}

static void compression_log(chttpx_request_t* req, chttpx_log_level_t level, const char* message)
{
    chttpx_serv_t* server = req ? req->_server : NULL;
    if (!server || !server->logger || server->log_level > level)
        return;

    server->logger(level, req->request_id, message, server->logger_data);
}

static int token_character(unsigned char ch)
{
    if (isalnum(ch))
        return 1;

    switch (ch)
    {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
        return 1;
    default:
        return 0;
    }
}

static int valid_encoding_name(const char* encoding)
{
    if (!encoding || !*encoding || strcasecmp(encoding, "identity") == 0)
        return 0;

    for (const unsigned char* cursor = (const unsigned char*)encoding; *cursor; cursor++)
        if (!token_character(*cursor))
            return 0;

    return 1;
}

static int duplicate_strings(const char** source, size_t count, char*** destination)
{
    if (!destination || (count > 0 && !source))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    char** copy = count ? calloc(count, sizeof(*copy)) : NULL;
    if (count && !copy)
        return cHTTPX_ERR_MEMORY;

    for (size_t i = 0; i < count; i++)
    {
        if (!source[i])
        {
            for (size_t j = 0; j < i; j++)
                free(copy[j]);
            free(copy);
            return cHTTPX_ERR_INVALID_ARGUMENT;
        }

        copy[i] = strdup(source[i]);
        if (!copy[i])
        {
            for (size_t j = 0; j < i; j++)
                free(copy[j]);
            free(copy);
            return cHTTPX_ERR_MEMORY;
        }
    }

    *destination = copy;
    return cHTTPX_OK;
}

static void free_compression_state(chttpx_compression_state_t* state)
{
    if (!state)
        return;

    for (size_t i = 0; i < state->config.include_types_count; i++)
        free(state->include_types[i]);
    free(state->include_types);

    for (size_t i = 0; i < state->config.exclude_types_count; i++)
        free(state->exclude_types[i]);
    free(state->exclude_types);

    for (size_t i = 0; i < state->config.providers_count; i++)
        free(state->provider_encodings[i]);
    free(state->provider_encodings);
    free(state->providers);
    free(state);
}

static int gzip_encode_buffer(const unsigned char* input,
                              size_t input_size,
                              int level,
                              unsigned char** output,
                              size_t* output_size,
                              void* user_data)
{
    (void)user_data;

    if (!input || input_size == 0 || !output || !output_size || level < -1 || level > 9)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    if (input_size > (size_t)ULONG_MAX)
        return cHTTPX_ERR_LIMIT;

    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    int zresult = deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (zresult != Z_OK)
        return cHTTPX_ERR_COMPRESSION;

    uLong bound_value = deflateBound(&stream, (uLong)input_size);
    if (bound_value == 0 || (uintmax_t)bound_value > (uintmax_t)SIZE_MAX)
    {
        deflateEnd(&stream);
        return cHTTPX_ERR_LIMIT;
    }

    size_t capacity = (size_t)bound_value;
    unsigned char* buffer = malloc(capacity);
    if (!buffer)
    {
        deflateEnd(&stream);
        return cHTTPX_ERR_MEMORY;
    }

    size_t input_offset = 0;
    size_t output_offset = 0;

    while (input_offset < input_size)
    {
        size_t remaining = input_size - input_offset;
        uInt chunk = remaining > UINT_MAX ? UINT_MAX : (uInt)remaining;

        stream.next_in = (Bytef*)(input + input_offset);
        stream.avail_in = chunk;

        while (stream.avail_in > 0)
        {
            if (output_offset >= capacity)
                goto compression_error;

            size_t output_remaining = capacity - output_offset;
            uInt output_chunk = output_remaining > UINT_MAX ? UINT_MAX : (uInt)output_remaining;
            stream.next_out = buffer + output_offset;
            stream.avail_out = output_chunk;

            zresult = deflate(&stream, Z_NO_FLUSH);
            if (zresult != Z_OK)
                goto compression_error;

            output_offset += (size_t)(output_chunk - stream.avail_out);
            if (stream.avail_out == 0 && output_offset >= capacity)
                goto compression_error;
        }

        input_offset += chunk;
    }

    do
    {
        if (output_offset >= capacity)
            goto compression_error;

        size_t output_remaining = capacity - output_offset;
        uInt output_chunk = output_remaining > UINT_MAX ? UINT_MAX : (uInt)output_remaining;
        stream.next_out = buffer + output_offset;
        stream.avail_out = output_chunk;

        zresult = deflate(&stream, Z_FINISH);
        output_offset += (size_t)(output_chunk - stream.avail_out);

        if (zresult != Z_OK && zresult != Z_STREAM_END)
            goto compression_error;
    } while (zresult != Z_STREAM_END);

    deflateEnd(&stream);
    *output = buffer;
    *output_size = output_offset;
    return cHTTPX_OK;

compression_error:
    deflateEnd(&stream);
    free(buffer);
    return cHTTPX_ERR_COMPRESSION;
}

chttpx_compression_config_t cHTTPX_CompressionDefault(void)
{
    return (chttpx_compression_config_t){
        .min_size = 1024,
        .level = 5,
        .include_types = default_include_types,
        .include_types_count = sizeof(default_include_types) / sizeof(default_include_types[0]),
        .exclude_types = default_exclude_types,
        .exclude_types_count = sizeof(default_exclude_types) / sizeof(default_exclude_types[0]),
        .providers = NULL,
        .providers_count = 0,
    };
}

static int copy_providers(const chttpx_compression_provider_t* providers,
                          size_t count,
                          chttpx_compression_provider_t** providers_out,
                          char*** encodings_out)
{
    if (!providers_out || !encodings_out || (count > 0 && !providers))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_compression_provider_t* provider_copy = count ? calloc(count, sizeof(*provider_copy)) : NULL;
    char** encoding_copy = count ? calloc(count, sizeof(*encoding_copy)) : NULL;
    if (count && (!provider_copy || !encoding_copy))
    {
        free(provider_copy);
        free(encoding_copy);
        return cHTTPX_ERR_MEMORY;
    }

    for (size_t i = 0; i < count; i++)
    {
        if (!valid_encoding_name(providers[i].encoding) || !providers[i].encode_buffer)
        {
            for (size_t j = 0; j < i; j++)
                free(encoding_copy[j]);
            free(provider_copy);
            free(encoding_copy);
            return cHTTPX_ERR_INVALID_ARGUMENT;
        }

        encoding_copy[i] = strdup(providers[i].encoding);
        if (!encoding_copy[i])
        {
            for (size_t j = 0; j < i; j++)
                free(encoding_copy[j]);
            free(provider_copy);
            free(encoding_copy);
            return cHTTPX_ERR_MEMORY;
        }

        provider_copy[i] = providers[i];
        provider_copy[i].encoding = encoding_copy[i];
    }

    *providers_out = provider_copy;
    *encodings_out = encoding_copy;
    return cHTTPX_OK;
}

static int prepare_compression_state(const chttpx_compression_config_t* config, chttpx_compression_state_t** state_out)
{
    if (!config || !state_out ||
        (config->include_types_count > 0 && !config->include_types) ||
        (config->exclude_types_count > 0 && !config->exclude_types) ||
        (config->providers_count > 0 && !config->providers))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    if (config->providers_count == 0 && (config->level < -1 || config->level > 9))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_compression_state_t* state = calloc(1, sizeof(*state));
    if (!state)
        return cHTTPX_ERR_MEMORY;

    state->config = *config;

    int result = duplicate_strings(config->include_types, config->include_types_count, &state->include_types);
    if (result != cHTTPX_OK)
        goto error;
    state->config.include_types = (const char**)state->include_types;

    result = duplicate_strings(config->exclude_types, config->exclude_types_count, &state->exclude_types);
    if (result != cHTTPX_OK)
        goto error;
    state->config.exclude_types = (const char**)state->exclude_types;

    if (config->providers_count > 0)
    {
        result = copy_providers(config->providers, config->providers_count, &state->providers, &state->provider_encodings);
        if (result != cHTTPX_OK)
            goto error;
    }
    else
    {
        chttpx_compression_provider_t gzip_provider = {
            .encoding = "gzip",
            .encode_buffer = gzip_encode_buffer,
            .user_data = NULL,
        };
        result = copy_providers(&gzip_provider, 1, &state->providers, &state->provider_encodings);
        if (result != cHTTPX_OK)
            goto error;
        state->config.providers_count = 1;
    }

    state->config.providers = state->providers;
    *state_out = state;
    return cHTTPX_OK;

error:
    free_compression_state(state);
    return result;
}

static int wildcard_match_ci(const char* value, size_t value_length, const char* pattern)
{
    if (!value || !pattern)
        return 0;

    const char* star = NULL;
    size_t star_value = 0;
    size_t value_index = 0;
    size_t pattern_index = 0;
    size_t pattern_length = strlen(pattern);

    while (value_index < value_length)
    {
        if (pattern_index < pattern_length &&
            (pattern[pattern_index] == '?' ||
             tolower((unsigned char)pattern[pattern_index]) == tolower((unsigned char)value[value_index])))
        {
            value_index++;
            pattern_index++;
            continue;
        }

        if (pattern_index < pattern_length && pattern[pattern_index] == '*')
        {
            star = pattern + pattern_index++;
            star_value = value_index;
            continue;
        }

        if (star)
        {
            pattern_index = (size_t)(star - pattern) + 1;
            value_index = ++star_value;
            continue;
        }

        return 0;
    }

    while (pattern_index < pattern_length && pattern[pattern_index] == '*')
        pattern_index++;

    return pattern_index == pattern_length;
}

static int mime_matches(const char* mime, const char* pattern)
{
    if (!mime || !pattern)
        return 0;

    const char* semicolon = strchr(mime, ';');
    size_t mime_length = semicolon ? (size_t)(semicolon - mime) : strlen(mime);
    while (mime_length > 0 && isspace((unsigned char)mime[mime_length - 1]))
        mime_length--;

    return wildcard_match_ci(mime, mime_length, pattern);
}

static int mime_is_eligible(const chttpx_compression_state_t* state, const char* mime)
{
    if (!state || !mime)
        return 0;

    for (size_t i = 0; i < state->config.exclude_types_count; i++)
        if (mime_matches(mime, state->config.exclude_types[i]))
            return 0;

    if (state->config.include_types_count == 0)
        return 1;

    for (size_t i = 0; i < state->config.include_types_count; i++)
        if (mime_matches(mime, state->config.include_types[i]))
            return 1;

    return 0;
}

static int response_header_index(const chttpx_response_t* response, const char* name)
{
    if (!response || !name)
        return -1;

    for (size_t i = 0; i < response->headers_count; i++)
        if (strcasecmp(response->headers[i].name, name) == 0)
            return (int)i;

    return -1;
}

static const char* response_header_get(const chttpx_response_t* response, const char* name)
{
    int index = response_header_index(response, name);
    return index >= 0 ? response->headers[index].value : NULL;
}

static int comma_token_contains(const char* value, const char* token)
{
    if (!value || !token)
        return 0;

    const char* cursor = value;
    size_t token_length = strlen(token);

    while (*cursor)
    {
        while (*cursor == ',' || isspace((unsigned char)*cursor))
            cursor++;

        const char* end = strchr(cursor, ',');
        if (!end)
            end = cursor + strlen(cursor);

        const char* trimmed_end = end;
        while (trimmed_end > cursor && isspace((unsigned char)trimmed_end[-1]))
            trimmed_end--;

        const char* equals = memchr(cursor, '=', (size_t)(trimmed_end - cursor));
        if (equals)
            trimmed_end = equals;
        while (trimmed_end > cursor && isspace((unsigned char)trimmed_end[-1]))
            trimmed_end--;

        if ((size_t)(trimmed_end - cursor) == token_length && strncasecmp(cursor, token, token_length) == 0)
            return 1;

        cursor = *end ? end + 1 : end;
    }

    return 0;
}

static int vary_has_accept_encoding(const char* value)
{
    return value && (comma_token_contains(value, "*") || comma_token_contains(value, "Accept-Encoding"));
}

static int compression_headers_possible(const chttpx_response_t* response)
{
    if (!response)
        return 0;

    if (response_header_get(response, "Content-Encoding"))
        return 0;

    int vary_index = response_header_index(response, "Vary");
    size_t required_slots = vary_index >= 0 ? 1 : 2;
    if (response->headers_count + required_slots > MAX_HEADERS)
        return 0;

    if (vary_index >= 0 && !vary_has_accept_encoding(response->headers[vary_index].value))
    {
        size_t current = strlen(response->headers[vary_index].value);
        size_t suffix = strlen(", Accept-Encoding");
        if (current + suffix >= MAX_HEADER_VALUE)
            return 0;
    }

    return 1;
}

static int add_compression_headers(chttpx_response_t* response, const char* encoding)
{
    if (!response || !encoding || cHTTPX_HeaderAdd(response, "Content-Encoding", encoding) != 0)
        return 0;

    int vary_index = response_header_index(response, "Vary");
    if (vary_index < 0)
    {
        if (cHTTPX_HeaderAdd(response, "Vary", "Accept-Encoding") != 0)
        {
            response->headers_count--;
            return 0;
        }
        return 1;
    }

    if (vary_has_accept_encoding(response->headers[vary_index].value))
        return 1;

    size_t length = strlen(response->headers[vary_index].value);
    const char suffix[] = ", Accept-Encoding";
    if (length + sizeof(suffix) > sizeof(response->headers[vary_index].value))
    {
        response->headers_count--;
        return 0;
    }

    memcpy(response->headers[vary_index].value + length, suffix, sizeof(suffix));
    return 1;
}

static void trim_string(char** start, char** end)
{
    while (*start < *end && isspace((unsigned char)**start))
        (*start)++;
    while (*end > *start && isspace((unsigned char)(*end)[-1]))
        (*end)--;
}

static double parse_quality_parameter(char* parameters)
{
    double quality = 1.0;
    char* cursor = parameters;

    while (cursor && *cursor)
    {
        while (*cursor == ';' || isspace((unsigned char)*cursor))
            cursor++;
        if (!*cursor)
            break;

        char* end = strchr(cursor, ';');
        if (!end)
            end = cursor + strlen(cursor);

        char* item_start = cursor;
        char* item_end = end;
        trim_string(&item_start, &item_end);

        char* equals = memchr(item_start, '=', (size_t)(item_end - item_start));
        if (equals)
        {
            char* name_start = item_start;
            char* name_end = equals;
            trim_string(&name_start, &name_end);

            if ((size_t)(name_end - name_start) == 1 && tolower((unsigned char)*name_start) == 'q')
            {
                char* value_start = equals + 1;
                char* value_end = item_end;
                trim_string(&value_start, &value_end);

                if (value_start == value_end)
                    return 0.0;

                char saved = *value_end;
                *value_end = '\0';
                char* parsed_end = NULL;
                double parsed = strtod(value_start, &parsed_end);
                *value_end = saved;

                while (parsed_end && parsed_end < value_end && isspace((unsigned char)*parsed_end))
                    parsed_end++;

                if (!parsed_end || parsed_end != value_end || parsed < 0.0 || parsed > 1.0)
                    return 0.0;

                quality = parsed;
            }
        }

        cursor = *end ? end + 1 : end;
    }

    return quality;
}

static void quality_from_header_value(const char* header,
                                      const char* target,
                                      double* exact_quality,
                                      int* exact_found,
                                      double* wildcard_quality,
                                      int* wildcard_found)
{
    if (!header || !target || !exact_quality || !exact_found || !wildcard_quality || !wildcard_found)
        return;

    const char* cursor = header;

    while (*cursor)
    {
        const char* comma = strchr(cursor, ',');
        const char* item_end_const = comma ? comma : cursor + strlen(cursor);
        size_t item_length = (size_t)(item_end_const - cursor);

        if (item_length > 0 && item_length < MAX_HEADER_VALUE)
        {
            char item[MAX_HEADER_VALUE];
            memcpy(item, cursor, item_length);
            item[item_length] = '\0';

            char* start = item;
            char* end = item + item_length;
            trim_string(&start, &end);
            *end = '\0';

            char* semicolon = strchr(start, ';');
            double quality = semicolon ? parse_quality_parameter(semicolon) : 1.0;

            char* token_end = semicolon ? semicolon : end;
            trim_string(&start, &token_end);
            char saved = *token_end;
            *token_end = '\0';

            if (strcasecmp(start, target) == 0)
            {
                if (!*exact_found || quality > *exact_quality)
                    *exact_quality = quality;
                *exact_found = 1;
            }
            else if (strcmp(start, "*") == 0)
            {
                if (!*wildcard_found || quality > *wildcard_quality)
                    *wildcard_quality = quality;
                *wildcard_found = 1;
            }

            *token_end = saved;
        }

        if (!comma)
            break;
        cursor = comma + 1;
    }
}

static double request_encoding_quality(const chttpx_request_t* request, const char* encoding, int identity, int* header_present)
{
    if (!request || !encoding)
        return identity ? 1.0 : 0.0;

    double exact_quality = 0.0;
    double wildcard_quality = 0.0;
    int exact_found = 0;
    int wildcard_found = 0;
    int found_header = 0;

    for (size_t i = 0; i < request->headers_count; i++)
    {
        if (strcasecmp(request->headers[i].name, "Accept-Encoding") != 0)
            continue;

        found_header = 1;
        quality_from_header_value(request->headers[i].value, encoding, &exact_quality, &exact_found, &wildcard_quality, &wildcard_found);
    }

    if (header_present)
        *header_present = found_header;

    if (!found_header)
        return identity ? 1.0 : 0.0;

    if (exact_found)
        return exact_quality;

    if (identity)
        return wildcard_found && wildcard_quality == 0.0 ? 0.0 : 1.0;

    return wildcard_found ? wildcard_quality : 0.0;
}

static void make_empty_error(chttpx_response_t* response, int status)
{
    if (!response)
        return;

    cHTTPX_ResponseCleanup(response);
    response->status = status;
    response->content_type = cHTTPX_CTYPE_TEXT;
    response->headers_count = 0;
    response->compression_disabled = true;
}

static int response_semantics_allow_compression(const chttpx_request_t* request, const chttpx_response_t* response)
{
    if (!request || !response || response->compression_disabled || !response->body || response->body_size == 0)
        return 0;

    if (strcasecmp(request->method ? request->method : "", "HEAD") == 0)
        return 0;

    if (response->status < 200 || response->status == 204 || response->status == 205 || response->status == 206 || response->status == 304)
        return 0;

    if (cHTTPX_HeaderGet((chttpx_request_t*)request, "Range") || response_header_get(response, "Content-Range"))
        return 0;

    const char* cache_control = response_header_get(response, "Cache-Control");
    if (cache_control && comma_token_contains(cache_control, "no-transform"))
        return 0;

    if (response_header_get(response, "Content-Encoding"))
        return 0;

    return 1;
}

static chttpx_middleware_result_t compression_middleware(chttpx_request_t* request, chttpx_response_t* response)
{
    chttpx_serv_t* server = request ? request->_server : NULL;
    chttpx_compression_state_t* state = server ? (chttpx_compression_state_t*)server->compression_state : NULL;
    if (!state || !response || !response_semantics_allow_compression(request, response))
        return next;

    int accept_encoding_present = 0;
    double identity_quality = request_encoding_quality(request, "identity", 1, &accept_encoding_present);
    if (!accept_encoding_present)
        return next;

    const chttpx_compression_provider_t* selected = NULL;
    double selected_quality = 0.0;

    for (size_t i = 0; i < state->config.providers_count; i++)
    {
        int ignored = 0;
        double quality = request_encoding_quality(request, state->providers[i].encoding, 0, &ignored);
        if (quality > selected_quality)
        {
            selected = &state->providers[i];
            selected_quality = quality;
        }
    }

    if (!selected || selected_quality <= 0.0)
    {
        if (identity_quality <= 0.0)
            make_empty_error(response, cHTTPX_StatusNotAcceptable);
        return next;
    }

    if (selected_quality < identity_quality)
        return next;

    if (response->body_size < state->config.min_size || !mime_is_eligible(state, response->content_type))
    {
        if (identity_quality <= 0.0)
            make_empty_error(response, cHTTPX_StatusNotAcceptable);
        return next;
    }

    if (!compression_headers_possible(response))
    {
        if (identity_quality <= 0.0)
            make_empty_error(response, cHTTPX_StatusInternalServerError);
        return next;
    }

    unsigned char* compressed = NULL;
    size_t compressed_size = 0;
    int result = selected->encode_buffer(response->body, response->body_size, state->config.level, &compressed, &compressed_size, selected->user_data);
    if (result != cHTTPX_OK || !compressed || compressed_size == 0)
    {
        free(compressed);
        compression_log(request, cHTTPX_LOG_WARN, "response compression provider failed; using identity when allowed");
        if (identity_quality <= 0.0)
            make_empty_error(response, cHTTPX_StatusInternalServerError);
        return next;
    }

    if (compressed_size >= response->body_size && identity_quality >= selected_quality && identity_quality > 0.0)
    {
        free(compressed);
        return next;
    }

    size_t original_size = response->body_size;
    if (!add_compression_headers(response, selected->encoding))
    {
        free(compressed);
        compression_log(request, cHTTPX_LOG_WARN, "response compression headers could not be added; using identity when allowed");
        if (identity_quality <= 0.0)
            make_empty_error(response, cHTTPX_StatusInternalServerError);
        return next;
    }

    if (response->body_ownership == cHTTPX_BODY_OWNED)
        free((void*)response->body);

    response->body = compressed;
    response->body_size = compressed_size;
    response->body_ownership = cHTTPX_BODY_OWNED;

    if (server->logger && server->log_level <= cHTTPX_LOG_DEBUG)
    {
        char message[192];
        snprintf(message, sizeof(message), "compressed response encoding=%s original=%zu compressed=%zu",
                 selected->encoding, original_size, compressed_size);
        server->logger(cHTTPX_LOG_DEBUG, request->request_id, message, server->logger_data);
    }

    return next;
}

int cHTTPX_CompressionUse(chttpx_serv_t* server, const chttpx_compression_config_t* config)
{
    if (!server || !server->initialized)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_compression_config_t defaults;
    if (!config)
    {
        defaults = cHTTPX_CompressionDefault();
        config = &defaults;
    }

    if (!compression_middleware_registered(server, compression_middleware) &&
        server->middleware.after_middleware_count >= MAX_MIDDLEWARES)
        return cHTTPX_ERR_LIMIT;

    chttpx_compression_state_t* state = NULL;
    int result = prepare_compression_state(config, &state);
    if (result != cHTTPX_OK)
        return result;

    chttpx_compression_state_t* old_state = (chttpx_compression_state_t*)server->compression_state;
    server->compression_state = state;
    free_compression_state(old_state);

    if (!compression_middleware_registered(server, compression_middleware))
        cHTTPX_MiddlewareUseAfter(server, compression_middleware);

    return cHTTPX_OK;
}

int cHTTPX_RouteCompression(chttpx_route_t* route, bool enabled)
{
    if (!route)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    route->compression_disabled = !enabled;
    return cHTTPX_OK;
}

void cHTTPX_ResponseCompression(chttpx_response_t* response, bool enabled)
{
    if (response)
        response->compression_disabled = !enabled;
}

void _chttpx_compression_server_cleanup(chttpx_serv_t* server)
{
    if (!server)
        return;

    free_compression_state((chttpx_compression_state_t*)server->compression_state);
    server->compression_state = NULL;
}
