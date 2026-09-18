# Request data

Handler получает разобранный `chttpx_request_t`.

## Metadata

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

Эти данные принадлежат request.

## Headers

```c
const char* authorization =
    cHTTPX_HeaderGet(req, "Authorization");

const char* ip =
    cHTTPX_ClientIP(req);
```

`cHTTPX_HeaderSet()` меняет или добавляет request header. Header API проверяет имена/значения и блокирует CR/LF injection.

## Bearer

```c
const char* token =
    cHTTPX_BearerToken(req);
```

Pointer borrowed — освобождать не нужно.

## Path params

```c
uint64_t user_id;

if (!cHTTPX_ParamU64(req, "user_id", &user_id))
{
    /* invalid */
}
```

Есть `Param`, `ParamInt`, `ParamU64`, `ParamBool`.

## Query

```c
uint64_t offset;
bool enabled;
double score;

cHTTPX_QueryU64Default(req, "offset", &offset, 0);
cHTTPX_QueryBool(req, "enabled", &enabled);
cHTTPX_QueryDouble(req, "score", &score);
```

Typed helpers отклоняют invalid input, overflow и отрицательные значения для unsigned.

## URL decode

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

Query/form parser автоматически декодирует URL encoding.

## Body

Для обычных JSON/text/urlencoded запросов:

```c
req->body;
req->body_size;
```

Не считайте arbitrary binary body null-terminated.

## Chunk replay

`cHTTPX_OnBodyChunk()` позволяет читать уже полученный body/upload ограниченными chunks.

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

Это не streaming callback во время прихода network bytes.

## Ownership

Headers, params, query, cookies, form values и metadata — borrowed/request-owned. Не освобождайте их вручную.
