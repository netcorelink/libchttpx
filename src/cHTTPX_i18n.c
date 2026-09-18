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

#include "cHTTPX_i18n.h"

#include "cHTTPX_crosspltm.h"
#include "cHTTPX_serv.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#if defined(_WIN32) || defined(_WIN64)
#include "../lib/cjson/cJSON.h"
#else
#include <cjson/cJSON.h>
#endif

static i18n_manager_t* i18n_manager = NULL;

static i18n_locale_t load_locale_file(const char* path, const char* locale)
{
    i18n_locale_t loc;
    memset(&loc, 0, sizeof(loc));
    snprintf(loc.locale, sizeof(loc.locale), "%s", locale);

    FILE* f = fopen(path, "rb");
    if (!f)
        return loc;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return loc;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return loc;
    }

    char* data = malloc(size + 1);
    if (!data)
    {
        fclose(f);
        return loc;
    }

    long read_bytes = fread(data, 1, size, f);
    data[size] = '\0';

    if (read_bytes != size)
    {
        free(data);
        fclose(f);
        return loc;
    }

    fclose(f);

    cJSON* root = cJSON_Parse(data);
    free(data);
    if (!root || !cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return loc;
    }

    loc.count = cJSON_GetArraySize(root);
    loc.entries = calloc(loc.count, sizeof(i18n_entry_t));
    if (loc.count && !loc.entries)
    {
        loc.count = 0;
        cJSON_Delete(root);
        return loc;
    }

    int indx = 0;
    cJSON* child = NULL;
    cJSON_ArrayForEach(child, root)
    {
        const char* key = child->string;
        const char* value = cJSON_GetStringValue(child);

        if (key && value)
        {
            loc.entries[indx].key = strdup(key);
            loc.entries[indx].value = strdup(value);
            if (!loc.entries[indx].key || !loc.entries[indx].value)
            {
                free(loc.entries[indx].key);
                free(loc.entries[indx].value);
                for (int i = 0; i < indx; i++)
                {
                    free(loc.entries[i].key);
                    free(loc.entries[i].value);
                }
                free(loc.entries);
                loc.entries = NULL;
                loc.count = 0;
                cJSON_Delete(root);
                return loc;
            }
            indx++;
        }
    }

    loc.count = (size_t)indx;

    cJSON_Delete(root);
    return loc;
}

static void i18n_shutdown(void)
{
    if (!i18n_manager)
        return;

    for (size_t i = 0; i < i18n_manager->count; i++)
    {
        for (size_t j = 0; j < i18n_manager->locales[i].count; j++)
        {
            free(i18n_manager->locales[i].entries[j].key);
            free(i18n_manager->locales[i].entries[j].value);
        }

        free(i18n_manager->locales[i].entries);
    }

    free(i18n_manager);
    i18n_manager = NULL;
}

/**
 * Initializes the global i18n manager.
 *
 * Loads all locale JSON files from the specified directory.
 * The file name determines the locale language:
 * en.json -> "en"
 * ru.json -> "ru"
 * fr.json -> "fr"
 *
 * All translations are stored globally in memory and are used by the cHTTPX_i18n_t() function.
 *
 * The memory is automatically freed when the program ends.
 *
 * @param directory The path to the directory with locale JSON files.
 *
 * Example:
 *   cHTTPX_i18n("public");
 */
void cHTTPX_i18n(const char* directory)
{
    if (!directory)
        return;
    i18n_shutdown();
    i18n_manager = calloc(1, sizeof(i18n_manager_t));
    if (!i18n_manager)
        return;

    DIR* dir = opendir(directory);
    if (!dir)
    {
        free(i18n_manager);
        i18n_manager = NULL;
        return;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL)
    {
        size_t file_name_size = strlen(ent->d_name);
        if (file_name_size <= 5 || strcmp(ent->d_name + file_name_size - 5, ".json") != 0)
            continue;

        if (i18n_manager->count >= MAX_LOCALES)
            break;

        char locale[8] = {0};
        size_t locale_size = file_name_size - 5;
        if (locale_size == 0)
            continue;
        if (locale_size >= sizeof(locale))
            locale_size = sizeof(locale) - 1;
        memcpy(locale, ent->d_name, locale_size);
        locale[locale_size] = '\0';

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", directory, ent->d_name);

        i18n_manager->locales[i18n_manager->count++] = load_locale_file(path, locale);
    }

    closedir(dir);

    if (i18n_manager->count > 0)
    {
        i18n_manager->default_locale = &i18n_manager->locales[0];
    }

    atexit(i18n_shutdown);
}

/**
 * Returns a translation by key and language.
 *
 * Searches for a translation by key in the specified locale.
 * If the language is not found, the default locale is used.
 * If the key is not found, the key itself is returned.
 *
 * The function does not allocate memory — the returned string
 * belongs to the i18n manager.
 *
 * @param key  Translation key (for example: "welcome").
 * @param lang Language code ("en", "ru", NULL for default).
 *
 * @return The translation string or key if the translation is not found.
 *
 * Example:
 *   const char* text = cHTTPX_i18n_t("welcome", "ru");
 */
const char* cHTTPX_i18n_t(const char* key, const char* lang)
{
    if (!i18n_manager || !i18n_manager->default_locale)
        return key;

    i18n_locale_t* loc = i18n_manager->default_locale;

    if (lang)
    {
        for (size_t i = 0; i < i18n_manager->count; i++)
        {
            if (strcmp(i18n_manager->locales[i].locale, lang) == 0)
            {
                loc = &i18n_manager->locales[i];
                break;
            }
        }
    }

    for (size_t i = 0; i < loc->count; i++)
    {
        if (strcmp(loc->entries[i].key, key) == 0)
        {
            return loc->entries[i].value;
        }
    }

    return key;
}

int cHTTPX_i18n_languages(chttpx_serv_t* server, const char** languages, size_t count, const char* fallback)
{
    if (!server || !server->initialized)
        return CHTTPX_ERR_INVALID_ARGUMENT;
    return _chttpx_server_set_languages(server, languages, count, fallback);
}

const char* LANGUAGE_CODES[LANG_COUNT] = {
    "en", // LANG_EN
    "ru", // LANG_RU
    "es", // LANG_ES
    "fr"  // LANG_FR
};

i18n_language_t i18n_lang_from_string(const char* code)
{
    if (!code)
        return LANG_EN;

    for (int i = 0; i < LANG_COUNT; i++)
    {
        if (strcmp(code, LANGUAGE_CODES[i]) == 0)
        {
            return (i18n_language_t)i;
        }
    }

    return LANG_EN;
}
