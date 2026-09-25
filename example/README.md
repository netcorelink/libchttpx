# libchttpx examples

Each file is a standalone example using only the public header:

```c
#include <libchttpx.h>
```

Build all examples on Linux:

```sh
make examples
```

Generated binaries are placed in `.build/`:

- `example-basic`
- `example-multiple_servers`
- `example-local_call`
- `example-remote_call`
- `example-middleware`
- `example-json`
- `example-upload`
- `example-metrics`
- `example-sse`

`example/sse_client.html` is a browser `EventSource` client for `example-sse`.

The normal `make lin` / `make win` demo target uses `example/basic.c`.
