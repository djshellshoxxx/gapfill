# F-L21 Transient-derived convolution reverb

Status: specified, not approved. Size M-L. Category FX/VAL, value M.
Overlap: none in `03`-`05`/`10` (no convolution there). Related: F-M19 freeze/scrub and F-X02 spectral freeze make textures from one frame; this makes them by convolving the fill with a hit from the track. Plugs into the F-L14 wet-stage seam.

## 1 Summary and user-visible behaviour
The plug-in lifts a short slice at a detected hit (a snare, a clap, a vocal consonant) out of the incoming audio, shapes it into an impulse response (IR), and convolves the generated fill audio with it. The result is a reverb or texture that has the character of the track itself. Modes: Last Hit (auto-follow the most recent suitable hit), Strongest (best hit in the last 4 s), Manual (only when "Capture IR" is pressed). Hold keeps the current IR. Before any IR exists a deterministic built-in room IR plays so the control is never silent. Wet fill audio only; Mix 0 = off.

## 2 Success / quality metric
- Mix 0: bit-identical to baseline.
- Correctness: partitioned output equals direct convolution of the same IR within -120 dBFS error (float) for random IRs and inputs.
- IR swap: no step above -50 dBFS at the 50 ms equal-power crossfade; audio-thread block time during a swap at most 1.6x the steady-state block time (two engines running).
- CPU: at 48 kHz stereo with the maximum IR (800 ms), at most 3% of one core steady; at 192 kHz maximum IR length is capped by the partition rule (3.4) so CPU stays within 2x of the 48 kHz figure.
- Output bounded: `|y| <= 1.5` after the stage limiter for any IR/input at full scale.

## 3 Design
Files: `Source/features/convreverb/IrCapture.{h,cpp}` (selection and shaping, pure), `PartitionedConvolver.{h,cpp}`, `IrPreparer.{h,cpp}` (worker), `ConvStage.{h,cpp}` (implements `wetfx::WetStage`), `Gui.{h,cpp}`, `Help.cpp`.

### 3.1 IR capture (careful selection)
Source of hits: the existing `AnalysisEngine` main-lane events (`mainEvents`, with transient strength). A candidate is accepted when: transient strength >= 0.5; the 30 ms before the onset has peak at least 20 dB below the hit peak (isolated); the following 120 ms does not contain a stronger event (no flam/double); the source is not clipping (peak < 0.99). Mode Last Hit: newest accepted candidate; Strongest: highest strength in the last 4 s (ring of 8 candidate descriptors `{pos, strength}`, no audio); Manual: the `CaptureIr` command takes the most recent accepted candidate (if none: status "No suitable hit yet"). Hold: ignore new candidates.
The audio thread copies the slice `[onset - 2 ms, onset + irLength]` (stereo, from `SourceHistory`, which is read-position clamped) into a preallocated staging slot (max 2 x 0.8 s x fs; at most 4096 samples per block incrementally, as C-10 does) and posts `IrRequest {slot, length, params}` to the worker via `SpscQueue<IrRequest, 4>`. Candidates whose slice is no longer in history are dropped.

### 3.2 IR shaping (worker thread, off the audio thread)
In order: remove DC; align the peak to sample `preDelay` (0..50 ms); multiply by a fade-in of 2 ms and a fade-out `w(t) = (1 - t/T)^(1 + 3 decay01)` over the last 70% of the length (raised so the tail ends at exactly zero); low-pass at `damp` Hz (2nd-order, applied forward-backward = zero phase); optional mono fold (`fl21_stereo`); normalise to L2 energy 1 (`h /= sqrt (sum h^2)`), then limit the l1 norm: `h *= min (1, 16 / sum |h|)` as a safety against pathological IRs; verify finite. Result is partitioned (3.3) into a spare `ConvEngine` (set B), then `IrReady {slot, generation}` is posted back.

### 3.3 Convolution
Uniformly partitioned frequency-domain delay line (overlap-save), partition size `B` per 3.4, FFT size `2B` (`juce::dsp::FFT`), `P = ceil (len / B)` partitions per channel. Per `B` input samples: 1 forward FFT, `P` complex multiply-accumulates over `B+1` bins (frequency-domain delay line ring), 1 inverse FFT. Complexity per second per channel `~ (fs/B) * (2 * 5 * 2B * log2 (2B) + 8 * P * (B+1))` flops. Example 48 kHz, B = 512, 0.8 s IR (P = 75): about 38 MFLOP/s per channel. The stage adds a fixed pre-delay of `B` samples (10.7 ms at B = 512), which is part of the reverb character; reported plug-in latency is unchanged because the stage output is wet-only. Two engines (A active, B spare); on `IrReady` the audio thread starts a 50 ms equal-power crossfade A to B at the next block boundary, then swaps roles. A new `IrReady` during a crossfade replaces the pending spare only after the crossfade finishes (latest wins). Output scaled by `mix`, passed through `softclip` (tanh, ceiling 1.5) and `sanitise`.

### 3.4 CPU control
Partition size by sample rate: B = 512 (fs <= 52 kHz), 1024 (<= 100 kHz), 2048 (<= 200 kHz). Maximum partitions 96, so `irMaxSeconds = min (0.8, 96 B / fs)`: 0.8 s at 48 kHz, 0.8 s at 96 kHz (B 1024: 1.02 s cap), 0.8 s at 192 kHz (B 2048). The UI Length knob maximum follows this cap. `quality` from `StageContext` (F-L25): 1 halves the active partitions (truncates the IR tail with a fade), 2 disables the stage (fade 20 ms).

