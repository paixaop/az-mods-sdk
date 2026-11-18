/* SPDX-License-Identifier: GPL-3.0-only */
/* Storage workers must remain independent of concurrent UI callback context. */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>

static void *storage_during_draw(void *unused)
{
    (void)unused;
    thread_lifetime = __atomic_load_n(&lifetime, __ATOMIC_ACQUIRE);
    assert(s_disk_dir("storage-test") == NULL);
    return NULL;
}

int main(void)
{
    mod_id = "storage-test";
    ctx = CTX_DRAW;
    pthread_t worker;
    assert(pthread_create(&worker, NULL, storage_during_draw, NULL) == 0);
    assert(pthread_join(worker, NULL) == 0);
    assert(kfails[K_API] == 0);
    /* The same operation on the drawing thread must still be rejected. */
    assert(s_disk_dir("storage-test") == NULL);
    assert(kfails[K_API] == 1);
    assert(strstr(kfirst[K_API], "from on_draw"));
    ctx = CTX_INIT;
    assert(s_disk_dir("storage-test") == NULL);
    assert(kfails[K_API] == 1);
    return 0;
}
