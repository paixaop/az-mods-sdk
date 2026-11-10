/* SPDX-License-Identifier: GPL-3.0-only */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#include "../../examples/cue-command-probe/probe.c"
static az_library_v1 service;
static az_core_v1 host;
static char directory[128];
static int submits, acks, submit_status, malformed, drop, clock_bad;
static az_command_completion_fn callback;
static void *context;
static az_command_request submitted;
static const uint32_t source_rate_hz = 48000;
static void (*job)(void *);
static void *job_user;
static void log_line(const char *id, const char *format, ...)
{
    (void)format;
    assert(!strcmp(id, ID));
}
static const char *data_dir(const char *id)
{
    assert(!strcmp(id, ID));
    return directory;
}
static const az_library_v1 *library(uint32_t abi) { return abi == AZ_LIBRARY_ABI ? &service : NULL; }
static int caps(az_data_capabilities *out)
{
    out->version = AZ_DATA_VERSION;
    out->commands = (UINT64_C(1) << (AZ_CMD_CUE_LOOP_SET + 1)) - 2;
    return AZ_OK;
}
static int snapshot(int deck, az_deck_snapshot *out)
{
    out->version = AZ_DATA_VERSION;
    out->deck = (unsigned)deck;
    out->valid = AZ_LIVE_NATIVE_LOAD;
    out->native_load_generation = 42;
    out->native_load_unit_generation = 3;
    out->native_load_observed_ns = clock_bad ? UINT64_MAX : 1;
    return AZ_OK;
}
static int native_cues(int deck, az_native_cue_snapshot *out)
{
    out->version = AZ_DATA_VERSION;
    out->deck = (unsigned)deck;
    out->count = 0;
    out->source_rate_hz = source_rate_hz;
    return AZ_OK;
}
static int submit(const char *id, const az_command_request *r, az_command_completion_fn fn, void *user, az_command_result *out)
{
    assert(!strcmp(id, ID));
    ++submits;
    submitted = *r;
    callback = fn;
    context = user;
    assert(r->size == sizeof *r && r->request_id && r->expected_load_generation == 42);
    if (submit_status != AZ_INVALID && submit_status != AZ_BUSY) {
        out->version = malformed ? 99 : AZ_DATA_VERSION;
        out->request_id = r->request_id;
        out->outcome = submit_status == AZ_OK ? AZ_COMMAND_APPLIED : AZ_COMMAND_REJECTED;
        out->reason = submit_status;
        out->revision = submit_status == AZ_OK ? 2 : 0;
        out->observed_ns = 1;
    }
    return submit_status;
}
static int ack(const char *id, uint64_t rid)
{
    assert(!strcmp(id, ID) && rid == submitted.request_id);
    ++acks;
    return AZ_OK;
}
static int forbidden_poll(const char *id, uint64_t rid, az_command_result *out)
{
    (void)id;
    (void)rid;
    (void)out;
    assert(!"command polling is forbidden");
    return AZ_FAILED;
}
static int forbidden_release(const char *id, uint64_t rid)
{
    (void)id;
    (void)rid;
    assert(!"legacy release is forbidden");
    return AZ_FAILED;
}
static int forbidden_event(const char *id, uint64_t after, az_data_change *out)
{
    (void)id;
    (void)after;
    (void)out;
    assert(!"command journal polling is forbidden");
    return AZ_FAILED;
}
static int start(const char *id, const char *name, void *(*worker)(void *), void *arg)
{
    assert(!strcmp(id, ID) && !strcmp(name, "sdk-cue-command") && worker == cue_worker && !arg);
    return AZ_OK;
}
static void post(const char *id, void (*fn)(void *), void *user)
{
    assert(!strcmp(id, ID));
    if (drop) return;
    assert(!job);
    job = fn;
    job_user = user;
}
static void dispatch(void)
{
    assert(job);
    void (*fn)(void *) = job;
    job = NULL;
    fn(job_user);
}
static struct cue_input request(uint64_t id)
{
    struct cue_input r = { .pid = (uint64_t)getpid(), .id = id, .deck = 0, .op = AZ_CMD_HOT_SET, .load = 42, .revision = 7, .timeout = UINT64_C(5000000000), .flags = AZ_COMMAND_NATIVE_LOAD | AZ_COMMAND_NATIVE_CUE_IDS | AZ_COMMAND_PERSIST, .classification = AZ_CUE_HOT, .entry = 1 };
    strcpy(r.action, "submit");
    return r;
}
static void notify(unsigned kind, unsigned outcome, unsigned settled)
{
    az_command_completion c = { .size = sizeof c, .version = AZ_DATA_VERSION, .kind = kind, .settled = settled, .result = { sizeof(az_command_result), AZ_DATA_VERSION, outcome, outcome == AZ_COMMAND_APPLIED ? AZ_OK : AZ_FAILED, submitted.request_id, outcome == AZ_COMMAND_APPLIED ? 2 : 0, 1 } };
    callback(&c, context);
}
int main(void)
{
    strcpy(directory, "/tmp/cue-async.XXXXXX");
    assert(mkdtemp(directory));
    service = (az_library_v1){ .abi = AZ_LIBRARY_ABI, .size = sizeof service, .data_capabilities = caps, .deck_snapshot = snapshot, .native_cues = native_cues, .command_submit_async = submit, .command_acknowledge = ack, .command_cancel = forbidden_poll, .command_poll = forbidden_poll, .command_release = forbidden_release, .data_event = forbidden_event };
    host = (az_core_v1){ .abi = AZ_MOD_ABI, .size = sizeof host, .log = log_line, .data_dir = data_dir, .library_service = library, .thread_start = start, .post_ui = post };
    assert(!init(&host));
    for (unsigned outcome = AZ_COMMAND_APPLIED; outcome <= AZ_COMMAND_UNCERTAIN; outcome++) {
        struct cue_input r = request(outcome);
        malformed = outcome == AZ_COMMAND_UNCERTAIN;
        assert(cue_handle(&r) == AZ_OK && cue.pending && !cue.terminal);
        /* No tick, query or worker progress is required for either callback. */
        notify(AZ_COMMAND_COMPLETION_RELEASED, outcome, 1);
        assert(cue.pending && !cue.terminal);
        notify(AZ_COMMAND_COMPLETION_TERMINAL, AZ_COMMAND_ACCEPTED, 1);
        assert(!cue.terminal);
        notify(AZ_COMMAND_COMPLETION_TERMINAL, outcome, outcome == AZ_COMMAND_UNCERTAIN ? 0 : 1);
        assert(cue.pending && cue.terminal && cue.acknowledged);
        notify(AZ_COMMAND_COMPLETION_TERMINAL, outcome, 1);
        assert(acks == (int)outcome);
        notify(AZ_COMMAND_COMPLETION_RELEASED, outcome, 1);
        assert(!cue.pending);
    }
    assert(acks == 4);
    malformed = 0;
    int statuses[] = { AZ_INVALID, AZ_BUSY, AZ_UNSUPPORTED, AZ_STALE, AZ_FAILED, AZ_LIMIT, AZ_NOT_FOUND };
    for (unsigned i = 0; i < sizeof statuses / sizeof statuses[0]; i++) {
        struct cue_input r = request(20 + i);
        submit_status = statuses[i];
        assert(cue_handle(&r) == submit_status && !cue.pending);
    }
    submit_status = AZ_OK;
    /* Parse and submit both appended creation commands, then reject the first
     * unknown operation before it reaches the public service. */
    for (unsigned op = AZ_CMD_CUE_POINT_SET; op <= AZ_CMD_CUE_LOOP_SET; ++op) {
        char input[512];
        int length = snprintf(input, sizeof input, "v=1 pid=%ld action=submit id=%u deck=0 op=%u load=42 revision=7 timeout_ns=5000000000 flags=7 classification=1 entry_id=2 position=1.25 end=%s beats=0 rgb=1193046 comment_hex=437565\n", (long)getpid(), 50 + op, op, op == AZ_CMD_CUE_LOOP_SET ? "3.75" : "0");
        struct cue_input r;
        assert(length > 0 && (size_t)length < sizeof input && cue_parse(input, (size_t)length, &r));
        assert(r.op == op);
        int before = submits;
        assert(cue_handle(&r) == AZ_OK && cue.pending && submits == before + 1);
        assert(submitted.operation == op && submitted.flags == 7 && submitted.classification == AZ_CUE_HOT && submitted.entry_id == 2 && !strcmp(submitted.comment, "Cue"));
        assert(compare_timestamps(submitted.position_s, 1.25, 1, source_rate_hz, 1) == AZ_TIME_EQUAL);
        assert(compare_timestamps(submitted.end_s, op == AZ_CMD_CUE_LOOP_SET ? 3.75 : 0., 1, source_rate_hz, 1) == AZ_TIME_EQUAL);
        notify(AZ_COMMAND_COMPLETION_TERMINAL, AZ_COMMAND_APPLIED, 1);
        notify(AZ_COMMAND_COMPLETION_RELEASED, AZ_COMMAND_APPLIED, 1);
        assert(!cue.pending);
    }
    struct cue_input unsupported = request(90);
    unsupported.op = AZ_CMD_CUE_LOOP_SET + 1;
    int creation_submits = submits;
    assert(cue_handle(&unsupported) == AZ_INVALID && submits == creation_submits && !cue.pending);
    cue_flush();
    /* Best-effort handoff: a dropped job establishes no command ownership. */
    atomic_store(&cue.ui_ready, 1);
    drop = 1;
    char path[256], line[1024];
    snprintf(path, sizeof path, "%s/request", directory);
    int n = snprintf(line, sizeof line, "v=1 pid=%ld action=submit id=100 deck=0 op=21 load=42 revision=7 timeout_ns=5000000000 flags=7 classification=1 entry_id=1 position=0 end=0 beats=0 rgb=0 comment_hex=-\n", (long)getpid());
    FILE *f = fopen(path, "w");
    assert(f);
    assert(fwrite(line, 1, (size_t)n, f) == (size_t)n);
    assert(!fclose(f));
    int before = submits;
    uint64_t high = cue.highwater;
    cue_worker_step();
    assert(atomic_load(&cue_job.busy) && !job && !cue.pending && cue.highwater == high && submits == before);
    /* Simulate delivery of a separate request using the same stable scheduling
     * context. Failed target validation precedes actual submission. */
    cue_job.value = request(101);
    cue_job.valid = 1;
    clock_bad = 1;
    cue_run_job(&cue_job);
    assert(submits == before && !cue.pending && !atomic_load(&cue_job.busy));
    clock_bad = 0;
    drop = 0;
    cue_job.value = request(102);
    cue_job.valid = 1;
    atomic_store(&cue_job.busy, 1);
    post(ID, cue_run_job, &cue_job);
    dispatch();
    assert(submits == before + 1 && cue.pending);
    notify(AZ_COMMAND_COMPLETION_TERMINAL, AZ_COMMAND_APPLIED, 1);
    notify(AZ_COMMAND_COMPLETION_RELEASED, AZ_COMMAND_APPLIED, 1);
    cue_flush();
    assert(!cue.pending);
    service.size = offsetof(az_library_v1, command_submit_async);
    assert(init(&host) == -1);
    snprintf(path, sizeof path, "%s/results.%ld.log", directory, (long)getpid());
    assert(!unlink(path));
    assert(!rmdir(directory));
    puts("PASS async cue command client: lifecycle, refusals, malformed replies, UI handoff and no command polling");
    return 0;
}
