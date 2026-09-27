#ifndef CHTTPX_RUNTIME_H
#define CHTTPX_RUNTIME_H

#include "cHTTPX_serv.h"
#include "cHTTPX_worker.h"

/**
 * Create event loop and worker pool for a server.
 *
 * @param server Target server.
 * @return Zero on success or a negative error code.
 */
int _chttpx_runtime_init(chttpx_serv_t* server);

/**
 * Run the accept/read/write event loop until stop is requested.
 *
 * @param server Target server.
 */
void _chttpx_runtime_listen(chttpx_serv_t* server);

/**
 * Request graceful runtime shutdown.
 *
 * @param server Target server.
 */
void _chttpx_runtime_request_stop(chttpx_serv_t* server);

/**
 * Tear down runtime resources after listen returns.
 *
 * @param server Target server.
 */
void _chttpx_runtime_cleanup(chttpx_serv_t* server);

/**
 * Copy worker pool statistics for a server.
 *
 * @param server Target server.
 * @param stats Output stats buffer.
 * @return Zero on success or a negative error code.
 */
int _chttpx_runtime_worker_stats(chttpx_serv_t* server, chttpx_worker_stats_t* stats);

#endif
