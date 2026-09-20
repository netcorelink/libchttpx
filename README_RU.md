# libchttpx

`libchttpx` — компактная кроссплатформенная HTTP/1.1-библиотека для C. Она предоставляет App-runtime, routing, middleware, разбор запросов, JSON binding/response, uploads, request-scoped память, CORS, cookies, i18n, logging, rate limiting и graceful shutdown, сохраняя простой C-style API.

Идея библиотеки: handler должен содержать бизнес-логику приложения, а не повторяющийся HTTP boilerplate.

## Основные возможности

- Linux и Windows
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
- optional gzip-сжатие ответов с `Accept-Encoding` negotiation
- лимиты сервера и graceful shutdown

> `cHTTPX_ResFile()` пока полностью читает файл в память. Streaming response, `sendfile()` и zero-copy output в текущем API не реализованы.

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

### Старые install-скрипты

Bash- и PowerShell-скрипты пока остаются как запасной способ установки.

Linux:

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
```

Windows:

```powershell
iwr https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.ps1 -UseBasicParsing | iex
```

После установки в Windows перезапустите терминал.

### Docker

```bash
docker pull noneandundefined/libchttpx:latest
```

Image содержит установленную shared library, headers, pkg-config metadata и runtime cJSON.

```dockerfile
FROM noneandundefined/libchttpx:latest

COPY my-server /usr/local/bin/my-server
CMD ["/usr/local/bin/my-server"]
```

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

Native TLS/HTTPS через OpenSSL включается отдельно и не добавляет OpenSSL-зависимость обычной HTTP-сборке:

```bash
sudo apt install -y libssl-dev openssl
make TLS=1 libchttpx.so
make test-tls
```

Настройка server certificate, HTTPS remote calls, custom CA и mTLS описана в [Native TLS / HTTPS](docs/tls/README_RU.md).

Встроенное gzip-сжатие ответов через zlib также включается отдельно:

```bash
sudo apt install -y zlib1g-dev
make COMPRESSION=1 libchttpx.so
make test-compression
make benchmark-compression
```

TLS и gzip можно включить вместе: `make TLS=1 COMPRESSION=1 libchttpx.so`. Текущие нативные Linux-пакеты пока собираются в обычном режиме без встроенного gzip provider. Подробнее: [Сжатие HTTP-ответов](docs/compression/README_RU.md).

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

- [Индекс документации](docs/README_RU.md)
- [App runtime, несколько серверов, AppRemote, Call и CallEx](docs/app/README_RU.md)
- [Конфигурация сервера, лимиты, lifecycle и error codes](docs/server/README_RU.md)
- [Native TLS / HTTPS](docs/tls/README_RU.md)
- [Сжатие HTTP-ответов](docs/compression/README_RU.md)
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
- [WebSocket API — experimental](docs/websocket/README_RU.md)


## Лицензия

BSD 3-Clause. См. [LICENSE](LICENSE).
