# Routing

Routers are bound to a specific App-managed server.

## Root router and method helpers

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

Method helpers return a stable `chttpx_route_t*` for route middleware/policies.

## Groups

```c
chttpx_router_t auth =
    cHTTPX_RouteGroup(&api, "/auth");

chttpx_router_t admin =
    cHTTPX_RouteGroup(&api, "/admin");

cHTTPX_Post(&auth, "/login", login_handler);
cHTTPX_Get(&admin, "/users", list_users);
```

Resulting paths are `/api/v2/auth/login` and `/api/v2/admin/users`.

## Path parameters

```c
cHTTPX_Get(
    &api,
    "/users/{user_id}",
    get_user
);
```

Read values with:

- `cHTTPX_Param()`
- `cHTTPX_ParamInt()`
- `cHTTPX_ParamU64()`
- `cHTTPX_ParamBool()`

Example:

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

## Router middleware

```c
chttpx_router_t private_api =
    cHTTPX_RouteGroup(&api, "");

cHTTPX_RouterUse(
    &private_api,
    authenticate
);

cHTTPX_Get(
    &private_api,
    "/users/me",
    get_me
);
```

Router middleware applies to routes registered through that router.

## Route middleware/policies

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

Upload policies also attach to a route with `cHTTPX_RouteUploadPolicy()`.

## Compatibility API

`cHTTPX_RegisterRoute()` remains available, but method helpers are preferred because they return the route handle.

## Lifetime

A router is a lightweight value containing a server pointer, prefix, and router middleware. `cHTTPX_RouterFree()` clears router state; registered routes are server-owned and released on server/App shutdown.
