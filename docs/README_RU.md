# Документация libchttpx

Корневой `README_RU.md` содержит обзор проекта, способы установки и навигацию. Подробная документация разделена по модулям.

| Модуль | Документация | Что описано |
| --- | --- | --- |
| App runtime | [app](app/README_RU.md) | `cHTTPX_App`, несколько серверов, remote registry, `Call`, `CallEx` |
| Server | [server](server/README_RU.md) | config, limits, lifecycle, error codes, shutdown |
| TLS / HTTPS | [tls](tls/README_RU.md) | optional OpenSSL build, HTTPS server/client, CA verification, mTLS |
| Routing | [routing](routing/README_RU.md) | routers, groups, method helpers, path parameters |
| Middleware | [middleware](middleware/README_RU.md) | global/router/route middleware, before/after |
| Request | [request](request/README_RU.md) | metadata, headers, params, queries, bearer token, body |
| Responses | [responses](responses/README_RU.md) | response helpers, headers, ownership, files, status codes |
| JSON | [json](json/README_RU.md) | bind, validation, normalization, builder |
| Memory | [memory](memory/README_RU.md) | request allocator, defer/detach, named contexts |
| Uploads | [uploads](uploads/README_RU.md) | multipart, files, form fields, policies, MIME |
| CORS | [cors](cors/README_RU.md) | origins, methods, headers, preflight |
| Cookies | [cookies](cookies/README_RU.md) | чтение и установка cookies |
| i18n | [i18n](i18n/README_RU.md) | request ID, `Accept-Language`, translations |
| Logging | [logging](logging/README_RU.md) | logger callback и logging middleware |
| Rate limiting | [rate-limiting](rate-limiting/README_RU.md) | встроенный fixed-window limiter |
| WebSocket | [websocket](websocket/README_RU.md) | текущий experimental API и ограничения |

## Рекомендуемый порядок

1. [App runtime](app/README_RU.md)
2. [Server config](server/README_RU.md)
3. [Routing](routing/README_RU.md)
4. [Request](request/README_RU.md) и [Responses](responses/README_RU.md)
5. [Middleware](middleware/README_RU.md)
6. Остальные модули по необходимости.

Во всех примерах предполагается:

```c
#include <libchttpx/libchttpx.h>
```

если в конкретном разделе не указано иначе.
