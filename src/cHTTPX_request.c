/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "cHTTPX_request.h"

#include "cHTTPX_i18n.h"
#include "cHTTPX_crosspltm.h"
#include "cHTTPX_headers.h"
#include "cHTTPX_response.h"
#include "cHTTPX_http.h"

#if defined(_WIN32) || defined(_WIN64)
#include "../lib/cjson/cJSON.h"
#else
#include <cjson/cJSON.h>
#endif

#include <ctype.h>
#include <stdio.h>

#define CHTTPX_CLEANUP_BLOCK_CAPACITY 32
#define CHTTPX_CONTEXT_BUCKET_COUNT 32

typedef struct chttpx_cleanup_entry
{
    void* resource;
    chttpx_cleanup_fn cleanup_fn;
    struct chttpx_cleanup_entry* next;
} chttpx_cleanup_entry_t;

typedef struct chttpx_cleanup_block
{
    size_t used;
    chttpx_cleanup_entry_t entries[CHTTPX_CLEANUP_BLOCK_CAPACITY];
    struct chttpx_cleanup_block* next;
} chttpx_cleanup_block_t;

typedef struct
{
    chttpx_cleanup_entry_t* head;
    chttpx_cleanup_block_t first_block;
    chttpx_cleanup_block_t* extra_blocks;
} chttpx_cleanup_state_t;

typedef struct chttpx_context_entry
{
    void* value;
    chttpx_context_free_fn cleanup_fn;
    struct chttpx_context_entry* next;
    char name[];
} chttpx_context_entry_t;

typedef struct
{
    chttpx_context_entry_t* buckets[CHTTPX_CONTEXT_BUCKET_COUNT];
} chttpx_context_table_t;

/**
 * Hash a short request-scoped lookup key with FNV-1a.
 *
 * @param value Null-terminated key.
 * @return Stable 64-bit hash value.
 */
static uint64_t request_hash_string(const char* value)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char* p = (const unsigned char*)value; *p; ++p)
    {
        hash ^= (uint64_t)*p;
        hash *= 1099511628211ULL;
    }
    return hash;
}

/**
 * Return the request cleanup state, creating it on first use when requested.
 *
 * @param req Current request.
 * @param create Non-zero to allocate missing state.
 * @return Cleanup state or NULL.
 */
static chttpx_cleanup_state_t* cleanup_state(chttpx_request_t* req, int create)
{
    if (!req)
        return NULL;

    chttpx_cleanup_state_t* state = req->_cleanup_entries;
    if (!state && create)
    {
        state = calloc(1, sizeof(*state));
        if (!state)
            return NULL;
        req->_cleanup_entries = state;
    }
    return state;
}

/**
 * Allocate one cleanup entry from request-local blocks.
 *
 * Entries are pooled in groups to avoid one heap allocation per deferred
 * resource while preserving cHTTPX_Detach semantics for the resource itself.
 *
 * @param state Cleanup state owned by the request.
 * @return Reusable cleanup entry or NULL on allocation failure.
 */
static chttpx_cleanup_entry_t* cleanup_entry_alloc(chttpx_cleanup_state_t* state)
{
    if (!state)
        return NULL;

    if (state->first_block.used < CHTTPX_CLEANUP_BLOCK_CAPACITY)
        return &state->first_block.entries[state->first_block.used++];

    chttpx_cleanup_block_t* block = state->extra_blocks;
    if (!block || block->used == CHTTPX_CLEANUP_BLOCK_CAPACITY)
    {
        chttpx_cleanup_block_t* created = calloc(1, sizeof(*created));
        if (!created)
            return NULL;
        created->next = state->extra_blocks;
        state->extra_blocks = created;
        block = created;
    }

    return &block->entries[block->used++];
}

/**
 * Return the named-context hash table, creating it on first use when requested.
 *
 * @param req Current request.
 * @param create Non-zero to allocate the table when absent.
 * @return Context table or NULL.
 */
static chttpx_context_table_t* context_table(chttpx_request_t* req, int create)
{
    if (!req)
        return NULL;

    chttpx_context_table_t* table = req->_contexts;
    if (!table && create)
    {
        table = calloc(1, sizeof(*table));
        if (!table)
            return NULL;
        req->_contexts = table;
    }
    return table;
}

/**
 * Allocate zero-initialized memory owned by the current request.
 *
 * @param req Current HTTP request.
 * @param size Number of bytes to allocate.
 * @return Request-owned memory or NULL on failure.
 */
