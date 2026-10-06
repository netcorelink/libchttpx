# libchttpx

`libchttpx` — компактная HTTP/2-библиотека для C под Linux и macOS. Она предоставляет App-runtime, routing, middleware, разбор запросов, JSON binding/response, uploads, request-scoped память, CORS, cookies, i18n, logging, rate limiting и graceful shutdown, сохраняя простой C-style API.

Идея библиотеки: handler должен содержать бизнес-логику приложения, а не повторяющийся HTTP boilerplate.

## Основные возможности

- поддержка Linux и macOS
- несколько независимых HTTP-серверов внутри одного `cHTTPX_App`
- прямые local-вызовы между серверами и remote-вызовы по HTTP/HTTPS
- route groups и пути с `{parameter}`
- global/router/route middleware с before/after фазами
- typed path/query helpers и URL decoding
- request-scoped allocator, defer cleanup и named contexts
- JSON parsing, validation, normalization, binding и JSON builder
- multipart forms, несколько файлов, upload policy и автоматическое удаление временных файлов
- request ID и `Accept-Language` negotiation
- CORS, cookies, callback-based logging и rate limiting
- настраиваемое gzip-сжатие ответов с `Accept-Encoding` negotiation
- опциональные встроенные metrics, thread-safe snapshot и Prometheus exporter
- first-class Server-Sent Events по HTTP/2 с retry, heartbeat и disconnect handling
- лимиты сервера и graceful shutdown

> `cHTTPX_ResFile()` теперь работает напрямую с файлом и отправляет его ограниченными чанками по 64 КиБ, поэтому для ответа на несколько гигабайт не требуется столько же RAM. Zero-copy через `sendfile()` пока не реализован.

## Установка

### Debian / Ubuntu через APT (рекомендуется)

Для Debian/Ubuntu на **amd64** репозиторий libchttpx нужно добавить один раз:

```bash
curl -fsSL https://netcorelink.github.io/libchttpx/apt/libchttpx.sources \
  | sudo tee /etc/apt/sources.list.d/libchttpx.sources >/dev/null

sudo apt update
sudo apt install libchttpx-dev
```

После этого новые версии libchttpx устанавливаются обычным обновлением APT:

```bash
sudo apt update
sudo apt upgrade
```

Удалить библиотеку:

```bash
sudo apt remove libchttpx-dev
```

Удалить сам репозиторий из системы:

```bash
sudo rm /etc/apt/sources.list.d/libchttpx.sources
sudo apt update
```

Сейчас репозиторий использует `Trusted: yes`, поэтому отдельный GPG-ключ добавлять не требуется. Позже репозиторий можно перевести на подпись пакетов.

После установки приложение можно собрать через `pkg-config`:

```bash
gcc server.c -o server $(pkg-config --cflags --libs libchttpx)
```

### Другие Linux-дистрибутивы

Нативные пакеты также прикладываются к GitHub Releases.

**Fedora / RHEL и совместимые RPM-дистрибутивы**

```bash
sudo dnf install ./libchttpx-dev-*.rpm
```

**Arch Linux**

```bash
sudo pacman -U ./libchttpx-dev-*.pkg.tar.zst
```

**Alpine Linux**

```sh
sudo apk add --allow-untrusted ./libchttpx-dev_*.apk
```

Нужный пакет можно скачать из [GitHub Releases](https://github.com/netcorelink/libchttpx/releases).

### Готовые пакеты для macOS (без Homebrew)

В GitHub Releases публикуются self-contained архивы для macOS со всеми
runtime-зависимостями (cJSON, nghttp2, OpenSSL). Это основной способ установки
на Catalina и других системах, где современный Homebrew уже недоступен:

- `libchttpx-macos-10.15-x86_64.tar.gz` — Intel, macOS 10.15 Catalina и новее
- `libchttpx-macos-11-arm64.tar.gz` — Apple Silicon, macOS 11 Big Sur и новее

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
```

Скрипт смотрит `sw_vers -productVersion` и `uname -m`, скачивает нужный архив и
ставит библиотеку в `/usr/local` вместе с bundled-зависимостями.

### Legacy install-скрипт (Linux)

На Linux тот же скрипт ставит `libchttpx-dev.tar.gz` и системные зависимости
через пакетный менеджер дистрибутива:

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
```

### Docker

```bash
docker pull noneandundefined/libchttpx:latest
```

Image содержит установленную shared library, headers, pkg-config metadata и runtime-зависимости cJSON, zlib и nghttp2.

```dockerfile
FROM noneandundefined/libchttpx:latest

COPY my-server /usr/local/bin/my-server
CMD ["/usr/local/bin/my-server"]
```

Native TLS/HTTPS через OpenSSL описан в [Native TLS / HTTPS](docs/tls/README_RU.md).

Сжатие ответов входит в обычную сборку; gzip включается в коде через `cHTTPX_CompressionUse()`. Подробнее: [Сжатие HTTP-ответов](docs/compression/README_RU.md).

## Документация

Подробная документация разделена по функционалу:

- [Индекс документации](docs/README_RU.md)
- [App runtime, несколько серверов, AppRemote, Call и CallEx](docs/app/README_RU.md)
- [Конфигурация сервера, лимиты, lifecycle и error codes](docs/server/README_RU.md)
- [Native TLS / HTTPS](docs/tls/README_RU.md)
- [Сжатие HTTP-ответов](docs/compression/README_RU.md)
- [Metrics и Prometheus exporter](docs/metrics/README_RU.md)
- [Routing и route groups](docs/routing/README_RU.md)
- [Middleware](docs/middleware/README_RU.md)
- [Requests, headers, params, queries и body](docs/request/README_RU.md)
- [Responses и ownership](docs/responses/README_RU.md)
- [JSON binding, validation и JSON builder](docs/json/README_RU.md)
- [Request-scoped память и contexts](docs/memory/README_RU.md)
- [Uploads, multipart forms и MIME helpers](docs/uploads/README_RU.md)
- [CORS](docs/cors/README_RU.md)
- [Cookies](docs/cookies/README_RU.md)
- [Request ID и i18n](docs/i18n/README.md)
- [Logging](docs/logging/README_RU.md)
- [Rate limiting](docs/rate-limiting/README_RU.md)
- [Server-Sent Events](docs/sse/README_RU.md)
- [WebSocket API — experimental](docs/websocket/README_RU.md)

## Лицензия

BSD 3-Clause. См. [LICENSE](LICENSE).
