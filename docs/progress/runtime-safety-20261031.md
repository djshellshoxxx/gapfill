# Runtime safety work, 2026-10-31

Integration starting point: `1432394` (master `e89aecd`, reviewed PR #31, the
M0 acceptance pack and the finalization-plan document). Work belongs to the
isolated `codex/runtime-safety-20261031` branch.

## Calibration recording cancellation

The residual is in `features/calibrate/RecordTap`, not the export recorder.
Its public state could become Cancelled while an entered audio callback was
still using the recording. `allocate()` and `start()` treated that public
state as proof that storage and counters were free to replace.

A test-only hook and two barriers hold a writer after entering Recording.
After cancellation, the regression attempts a different-rate allocation or a
same-rate restart before releasing that writer. Both operations were accepted
by the old implementation; a standalone native probe reported both failures.

The fix uses a one-attempt, lock-free atomic ownership claim shared by buffer
replacement, recording start and the bounded audio copy. The audio thread
never waits, spins, allocates or touches a mutex. Cancellation still changes
the visible state immediately. A restart/reallocation while the old copy
owns storage returns false so the existing UI can report Try again. A second
state check after the audio claim handles a cancellation before entry. The
claim's RAII release also covers exceptions during message-thread allocation.

Reset behavior is unchanged. A rate change allocates only after ownership is
available, bypass/transport cancellation retains its state, and processor
shutdown still requires the host to stop callbacks before destroying it.
The tap does not require a future audio callback to acknowledge cancellation.

Verification so far:

- Native GCC C++20 probe: red on both old paths, green after the fix.
- The green probe also passed `-Wall -Wextra -Wpedantic -Wconversion -Wshadow
  -Werror`, AddressSanitizer+UBSan and a separate ThreadSanitizer build.
- LeakSanitizer cannot run in this execution runner (fatal ptrace restriction).
  The ASan+UBSan pass used `ASAN_OPTIONS=detect_leaks=0`; this is not a leak-check
  pass. No application-code sanitizer suppression was used.
- The checked-in regressions are
  `Calibrate.cancelled_tap_refuses_reallocation_until_entered_writer_returns`
  and `Calibrate.cancelled_tap_refuses_restart_until_entered_writer_returns`.
- Their translation unit passes C++20 syntax compilation against JUCE 8.0.15.
  A complete JUCE-linked test execution is still pending the coordinator's build.

## Editor lifecycle and real message-loop delivery

The normal `EditorLifecycle` case launches a fresh copy of the test executable
with `--message-loop EditorLifecycle`. In that child, the application's main
thread runs JUCE's public `MessageManager::runDispatchLoop()`. The test runs
on a coordinator thread and uses `callSync()` for processor/editor/preset/state
operations on the genuine message thread. Separate workers automate every
parameter and process the shared deterministic drum-loop input.

There are 200 create/resize/destroy cycles, 100 preset loads and 100 state
restores. Every live editor awaits actual 50 ms Timer delivery and an
AsyncUpdater callback; a queued SafePointer callback then runs after its
editor was destroyed. Callback/thread counts and finite audio are asserted.
The test asserts at least 10 kHz of parameter writes and prints the measured
automation throughput instead of assuming its rate.
The child has a ten-minute hang deadline. It cannot leave the parent suite's
MessageManager with a permanent quit flag, and it cannot recursively launch
another child because message-loop mode is explicit process-local state.

The production `JUCE_MODAL_LOOPS_PERMITTED=0` definition is unchanged. No
private JUCE dispatch function or OS-specific event-pump shim is used.

This code passes C++20 syntax compilation against the pinned JUCE source.
Linked runtime, native Windows/macOS, pluginval repeat runs and sanitizer
coverage of the complete GUI stress remain unverified until their commands
actually execute. A green test is not a root-cause repair claim for the
historical W-01 Windows crash.

Focused linked commands when the coordinator's build is ready:

```sh
xvfb-run -a <build>/artefacts/GapFillTests Calibrate
xvfb-run -a <build>/artefacts/GapFillTests EditorLifecycle
```

The second command exercises the child-process launcher as well as the stress
case. `--message-loop EditorLifecycle` can run the child driver directly for
debugging. The normal CLI filters and golden-writing paths retain their
previous behavior; message-loop mode requires an explicit filter.
