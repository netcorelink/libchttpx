# Request-scoped memory and contexts

Request-scoped ownership removes cleanup boilerplate from handlers with many return paths.

## Allocate

```c
user_state_t* state =
    cHTTPX_Alloc(req, sizeof(*state));

char* copy =
    cHTTPX_Strdup(req, source);
```

Both are released automatically after the request.

## Deferred cleanup

```c
FILE* file = fopen(path, "rb");

if (!file)
    return;

if (cHTTPX_Defer(
        req,
        file,
        (chttpx_cleanup_fn)fclose) != 0)
{
    fclose(file);
    return;
}
```

Use `Defer` for arbitrary resources with a cleanup callback.

## Detach

```c
FILE* owned =
    cHTTPX_Detach(req, file);
```

After successful detach, request cleanup no longer owns the resource.

## Named contexts

```c
auth_context_t* auth =
    malloc(sizeof(*auth));

if (cHTTPX_ContextSet(
        req,
        "auth",
        auth,
        free) != 0)
{
    free(auth);
    return;
}
```

Read later:

```c
auth_context_t* auth =
    cHTTPX_ContextGet(req, "auth");
```

Detach when state must outlive the request:

```c
auth_context_t* owned =
    cHTTPX_ContextDetach(req, "auth");
```

Replacing a named context cleans the previous value using its registered callback.

## Legacy context

`req->context` / `req->context_free` remain for compatibility. Named contexts are preferred because unrelated middleware can store independent state.

## Ownership summary

| Value | Lifetime |
| --- | --- |
| `cHTTPX_Alloc` / `Strdup` | request |
| `cHTTPX_Defer` resource | request unless detached |
| named context | request unless detached |
| JSON bind strings/arrays | request |
| headers/query/params/cookies | request/borrowed |
| temporary upload | request unless kept/detached |

Never use request-owned memory after request completion.
