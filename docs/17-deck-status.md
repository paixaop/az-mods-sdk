# Listening to a loaded deck

A deck-status subscription delivers the current state and subsequent changes on logical decks 0–3. A mod can listen to the physical deck with no tick, worker, open screen or outstanding command. A mod that requires these listeners declares `"min_core": "1.2.6"` in its manifest and checks both service pointers at runtime. A mod with an older-core fallback can keep its earlier minimum and select the fallback when either member is absent. A field's supported bit and availability describe what the running firmware and source can actually supply.

```c
#include "azmod-data-helpers.h"

#define ID "loop-watch"
static az_deck_subscription subscription;
static int engaged[4];

static void changed(const az_deck_status_event_v1 *event, void *user)
{
    const az_status_loop_v1 *loop;
    if (!az_read_event_loop(event, &loop)) return;
    int on;
    ((int *)user)[event->deck] = az_read_event_loop_engaged(event, &on) ? on : -1;
}

static int init(const az_core_v1 *core)
{
    if (!AZ_HAS(core, library_service)) return -1;
    const az_library_v1 *lib = core->library_service(AZ_LIBRARY_ABI);
    if (!AZ_LIB_HAS(lib, on_deck_status) || !AZ_LIB_HAS(lib, deck_status_release)) return -1;
    const az_deck_watch_v1 watch = {
        sizeof watch, AZ_DATA_VERSION, AZ_STATUS_ALL_DECKS, 0, AZ_STATUS_TOPIC(AZ_STATUS_LOOP)
    };
    return lib->on_deck_status(ID, &watch, changed, engaged, &subscription) == AZ_OK ? 0 : -1;
}
```

Pass the mod ID, a size/version-tagged watch, a function in the mod, its user pointer, and an output handle. An error leaves the handle untouched. Register in init or a callback on the core UI thread. To stop, call `lib->deck_status_release(ID, subscription)` on that thread. You may release from inside the callback. Values, text and arrays belong to the core through the callback's return. Copy anything you want to retain.

## Reading events

- **INITIAL** carries one full selected view per deck. A mod joining during playback can display an established value immediately. A verified producer still awaiting its first observation reports loading.
- **CHANGE** identifies the selected topics and fields that changed. The full view reflects that publication. For example, `event->view->loop->retained_out_info.info.availability` says whether OUT exists. Test `loop->header.changed & az_status_field_bit(AZ_STATUS_LOOP_ENGAGED)` for an engagement change. Field selectors are local to their topic and the named bits are obtained with `az_status_field_bit`.
- **RESYNC** replaces the selected view after history loss. Inspect `lost_topics` and `loss_reason` and replace local state with the view delivered. The next changes are strictly after that cut.

