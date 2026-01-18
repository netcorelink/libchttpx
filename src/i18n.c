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

#include "i18n.h"

#include "crosspltm.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#if defined(_WIN32) || defined(_WIN64)
#include "lib/cjson/cJSON.h"
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

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* data = malloc(size + 1);
    fread(data, 1, size, f);
    data[size] = '\0';

    fclose(f);

    cJSON* root = cJSON_Parse(data);
    free(data);
    if (!root || !cJSON_IsObject(root))
        return loc;

    loc.count = cJSON_GetArraySize(root);
    loc.entries = calloc(loc.count, sizeof(i18n_entry_t));

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
            indx++;
        }
    }

    cJSON_Delete(root);
    return loc;
}

static void i18n_shutdown(void)
{
    for (size_t i = 0; i < i18n_manager->count; i++)
    {
        for (size_t j = 0; j < i18n_manager->locales[i].count; j++)
        {
            free(i18n_manager->locales[i].entries[j].key);
            free(i18n_manager->locales[i].entries[j].value);
        }

        free(i18n_manager->locales[i].entries);
    }
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
    memset(&i18n_manager, 0, sizeof(i18n_manager));

    DIR* dir = opendir(directory);
    if (!dir)
        return;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (!strstr(ent->d_name, ".json"))
            continue;

        if (i18n_manager->count >= MAX_LOCALES)
            break;

        char locale[8] = {0};
        strncpy(locale, ent->d_name, strchr(ent->d_name, '.') - ent->d_name);

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
    if (!i18n_manager)
    {
        return key;
    }

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