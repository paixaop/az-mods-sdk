# Background track analysis

The append-only `az_library_v1` tail provides `analysis_open`, `analysis_open_loaded`,
`analysis_commit` and `analysis_close`. Check each entry you use with `AZ_LIB_HAS`;
a loaded-track analyzer needs `analysis_open_loaded`, `analysis_commit` and
`analysis_close`. These calls belong on a mod worker, perform I/O, and refuse UI,
frame and audio contexts. They are independent of the legacy track-data development
flag. Mods use only this public API; the core owns native source qualification,
firmware integration and publication.

## Analyze the current load

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

The worker's decoded clock must agree with the grid's source clock. The supplied
Track Analysis worker preserves encoded MP3/AAC timing. Its AAC-LC decoder returns
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
