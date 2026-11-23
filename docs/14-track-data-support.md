# Track data and command support

Check capabilities at runtime. Supported fields depend on the firmware binding,
producer and current load. Native observations can be available independently
of a qualified file reference.

## Sources

| Source | Stored tickets | Loaded data |
| --- | --- | --- |
| Local USB with OneLibrary | Metadata, saved cues, grid, phrases and waveform windows when the required records and tags exist. | The current load can combine native and saved data when the source and file edition match. |
| Legacy USB media | The library service requires a resolved OneLibrary export. | Native fields can be available independently of a qualified file reference. |
| Linked media | USB tickets resolve only local physical ports. | Native metadata, transport and analysis may be available independently of a local library. |
| Beatport and cloud | USB tickets resolve only local physical ports. | Read the native fields with valid receipts. Eligible completed Beatport audio can use the analysis service. |

EMPTY confirms absence, STALE identifies invalidated data, and UNSUPPORTED
means the producer or component is unavailable. Whole-track waveform previews
use an approximate duration axis. Detail windows use the fixed analysis clock.

## Commands

| Command family | Admission and validation |
| --- | --- |
| Working loops | Require the operation capability and current native load generation. Verify the resulting state on the same load. |
| Main cue and saved cue recall | Require the corresponding native capability and matching target. Revision-qualified commands use the IDs from native_cues. |
| Saved hot and memory cue changes | Require persistence and any applicable native revision fence. Refresh the cue snapshot after the terminal result, and verify persistence across reload separately. |
| Explicit-position points and loops | Require a valid source position clock and the matching native cue revision. Occupied hot slots and duplicate memory positions are rejected. |

The SDK header gives the operation flags and arguments. The
[track data guide](13-track-data.md#observations-events-and-commands) covers
submission and completion. Qualify behavior on the exact firmware and source
using the [core test guide](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/CORE-SDK-AUTOMATION.md).
