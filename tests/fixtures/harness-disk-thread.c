/* SPDX-License-Identifier: GPL-3.0-only */
/* Storage workers must remain independent of concurrent UI callback context. */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>

static const char test_id[] = "storage-test";

static void wait_for_workers(void)
{
    pthread_mutex_lock(&park_mu);
    while (nthreads) pthread_cond_wait(&park_cv, &park_mu);
    pthread_mutex_unlock(&park_mu);
}

static void *storage_during_draw(void *unused)
{
    (void)unused;
    assert(s_disk_dir(test_id) == NULL);
    return NULL;
}

static void storage_context(void)
{
    ctx = CTX_DRAW;
    for (int i = 0; i < 8; i++) {
        assert(s_thread_start(test_id, "storage", storage_during_draw, NULL) == 0);
        wait_for_workers();
    }
    assert(kfails[K_API] == 0);
    /* The same operation on the drawing thread must still be rejected. */
    assert(s_disk_dir("storage-test") == NULL);
    assert(kfails[K_API] == 1);
    assert(strstr(kfirst[K_API], "from on_draw"));
    ctx = CTX_INIT;
    assert(s_disk_dir("storage-test") == NULL);
    assert(kfails[K_API] == 1);
}

static atomic_bool release_workers;

static void *held_worker(void *arg)
{
    while (!atomic_load(&release_workers)) sched_yield();
    if (arg) pthread_exit(NULL);
    return NULL;
}

static void active_worker_limit(void)
{
    for (int i = 0; i < 4; i++) assert(s_thread_start(test_id, "held", held_worker, &release_workers) == 0);
    assert(s_thread_start(test_id, "extra", held_worker, NULL) == -1);
    assert(s_thread_start(test_id, NULL, held_worker, NULL) == -1);
    atomic_store(&release_workers, true);
    wait_for_workers();
    assert(s_thread_start(test_id, "available", held_worker, NULL) == 0);
    wait_for_workers();
}

static void *retiring_worker(void *arg)
{
    while (!mod_thread_stale()) sched_yield();
    if (arg) {
        s_cfg_set(test_id, "old-worker", 1);
        assert(!"Retired worker continued through its next core call");
    }
    return NULL;
}

static void fixture_post(void *user)
{
    *(int *)user += 1;
}

static void reload_workers(void)
{
    Dl_info di;
    assert(dladdr((void *)fixture_post, &di));
    mod_base = di.dli_fbase;
    int delivered = 0;
    s_post_ui(test_id, fixture_post, &delivered);
    drain_post();
    assert(delivered == 1);
    s_post_ui(test_id, fixture_post, &delivered);
    s_post_ui(test_id, NULL, &delivered);
    assert(s_thread_start(test_id, "returns", retiring_worker, NULL) == 0);
    assert(s_thread_start(test_id, "parks", retiring_worker, &release_workers) == 0);
    reset_mod_services();
    assert(nretiring == 0);
    assert(s_cfg_get(test_id, "old-worker", 0) == 0);
    drain_post();
    assert(delivered == 1);
    assert(s_thread_start(test_id, "next lifetime", held_worker, NULL) == 0);
    wait_for_workers();
}

static void settings_utf8(void)
{
    char out[130];
    s_cfg_set_str(test_id, "mixed", "caf\xc3\xa9\n!\x7f");
    s_cfg_set(test_id, "mixed", 42);
    assert(s_cfg_get_str(test_id, "mixed", out, 5, NULL) == 3 && !strcmp(out, "caf"));
    assert(s_cfg_get_str(test_id, "mixed", out, 6, NULL) == 5 && !strcmp(out, "caf\xc3\xa9"));
    assert(s_cfg_get_str(test_id, "mixed", out, 1, NULL) == 0 && !out[0]);
    assert(s_cfg_get_str(test_id, "mixed", out, sizeof out, NULL) == 6 && !strcmp(out, "caf\xc3\xa9!"));
    assert(s_cfg_get(test_id, "mixed", 0) == 42);
    assert(s_cfg_get_str(test_id, "missing", out, 3, "\xe5\xb1\xb1") == 0 && !out[0]);
    assert(s_cfg_get_str(test_id, "missing", out, 1, NULL) == 0 && !out[0]);
    assert(s_cfg_get_str(test_id, "missing", out, 0, NULL) == 0);
    strcpy(out, "caf\xc3\xa9");
    assert(s_cfg_get_str(test_id, "missing", out, 5, out) == 3 && !strcmp(out, "caf"));
    assert(s_cfg_get_str(test_id, NULL, out, sizeof out, "fallback") == 8 && !strcmp(out, "fallback"));
    s_cfg_set_str(test_id, "mixed", NULL);
    assert(!strcmp(cfg_str("mixed"), "caf\xc3\xa9!"));
    assert(cfg_str("missing") == NULL);

    char large[132];
    memset(large, 'x', 125);
    strcpy(large + 125, "\xf0\x9f\x8e\xb5");
    s_cfg_set_str(test_id, "boundary", large);
    assert(s_cfg_get_str(test_id, "boundary", out, sizeof out, NULL) == 125 && !out[125]);
    strcpy(large + 125, "\xc3\xa9\xe5\xb1\xb1");
    s_cfg_set_str(test_id, "boundary", large);
    assert(s_cfg_get_str(test_id, "boundary", out, sizeof out, NULL) == 127 && !memcmp(out, large, 127));
}

