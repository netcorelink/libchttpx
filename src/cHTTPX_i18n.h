/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef I18N_H
#define I18N_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <string.h>

#define MAX_LOCALES 64

    struct chttpx_serv;

    /** One key/value translation entry loaded from a locale file. */
    typedef struct
    {
        char* key;
        char* value;
    } i18n_entry_t;

    /** Translations for a single locale (e.g. "en", "ru"). */
    typedef struct
    {
        /* Language en, ru, es */
        char locale[8];

        /* Entries i18n */
        i18n_entry_t* entries;
        size_t count;
    } i18n_locale_t;

    /** In-memory table of loaded locale files. */
    typedef struct
    {
        i18n_locale_t locales[MAX_LOCALES];
        size_t count;
        i18n_locale_t* default_locale;
    } i18n_manager_t;

    /** Supported language codes for server negotiation. */
    typedef enum
    {
        LANG_EN,
        LANG_RU,
        LANG_ES,
        LANG_FR,
        LANG_COUNT
    } i18n_language_t;

    /**
     * Map a language code string to i18n_language_t.
     *
     * @param code Language code (e.g. "en"); NULL defaults to LANG_EN.
     * @return Matching language enum, or LANG_EN if unknown.
     */
    i18n_language_t i18n_lang_from_string(const char* code);

    /**
     * Initializes the global i18n manager.
     *
     * Loads all locale JSON files from the specified directory.
     * The file name determines the locale language:
     * en.json -> "en", ru.json -> "ru", fr.json -> "fr".
     *
     * Memory is freed automatically at program exit.
     *
     * @param directory Path to the directory with locale JSON files.
     */
    void cHTTPX_i18n(const char* directory);

    /**
     * Returns a translation by key and language.
     *
     * Uses the default locale when lang is NULL or unknown; returns key if not found.
     * The returned string belongs to the i18n manager (not copied per call).
     *
     * @param key Translation key (for example: "welcome").
     * @param lang Language code ("en", "ru", NULL for default).
     * @return Translation string, or key if not found.
     */
    const char* cHTTPX_i18n_t(const char* key, const char* lang);

    /**
     * Configure the language preference list used for request negotiation.
     *
     * @param server Initialized HTTP server.
     * @param languages Ordered array of supported language codes.
     * @param count Number of elements in languages.
     * @param fallback Fallback language code.
     * @return cHTTPX_OK on success, otherwise a negative error code.
     */
    int cHTTPX_i18n_languages(struct chttpx_serv* server, const char** languages, size_t count, const char* fallback);

#ifdef __cplusplus
}
#endif

#endif
