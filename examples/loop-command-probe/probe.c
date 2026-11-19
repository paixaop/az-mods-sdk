/* SPDX-License-Identifier: GPL-3.0-only */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include "azmod-timestamps.h"
#include "azmod-data-helpers.h"
#include "command-probe-queue.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define ID "sdk-loop-command-probe"
#ifndef PROBE_MAX_TICKS
#define PROBE_MAX_TICKS 36000u
#endif
#define PROBE_MAX_RECORDS 4096u
struct probe_input {
    uint64_t pid, id, load, deadline;
    uint32_t deck, op;
    double position, end, beats;
    char action[9];
};
enum {
    PLAN_NONE,
    PLAN_REFUSALS,
    PLAN_MATRIX
};
struct probe_plan {
    int kind, phase, stopping;
    unsigned index, count, completed;
    uint64_t pid, first_id, load, overall_deadline, next_at, step_started, observe_deadline;
    uint64_t before_unit, before_revision;
    struct probe_input current;
};
struct probe_state {
    const az_core_v1 *core;
    const az_library_v1 *library;
    uint64_t highwater, sequence, commands;
    struct probe_input active;
    az_command_result last;
    int pending, terminal, last_rc, acknowledged;
    atomic_int io_failed;
    struct probe_plan plan;
};
static struct probe_state probe;
static struct probe_log_queue probe_logs;
/* A dropped post_ui job has no command ownership and no promised result. The
 * context remains valid even if the worker exits before delivery. */
static struct {
    atomic_int busy;
    struct probe_input value;
    int valid;
} probe_job;
static void probe_enqueue(const char *line, size_t length)
{
    if (!probe_log_push(&probe_logs, line, length)) atomic_store(&probe.io_failed, 1);
}
static uint64_t probe_now(void)
{
#ifdef PROBE_TEST_NOW
    return PROBE_TEST_NOW();
#else
    struct timespec ts = { 0 };
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    const int seconds = compare_timestamps(ts.tv_sec, 0, 1, 1000000000, 1);
    const int nanos = compare_timestamps(ts.tv_nsec, 1000000000L, 1000000000, 1000000000, 1);
    if (seconds == AZ_TIME_INVALID || seconds == AZ_TIME_BEFORE || nanos == AZ_TIME_INVALID || nanos != AZ_TIME_BEFORE) return 0;
    uint64_t sec = (uint64_t)ts.tv_sec;
    const int range = compare_timestamps(sec, (UINT64_MAX - (uint64_t)ts.tv_nsec) / UINT64_C(1000000000), 1, 1000000000, 1);
    return range != AZ_TIME_INVALID && range <= AZ_TIME_EQUAL
               ? sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec
               : 0;
#endif
}
static int probe_integer(const char *s, uint64_t *out)
{
    if (!*s || (s[0] == '0' && s[1])) return 0;
    uint64_t value = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9' || value > (UINT64_MAX - (unsigned)(*s - '0')) / 10) return 0;
        value = value * 10 + (unsigned)(*s - '0');
    }
    *out = value;
    return 1;
}
static int probe_number(const char *s, double *out)
{
    /* No whitespace, hexadecimal, NaN or infinity spellings in the transport. */
    if (!*s || strspn(s, "0123456789.eE+-") != strlen(s)) return 0;
    char *end;
    errno = 0;
    double value = strtod(s, &end);
    if (*end || errno || !isfinite(value)) return 0;
    *out = value;
    return 1;
}
static int probe_field(char **at, const char *key, char **value, int last)
{
    size_t length = strlen(key);
    if (strncmp(*at, key, length)) return 0;
    *value = *at + length;
    char *space = strchr(*value, ' ');
    if (last) return space == NULL;
    if (!space || space == *value) return 0;
    *space = 0;
    *at = space + 1;
    return 1;
}
static int probe_fields(const char *line, size_t length, char *copy,
                        const char *const *keys, char **values, unsigned count)
{
    if (!line || !length || length >= 512 || line[length - 1] != '\n' || memchr(line, 0, length)) return 0;
    memcpy(copy, line, length - 1);
    copy[length - 1] = 0;
    char *at = copy;
    for (unsigned i = 0; i < count; ++i)
        if (!probe_field(&at, keys[i], &values[i], i + 1 == count)) return 0;
    return 1;
}
static int probe_action(const char *action)
{
    static const char *const actions[] = { "submit", "cancel", "snapshot", "refusals", "matrix" };
    for (unsigned i = 0; i < sizeof actions / sizeof *actions; ++i)
        if (!strcmp(action, actions[i])) return 1;
    return 0;
}

