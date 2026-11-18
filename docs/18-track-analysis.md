# Background track analysis

The append-only `az_library_v1` exposes analysis leases, typed grid snapshots,
typed result publication and lease cleanup. Check every entry you use with
`AZ_LIB_HAS`. Run these calls on an SDK-created mod thread: they perform I/O
and refuse UI, frame and audio contexts. Mods use only this public API; the core
owns source qualification, firmware integration and publication.

## Typed analysis

Use `analysis_open_loaded_v2`, `analysis_grid`, `analysis_commit_result` and
`analysis_close` for an in-process analyzer. These calls do not require a helper
executable or serialized result buffer. A loaded lease never requests a new grid.
For vocal-only work, skip the grid read entirely.

Initialize `az_analysis_grid_v1.size` and call `analysis_grid(id, job, &grid,
NULL, 0)`. `AZ_LIMIT` reports the required `grid.beat_count`; allocate that many
`az_beat` records and repeat the call with the capacity. The snapshot belongs to
the lease. Insufficient capacity does not copy a partial grid. Other failures
leave the descriptor and beat array untouched. Stale load, source or lease
identity returns `AZ_STALE`. Beat times are seconds on the native source clock.

Publish an `az_analysis_result_v1` with `size`, `version = AZ_DATA_VERSION`,
actual decoded `source_rate` and `source_frames`. Populate only requested maps:

- `phrases`: mood 1/2/3, terminal beat and an array of
  `az_analysis_phrase_v1` records. Phrase starts, fill starts, structure beats and
  the terminal use **one-based native beat ordinals**; zero marks absent optional
  positions. Starts are strictly increasing and precede the terminal.
- `vocals`: `sample_rate = 22050`, `hop_samples = 1024`, `method = AZ_VOCAL_LSTM`,
  count and an array of `az_vocal_frame_v1` records with activity 0 through 4.
- `beats`: supplied only for a file-only lease requesting a missing grid.
  A loaded lease leaves both `beats` and `beat_count` zero.

Zero-initialize reserved fields and absent maps, including their pointers. Arrays
remain caller-owned and must stay valid and unchanged for the synchronous commit
call. The core validates counts, dimensions and values before serializing native
file formats internally, then applies the same freshness and publication checks
as legacy commits. Invalid records return `AZ_INVALID`; allocation pressure can
return `AZ_LIMIT`. Success consumes the lease. Retry `AZ_BUSY` while the job is
current; close every lease that did not commit successfully.

The typed vocal count is
`ceil(floor(source_frames * 22050 / source_rate) / 1024)`, with at least nine
frames. A source rate of 44100 or 48000 and at most twenty minutes are accepted.
The grid and decoded PCM must refer to the same clock; compare timestamp values
with `compare_timestamps()` using the actual decoded rate.

The raw `analysis_commit` entry remains available for existing callers. Its
TA01/TA02/TA03 compatibility layouts are documented below; new analyzers should
use typed publication.

## Phrase and vocal completion (v2)

New analyzers should use `analysis_open_v2` or `analysis_open_loaded_v2`, plus the
typed `analysis_commit_result` and `analysis_close`. Check each member with `AZ_LIB_HAS`.
The original open calls remain phrase-only and keep their TA01 contract below.
The v2 calls request each missing component independently:

| Saved/current analysis | `needed` work |
| --- | --- |
| Phrases and populated PVDI | None; `AZ_NOT_FOUND`, `job=0` |
| Phrases only | `AZ_ANALYSIS_VOCALS` |
| PVDI only | `AZ_DATA_PHRASES` (plus grid for file-only jobs if missing) |
| Neither | `AZ_DATA_PHRASES` and `AZ_ANALYSIS_VOCALS` (plus missing file-only grid) |

A valid populated PVDI with no positive detections is complete. An absent tag or a
zero-entry placeholder needs analysis. Unknown versions, malformed tags and duplicate
maps are refused, never overwritten. Loaded phrase work still requires a ready native
grid. A vocal-only loaded job may have an empty `analysis_path`; no grid is needed for
that work. Cached phrases are refreshed into the native holder before retaining a
vocal-only job, and its native map edition is recaptured after refresh.

Legacy v2 callers can commit `TA02`, with big-endian fields:

