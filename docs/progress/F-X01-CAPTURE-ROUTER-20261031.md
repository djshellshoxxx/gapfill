# Capture-aware pitch source routing —2026-10-31

`PitchSourceRouter` owns one incremental detector and three immutable256-frame
banks (original frozen history plus two recapture destinations). The complete
router is below40KiB and performs no PCM catch-up. Source/core code is independent
of JUCE, CaptureLatch, FillVoice and host threads.

## Adapter contract

After the history writer and CaptureLatch have processed a chunk, pass the exact
mono samples written to the selected analysis writer and a `PitchSourceChunk`:

- `liveFirst`: first coordinate in that actual writer. Legacy mode can use the
  always-recording tap; detailed Capture may use normal history while released
  and a newly started tap while held. Discontinuities reset only the detector.
- `historyFirst`: first newly written normal-history coordinate, or its frozen
  next-write position when held. `historyWritten` is false whenever paused.
- `held`: actual effective Capture selection, including suppression/readiness.
- `copy`: most recently started copy's nonzero monotonically unique generation,
  target index0/1, inclusive live start and positive total length. Keep it visible
  when a copy starts and completes in one update; the bank must freeze at start.
- `activeSnapshotIndex/Generation`: actual selected audio snapshot identity. A
  missing/mismatched bank returns unknown; it never falls back to the old source.

The router freezes old history before pushing the first held input. A new copy
freezes after that callback's actual writer push, restricts frames to the copied
range and remaps endpoints to0..total-1. Further copy callbacks cannot change its
bank. Reusing index0 after index1 cannot resurrect an older generation. Cancelled
copies are never activated by the adapter; a failed overwritten copy must retain
the previous source identity. No peer/component pointer is stored.

Normal release starts a new affine history/analysis segment and rejects query
ranges crossing the join. Frames require one full512+tauMax decimated analysis
extent plus128 decimated lowpass-settling samples within the new source. A mode
enabled after an existing hold has no old PCM analysis; it correctly returns
unknown until a newly analysed recapture. This avoids an unbounded audio-thread
backfill and prevents incoming pitch from being assigned to unrelated held PCM.

The engine must derive both the PCM capture and inclusive pitch-query window from
the same actual selected SourceHistory/Scan anchor. Its latest sample is
`nextWrite-1`, not `nextWrite`. Existing frozen/snapshot read floors remain owned
by CaptureLatch. Query/frame methods are audio-only; publish separate atomics to
the UI. Root owns CaptureLatch tickets, overwrite detection and engine hooks.

## Verification

Eight strict standalone router cases pass, also under AddressSanitizer and
UndefinedBehaviorSanitizer (leak checks disabled for ptrace): held220Hz vs incoming440Hz, copy-start
freeze while incoming880Hz, remapped/ABA/missing identities, quarantined release,
held-only tap restarting at0, cold-held enable, full two-second early/late Scan
windows at44.1/48/96/192kHz and block32/64/512/4096 partition equivalence. Native
AllocGuard adds the ninth case. The complete native core runner passes42/42:
tracker10, router9, metadata4, epochs5, intervals4 and voice10. New128-decimated snapshot cadence retains all
two seconds at all four rates and keeps8192MAC/16lag limits. Updated tracker
accuracy passes1212/1212 voiced frames within20c, zero octave errors (worst2.24c);
unpitched cases exceed90% rejection (noise168/168, pink168/168, kicks165/168 and
hats168/168). Native tracker CPU is0.312% of one core with the unchanged0.5%
assertion enabled; managed-host timing is component evidence, not reference-host
certification. Whole-plugin routing remains adapter/native
integration work; these component results do not claim host validation.