void* cHTTPX_Alloc(chttpx_request_t* req, size_t size)
{
    if (!req || size == 0)
        return NULL;

    void* memory = calloc(1, size);
    if (!memory)
        return NULL;

    if (cHTTPX_Defer(req, memory, free) != 0)
    {
        free(memory);
        return NULL;
    }

    return memory;
}

/**
 * Duplicate a string into request-owned memory.
 *
 * @param req Current HTTP request.
 * @param str Null-terminated source string.
 * @return Request-owned copy or NULL on failure.
 */
char* cHTTPX_Strdup(chttpx_request_t* req, const char* str)
{
    if (!str)
        return NULL;

    size_t size = strlen(str) + 1;
    char* copy = cHTTPX_Alloc(req, size);
    if (copy)
        memcpy(copy, str, size);
    return copy;
}

/**
 * Register an arbitrary resource for cleanup at the end of the request.
 *
 * @param req Current HTTP request.
 * @param resource Resource passed to cleanup_fn.
 * @param cleanup_fn Function that releases resource.
 * @return 0 on success, -1 on invalid input or allocation failure.
 */
int cHTTPX_Defer(chttpx_request_t* req, void* resource, chttpx_cleanup_fn cleanup_fn)
{
    if (!req || !resource || !cleanup_fn)
        return -1;

    chttpx_cleanup_state_t* state = cleanup_state(req, 1);
    if (!state)
        return -1;

    chttpx_cleanup_entry_t* entry = cleanup_entry_alloc(state);
    if (!entry)
        return -1;

    entry->resource = resource;
    entry->cleanup_fn = cleanup_fn;
    entry->next = state->head;
    state->head = entry;
    return 0;
}

/**
 * Remove a resource from automatic request cleanup.
 *
 * @param req Current HTTP request.
 * @param resource Previously deferred resource.
 * @return Detached resource or NULL when not registered.
 */
void* cHTTPX_Detach(chttpx_request_t* req, void* resource)
{
    chttpx_cleanup_state_t* state = cleanup_state(req, 0);
    if (!state || !resource)
        return NULL;

    chttpx_cleanup_entry_t** current = &state->head;
    while (*current)
    {
        if ((*current)->resource == resource)
        {
            chttpx_cleanup_entry_t* detached = *current;
            *current = detached->next;
            detached->resource = NULL;
            detached->cleanup_fn = NULL;
            detached->next = NULL;
            return resource;
        }
        current = &(*current)->next;
    }

    return NULL;
}

/**
 * Store or replace a named request context.
 *
 * Contexts are indexed through a small request-local hash table so lookup does
 * not grow linearly with the number of named contexts.
 *
 * @param req Current HTTP request.
 * @param name Context name.
 * @param value Application-owned value.
 * @param cleanup_fn Optional callback invoked when the context is replaced or cleaned up.
 * @return 0 on success, -1 on invalid input or allocation failure.
 */
int cHTTPX_ContextSet(chttpx_request_t* req, const char* name, void* value, chttpx_context_free_fn cleanup_fn)
{
    if (!req || !name || !*name)
        return -1;

    chttpx_context_table_t* table = context_table(req, 1);
    if (!table)
        return -1;

    size_t bucket = (size_t)(request_hash_string(name) % CHTTPX_CONTEXT_BUCKET_COUNT);
    chttpx_context_entry_t* entry = table->buckets[bucket];
    while (entry)
    {
        if (strcmp(entry->name, name) == 0)
        {
            if (entry->cleanup_fn && entry->value && entry->value != value)
                entry->cleanup_fn(entry->value);
            entry->value = value;
            entry->cleanup_fn = cleanup_fn;
            return 0;
        }
        entry = entry->next;
    }

    size_t name_size = strlen(name) + 1;
    if (name_size > SIZE_MAX - sizeof(*entry))
        return -1;

    entry = malloc(sizeof(*entry) + name_size);
    if (!entry)
        return -1;

    entry->value = value;
    entry->cleanup_fn = cleanup_fn;
    entry->next = table->buckets[bucket];
    memcpy(entry->name, name, name_size);
    table->buckets[bucket] = entry;
    return 0;
}

/**
 * Look up a named request context.
 *
 * @param req Current HTTP request.
 * @param name Context name.
 * @return Borrowed context value or NULL when absent.
 */