static int probe_integers(char *const values[11], struct probe_input *v)
{
    static const unsigned indices[] = { 0, 1, 3, 4, 5, 6, 7 };
    uint64_t version, deck, op;
    uint64_t *const integers[] = { &version, &v->pid, &v->id, &deck, &op, &v->load, &v->deadline };
    for (unsigned i = 0; i < sizeof indices / sizeof *indices; ++i)
        if (!probe_integer(values[indices[i]], integers[i])) return 0;
    if (version != 1 || deck > 3 || op > 12) return 0;
    v->deck = (uint32_t)deck;
    v->op = (uint32_t)op;
    return 1;
}

static int probe_parse(const char *line, size_t n, struct probe_input *out)
{
    static const char *keys[] = { "v=", "pid=", "action=", "id=", "deck=", "op=", "load=", "deadline_ns=", "position=", "end=", "beats=" };
    if (!out) return 0;
    char copy[512], *value[11];
    if (!probe_fields(line, n, copy, keys, value, 11)) return 0;
    struct probe_input v = { 0 };
    if (!probe_integers(value, &v) || !probe_action(value[2]) ||
        !probe_number(value[8], &v.position) || !probe_number(value[9], &v.end) ||
        !probe_number(value[10], &v.beats)) return 0;
    memcpy(v.action, value[2], strlen(value[2]) + 1);
    *out = v;
    return 1;
}
static int probe_directory_valid(const char *directory)
{
    return directory && directory[0] == '/' && strlen(directory) <= 300 && !strpbrk(directory, " \t\r\n");
}

static int probe_results_path(char *path, size_t capacity)
{
    const char *directory = probe.core->data_dir(ID);
    if (!probe_directory_valid(directory)) return 0;
    int n = snprintf(path, capacity, "%s/results.%ld.log", directory, (long)getpid());
    return n > 0 && (size_t)n < capacity;
}
static int probe_result_file_valid(int fd, size_t length)
{
    struct stat st;
    return fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size >= 0 &&
           length <= 1024 && st.st_size <= (off_t)(2u * 1024u * 1024u - length);
}

static void probe_write_record(const char *line, size_t length)
{
    char path[512];
    int fd = -1;
    int okay = probe_results_path(path, sizeof path);
    if (okay) fd = open(path, O_WRONLY | O_APPEND | O_NOFOLLOW | O_NONBLOCK);
    okay = okay && probe_result_file_valid(fd, length);
    if (okay) okay = write(fd, line, length) == (ssize_t)length;
    if (fd >= 0 && close(fd)) okay = 0;
    if (!okay && !probe.io_failed) {
        probe.io_failed = 1;
        probe.core->log(ID, "SDK_LOOP_IO error=results_file");
    }
}
struct probe_observation {
    az_deck_snapshot snapshot;
    uint64_t query;
    int load_valid, engaged_valid, bounds_valid, engaged;
    double in_s, out_s;
};
static void probe_normalize_observation(struct probe_observation *o)
{
    if (!o->load_valid) {
        o->snapshot.native_load_generation = 0;
        o->snapshot.native_load_unit_generation = 0;
        o->snapshot.native_load_observed_ns = 0;
    }
    if (!o->engaged_valid && !o->bounds_valid) {
        o->snapshot.loop_unit_generation = 0;
        o->snapshot.loop_revision = 0;
        o->snapshot.loop_observed_ns = 0;
    }
    if (!o->engaged_valid) o->engaged = -1;
    if (!o->bounds_valid) o->in_s = o->out_s = 0;
}
static struct probe_observation probe_observe(unsigned deck)
{
    struct probe_observation o = { .snapshot.size = sizeof o.snapshot, .engaged = -1 };
    int status = probe.library->deck_snapshot((int)deck, &o.snapshot);
    o.query = probe_now();
    uint64_t generation = 0;
    if (status == AZ_OK && o.snapshot.size >= sizeof o.snapshot && o.snapshot.version == AZ_DATA_VERSION && o.snapshot.deck == deck) {
        o.load_valid = az_read_native_load_target(&o.snapshot, deck, o.query, &generation);
        o.engaged_valid = az_read_loop_engaged(&o.snapshot, deck, o.query, &o.engaged);
        o.bounds_valid = az_read_loop_bounds(&o.snapshot, deck, o.query, &o.in_s, &o.out_s);
    }
    probe_normalize_observation(&o);
    return o;
}

