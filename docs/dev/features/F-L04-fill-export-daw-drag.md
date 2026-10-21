# F-L04 Fill Export / DAW drag
Status: implementation spec, Gate C candidate (backlog `08` F-L04, existing ticket U-2). Category VAL, value H, size L. Canonical: `05` U-2 (WetTap ring, `ExportSegment` command, `exportState` handshake) and `10` section 4.5 (wet-only content, metadata, worker thread). This file resolves their differences, defines the drag-out mechanics, temp-file lifecycle, memory protocol and WAV metadata that neither spec fixes.

## 1 Summary & user-visible behaviour
- **Export fill...** (FILE menu and History chip menu, F-L02) saves the wet-only audio of the selected fill (or the latest fill/preview if none selected) as a WAV: 24-bit PCM by default, session sample rate, main-bus channel count, 20 ms lead-in, fill body, and the voice's fade-out tail. Default name `GapFill_<Algo>_<bpm>bpm_<yyyyMMdd-HHmmss>.wav`.
- **Drag handle** on each chip (and in the export result card): press and drag out of the plugin window to drop the file on a DAW track or file browser. The file already exists on disk before the drag starts (it is exported to a temp file first).
- Content is exactly what GapFill generated: post Low/Centre Protect and width, pre Dry/Wet mix and Output trim, no dry signal (10 section 4.5). Aligned to the fill, not to the dry path latency.
- Messages (footer status, 4 s): "That fill is too old" (older than the 8 s ring), "Export busy", "Start playback to export", "Disk error: <reason>", "Drag not supported by this host - use Export".
- Exporting never changes sound, latency, the live recipe or the saved project state.

## 2 Success/quality metric
- Sample accuracy: exported samples equal an offline capture of the wet bus for the same fill with max abs error <= 1 LSB of the chosen bit depth (24-bit: 1.2e-7), start offset error 0 samples.
- No dry leakage: dry-only fixture (fills off) exports -> "Nothing generated"; with fills on, correlation of export with dry input <= the correlation of the wet capture itself.
- Time budget: message-thread export of a 2 s stereo fill completes (file closed) in < 250 ms at 48 kHz on the reference machine; audio-thread added cost while armed < 0.5 % of a 64-sample block, 0 when disarmed (counter test).

## 3 Design
Files (`cmake/sources/export.cmake`, `Tests/features/TestExport.cpp`):
- `Source/features/export/WetTap.{h,cpp}`: 
  ```cpp
  class WetTap {
  public:
      static constexpr double ringSeconds = 8.0, maxSegmentSeconds = 4.0;
      bool allocate (double sampleRate, int channels);                 // MESSAGE thread only; idempotent; ~12 MB at 192k stereo
      void release();                                                  // from releaseResources/destructor only (audio is stopped)
      void arm() noexcept; void disarm() noexcept; bool isArmed() const noexcept;  // atomics
      void write (const float* const* ch, int numCh, int start, int n, std::int64_t outputPos) noexcept;  // audio, no-op unless armed && allocated
      bool request (std::int64_t startPos, std::int64_t lengthSamples) noexcept;  // false if busy / too old / too long
      void pump (std::int64_t latestWritePos) noexcept;                // audio: copy <= 4096 samples/block into exportBuffer
      enum class State : std::uint8_t { Idle, Copying, Ready, Failed };
      State state() const noexcept;                                    // acquire load
      bool takeResult (juce::AudioBuffer<float>& out) ;                // MESSAGE thread; copies then sets Idle
  };
  ```
  Memory protocol: storage is allocated once on the message thread the first time the editor arms the tap and published with a release store of `allocated`; the audio thread reads it with acquire. It is never freed while audio may run (freed in `releaseResources`/destructor). Disarm only stops writes and copies; the 12 MB stays resident after first use (documented; counts against the `06` +12 MB armed budget). One request at a time; request fields are written before the `Copying` release store.
