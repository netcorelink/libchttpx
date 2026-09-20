# libchttpx documentation

The root README contains only the project overview, installation methods, and navigation. Detailed usage is documented here by module.

| Module | Documentation | Covers |
| --- | --- | --- |
| App runtime | [app](app/README.md) | `cHTTPX_App`, multiple servers, remote registry, `Call`, `CallEx` |
| Server | [server](server/README.md) | config, limits, lifecycle, errors, shutdown |
| TLS / HTTPS | [tls](tls/README.md) | optional OpenSSL build, HTTPS server/client, CA verification, mTLS |
| Compression | [compression](compression/README.md) | Accept-Encoding negotiation, optional gzip, MIME policy, providers |
| Routing | [routing](routing/README.md) | routers, groups, method helpers, path parameters |
| Middleware | [middleware](middleware/README.md) | global/router/route before and after middleware |
| Request | [request](request/README.md) | metadata, headers, params, queries, bearer tokens, body access |
| Responses | [responses](responses/README.md) | response helpers, headers, ownership, files, status codes |
| JSON | [json](json/README.md) | bind, validation, normalization, builder |
| Memory | [memory](memory/README.md) | request arena, defer/detach, named contexts |
| Uploads | [uploads](uploads/README.md) | multipart, form fields, files, upload policies, MIME helpers |
| CORS | [cors](cors/README.md) | origins, methods, headers, preflight |
| Cookies | [cookies](cookies/README.md) | read/write cookies and attributes |
| i18n | [i18n](i18n/README.md) | request IDs, `Accept-Language`, translations |
| Logging | [logging](logging/README.md) | logger callbacks and request logging middleware |
| Rate limiting | [rate-limiting](rate-limiting/README.md) | per-server fixed-window limiter |
| WebSocket | [websocket](websocket/README.md) | current experimental API and limitations |

## Recommended reading order

1. [App runtime](app/README.md)
2. [Server configuration](server/README.md)
3. [Routing](routing/README.md)
4. [Requests](request/README.md) and [Responses](responses/README.md)
5. [Middleware](middleware/README.md)
6. Add the remaining modules as needed.

Examples assume:

```c
#include <libchttpx/libchttpx.h>
```

unless a module explicitly states otherwise.
