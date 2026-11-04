/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>
#include <time.h>
static int test_clock(clockid_t id, struct timespec *t)
{
    (void)id;
    t->tv_sec = 1;
    t->tv_nsec = 0;
    return 0;
}
#define clock_gettime test_clock
#include "control.c"
static az_library_v1 service;
const az_library_v1 *library = &service;
static int submit_rc, malformed, immediate, acknowledgements, submissions, redraws, in_call;
static uint64_t submitted_id;
static az_command_completion_fn callback;
static void *context;
static void redraw(void) { ++redraws; }
static const az_library_v1 *get_service(uint32_t abi) { return abi == AZ_LIBRARY_ABI ? &service : NULL; }
static int caps(az_data_capabilities *out)
{
    out->version = AZ_DATA_VERSION;
    out->commands = UINT64_C(1) << AZ_CMD_LOOP_EXIT;
    return AZ_OK;
}
static int snapshot(int deck, az_deck_snapshot *out)
{
    out->version = AZ_DATA_VERSION;
    out->deck = (uint32_t)deck;
    out->valid = AZ_LIVE_NATIVE_LOAD;
    out->native_load_generation = 3;
    out->native_load_unit_generation = 4;
    out->native_load_observed_ns = 1;
    return AZ_OK;
}
static int submit(const char *id, const az_command_request *r, az_command_completion_fn fn, void *user, az_command_result *out)
{
    assert(!strcmp(id, ID));
    assert(r->expected_load_generation == 3);
    assert(r->request_id);
    assert(!in_call);
    ++in_call;
    ++submissions;
    callback = fn;
    context = user;
    submitted_id = r->request_id;
    if (submit_rc != AZ_INVALID && submit_rc != AZ_BUSY) {
        out->version = malformed ? 99 : AZ_DATA_VERSION;
        out->request_id = r->request_id;
        out->outcome = submit_rc == AZ_OK ? (immediate ? AZ_COMMAND_APPLIED : AZ_COMMAND_ACCEPTED) : AZ_COMMAND_REJECTED;
        out->reason = submit_rc;
        out->revision = immediate ? 1 : 0;
        out->observed_ns = 1;
    }
    --in_call;
    return submit_rc;
}
static int acknowledge(const char *id, uint64_t request_id)
{
    assert(!strcmp(id, ID) && request_id == submitted_id);
    assert(!in_call);
    ++in_call;
    ++acknowledgements;
    --in_call;
    return AZ_OK;
}
static void event(unsigned kind, unsigned outcome, unsigned settled)
{
    az_command_completion c = { .size = sizeof c, .version = AZ_DATA_VERSION, .kind = kind, .settled = settled, .result = { sizeof(az_command_result), AZ_DATA_VERSION, outcome, outcome == AZ_COMMAND_APPLIED ? AZ_OK : AZ_FAILED, submitted_id, outcome == AZ_COMMAND_APPLIED ? 1 : 0, 1 } };
    assert(!in_call);
    callback(&c, context);
}
static void click(void)
{
    az_event e = { .kind = AZ_EV_CLICK };
    assert(input(&e, NULL) == 1);
}
int main(void)
{
    service = (az_library_v1){ .abi = AZ_LIBRARY_ABI, .size = sizeof service, .data_capabilities = caps, .deck_snapshot = snapshot, .command_submit_async = submit, .command_acknowledge = acknowledge };
    az_core_v1 c = { .abi = AZ_MOD_ABI, .size = sizeof c, .library_service = get_service, .request_redraw = redraw };
    assert(!init(&c));
    submit_rc = AZ_INVALID;
    click();
    assert(command.phase == IDLE && !command.id && !lines[3][0]);
    submit_rc = AZ_BUSY;
    click();
    assert(command.phase == IDLE && !lines[3][0]);
    int refusals[] = { AZ_UNSUPPORTED, AZ_STALE, AZ_FAILED, AZ_LIMIT, AZ_NOT_FOUND };
    for (unsigned i = 0; i < sizeof refusals / sizeof refusals[0]; i++) {
        submit_rc = refusals[i];
        click();
        assert(command.phase == IDLE && strstr(lines[3], "refused"));
        malformed = 1;
        click();
        assert(command.phase == IDLE && !lines[3][0]);
        malformed = 0;
    }
    submit_rc = AZ_OK;
    immediate = 1;
    click();
    assert(command.phase == WAIT_TERMINAL && !acknowledgements);
    /* Capabilities/ticks are irrelevant after admission. */
    service.data_capabilities = NULL;
    event(AZ_COMMAND_COMPLETION_RELEASED, AZ_COMMAND_APPLIED, 1);
    assert(command.phase == WAIT_TERMINAL && !acknowledgements);
    az_command_completion bad = { .size = sizeof bad, .version = AZ_DATA_VERSION, .kind = AZ_COMMAND_COMPLETION_TERMINAL, .settled = 1, .result = { sizeof(az_command_result), AZ_DATA_VERSION, AZ_COMMAND_APPLIED, AZ_OK, submitted_id + 1, 1, 1 } };
    callback(&bad, context);
    assert(!acknowledgements);
    for (unsigned fault = 0; fault < 9; ++fault) {
        az_command_completion invalid = { .size = sizeof invalid, .version = AZ_DATA_VERSION, .kind = AZ_COMMAND_COMPLETION_TERMINAL, .settled = 1, .result = { sizeof(az_command_result), AZ_DATA_VERSION, AZ_COMMAND_APPLIED, AZ_OK, submitted_id, 1, 1 } };
        switch (fault) {
        case 0: invalid.size--; break;
        case 1: invalid.version++; break;
        case 2: invalid.kind = 99; break;
        case 3: invalid.settled = 2; break;
        case 4: invalid.result.size--; break;
        case 5: invalid.result.version++; break;
        case 6: invalid.result.outcome = 99; break;
        case 7: invalid.result.revision = 0; break;
        case 8: invalid.result.observed_ns = 0; break;
        }
        callback(&invalid, context);
        assert(!acknowledgements && command.phase == WAIT_TERMINAL);
    }
    event(AZ_COMMAND_COMPLETION_TERMINAL, AZ_COMMAND_ACCEPTED, 1);
    assert(!acknowledgements);
    event(AZ_COMMAND_COMPLETION_TERMINAL, AZ_COMMAND_APPLIED, 0);
    assert(!acknowledgements);
    event(AZ_COMMAND_COMPLETION_TERMINAL, AZ_COMMAND_APPLIED, 1);
    assert(command.phase == WAIT_RELEASED && acknowledgements == 1);
    event(AZ_COMMAND_COMPLETION_TERMINAL, AZ_COMMAND_APPLIED, 1);
    assert(acknowledgements == 1 && command.phase == WAIT_RELEASED);
    event(AZ_COMMAND_COMPLETION_RELEASED, AZ_COMMAND_UNCERTAIN, 1);
    assert(command.phase == WAIT_RELEASED);
    event(AZ_COMMAND_COMPLETION_RELEASED, AZ_COMMAND_APPLIED, 1);
    assert(command.phase == IDLE && !command.id);
    service.data_capabilities = caps;
    for (unsigned outcome = AZ_COMMAND_REJECTED; outcome <= AZ_COMMAND_UNCERTAIN; outcome++) {
        immediate = 0;
        malformed = 1;
        click();
        assert(command.phase == WAIT_TERMINAL && command.id == submitted_id);
        event(AZ_COMMAND_COMPLETION_TERMINAL, outcome, outcome == AZ_COMMAND_UNCERTAIN ? 0 : 1);
        assert(command.phase == WAIT_RELEASED);
        event(AZ_COMMAND_COMPLETION_RELEASED, outcome, 1);
        assert(command.phase == IDLE);
    }
    assert(acknowledgements == 4);
    int before = submissions;
    service.size = offsetof(az_library_v1, command_submit_async);
    click();
    assert(status == AZ_UNSUPPORTED && submissions == before);
    return 0;
}