| Offset | Value |
| --- | --- |
| 0 | `TA02` |
| 4 | PQTZ byte length (zero for loaded jobs) |
| 8 | PSSI byte length, or zero when phrases were already present |
| 12 | PVDI byte length, or zero when vocals were already present |
| 16 | Actual decoded PCM sample rate (44100 or 48000) |
| 20 | Actual decoded source frame count (at most twenty minutes) |
| 24 | PQTZ, PSSI, PVDI tags in that order; omitted components consume no bytes |

Legacy LSTM callers first check `AZ_LIB_HAS(L, analysis_formats)` and the
`AZ_ANALYSIS_FORMAT_TA03` bit. `TA03` retains the TA02 fields at offsets 4–20,
adds a u32 method at 24, and starts tags at 28. Use `AZ_VOCAL_LSTM` (2) when
supplying vocals, or zero for a phrase-only result. Existing TA01/TA02 clients
remain supported. Older cores cannot accept TA03.

The supplied components must match `job.needed`. PVDI has header length 24,
version/flags at 12 (u16, zero for plain data), hop samples at 14 (u16), analysis
sample rate at 16 (u16), entry size 1 at 18 (u16), frame count at 20 (u32), and
one 0..4 confidence byte per frame. At most 262144 frames are accepted. Scrambled
or unknown versions are refused. Do not confuse the analysis rate with the source
PCM rate. TA02 retains its original duration-ceiling count rule and spectral method.
TA03 LSTM output requires hop=1024, rate=22050, at least nine frames, and count
`ceil(floor(source_frames * 22050 / source_rate) / 1024)`. The inner floor is
required: 18433 frames at 44100 Hz produce nine analysis frames, not ten.

The core saves PVDI to `.2EX` and adds an `AZVD` provenance tag: header/total length
32, u32 schema=1 at 12, method=`AZ_VOCAL_SPECTRAL` (TA02) or `AZ_VOCAL_LSTM` (TA03) at 16, FNV-1a-32 of the complete
PVDI tag at 20, decoded source frame count at 24 and source PCM rate at 28. A marker
only applies to its matching PVDI bytes. Unknown tags and unrelated bytes survive.
DAT, EXT and 2EX are published independently with synced renames; partial failure
can leave valid reusable components. A later lease requests only missing work.

## Reading vocal intervals

`vocal_intervals(id, &track, &out, intervals, capacity)` reads a USB OneLibrary track.
`vocal_intervals_loaded(id, deck, load_epoch, &out, intervals, capacity)` reads the
accepted current USB or completed Beatport load. Both are copied, worker-only calls
on the shared library service, independent of the legacy ticket development flag.
They perform I/O; never call them from UI, frame or audio callbacks. Initialize
`out.size = sizeof out`. They do not create a job, ticket, cache or ANLZ file.

`az_vocal_interval` contains only `start_s` and `stop_s`, defining half-open original-
track seconds. PVDI values >=3 form contiguous intervals. No smoothing, minimum
length or beat snapping is imposed. Convert playback clocks explicitly if needed.
`az_vocal_snapshot_v1` reports count, analysis clock, covered duration, component
revision and observation time. `method` is `AZ_VOCAL_PVDI` for existing data or
`AZ_VOCAL_SPECTRAL`/`AZ_VOCAL_LSTM` for a matching generated marker; the former does not authenticate
which application created the file. The last generated interval is clipped to the
exact decoded duration. File-only PVDI without a marker covers whole analysis hops.

| Result | Meaning |
| --- | --- |
| `AZ_OK` | Complete copy; zero intervals is valid completed instrumental analysis |
| `AZ_LIMIT` | `out.interval_count` gives required capacity; no intervals copied |
| `AZ_NOT_FOUND` | No populated PVDI; this does not mean there are no vocals |
| `AZ_UNSUPPORTED` | Unsupported source or PVDI version |
| `AZ_FAILED` | Malformed/ambiguous analysis or I/O failure |
| `AZ_STALE` | Load/source/files changed while reading |

Pass NULL/0 for a sizing read, allocate the reported count, then read again. Handle
`AZ_LIMIT` again if analysis changed, and use the final revision. Output stays unchanged
on errors other than `AZ_LIMIT`. Caller-owned intervals need no release. The SDK derives
intervals directly from stored PVDI, so there is no separate interval file to go stale.

