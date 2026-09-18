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

#include "request.h"

#include "i18n.h"
#include "crosspltm.h"
#include "headers.h"
#include "response.h"
#include "http.h"

#if defined(_WIN32) || defined(_WIN64)
#include "../lib/cjson/cJSON.h"
#else
#include <cjson/cJSON.h>
#endif

#include <ctype.h>
#include <stdio.h>

typedef struct chttpx_cleanup_entry
{
    void* resource;
    chttpx_cleanup_fn cleanup_fn;
    struct chttpx_cleanup_entry* next;
} chttpx_cleanup_entry_t;

typedef struct chttpx_context_entry
{
    char* name;
    void* value;
    chttpx_context_free_fn cleanup_fn;
    struct chttpx_context_entry* next;
} chttpx_context_entry_t;

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

int cHTTPX_Defer(chttpx_request_t* req, void* resource, chttpx_cleanup_fn cleanup_fn)
{
    if (!req || !resource || !cleanup_fn)
        return -1;

    chttpx_cleanup_entry_t* entry = malloc(sizeof(*entry));
    if (!entry)
        return -1;

    entry->resource = resource;
    entry->cleanup_fn = cleanup_fn;
    entry->next = req->_cleanup_entries;
    req->_cleanup_entries = entry;
    return 0;
}

void* cHTTPX_Detach(chttpx_request_t* req, void* resource)
{
    if (!req || !resource)
        return NULL;

    chttpx_cleanup_entry_t** current = (chttpx_cleanup_entry_t**)&req->_cleanup_entries;
    while (*current)
    {
        if ((*current)->resource == resource)
        {
            chttpx_cleanup_entry_t* detached = *current;
            *current = detached->next;
            free(detached);
            return resource;
        }
        current = &(*current)->next;
    }

    return NULL;
}

int cHTTPX_ContextSet(chttpx_request_t* req, const char* name, void* value, chttpx_context_free_fn cleanup_fn)
{
    if (!req || !name || !*name)
        return -1;

    chttpx_context_entry_t* entry = req->_contexts;
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

    entry = calloc(1, sizeof(*entry));
    if (!entry)
        return -1;
    entry->name = strdup(name);
    if (!entry->name)
    {
        free(entry);
        return -1;
    }
    entry->value = value;
    entry->cleanup_fn = cleanup_fn;
    entry->next = req->_contexts;
    req->_contexts = entry;
    return 0;
}

void* cHTTPX_ContextGet(chttpx_request_t* req, const char* name)
{
    if (!req || !name)
        return NULL;
    chttpx_context_entry_t* entry = req->_contexts;
    while (entry)
    {
        if (strcmp(entry->name, name) == 0)
            return entry->value;
        entry = entry->next;
    }
    return NULL;
}

void* cHTTPX_ContextDetach(chttpx_request_t* req, const char* name)
{
    if (!req || !name)
        return NULL;
    chttpx_context_entry_t** current = (chttpx_context_entry_t**)&req->_contexts;
    while (*current)
    {
        if (strcmp((*current)->name, name) == 0)
        {
            chttpx_context_entry_t* detached = *current;
            void* value = detached->value;
            *current = detached->next;
            free(detached->name);
            free(detached);
            return value;
        }
        current = &(*current)->next;
    }
    return NULL;
}

void cHTTPX_RequestCleanup(chttpx_request_t* req)
{
    if (!req)
        return;

    chttpx_context_entry_t* context = req->_contexts;
    while (context)
    {
        chttpx_context_entry_t* next_context = context->next;
        if (context->cleanup_fn && context->value)
            context->cleanup_fn(context->value);
        free(context->name);
        free(context);
        context = next_context;
    }
    req->_contexts = NULL;

    if (req->context)
    {
        if (req->context_free)
            req->context_free(req->context);
        req->context = NULL;
        req->context_free = NULL;
    }

    chttpx_cleanup_entry_t* cleanup = req->_cleanup_entries;
    while (cleanup)
    {
        chttpx_cleanup_entry_t* next_cleanup = cleanup->next;
        cleanup->cleanup_fn(cleanup->resource);
        free(cleanup);
        cleanup = next_cleanup;
    }
    req->_cleanup_entries = NULL;
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

            if (f->normalizers & CHTTPX_TRIM)
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

            if (f->normalizers & CHTTPX_LOWERCASE)
            {
                for (char* p = v; *p; p++)
                    *p = (char)tolower((unsigned char)*p);
            }
            else if (f->normalizers & CHTTPX_UPPERCASE)
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
