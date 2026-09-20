# Native TLS / HTTPS

libchttpx can use OpenSSL as an **optional** transport backend. Plain HTTP builds do not require OpenSSL.

## Build with TLS

On Debian/Ubuntu:

```bash
sudo apt install build-essential libcjson-dev libssl-dev openssl
make TLS=1 libchttpx.so
```

To install a TLS-enabled build from source:

```bash
make TLS=1 libchttpx.so
sudo make TLS=1 lib-install PREFIX=/usr/local DESTDIR=
```

The TLS install uses `libchttpx-tls.pc`, whose private link metadata declares OpenSSL. The default `libchttpx.pc` remains OpenSSL-free.

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

Certificate and private-key files are required when `tls.enabled` is true. A plain build returns `CHTTPX_ERR_UNAVAILABLE` when TLS is requested.

The runnable example is `example/tls.c`:

```bash
make examples-tls
.build/example-tls server.crt server.key
```

## HTTPS remote calls

`cHTTPX_AppRemote()` accepts both `http://` and `https://` URLs. For HTTPS, certificate and hostname verification are enabled by default and the system trust store is used.

```c
cHTTPX_AppRemote(
    &app,
    "payments",
    "https://payments.example.com"
);
```

For a private CA, use `cHTTPX_AppRemoteEx()`:

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

Setting `verify_peer = false` disables certificate and hostname verification. This is intended only for controlled development/testing environments.

## Mutual TLS

Client certificates are supported for HTTPS remote calls:

```c
chttpx_tls_client_config_t tls =
    cHTTPX_DefaultTLSClientConfig();

tls.client_cert_file = "/etc/myapp/client.crt";
tls.client_key_file = "/etc/myapp/client.key";

cHTTPX_AppRemoteEx(
    &app,
    "secure-api",
    "https://secure.example.com",
    &tls
);
```

A server can request/require client certificates with:

```c
config.tls.client_ca_file = "/etc/myapp/client-ca.pem";
config.tls.require_client_cert = true;
```

## Errors and logging

TLS initialization, handshake, certificate verification, and encrypted I/O failures use the normal libchttpx error path. TLS-specific failures return `CHTTPX_ERR_TLS`; a build without TLS support returns `CHTTPX_ERR_UNAVAILABLE` for HTTPS/TLS configuration.

TLS server failures are reported through the configured server logger. Remote TLS failures also use the source server logger.

## Integration test

```bash
make test-tls
```

The test creates a local self-signed certificate, verifies that the default client rejects it, then trusts that certificate explicitly and completes a real HTTPS request.
