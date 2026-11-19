/* SPDX-License-Identifier: GPL-3.0-only */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>

static void paint(az_draw *d, az_rect area, void *user)
{
    (void)d;
    (void)area;
    (void)user;
}

static unsigned painted, inputs, long_presses;
static void surface_paint(az_draw *d, az_rect area, void *user)
{
    assert(d == &draw_ctx && ctx == CTX_DRAW && user == &painted);
    assert(area.x == 20 && area.y == 30 && area.w == 100 && area.h == 50);
    ++painted;
}
static int surface_event(const az_event *event, void *user)
{
    assert(ctx == CTX_INPUT && user == &painted);
    ++inputs;
    long_presses += event->kind == AZ_EV_LONG;
    return 1;
}
static void surface_routes(void)
{
    screen = NULL;
    D.view = AZ_VIEW_DECK;
    D.count = 2;
    ctx = CTX_INIT;
    const az_surface_v1 descriptor = {
        .size = sizeof descriptor,
        .views = AZ_VIEW_TWO_DECKS,
        .on_paint = surface_paint,
        .on_input = surface_event,
        .user = &painted,
    };
    az_surface *surface = CORE.surface_create(mod_id, &descriptor);
    assert(surface);
    CORE.surface_show(surface, 1);
    paint_surfaces(1);
    assert(!painted); /* Visible but not placed. */
    ctx = CTX_UI;
    CORE.surface_place(surface, (az_rect){ 20, 30, 100, 50 });
    paint_surfaces(0);
    assert(painted == 1 && !surface->dirty);
    paint_surfaces(0);
    assert(painted == 1);
    paint_surfaces(1);
    assert(painted == 2);
    assert(call_input(AZ_EV_TAP, 0, 20, 30) == 1 && inputs == 1);
    assert(!call_input(AZ_EV_TAP, 0, 120, 30));
    assert(!call_input(AZ_EV_TAP, 0, 20, 80));
    assert(!call_input(AZ_EV_TAP, 0, 19, 30));
    assert(!call_input(AZ_EV_TAP, 0, 20, 29));
    assert(!call_input(AZ_EV_KNOB, 1, 0, 0));
    ctx = CTX_UI;
    assert(CORE.surface_focus(surface, 1));
    assert(call_input(AZ_EV_KNOB, 1, 0, 0) == 1 && inputs == 2);
    D.count = 4;
    paint_surfaces(1);
    assert(painted == 2 && !call_input(AZ_EV_TAP, 0, 20, 30));
    D.count = 2;
    ctx = CTX_UI;
    CORE.surface_show(surface, 0);
    paint_surfaces(1);
    assert(painted == 2);
    ctx = CTX_UI;
    CORE.surface_show(surface, 1);
    rng = 12345;
    for (unsigned i = 0; i < 128; ++i) {
        int x = -1, y = -1;
        unsigned before = long_presses;
        int delivered = soak_surface_press(&x, &y);
        assert(long_presses == before + (unsigned)delivered);
        if (delivered) assert(point_in_rect(surface->r, x, y));
    }
    assert(long_presses > 0);
    soak_long_press(120, 80);
    ctx = CTX_UI;
    assert(CORE.surface_focus(surface, 0));
    assert(!call_input(AZ_EV_KNOB, 1, 0, 0));
}

int main(void)
{
    mod_id = "screen-fixture";
    ctx = CTX_INIT;
    /* A descriptor compiled before flags were added remains accepted. */
    static const az_screen_v1 old = {
        .size = offsetof(az_screen_v1, flags),
        .on_draw = paint,
    };
    assert(CORE.register_screen(mod_id, &old) == 0);
    assert(screen == &old && screens_registered == 1);
    screen = NULL;
    screens_registered = 0;
    static const az_screen_v1 editor = {
        .size = sizeof editor,
        .on_draw = paint,
        .flags = AZ_SCREEN_DEDICATED,
    };
    assert(CORE.register_screen(mod_id, &editor) == 0);
    const az_row_v1 row = {
        .size = sizeof row,
        .key = "settings",
        .label = "Settings",
        .kind = AZ_ROW_SCREEN,
        .screen = &editor,
    };
    assert(CORE.register_row(mod_id, &row) >= 0);
    assert(screen == &editor && screens_registered == 1);
    az_screen_v1 invalid = editor;
    invalid.size = offsetof(az_screen_v1, user);
    assert(CORE.register_screen(mod_id, &invalid) != 0);
    surface_routes();
    puts("Screen harness: legacy descriptor and shared settings shortcut passed");
    return 0;
}
