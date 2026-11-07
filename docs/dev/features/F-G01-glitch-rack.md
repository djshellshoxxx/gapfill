# F-G01 Glitch rack (GLITCH tab)

Status: implemented on `claude/zealous-gauss-7b4omr` (see IMPLEMENTATION-STATUS.md). Owner request of 2026-11-07
(DECISIONS.md 19). Parameter group `GLITCH`, prefix `fg01_`, 64 IDs locked in `tools/param_ids/glitch.lock`.

## Goal

A rack of rhythmic effects on its own tab: stutter, the STAB macro (with infinite operation), reverse, microgator,
swing/timing, HP/LP/BP auto-sweep, phaser, and phase/pan inversion. It must be neutral at its defaults, musically
usable, tempo-locked, block-size invariant and real-time safe.

## Placement and signal flow

- Target `Fills only` (default): the wet bus, after Low/Centre protect and width, **before** the wet safety limiter
  and the recipe/export tap. The dry signal is never touched, so GapFill's "protect the hits" contract holds.
- Target `Whole output`: the final main output after the dry/wet/output mix (skipped while auditioning the detector).
- Fixed order: Stutter -> STAB -> Reverse -> Microgator -> Sweep -> Phaser -> Phase/pan flip -> Mix.

## Modules and contracts

| Module | Controls | Behaviour |
|---|---|---|
| Rack | On, Target, Mix, Swing, Timing variation | Off (default) and fully faded = samples untouched (bit-identical). On/off, host bypass and module switches fade (20 ms rack, 10 ms per module). |
| Stutter (B) | Mode Off/Tempo/Free, Slice (division) or Slice ms, Repeats 1-16, Probability, Reverse, Variation, Slice pick, Gate, Decay, Smoothing | Windows of `Repeats` slices on the (swung) grid. A stuttering window captures its first slice live and repeats it; Variation repeats 1/2 or 1/4 slices inside the same window; Slice pick repeats one of the 4 previous slices. Every boundary is a raised-cosine crossfade. |
| STAB (I) | STAB (momentary), Infinite, Slice, Repeats 1-32, Decay, Gate, Release | Press = capture one slice live and repeat it. Finite: exactly `Repeats` repeats then back to live (one-shot). Infinite: repeats until STAB is released or Infinite is switched off. Re-press = fresh capture. Slice length latched at the press. Fixed slice buffer: no memory growth however long it runs. |
| Reverse (H) | Mode Off/Tempo/Free, Length or Time ms, Probability, Mix, Fade | Each segment plays the previous one backwards (a one-segment creative time shift; no host latency reported). Segment edges fade. After a seek the first segment is forwards. |
| Microgator (D) | Mode Off/Tempo/Free, Rate (division) or Free Hz, Length, Depth, Attack, Release, Pattern (7), Variation, Stereo | Gain gate per grid step with attack/release one-poles; patterns are 16-step masks (Random hashes per step); Stereo offsets the right gate up to half a step. |
| Swing/timing (C) | Swing 0-100 % (50 % -> 75 % pair split), Timing variation (up to +-10 % of a step) | Pure function of the musical position; applies to Stutter, Microgator and Rhythmic flip grids. No accumulated drift; same result after a seek. |
| Sweep (F) | Filter Off/LP/HP/BP, Sync, Cycle or Free Hz, Min, Max, Depth, Resonance, Shape (6), Stereo phase | TPT state-variable filter; cutoff = min * (max/min)^(depth * lfo); coefficients every 16 samples on the free sample counter; Square/Random glide 5 ms. |
| Phaser (G) | Mix (0 = off), Stages 2-12, Sync, Cycle or Rate, Depth (+-2 oct), Feedback +-95 %, Centre, Stereo phase | First-order TPT all-pass chain; tanh-bounded feedback; Mix 100 % = (dry + all-pass) / 2 for full notches. |
| Phase/pan (E) | Invert L, Invert R, Pan inversion 0-100 %, Rhythmic flip (Swap/Invert L/R/both), Flip rate, Flip probability | 2x2 matrix gliding over 5 ms. Polarity inversion, channel swap (pan mirror) and phase shift are distinct: the phaser is the only phase shifter. Inverting one channel cancels in a mono fold; both or swap are mono-safe. |

Timing: tempo modes follow the host ppq (shifted by the plug-in latency so the grid matches what is heard) while the
transport plays; when stopped they continue on an internal beat clock at the last tempo. Random decisions hash
(Seed, module salt, step index), so a render is repeatable and independent of the host block size.

## Real-time and safety

- All buffers are allocated in `prepareToPlay` (slice 2 s, reverse 4 s, lookback 4 slices; ~4.6 MB at 48 kHz).
- `process()` does no allocation, locking, logging or I/O (AllocGuard tests). Output clamped to +-8 and finite.
- Mono output buses fold the stereo result to 0.5 (L + R).
- Not randomised (no `fg01_*` in Random/Random All/Surprise). `fg01_stab` is performance state (never in Undo
  snapshots). Factory presets leave every `fg01_*` at its default.

## Acceptance checks (Tests/features/TestGlitchDsp.cpp, TestGlitchPlugin.cpp)

1. Off / on-with-no-modules render bit-identically to the input and to the pre-feature processor.
2. Output bit-identical at block sizes 1, 37, 64, 997, 4096.
3. Finite and bounded with extreme settings at 44.1/48/96/192 kHz for noise, impulse and silence (silence stays silent).
4. Stutter repeats the captured slice sample-exactly on the grid; reverse/gate/decay shape repeats; probability 0 = identity; Free mode.
5. STAB finite plays N repeats then live while still held; Infinite repeats 9 s later from the same slice with zero allocations; re-press grabs a new slice.
6. Reverse plays the previous segment backwards sample-exactly; probability 0 = identity.
7. Microgator open/closed levels at 4 rates x 2 block sizes; pattern, depth, swing and stereo offset.
8. Sweep LP/HP/BP responses at 4 rates; LFO covers min..max exponentially on the bar.
9. Phaser notch at fc * tan(22.5 deg) for 4 stages.
10. Polarity, pan swap and mono fold; rhythmic flip and module switching never jump.
11. Transport start/stop, seek and tempo change stay finite; rack switch-off becomes bit-identical after 20 ms.
12. Registry tail = lock order with ParamInfo; state round trip resaves byte-identically; GLITCH tab lists every ID once without overlap; tab or FILE menu entry.

## Not verified

Real DAW hosts, audibility/listening tests, Windows/macOS interactive use, screen-reader output of the new tab.