static void probe_emit(const char *event, const struct probe_input *input, int rc, const az_command_result *result)
{
    if (probe.sequence >= PROBE_MAX_RECORDS) return;
    struct probe_observation o = probe_observe(input->deck);
    az_deck_snapshot snapshot = o.snapshot;
    uint64_t query = o.query;
    int load_valid = o.load_valid, engaged_valid = o.engaged_valid, bounds_valid = o.bounds_valid;
    az_command_result value = { 0 };
    int outcome = -1;
    if (result) {
        value = *result;
        outcome = (int)result->outcome;
    }
    char line[1024];
    int length = snprintf(line, sizeof line, "SDK_LOOP v=1 seq=%llu event=%s id=%llu deck=%u op=%u target_load=%llu rc=%d outcome=%d reason=%d revision=%llu receipt_ns=%llu query_ns=%llu load_valid=%d native_load=%llu load_unit=%llu load_ns=%llu engaged_valid=%d engaged=%d bounds_valid=%d in=%.17g out=%.17g loop_unit=%llu loop_rev=%llu loop_ns=%llu\n",
                          (unsigned long long)++probe.sequence, event, (unsigned long long)input->id, input->deck, input->op,
                          (unsigned long long)input->load, rc, outcome, value.reason,
                          (unsigned long long)(value.revision), (unsigned long long)(value.observed_ns),
                          (unsigned long long)query, !!load_valid, (unsigned long long)(snapshot.native_load_generation),
                          (unsigned long long)(snapshot.native_load_unit_generation),
                          (unsigned long long)(snapshot.native_load_observed_ns), !!engaged_valid,
                          o.engaged, !!bounds_valid, o.in_s, o.out_s,
                          (unsigned long long)(snapshot.loop_unit_generation),
                          (unsigned long long)(snapshot.loop_revision),
                          (unsigned long long)(snapshot.loop_observed_ns));
    if (length > 0 && (size_t)length < sizeof line) probe_enqueue(line, (size_t)length);
    else if (!probe.io_failed) {
        probe.io_failed = 1;
        probe.core->log(ID, "SDK_LOOP_IO error=format");
    }
}
static int probe_result_valid(const az_command_result *value, uint64_t id)
{
    return az_command_result_matches(value, id);
}
static int probe_released(const az_command_completion *c)
{
    return c->kind == AZ_COMMAND_COMPLETION_RELEASED && c->settled == 1 &&
           probe.terminal && probe.acknowledged && probe_terminal_equal(&probe.last, &c->result);
}

static void probe_complete(const az_command_completion *c, void *user)
{
    if (user != &probe || !probe.pending || !probe_completion_valid(c, probe.active.id)) {
        probe.core->log(ID, "SDK_LOOP_PROTOCOL invalid completion; context retained");
        return;
    }
    if (c->kind == AZ_COMMAND_COMPLETION_TERMINAL && !probe.terminal) {
        probe.last = c->result;
        probe.terminal = 1;
        probe_emit("terminal", &probe.active, AZ_OK, &probe.last);
        int rc = probe.library->command_acknowledge(ID, probe.active.id);
        probe.acknowledged = rc == AZ_OK;
        probe_emit("acknowledge", &probe.active, rc, &probe.last);
    } else if (probe_released(c)) {
        probe_emit("released", &probe.active, AZ_OK, &probe.last);
        probe.pending = probe.terminal = probe.acknowledged = 0;
        memset(&probe.active, 0, sizeof probe.active);
    } else probe.core->log(ID, "SDK_LOOP_PROTOCOL out-of-order completion; context retained");
}
static int probe_submit_result_valid(int status, const az_command_result *result, uint64_t id)
{
    return status != AZ_INVALID && status != AZ_BUSY && probe_result_valid(result, id);
}

