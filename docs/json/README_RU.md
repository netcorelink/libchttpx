# JSON binding, validation и builder

## Bind JSON request

`cHTTPX_BindJSON()` объединяет parsing, normalization, validation и запись результата в ваши переменные.

```c
typedef struct
{
    char* email;
    char* username;
} create_user_t;

static void create_user(
    chttpx_request_t* req,
    chttpx_response_t* res)
{
    create_user_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField(
            "email",
            &payload.email,
            true,
            3,
            254,
            CHTTPX_TRIM | CHTTPX_LOWERCASE,
            NULL
        ),
        cHTTPX_StringField(
            "username",
            &payload.username,
            true,
            3,
            32,
            CHTTPX_TRIM,
            NULL
        ),
    };

    if (!cHTTPX_BindJSON(
            req,
            res,
            fields,
            CHTTPX_ARRAY_LEN(fields)))
        return;

    *res = cHTTPX_ResMessage(
        cHTTPX_StatusCreated,
        "user created"
    );
}
```

При ошибке `BindJSON` возвращает 0 и формирует безопасный 400 response.

## Normalizers

- `CHTTPX_TRIM`
- `CHTTPX_LOWERCASE`
- `CHTTPX_UPPERCASE`

Можно комбинировать через OR.

## Compatibility API

Сохраняются:

- `chttpx_validation_string(...)`
- `chttpx_validation_integer(...)`
- `chttpx_validation_boolean(...)`
- `cHTTPX_Parse()`
- `cHTTPX_Validate()`

Для нового кода предпочтительнее `BindJSON`.

## Ownership

Строки и массивы после binding принадлежат request. `free(payload.email)` делать не нужно.

Если значение должно жить дольше request, скопируйте его в application-owned memory.

## JSON builder

```c
chttpx_json_t* json =
    cHTTPX_JsonObject(req);

cHTTPX_JsonString(json, "message", message);
cHTTPX_JsonNumber(json, "id", id);
cHTTPX_JsonBool(json, "active", true);

chttpx_json_t* tags =
    cHTTPX_JsonArray(req);

cHTTPX_JsonArrayString(tags, "c");
cHTTPX_JsonArrayString(tags, "http");

cHTTPX_JsonChild(json, "tags", tags);

*res = cHTTPX_ResJsonObject(
    cHTTPX_StatusOK,
    json
);
```

Поддерживаются object, array, string, number, bool, null и nested structures.

`cHTTPX_JsonEscape()` остаётся для legacy formatted JSON.
