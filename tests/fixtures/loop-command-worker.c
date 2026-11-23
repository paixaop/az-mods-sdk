/* SPDX-License-Identifier: GPL-3.0-only */
#define nanosleep fixture_sleep
#define main lifecycle_fixture_main
#include "loop-command-probe.c"
#undef main
#undef nanosleep
static unsigned sleeps;
static int unavailable_snapshot(int deck, az_deck_snapshot *out)
{
    snapshot(deck, out);
    return AZ_STALE;
}
int fixture_sleep(const struct timespec *delay, struct timespec *remaining)
{
    assert(!remaining && compare_timestamps(delay->tv_nsec, 50000000, 1000000000, 1000000000, 1) == AZ_TIME_EQUAL);
    errno = ++sleeps == 1 ? EINTR : EIO;
    return -1;
}
int main(void)
{
    assert(!lifecycle_fixture_main());
    strcpy(directory, "/tmp/loop-worker.XXXXXX");
    assert(mkdtemp(directory));
    reset();
    probe_worker(NULL);
    assert(sleeps == 2 && !probe.io_failed);
    probe_run_job(NULL);
    probe_job.value = input(1000);
    probe_job.value.load = 999;
    probe_job.valid = 1;
    atomic_store(&probe_job.busy, 1);
    unsigned before = submits;
    probe_run_job(&probe_job);
    assert(!atomic_load(&probe_job.busy) && submits == before && !probe.pending);
    probe_job.valid = 0;
    probe_run_job(&probe_job);
    assert(submits == before && !probe.pending);
    assert(strstr(last_log, "event=refused"));
    library_observation.deck_snapshot = unavailable_snapshot;
    struct probe_input view = { .deck = 0 };
    probe_emit("snapshot", &view, AZ_OK, NULL);
    assert(strstr(last_log, "load_valid=0 native_load=0 load_unit=0 load_ns=0"));
    assert(strstr(last_log, "engaged_valid=0 engaged=-1 bounds_valid=0 in=0 out=0 loop_unit=0 loop_rev=0 loop_ns=0"));
    char path[1100];
    snprintf(path, sizeof path, "%s/results.%ld.log", directory, (long)getpid());
    assert(!unlink(path) && !rmdir(directory));
    puts("PASS loop command worker: interrupted sleep, I/O exit and stale scheduling target");
    return 0;
}
