# Contributing to libchttpx

Thank you for contributing to libchttpx. Changes to the HTTP parser, network runtime, TLS, memory ownership, public API, and concurrency model are treated as security- or stability-sensitive.

## Pull request baseline

A pull request must not be merged when it:

- adds or changes public API without tests and documentation;
- changes ownership or lifetime rules without documenting who owns the resource and when it becomes invalid;
- changes public structure layout or function signatures without an API/ABI compatibility note;
- introduces blocking socket I/O into the application worker pool;
- adds unbounded queues, buffers, or client-controlled allocations;
- ignores a system or library error that can affect correctness;
- adds library-side `exit()` or `abort()`;
- introduces mutable global state without an explicit synchronization and ownership model;
- mixes unrelated refactoring with a feature or bug fix;
- changes parser/network/TLS behavior without negative-path tests.

## Runtime model

The server runtime owns sockets and network readiness. Application workers execute middleware, routing, handlers, validation, and application code only.

The worker pool has a fixed size of 32 threads. It is intentionally not exposed as a public configuration option. Work is submitted through a bounded queue and overload must fail predictably instead of growing memory without a limit.

Routes, middleware, and server configuration are expected to be registered before the server starts. After startup they are treated as immutable unless an API explicitly documents otherwise.

## Public naming

Public functions and public result values use the `cHTTPX_` prefix.

Canonical result values are:

`cHTTPX_OK`, `cHTTPX_ERR_MEMORY`, `cHTTPX_ERR_SOCKET`, `cHTTPX_ERR_BIND`, `cHTTPX_ERR_LISTEN`, `cHTTPX_ERR_INVALID_ARGUMENT`, `cHTTPX_ERR_LIMIT`, `cHTTPX_ERR_IO`, `cHTTPX_ERR_NOT_FOUND`, `cHTTPX_ERR_PROTOCOL`, `cHTTPX_ERR_STATE`, `cHTTPX_ERR_UNAVAILABLE`, `cHTTPX_ERR_TIMEOUT`, `cHTTPX_ERR_TLS`, and `cHTTPX_ERR_COMPRESSION`.

Legacy `CHTTPX_*` result names remain compatibility aliases for existing applications. New code and documentation must use `cHTTPX_*`.

## Ownership and cleanup

Every public pointer-bearing API must document whether a pointer is borrowed or owned, who frees it, and how long it remains valid.

Request-scoped allocations and deferred cleanups are released when the request is cleaned up unless explicitly detached. Response helpers that allocate response body data transfer ownership to the response object, which must be released with `cHTTPX_ResponseCleanup()`.

Cleanup functions must be safe after partial initialization when the API documents that cleanup is allowed.

## Error handling

Functions returning `chttpx_error_t` use `cHTTPX_OK` on success and a negative `cHTTPX_ERR_*` value on failure.

Pointer-returning functions use `NULL` for failure unless the API explicitly documents another contract. Avoid logging the same error on multiple internal layers; the layer with enough context to make the message useful should log it.

## Formatting

The repository uses `.clang-format`. Keep logical sections readable with blank lines between unrelated operations and between definition blocks. Do not compress multiple independent operations into visually dense blocks.

Do not wrap function declarations or calls merely to reduce line length when the existing style keeps them readable.

## Required checks

Changes must pass:

- formatting check;
- Linux build and unit/integration tests;
- Windows build/tests when the change is cross-platform;
- ASan/UBSan;
- TSan for concurrency-sensitive code;
- parser fuzz smoke tests;
- static analysis.

Compiler warnings are treated as defects. Library code is built with `-Wall -Wextra -Wpedantic` where supported.

## Parser and network changes

Parser inputs are untrusted. Tests should cover malformed framing, duplicate/conflicting Content-Length, Transfer-Encoding conflicts, oversized headers/body, embedded control bytes, truncated input, partial I/O, timeout, disconnect, and chunked edge cases.

Fuzz targets are part of the normal maintenance workflow and should be extended when new parsing surfaces are added.

## Versioning

libchttpx follows Semantic Versioning:

- backward-compatible feature: MINOR;
- backward-compatible fix: PATCH;
- incompatible public API/ABI change: MAJOR.

Use conventional PR titles so release automation can select the correct bump.

## Performance claims

Any published performance claim must include CPU, RAM, OS, compiler and flags, concurrency, payload size, TLS mode, keep-alive mode, requests/second, p50/p95/p99 latency, CPU utilization, and memory-per-connection where applicable.
