# libchttpx

`libchttpx` is a compact cross-platform HTTP/1.1 server library for C. It provides an App-based runtime, routing, middleware, request parsing, JSON binding and responses, uploads, request-scoped memory, CORS, cookies, i18n, logging, rate limiting, and graceful shutdown while keeping a direct C-style API.

The library is designed so handlers contain application logic instead of repetitive HTTP plumbing.

## Highlights

- Linux and Windows support
- multiple independent HTTP servers inside one `cHTTPX_App`
- local direct server-to-server calls and remote HTTP calls
- route groups and `{parameter}` paths
- global, router, and route middleware with before/after phases
- typed path/query accessors and URL decoding
- request-scoped allocation, deferred cleanup, and named contexts
- JSON parsing, validation, normalization, binding, and builder responses
- multipart forms, multiple uploads, upload policies, and temporary-file cleanup
- request IDs and `Accept-Language` negotiation
- CORS, cookies, logging callbacks, and rate limiting
- configurable server limits and graceful shutdown

> `cHTTPX_ResFile()` currently reads the complete file into memory. Streaming responses, `sendfile()`, and zero-copy output are not implemented in the current API.

## Installation

### Linux: install script

The installer downloads the latest GitHub release, installs cJSON when supported by the package manager, and copies the shared library, headers, and pkg-config file into `/usr/local`.

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
```

Compile an application with:

```bash
gcc server.c -o server $(pkg-config --cflags --libs libchttpx)
```

### Windows: PowerShell installer

```powershell
iwr https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.ps1 -UseBasicParsing | iex
```

Restart the terminal after installation so the environment changes are visible.

### Docker

Pull the published runtime image:

```bash
docker pull noneandundefined/libchttpx:latest
```

The image contains the installed shared library, headers, pkg-config metadata, and the cJSON runtime. It is useful as a base/runtime image for applications using libchttpx.

```dockerfile
FROM noneandundefined/libchttpx:latest

COPY my-server /usr/local/bin/my-server
CMD ["/usr/local/bin/my-server"]
```

The published image is not a complete compiler toolchain. Build your application in a build stage and copy the resulting binary into the runtime image, or install build tools explicitly.

### Build from source on Linux

Requirements: GCC, Make, pkg-config, and cJSON development files.

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libcjson-dev

git clone https://github.com/netcorelink/libchttpx.git
cd libchttpx

make lin-lib
sudo make lib-install PREFIX=/usr/local DESTDIR=
```

Tests:

```bash
make test
make test-sanitize
```

### Build from source on Windows

Use MinGW/GCC. The Windows build uses the bundled `lib/cjson` source.

```powershell
git clone https://github.com/netcorelink/libchttpx.git
cd libchttpx

make win-lib
make test-win
```

The generated DLL/import library and headers are copied into `tools/`.

## Documentation

Detailed documentation is split by functionality:

- [Documentation index](docs/README.md)
- [App runtime, multiple servers, AppRemote, Call and CallEx](docs/app/README.md)
- [Server configuration, limits, lifecycle and error codes](docs/server/README.md)
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
- [WebSocket API — experimental](docs/websocket/README.md)

## License

MIT. See [LICENSE](LICENSE).