```c
static void read_loaded_vocals(const az_library_v1 *lib, int deck,
                               uint64_t load_epoch) {
    az_vocal_interval spans[1024];
    az_vocal_snapshot_v1 vocals = {.size = sizeof vocals};
    if (AZ_LIB_HAS(lib, vocal_intervals_loaded)) {
        int rc = lib->vocal_intervals_loaded(MOD_ID, deck, load_epoch,
                                            &vocals, spans, 1024);
        if (rc == AZ_OK) {
            /* Copy/send spans[0..vocals.interval_count) to the UI on its own thread. */
        }
        /* Handle AZ_LIMIT with a larger buffer, and absence/staleness independently. */
    }
}
```

PVDI structure is corroborated by [pyrekordbox's parser](https://github.com/dylanljones/pyrekordbox/pull/208/files)
and [dj-setbox's timing observations](https://github.com/tgf-between-your-legs/dj-setbox).
Retained native-function and original-model fixtures validate the reconstructed
LSTM pipeline; generated-data and format tests establish
SDK behavior, not detection accuracy or native firmware display of this tag.

## Analyze the current load (legacy phrase-only calls)

`analysis_open_loaded(id, deck, load_epoch, &job)` accepts deck 0..3 and the
`load_epoch` from an accepted native LOAD status event. Initialize `job.size`.
The core requires a ready native grid and retains the exact loaded map edition.
The implemented native publication and Beatport source paths are restricted to the
qualified firmware 2.00 profile. The core checks its required native entry points;
an API member's presence does not guarantee that the running firmware is supported.

For USB, the core resolves the current load to a qualified OneLibrary reference
and its audio/analysis files. For Beatport, it leases the completed, decrypted file
retained by the native download resource. A streaming lease has an intentionally
empty `track` reference; mods must not invent a USB identity or guess a cache path.
Other streaming services and incomplete downloads have no qualified audio lease.

On `AZ_OK`, `job.job` is nonzero, `job.needed` is `AZ_DATA_PHRASES`, and
`AZ_ANALYSIS_LOADED` is set. `audio_path` is a read-only input locator.
`analysis_path` names a temporary, read-only PMAI/PQTZ snapshot of the pinned
native grid, not a writable destination. It preserves native row indexes and
converts coordinates using the holder's actual sample rate and signed origin.
Times are rounded to PQTZ milliseconds. The worker must use that grid and return
PSSI only; a loaded commit rejects a supplied replacement PQTZ.

| Result | Meaning and caller action |
| --- | --- |
| `AZ_OK` | A lease was retained. Analyze, then commit or close it. |
| `AZ_NOT_FOUND` | Native phrases are already populated, or matching saved phrases were refreshed into the current native holder. `job.job` is zero; no analysis is needed. |
| `AZ_BUSY` | A source/grid is still arriving, a relevant lease is occupied, or a lock is busy. Retry while the same load remains current. |
| `AZ_STALE` | The load, map edition, source or files changed, or a saved grid does not match the current native grid. Close any retained job before opening another. |
| `AZ_UNSUPPORTED` | The firmware profile, source capability or grid layout is unsupported. |
| `AZ_LIMIT` | A bounded resource limit was reached. At most four jobs per mod and eight globally are retained. |
| `AZ_INVALID` / `AZ_FAILED` | Invalid input or a validation/I/O failure. Do not consume output paths from a failed open. |

Flags describe the returned job:

| Flag | Meaning |
| --- | --- |
| `AZ_ANALYSIS_LOADED` | The lease pins a native load/grid; commit also refreshes native phrases. |
| `AZ_ANALYSIS_STREAMING` | A successful lease uses a completed Beatport download; `track` is empty. |
| `AZ_ANALYSIS_NEEDS_LINK` | A generated USB ANLZ locator needs the native library spool to apply its conditional update. |

Beatport analysis uses four bounded settings caches, one per deck. Each holds only
analysis DAT/EXT and an identity derived from the native TrackID and audio-file
fingerprint. A new identity replaces that deck's previous cache. Audio and credentials
are not copied into settings. Existing saved phrases are reused only when the saved
grid's count, beat positions, tempos and timestamps match the pinned native grid,
with a sample-rate-aware allowance for PQTZ millisecond rounding. Populated phrases
in DAT are not hidden by an empty EXT placeholder.

## Analyze a USB library reference

`analysis_open(id, &track, &job)` accepts a source-qualified USB OneLibrary reference
from a loaded status view or `track_resolve`. Initialize `job.size`. On `AZ_OK`,
`job.needed` names GRID and/or PHRASES, and the record supplies read-only audio and DAT
locators plus the library's original global BPM. The opaque job belongs to that mod.
At most four jobs per mod and eight globally are retained. `AZ_NOT_FOUND` means the
saved container already has phrases; `AZ_UNSUPPORTED` means the source has no supported
audio source. Unknown references and source changes fail closed.

Rows without an ANLZ locator receive a stable path under `PIONEER/USBANLZ/AZMODS`.
`AZ_ANALYSIS_NEEDS_LINK` in `job.flags` means commit queues the new locator through
the native library transaction spool. The mod calls `lib_spool_kick` on its worker
and retries until admitted; the existing playback gate controls application.
An already populated generated location returns `AZ_NOT_FOUND` with `job.job=0`
and this flag, repairing a previously interrupted link without recomputing phrases.
The SQL update changes only an empty locator on the matching content ID and audio
path; it preserves a later DJ edit. Text is encoded as hex literals for the spool.

## Worker output and timing

The mod can read the locators and launch its own analysis helper. It must not write
the ANLZ paths itself. `analysis_commit` accepts a bounded byte buffer:

| Offset | Value |
| --- | --- |
| 0 | Four bytes `TA01` |
| 4 | Big-endian u32 length of PQTZ; must be zero for a loaded lease |
| 8 | Big-endian u32 length of PSSI |
| 12 | Big-endian u32 actual analysis PCM sample rate: 44100 or 48000 |
| 16 | Optional complete PQTZ tag, then complete PSSI tag |

PQTZ uses its 24-byte header and eight-byte rows, limited to 32768 beats. PSSI uses
version zero, a 32-byte header and 24-byte rows, limited to 1024 phrases. The core checks
framing, timestamps, beat positions/tempos, mood, ordered phrase boundaries, counts and
the terminal beat against the saved/generated grid. Future layouts are refused.

An ordinary USB-reference job may supply a missing PQTZ. A loaded job always
uses its captured native grid: if saved PQTZ is missing, the core persists its own
snapshot; if present, the grid must match the snapshot before PSSI is saved.
Neither path replaces an existing populated grid. Loaded publication never installs
or retimes the native grid.

The analyzer's decoded clock must agree with the grid's source clock. The supplied
Track Analysis C decoder preserves encoded MP3/AAC timing. Its AAC-LC decoder returns
1024 source frames per access unit and restores the first 1024-frame slot suppressed
by FAAD's overlap initialization as silence. Remaining encoder priming stays in the
audio. MP4 edit-list start/duration metadata is explicit; it is not automatically
applied as gapless trimming, and iTunSMPB is not used to shift the timeline. Generated
FFmpeg and Apple witnesses retain 1024 and 2112 frames of encoder priming respectively.
Do not subtract either delay again when analyzing the loaded-grid snapshot. This
decoder contract does not establish equivalence to every encoder or native resampler.

## Publication and lease lifetime

Publication rechecks the physical USB source, library incarnation and DB/audio/ANLZ
fingerprints, or the retained Beatport source and its audio/cache fingerprints.
It preserves unrelated container bytes, refuses populated maps and binds temporary
files and renames to directory descriptors under the selected storage root.
Fingerprints are freshness witnesses, not transactional exclusion against every
native editor.

DAT and EXT are individually synced and renamed; they are not an atomic pair. DAT is
published first so no saved phrase references a missing new grid. Failure after DAT
can leave a usable grid without phrases. Native publication happens after durable
file publication. A load/map change at that point can leave saved analysis while
the native refresh returns stale. Close the old lease and open another job; a matching
saved map can then be refreshed without another decode.

For loaded jobs, the core installs native phrases only if the accepted load, grid
edition and empty phrase holder still match the lease. It uses the firmware's phrase
parser, counted ownership and normal notification route. A stale completion cannot
publish phrases into a replacement load, retimed grid or populated phrase holder.
The ordinary `analysis_open` path remains file-only.

Successful commit consumes the job. Close every other retained job, retrying
`AZ_BUSY`; stop using its locators once close succeeds. Close releases native map/audio
retentions and removes the grid snapshot. Commit and close serialize: a successful
close makes a later commit stale, while a successful commit makes a later close stale.
Close does not roll back already-published files or a commit that won the race.

Firmware 2.00 USB emulator testing verified native phrase bars and subscribing mods
after publication for two AAC-LC M4A tracks. Beatport resource/lease fixtures pass,
but authenticated catalog playback and physical-deck acceptance remain outstanding.
