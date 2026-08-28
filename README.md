<p align="center">
  <img alt="libchttpx logo" src="https://avatars.githubusercontent.com/u/252895549?s=400&u=6c747c431c2844620af7772fcd716ef423a6ab1d&v=4" height="150" />
  <h3 align="center">netcorelink/libchttpx</h3>
  <p align="center">Cross-platform HTTP/WebSocket server library in C/C++</p>
</p>

---

`libchttpx` — библиотека для HTTP-сервера на C/C++: маршруты, middleware, CORS, JSON-парсинг, WebSocket, загрузка файлов.

**Зависимости:** `libcjson` (Linux: `libcjson-dev`, Windows: входит в zip-релиз).

---

## Установка

### Linux — последняя версия

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
```

### Linux — конкретная версия

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh -s -- --version=1.5.5
```

Также работает: `-v=1.5.5`, `-v 1.5.5`, `--version v1.5.5`.

Локально:

```bash
sudo bash scripts/install.sh --version=1.5.5
sudo bash scripts/install.sh --help
```

Файлы ставятся в `/usr/local`:

| Путь | Содержимое |
|------|------------|
| `/usr/local/include/libchttpx/` | заголовки |
| `/usr/local/lib/libchttpx.so` | shared library |
| `/usr/local/lib/pkgconfig/libchttpx.pc` | pkg-config |

Проверка:

```bash
pkg-config --modversion libchttpx
ldconfig -p | grep libchttpx
```

### Windows — последняя версия

```powershell
iwr https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.ps1 -UseBasicParsing | iex
```

### Windows — конкретная версия

```powershell
iwr https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.ps1 -OutFile install.ps1
powershell -ExecutionPolicy Bypass -File install.ps1 --version=1.5.5
```

Через pipe `| iex` аргументы не передаются — используйте переменную окружения:

```powershell
$env:LIBCHTTPX_VERSION = '1.5.5'
iwr https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.ps1 -UseBasicParsing | iex
```

### Ручная установка из GitHub Releases

Список версий: https://github.com/netcorelink/libchttpx/releases

```bash
VERSION=v1.5.5
curl -L "https://github.com/netcorelink/libchttpx/releases/download/${VERSION}/libchttpx-dev.tar.gz" -o libchttpx-dev.tar.gz
tar -xzf libchttpx-dev.tar.gz
cd libchttpx-dev
sudo cp -r include/* /usr/local/include/libchttpx/
sudo cp libchttpx.so /usr/local/lib/
sudo cp libchttpx.pc /usr/local/lib/pkgconfig/
sudo ldconfig
```

### Docker (base image для сборки приложений)

```dockerfile
FROM noneandundefined/libchttpx:latest

WORKDIR /app
COPY . .
RUN gcc main.c $(pkg-config --cflags --libs libchttpx) -lcjson -o app
```

Образ содержит toolchain + установленную `libchttpx`. Тег `latest` обновляется при push в `main`.

