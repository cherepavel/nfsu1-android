#ifndef NV2A_FENCE_WAIT_H
#define NV2A_FENCE_WAIT_H
#include <pthread.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    unsigned waiters;
} Nv2aFenceWait;
#define NV2A_FENCE_WAIT_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0}

/* Same unsigned subtraction as D3D_BlockOnTime, including sequence wrap. */
static inline int nv2a_fence_pending(uint32_t submitted, uint32_t delta, uint32_t completed)
{
    return delta < (uint32_t)(submitted - completed);
}

static inline void nv2a_fence_publish(Nv2aFenceWait *wait, uint32_t *word, uint32_t value)
{
    pthread_mutex_lock(&wait->mutex);
    __atomic_store_n(word, value, __ATOMIC_RELEASE);
    if (wait->waiters) pthread_cond_broadcast(&wait->changed);
    pthread_mutex_unlock(&wait->mutex);
}

static inline void nv2a_fence_wait(Nv2aFenceWait *wait, uint32_t *word,
    uint32_t submitted, uint32_t delta, long timeout_ns)
{
    struct timespec deadline;
#ifdef __ANDROID__
    clock_gettime(CLOCK_MONOTONIC, &deadline);
#else
    clock_gettime(CLOCK_REALTIME, &deadline);
#endif
    deadline.tv_nsec += timeout_ns;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++; deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&wait->mutex);
    wait->waiters++;
    while (nv2a_fence_pending(submitted, delta,
                __atomic_load_n(word, __ATOMIC_ACQUIRE))) {
#ifdef __ANDROID__
        if (pthread_cond_clockwait(&wait->changed, &wait->mutex, CLOCK_MONOTONIC, &deadline)) break;
#else
        if (pthread_cond_timedwait(&wait->changed, &wait->mutex, &deadline)) break;
#endif
    }
    wait->waiters--;
    pthread_mutex_unlock(&wait->mutex);
    /* A bounded wait lets the original loop re-read its word and service
     * its existing yield points, even on shutdown or an unnotified write. */
}
#endif
