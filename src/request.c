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

#if defined(_WIN32) || defined(_WIN64)
#include "../lib/cjson/cJSON.h"
#else
#include <cjson/cJSON.h>
#endif

#include <ctype.h>
#include <stdio.h>

typedef struct
{
    const char* required;
    const char* min_length;
    const char* max_length;
    const char* invalid_email;
    const char* generic;
} validation_messages_t;

validation_messages_t messages_en = {
    "field '%s' is required", "field '%s' min length is %zu", "field '%s' max length is %zu",
    "field '%s' is not a valid email", "field '%s' validation error"};

validation_messages_t messages_ru = {"поле '%s' обязательно", "минимальная длина поля '%s' — %zu",
                                     "максимальная длина поля '%s' — %zu",
                                     "поле '%s' имеет неверный формат email",
                                     "ошибка валидации поля '%s'"};

validation_messages_t* messages[] = {&messages_en, &messages_ru};

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
    char* body = malloc(req->body_size + 1);
    if (!body) return 0;

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

        if (!item) continue;

        f->present = 1;

        switch (f->type)
        {
        case FIELD_STRING:
            if (cJSON_IsString(item))
            {
                *(char**)f->target = strdup(item->valuestring);
            }
            break;

        case FIELD_INT:
            if (cJSON_IsNumber(item))
            {
                *(int*)f->target = item->valueint;
            }
            break;

        case FIELD_BOOL:
            if (cJSON_IsBool(item))
            {
                *(uint8_t*)f->target = cJSON_IsTrue(item);
            }
            break;
        }
    }

    cJSON_Delete(json);
    return 1;
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
        if (!isalnum(*p) && *p != '@' && *p != '.' && *p != '_' && *p != '-')
            return 0;
    }

    return 1;
}

static void set_error(char* error_msg, size_t error_size, i18n_language_t lang, int key,
                      const char* field_name, size_t num)
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

        if (!f->present) continue;

        if (f->type == FIELD_STRING)
        {
            char* v = *(char**)f->target;
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
        }
    }

    return 1;
}