static int probe_submit(const struct probe_input *v)
{
    az_data_capabilities caps = { .size = sizeof caps };
    int rc = probe.library->data_capabilities(&caps);
    if (rc != AZ_OK || caps.size < sizeof caps || caps.version != AZ_DATA_VERSION || !(caps.commands & (UINT64_C(1) << v->op))) {
        probe_emit("refused", v, AZ_UNSUPPORTED, NULL);
        return AZ_UNSUPPORTED;
    }
    az_command_request request = { .size = sizeof request, .version = AZ_DATA_VERSION, .operation = v->op, .deck = v->deck, .request_id = v->id, .expected_load_generation = v->load, .deadline_ns = v->deadline, .quantization = AZ_QUANTIZE_NATIVE, .flags = AZ_COMMAND_NATIVE_LOAD, .position_s = v->position, .end_s = v->end, .beats = v->beats };
    az_command_result result = { .size = sizeof result };
    memset(&probe.last, 0, sizeof probe.last);
    rc = probe.library->command_submit_async(ID, &request, probe_complete, &probe, &result);
    probe_emit("submit", v, rc, probe_submit_result_valid(rc, &result, v->id) ? &result : NULL);
    if (probe_submit_result_valid(rc, &result, v->id)) probe.last = result;
    if (rc == AZ_OK) {
        probe.active = *v;
        probe.last = result;
        probe.pending = 1;
        probe.terminal = probe.acknowledged = 0;
        probe.last_rc = AZ_OK;
    }
    return rc;
}
static int probe_plan_start(const struct probe_input *v);
static int probe_command_empty(const struct probe_input *v)
{
    return !v->op && !v->load && !v->deadline && !v->position && !v->end && !v->beats;
}
static int probe_refuse(const struct probe_input *v, int status)
{
    probe_emit("refused", v, status, NULL);
    return status;
}
static int probe_snapshot_action(const struct probe_input *v)
{
    if (v->id || !probe_command_empty(v)) return probe_refuse(v, AZ_INVALID);
    probe_emit("snapshot", v, AZ_OK, NULL);
    return AZ_OK;
}
static int probe_cancel_result_valid(int rc, const az_command_result *result, uint64_t id)
{
    return (rc == AZ_OK || rc == AZ_PENDING || rc == AZ_STALE) && probe_result_valid(result, id);
}

static int probe_cancel_action(const struct probe_input *v)
{
    if (!probe.pending || v->id != probe.active.id || v->deck != probe.active.deck || !probe_command_empty(v)) return probe_refuse(v, AZ_INVALID);
    if (probe.plan.kind) probe.plan.stopping = 1;
    az_command_result result = { .size = sizeof result };
    int rc = probe.library->command_cancel(ID, v->id, &result);
    int valid = probe_cancel_result_valid(rc, &result, v->id);
    probe_emit("cancel", &probe.active, rc, valid ? &result : NULL);
    return rc;
}
static int probe_deadline_valid(uint64_t deadline, uint64_t now)
{
    const int order = compare_timestamps(deadline, now, 1000000000, 1000000000, 1);
    if (order == AZ_TIME_INVALID || order <= AZ_TIME_EQUAL) return 0;
    const int window = compare_timestamps(deadline - now, UINT64_C(30000000000), 1000000000, 1000000000, 1);
    return window != AZ_TIME_INVALID && window <= AZ_TIME_EQUAL;
}
static int probe_plan_payload_valid(const struct probe_input *v, int kind)
{
    return !v->position && !v->end && !v->beats && (kind != PLAN_REFUSALS || v->load == UINT64_MAX);
}

static int probe_plan_input_valid(const struct probe_input *v, int kind)
{
    if (!v->id || v->id > UINT64_MAX - 15 || v->deck >= 4 || v->op || !v->load) return 0;
    return probe_plan_payload_valid(v, kind);
}
static int probe_plan_clock_valid(const struct probe_input *v, uint64_t now)
{
    const int range = compare_timestamps(now, UINT64_MAX - UINT64_C(180000000000), 1000000000, 1000000000, 1);
    return range != AZ_TIME_INVALID && range <= AZ_TIME_EQUAL && probe_deadline_valid(v->deadline, now);
}

static int probe_submission_valid(const struct probe_input *v, uint64_t now)
{
    return !strcmp(v->action, "submit") && v->id && v->op && v->load && probe_deadline_valid(v->deadline, now);
}
static int probe_submit_action(const struct probe_input *v, uint64_t now)
{
    if (probe.plan.kind) return probe_refuse(v, AZ_BUSY);
    if (!probe_submission_valid(v, now)) return probe_refuse(v, AZ_INVALID);
    if (v->id <= probe.highwater) return probe_refuse(v, AZ_STALE);
    probe.highwater = v->id;
    if (probe.pending) return probe_refuse(v, AZ_BUSY);
    return probe_submit(v);
}

static int probe_handle(const struct probe_input *v)
{
    uint64_t now = probe_now();
    if (v->pid != (uint64_t)getpid() || !now) return probe_refuse(v, AZ_STALE);
    if (!strcmp(v->action, "snapshot")) return probe_snapshot_action(v);
    if (!strcmp(v->action, "cancel")) return probe_cancel_action(v);
    if (!strcmp(v->action, "refusals") || !strcmp(v->action, "matrix")) return probe_plan_start(v);
    return probe_submit_action(v, now);
}

