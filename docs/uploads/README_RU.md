# Uploads, multipart и MIME

libchttpx поддерживает multipart, URL-encoded forms, несколько файлов, route upload policies и temporary-file lifecycle.

## Получить файл

Первый upload:

```c
const chttpx_file_t* file =
    cHTTPX_RequestFile(req);
```

По имени multipart field:

```c
const chttpx_file_t* avatar =
    cHTTPX_FormFile(req, "avatar");
```

Поля:

```c
avatar->path;
avatar->original_name;
avatar->content_type;
avatar->size;
avatar->temporary;
avatar->field_name;
```

## Text form field

```c
const char* caption =
    cHTTPX_FormValue(req, "caption");
```

## Temporary files

Uploads по умолчанию удаляются после request.

Сохранить файлы:

```c
cHTTPX_FileKeep(req);
```

Отвязать конкретный файл:

```c
cHTTPX_FileDetach(req, avatar);
```

После keep/detach lifecycle файла контролирует приложение.

## Route upload policy

```c
const char* allowed[] = {
    "image/jpeg",
    "image/png",
    "image/gif"
};

chttpx_upload_policy_t policy = {
    .max_size = 10 * 1024 * 1024,
    .allowed_types = allowed,
    .allowed_types_count =
        CHTTPX_ARRAY_LEN(allowed),
};

chttpx_route_t* route =
    cHTTPX_Patch(
        &api,
        "/users/me/avatar",
        upload_avatar
    );

cHTTPX_RouteUploadPolicy(route, &policy);
```

Server копирует список MIME types.

При нарушении policy handler не запускается; возвращается например 413 или 415.

## MIME helpers

```c
cHTTPX_MimeMatch(mime, "image/*");
cHTTPX_MimeIsImage(mime);
cHTTPX_MimeIsVideo(mime);
cHTTPX_MimeIsAudio(mime);
```

## Большие uploads

Multipart body записывается во временный disk-backed stream и не требует RAM размером со весь upload.

JSON/text/urlencoded bodies остаются memory-backed и ограничиваются `max_body_size`.

Для uploads используется `max_upload_size`, а route policy может задать более строгий limit.

Chunked request bodies также поддерживаются.
