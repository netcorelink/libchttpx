# Response compression

libchttpx can compress buffered HTTP responses after the route handler and before the response is written to the socket. The built-in provider is gzip through zlib; the provider API is intentionally generic so Brotli or Zstandard providers can be added without changing route handlers.

## Availability

gzip support is part of the standard libchttpx build. zlib is a regular library dependency, so there is no separate compression build mode or compression-specific build flag.

Compression itself is still optional at runtime: nothing is compressed until the application calls `cHTTPX_CompressionUse()`.

For a source build on Debian/Ubuntu, install zlib together with the other development dependencies:

```bash
sudo apt install -y build-essential libcjson-dev zlib1g-dev
make libchttpx.so
make test-compression
```

TLS remains independently optional:

```bash
make TLS=1 libchttpx.so
```

## Basic setup

```c
chttpx_compression_config_t compression = cHTTPX_CompressionDefault();
compression.min_size = 1024;
compression.level = 5;

int result = cHTTPX_CompressionUse(server, &compression);
if (result != cHTTPX_OK) {
    /* handle configuration / provider error */
}
```

Configure compression before starting the server.

Default values:

- minimum body size: 1024 bytes
- gzip level: 5
- provider: built-in gzip
- text, JSON, JavaScript, XML, SVG and similar textual MIME types are compressible
- already compressed images, audio/video, fonts, archives, PDF, WASM and generic octet streams are excluded

## Accept-Encoding negotiation

The middleware parses `Accept-Encoding` case-insensitively, including quality values and the wildcard.

Examples:

```text
Accept-Encoding: gzip
Accept-Encoding: gzip;q=0.8, identity;q=1
Accept-Encoding: br;q=1, gzip;q=0.7, identity;q=0.2
Accept-Encoding: *;q=1, identity;q=0
```

A coding with `q=0` is not selected. When identity has a higher quality than the available provider, the original response is sent. If no supported coding and no identity representation are acceptable, libchttpx returns `406 Not Acceptable`.

When compression is applied, libchttpx adds:

```http
Content-Encoding: gzip
Vary: Accept-Encoding
```

`Content-Length` is generated from the compressed body size when the response is sent.

## MIME include/exclude policy

The default MIME policy is intentionally conservative. Exclusions win over inclusions.

You can replace either list:

```c
const char *include[] = {
    "text/*",
    "application/json",
    "application/*+json",
};

const char *exclude[] = {
    "text/event-stream",
};

chttpx_compression_config_t compression = cHTTPX_CompressionDefault();
compression.include_types = include;
compression.include_types_count = CHTTPX_ARRAY_LEN(include);
compression.exclude_types = exclude;
compression.exclude_types_count = CHTTPX_ARRAY_LEN(exclude);

cHTTPX_CompressionUse(server, &compression);
```

Patterns support `*`, including suffix patterns such as `application/*+json`.

## Responses that are not transformed

Compression is skipped when transforming the response would be inappropriate, including:

- no response body
- `HEAD`
- informational responses and `204`, `205`, `206`, `304`
- range requests / `Content-Range`
- an existing `Content-Encoding`
- `Cache-Control: no-transform`
- bodies below `min_size`
- excluded/non-included MIME types

This keeps range semantics and application-provided encodings intact.

## Disable compression for a route or response

Per route:

```c
chttpx_route_t *route = cHTTPX_Get(&router, "/download", download_handler);
cHTTPX_RouteCompression(route, false);
```

Per response:

```c
static void handler(chttpx_request_t *req, chttpx_response_t *res)
{
    (void)req;
    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"ok\":true}");
    cHTTPX_ResponseCompression(res, false);
}
```

## Custom providers

A provider receives one complete buffered response and returns an allocated encoded buffer:

```c
static int encode_zstd(
    const unsigned char *input,
    size_t input_size,
    int level,
    unsigned char **output,
    size_t *output_size,
    void *user_data)
{
    /* allocate *output and encode the complete input */
    return cHTTPX_OK;
}

chttpx_compression_provider_t providers[] = {
    {
        .encoding = "zstd",
        .encode_buffer = encode_zstd,
        .user_data = NULL,
    },
};

chttpx_compression_config_t compression = cHTTPX_CompressionDefault();
compression.providers = providers;
compression.providers_count = CHTTPX_ARRAY_LEN(providers);

cHTTPX_CompressionUse(server, &compression);
```

Providers are evaluated using the client's quality values. When qualities tie, provider configuration order is used. The current callback is deliberately buffer-oriented; a future streaming provider interface can be added without changing response policy or negotiation configuration.

## Errors and logging

Invalid configuration returns `cHTTPX_ERR_INVALID_ARGUMENT`; allocation failures return `cHTTPX_ERR_MEMORY`; gzip/provider failures use `cHTTPX_ERR_COMPRESSION` internally.

If encoding fails and identity is acceptable, libchttpx logs a warning and sends the original response. If identity is forbidden, it sends an empty `500 Internal Server Error` rather than silently violating `Accept-Encoding`.

## Example

```bash
make examples-compression
./.build/example-compression
curl --compressed -i http://127.0.0.1:8080/
```
