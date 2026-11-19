/* SPDX-License-Identifier: GPL-3.0-only */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
static uint64_t virtual_ns = UINT64_C(1000000000000);
static uint64_t virtual_clock(void) { return virtual_ns; }
#define CUE_TEST_NOW virtual_clock
#define nanosleep fixture_sleep
#define main async_fixture_main
#include "cue-command-async.c"
#undef main
#undef nanosleep
static unsigned sleeps;
static int interrupt_sleep;
static int cancel_reply;
static int cancel_request(const char *id, uint64_t request_id, az_command_result *out)
{
    assert(!strcmp(id, ID) && request_id == submitted.request_id);
    *out = (az_command_result){ .size = sizeof *out, .version = AZ_DATA_VERSION, .request_id = request_id, .outcome = AZ_COMMAND_CANCELLED, .reason = AZ_CANCELLED };
    return cancel_reply;
}
int fixture_sleep(const struct timespec *delay, struct timespec *remaining)
{
    assert(!remaining && compare_timestamps(delay->tv_nsec, 50000000, 1000000000, 1000000000, 1) == AZ_TIME_EQUAL);
    ++sleeps;
    errno = interrupt_sleep ? EINTR : EIO;
    if (interrupt_sleep) virtual_ns += CUE_LIFETIME_NS;
    return -1;
}
static void clock_limits(void)
{
    assert(!cue_worker_begin(0));
    assert(!cue_worker_begin(UINT64_MAX));
    assert(cue_worker_begin(virtual_ns));
    assert(cue_worker_continue(virtual_ns));
    assert(!cue_worker_continue(virtual_ns - 1));
    assert(!cue_worker_continue(virtual_ns + CUE_IDLE_NS));
    cue.pending = 1;
    assert(cue_worker_continue(virtual_ns + CUE_IDLE_NS));
    uint64_t deadline = cue.worker_drain_deadline_ns;
    cue_note_activity(virtual_ns + CUE_IDLE_NS);
    assert(compare_timestamps(cue.worker_drain_deadline_ns, deadline, 1000000000, 1000000000, 1) == AZ_TIME_EQUAL);
    assert(!cue_worker_continue(deadline));
    cue.pending = 0;
    cue.worker_started_ns = 0;
    cue.worker_drain_deadline_ns = 0;
    cue_note_activity(virtual_ns);
    assert(cue_worker_continue(virtual_ns));
    cue_note_activity(0);
    assert(cue.io_failed && !cue_worker_continue(virtual_ns));
    cue.io_failed = 0;
    assert(cue_worker_begin(virtual_ns));
    cue.sequence = 8192;
    assert(!cue_worker_continue(virtual_ns));
    cue.sequence = 0;
    assert(!cue_worker_continue(virtual_ns + CUE_LIFETIME_NS));
    cue.worker_started_ns = UINT64_MAX - 100;
    cue.pending = 1;
    assert(!cue_worker_continue(UINT64_MAX - 50));
    cue.pending = 0;
    assert(cue_worker_begin(virtual_ns));
}
static void transport_records(void)
{
    char encoded[257], decoded[128];
    cue_hex("Cue \xc3\xa9", encoded);
    assert(!strcmp(encoded, "43756520c3a9") && cue_comment(encoded, decoded));
    assert(!strcmp(decoded, "Cue \xc3\xa9"));
    cue_hex("", encoded);
    assert(!strcmp(encoded, "-") && cue_comment(encoded, decoded) && !decoded[0]);
    assert(!cue_comment("00", decoded) && !cue_comment("ff0", decoded) && !cue_comment("gg", decoded));
    struct cue_input v = { .pid = (uint64_t)getpid() };
    strcpy(v.action, "snapshot");
    assert(cue_handle(&v) == AZ_OK);
    v.rgb = 1;
    assert(cue_handle(&v) == AZ_INVALID);
    v.rgb = 0;
    strcpy(v.action, "cancel");
    assert(cue_handle(&v) == AZ_INVALID);
    v.pid = 0;
    assert(cue_handle(&v) == AZ_STALE);
    az_native_cue_snapshot snapshot = { .size = sizeof snapshot, .version = AZ_DATA_VERSION, .native_load_generation = 42, .revision = 7, .count = 1 };
    snapshot.entries[0] = (az_cue_entry){ .valid = AZ_CUE_START | AZ_CUE_END, .start_s = 1.25, .end_s = 2.5 };
    strcpy(snapshot.entries[0].comment, "Cue");
    cue_emit_entries(2, 99, &snapshot);
    char line[PROBE_LOG_BYTES];
    size_t length;
    int found = 0;
    while (probe_log_pop(&cue_logs, line, &length)) {
        assert(length < sizeof line);
        line[length] = 0;
        if (strstr(line, "SDK_CUE_ENTRY")) {
            assert(strstr(line, "snapshot_seq=99 deck=2 load=42 revision=7"));
            assert(strstr(line, "start=1.25 end=2.5") && strstr(line, "comment_hex=437565"));
            ++found;
        }
    }
    assert(found == 1);
    struct cue_input command = request(200);
    assert(cue_handle(&command) == AZ_OK && cue.pending);
    service.command_cancel = cancel_request;
    struct cue_input cancel = { .pid = (uint64_t)getpid(), .id = 200 };
    strcpy(cancel.action, "cancel");
    const int replies[] = { AZ_OK, AZ_PENDING, AZ_STALE, AZ_INVALID };
    for (unsigned i = 0; i < sizeof replies / sizeof *replies; ++i) {
        cancel_reply = replies[i];
        assert(cue_handle(&cancel) == cancel_reply && cue.pending);
    }
    notify(AZ_COMMAND_COMPLETION_TERMINAL, AZ_COMMAND_CANCELLED, 1);
    notify(AZ_COMMAND_COMPLETION_RELEASED, AZ_COMMAND_CANCELLED, 1);
    assert(!cue.pending);
    cue_flush();
}
int main(void)
{
    assert(async_fixture_main() == 0);
    service.size = sizeof service;
    strcpy(directory, "/tmp/cue-lifetime.XXXXXX");
    assert(mkdtemp(directory) && !init(&host));
    clock_limits();
    transport_records();
    cue_tick();
    assert(atomic_load(&cue.ui_ready) && !atomic_load(&cue.worker_stop));
    cue.commands = 0;
    cue_step();
    assert(cue.commands);
    cue_worker(NULL);
    assert(sleeps == 1);
    interrupt_sleep = 1;
    cue_worker(NULL);
    assert(sleeps == 2); /* Interrupted sleep still respects the absolute lifetime. */
    virtual_ns = 0;
    cue_worker(NULL);
    assert(sleeps == 2);
    char path[256];
    snprintf(path, sizeof path, "%s/results.%ld.log", directory, (long)getpid());
    assert(!unlink(path) && !rmdir(directory));
    puts("PASS cue command lifetime: bounded shutdown, transport records and empty action guards");
    return 0;
}
