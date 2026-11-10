/* SPDX-License-Identifier: GPL-3.0-only */
#include "pages.h"
#include "azmod-data-helpers.h"
#include <stdio.h>
#include <time.h>
#define ID LIBRARY_INSPECTOR_ID
static const az_core_v1 *core;
static int status = AZ_PENDING, attempted, protocol_error;
/* Static context survives page switches and suppressed retirement notifications. */
static struct {
    enum { IDLE,
           WAIT_TERMINAL,
           WAIT_RELEASED } phase;
    uint64_t id;
    az_command_result terminal;
    int terminal_seen;
} command;
static char lines[8][112];
static int supported(void)
{
    return AZ_LIB_HAS(library, command_submit_async) && library->command_submit_async &&
           AZ_LIB_HAS(library, command_acknowledge) && library->command_acknowledge &&
           AZ_LIB_HAS(library, data_capabilities) && library->data_capabilities &&
           AZ_LIB_HAS(library, deck_snapshot) && library->deck_snapshot;
}
static void draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    if (area.w < 32 || area.h < 32) return;
    uint32_t color = core->color(AZ_COL_TEXT);
    int y = area.y + 8;
    for (int i = 0; i < 8 && y + 24 < area.y + area.h; i++, y += 28)
        if (core->text_width(lines[i], 16) < area.w - 16)
            core->draw_text(d, area.x + 8, y, lines[i], 16, color);
}
static int same_result(const az_command_result *a, const az_command_result *b)
{
    int observed = compare_timestamps(a->observed_ns, b->observed_ns,
                                      UINT64_C(1000000000), UINT64_C(1000000000), 1);
    if (observed == AZ_TIME_INVALID) return 0;
    return a->request_id == b->request_id && a->outcome == b->outcome && a->reason == b->reason &&
           a->revision == b->revision && observed == AZ_TIME_EQUAL;
}
static void complete(const az_command_completion *c, void *user)
{
    if (user != &command || !az_data_covers(c, sizeof *c) || c->version != AZ_DATA_VERSION ||
        c->settled > 1 || !az_command_result_matches(&c->result, command.id) ||
        c->result.outcome == AZ_COMMAND_ACCEPTED ||
        (c->result.outcome != AZ_COMMAND_UNCERTAIN && !c->settled) ||
        (c->result.outcome == AZ_COMMAND_APPLIED &&
         (c->result.reason != AZ_OK || !c->result.revision || !c->result.observed_ns))) {
        protocol_error = 1;
    } else if (c->kind == AZ_COMMAND_COMPLETION_TERMINAL && command.phase == WAIT_TERMINAL && !command.terminal_seen) {
        command.terminal = c->result;
        command.terminal_seen = 1;
        snprintf(lines[3], sizeof lines[3], "Request %llu: outcome %u, reason %d",
                 (unsigned long long)command.id, c->result.outcome, c->result.reason);
        int rc = library->command_acknowledge(ID, command.id);
        if (rc == AZ_OK) {
            command.phase = WAIT_RELEASED;
            snprintf(lines[5], sizeof lines[5], "%s", c->settled ? "Acknowledged; cleanup pending" : "Acknowledged; native settlement and cleanup pending");
        } else {
            protocol_error = 1;
            snprintf(lines[5], sizeof lines[5], "Acknowledgement failed: %d; context retained", rc);
        }
    } else if (c->kind == AZ_COMMAND_COMPLETION_RELEASED && command.phase == WAIT_RELEASED &&
               c->settled == 1 && command.terminal_seen && same_result(&c->result, &command.terminal)) {
        command.id = 0;
        command.phase = IDLE;
        command.terminal_seen = 0;
        snprintf(lines[5], sizeof lines[5], "Released; ready");
    } else protocol_error = 1;
    if (protocol_error) snprintf(lines[6], sizeof lines[6], "Command protocol error; unvalidated state retained");
    core->request_redraw();
}
static void tick(void)
{
    if (!core) return;
    az_data_capabilities caps = { .size = sizeof caps };
    int available = supported() && library->data_capabilities(&caps) == AZ_OK &&
                    caps.version == AZ_DATA_VERSION && (caps.commands & (UINT64_C(1) << AZ_CMD_LOOP_EXIT));
    snprintf(lines[1], sizeof lines[1], "Loop EXIT capability: %s", available ? "available" : "unavailable");
    if (attempted) snprintf(lines[2], sizeof lines[2], "Last attempt status %d", status);
    else snprintf(lines[2], sizeof lines[2], "No command submitted");
}
static int input(const az_event *event, void *user)
{
    (void)user;
    if (event->kind != AZ_EV_CLICK) return 0;
    if (command.phase != IDLE) return 1;
    attempted = 1;
    lines[3][0] = lines[5][0] = lines[6][0] = 0;
    protocol_error = 0;
    az_data_capabilities caps = { .size = sizeof caps };
    if (!supported() || library->data_capabilities(&caps) != AZ_OK || caps.version != AZ_DATA_VERSION ||
        !(caps.commands & (UINT64_C(1) << AZ_CMD_LOOP_EXIT))) {
        status = AZ_UNSUPPORTED;
        return 1;
    }
    az_deck_snapshot deck = { .size = sizeof deck };
    int rc = library->deck_snapshot(0, &deck);
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        status = AZ_FAILED;
        return 1;
    }
    const int seconds = compare_timestamps(now.tv_sec, 0, 1, 1000000000, 1);
    const int nanos = compare_timestamps(now.tv_nsec, 1000000000L, 1000000000, 1000000000, 1);
    const int range = compare_timestamps(now.tv_sec,
        (UINT64_MAX - (uint64_t)now.tv_nsec - UINT64_C(5000000000)) / UINT64_C(1000000000), 1, 1000000000, 1);
    if (seconds == AZ_TIME_INVALID || seconds == AZ_TIME_BEFORE || nanos == AZ_TIME_INVALID || nanos != AZ_TIME_BEFORE ||
        range == AZ_TIME_INVALID || range == AZ_TIME_AFTER) {
        status = AZ_FAILED;
        return 1;
    }
    uint64_t ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    uint64_t generation = 0;
    if (rc != AZ_OK || !az_read_native_load_target(&deck, 0, ns, &generation)) {
        status = rc == AZ_OK ? AZ_STALE : rc;
        return 1;
    }
    static uint64_t next_request;
    if (next_request == UINT64_MAX) {
        status = AZ_LIMIT;
        return 1;
    }
    az_command_request request = { .size = sizeof request, .version = AZ_DATA_VERSION, .operation = AZ_CMD_LOOP_EXIT, .deck = 0, .request_id = ++next_request, .expected_load_generation = generation, .flags = AZ_COMMAND_NATIVE_LOAD, .quantization = AZ_QUANTIZE_NATIVE, .deadline_ns = ns + UINT64_C(5000000000) };
    az_command_result result = { .size = sizeof result };
    status = library->command_submit_async(ID, &request, complete, &command, &result);
    if (status == AZ_OK) {
        command.id = request.request_id;
        command.phase = WAIT_TERMINAL;
        command.terminal_seen = 0;
        snprintf(lines[5], sizeof lines[5], "Waiting for terminal notification");
        if (!az_command_result_matches(&result, request.request_id)) {
            protocol_error = 1;
            snprintf(lines[6], sizeof lines[6], "Malformed submit reply; awaiting callback");
        }
    } else if (status != AZ_INVALID && status != AZ_BUSY &&
               az_command_result_matches(&result, request.request_id) &&
               result.outcome == AZ_COMMAND_REJECTED && result.reason == status) {
        snprintf(lines[3], sizeof lines[3], "Request %llu refused: %d", (unsigned long long)request.request_id, result.reason);
    }
    return 1;
}
static int init(const az_core_v1 *c)
{
    if (!AZ_HAS(c, library_service)) return -1;
    library = c->library_service(AZ_LIBRARY_ABI);
    core = c;
    snprintf(lines[0], sizeof lines[0], "Cue and loop command results");
    snprintf(lines[4], sizeof lines[4], "Click to EXIT the loop on deck 1, when available");
    if (!supported()) snprintf(lines[1], sizeof lines[1], "Asynchronous commands unsupported");
    return 0;
}
const library_page library_page_control = { init, tick, draw, input };
