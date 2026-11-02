# MIDI command dispatch

`BindingModel` is message-thread owned and publishes a fixed table to the audio-thread `Scanner`. Scanning remains bounded to 128 relevant MIDI messages and 32 command events per block; held bindings are released when the table changes. Existing command numbers 1–32 are unchanged; factory `PresetGoto` is appended as 33.

`Cmd` schema 2 adds one mutually exclusive automatic Program Change target: Off, Scenes or Factory presets, with a channel (0 means Any). Schema 1 migrates its original scene enable/channel unchanged. Explicit Program Change bindings take precedence over the automatic target, so one incoming message cannot also recall a scene. This target is stored with the existing command table, rather than maintaining conflicting controls in both `Perf` and `Cmd`. Factory programs outside the processor's actual program count are ignored on the message thread.

Controller JSON schema 1 retains its original command-key set. Schema 2 adds SkipLatest, SkipRecipe and PresetGoto; export stays at version 1 unless an extension is used. Bindings and LED feedback continue to use the existing size limits, validation and Standalone-only device handling.

`LiveControl` retains immediate Panic/Recover and the existing one-block queue for other MIDI commands. `setFeatureCommandSink` is installed while audio is stopped. It receives each non-live MIDI press exactly once on audio; GUI command dispatch never invokes this sink because the processor already dispatches those commands to feature modules. The processor must route the sink through the same explain, capture, FX, engine and context handlers without re-entering `LiveControl`.

Recipe commands require `RecipeTargetSnapshot`, a fixed copy of at most eight history recipes published by the message thread whenever history changes, including restore/clear. The audio side adopts the snapshot at the block boundary and captures its generation with queued MIDI events. Missing, invalid or changed-generation targets are rejected and counted instead of addressing a different recipe at a reused history index. `PreviewRecipe` resets its separate gain-trim index to zero after resolving the MIDI history index.

Tests: CommandMap and Controllers cover channel filtering, precedence, migration, schema compatibility, payload/generation rejection, delayed sink dispatch and GUI duplication protection. Native processor execution and physical MIDI/LED acceptance require their actual build/host/device evidence; syntax checks and small JUCE-backed probes do not establish those results.
