# Request data

Handlers receive a parsed `chttpx_request_t`.

## Metadata

Common fields:

```c
req->request_id;
req->client_ip;
req->method;
req->path;
req->protocol;
req->user_agent;
req->language;
req->content_type;
req->content_length;
req->body;
req->body_size;
```

These fields belong to the request.

## Headers

```c
const char* authorization =
    cHTTPX_HeaderGet(req, "Authorization");

const char* ip =
    cHTTPX_ClientIP(req);
```

`cHTTPX_HeaderSet()` can set/replace a request header. Header APIs validate names/values and reject CR/LF injection.

## Bearer token

```c
const char* token =
    cHTTPX_BearerToken(req);
```

The returned pointer is borrowed request memory.

## Path params

```c
const char* raw =
    cHTTPX_Param(req, "user_id");

uint64_t user_id;
cHTTPX_ParamU64(req, "user_id", &user_id);
```

Typed helpers: `ParamInt`, `ParamU64`, `ParamBool`.

## Query params

```c
uint64_t offset;
bool enabled;
double score;

cHTTPX_QueryU64Default(req, "offset", &offset, 0);
cHTTPX_QueryBool(req, "enabled", &enabled);
cHTTPX_QueryDouble(req, "score", &score);
```

Also available: `Query`, `QueryInt`, `QueryU64`.

Typed parsing rejects empty/invalid/trailing input, overflow, and negative unsigned values.

## URL decoding

```c
char decoded[256];

if (!cHTTPX_UrlDecode(
        decoded,
        sizeof(decoded),
        "hello%20world",
        true))
{
    /* invalid encoding */
}
```

Query/form parsing automatically decodes valid URL encoding.

## Body

Normal JSON/text/urlencoded bodies are available as `req->body` + `req->body_size`. Do not assume arbitrary binary bodies are null-terminated.

## Chunk replay

`cHTTPX_OnBodyChunk()` replays an already received body/disk-backed upload in bounded chunks.

```c
static int consume(
    const unsigned char* data,
    size_t size,
    void* user_data)
{
    FILE* out = user_data;
    return fwrite(data, 1, size, out) == size ? 0 : -1;
}

cHTTPX_OnBodyChunk(req, consume, file);
```

This is not a callback invoked while network bytes are still arriving.

## Ownership

Headers, params, queries, cookies, form values, and metadata are borrowed/request-owned. Do not free them manually. Use [request-scoped memory](../memory/README.md) for application allocations tied to request lifetime.