static void probe_plan_record(const char *event, const char *status)
{
    char line[512];
    int n = snprintf(line, sizeof line,
                     "SDK_LOOP_PLAN v=1 event=%s kind=%s id=%llu deck=%u load=%llu status=%s completed=%u query_ns=%llu deadline_ns=%llu\n",
                     event, probe.plan.kind == PLAN_REFUSALS ? "refusals" : "matrix",
                     (unsigned long long)probe.plan.first_id, probe.plan.current.deck, (unsigned long long)probe.plan.load,
                     status, probe.plan.completed, (unsigned long long)probe_now(), (unsigned long long)probe.plan.overall_deadline);
    if (n > 0 && (size_t)n < sizeof line) probe_enqueue(line, (size_t)n);
}
static void probe_plan_finish(const char *status)
{
    if (!probe.plan.kind) return;
    probe_plan_record("done", status);
    probe.plan.kind = PLAN_NONE;
}
static int probe_snapshot(uint32_t deck, az_deck_snapshot *snapshot, uint64_t now)
{
    memset(snapshot, 0, sizeof *snapshot);
    snapshot->size = sizeof *snapshot;
    int rc = probe.library->deck_snapshot((int)deck, snapshot);
    uint64_t query = probe_now();
    uint64_t generation = 0;
    const int order = compare_timestamps(query, now, 1000000000, 1000000000, 1);
    return order != AZ_TIME_INVALID && order >= AZ_TIME_EQUAL && rc == AZ_OK && az_read_native_load_target(snapshot, deck, query, &generation);
}
static void probe_plan_snapshot(void)
{
    struct probe_input snapshot = { .deck = probe.plan.current.deck };
    probe_emit("snapshot", &snapshot, AZ_OK, NULL);
}
static void probe_plan_cancel(void)
{
    if (probe.pending && !probe.terminal) {
        az_command_result result = { .size = sizeof result };
        int rc = probe.library->command_cancel(ID, probe.active.id, &result);
        probe_emit("cancel", &probe.active, rc, probe_result_valid(&result, probe.active.id) ? &result : NULL);
    }
}
static int probe_plan_start(const struct probe_input *v)
{
    uint64_t now = probe_now();
    int kind = !strcmp(v->action, "refusals") ? PLAN_REFUSALS : PLAN_MATRIX;
    unsigned count = kind == PLAN_REFUSALS ? 12u : 15u;
    if (probe.plan.kind || probe.pending) {
        probe_emit("refused", v, AZ_BUSY, NULL);
        return AZ_BUSY;
    }
    if (!probe_plan_input_valid(v, kind) || !probe_plan_clock_valid(v, now)) return probe_refuse(v, AZ_INVALID);
    if (v->id <= probe.highwater) {
        probe_emit("refused", v, AZ_STALE, NULL);
        return AZ_STALE;
    }
    probe.highwater = v->id + count - 1; /* Never reuse even unissued reserved plan IDs. */
    memset(&probe.plan, 0, sizeof probe.plan);
    probe.plan.kind = kind;
    probe.plan.phase = 1;
    probe.plan.count = count;
    probe.plan.pid = v->pid;
    probe.plan.first_id = v->id;
    probe.plan.load = v->load;
    probe.plan.current = *v;
    probe.plan.overall_deadline = now + UINT64_C(180000000000);
    probe_plan_record("start", "INCOMPLETE");
    return AZ_OK;
}
static void probe_plan_arguments(struct probe_input *v)
{
    static const unsigned matrix[15] = { AZ_CMD_LOOP_EXIT, AZ_CMD_LOOP_IN, AZ_CMD_LOOP_OUT, AZ_CMD_LOOP_DOUBLE,
                                         AZ_CMD_LOOP_HALVE, AZ_CMD_LOOP_LENGTH, AZ_CMD_LOOP_ADJUST_IN, AZ_CMD_LOOP_ADJUST_OUT, AZ_CMD_LOOP_MOVE,
                                         AZ_CMD_LOOP_EXIT, AZ_CMD_RELOOP, AZ_CMD_LOOP_EXIT, AZ_CMD_LOOP_ENTER, AZ_CMD_LOOP_EXIT, AZ_CMD_LOOP_CLEAR };
    memset(v, 0, sizeof *v);
    v->pid = probe.plan.pid;
    v->id = probe.plan.first_id + probe.plan.index;
    v->deck = probe.plan.current.deck;
    v->load = probe.plan.load;
    v->op = probe.plan.kind == PLAN_REFUSALS ? probe.plan.index + 1 : matrix[probe.plan.index];
    strcpy(v->action, "submit");
    if (v->op == AZ_CMD_LOOP_LENGTH) v->beats = 8;
    if (v->op == AZ_CMD_LOOP_MOVE || v->op == AZ_CMD_LOOP_ADJUST_IN) v->position = .05;
    if (v->op == AZ_CMD_LOOP_ADJUST_OUT) v->end = .05;
}
static void probe_plan_advance(uint64_t now)
{
    unsigned operation = probe.plan.current.op;
    probe.plan.completed++;
    probe.plan.index++;
    if (probe.plan.index == probe.plan.count) {
        probe_plan_finish("PASS");
        return;
    }
    probe.plan.phase = 1;
    probe.plan.next_at = now + (operation == AZ_CMD_LOOP_IN && probe.plan.kind == PLAN_MATRIX ? UINT64_C(1000000000) : 0);
}
static int probe_plan_observation_fresh(const az_deck_snapshot *snapshot)
{
    const int start = compare_timestamps(snapshot->loop_observed_ns, probe.plan.step_started, 1000000000, 1000000000, 1);
    if (start == AZ_TIME_INVALID || start < AZ_TIME_EQUAL) return 0;
    const int end = compare_timestamps(snapshot->loop_observed_ns, probe_now(), 1000000000, 1000000000, 1);
    return end != AZ_TIME_INVALID && end <= AZ_TIME_EQUAL;
}
static int probe_plan_observation_valid(const az_deck_snapshot *snapshot)
{
    return (snapshot->valid & AZ_LIVE_LOOP_ENGAGED) && (snapshot->loop.valid & AZ_LOOP_ENGAGED) &&
           snapshot->loop_unit_generation && snapshot->loop_revision > probe.plan.before_revision &&
           (!probe.plan.before_unit || snapshot->loop_unit_generation == probe.plan.before_unit) &&
           probe_plan_observation_fresh(snapshot);
}
static int probe_plan_visible(const az_deck_snapshot *snapshot, unsigned op)
{
    if (!probe_plan_observation_valid(snapshot)) return 0;
    if (op == AZ_CMD_LOOP_EXIT) return snapshot->loop.engaged == 0;
    return snapshot->loop.engaged == 1 && (snapshot->valid & AZ_LIVE_LOOP_BOUNDS) &&
           (snapshot->loop.valid & AZ_LOOP_BOUNDS) && isfinite(snapshot->loop.in_s) && isfinite(snapshot->loop.out_s);
}
static int probe_plan_terminal(uint64_t now)
{
    if (probe.pending) return 0;
    if (probe.plan.stopping) {
        probe_plan_finish("INCOMPLETE");
        return 0;
    }
    /* Any retained request during refusal testing is unexpected, even if
     * subsequently cancelled or applied. Drain it without issuing later work. */
    if (probe.plan.kind == PLAN_REFUSALS || !probe_result_valid(&probe.last, probe.plan.current.id) ||
        probe.last.outcome != AZ_COMMAND_APPLIED) {
        probe_plan_finish("FAIL");
        return 0;
    }
    probe.plan.phase = 3;
    probe.plan.observe_deadline = now + UINT64_C(5000000000);
    return 1;
}
static void probe_plan_observe(uint64_t now)
{
    az_deck_snapshot snapshot;
    int loaded = probe_snapshot(probe.plan.current.deck, &snapshot, now);
    probe_plan_snapshot();
    if (!loaded || snapshot.native_load_generation != probe.plan.load) {
        probe_plan_finish("INCOMPLETE");
        return;
    }
    unsigned op = probe.plan.current.op;
    if (op == AZ_CMD_LOOP_IN || op == AZ_CMD_LOOP_CLEAR || probe.plan.index == 0) {
        probe_plan_advance(now);
        return;
    }
    int visible = probe_plan_visible(&snapshot, op);
    if (visible) {
        probe_plan_advance(now);
        return;
    }
    if (compare_timestamps(now, probe.plan.observe_deadline, 1000000000, 1000000000, 1) >= AZ_TIME_EQUAL) probe_plan_finish("INCOMPLETE");
    return;
}