static const char *const texts[] = {
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
};

static int valid_worker_text(const char *text)
{
    return !strcmp(text, texts[0]) || !strcmp(text, texts[1]);
}

static void *settings_and_log_worker(void *arg)
{
    const char *key = arg;
    for (int i = 0; i < 100; i++) {
        char out[128];
        s_cfg_set(test_id, key, i);
        assert(s_cfg_get(test_id, key, -1) == i);
        s_cfg_set_str(test_id, "shared", texts[i % 2]);
        assert(s_cfg_get_str(test_id, "shared", out, sizeof out, NULL) == (int)strlen(texts[0]));
        assert(valid_worker_text(out));
        s_log(test_id, "%s", texts[i % 2]);
        if (i < 12) s_banner(test_id, texts[i % 2], -1);
    }
    return NULL;
}

static void concurrent_settings_and_log(void)
{
    const unsigned long before = log_snapshot(NULL);
    s_banner(test_id, texts[0], 0);
    advance(1);
    char keys[][2] = { "a", "b", "c", "d" };
    for (int i = 0; i < 4; i++) assert(s_thread_start(test_id, "settings", settings_and_log_worker, keys[i]) == 0);
    for (int i = 0; i < 100; i++) {
        log_snapshot(NULL);
        advance(1);
        assert(valid_worker_text(last_banner()));
    }
    wait_for_workers();
    char last[sizeof last_log];
    assert(log_snapshot(&last) == before + 400 && valid_worker_text(last));
    for (int i = 0; i < 4; i++) assert(cfg_int(keys[i], -1) == 99);
    soak_report(&(struct soak){ .name = "settings workers", .ticks = 100 }, 4.0, redraw_tick, before);
    assert(kfails[K_LOG] == 1 && strstr(kfirst[K_LOG], "400 log lines"));
}

static void banner_feedback(void)
{
    s_banner(test_id, texts[0], 0);
    assert(!last_banner()[0]);
    advance(TICK_NS);
    assert(!strcmp(last_banner(), texts[0]));
    s_banner(test_id, "", 50);
    s_banner(test_id, NULL, 50);
    assert(!strcmp(last_banner(), texts[0]));
    char long_message[140];
    memset(long_message, 'x', sizeof long_message - 1);
    long_message[sizeof long_message - 1] = '\0';
    s_banner(test_id, long_message, 75);
    assert(!strcmp(last_banner(), texts[0]));
    advance(TICK_NS);
    const char *copy = last_banner();
    assert(strlen(copy) == 111 && !strncmp(copy, long_message, 111));
    s_banner(test_id, texts[0], -1);
    advance(TICK_NS);
    assert(strlen(copy) == 111);
    assert(!strcmp(last_banner(), texts[0]));
}

static void banner_queue(void)
{
    char text[32];
    unsigned long before = log_snapshot(NULL);
    for (unsigned i = 1; i <= 64; ++i) {
        snprintf(text, sizeof text, "banner %u", i);
        s_banner(test_id, text, 100);
    }
    s_banner(test_id, "overflow", 0);
    char last[sizeof last_log];
    assert(log_snapshot(&last) == before + 1 && strstr(last, "banner dropped"));
    assert(!strcmp(last_banner(), texts[0]));
    for (unsigned i = 16; i <= 64; i += 16) {
        advance(TICK_NS);
        snprintf(text, sizeof text, "banner %u", i);
        assert(!strcmp(last_banner(), text));
    }
    advance(TICK_NS);
    assert(!strcmp(last_banner(), "banner 64"));
    s_banner(test_id, "next message", -1);
    advance(TICK_NS);
    assert(!strcmp(last_banner(), "next message"));
    s_banner(test_id, "before reload", -1);
    reset_mod_services();
    advance(TICK_NS);
    assert(!last_banner()[0]);
    s_banner(test_id, texts[0], 0);
    advance(TICK_NS);
}

struct fixture_confirmation {
    int calls, answer;
};

static void received_answer(int yes, void *user)
{
    assert(ctx == CTX_UI && !thread_lifetime);
    struct fixture_confirmation *result = user;
    result->calls++;
    result->answer = yes;
}

