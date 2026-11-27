/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-data-helpers.h"
#include <inttypes.h>
#define ID "sdk-slip-capture"
static const az_core_v1 *core;
static az_deck_subscription subscription;
static int slip_value(const az_status_settings_v1 *settings)
{
    uint64_t bit = az_status_field_bit(AZ_STATUS_SETTINGS_SLIP);
    if (!(settings->header.supported & settings->header.valid & bit)) return -1;
    if (settings->slip_info.info.availability != AZ_AVAIL_READY) return -1;
    return settings->slip == 0 || settings->slip == 1 ? settings->slip : -1;
}
static void changed(const az_deck_status_event_v1 *event, void *user)
{
    (void)user;
    const az_deck_status_view_v1 *view = az_data_event_view(event, AZ_STATUS_TOPIC(AZ_STATUS_SETTINGS),
                                                            offsetof(az_deck_status_view_v1, settings) + sizeof(((az_deck_status_view_v1 *)0)->settings));
    if (!view || !az_data_covers(view->settings, sizeof *view->settings)) return;
    const az_status_settings_v1 *settings = view->settings;
    if (settings->header.version != AZ_DATA_VERSION) return;
    core->log(ID, "SDK_PUBLIC_SLIP v=1 deck=%u seq=%" PRIu64 " kind=%u enabled=%d availability=%u scope=%u scope_id=%" PRIu64,
              event->deck + 1, event->sequence, event->kind, slip_value(settings),
              settings->slip_info.info.availability, settings->slip_info.scope, settings->slip_info.scope_id);
}
static int init(const az_core_v1 *c)
{
    if (!c || c->abi != AZ_MOD_ABI || !AZ_HAS(c, observation_service) || !AZ_HAS(c, log)) return -1;
    const az_observation_v1 *observation = c->observation_service(AZ_OBSERVATION_ABI);
    if (!AZ_OBSERVATION_HAS(observation, on_deck_status)) return -1;
    core = c;
    const az_deck_watch_v1 watch = { sizeof watch, AZ_DATA_VERSION, AZ_STATUS_ALL_DECKS, 0,
                                     AZ_STATUS_TOPIC(AZ_STATUS_SETTINGS) };
    return observation->on_deck_status(ID, &watch, changed, NULL, &subscription) == AZ_OK ? 0 : -1;
}
static const char *const tags[] = { "any", NULL };
static const az_mod_v1 mod = { .abi = AZ_MOD_ABI, .size = sizeof mod, .id = ID, .fw_tags = tags, .init = init };
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
