# Request-scoped память и contexts

Request-scoped ownership позволяет не писать cleanup на каждом return из handler.

## Alloc / Strdup

```c
user_state_t* state =
    cHTTPX_Alloc(req, sizeof(*state));

char* copy =
    cHTTPX_Strdup(req, source);
```

Эта память автоматически освобождается после request.

## Defer

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

`Defer` подходит для любых ресурсов с cleanup callback.

## Detach

Если ресурс должен пережить request:

```c
FILE* owned =
    cHTTPX_Detach(req, file);
```

После успешного detach request больше не владеет этим ресурсом.

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

Получить:

```c
auth_context_t* auth =
    cHTTPX_ContextGet(req, "auth");
```

Передать ownership приложению:

```c
auth_context_t* owned =
    cHTTPX_ContextDetach(req, "auth");
```

При замене context старое значение cleanup-ится зарегистрированным callback.

## Legacy context

`req->context` и `req->context_free` остаются для совместимости. Named contexts лучше, потому что разные middleware не конфликтуют друг с другом.

## Ownership summary

| Значение | Lifetime |
| --- | --- |
| `cHTTPX_Alloc / Strdup` | request |
| `cHTTPX_Defer` resource | request до detach |
| named context | request до detach |
| JSON-bound strings/arrays | request |
| headers/query/params/cookies | request/borrowed |
| temporary upload | request до keep/detach |
