# Native TLS / HTTPS

libchttpx умеет использовать OpenSSL как **опциональный** transport backend. Обычная HTTP-сборка не зависит от OpenSSL.

## Сборка с TLS

Debian/Ubuntu:

```bash
sudo apt install build-essential libcjson-dev libssl-dev openssl
make TLS=1 libchttpx.so
```

Установка TLS-сборки из исходников:

```bash
make TLS=1 libchttpx.so
sudo make TLS=1 lib-install PREFIX=/usr/local DESTDIR=
```

При такой установке используется `libchttpx-tls.pc`, где OpenSSL указан как private dependency. Обычный `libchttpx.pc` остаётся без OpenSSL.

## HTTPS server

```c
chttpx_config_t config = cHTTPX_DefaultConfig();
config.port = 8443;
config.tls.enabled = true;
config.tls.cert_file = "/etc/myapp/server.crt";
config.tls.key_file = "/etc/myapp/server.key";

chttpx_serv_t* server =
    cHTTPX_AppServer(&app, "https", &config);
```

Когда `tls.enabled = true`, необходимо указать certificate и private key. Если библиотека собрана без TLS, попытка включить TLS вернёт `cHTTPX_ERR_UNAVAILABLE`.

Готовый пример находится в `example/tls.c`:

```bash
make examples-tls
.build/example-tls server.crt server.key
```

## HTTPS remote calls

`cHTTPX_AppRemote()` принимает и `http://`, и `https://`. Для HTTPS проверка сертификата и hostname включена по умолчанию, используется системное хранилище доверенных CA.

```c
cHTTPX_AppRemote(
    &app,
    "payments",
    "https://payments.example.com"
);
```

Для собственного CA используйте `cHTTPX_AppRemoteEx()`:

```c
chttpx_tls_client_config_t tls =
    cHTTPX_DefaultTLSClientConfig();

tls.ca_file = "/etc/myapp/private-ca.pem";

cHTTPX_AppRemoteEx(
    &app,
    "payments",
    "https://payments.internal:8443",
    &tls
);
```

`verify_peer = false` отключает проверку сертификата и hostname. Этот режим предназначен только для контролируемой разработки/тестов.

## Mutual TLS

Для remote HTTPS можно передать клиентский сертификат:

```c
chttpx_tls_client_config_t tls =
    cHTTPX_DefaultTLSClientConfig();

tls.client_cert_file = "/etc/myapp/client.crt";
tls.client_key_file = "/etc/myapp/client.key";
```

На server стороне клиентский сертификат можно сделать обязательным:

```c
config.tls.client_ca_file = "/etc/myapp/client-ca.pem";
config.tls.require_client_cert = true;
```

## Ошибки и logging

Ошибки инициализации TLS, handshake, certificate verification и encrypted I/O проходят через обычный error path библиотеки. TLS-ошибки возвращают `cHTTPX_ERR_TLS`; если TLS вообще не собран, HTTPS/TLS-конфигурация возвращает `cHTTPX_ERR_UNAVAILABLE`.

Ошибки TLS server пишутся через настроенный logger. Ошибки исходящих HTTPS-вызовов также проходят через logger исходного server.

## Integration test

```bash
make test-tls
```

Тест создаёт локальный self-signed сертификат, сначала проверяет, что default client его отклоняет, затем явно доверяет этому сертификату и выполняет настоящий HTTPS-запрос.
