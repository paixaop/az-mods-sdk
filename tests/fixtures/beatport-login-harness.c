/* SPDX-License-Identifier: GPL-3.0-only */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>

int main(void)
{
    mod_id = "beatport-fixture";
    const az_beatport_login_v1 *service = CORE.beatport_login_service(AZ_BEATPORT_LOGIN_ABI);
    assert(AZ_HAS(&CORE, beatport_login_service));
    assert(!CORE.beatport_login_service(2));
    assert(AZ_BEATPORT_LOGIN_HAS(service, configure));
    assert(AZ_BEATPORT_LOGIN_HAS(service, status));
    assert(AZ_BEATPORT_LOGIN_HAS(service, forget));
    az_core_v1 old_core = CORE;
    old_core.size = offsetof(az_core_v1, beatport_login_service);
    assert(!AZ_HAS(&old_core, beatport_login_service));
    az_beatport_login_v1 old_service = *service;
    old_service.size = offsetof(az_beatport_login_v1, forget);
    assert(!AZ_BEATPORT_LOGIN_HAS(&old_service, forget));

    az_beatport_login_options options = {
        .size = sizeof options,
        .auto_login = 1,
        .remember = 1,
        .delay_sec = 45,
        .max_attempts = 1,
    };
    az_beatport_login_status state = { .size = sizeof state };
    ctx = CTX_TICK;
    assert(service->configure(mod_id, &options) == -1);
    assert(service->status(mod_id, &state) == -1);
    ctx = CTX_INIT;
    assert(service->configure(mod_id, &options) == 0);
    options.delay_sec = 75;
    assert(hb_options.delay_sec == 45); /* caller memory was copied */
    assert(service->status(mod_id, &state) == 0 && state.save_pending);
    hb_advance();
    ctx = CTX_INPUT;
    assert(service->configure(mod_id, &options) == 0);
    assert(hb_options.delay_sec == 75);
    options.remember = 2;
    assert(service->configure(mod_id, &options) == -1);
    assert(hb_options.remember == 1);
    options.remember = 1;
    options.delay_sec = 601;
    assert(service->configure(mod_id, &options) == -1);
    assert(hb_options.delay_sec == 75);
    options.delay_sec = 600;
    options.max_attempts = 5;
    assert(service->configure(mod_id, &options) == -1);
    options.max_attempts = 4;
    options.size = 0;
    assert(service->configure(mod_id, &options) == -1);

    hb_status.have_credentials = 1;
    snprintf(hb_status.username, sizeof hb_status.username, "fixture@example.test");
    hb_status.storage_error = EIO;
    assert(service->status(mod_id, &state) == 0);
    assert(state.have_credentials && !state.save_pending && state.storage_error == EIO);
    assert(strcmp(state.username, "fixture@example.test") == 0);
    assert(service->forget(mod_id) == 0);
    assert(service->status(mod_id, &state) == 0 && state.save_pending && state.have_credentials);
    hb_advance();
    assert(service->status(mod_id, &state) == 0);
    assert(!state.have_credentials && !state.save_pending && !state.storage_error && !state.username[0]);
    state.size = 0;
    assert(service->status(mod_id, &state) == -1);
    assert(service->status(mod_id, NULL) == -1);
    ctx = CTX_NONE;
    assert(service->forget(mod_id) == -1);
    puts("Beatport harness covers optional ABI, copied options, validation and async status");
    return 0;
}
