/* cc -std=c11 -O2 -Wall -Wextra -Werror -pthread
 * -fsanitize=address,undefined tests/nv2a_fence_wait.c -o /tmp/nfsu-fence-test */
#define _POSIX_C_SOURCE 200809L
#include "../xboxrecomp/src/kernel/nv2a_fence_wait.h"
#include <assert.h>
#include <sched.h>
#include <stdio.h>

static Nv2aFenceWait event = NV2A_FENCE_WAIT_INITIALIZER;
static uint32_t completed;
static unsigned stop, notified;
static void *publisher(void *unused)
{
    (void)unused;
    while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) {
        pthread_mutex_lock(&event.mutex);
        unsigned waiters = event.waiters;
        pthread_mutex_unlock(&event.mutex);
        if (waiters) {
            /* Wakes and partial progress must not satisfy a later fence. */
            nv2a_fence_publish(&event, &completed, 26);
            nv2a_fence_publish(&event, &completed, 27);
            __atomic_store_n(&notified, 1, __ATOMIC_RELEASE);
            break;
        }
        sched_yield();
    }
    return NULL;
}
static void *waiter(void *unused)
{
    (void)unused;
    nv2a_fence_wait(&event, &completed, 32, 32-27, 500000000L);
    assert(__atomic_load_n(&completed, __ATOMIC_ACQUIRE) == 27);
    return NULL;
}
int main(void)
{
    assert(nv2a_fence_pending(32,32-27,26));
    assert(!nv2a_fence_pending(32,32-27,27));
    assert(!nv2a_fence_pending(32,32-27,32));
    assert(nv2a_fence_pending(0x10,0x10-3,0xFFFFFFF0));
    assert(!nv2a_fence_pending(0x10,0x10-3,3));
    assert(!nv2a_fence_pending(0x10,0x10-3,0x10));
    /* A signal before entering wait must not be lost. */
    nv2a_fence_publish(&event, &completed, 27);
    nv2a_fence_wait(&event, &completed, 32, 32-27, 1000000L);
    assert(!event.waiters);
    for (unsigned trial=0;trial<128;trial++) {
        nv2a_fence_publish(&event, &completed, 0);
        __atomic_store_n(&stop,0,__ATOMIC_RELEASE);
        __atomic_store_n(&notified,0,__ATOMIC_RELEASE);
        pthread_t thread;
        assert(!pthread_create(&thread,NULL,publisher,NULL));
        nv2a_fence_wait(&event, &completed, 32, 32-27, 500000000L);
        __atomic_store_n(&stop,1,__ATOMIC_RELEASE);
        assert(!pthread_join(thread,NULL));
        assert(__atomic_load_n(&notified,__ATOMIC_ACQUIRE));
        assert(completed==27 && !event.waiters);
    }
    /* One completion wakes all interested waiters. */
    nv2a_fence_publish(&event,&completed,0);
    pthread_t threads[4];
    for(unsigned i=0;i<4;i++) assert(!pthread_create(&threads[i],NULL,waiter,NULL));
    nv2a_fence_publish(&event,&completed,27);
    for(unsigned i=0;i<4;i++) assert(!pthread_join(threads[i],NULL));
    /* Missing notifications remain bounded; caller rechecks the original loop. */
    nv2a_fence_publish(&event,&completed,0);
    nv2a_fence_wait(&event,&completed,32,32-27,1000000L);
    assert(completed==0 && !event.waiters);
    puts("fence event wait: completion, wrap, notify-before-wait, races, multiple waiters and timeout passed");
    return 0;
}
