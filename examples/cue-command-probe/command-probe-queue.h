/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef COMMAND_PROBE_QUEUE_H
#define COMMAND_PROBE_QUEUE_H
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
/* One serialized UI producer, one file worker. No allocation or file operations
 * in command callbacks. An overflow stops diagnostics without dropping command
 * acknowledgement or invalidating process-lifetime callback context. */
#define PROBE_LOG_SLOTS 64u
#define PROBE_LOG_BYTES 2048u
struct probe_log_queue {
    atomic_uint written, consumed;
    struct {
        size_t length;
        char bytes[PROBE_LOG_BYTES];
    } slots[PROBE_LOG_SLOTS];
};
static inline int probe_log_push(struct probe_log_queue *q, const char *bytes, size_t length)
{
    unsigned written = atomic_load_explicit(&q->written, memory_order_relaxed);
    unsigned consumed = atomic_load_explicit(&q->consumed, memory_order_acquire);
    if (written - consumed >= PROBE_LOG_SLOTS || length > PROBE_LOG_BYTES) return 0;
    unsigned at = written % PROBE_LOG_SLOTS;
    q->slots[at].length = length;
    memcpy(q->slots[at].bytes, bytes, length);
    atomic_store_explicit(&q->written, written + 1, memory_order_release);
    return 1;
}
static inline int probe_log_pop(struct probe_log_queue *q, char bytes[PROBE_LOG_BYTES], size_t *length)
{
    unsigned consumed = atomic_load_explicit(&q->consumed, memory_order_relaxed);
    if (consumed == atomic_load_explicit(&q->written, memory_order_acquire)) return 0;
    unsigned at = consumed % PROBE_LOG_SLOTS;
    *length = q->slots[at].length;
    memcpy(bytes, q->slots[at].bytes, *length);
    atomic_store_explicit(&q->consumed, consumed + 1, memory_order_release);
    return 1;
}
/* Notification validity is separate from synchronous call status/ownership. */
static inline int probe_completion_valid(const az_command_completion *c, uint64_t id)
{
    return az_data_covers(c, sizeof *c) && c->version == AZ_DATA_VERSION && c->settled <= 1 &&
           (c->kind == AZ_COMMAND_COMPLETION_TERMINAL || c->kind == AZ_COMMAND_COMPLETION_RELEASED) &&
           az_command_result_matches(&c->result, id) && c->result.outcome != AZ_COMMAND_ACCEPTED &&
           (c->result.outcome == AZ_COMMAND_UNCERTAIN || c->settled == 1) &&
           (c->result.outcome != AZ_COMMAND_APPLIED ||
            (c->result.reason == AZ_OK && c->result.revision && c->result.observed_ns));
}
static inline int probe_terminal_equal(const az_command_result *a, const az_command_result *b)
{
    return a->request_id == b->request_id && a->outcome == b->outcome && a->reason == b->reason &&
           a->revision == b->revision && a->observed_ns == b->observed_ns;
}
#endif
