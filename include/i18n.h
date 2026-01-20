/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef I18N_H
#define I18N_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <string.h>

#define MAX_LOCALES 64

typedef struct {
    char *key;
    char *value;
} i18n_entry_t;

typedef struct {
    /* Language en, ru, es */
    char locale[8];

    /* Entries i18n */
    i18n_entry_t *entries;
    size_t count;
} i18n_locale_t;

typedef struct {
    i18n_locale_t locales[MAX_LOCALES];
    size_t count;
    i18n_locale_t *default_locale;
} i18n_manager_t;

typedef enum {
    LANG_EN,
    LANG_RU,
    LANG_ES,
    LANG_FR,
    LANG_COUNT
} i18n_language_t;

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
void cHTTPX_i18n(const char *directory);

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
const char* cHTTPX_i18n_t(const char *key, const char *lang);

i18n_language_t i18n_lang_from_string(const char* code);

#ifdef __cplusplus
}
#endif

#endif