static int probe_plan_refusal_valid(int rc, uint64_t id)
{
    return probe.plan.kind == PLAN_REFUSALS && rc == AZ_STALE && probe_result_valid(&probe.last, id) &&
           probe.last.outcome == AZ_COMMAND_REJECTED && probe.last.reason == AZ_STALE && !probe.last.revision && !probe.pending;
}
static int probe_plan_prepare(struct probe_input *request, uint64_t now)
{
    probe_plan_arguments(request);
    az_deck_snapshot before;
    int loaded = probe_snapshot(request->deck, &before, now);
    probe_plan_snapshot();
    if (probe.plan.kind == PLAN_MATRIX && (!loaded || before.native_load_generation != probe.plan.load)) {
        probe_plan_finish("INCOMPLETE");
        return 0;
    }
    probe.plan.before_unit = before.loop_unit_generation;
    probe.plan.before_revision = before.loop_revision;
    uint64_t dispatch = probe_now();
    const int range = compare_timestamps(dispatch, UINT64_MAX - UINT64_C(10000000000), 1000000000, 1000000000, 1);
    if (!dispatch || compare_timestamps(dispatch, probe.plan.overall_deadline, 1000000000, 1000000000, 1) >= AZ_TIME_EQUAL ||
        range == AZ_TIME_INVALID || range == AZ_TIME_AFTER) {
        probe_plan_finish("INCOMPLETE");
        return 0;
    }
    probe.plan.step_started = dispatch;
    request->deadline = dispatch + UINT64_C(10000000000);
    probe.plan.current = *request;
    return 1;
}
static void probe_plan_submitted(int rc, uint64_t id, uint64_t now)
{
    if (probe.io_failed) {
        probe.plan.kind = PLAN_NONE;
        return;
    }
    if (probe_plan_refusal_valid(rc, id)) {
        probe_plan_snapshot();
        probe_plan_advance(now);
        return;
    }
    if (probe.pending) {
        probe.plan.phase = 2;
        return;
    }
    probe_plan_finish("FAIL");
}
static void probe_plan_dispatch(uint64_t now)
{
    if (probe.plan.phase != 1 || compare_timestamps(now, probe.plan.next_at, 1000000000, 1000000000, 1) == AZ_TIME_BEFORE || probe.pending) return;
    if (probe.plan.stopping) {
        probe_plan_finish("INCOMPLETE");
        return;
    }
    struct probe_input request;
    if (!probe_plan_prepare(&request, now)) return;
    int rc = probe_submit(&request);
    probe_plan_submitted(rc, request.id, now);
}

