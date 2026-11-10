# Loop Watch

Loop Watch registers one callback for all four logical decks during initialization. It records retained IN and OUT independently from working engagement. An IN at zero is valid, and EXIT can leave both retained points in place. With mod logging enabled, set IN and OUT, change the loop length, then press EXIT and RELOOP. Each observed change carries its deck, load and publication sequence. A recovery event means the listener received current values after a history gap.

Build with `tools/azmod-pack build examples/loop-watch` from the SDK root, then run `tools/azmod-pack package examples/loop-watch`. Install through the normal Community Mods loader on a deck-status-capable core version 1.2.6 or later. The mod has no periodic tick, screen, worker, device hooks or deck commands.
