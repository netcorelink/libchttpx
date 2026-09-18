# libchttpx

`libchttpx` — компактная кроссплатформенная HTTP/1.1-библиотека для C. Она предоставляет App-runtime, routing, middleware, разбор запросов, JSON binding/response, uploads, request-scoped память, CORS, cookies, i18n, logging, rate limiting и graceful shutdown, сохраняя простой C-style API.

Идея библиотеки: handler должен содержать бизнес-логику приложения, а не повторяющийся HTTP boilerplate.

## Основные возможности

- Linux и Windows
- несколько независимых HTTP-серверов внутри одного `cHTTPX_App`
- прямые local-вызовы между серверами и remote-вызовы по HTTP
- route groups и пути с `{parameter}`
- global/router/route middleware с before/after фазами
- typed path/query helpers и URL decoding
- request-scoped allocator, defer cleanup и named contexts
- JSON parsing, validation, normalization, binding и JSON builder
- multipart forms, несколько файлов, upload policy и автоматическое удаление временных файлов
- request ID и `Accept-Language` negotiation
- CORS, cookies, callback-based logging и rate limiting
- лимиты сервера и graceful shutdown

> `cHTTPX_ResFile()` пока полностью читает файл в память. Streaming response, `sendfile()` и zero-copy output в текущем API не реализованы.

## Установка

### Linux: install script

Скрипт скачивает последний GitHub Release, при необходимости устанавливает cJSON и копирует библиотеку, headers и pkg-config файл в `/usr/local`.

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
```

Компиляция приложения:

```bash
gcc server.c -o server $(pkg-config --cflags --libs libchttpx)
```

### Windows: PowerShell installer

```powershell
iwr https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.ps1 -UseBasicParsing | iex
```

После установки перезапустите терминал, чтобы применились переменные окружения.

### Docker

Скачать опубликованный runtime image:

```bash
docker pull noneandundefined/libchttpx:latest
```

Image содержит установленную shared library, headers, pkg-config metadata и runtime cJSON.

```dockerfile
FROM noneandundefined/libchttpx:latest

COPY my-server /usr/local/bin/my-server
CMD ["/usr/local/bin/my-server"]
```

Опубликованный image не является полноценным compiler toolchain. Приложение лучше собирать в отдельном build stage, а готовый binary копировать в runtime image, либо отдельно установить build tools.

### Самостоятельная сборка на Linux

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libcjson-dev

git clone https://github.com/netcorelink/libchttpx.git
cd libchttpx

make lin-lib
sudo make lib-install PREFIX=/usr/local DESTDIR=
```

Тесты:

```bash
make test
make test-sanitize
```

### Самостоятельная сборка на Windows

Используется MinGW/GCC. Для Windows cJSON уже находится в `lib/cjson`.

```powershell
git clone https://github.com/netcorelink/libchttpx.git
cd libchttpx

make win-lib
make test-win
```

DLL, import library и headers копируются в `tools/`.

## Документация

Подробная документация разделена по функционалу:

- [Индекс документации](docs/README.md)
- [App runtime, несколько серверов, AppRemote, Call и CallEx](docs/app/README.md)
- [Конфигурация сервера, лимиты, lifecycle и error codes](docs/server/README.md)
- [Routing и route groups](docs/routing/README.md)
- [Middleware](docs/middleware/README.md)
- [Requests, headers, params, queries и body](docs/request/README.md)
- [Responses и ownership](docs/responses/README.md)
- [JSON binding, validation и JSON builder](docs/json/README.md)
- [Request-scoped память и contexts](docs/memory/README.md)
- [Uploads, multipart forms и MIME helpers](docs/uploads/README.md)
- [CORS](docs/cors/README.md)
- [Cookies](docs/cookies/README.md)
- [Request ID и i18n](docs/i18n/README.md)
- [Logging](docs/logging/README.md)
- [Rate limiting](docs/rate-limiting/README.md)
- [WebSocket API — experimental](docs/websocket/README.md)

Модульная документация внутри `docs/` сейчас написана на английском.

## Лицензия

MIT. См. [LICENSE](LICENSE).