> Для фиксации версии в production используйте tarball из [Releases](https://github.com/netcorelink/libchttpx/releases) или соберите образ из git-тега (`git checkout v1.5.5 && docker build -t libchttpx:1.5.5 .`).

### Сборка из исходников

```bash
git clone https://github.com/netcorelink/libchttpx.git
cd libchttpx
sudo apt install -y gcc make libcjson-dev   # Debian/Ubuntu

make libchttpx.so
sudo make lib-install PREFIX=/usr/local
sudo ldconfig
```

Архив для релиза:

```bash
make lin-lib   # → libchttpx-dev.tar.gz
```

---

## Компиляция вашего приложения

```bash
gcc main.c $(pkg-config --cflags --libs libchttpx) -lcjson -o server
```

Windows (после `install.ps1`):

```powershell
gcc server.c -I"%ProgramFiles%\libchttpx\include" -L"%ProgramFiles%\libchttpx" -lchttpx -lws2_32 -o server.exe
```

Подключение:

```c
#include <libchttpx/libchttpx.h>
```

---

## Быстрый старт

```c
#include <libchttpx/libchttpx.h>
#include <stdio.h>

void home(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResHtml(cHTTPX_StatusOK, "<h1>Hello</h1>");
}

int main(void)
{
    chttpx_serv_t serv = {0};

    size_t max_clients = 1024;
    if (cHTTPX_Init(&serv, 8080, &max_clients) != 0)
        return 1;

    chttpx_router_t api = cHTTPX_RoutePathPrefix("/api/v1");
    cHTTPX_RegisterRoute(&api, "GET", "/", home);

    cHTTPX_Listen();
    cHTTPX_Shutdown();
    return 0;
}
```

---

## Документация

### Инициализация и лимиты

```c
chttpx_serv_t serv = {0};
size_t max_clients = 1024;

cHTTPX_Init(&serv, 8080, &max_clients);  /* NULL вместо &max_clients → 255 по умолчанию */

serv.read_timeout_sec = 300;
serv.write_timeout_sec = 300;
serv.idle_timeout_sec = 90;
```

### CORS

```c
const char* allowed_origins[] = {
    "https://example.com",
    "http://localhost:8080",
};

cHTTPX_Cors(allowed_origins, 2, NULL, "Content-Type, Authorization");
```

| Параметр | Описание |
|----------|----------|
| `origins` | Разрешённые Origin (точное совпадение) |
| `origins_count` | Количество элементов |
| `methods` | `NULL` → `GET, POST, PUT, DELETE, OPTIONS` |
| `headers` | `NULL` → `Content-Type` |

### Middleware

```c
chttpx_middleware_result_t auth_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* token = cHTTPX_HeaderGet(req, "Authorization");
    if (!token)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusUnauthorized, "{\"error\":\"unauthorized\"}");
        return out;
    }
    return next;
}

cHTTPX_MiddlewareRecovery();
cHTTPX_MiddlewareLogging();
cHTTPX_MiddlewareUse(auth_middleware);
```

Встроенные: `cHTTPX_MiddlewareRecovery()`, `cHTTPX_MiddlewareLogging()`, `cHTTPX_MiddlewareRateLimiter(max, window_sec)`.

### Маршруты

Маршруты группируются префиксом:

```c
chttpx_router_t v1 = cHTTPX_RoutePathPrefix("/api/v1");

cHTTPX_RegisterRoute(&v1, "GET", "/", home_index);
cHTTPX_RegisterRoute(&v1, "GET", "/users/{uuid}", get_user);
cHTTPX_RegisterRoute(&v1, "POST", "/users", create_user);
```

Параметры пути:

```c
const char* uuid = cHTTPX_Param(req, "uuid");
```

Query-параметры:

```c
const char* page = cHTTPX_Query(req, "page");
```

### Handlers и ответы

```c
void home_index(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResHtml(cHTTPX_StatusOK, "<h1>Home</h1>");
}

*res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"ok\":true}");
*res = cHTTPX_ResText(cHTTPX_StatusOK, "plain text");
*res = cHTTPX_ResFile(cHTTPX_StatusOK, "/path/to/file.png");
```

### JSON parsing

```c
typedef struct {
    char* uuid;
    char* password;
    int is_admin;
} user_t;

void create_user(chttpx_request_t* req, chttpx_response_t* res)
{
    user_t user = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("uuid", &user.uuid, true, 0, 36, VALIDATOR_NONE),
        chttpx_validation_string("password", &user.password, true, 6, 16, VALIDATOR_NONE),
        chttpx_validation_boolean("is_admin", &user.is_admin, false),
    };

    if (!cHTTPX_Parse(req, fields, ARRAY_LEN(fields)))
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\":\"%s\"}", req->error_msg);
        return;
    }

    *res = cHTTPX_ResJson(cHTTPX_StatusCreated, "{\"uuid\":\"%s\"}", user.uuid);
}
```

> Ошибки парсинга — в `req->error_msg`. Для i18n-сообщений: `cHTTPX_Validate(req, fields, ARRAY_LEN(fields), "en")`.

### Заголовки и IP клиента

```c
const char* origin = cHTTPX_HeaderGet(req, "Origin");
const char* ip = cHTTPX_ClientIP(req);
```

### WebSocket

Event-driven API: один poll-поток на все соединения.

```c
void chat_on_open(chttpx_wsocket_t* ws, void* userdata)
{
    (void)userdata;
    const char* room = cHTTPX_WSocketParam(ws, "room_id");
    cHTTPX_WSocketSend(ws, room ? room : "welcome");
}

void chat_on_message(chttpx_wsocket_t* ws, const unsigned char* data, size_t len, int opcode, void* userdata)
{
    (void)opcode;
    (void)userdata;

    char msg[4096];
    if (len >= sizeof(msg))
        len = sizeof(msg) - 1;
    memcpy(msg, data, len);
    msg[len] = '\0';

    /* Только клиенты в той же комнате (тот же URL) */
    cHTTPX_WSocketBroadcastPeers(ws, msg);
}

void chat_on_close(chttpx_wsocket_t* ws, void* userdata)
{
    (void)ws;
    (void)userdata;
}

static chttpx_wsocket_callbacks_t chat_callbacks = {
    .on_open = chat_on_open,
    .on_message = chat_on_message,
    .on_close = chat_on_close,
};

chttpx_router_t v1 = cHTTPX_RoutePathPrefix("/api/v1");
cHTTPX_WSocketRegisterRoute(&v1, "/ws/chat/{room_id}", &chat_callbacks);
```

Клиент подключается к комнате через URL:

```
ws://localhost:8080/api/v1/ws/chat/lobby
```

| Функция | Назначение |
|---------|------------|
| `cHTTPX_WSocketSend(ws, text)` | Отправить одному клиенту |
| `cHTTPX_WSocketBroadcastPeers(ws, text)` | Всем в той же комнате |
| `cHTTPX_WSocketBroadcast(path, text)` | По полному пути |
| `cHTTPX_WSocketBroadcastRoom("room_id", "42", text)` | По параметру маршрута |
| `cHTTPX_WSocketParam(ws, "room_id")` | Параметр из URL |

### Завершение работы

```c
cHTTPX_Listen();      /* блокирует, принимает соединения */
cHTTPX_Shutdown();    /* освобождает ресурсы, закрывает WebSocket */
```

---

## Preview-сборки для PR

При pull request CI публикует артефакт `libchttpx-dev-pr-N` (14 дней). Инструкция появляется в описании PR.

```bash
gh run download RUN_ID -R netcorelink/libchttpx -n libchttpx-dev-pr-42
tar -xzf libchttpx-dev.tar.gz && cd libchttpx-dev
# установка как в разделе «Ручная установка»
```

---

## Лицензия

MIT — см. исходники репозитория.
