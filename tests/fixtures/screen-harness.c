/* SPDX-License-Identifier: GPL-3.0-only */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>

static void paint(az_draw *d, az_rect area, void *user)
{
    (void)d; (void)area; (void)user;
}

int main(void)
{
    mod_id = "screen-fixture";
    ctx = CTX_INIT;
    /* A descriptor compiled before flags were added remains accepted. */
    static const az_screen_v1 old = {
        .size = offsetof(az_screen_v1, flags), .on_draw = paint,
    };
    assert(CORE.register_screen(mod_id, &old) == 0);
    assert(screen == &old && screens_registered == 1);
    screen = NULL;
    screens_registered = 0;
    static const az_screen_v1 editor = {
        .size = sizeof editor, .on_draw = paint, .flags = AZ_SCREEN_DEDICATED,
    };
    assert(CORE.register_screen(mod_id, &editor) == 0);
    const az_row_v1 row = {
        .size = sizeof row, .key = "settings", .label = "Settings",
        .kind = AZ_ROW_SCREEN, .screen = &editor,
    };
    assert(CORE.register_row(mod_id, &row) >= 0);
    assert(screen == &editor && screens_registered == 1);
    az_screen_v1 invalid = editor;
    invalid.size = offsetof(az_screen_v1, user);
    assert(CORE.register_screen(mod_id, &invalid) != 0);
    puts("Screen harness: legacy descriptor and shared settings shortcut passed");
    return 0;
}