static int probe_plan_expired(uint64_t now)
{
    const int deadline = compare_timestamps(now, probe.plan.overall_deadline, 1000000000, 1000000000, 1);
    return !now || deadline == AZ_TIME_INVALID || deadline >= AZ_TIME_EQUAL || probe.plan.pid != (uint64_t)getpid();
}

static void probe_plan_step(void)
{
    if (!probe.plan.kind) return;
    uint64_t now = probe_now();
    if (probe.io_failed) {
        probe.plan.kind = PLAN_NONE;
        return;
    }
    if (probe_plan_expired(now)) {
        probe_plan_cancel();
        probe_plan_finish("INCOMPLETE");
        return;
    }
    if (probe.plan.stopping && !probe.pending) {
        probe_plan_finish("INCOMPLETE");
        return;
    }
    if (probe.plan.phase == 2 && !probe_plan_terminal(now)) return;
    if (probe.plan.phase == 3) probe_plan_observe(now);
    else probe_plan_dispatch(now);
}

static void probe_ready(const char *directory)
{
    probe.core->log(ID, "SDK_LOOP_READY v=1 pid=%ld commands=%llu data=%s", (long)getpid(), (unsigned long long)probe.commands, directory);
}
static void probe_step(void)
{
    probe_plan_step();
    if (probe.io_failed) return;
    const char *directory = probe.core->data_dir(ID);
    if (!probe_directory_valid(directory)) return;
    az_data_capabilities caps = { .size = sizeof caps };
    if (probe.library->data_capabilities(&caps) == AZ_OK && caps.version == AZ_DATA_VERSION && caps.commands != probe.commands) {
        probe.commands = caps.commands;
        probe_ready(directory);
    }
}
static void probe_run_job(void *user)
{
    if (user != &probe_job) return;
    if (probe_job.valid && !strcmp(probe_job.value.action, "submit")) {
        az_deck_snapshot s = { .size = sizeof s };
        uint64_t generation = 0;
        int rc = probe.library->deck_snapshot((int)probe_job.value.deck, &s);
        if (rc != AZ_OK || !az_read_native_load_target(&s, probe_job.value.deck, probe_now(), &generation) || generation != probe_job.value.load) {
            probe_emit("refused", &probe_job.value, rc == AZ_OK ? AZ_STALE : rc, NULL);
            atomic_store_explicit(&probe_job.busy, 0, memory_order_release);
            return;
        }
    }
    if (probe_job.valid) (void)probe_handle(&probe_job.value);
    else probe_emit("refused", &probe_job.value, AZ_INVALID, NULL);
    atomic_store_explicit(&probe_job.busy, 0, memory_order_release);
}
static void probe_flush(void)
{
    char line[PROBE_LOG_BYTES];
    size_t length;
    while (probe_log_pop(&probe_logs, line, &length)) probe_write_record(line, length);
}
static int probe_claim_request(const char *directory, char claimed[1024])
{
    char request[1024];
    int a = snprintf(request, sizeof request, "%s/request", directory);
    int b = snprintf(claimed, 1024, "%s/request.processing", directory);
    return a >= 0 && (size_t)a < sizeof request && b >= 0 && b < 1024 && !rename(request, claimed);
}
static int probe_request_file_valid(int fd)
{
    struct stat st;
    return fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() && st.st_size > 0 && st.st_size < 512;
}
static ssize_t probe_read_request(const char *claimed, char line[512])
{
    int fd = open(claimed, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    ssize_t n = -1;
    if (probe_request_file_valid(fd)) n = read(fd, line, 512);
    if (fd >= 0) close(fd);
    unlink(claimed);
    return n;
}

static void probe_worker_step(void)
{
    probe_flush();
    if (atomic_load_explicit(&probe_job.busy, memory_order_acquire) || atomic_load(&probe.io_failed)) return;
    const char *directory = probe.core->data_dir(ID);
    if (!probe_directory_valid(directory)) return;
    char claimed[1024], line[512];
    if (!probe_claim_request(directory, claimed)) return;
    ssize_t n = probe_read_request(claimed, line);
    memset(&probe_job.value, 0, sizeof probe_job.value);
    probe_job.valid = n > 0 && probe_parse(line, (size_t)n, &probe_job.value);
    atomic_store_explicit(&probe_job.busy, 1, memory_order_release);
    probe.core->post_ui(ID, probe_run_job, &probe_job);
}
static void *probe_worker(void *unused)
{
    (void)unused;
    for (unsigned tick = 0; tick < PROBE_MAX_TICKS; ++tick) {
        probe_worker_step();
        struct timespec delay = { 0, 50000000 };
        if (nanosleep(&delay, NULL) && errno != EINTR) break;
    }
    probe_flush();
    probe.core->log(ID, "SDK_LOOP_DONE; unreleased callback and scheduling contexts retained");
    return NULL;
}
static int probe_core_valid(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI || !AZ_HAS(core, log) || !AZ_HAS(core, data_dir) ||
        !AZ_HAS(core, thread_start) || !AZ_HAS(core, library_service) || !AZ_HAS(core, post_ui)) return 0;
    return 1;
}
static int probe_library_valid(const az_library_v1 *lib)
{
    if (!AZ_LIB_HAS(lib, data_capabilities) || !AZ_LIB_HAS(lib, deck_snapshot) || !AZ_LIB_HAS(lib, command_submit_async) ||
        !AZ_LIB_HAS(lib, command_acknowledge) || !AZ_LIB_HAS(lib, command_cancel)) return 0;
    return 1;
}

