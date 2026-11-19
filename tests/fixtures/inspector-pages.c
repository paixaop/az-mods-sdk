/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>
#include <time.h>
static uint64_t clock_ns = UINT64_C(1000000000);
static int test_clock(clockid_t kind, struct timespec *out)
{
    assert(kind == CLOCK_MONOTONIC);
    out->tv_sec = clock_ns / UINT64_C(1000000000);
    out->tv_nsec = clock_ns % UINT64_C(1000000000);
    return 0;
}
#define clock_gettime test_clock
#include "example.c"

static unsigned initialized, ticked, drawn, fills, texts, redrawn, inputs;
static int init_status;
static const az_screen_v1 *registered;
static int page_init(const az_core_v1 *c)
{
    assert(c);
    ++initialized;
    return init_status;
}
static void page_tick(void) { ++ticked; }
static void page_draw(az_draw *d, az_rect area, void *user)
{
    (void)d;
    assert(!user && area.x == 10 && area.y == 60 && area.w == 400 && area.h == 260);
    ++drawn;
}
static int page_input(const az_event *event, void *user)
{
    assert(event && !user);
    ++inputs;
    return 17;
}
const library_page library_page_cues = { page_init, page_tick, page_draw, page_input };
const library_page library_page_live = { page_init, page_tick, page_draw, NULL };
const library_page library_page_analysis = { page_init, page_tick, page_draw, NULL };
const library_page library_page_control = { page_init, NULL, page_draw, page_input };
static int caps(az_data_capabilities *out)
{
    (void)out;
    return AZ_OK;
}
static int deck(int index, az_deck_snapshot *out)
{
    (void)index;
    (void)out;
    return AZ_OK;
}
static az_library_v1 service = { .abi = AZ_LIBRARY_ABI, .size = sizeof service,  };
static az_observation_v1 service_observation = { .abi = AZ_OBSERVATION_ABI, .size = sizeof service_observation, .data_capabilities = caps, .deck_snapshot = deck };
static az_commands_v1 service_commands = { .abi = AZ_COMMANDS_ABI, .size = sizeof service_commands };
static const az_library_v1 *get_service(uint32_t abi)
{
    assert(abi == AZ_LIBRARY_ABI);
    return &service;
}
static const az_observation_v1 *get_service_observation(uint32_t abi)
{
    assert(abi == AZ_OBSERVATION_ABI);
    return &service_observation;
}

static const az_commands_v1 *get_service_commands(uint32_t abi)
{
    assert(abi == AZ_COMMANDS_ABI);
    return &service_commands;
}

static int screen_register(const char *id, const az_screen_v1 *screen)
{
    assert(!strcmp(id, LIBRARY_INSPECTOR_ID));
    registered = screen;
    return 0;
}
static void log_line(const char *id, const char *format, ...) { assert(id && format); }
static uint32_t color(int key) { return (uint32_t)key; }
static int width(const char *text, int px) { return (int)strlen(text) * px / 2; }
static void fill(az_draw *d, int x, int y, int w, int h, uint32_t rgb)
{
    (void)d;
    (void)rgb;
    assert(x >= 10 && y == 20 && w == 100 && h == 36);
    ++fills;
}
static void paint(az_draw *d, int x, int y, const char *text, int px, uint32_t rgb)
{
    (void)d;
    (void)rgb;
    assert(x >= 10 && y == 28 && text && px == 16);
    ++texts;
}
static void redraw(void) { ++redrawn; }
int main(void)
{
    az_core_v1 c = { .abi = AZ_MOD_ABI, .size = sizeof c, .library_service = get_service, .observation_service = get_service_observation, .command_service = get_service_commands, .register_screen = screen_register, .log = log_line, .color = color, .text_width = width, .fill_rect = fill, .draw_text = paint, .request_redraw = redraw };
    assert(init(&c) == 0 && initialized == 4 && registered);
    registered->on_draw(NULL, (az_rect){ 10, 20, 400, 300 }, NULL);
    assert(drawn == 1 && fills == 4 && texts == 3); /* The analysis tab is too wide. */
    registered->on_draw(NULL, (az_rect){ 0, 0, 20, 20 }, NULL);
    assert(drawn == 1 && fills == 4);
    assert(input(&(az_event){ .kind = AZ_EV_CLICK }, NULL) == 17 && inputs == 1);
    assert(input(&(az_event){ .kind = AZ_EV_TAP, .x = 150, .y = 30 }, NULL) == 1 && active == 1);
    assert(input(&(az_event){ .kind = AZ_EV_KNOB, .dir = 1 }, NULL) == 1 && active == 2);
    assert(input(&(az_event){ .kind = AZ_EV_KNOB, .dir = -1 }, NULL) == 1 && active == 1);
    assert(input(&(az_event){ .kind = AZ_EV_BACK }, NULL) == 0);
    tick();
    assert(ticked == 3 && redrawn == 1);
    tick();
    assert(ticked == 6 && redrawn == 1);
    clock_ns += UINT64_C(250000000);
    tick();
    assert(redrawn == 2);
    init_status = -1;
    assert(init(&c) == -1);
    c.library_service = NULL;
    assert(init(&c) == -1);
    return 0;
}
