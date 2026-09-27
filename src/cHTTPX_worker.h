#ifndef CHTTPX_WORKER_H
#define CHTTPX_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHTTPX_WORKER_THREADS 32

/**
 * Opaque worker thread pool handle.
 */
typedef struct chttpx_worker_pool chttpx_worker_pool_t;

/**
 * Worker pool job callback.
 *
 * @param job Opaque job pointer submitted to the pool.
 * @param context User context registered at pool creation.
 */
typedef void (*chttpx_worker_execute_fn)(void* job, void* context);

/**
 * Snapshot of worker pool queue and counters.
 */
typedef struct
{
    size_t queue_depth;
    size_t active_workers;
    uint64_t rejected_jobs;
    uint64_t completed_jobs;
    uint64_t total_queue_wait_ns;
} chttpx_worker_stats_t;

/**
 * Create a worker pool.
 *
 * @return Zero on success or -1 on failure.
 */
int _chttpx_worker_pool_create(chttpx_worker_pool_t** pool, size_t queue_capacity, chttpx_worker_execute_fn execute, void* context);

/** Enqueue one job. @return False when stopping or the queue is full. */
bool _chttpx_worker_submit(chttpx_worker_pool_t* pool, void* job);

/** Signal workers to stop after draining the queue. */
void _chttpx_worker_stop(chttpx_worker_pool_t* pool);

/** Stop workers, join threads, and free the pool. */
void _chttpx_worker_pool_destroy(chttpx_worker_pool_t* pool);

/** Snapshot queue depth and worker counters. */
void _chttpx_worker_stats(chttpx_worker_pool_t* pool, chttpx_worker_stats_t* stats);

#endif
