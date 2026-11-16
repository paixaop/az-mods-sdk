/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic service state only: no firmware editor, authentication or storage.
 * Scenarios may set hb_status to exercise pending/saved/error presentation. */
static az_beatport_login_options hb_options;
static az_beatport_login_status hb_status = { .size = sizeof(az_beatport_login_status) };
static int hb_configured, hb_forget_pending;

static int hb_context(const char *id, const char *operation)
{
    check_id(id, operation);
    if (!id || !mod_id || strcmp(id, mod_id) != 0 || thread_lifetime || ctx == CTX_NONE)
        return -1;
    return 0;
}

static int hb_configure(const char *id, const az_beatport_login_options *options)
{
    if (hb_context(id, "beatport configure") != 0 ||
        (!hb_configured && ctx != CTX_INIT) || !options || options->size < sizeof *options ||
        (options->auto_login != 0 && options->auto_login != 1) ||
        (options->remember != 0 && options->remember != 1) ||
        options->delay_sec < 0 || options->delay_sec > 600 ||
        options->max_attempts < 1 || options->max_attempts > 4)
        return -1;
    hb_options = *options;
    if (!hb_configured) hb_status.save_pending = 1;
    hb_configured = 1;
    return 0;
}

static int hb_get_status(const char *id, az_beatport_login_status *out)
{
    if (hb_context(id, "beatport status") != 0 || !hb_configured ||
        !out || out->size < sizeof *out)
        return -1;
    *out = hb_status;
    return 0;
}

static int hb_forget(const char *id)
{
    if (hb_context(id, "beatport forget") != 0 || !hb_configured) return -1;
    hb_forget_pending = 1;
    hb_status.save_pending = 1;
    return 0;
}

static void hb_advance(void)
{
    if (hb_forget_pending) {
        hb_status.have_credentials = 0;
        memset(hb_status.username, 0, sizeof hb_status.username);
        hb_status.storage_error = 0;
        hb_forget_pending = 0;
    }
    hb_status.save_pending = 0;
}

static const az_beatport_login_v1 HB_SERVICE = {
    .abi = AZ_BEATPORT_LOGIN_ABI,
    .size = sizeof(az_beatport_login_v1),
    .configure = hb_configure,
    .status = hb_get_status,
    .forget = hb_forget,
};

static const az_beatport_login_v1 *hb_service(uint32_t abi)
{
    return abi == AZ_BEATPORT_LOGIN_ABI ? &HB_SERVICE : NULL;
}
