/* SPDX-License-Identifier: GPL-3.0-only */
#include "pages.h"
#include "azmod-data-helpers.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define ID LIBRARY_INSPECTOR_ID
static const az_core_v1 *core;
static char lines[10][112];
static void draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    if (area.w < 32 || area.h < 32) return;
    uint32_t color = core->color(AZ_COL_TEXT);
    int y = area.y + 8;
    /* Fixed short lines fit at the native screen size; tiny harness areas show status only. */
    for (int i = 0; i < 10 && y + 24 < area.y + area.h; i++, y += 28) {
        if (core->text_width(lines[i], 16) < area.w - 16)
            core->draw_text(d, area.x + 8, y, lines[i], 16, color);
    }
}
static void tick(void)
{
    if (!core) return;
    for (int deck = 0; deck < 4; deck++) {
        az_deck_snapshot s = { 0 };
        s.size = sizeof s;
        int rc = library->deck_snapshot(deck, &s);
        int loaded = -1, engaged = -1;
        double in_s = 0, out_s = 0;
        int bounds = 0;
        struct timespec now;
        if (rc == AZ_OK) {
            if (az_data_deck_header(&s, (uint32_t)deck) &&
                az_data_covers(&s, offsetof(az_deck_snapshot, loaded) + sizeof s.loaded) &&
                (s.valid & AZ_LIVE_LOADED) && (s.loaded == 0 || s.loaded == 1)) loaded = s.loaded;
            uint64_t ns = 0;
            if (!clock_gettime(CLOCK_MONOTONIC, &now) && now.tv_sec >= 0 && now.tv_nsec >= 0 &&
                now.tv_nsec < 1000000000L &&
                (uint64_t)now.tv_sec <= (UINT64_MAX - (uint64_t)now.tv_nsec) / UINT64_C(1000000000))
                ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
            (void)az_read_loop_engaged(&s, (uint32_t)deck, ns, &engaged);
            bounds = az_read_loop_bounds(&s, (uint32_t)deck, ns, &in_s, &out_s);
        }
        snprintf(lines[deck * 2 + 1], sizeof lines[deck * 2 + 1], "Deck %d: loaded %d, loop %s (status %d)", deck + 1,
                 loaded, engaged < 0 ? "unavailable" : engaged ? "engaged"
                                                               : "off",
                 rc);
        if (bounds)
            snprintf(lines[deck * 2 + 2], sizeof lines[deck * 2 + 2], "  Working IN %.6f s / OUT %.6f s", in_s, out_s);
        else
            snprintf(lines[deck * 2 + 2], sizeof lines[deck * 2 + 2], "  Working bounds unavailable; retained points unknown");
    }
}
static int init(const az_core_v1 *c)
{
    if (!AZ_LIB_HAS(library, data_capabilities) || !AZ_LIB_HAS(library, deck_snapshot)) return -1;
    core = c;
    snprintf(lines[0], sizeof lines[0], "Live deck and loop inspector");
    return 0;
}

const library_page library_page_live = { init, tick, draw, NULL };
