# Maintainer architecture baseline

This document defines the architecture and maintenance constraints for libchttpx.

## Server responsibilities

`cHTTPX_serv.c` owns public server configuration, lifecycle, route registration, and shutdown coordination.

`cHTTPX_runtime.c` owns connection lifecycle and the state machine. The runtime never exposes the event backend or scheduler through the public synchronous handler API.

`cHTTPX_event.c` owns readiness notification and non-blocking socket registration.

`cHTTPX_worker.c` owns the bounded application job queue and the fixed 32-thread worker pool.

The intended flow is:

```text
listener / event loop
        |
        +-- non-blocking connection I/O
        |
        +-- HTTP framing
                |
                v
          bounded job queue
                |
                v
         32 application workers
                |
                v
        completed response queue
                |
                v
           event loop
                |
                v
        non-blocking socket write
```

Socket read/write ownership remains in the runtime/event loop. Workers execute request processing and application code and return completed response buffers.

## Connection state machine

Connections move through explicit states:

- TLS handshake;
- reading headers;
- reading body;
- processing;
- writing;
- closing.

A connection is removed from readiness polling while application code is processing it. Completed responses are handed back to the event loop before socket writes resume.

## Backpressure

The application queue is bounded by the server connection capacity. Submitting to a full or stopping pool is rejected instead of allocating an unbounded linked list or growing queue.

Runtime metrics expose queue depth, active workers, rejected jobs, completed jobs, and total queue wait time.

Other request limits remain enforced independently, including maximum clients, headers, body, and upload size.

## Ownership and thread safety

The event loop owns live connection objects except while a connection is executing inside the worker pool. The worker returns ownership through the completion queue.

Routes, middleware, and server configuration are configured before startup and treated as immutable while the server is running.

Metrics and completion queues use explicit synchronization. Runtime stop state is atomic.

Public request-owned memory is valid until request cleanup unless it is detached. Public response-owned memory is released by `cHTTPX_ResponseCleanup()`.

## Public API and ABI policy

Backward-compatible additions are allowed in MINOR releases. Breaking signature, semantic, or public layout changes require a MAJOR release.

Opaque/internal state stays under `src/` and must not be promoted to the public header without a compatibility reason.

Canonical public result names use `cHTTPX_*`. Legacy `CHTTPX_*` result macros are compatibility aliases and may be disabled with `CHTTPX_DISABLE_LEGACY_ERROR_NAMES`.

## Error model

`chttpx_error_t` APIs return `cHTTPX_OK` or a negative `cHTTPX_ERR_*` value.

Pointer APIs use `NULL` for failure. APIs that intentionally use another convention must document it.

The library should log an error once at the layer that has actionable context and still return the error to the application when the application needs to decide what to do.

## Security boundary

The HTTP parser and framing code process attacker-controlled bytes. Changes must be tested with malformed, fragmented, oversized, conflicting, truncated, and ambiguous input.

The fuzz smoke target continuously exercises header parsing. Parser regression tests remain the source for exact expected behavior around framing rules.

## CI merge gates

The expected order is:

```text
format/static analysis
        -> build matrix
        -> unit/integration tests
        -> ASan/UBSan/TSan
        -> parser fuzz smoke
```

A sanitizer or fuzz failure blocks merge even when normal unit tests pass.

## Documentation source of truth

Module documentation lives under `docs/`. The website should consume or link to that content rather than maintaining separate hand-written copies of the same API description.

## Benchmark protocol

Performance comparisons must record:

- CPU and RAM;
- operating system;
- compiler and flags;
- connection concurrency;
- request/response payload;
- HTTP/TLS mode;
- keep-alive mode;
- requests per second;
- p50/p95/p99 latency;
- CPU utilization;
- memory per connection.

Benchmarks are tracked separately from correctness CI because noisy timing variance must not block unrelated pull requests.
