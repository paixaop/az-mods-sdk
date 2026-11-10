/* SPDX-License-Identifier: GPL-3.0-only */
#include "pages.h"
#include "azmod-data-helpers.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define ID LIBRARY_INSPECTOR_ID
enum { LIVE_STATUS,
       LIVE_CUES,
       LIVE_ANALYSIS,
       LIVE_PAGES };
static const char *const page_names[] = { "Status", "Cues", "Analysis" };
static const az_core_v1 *core;
static az_deck_subscription watch;
static unsigned selected, page;
/* UI-owned copies. Pointers into the callback never reach draw(). */
static char lines[4][LIVE_PAGES][6][116];

static void draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    if (area.w < 32 || area.h < 32) return;
    char title[96];
    snprintf(title, sizeof title, "Deck %u / %s   (knob: deck, click: page)", selected + 1, page_names[page]);
    uint32_t color = core->color(AZ_COL_TEXT);
    int y = area.y + 8;
    core->draw_text(d, area.x + 8, y, title, 16, color);
    for (int i = 0; i < 6 && (y += 28) + 24 < area.y + area.h; ++i)
        if (core->text_width(lines[selected][page][i], 16) < area.w - 16)
            core->draw_text(d, area.x + 8, y, lines[selected][page][i], 16, color);
}
static int input(const az_event *ev, void *user)
{
    (void)user;
    if (ev->kind == AZ_EV_KNOB && ev->dir) selected = (selected + (ev->dir < 0 ? 3 : 1)) % 4;
    else if (ev->kind == AZ_EV_CLICK) page = (page + 1) % LIVE_PAGES;
    else return 0;
    core->request_redraw();
    return 1;
}
static const char *availability(uint32_t value)
{
    switch (value) {
    case AZ_AVAIL_LOADING: return "awaiting data";
    case AZ_AVAIL_READY: return "ready";
    case AZ_AVAIL_EMPTY: return "empty";
    case AZ_AVAIL_FAILED: return "failed";
    case AZ_AVAIL_STALE: return "stale";
    default: return "unavailable";
    }
}
static const char *number(char out[32], const double *v, const az_status_field_info_v1 *info)
{
    if (info->info.availability != AZ_AVAIL_READY) return availability(info->info.availability);
    if (!__builtin_isfinite(*v)) return "unavailable";
    snprintf(out, 32, "%.3f", *v);
    return out;
}
static int covers(const void *ptr, size_t size)
{
    if (!az_data_covers(ptr, size)) return 0;
    const az_status_topic_v1 *h = ptr;
    return h->version == AZ_DATA_VERSION;
}
static void changed(const az_deck_status_event_v1 *e, void *user)
{
    (void)user;
    if (!az_data_covers(e, offsetof(az_deck_status_event_v1, view) + sizeof e->view) ||
        e->version != AZ_DATA_VERSION || e->deck >= 4) return;
    char (*out)[6][116] = lines[e->deck];
    memset(out, 0, sizeof lines[e->deck]);
    snprintf(out[LIVE_STATUS][0], 116, "Load %" PRIu64 " / seq %" PRIu64 "%s", e->load_epoch, e->sequence, e->kind == AZ_STATUS_RESYNC ? " / resync" : "");
    if (!az_data_covers(e->view, sizeof *e->view) || e->view->version != AZ_DATA_VERSION) return;
    const az_deck_status_view_v1 *v = e->view;
    if (covers(v->metadata, sizeof *v->metadata)) {
        const az_status_metadata_v1 *m = v->metadata;
        snprintf(out[LIVE_STATUS][1], 116, "Track %.98s", m->title_info.info.availability == AZ_AVAIL_READY && m->title ? m->title : availability(m->title_info.info.availability));
    }
    if (covers(v->position, sizeof *v->position)) {
        char head[32], remaining[32];
        snprintf(out[LIVE_STATUS][2], 116, "Head %s / remaining %s s", number(head, &v->position->head.seconds, &v->position->head_info),
                 number(remaining, &v->position->remaining_s, &v->position->remaining_s_info));
    }
    if (covers(v->transport, sizeof *v->transport) && covers(v->sync, sizeof *v->sync)) {
        const az_status_transport_v1 *tr = v->transport;
        const char *play = availability(tr->state_info.info.availability);
        if (tr->state_info.info.availability == AZ_AVAIL_READY) {
            switch (tr->state) {
            case AZ_PLAYBACK_PAUSED: play = "paused"; break;
            case AZ_PLAYBACK_PLAYING: play = "playing"; break;
            case AZ_PLAYBACK_CUEING: play = "cueing"; break;
            case AZ_PLAYBACK_STOPPED: play = "stopped"; break;
            case AZ_PLAYBACK_ENDED: play = "ended"; break;
            }
        }
        const az_status_sync_v1 *sync = v->sync;
        snprintf(out[LIVE_STATUS][3], 116, "Transport %s / Sync %s", play, sync->enabled_info.info.availability == AZ_AVAIL_READY ? (sync->enabled ? "on" : "off") : availability(sync->enabled_info.info.availability));
    }
    if (covers(v->tempo, sizeof *v->tempo) && covers(v->key, sizeof *v->key)) {
        char bpm[32];
        snprintf(out[LIVE_STATUS][4], 116, "BPM %s / original key %.31s", number(bpm, &v->tempo->current_bpm, &v->tempo->current_bpm_info),
                 v->key->original_info.info.availability == AZ_AVAIL_READY && v->key->original ? v->key->original : availability(v->key->original_info.info.availability));
    }
    const az_status_loop_v1 *loop;
    if (az_read_event_loop(e, &loop)) {
        int on;
        snprintf(out[LIVE_CUES][0], 116, "Working loop %s", az_read_event_loop_engaged(e, &on) ? (on ? "engaged" : "off") : availability(loop->engaged_info.info.availability));
        double point;
        const int has_in = az_read_event_loop_point(e, AZ_STATUS_LOOP_RETAINED_IN, &point);
        if (has_in) snprintf(out[LIVE_CUES][1], 116, "Retained IN %.6f s", point);
        else snprintf(out[LIVE_CUES][1], 116, "Retained IN %s", availability(loop->retained_in_info.info.availability));
        if (az_read_event_loop_point(e, AZ_STATUS_LOOP_RETAINED_OUT, &point)) snprintf(out[LIVE_CUES][2], 116, "Retained OUT %.6f s", point);
        else snprintf(out[LIVE_CUES][2], 116, "Retained OUT %s", availability(loop->retained_out_info.info.availability));
    }
    if (covers(v->cues, sizeof *v->cues)) {
        const az_status_cues_v1 *c = v->cues;
        snprintf(out[LIVE_CUES][3], 116, "Live hot %s (%u), memory %s (%u)", availability(c->live_hot_info.info.availability), c->live_hot_info.count,
                 availability(c->live_memory_info.info.availability), c->live_memory_info.count);
        snprintf(out[LIVE_CUES][4], 116, "Saved hot %s (%u), memory %s (%u)", availability(c->saved_hot_info.info.availability), c->saved_hot_info.count,
                 availability(c->saved_memory_info.info.availability), c->saved_memory_info.count);
        if (c->live_memory_info.info.availability == AZ_AVAIL_READY && c->live_memory_info.count && c->live_memory) {
            const az_cue_entry *first = c->live_memory;
            if (az_data_covers(first, sizeof *first) && first->version == AZ_DATA_VERSION && (first->valid & AZ_CUE_START))
                snprintf(out[LIVE_CUES][5], 116, "First memory cue %.3f s  %.60s", first->start_s,
                         first->valid & AZ_CUE_COMMENT ? first->comment : "");
        }
    }
    if (covers(v->grid, sizeof *v->grid) && covers(v->phrases, sizeof *v->phrases)) {
        const az_status_grid_v1 *g = v->grid;
        const az_status_phrases_v1 *p = v->phrases;
        snprintf(out[LIVE_ANALYSIS][0], 116, "Current grid %s (%u beats)", availability(g->current_info.info.availability), g->current_info.count);
        snprintf(out[LIVE_ANALYSIS][1], 116, "Current phrases %s (%u)", availability(p->current_info.info.availability), p->current_info.count);
        if (g->current_info.info.availability == AZ_AVAIL_READY && g->current_info.count && g->current &&
            p->current_info.info.availability == AZ_AVAIL_READY && p->current_info.count && p->current)
            snprintf(out[LIVE_ANALYSIS][5], 116, "Beat 0 %.3f s / first phrase beats %u..%u (exclusive)",
                     g->current[0].time_s, p->current[0].start_beat, p->current[0].end_beat);
        snprintf(out[LIVE_ANALYSIS][2], 116, "Saved grid %s (%u), phrases %s (%u)", availability(g->saved_info.info.availability), g->saved_info.count,
                 availability(p->saved_info.info.availability), p->saved_info.count);
    }
    if (covers(v->overview, sizeof *v->overview)) {
        const az_status_overview_v1 *o = v->overview;
        snprintf(out[LIVE_ANALYSIS][3], 116, "Stored overview %s (%u samples)", availability(o->saved_info.info.availability),
                 o->saved_info.info.availability == AZ_AVAIL_READY && az_data_covers(o->saved, sizeof *o->saved) ? o->saved->sample_count : 0);
    }
    if (covers(v->phrase_playback, sizeof *v->phrase_playback)) {
        const az_status_phrase_playback_v1 *pp = v->phrase_playback;
        if (pp->active_info.info.availability == AZ_AVAIL_READY && pp->active) {
            if (pp->active->kind == AZ_PHRASE_ITEM) snprintf(out[LIVE_ANALYSIS][4], 116, "Active phrase %u (kind %u)", pp->active->index + 1, pp->active->phrase.raw_kind);
            else snprintf(out[LIVE_ANALYSIS][4], 116, "Active %s", pp->active->kind == AZ_PHRASE_START ? "START" : pp->active->kind == AZ_PHRASE_END ? "END"
                                                                                                                                                     : "GAP");
        } else snprintf(out[LIVE_ANALYSIS][4], 116, "Active phrase %s", availability(pp->active_info.info.availability));
    }
}
static int init(const az_core_v1 *c)
{
    core = c;
    memset(lines, 0, sizeof lines);
    const az_deck_watch_v1 options = { sizeof options, AZ_DATA_VERSION, AZ_STATUS_ALL_DECKS, 4, AZ_STATUS_ALL_V1 };
    int rc = AZ_UNSUPPORTED;
    if (AZ_LIB_HAS(library, on_deck_status) && AZ_LIB_HAS(library, deck_status_release))
        rc = library->on_deck_status(ID, &options, changed, NULL, &watch);
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned j = 0; j < LIVE_PAGES; ++j)
            snprintf(lines[i][j][0], 116, rc == AZ_OK ? "Awaiting deck status" : "Deck status unavailable (%d)", rc);
    return 0;
}
const library_page library_page_live = { init, NULL, draw, input };
