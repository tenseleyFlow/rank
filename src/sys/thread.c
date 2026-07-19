#include "sys/thread.h"

#include <pthread.h>
#include <stdatomic.h>

enum {
    RANK_MAX_WORKERS = 64
};

struct rank_task_state {
    rank_task_fn fn;
    void *ctx;
    size_t count;
    _Atomic size_t next;
};

static void *rank_task_worker(void *arg);

void
rank_run_tasks(rank_task_fn fn, void *ctx, size_t task_count, size_t threads)
{
    struct rank_task_state state;
    pthread_t workers[RANK_MAX_WORKERS];
    size_t spawned = 0;
    size_t want;
    size_t i;

    if (task_count == 0) {
        return;
    }
    state.fn = fn;
    state.ctx = ctx;
    state.count = task_count;
    atomic_init(&state.next, 0);

    if (threads > RANK_MAX_WORKERS) {
        threads = RANK_MAX_WORKERS;
    }
    want = threads < task_count ? threads : task_count;
    for (i = 1; i < want; i++) {
        if (pthread_create(&workers[spawned], NULL, rank_task_worker, &state) != 0) {
            break;
        }
        spawned++;
    }
    (void)rank_task_worker(&state);
    for (i = 0; i < spawned; i++) {
        (void)pthread_join(workers[i], NULL);
    }
}

static void *
rank_task_worker(void *arg)
{
    struct rank_task_state *state = arg;

    for (;;) {
        size_t index = atomic_fetch_add(&state->next, 1);

        if (index >= state->count) {
            break;
        }
        state->fn(state->ctx, index);
    }
    return NULL;
}