void* cHTTPX_ContextGet(chttpx_request_t* req, const char* name)
{
    chttpx_context_table_t* table = context_table(req, 0);
    if (!table || !name)
        return NULL;

    size_t bucket = (size_t)(request_hash_string(name) % CHTTPX_CONTEXT_BUCKET_COUNT);
    for (chttpx_context_entry_t* entry = table->buckets[bucket]; entry; entry = entry->next)
        if (strcmp(entry->name, name) == 0)
            return entry->value;

    return NULL;
}

/**
 * Detach a named request context without running its cleanup callback.
 *
 * @param req Current HTTP request.
 * @param name Context name.
 * @return Detached value owned by the caller or NULL when absent.
 */
void* cHTTPX_ContextDetach(chttpx_request_t* req, const char* name)
{
    chttpx_context_table_t* table = context_table(req, 0);
    if (!table || !name)
        return NULL;

    size_t bucket = (size_t)(request_hash_string(name) % CHTTPX_CONTEXT_BUCKET_COUNT);
    chttpx_context_entry_t** current = &table->buckets[bucket];
    while (*current)
    {
        if (strcmp((*current)->name, name) == 0)
        {
            chttpx_context_entry_t* detached = *current;
            void* value = detached->value;
            *current = detached->next;
            free(detached);
            return value;
        }
        current = &(*current)->next;
    }
    return NULL;
}

/**
 * Run all request-owned cleanup callbacks and release internal lookup state.
 *
 * @param req Request whose scoped resources must be released.
 */
void cHTTPX_RequestCleanup(chttpx_request_t* req)
{
    if (!req)
        return;

    chttpx_context_table_t* table = context_table(req, 0);
    if (table)
    {
        for (size_t bucket = 0; bucket < CHTTPX_CONTEXT_BUCKET_COUNT; ++bucket)
        {
            chttpx_context_entry_t* entry = table->buckets[bucket];
            while (entry)
            {
                chttpx_context_entry_t* next = entry->next;
                if (entry->cleanup_fn && entry->value)
                    entry->cleanup_fn(entry->value);
                free(entry);
                entry = next;
            }
        }
        free(table);
        req->_contexts = NULL;
    }

    if (req->context)
    {
        if (req->context_free)
            req->context_free(req->context);
        req->context = NULL;
        req->context_free = NULL;
    }

    chttpx_cleanup_state_t* state = cleanup_state(req, 0);
    if (state)
    {
        chttpx_cleanup_entry_t* entry = state->head;
        while (entry)
        {
            chttpx_cleanup_entry_t* next = entry->next;
            if (entry->cleanup_fn && entry->resource)
                entry->cleanup_fn(entry->resource);
            entry = next;
        }

        chttpx_cleanup_block_t* block = state->extra_blocks;
        while (block)
        {
            chttpx_cleanup_block_t* next = block->next;
            free(block);
            block = next;
        }

        free(state);
        req->_cleanup_entries = NULL;
    }
}

const char* cHTTPX_BearerToken(chttpx_request_t* req)
{
    const char* authorization = cHTTPX_HeaderGet(req, "Authorization");
    if (!authorization || strncasecmp(authorization, "Bearer ", 7) != 0 || authorization[7] == '\0')
        return NULL;
    return authorization + 7;
}

