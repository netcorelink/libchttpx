#ifndef CHTTPX_WORKER_H
#define CHTTPX_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHTTPX_WORKER_THREADS 32

typedef struct chttpx_worker_pool chttpx_worker_pool_t;
typedef void (*chttpx_worker_execute_fn)(void* job, void* context);

typedef struct
{
    size_t queue_depth;
    size_t active_workers;
    uint64_t rejected_jobs;
    uint64_t completed_jobs;
    uint64_t total_queue_wait_ns;
} chttpx_worker_stats_t;

int _chttpx_worker_pool_create(chttpx_worker_pool_t** pool,
                               size_t queue_capacity,
                               chttpx_worker_execute_fn execute,
                               void* context);
bool _chttpx_worker_submit(chttpx_worker_pool_t* pool, void* job);
void _chttpx_worker_stop(chttpx_worker_pool_t* pool);
void _chttpx_worker_pool_destroy(chttpx_worker_pool_t* pool);
void _chttpx_worker_stats(chttpx_worker_pool_t* pool, chttpx_worker_stats_t* stats);

#endif
