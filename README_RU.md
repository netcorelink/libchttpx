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

### Linux: нативные пакеты (рекомендуется)

В Linux-релизах публикуются нативные пакеты для основных семейств дистрибутивов. Скачайте нужный файл из [GitHub Releases](https://github.com/netcorelink/libchttpx/releases), после чего установите его штатным пакетным менеджером.

**Debian / Ubuntu**

```bash
sudo apt install ./libchttpx-dev_*.deb
```

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

Пакет устанавливает shared library, публичный header, pkg-config metadata, лицензию и зависимость от runtime cJSON. Обновление и удаление библиотеки после этого выполняются тем же пакетным менеджером.

Компиляция приложения:

```bash
gcc server.c -o server $(pkg-config --cflags --libs libchttpx)
```

> Пока это установка скачанного пакета из GitHub Release. Отдельный APT/RPM-репозиторий, который позволит выполнять просто `apt install libchttpx-dev` без предварительного скачивания файла, является следующим отдельным этапом публикации.

### Старые install-скрипты

Существующие Bash- и PowerShell-скрипты пока оставлены как запасной вариант для обратной совместимости.

Linux:

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
```

Windows:

```powershell
iwr https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.ps1 -UseBasicParsing | iex
```

После установки в Windows перезапустите терминал, чтобы применились переменные окружения.

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

- [Индекс документации](docs/README_RU.md)
- [App runtime, несколько серверов, AppRemote, Call и CallEx](docs/app/README_RU.md)
- [Конфигурация сервера, лимиты, lifecycle и error codes](docs/server/README_RU.md)
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
