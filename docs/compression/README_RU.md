# Сжатие HTTP-ответов

libchttpx умеет сжимать buffered HTTP-ответ после выполнения handler и до отправки ответа в сокет. Встроенный provider — gzip через zlib. Provider API сделан универсальным, чтобы позже можно было добавить Brotli или Zstandard без изменения handler'ов.

## Доступность

gzip входит в обычную сборку libchttpx. zlib является стандартной зависимостью библиотеки, поэтому отдельного режима сборки и флага `COMPRESSION=1` нет.

Само сжатие остаётся опциональным во время выполнения: пока приложение не вызовет `cHTTPX_CompressionUse()`, ответы не сжимаются.

Для сборки из исходников на Debian/Ubuntu zlib устанавливается вместе с остальными development-зависимостями:

```bash
sudo apt install -y build-essential libcjson-dev zlib1g-dev
make libchttpx.so
make test-compression
```

TLS остаётся отдельной опцией:

```bash
make TLS=1 libchttpx.so
```

## Базовая настройка

```c
chttpx_compression_config_t compression = cHTTPX_CompressionDefault();
compression.min_size = 1024;
compression.level = 5;

int result = cHTTPX_CompressionUse(server, &compression);
if (result != CHTTPX_OK) {
    /* обработка ошибки */
}
```

Настраивать compression лучше до запуска сервера.

Значения по умолчанию:

- минимальный размер body — 1024 байта
- gzip level — 5
- provider — встроенный gzip
- текст, JSON, JavaScript, XML, SVG и похожие текстовые MIME сжимаются
- уже сжатые изображения, audio/video, fonts, архивы, PDF, WASM и `application/octet-stream` исключены

## Negotiation через Accept-Encoding

Middleware разбирает `Accept-Encoding` без учёта регистра, включая quality values и wildcard.

Примеры:

```text
Accept-Encoding: gzip
Accept-Encoding: gzip;q=0.8, identity;q=1
Accept-Encoding: br;q=1, gzip;q=0.7, identity;q=0.2
Accept-Encoding: *;q=1, identity;q=0
```

Кодировка с `q=0` не выбирается. Если у `identity` quality выше, чем у доступного provider, отправляется исходный body. Если клиент не принимает ни поддерживаемое сжатие, ни identity, libchttpx отвечает `406 Not Acceptable`.

При gzip-сжатии автоматически добавляются:

```http
Content-Encoding: gzip
Vary: Accept-Encoding
```

`Content-Length` при отправке вычисляется уже по размеру сжатого body.

## Include/exclude для MIME

Политика по умолчанию специально консервативная. Exclude имеет приоритет над include.

Списки можно заменить:

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

В pattern поддерживается `*`, в том числе `application/*+json`.

## Какие ответы не сжимаются

Compression пропускается, когда преобразование ответа нежелательно или ломает HTTP semantics:

- body отсутствует
- метод `HEAD`
- informational response, а также `204`, `205`, `206`, `304`
- range request / `Content-Range`
- уже есть `Content-Encoding`
- `Cache-Control: no-transform`
- body меньше `min_size`
- MIME не входит в include или входит в exclude

Так сохраняется корректная работа range response и уже заданного приложением encoding.

## Отключение для route или конкретного response

Для route:

```c
chttpx_route_t *route = cHTTPX_Get(&router, "/download", download_handler);
cHTTPX_RouteCompression(route, false);
```

Для конкретного response:

```c
static void handler(chttpx_request_t *req, chttpx_response_t *res)
{
    (void)req;
    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"ok\":true}");
    cHTTPX_ResponseCompression(res, false);
}
```

## Собственный provider

Provider получает целиком buffered body и возвращает выделенный encoded buffer:

```c
static int encode_zstd(
    const unsigned char *input,
    size_t input_size,
    int level,
    unsigned char **output,
    size_t *output_size,
    void *user_data)
{
    /* выделить *output и закодировать input */
    return CHTTPX_OK;
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

Provider выбирается по quality клиента. При одинаковом quality используется порядок providers в config. Сейчас callback рассчитан на buffered body; в будущем streaming provider API можно добавить отдельно, не меняя конфигурацию negotiation и policy.

## Ошибки и logging

Некорректная конфигурация возвращает `CHTTPX_ERR_INVALID_ARGUMENT`, ошибка памяти — `CHTTPX_ERR_MEMORY`, а ошибка compression provider — `CHTTPX_ERR_COMPRESSION` внутри middleware.

Если provider не смог сжать body, но клиент принимает identity, libchttpx пишет warning в logger и отправляет исходный ответ. Если identity запрещён, возвращается пустой `500 Internal Server Error`, а не ответ с неподдерживаемым encoding.

## Benchmark: CPU против bandwidth

Запуск:

```bash
make benchmark-compression
```

Benchmark многократно сжимает JSON-like payload размером 1 MiB на gzip levels 1, 5 и 9 и печатает CSV:

```text
level,input_bytes,compressed_bytes,ratio,throughput_mib_s
```

`ratio` показывает экономию трафика, а `throughput_mib_s` — стоимость по CPU. Результат зависит от процессора, compiler, версии zlib и данных, поэтому универсальный «лучший» level в документации не фиксируется.

## Пример

```bash
make examples-compression
./.build/example-compression
curl --compressed -i http://127.0.0.1:8080/
```
