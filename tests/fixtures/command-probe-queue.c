/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "azmod-data-helpers.h"
#include "../../examples/loop-command-probe/command-probe-queue.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
static struct probe_log_queue queue;
#define RECORDS 100000u
static void *produce(void *unused)
{
    (void)unused;
    for (unsigned n = 0; n < RECORDS; n++) {
        unsigned payload[4] = { n, ~n, n + 1, n + 2 };
        while (!probe_log_push(&queue, (const char *)payload, sizeof payload)) sched_yield();
    }
    return NULL;
}
static void *consume(void *unused)
{
    (void)unused;
    for (unsigned n = 0; n < RECORDS; n++) {
        char bytes[PROBE_LOG_BYTES];
        size_t length = 0;
        unsigned payload[4];
        while (!probe_log_pop(&queue, bytes, &length)) sched_yield();
        assert(length == sizeof payload);
        memcpy(payload, bytes, sizeof payload);
        assert(payload[0] == n && payload[1] == ~n && payload[2] == n + 1 && payload[3] == n + 2);
    }
    return NULL;
}
int main(void)
{
    char bytes[PROBE_LOG_BYTES] = { 0 };
    size_t length = 0;
    assert(!probe_log_pop(&queue, bytes, &length));
    for (unsigned n = 0; n < PROBE_LOG_SLOTS; ++n) assert(probe_log_push(&queue, bytes, 1));
    assert(!probe_log_push(&queue, bytes, 1));
    for (unsigned n = 0; n < PROBE_LOG_SLOTS; ++n) assert(probe_log_pop(&queue, bytes, &length) && length == 1);
    assert(!probe_log_pop(&queue, bytes, &length));
    pthread_t writer, reader;
    assert(!pthread_create(&writer, NULL, produce, NULL));
    assert(!pthread_create(&reader, NULL, consume, NULL));
    assert(!pthread_join(writer, NULL));
    assert(!pthread_join(reader, NULL));
    assert(!probe_log_pop(&queue, bytes, &length));
    return 0;
}