static void confirm_defaults(void)
{
    static const az_mod_v1 empty_overlay = { 0 };
    mod = &empty_overlay;
    Dl_info info;
    assert(dladdr((void *)received_answer, &info));
    mod_base = info.dli_fbase;
    struct fixture_confirmation result = { 0 };
    struct confirm_prompt prompt;
    ctx = CTX_INPUT;
    assert(s_confirm(test_id, NULL, NULL, NULL, received_answer, &result) == 0);
    assert(confirm_snapshot(&prompt) == CONFIRM_PENDING);
    assert(!strcmp(prompt.title, "Are you sure?") && !prompt.text[0] && !strcmp(prompt.yes, "OK"));
    answer(1);
    assert(!result.calls);
    assert(s_confirm(test_id, "ignored", "ignored", "ignored", received_answer, &result) == -1);
    call_tick();
    assert(confirm_snapshot(&prompt) == CONFIRM_SHOWN && !strcmp(prompt.title, "Are you sure?"));
    answer(42);
    assert(result.calls == 1 && result.answer == 1 && ctx == CTX_INPUT);
    answer(0);
    assert(result.calls == 1);
    assert(s_confirm(test_id, "", "custom body", "", received_answer, &result) == 0);
    assert(confirm_snapshot(&prompt) == CONFIRM_PENDING);
    assert(!strcmp(prompt.title, "Are you sure?") && !strcmp(prompt.text, "custom body") && !strcmp(prompt.yes, "OK"));
    call_tick();
    answer(0);
    assert(result.calls == 2 && result.answer == 0);
    char bounded[256];
    memset(bounded, 'a', sizeof bounded - 1);
    bounded[sizeof bounded - 1] = '\0';
    ctx = CTX_DRAW;
    assert(s_confirm(test_id, bounded, bounded, bounded, received_answer, &result) == 0);
    assert(confirm_snapshot(&prompt) == CONFIRM_PENDING);
    assert(strlen(prompt.title) == 47 && strlen(prompt.text) == 239 && strlen(prompt.yes) == 23);
    ctx = CTX_NONE;
    call_tick();
    answer(0);
    unsigned long before = kfails[K_API];
    ctx = CTX_INPUT;
    assert(s_confirm(test_id, NULL, NULL, NULL, NULL, NULL) == -1);
    assert(s_confirm("foreign", NULL, NULL, NULL, received_answer, &result) == -1);
    ctx = CTX_NONE;
    assert(s_confirm(test_id, NULL, NULL, NULL, received_answer, &result) == -1);
    ctx = CTX_FRAME;
    pthread_mutex_lock(&confirm_mu);
    assert(s_confirm(test_id, NULL, NULL, NULL, received_answer, &result) == -1);
    pthread_mutex_unlock(&confirm_mu);
    assert(kfails[K_API] == before + 3 && confirm_snapshot(NULL) == CONFIRM_IDLE && result.calls == 3);
    ctx = CTX_NONE;
}

struct worker_confirm_retry {
    atomic_int phase;
    int first, second;
    struct fixture_confirmation result;
};

static void wait_for_confirm_phase(struct worker_confirm_retry *retry, int phase)
{
    while (atomic_load(&retry->phase) < phase) sched_yield();
}

static void *worker_confirmation(void *user)
{
    struct worker_confirm_retry *retry = user;
    retry->first = s_confirm(test_id, "Busy", NULL, NULL, received_answer, &retry->result);
    atomic_store(&retry->phase, 1);
    wait_for_confirm_phase(retry, 2);
    retry->second = s_confirm(test_id, "Worker", "Retry succeeded", NULL, received_answer, &retry->result);
    atomic_store(&retry->phase, 3);
    wait_for_confirm_phase(retry, 4);
    answer(1); /* Only the overlay can answer the shown dialog. */
    return NULL;
}

static void confirm_worker_retry(void)
{
    struct fixture_confirmation overlay = { 0 };
    struct worker_confirm_retry retry = { 0 };
    struct confirm_prompt prompt;
    ctx = CTX_INPUT;
    assert(s_confirm(test_id, "Overlay", NULL, NULL, received_answer, &overlay) == 0);
    ctx = CTX_NONE;
    assert(s_thread_start(test_id, "confirm retry", worker_confirmation, &retry) == 0);
    wait_for_confirm_phase(&retry, 1);
    assert(retry.first == -1 && !retry.result.calls);
    assert(confirm_snapshot(&prompt) == CONFIRM_PENDING && !strcmp(prompt.title, "Overlay"));
    call_tick();
    answer(0);
    assert(overlay.calls == 1 && overlay.answer == 0);
    atomic_store(&retry.phase, 2);
    wait_for_confirm_phase(&retry, 3);
    assert(retry.second == 0 && !retry.result.calls);
    assert(confirm_snapshot(&prompt) == CONFIRM_PENDING && !strcmp(prompt.title, "Worker"));
    assert(!strcmp(prompt.text, "Retry succeeded"));
    call_tick();
    atomic_store(&retry.phase, 4);
    wait_for_workers();
    assert(confirm_snapshot(NULL) == CONFIRM_SHOWN && !retry.result.calls);
    answer(1);
    assert(retry.result.calls == 1 && retry.result.answer == 1 && confirm_snapshot(NULL) == CONFIRM_IDLE);
}

