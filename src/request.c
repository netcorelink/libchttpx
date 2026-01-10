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

#include "include/request.h"

#include "include/crosspltm.h"

#ifdef _WIN32
    #include "lib/cjson/cJSON.h"
#else
    #include <cjson/cJSON.h>
#endif

#include <stdio.h>

/**
 * Parse a JSON body and validate fields according to the provided definitions.
 * @param req Pointer to the HTTP request.
 * @param fields Array of field validation definitions (cHTTPX_FieldValidation).
 * @param field_count Number of fields in the array.
 * @return 1 if parsing and validation succeed, 0 if there is an error.
 * This function automatically checks required fields, string length, boolean types, etc.
 */
int cHTTPX_Parse(chttpx_request_t *req, chttpx_validation_t *fields, size_t field_count) {
    cJSON *json = cJSON_Parse(req->body);
    if (!json) {
        snprintf(req->error_msg, sizeof(req->error_msg), "Invalid JSON");
        return 0;
    }

    for (size_t i = 0; i < field_count; i++) {
        chttpx_validation_t *f = &fields[i];
        cJSON *item = cJSON_GetObjectItem(json, f->name);

        if (!item) {
            continue;
        }

        switch (f->type)
        {
        case FIELD_STRING:
            if (cJSON_IsString(item)) {
                *(char **)f->target = strdup(item->valuestring);
            }
            break;

        case FIELD_INT:
            if (cJSON_IsNumber(item)) {
                *(int *)f->target = item->valueint;
            }
            break;

        case FIELD_BOOL:
            if (cJSON_IsBool(item)) {
                *(uint8_t *)f->target = cJSON_IsTrue(item);
            }
            break;
        }
    }

    cJSON_Delete(json);
    return 1;
}

/*
 * Validates an array of cHTTPX_FieldValidation structures.
 * This function ensures that required fields are present, string lengths are within limits,
 * and basic validation for integers and boolean fields is performed.
 */
int cHTTPX_Validate(chttpx_request_t *req, chttpx_validation_t *fields, size_t field_count) {
    for (size_t i = 0; i < field_count; i++) {
        chttpx_validation_t *f = &fields[i];

        char *v;

        switch (f->type)
        {
        case FIELD_STRING:
            v = *(char **)f->target;

            if (!v) {
                if (f->required) {
                    snprintf(req->error_msg, sizeof(req->error_msg), "field '%s' is required", f->name);
                    return 0;
                }
                break;
            }

            size_t len = strlen(v);

            if (f->min_length && len < f->min_length) {
                snprintf(req->error_msg, sizeof(req->error_msg), "field '%s' min length is %zu", f->name, f->min_length);
                return 0;
            }

            if (f->max_length && len > f->max_length) {
                snprintf(req->error_msg, sizeof(req->error_msg), "field '%s' max length is %zu", f->name, f->max_length);
                return 0;
            }

            break;

        case FIELD_INT:
        case FIELD_BOOL:
            if (f->required && !*(uint8_t *)f->target) {
                snprintf(req->error_msg, sizeof(req->error_msg), "field '%s' is required", f->name);
                return 0;
            }
            break;
        }
    }

    return 1;
}