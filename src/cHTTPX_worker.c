#include "cHTTPX_worker.h"

#include "cHTTPX_crosspltm.h"
#include "cHTTPX_utils.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    void* data;
    uint64_t queued_at_ns;
} chttpx_worker_job_t;

struct chttpx_worker_pool
{
    thread_t threads[CHTTPX_WORKER_THREADS];
    size_t threads_started;

    chttpx_worker_job_t* queue;
    size_t queue_capacity;
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;

    size_t active_workers;
    uint64_t rejected_jobs;
    uint64_t completed_jobs;
    uint64_t total_queue_wait_ns;

    bool stopping;
    chttpx_worker_execute_fn execute;
    void* context;

#ifdef CHTTPX_PLATFORM_WINDOWS
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE ready;
#else
    pthread_mutex_t mutex;
    pthread_cond_t ready;
#endif
};

static uint64_t monotonic_ns(void)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    return GetTickCount64() * 1000000ULL;
#else
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;

    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
#endif
}

static void pool_lock(chttpx_worker_pool_t* pool)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    EnterCriticalSection(&pool->mutex);
#else
    pthread_mutex_lock(&pool->mutex);
#endif
}

static void pool_unlock(chttpx_worker_pool_t* pool)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    LeaveCriticalSection(&pool->mutex);
#else
    pthread_mutex_unlock(&pool->mutex);
#endif
}

static void pool_wait(chttpx_worker_pool_t* pool)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    SleepConditionVariableCS(&pool->ready, &pool->mutex, INFINITE);
#else
    pthread_cond_wait(&pool->ready, &pool->mutex);
#endif
}

static void pool_signal(chttpx_worker_pool_t* pool)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    WakeConditionVariable(&pool->ready);
#else
    pthread_cond_signal(&pool->ready);
#endif
}

static void pool_broadcast(chttpx_worker_pool_t* pool)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    WakeAllConditionVariable(&pool->ready);
#else
    pthread_cond_broadcast(&pool->ready);
#endif
}

static int pool_sync_init(chttpx_worker_pool_t* pool)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    InitializeCriticalSection(&pool->mutex);
    InitializeConditionVariable(&pool->ready);
    return 0;
#else
    if (pthread_mutex_init(&pool->mutex, NULL) != 0)
        return -1;

    if (pthread_cond_init(&pool->ready, NULL) != 0)
    {
        pthread_mutex_destroy(&pool->mutex);
        return -1;
    }

    return 0;
#endif
}

static void pool_sync_destroy(chttpx_worker_pool_t* pool)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    DeleteCriticalSection(&pool->mutex);
#else
    pthread_cond_destroy(&pool->ready);
    pthread_mutex_destroy(&pool->mutex);
#endif
}

static bool worker_take(chttpx_worker_pool_t* pool, chttpx_worker_job_t* job)
{
    pool_lock(pool);

    while (pool->queue_count == 0 && !pool->stopping)
        pool_wait(pool);

    if (pool->queue_count == 0 && pool->stopping)
    {
        pool_unlock(pool);
        return false;
    }

    *job = pool->queue[pool->queue_head];
    memset(&pool->queue[pool->queue_head], 0, sizeof(pool->queue[pool->queue_head]));
    pool->queue_head = (pool->queue_head + 1) % pool->queue_capacity;
    pool->queue_count--;
    pool->active_workers++;

    uint64_t now = monotonic_ns();
    if (now >= job->queued_at_ns)
        pool->total_queue_wait_ns += now - job->queued_at_ns;

    pool_unlock(pool);
    return true;
}

static void* worker_main(void* argument)
{
    chttpx_worker_pool_t* pool = argument;

    for (;;)
    {
        chttpx_worker_job_t job;

        if (!worker_take(pool, &job))
            break;

        pool->execute(job.data, pool->context);

        pool_lock(pool);

        if (pool->active_workers > 0)
            pool->active_workers--;

        pool->completed_jobs++;
        pool_unlock(pool);
    }

    return NULL;
}

int _chttpx_worker_pool_create(chttpx_worker_pool_t** pool,
                               size_t queue_capacity,
                               chttpx_worker_execute_fn execute,
                               void* context)
{
    if (!pool || !queue_capacity || !execute)
        return -1;

    *pool = NULL;

    chttpx_worker_pool_t* created = calloc(1, sizeof(*created));
    if (!created)
        return -1;

    created->queue = calloc(queue_capacity, sizeof(*created->queue));
    if (!created->queue)
    {
        free(created);
        return -1;
    }

    created->queue_capacity = queue_capacity;
    created->execute = execute;
    created->context = context;

    if (pool_sync_init(created) != 0)
    {
        free(created->queue);
        free(created);
        return -1;
    }

    for (size_t i = 0; i < CHTTPX_WORKER_THREADS; i++)
    {
        if (_thread_create(&created->threads[i], worker_main, created) != 0)
        {
            _chttpx_worker_stop(created);

            for (size_t j = 0; j < created->threads_started; j++)
                _thread_join(created->threads[j]);

            pool_sync_destroy(created);
            free(created->queue);
            free(created);
            return -1;
        }

        created->threads_started++;
    }

    *pool = created;
    return 0;
}

bool _chttpx_worker_submit(chttpx_worker_pool_t* pool, void* job)
{
    if (!pool || !job)
        return false;

    pool_lock(pool);

    if (pool->stopping || pool->queue_count >= pool->queue_capacity)
    {
        pool->rejected_jobs++;
        pool_unlock(pool);
        return false;
    }

    pool->queue[pool->queue_tail].data = job;
    pool->queue[pool->queue_tail].queued_at_ns = monotonic_ns();
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_capacity;
    pool->queue_count++;

    pool_signal(pool);
    pool_unlock(pool);
    return true;
}

void _chttpx_worker_stop(chttpx_worker_pool_t* pool)
{
    if (!pool)
        return;

    pool_lock(pool);
    pool->stopping = true;
    pool_broadcast(pool);
    pool_unlock(pool);
}

void _chttpx_worker_pool_destroy(chttpx_worker_pool_t* pool)
{
    if (!pool)
        return;

    _chttpx_worker_stop(pool);

    for (size_t i = 0; i < pool->threads_started; i++)
        _thread_join(pool->threads[i]);

    pool_sync_destroy(pool);
    free(pool->queue);
    free(pool);
}

void _chttpx_worker_stats(chttpx_worker_pool_t* pool, chttpx_worker_stats_t* stats)
{
    if (!stats)
        return;

    memset(stats, 0, sizeof(*stats));

    if (!pool)
        return;

    pool_lock(pool);
    stats->queue_depth = pool->queue_count;
    stats->active_workers = pool->active_workers;
    stats->rejected_jobs = pool->rejected_jobs;
    stats->completed_jobs = pool->completed_jobs;
    stats->total_queue_wait_ns = pool->total_queue_wait_ns;
    pool_unlock(pool);
}
