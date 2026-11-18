/* SPDX-License-Identifier: GPL-3.0-only */
/* Deck status page using only the public C SDK. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "hello-decks"

enum { kMaxDecks = 4,
       kMinRedrawMs = 250 };
static const az_core_v1 *C;
typedef struct {
    int loaded;
    long bpm_centi;
} DeckRow;
static DeckRow rows[kMaxDecks];
static bool update_row(DeckRow *row, int loaded, double bpm)
{
    if (!(bpm > 0.0 && bpm < 1000.0)) bpm = 0.0;
    long centi = (long)(bpm * 100.0 + 0.5);
    bool changed = loaded != row->loaded || centi != row->bpm_centi;
    row->loaded = loaded;
    row->bpm_centi = centi;
    return changed;
}
static int shown_decks = -1;
static int dirty = 0;
static long last_redraw_ms = -kMinRedrawMs;

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi
                                : v;
}

static long now_ms(void)
{
    struct timespec ts = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int deck_count(void)
{
    int n = C->deck_count();
    return n < 0 ? 0 : clamp(n, 0, kMaxDecks);     // unknown: show nothing
}

// Draw s, shortened from the end (whole UTF-8 characters) until it fits in max_w pixels: the
// core cuts drawing off at the area's edge but does not shorten text.
static void draw_fitted(az_draw *d, int x, int y, const char *s, int px, uint32_t color, int max_w)
{
    char buf[64] = { 0 };
    snprintf(buf, sizeof buf, "%s", s);
    size_t len = strlen(buf);
    while (len > 0 && C->text_width(buf, px) > max_w) {
        do len--;
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
        buf[len] = '\0';
    }
    if (len > 0) C->draw_text(d, x, y, buf, px, color);
}

static void on_draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    const uint32_t text = C->color(AZ_COL_TEXT);
    const uint32_t dim = C->color(AZ_COL_TEXT_DIM);
    const int row = C->row_height(d);
    const int px = clamp(row / 2, 8, 96);
    const int pad = row / 4;
    int y = area.y;

    C->draw_header(d, "Hello Decks");
    y += row;
    for (int deck = 0; deck < deck_count() && y + row <= area.y + area.h; deck++) {
        const DeckRow *r = &rows[deck];
        char line[64];
        if (!r->loaded)
            snprintf(line, sizeof line, "Deck %d   no track", deck + 1);
        else if (r->bpm_centi == 0)
            snprintf(line, sizeof line, "Deck %d   --- BPM", deck + 1);
        else
            snprintf(line, sizeof line, "Deck %d   %ld.%02ld BPM", deck + 1,
                     r->bpm_centi / 100, r->bpm_centi % 100);
        draw_fitted(d, area.x + pad, y + pad, line, px, r->loaded ? text : dim, area.w - 2 * pad);
        y += row;
    }
}

static int on_input(const az_event *ev, void *user)
{
    (void)user;
    return ev->kind == AZ_EV_KNOB || ev->kind == AZ_EV_CLICK;   // BACK and taps: the core
}

static const az_screen_v1 SCREEN = { sizeof(az_screen_v1), on_draw, on_input, NULL, NULL, 0 };

// Periodic at a varying rate: repaints are paced by the monotonic clock, not by ticks.
static void tick(void)
{
    int n = deck_count();
    if (n != shown_decks) {
        shown_decks = n;
        dirty = 1;
    }
    for (int deck = 0; deck < n; deck++) {
        int loaded = C->deck_loaded(deck) == 1;
        if (update_row(&rows[deck], loaded, loaded ? C->deck_bpm(deck) : 0.0))
            dirty = 1;
    }
    long now = now_ms();
    if (dirty && now - last_redraw_ms >= kMinRedrawMs) {
        dirty = 0;
        last_redraw_ms = now;
        C->request_redraw();
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_count) || !AZ_HAS(C, request_redraw) || !AZ_HAS(C, text_width))
        return 2;
    if (C->register_screen(MOD_ID, &SCREEN) != 0) return 3;
    C->log(MOD_ID, "loaded on %s", C->fw_tag());
    return 0;
}

static const char *const FW_TAGS[] = { "any", NULL };

static const az_mod_v1 MOD = { .abi = AZ_MOD_ABI, .size = sizeof(az_mod_v1), .id = MOD_ID, .fw_tags = FW_TAGS, .init = init, .tick = tick, .on_frame = NULL };

// The public C entry point.
const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