- `Source/features/export/SegmentLocator.{h,cpp}`: from a `FillEvent` (`startPos`, `lengthSamples`, `fadeOutSamples`) compute `[startPos - leadIn, startPos + length + fadeOut)` clamped to the ring; `leadIn = round (0.020 * sr)`. History entries carry `outputPos` (F-L02), so a reloaded project entry is rejected as too old.
- `Source/features/export/WavWriter.{h,cpp}` (message thread or `juce::ThreadPool` job): `bool writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sr, int bits, const ExportMeta&, juce::String& error)` using `juce::WavAudioFormat::createWriterFor (std::unique_ptr<OutputStream>, options)` with metadata `StringPairArray` (BWF `bwav description`/`originator`, RIFF INFO software/comment: "GapFill <ver>; algo=..; seed=..; sr=..; bpm=.."). Confirm the exact metadata key constants against the pinned JUCE 8.0.15 `juce_WavAudioFormat.h` before coding. Metadata excludes host project paths, user name and dry data (10 4.5). Also writes `ExportMeta { sampleRate, channels, bpm, ppqStart, ppqEnd (if valid), seed, recipe, pluginVersion }`.
- `Source/features/export/TempFiles.{h,cpp}`: temp folder `<tempDirectory>/GapFill-export-<instance id>/`; keep <= 16 files; delete files older than 24 h on first use per process; delete this instance's folder in the destructor; names are generated, never from user text.
- `Source/features/export/DragSource.{h,cpp}` + `Gui.{h,cpp}`: drag handle `juce::Component` that on `mouseDown` starts the export to temp (async) and on the first `mouseDrag` after the file is ready and the pointer has left the handle by > 6 px calls `juce::DragAndDropContainer::performExternalDragDropOfFiles ({ path }, false, this, onDone)`. The editor derives from `DragAndDropContainer` (top-level requirement). If the file is not ready the drag is deferred (cursor "busy" text), not started with a missing file. Research notes: JUCE forum reports that `performExternalDragDropOfFiles` from a plugin editor works when called from `mouseDrag`, repeated `mouseDrag` calls must be guarded, behaviour differs across hosts/formats (https://forum.juce.com/t/implementing-drag-drop-from-juce-to-daw/55905, https://forum.juce.com/t/performexternaldragdropoffiles-not-working-in-ableton/42675); treat DAW drop support as a per-host QA row, not a guarantee.
Data flow: audio `write()` per chunk from the post-width wet buffer in `processChunk` (after the width loop, before the mix loop) -> `request()` from message thread -> `pump()` each chunk until Ready -> message thread `takeResult` -> worker writes WAV -> status/drag.
Minimal hooks in existing files: two lines in `processChunk` (`wetTap.write`, `wetTap.pump`), `releaseResources` (`wetTap.release`), FILE menu item, editor derives from `DragAndDropContainer`.

## 4 Parameters & state
No APVTS parameters. Preferences (bit depth 24-bit PCM / 32-bit float, last export folder) live in the global settings file (U-4 `settings.xml`) so they are per user, not per project; project state gains nothing. Neutral proof: tap disarmed -> `write()` returns on the first line; golden A-F bit-identical; `exportState` never touches audio buffers. Old sessions: unaffected.

## 5 Real-time safety
Audio: when armed one bounded `memcpy`-style copy of the wet block into the ring (<= block size x channels), `pump` copies <= 4096 samples/block into the preallocated `exportBuffer`; nothing else. No allocation (storage preallocated by the message thread), no locks, no I/O. Failure modes: sample-rate change or `prepareToPlay` while Copying -> state `Failed` ("Export cancelled"), ring contents invalidated by bumping a `generation`. Transport stop with a stalled callback: request stays Copying; message thread times out after 3 s using its own clock and sets `Failed` through a `cancel()` atomic. Arm/idle: disarm after 60 s without UI use via the editor timer. Bypass: tap still writes zeros-or-wet consistent with the wet buffer (no special case).

## 6 UI
FILE menu "Export fill..." plus chip menu and drag handle (F-L02 strip). Handle: grip icon with text "Drag" at normal width, icon only when narrow; tooltip "Drag the audio of this fill into your DAW (24-bit WAV, fill only, no dry signal)". Keyboard has no drag; the Export menu item is the accessible path (documented in Help). Accessible name "Export fill audio" / "Drag fill audio to host". Reduced Motion: no drag-ghost animation beyond the system cursor. Errors in the footer status line. Help section "Export fill" (order 1130) with the host-support table filled in from QA.

## 7 MIDI/automation/CLAP/host impacts
No parameter, no latency/PDC change. Drag-and-drop is host/OS dependent (Ableton, FL Studio, REAPER, Bitwig, Logic differ; some hosts accept files only on certain lanes); standalone drags to the OS. AU/VST3/CLAP use the same editor code; on macOS sandboxed hosts the temp folder must be the app-writable `NSTemporaryDirectory` that `File::tempDirectory` returns. Export during offline render is not meaningful and is disabled when `isNonRealtime()`.

## 8 Tests
Group `Export`: `export_segment_matches_wet_bus` (offline capture compare) | `export_disarmed_zero_cost` (write counter 0) | `export_too_old_rejected` | `export_too_long_rejected` | `export_bounded_copy_per_block` (<= 4096) | `export_busy_rejected` | `export_samplerate_change_cancels` | `export_ring_wraparound_exact` | `export_mono_main_one_channel` | `export_contains_no_dry` | `export_lead_in_and_tail_lengths` | `export_wav_header_valid_24bit_and_float` | `export_wav_metadata_has_no_paths` | `export_disk_error_reported` (read-only folder) | `export_cancel_on_timeout` | `temp_files_capped_and_aged_out` | `export_no_audio_thread_allocation` | `export_tsan_request_vs_pump` (TSan job). Manual matrix: drop onto a track in REAPER (Win/Linux), Ableton Live, FL Studio, Bitwig; note result per host in `docs/progress/F-L04.md`.

## 9 Dependencies on other tickets
Hard: B4 (queues), F-L02 (chip with `startPos`/`outputPos`), event carrying `lengthSamples`/`fadeOutSamples` (add to `FillEvent` in F0-4). Soft: E-1 (preview export), F-S12 `settings.xml` from U-4. Shares the post-width wet tap point with F-L12 (the wet bus reads the same buffer; neither mutates it). Build order: after F-L02; independent of F-L05..F-L13.

## 10 Effort & risks
Effort L. Risks: host drag support is uneven and unverifiable in CI; 12 MB resident after first arm at 192 kHz; temp-file leaks on crash (mitigated by age-out); metadata key availability in JUCE; users expecting dry+wet stem export (explicitly not offered).

## 11 Acceptance checklist
- [ ] Export equals offline wet capture within 1 LSB; no dry content
- [ ] Disarmed tap costs nothing; arm/disarm lifecycle tested
- [ ] All disk, busy, too-old, rate-change, timeout paths give a message and leave state unchanged
- [ ] Temp files created, capped and cleaned; no user-controlled filenames
- [ ] Drag works in at least REAPER on Windows and Linux; other hosts recorded as pass/fail/unknown
- [ ] Keyboard path (Export menu) works; names and tooltips present
- [ ] AllocGuard and TSan tests green; golden unchanged