int cHTTPX_OnBodyChunk(chttpx_request_t* req, chttpx_body_chunk_fn callback, void* user_data)
{
    if (!req || !callback)
        return -1;
    req->_body_chunk_fn = callback;
    req->_body_chunk_data = user_data;

    if (req->body && req->body_size)
    {
        size_t offset = 0;
        while (offset < req->body_size)
        {
            size_t size = req->body_size - offset;
            if (size > BUFFER_SIZE)
                size = BUFFER_SIZE;
            if (callback(req->body + offset, size, user_data) != 0)
                return -1;
            offset += size;
        }
    }
    else if (req->files_count && req->files[0].path)
    {
        FILE* file = fopen(req->files[0].path, "rb");
        if (!file)
            return -1;
        unsigned char buffer[BUFFER_SIZE];
        size_t size;
        while ((size = fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            if (callback(buffer, size, user_data) != 0)
            {
                fclose(file);
                return -1;
            }
        }
        if (ferror(file))
        {
            fclose(file);
            return -1;
        }
        fclose(file);
    }
    return 0;
}

typedef struct
{
    const char* required;
    const char* min_length;
    const char* max_length;
    const char* invalid_email;
    const char* generic;
} validation_messages_t;

validation_messages_t messages_en = {"field '%s' is required", "field '%s' min length is %zu", "field '%s' max length is %zu",
                                     "field '%s' is not a valid email", "field '%s' validation error"};

validation_messages_t messages_ru = {"поле '%s' обязательно", "минимальная длина поля '%s' — %zu", "максимальная длина поля '%s' — %zu",
                                     "поле '%s' имеет неверный формат email", "ошибка валидации поля '%s'"};

validation_messages_t* messages[LANG_COUNT] = {&messages_en, &messages_ru, &messages_en, &messages_en};

/**
 * Parse a JSON body and validate fields according to the provided definitions.
 * @param req Pointer to the HTTP request.
 * @param fields Array of field validation definitions (cHTTPX_FieldValidation).
 * @param field_count Number of fields in the array.
 * @return 1 if parsing and validation succeed, 0 if there is an error.
 * This function automatically checks required fields, string length, boolean types, etc.
 */
int cHTTPX_Parse(chttpx_request_t* req, chttpx_validation_t* fields, size_t field_count)
{
    if (!req || !fields || !req->body)
        return 0;

    char* body = malloc(req->body_size + 1);
    if (!body)
        return 0;

    memcpy(body, (const void*)req->body, req->body_size);
    body[req->body_size] = '\0';

    cJSON* json = cJSON_Parse(body);
    free(body);

    if (!json)
    {
        snprintf(req->error_msg, sizeof(req->error_msg), "Invalid JSON");
        return 0;
    }

    for (size_t i = 0; i < field_count; i++)
    {
        chttpx_validation_t* f = &fields[i];
        cJSON* item = cJSON_GetObjectItem(json, f->name);

        if (!item)
            continue;

        f->present = 1;

        switch (f->type)
        {
        case FIELD_STRING:
            if (cJSON_IsString(item))
            {
                *(char**)f->target = cHTTPX_Strdup(req, item->valuestring);
                if (!*(char**)f->target)
                    goto memory_error;
            }
            else
                goto type_error;
            break;

        case FIELD_STRING_ARRAY:
            if (cJSON_IsArray(item))
            {
                size_t count = cJSON_GetArraySize(item);

                chttpx_string_array_t* arr = (chttpx_string_array_t*)f->target;
                arr->count = count;
                arr->items = cHTTPX_Alloc(req, sizeof(char*) * count);
                if (count && !arr->items)
                    goto memory_error;

                for (size_t j = 0; j < count; j++)
                {
                    cJSON* el = cJSON_GetArrayItem(item, j);
                    if (cJSON_IsString(el))
                    {
                        arr->items[j] = cHTTPX_Strdup(req, el->valuestring);
                        if (!arr->items[j])
                            goto memory_error;
                    }
                    else
                    {
                        goto type_error;
                    }
                }
            }
            else
                goto type_error;
            break;

        case FIELD_NUMBER:
            if (cJSON_IsNumber(item))
            {
                *(int*)f->target = 0;
                *(int*)f->target = item->valueint;
            }
            else
                goto type_error;
            break;

        case FIELD_NUMBER_ARRAY:
            if (cJSON_IsArray(item))
            {
                size_t count = cJSON_GetArraySize(item);

                chttpx_number_array_t* arr = (chttpx_number_array_t*)f->target;
                arr->count = count;
                arr->items = cHTTPX_Alloc(req, sizeof(int) * count);
                if (count && !arr->items)
                    goto memory_error;

                for (size_t j = 0; j < count; j++)
                {
                    cJSON* el = cJSON_GetArrayItem(item, j);
                    if (cJSON_IsNumber(el))
                    {
                        arr->items[j] = el->valueint;
                    }
                    else
                    {
                        goto type_error;
                    }
                }
            }
            else
                goto type_error;
            break;

        case FIELD_BOOL:
            if (cJSON_IsBool(item))
            {
                *(uint8_t*)f->target = 0;
                *(uint8_t*)f->target = cJSON_IsTrue(item);
            }
            else
                goto type_error;
            break;

        default:
            goto type_error;
        }
    }

    cJSON_Delete(json);
    return 1;

memory_error:
    cJSON_Delete(json);
    snprintf(req->error_msg, sizeof(req->error_msg), "Out of memory");
    return 0;

type_error:
    snprintf(req->error_msg, sizeof(req->error_msg), "Invalid JSON field type");
    cJSON_Delete(json);
    return 0;
}

/* Validator email string */
static int is_valid_email(const char* email)
{
    if (!email)
        return 0;

    const char* at = strchr(email, '@');
    if (!at || at == email)
        return 0;

    const char* dot = strrchr(at, '.');
    if (!dot || dot == at + 1)
        return 0;

    for (const char* p = email; *p; p++)
    {
        if (!isalnum((unsigned char)*p) && *p != '@' && *p != '.' && *p != '_' && *p != '-')
            return 0;
    }

    return 1;
}

static void set_error(char* error_msg, size_t error_size, i18n_language_t lang, int key, const char* field_name, size_t num)
{
    validation_messages_t* msg = messages[lang];

    switch (key)
    {
    case 0:
        snprintf(error_msg, error_size, msg->required, field_name);
        break;
    case 1:
        snprintf(error_msg, error_size, msg->min_length, field_name, num);
        break;
    case 2:
        snprintf(error_msg, error_size, msg->max_length, field_name, num);
        break;
    case 3:
        snprintf(error_msg, error_size, msg->invalid_email, field_name);
        break;
    default:
        snprintf(error_msg, error_size, msg->generic, field_name);
        break;
    }
}

/*
 * Validates an array of cHTTPX_FieldValidation structures.
 * This function ensures that required fields are present, string lengths are within limits,
 * and basic validation for integers and boolean fields is performed.
 */
int cHTTPX_Validate(chttpx_request_t* req, chttpx_validation_t* fields, size_t field_count, const char* l)
{
    i18n_language_t lang = i18n_lang_from_string(l ? l : "en");

    for (size_t i = 0; i < field_count; i++)
    {
        chttpx_validation_t* f = &fields[i];

        if (f->required && !f->present)
        {
            set_error(req->error_msg, sizeof(req->error_msg), lang, 0, f->name, 0);
            return 0;
        }

        if (!f->present)
            continue;

        if (f->type == FIELD_STRING)
        {
            char* v = *(char**)f->target;
            if (!v)
            {
                set_error(req->error_msg, sizeof(req->error_msg), lang, 4, f->name, 0);
                return 0;
            }

            if (f->normalizers & cHTTPX_TRIM)
            {
                char* start = v;
                while (*start && isspace((unsigned char)*start))
                    start++;
                if (start != v)
                    memmove(v, start, strlen(start) + 1);
                size_t trim_len = strlen(v);
                while (trim_len > 0 && isspace((unsigned char)v[trim_len - 1]))
                    v[--trim_len] = '\0';
            }

            if (f->normalizers & cHTTPX_LOWERCASE)
            {
                for (char* p = v; *p; p++)
                    *p = (char)tolower((unsigned char)*p);
            }
            else if (f->normalizers & cHTTPX_UPPERCASE)
            {
                for (char* p = v; *p; p++)
                    *p = (char)toupper((unsigned char)*p);
            }

            size_t len = strlen(v);

            if (f->min_length && len < f->min_length)
            {
                set_error(req->error_msg, sizeof(req->error_msg), lang, 1, f->name, f->min_length);
                return 0;
            }

            if (f->max_length && len > f->max_length)
            {
                set_error(req->error_msg, sizeof(req->error_msg), lang, 2, f->name, f->max_length);
                return 0;
            }

            if (f->validator == VALIDATOR_EMAIL && !is_valid_email(v))
            {
                set_error(req->error_msg, sizeof(req->error_msg), lang, 3, f->name, 0);
                return 0;
            }

            if (f->custom_validator && !f->custom_validator(v, req->error_msg, sizeof(req->error_msg)))
            {
                if (!req->error_msg[0])
                    set_error(req->error_msg, sizeof(req->error_msg), lang, 4, f->name, 0);
                return 0;
            }
        }
    }

    return 1;
}

int cHTTPX_BindJSON(chttpx_request_t* req, chttpx_response_t* res, chttpx_validation_t* fields, size_t field_count)
{
    if (!req || !res || !fields)
        return 0;

    if (!cHTTPX_Parse(req, fields, field_count) || !cHTTPX_Validate(req, fields, field_count, req->language[0] ? req->language : "en"))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, req->error_msg[0] ? req->error_msg : "invalid request body");
        return 0;
    }

    return 1;
}
