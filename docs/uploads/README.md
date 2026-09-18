# Uploads, multipart forms, and MIME helpers

libchttpx supports multipart forms, URL-encoded forms, multiple uploaded files, route upload policies, and temporary-file ownership.

## Read uploaded files

First uploaded file:

```c
const chttpx_file_t* file =
    cHTTPX_RequestFile(req);
```

File by multipart field name:

```c
const chttpx_file_t* avatar =
    cHTTPX_FormFile(
        req,
        "avatar"
    );
```

Useful fields:

```c
avatar->path;
avatar->original_name;
avatar->content_type;
avatar->size;
avatar->temporary;
avatar->field_name;
```

The returned structure is request-owned.

## Text form fields

```c
const char* caption =
    cHTTPX_FormValue(
        req,
        "caption"
    );
```

URL-encoded form values are decoded.

## Temporary-file lifecycle

Uploaded files are temporary by default and are removed during request cleanup.

Use the file while the request is active:

```c
FILE* input =
    fopen(avatar->path, "rb");
```

Keep uploaded temporary files:

```c
if (!cHTTPX_FileKeep(req))
{
    /* failed */
}
```

Detach one specific file:

```c
if (!cHTTPX_FileDetach(req, avatar))
{
    /* failed */
}
```

After keep/detach, the application is responsible for the file lifecycle.

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

cHTTPX_RouteUploadPolicy(
    route,
    &policy
);
```

The server deep-copies allowed MIME strings.

Policy rejection skips the handler and returns an HTTP error such as:

- `413 Payload Too Large`
- `415 Unsupported Media Type`

## MIME helpers

```c
cHTTPX_MimeMatch(mime, "image/*");
cHTTPX_MimeIsImage(mime);
cHTTPX_MimeIsVideo(mime);
cHTTPX_MimeIsAudio(mime);
```

## Large multipart bodies

Multipart bodies are spooled to disk while being received, then parsed into bounded text values and request-owned temporary files. Large multipart uploads therefore do not require a RAM buffer equal to the complete upload.

Normal JSON/text/urlencoded request bodies remain memory-backed and are limited by `max_body_size`.

`max_upload_size` controls the server-wide upload limit; a route policy can apply a stricter limit.

## Chunked requests

Chunked request transfer encoding is supported. Ambiguous framing such as conflicting `Content-Length` or unsupported transfer encoding is rejected.

`cHTTPX_OnBodyChunk()` can replay a received body/upload in bounded chunks; see [Request data](../request/README.md).
