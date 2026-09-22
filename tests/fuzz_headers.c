#include "cHTTPX_headers.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!data || size == 0 || size > 32768)
        return 0;

    char* buffer = malloc(size + 1);
    if (!buffer)
        return 0;

    memcpy(buffer, data, size);
    buffer[size] = '\0';

    chttpx_request_t request;
    memset(&request, 0, sizeof(request));

    _parse_req_headers(&request, buffer, size);

    free(buffer);
    return 0;
}