Use named fields and their `*_info` receipts. EMPTY confirms absence, READY permits reading the value, and LOADING, STALE, FAILED and UNSUPPORTED are separate states. A valid IN at zero and a disengaged loop both carry real values. The optional readers preserve their outputs on failure. `az_read_event_loop_point` distinguishes retained IN/OUT from the currently working bounds. Continuous head updates use the requested 1–30 Hz ceiling (0 selects 30 Hz). Discrete transitions can arrive between those updates. [No-tick loop example](https://github.com/Kyle-Hosman/az-mods-sdk/tree/main/examples/loop-watch)

When `LOAD.state` is READY with NATIVE provenance and equals `AZ_STATUS_LOAD_LOADED`, the event's nonzero `load_epoch` is the matched `native_load_generation`. A READY `LOAD.track` with `AZ_STATUS_LOAD_SCOPE` and `scope_id == event->load_epoch` supplies the qualified library identity for that load. The native generation alone does not establish a USB or library association. INITIAL, CHANGE and RESYNC retain the epoch of the image they deliver.

To plan a native command from a retained status view, keep that qualified track and epoch, then read a fresh native load target with `az_read_native_load_target`. Require its generation to equal the retained epoch before planning and submitting commands. A replacement can become current before its status callback arrives; that mismatch must defer the old plan. Put the same generation in the command's `expected_load_generation` with `AZ_COMMAND_NATIVE_LOAD`, so the core checks the target again when executing it. The legacy `deck_snapshot.track` field has separate support and is not required for this join.

When `loop->mode_info` is READY, `AZ_STATUS_LOOP_MODE_TIME` means the working loop length was set in source samples. `AZ_STATUS_LOOP_MODE_BEAT` means it was set as a beat fraction. Read `loop->engaged` separately to find out whether that loop is playing. When `loop->reloop_available_info` is READY, `reloop_available` says whether both ends of the retained loop are ready for re-entry on this source. It can stay true after EXIT clears the working loop. SDK command completion reports whether a requested action actually ran.

Subscribe to SETTINGS for the Slip switch and to POSITION for the audible and background playheads. Slip can be enabled while the background position is EMPTY. During a Slip operation, a READY `position->slip_head` identifies where background playback has reached. Each coordinate carries its source sample count and sample rate.

Use `compare_timestamps()` for timestamp equality, ordering and range checks, and handle `AZ_TIME_INVALID` explicitly. Establish the source rate and observation window before comparing, then convert both values into the same clock domain. Equivalence uses an absolute difference strictly smaller than N/Fs seconds for an N-sample observation at rate Fs. The subscription's `position_hz` controls callback cadence. Native observation windows follow their producers independently. LOOP events and PHRASE_TRANSITION pairs already carry the core's confirmed boundary decisions.

The SYNC topic distinguishes the switch, master role, tempo following and beat lock. On an enabled follower, `sync->following` becomes true when the deck starts tracking the elected master’s tempo. `sync->locked` becomes true when the audible beat-alignment job locks to that master. A listener can see following become true before lock, or continue following tempo when beat lock drops. Read each field only when its own receipt is READY.

The KEY topic preserves the original track key alongside the applied semitone shift and effective key. A mod displaying what the deck currently plays can use `key->effective` when its receipt is READY. The original and effective keys have independent receipts.

## Track analysis and phrase transitions

Use CUES, GRID, PHRASES and OVERVIEW for loaded data. An immutable callback can contain hot and memory cues, beat positions and tempo, phrase beat ranges and a bounded saved waveform overview. Saved and current data have separate receipts. Always check load, provider and revision before comparing a cue or grid reference with another view. Use the explicit ticket APIs when browsing a library track independently of the loaded decks. [Stored track and waveform windows](13-track-data.md)

For a live cue, `AZ_CUE_SAVED` makes `saved` readable. It indicates whether the current committed file edition for this load contains the hot slot or a unique corresponding memory cue. `modified` is 1 for a known unsaved change or new cue, 0 when the applicable values match, and −1 when the comparison is unavailable. A file commit or removal can change these flags and advance the live collection revision. The live coordinates, color and comment still come from the native cue owner. Use the saved collection's receipt for the stored values.

Subscribe to PHRASE_TRANSITION to follow the audible head through the current phrase map. Each transition carries its departure and arrival together. A seek over a phrase produces the direct pair. A wrap within one phrase produces no pair. INITIAL and RESYNC provide the active endpoint with a null transition. START, ITEM, GAP and END are distinct endpoint kinds. A map change at a stationary head can update active state without a crossing. The phrase reader copies a validated transition before the callback returns.

```c
#include "azmod-data-helpers.h"

static void phrase_changed(const az_deck_status_event_v1 *event, void *user)
{
    az_status_phrase_transition_v1 transition = {0};
    transition.size = sizeof transition;
    if (az_read_phrase_transition(event, &transition))
        ((az_status_phrase_transition_v1 *)user)[event->deck] = transition;
}
```

The library inspector’s **Live decks** page follows the selected logical deck’s transport, working loop, cue collections, grid, phrases and overview. Turn the knob to change deck and click to change page. Its USB pages inspect a separately selected library track. [Deck-status firmware and source evidence](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/CORE-SDK-DECK-STATUS.md)