static int probe_create_results(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) return 0;
    struct stat st;
    int okay = !fstat(fd, &st) && S_ISREG(st.st_mode);
    if (close(fd)) okay = 0;
    if (!okay) return 0;
    return 1;
}

static int init(const az_core_v1 *core)
{
    if (!probe_core_valid(core)) return -1;
    const az_library_v1 *lib = core->library_service(AZ_LIBRARY_ABI);
    if (!probe_library_valid(lib)) return -1;
    az_data_capabilities caps = { .size = sizeof caps };
    if (lib->data_capabilities(&caps) != AZ_OK) return -1;
    memset(&probe, 0, sizeof probe);
    atomic_init(&probe.io_failed, 0);
    atomic_init(&probe_job.busy, 0);
    atomic_init(&probe_logs.written, 0);
    atomic_init(&probe_logs.consumed, 0);
    probe.core = core;
    probe.library = lib;
    probe.commands = caps.commands;
    const char *directory = core->data_dir(ID);
    if (!probe_directory_valid(directory)) return -1;
    char path[512];
    if (!probe_results_path(path, sizeof path)) return -1;
    if (!probe_create_results(path)) return -1;
    probe_ready(directory);
    return core->thread_start(ID, "sdk-loop-command", probe_worker, NULL) == 0 ? 0 : -1;
}
static const char *const tags[] = { "any", NULL };
static const az_mod_v1 mod = { .abi = AZ_MOD_ABI, .size = sizeof mod, .id = ID, .fw_tags = tags, .init = init, .tick = probe_step };
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