### 3.5 Fallback IR
Deterministic exponentially decaying noise: `Rng (0x5EED21)`, T60 = irLength, stereo decorrelated, shaped like 3.2. Used until a captured IR is ready and when capture is Manual and none exists.

## 4 Parameters and state
Group `FL21`, `tools/param_ids/fl21.lock`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl21_mix` | float 0..100 | 0 | % (0 = off) |
| `fl21_source` | choice Last Hit, Strongest, Manual | Last Hit | |
| `fl21_hold` | bool | off | |
| `fl21_length` | float 50..800 | 250 | ms (clamped by 3.4) |
| `fl21_preDelay` | float 0..50 | 0 | ms |
| `fl21_damp` | float 2000..20000 log | 12000 | Hz |
| `fl21_decay` | float 0..100 | 40 | % (tail shape exponent) |
| `fl21_stereo` | choice Stereo, Mono | Stereo | |
Command (not a parameter): `CaptureIr`. IR audio is never serialised (F0-6); after loading a session the fallback IR is used until a hit is captured (status "Using built-in room"). No subtree. Neutral proof: mix 0 => stage not called; test compares wet-bus hash. Old sessions load unchanged.

## 5 Real-time safety
Audio thread: candidate bookkeeping, incremental slice copy (bounded 4096 samples/block), queue operations, block convolution and crossfade. All FFT plans, `ConvEngine` spectra (2 sets x channels x P x (B+1) complex floats; at 48 kHz maximum about 2 x 2 x 75 x 513 x 8 B = 1.2 MB) are allocated in `prepare`. The worker thread (`juce::Thread`, low priority, started in `prepare`, joined in `releaseResources`) performs shaping and partition FFTs; it touches only its own slot and the spare engine, handed over by the SPSC queues (single producer/consumer each, release/acquire). No locks, no allocation, no IO in `process`. Worker stalls only delay IR updates; audio keeps the old IR. Sample-rate change: stop worker work, re-prepare, fallback IR. Denormals flushed in accumulators.

## 6 UI
Slot `CreativeExtra`, card "Hit Reverb" (resonant/delay group). Controls: Source combo, Hold toggle, Capture IR button (enabled in Manual), Length, Pre-delay, Damping, Decay, Stereo, Mix, and a small IR thumbnail (peak envelope, 128 points, updated from a message-thread copy on `IrReady`; text alternative "IR: 240 ms, captured 3 s ago"). Status: "Using built-in room" / "IR from snare, 1.2 s ago" / "No suitable hit yet". Tooltips: Source "Which hit from your track becomes the reverb." Help section order 1210 "Hit Reverb". Accessible: thumbnail has a text description; the Capture button announces the result. Keyboard standard. Reduced motion: thumbnail updates without fade. Narrow: thumbnail hidden, status text kept.

## 7 MIDI / automation / CLAP / host
Parameters automatable; `CaptureIr` bindable through F-L24. Tail <= IR length + B, below the 2.5 s reported tail. Offline render: capture decisions are deterministic functions of the audio; the worker result timing must not change output, so in non-realtime mode `IrPreparer` runs synchronously inside the next block boundary (flag `nonRealtime`), guaranteeing identical renders. No latency change.

## 8 Tests (`Tests/features/TestConvReverb.cpp`)
`partitioned_equals_direct_conv_random` (B 512/1024/2048, lengths 1..max) | `unit_impulse_ir_is_pure_predelay` | `candidate_selection_rules` (isolation, double hit, clipping, weak hit) | `strongest_over_4s` | `manual_capture_without_candidate_reports` | `shaping_dc_removed_fade_ends_zero_l2_normalised` | `l1_limit_applied` | `ir_swap_crossfade_no_click` | `swap_block_time_ratio` | `latest_wins_during_crossfade` | `worker_stall_keeps_old_ir` | `nonrealtime_render_identical_twice` | `mix0_bit_identical` | `fallback_ir_deterministic` | `sample_rate_partition_table` | `partition_cap_96` | `quality1_halves_cost_quality2_fades_off` | `panic_clears_tail_within_25ms` | `no_audio_thread_allocation` | `state_roundtrip_old_session`. Golden: render of a drum loop with its own snare as IR (committed WAV, tolerance 1e-5).

## 9 Dependencies and order
F-L14 seam; F0-4 (`SpscQueue`), F0-6, F0-9. Uses `AnalysisEngine` events (read-only). Order after F-L15/F-L19 are profiled (CPU baseline), as `10` section 6 requires heavy DSP last. F-L25 may shed this stage first.

## 10 Effort and risks
M-L. Risks: worker lifetime and join on destruction in hosts that tear down quickly (test with 30+ create/destroy cycles); memory at 192 kHz (cap table keeps it under 2 MB); hits that are not impulse-like produce smeared rather than reverberant sound (an expected character, Help explains); L2 normalisation makes tonal inputs loud, so the stage limiter is mandatory.
References: Wefers/Vorlander and DAFx partitioned-convolution papers (e.g. dafx.de archive 2011 paper 74) for uniform vs non-uniform partitioning; non-uniform partitioning is out of scope.

## 11 Acceptance checklist
- [ ] Convolution error test passes; mix 0 hash identical
- [ ] IR swap click-free; swap CPU ratio within 1.6x
- [ ] Worker starts/stops cleanly across 30+ plug-in create/destroy cycles
- [ ] Non-realtime renders are bit-identical across runs
- [ ] CPU and memory budgets met at 48/96/192 kHz
- [ ] Status strings cover no-IR, fallback, manual-without-hit
- [ ] UI reviewed (min size, keyboard, text alternative for thumbnail)
- [ ] IDs locked; legacy state loads unchanged
