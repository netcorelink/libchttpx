#ifndef CHTTPX_RUNTIME_H
#define CHTTPX_RUNTIME_H

#include "cHTTPX_serv.h"
#include "cHTTPX_worker.h"

int _chttpx_runtime_init(chttpx_serv_t* server);
void _chttpx_runtime_listen(chttpx_serv_t* server);
void _chttpx_runtime_request_stop(chttpx_serv_t* server);
void _chttpx_runtime_cleanup(chttpx_serv_t* server);
int _chttpx_runtime_worker_stats(chttpx_serv_t* server, chttpx_worker_stats_t* stats);

#endif
