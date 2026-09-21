# JSON binding, validation, and builder

## Bind a JSON request

`cHTTPX_BindJSON()` parses, normalizes, validates, and stores fields.

```c
typedef struct
{
    char* email;
    char* username;
} create_user_t;

static bool validate_username(
    const void* value,
    char* error,
    size_t error_size)
{
    const char* username = value;

    if (strlen(username) >= 3)
        return true;

    snprintf(error, error_size, "username is too short");
    return false;
}

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
            cHTTPX_TRIM | cHTTPX_LOWERCASE,
            NULL
        ),
        cHTTPX_StringField(
            "username",
            &payload.username,
            true,
            3,
            32,
            cHTTPX_TRIM,
            validate_username
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

On failure, `BindJSON` returns 0 and builds a safe HTTP 400 response.

## Normalizers

- `cHTTPX_TRIM`
- `cHTTPX_LOWERCASE`
- `cHTTPX_UPPERCASE`

They can be OR-combined.

## Compatibility validation macros

- `chttpx_validation_string(...)`
- `chttpx_validation_integer(...)`
- `chttpx_validation_boolean(...)`

`cHTTPX_Parse()` and `cHTTPX_Validate()` remain for compatibility; prefer `BindJSON` for new handlers.

## Ownership

Bound strings/arrays are request-owned. Do not free them. Copy data if it must survive request completion.

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

Supported values: object, array, string, number, bool, null, and nested object/array.

`cHTTPX_JsonEscape(req, value)` is available for compatibility formatted JSON and returns request-owned memory.
