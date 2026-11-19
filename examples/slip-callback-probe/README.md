# Public Slip status

This example subscribes to the public SETTINGS topic and logs the qualified Slip
enabled setting. Unsupported, invalid and unavailable states remain `enabled=-1`.
The receipt's scope and identity belong to the supplied status field; a setting
observation does not establish playback activity or a loaded track association.

Native scalar capture, exact firmware bindings and forwarding tests live in the
core's `src/azmod/slip_diagnostic` tooling. This mod installs no native hooks and
never reads native objects. The core diagnostic's raw scalar records and this
client's logical-deck setting records have different meanings.
