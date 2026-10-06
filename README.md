# libchttpx

`libchttpx` is a compact HTTP/2 server library for C on Linux and macOS. It provides an App-based runtime, routing, middleware, request parsing, JSON binding and responses, uploads, request-scoped memory, CORS, cookies, i18n, logging, rate limiting, and graceful shutdown while keeping a direct C-style API.

The library is designed so handlers contain application logic instead of repetitive HTTP plumbing.

## Highlights

- Linux and macOS support
- multiple independent HTTP servers inside one `cHTTPX_App`
- local direct server-to-server calls and remote HTTP/HTTPS calls
- route groups and `{parameter}` paths
- global, router, and route middleware with before/after phases
- typed path/query accessors and URL decoding
- request-scoped allocation, deferred cleanup, and named contexts
- JSON parsing, validation, normalization, binding, and builder responses
- multipart forms, multiple uploads, upload policies, and temporary-file cleanup
- request IDs and `Accept-Language` negotiation
- CORS, cookies, logging callbacks, and rate limiting
- configurable gzip response compression with `Accept-Encoding` negotiation
- optional built-in metrics with thread-safe snapshots and a Prometheus exporter
- first-class HTTP/2 Server-Sent Events with retry, heartbeat, and disconnect handling
- configurable server limits and graceful shutdown

> `cHTTPX_ResFile()` is disk-backed and streams files in bounded 64 KiB chunks, so multi-gigabyte responses do not require an equally large RAM allocation. Generic zero-copy `sendfile()` output is still not implemented.

## Installation

### Debian / Ubuntu via APT (recommended)

For Debian/Ubuntu on **amd64**, add the libchttpx APT repository once:

```bash
curl -fsSL https://netcorelink.github.io/libchttpx/apt/libchttpx.sources \
  | sudo tee /etc/apt/sources.list.d/libchttpx.sources >/dev/null

sudo apt update
sudo apt install libchttpx-dev
```

After that, libchttpx updates are installed through the normal APT flow:

```bash
sudo apt update
sudo apt upgrade
```

Remove the package:

```bash
sudo apt remove libchttpx-dev
```

Remove the repository itself:

```bash
sudo rm /etc/apt/sources.list.d/libchttpx.sources
sudo apt update
```

The current repository uses `Trusted: yes`, so users do not need to install a separate GPG key. Package signing can be added later.

Compile an application after installation:

```bash
gcc server.c -o server $(pkg-config --cflags --libs libchttpx)
```

### Other Linux distributions

Native packages are also attached to GitHub Releases:

**Fedora / RHEL and compatible RPM distributions**

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

Download the package for your distribution from [GitHub Releases](https://github.com/netcorelink/libchttpx/releases).

### Installer script (Linux / macOS)

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
```

### Docker

Pull the published runtime image:

```bash
docker pull noneandundefined/libchttpx:latest
```

The image contains the installed shared library, headers, pkg-config metadata, and the cJSON, zlib, and nghttp2 runtimes.

```dockerfile
FROM noneandundefined/libchttpx:latest

COPY my-server /usr/local/bin/my-server
CMD ["/usr/local/bin/my-server"]
```

Optional native TLS/HTTPS support uses OpenSSL. See [Native TLS / HTTPS](docs/tls/README.md) for server certificates, HTTPS remote calls, custom CA verification, and mTLS.

Response compression is included in the standard build. Applications enable gzip at runtime with `cHTTPX_CompressionUse()`. See [Response compression](docs/compression/README.md).

## Documentation

Detailed documentation is split by functionality:

- [Documentation index](docs/README.md)
- [App runtime, multiple servers, AppRemote, Call and CallEx](docs/app/README.md)
- [Server configuration, limits, lifecycle and error codes](docs/server/README.md)
- [Native TLS / HTTPS](docs/tls/README.md)
- [Response compression](docs/compression/README.md)
- [Metrics and Prometheus exporter](docs/metrics/README.md)
- [Routing and route groups](docs/routing/README.md)
- [Middleware](docs/middleware/README.md)
- [Requests, headers, params, queries and body access](docs/request/README.md)
- [Responses and ownership](docs/responses/README.md)
- [JSON binding, validation and JSON builder](docs/json/README.md)
- [Request-scoped memory and contexts](docs/memory/README.md)
- [Uploads, multipart forms and MIME helpers](docs/uploads/README.md)
- [CORS](docs/cors/README.md)
- [Cookies](docs/cookies/README.md)
- [Request IDs and i18n](docs/i18n/README.md)
- [Logging](docs/logging/README.md)
- [Rate limiting](docs/rate-limiting/README.md)
- [Server-Sent Events](docs/sse/README.md)
- [WebSocket API — experimental](docs/websocket/README.md)

## License

BSD 3-Clause. See [LICENSE](LICENSE).
