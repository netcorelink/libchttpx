# Routing

Router привязан к конкретному AppServer.

## Router и method helpers

```c
chttpx_router_t api =
    cHTTPX_RoutePathPrefix(server, "/api/v2");

cHTTPX_Get(&api, "/health", health);
cHTTPX_Post(&api, "/users", create_user);
cHTTPX_Put(&api, "/users/{user_id}", replace_user);
cHTTPX_Patch(&api, "/users/{user_id}", update_user);
cHTTPX_Delete(&api, "/users/{user_id}", delete_user);
cHTTPX_Options(&api, "/users", options_handler);
```

Method helpers возвращают стабильный `chttpx_route_t*`, на который можно навесить middleware или upload policy.

## Route groups

```c
chttpx_router_t auth =
    cHTTPX_RouteGroup(&api, "/auth");

chttpx_router_t admin =
    cHTTPX_RouteGroup(&api, "/admin");

cHTTPX_Post(&auth, "/login", login_handler);
cHTTPX_Get(&admin, "/users", list_users);
```

Получатся routes:

- `POST /api/v2/auth/login`
- `GET /api/v2/admin/users`

## Path params

```c
cHTTPX_Get(
    &api,
    "/users/{user_id}",
    get_user
);
```

В handler:

```c
uint64_t user_id;

if (!cHTTPX_ParamU64(req, "user_id", &user_id))
{
    *res = cHTTPX_ResError(
        cHTTPX_StatusBadRequest,
        "invalid user_id"
    );
    return;
}
```

Helpers:

- `cHTTPX_Param()`
- `cHTTPX_ParamInt()`
- `cHTTPX_ParamU64()`
- `cHTTPX_ParamBool()`

## Router middleware

```c
chttpx_router_t private_api =
    cHTTPX_RouteGroup(&api, "");

cHTTPX_RouterUse(&private_api, authenticate);

cHTTPX_Get(
    &private_api,
    "/users/me",
    get_me
);
```

Middleware такого router применяется только к routes, зарегистрированным через него.

## Route middleware

```c
chttpx_route_t* route =
    cHTTPX_Post(
        &private_api,
        "/admin/import",
        import_handler
    );

cHTTPX_RouteUse(route, require_admin);
cHTTPX_RouteUseAfter(route, audit_request);
```

## Compatibility API

`cHTTPX_RegisterRoute()` оставлен для совместимости. Для нового кода удобнее method helpers.

## Lifetime

Router — лёгкая структура с server pointer, prefix и middleware config. Зарегистрированные routes принадлежат server и очищаются при shutdown.
