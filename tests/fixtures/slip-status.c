/* SPDX-License-Identifier: GPL-3.0-only */
#include "../../examples/slip-callback-probe/probe.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static char line[512];
static az_deck_status_fn listener;
static void log_line(const char *id, const char *format, ...) {
    assert(!strcmp(id,ID)); va_list args;va_start(args,format);
    vsnprintf(line,sizeof line,format,args);va_end(args);
}
static int subscribe(const char *id,const az_deck_watch_v1 *watch,az_deck_status_fn fn,void *user,az_deck_subscription *out) {
    assert(!strcmp(id,ID) && watch->topics==AZ_STATUS_TOPIC(AZ_STATUS_SETTINGS));
    assert(!user);listener=fn;*out=1;return AZ_OK;
}
static az_observation_v1 observations={.abi=AZ_OBSERVATION_ABI,.size=sizeof observations,.on_deck_status=subscribe};
static const az_observation_v1 *service(uint32_t abi) { return abi==AZ_OBSERVATION_ABI?&observations:NULL; }
int main(void) {
    az_core_v1 host={.abi=AZ_MOD_ABI,.size=sizeof host,.observation_service=service,.log=log_line};
    assert(!init(&host));
    az_status_settings_v1 settings={.header={.size=sizeof settings,.version=AZ_DATA_VERSION}};
    az_deck_status_view_v1 view={.size=sizeof view,.version=AZ_DATA_VERSION,.settings=&settings};
    az_deck_status_event_v1 event={.size=sizeof event,.version=AZ_DATA_VERSION,.kind=AZ_STATUS_INITIAL,.deck=2,.sequence=9,
                                 .topics=AZ_STATUS_TOPIC(AZ_STATUS_SETTINGS),.view=&view};
    listener(NULL,NULL);assert(!line[0]);listener(&event,NULL);assert(strstr(line,"enabled=-1"));
    settings.header.supported=settings.header.valid=az_status_field_bit(AZ_STATUS_SETTINGS_SLIP);
    settings.slip_info.info.availability=AZ_AVAIL_READY;settings.slip_info.scope=AZ_STATUS_UNIT;settings.slip_info.scope_id=4;
    settings.slip=1;listener(&event,NULL);assert(strstr(line,"deck=3 seq=9") && strstr(line,"enabled=1"));
    settings.slip=0;listener(&event,NULL);assert(strstr(line,"enabled=0"));
    settings.slip=7;listener(&event,NULL);assert(strstr(line,"enabled=-1"));
    settings.slip=1;settings.slip_info.info.availability=AZ_AVAIL_LOADING;listener(&event,NULL);assert(strstr(line,"enabled=-1"));
    settings.header.version=99;line[0]=0;listener(&event,NULL);assert(!line[0]);
    view.settings=NULL;listener(&event,NULL);assert(!line[0]);
    assert(init(NULL)==-1);host.abi=1;assert(init(&host)==-1);host.abi=AZ_MOD_ABI;
    observations.on_deck_status=NULL;assert(init(&host)==-1);
    puts("PASS public Slip status: known, unknown, unsupported and malformed states");return 0;
}
