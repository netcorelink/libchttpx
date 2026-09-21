#ifndef CHTTPX_RUNTIME_H
#define CHTTPX_RUNTIME_H

#include "cHTTPX_serv.h"

int _chttpx_runtime_init(chttpx_serv_t* server);
void _chttpx_runtime_listen(chttpx_serv_t* server);
void _chttpx_runtime_request_stop(chttpx_serv_t* server);
void _chttpx_runtime_cleanup(chttpx_serv_t* server);

#endif