static void reopen_from_answer(int yes, void *user)
{
    received_answer(yes, user);
    assert(s_confirm(test_id, "Reopened", NULL, NULL, received_answer, user) == 0);
}

static void confirm_reopen_and_reload(void)
{
    struct fixture_confirmation result = { 0 };
    ctx = CTX_INPUT;
    assert(s_confirm(test_id, "First", NULL, NULL, reopen_from_answer, &result) == 0);
    ctx = CTX_NONE;
    call_tick();
    answer(0);
    assert(result.calls == 1 && confirm_snapshot(NULL) == CONFIRM_PENDING);
    reset_mod_services();
    call_tick();
    answer(1);
    assert(result.calls == 1 && confirm_snapshot(NULL) == CONFIRM_IDLE);
}

static const int64_t clock_step = NS + NS / 4;
static atomic_bool read_clocks;

static void *clock_worker(void *unused)
{
    (void)unused;
    while (!atomic_load(&read_clocks)) sched_yield();
    int64_t previous = 0;
    for (int i = 0; i < 2000; ++i) {
        struct timespec clock;
        assert(clock_gettime(CLOCK_MONOTONIC, &clock) == 0);
        int64_t instant = clock.tv_sec * NS + clock.tv_nsec;
        int order = compare_timestamps(instant, previous, NS, NS, 1);
        assert(order == AZ_TIME_EQUAL || order == AZ_TIME_AFTER);
        previous = instant;

        struct timeval wall;
        assert(gettimeofday(&wall, NULL) == 0);
        int64_t offset = (wall.tv_sec - T0_WALL) * NS + wall.tv_usec * 1000 - NS / 2;
        int alignment = compare_timestamps(offset, offset / clock_step * clock_step, NS, NS, 1);
        assert(alignment == AZ_TIME_EQUAL && offset >= 0);
        time_t saved;
        time_t seconds = time(&saved);
        assert(compare_timestamps(saved, seconds, 1, 1, 1) == AZ_TIME_EQUAL);
        int view = CORE.view();
        assert(view >= AZ_VIEW_OTHER && view <= AZ_VIEW_MENU);
    }
    return NULL;
}

static void concurrent_clocks_and_view(void)
{
    reset_world();
    assert(s_thread_start(test_id, "clock worker", clock_worker, NULL) == 0);
    atomic_store(&read_clocks, true);
    const struct soak simulation = { 0 };
    for (int i = 0; i < 2000; ++i) {
        advance(clock_step);
        soak_decks(&simulation, i);
    }
    wait_for_workers();
}

static void full_settings(void)
{
    for (int i = 0; i < 33; i++) {
        char key[8];
        snprintf(key, sizeof key, "slot%d", i);
        s_cfg_set(test_id, key, i);
        s_cfg_set_str(test_id, key, key);
    }
    assert(s_cfg_get(test_id, "slot32", -1) == -1);
    assert(!cfg_str("slot32"));
    s_cfg_set(test_id, "slot0", 7);
    s_cfg_set_str(test_id, "slot0", "updated");
    assert(s_cfg_get(test_id, "slot0", -1) == 7);
    assert(!strcmp(cfg_str("slot0"), "updated"));
}

static void fixture_audio(const float *samples, int frames, void *user)
{
    (void)samples;
    *(int *)user = frames;
}

static void audio_tap_limits(void)
{
    int frames = 0;
    assert(s_audio_tap(test_id, -1, 63, fixture_audio, &frames) == -1);
    assert(s_audio_tap(test_id, 0, 64, NULL, NULL) == -1);
    for (int d = 0; d < 4; d++) assert(s_audio_tap(test_id, d, 64, fixture_audio, &frames) == 0);
    assert(s_audio_tap(test_id, 0, 64, fixture_audio, &frames) == -1);
    assert(taps[3].source == 3 && taps[3].frames == 64);
    taps[3].fn(NULL, taps[3].frames, taps[3].user);
    assert(frames == 64);
}

int main(void)
{
    mod_id = test_id;
    concurrent_clocks_and_view();
    storage_context();
    active_worker_limit();
    reload_workers();
    settings_utf8();
    banner_feedback();
    banner_queue();
    confirm_defaults();
    confirm_worker_retry();
    confirm_reopen_and_reload();
    concurrent_settings_and_log();
    full_settings();
    audio_tap_limits();
    return 0;
}
