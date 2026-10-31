# Detailed opt-in Tape Damage — 2026-10-31

The new `fm20_*` path is independent of the released `td*` processor. The pure
stage is implemented; production Voice integration and UI are separate slices.
Amount 0 and replay with absent optional metadata return inactive without creating
new decisions. All nine controls are quantised to 0.01 percent and dequantised
before the original render. Replay uses that exact 28-byte decision, including a
failed chance; no FillRecipe flag or base 16-byte layout is repurposed.

`TapeDamage::prepare` owns one interleaved stereo power-of-two line for 60 ms.
`begin` latches settings and pre-generates at most 64 merged events in 128 bounded
attempts. `process` allocates nothing, borrows no external pointer and has one
audio-thread owner. The first output is delayed by ceil(4 ms * sampleRate), with a
minimum 40 ms span. The caller must stop generating new source after
`sourceInputSamples()` and drain the stage for `latencySamples()` within the
original deadline, before any later FL20 drain. The plugin reports no new PDC.

The 4 ms centre and ±3.5 ms range cannot produce 40 cents at 0.5 Hz: the required
sinusoidal amplitude is 7.44 ms. The new path preserves requested depth by choosing
wow frequency from [max(0.5, (2^(cents/1200)-1)/(2*pi*2.4 ms)), 2] Hz. A constrained
random phase and initial-offset subtraction preserve the 4 ms starting delay and
2.4 ms wow budget. Flutter and drift reserve 0.65 ms and 0.35 ms; splices use a
bounded read delay of 0.5..7.5 ms. Fixed control-rate noise is linearly interpolated
with an analytic variance correction; the 6..14 Hz flutter has its own seeded stream.

The specified `tanh(drive*x)/tanh(drive)` has up to 6x small-signal gain and cannot
meet the specified +3 dB limit. The approved new curve is the JUCE Padé tanh
approximation divided by drive (latched 1/drive multiplication), with paired 3 kHz
pre/de-emphasis. This retains nonlinear shape and removes that gain. Equal-power
splice sums receive the small correction needed to stay at the exact +3 dB
coherent bound. The old `td*` curve is untouched.

Native GCC 14.2.0/O3 tests linked only the new stage/codec/test code plus existing
JUCE 8.0.15 module objects. Nine component cases pass: 4 ms onset at
44.1/48/96/192 kHz; off/absent replay identity; short/mix-zero fallback; canonical
metadata and chunk sizes 1/13/512/8192 bit equality; wow 12 c and 40 c within 10%;
flutter 32-seed RMS 15.0798 c against 15 c; 192 kHz event/ramp/delay/drift bounds and
AllocGuard; saturation peak/RMS +3 dB/DC < 1e-3 and exact silence; deterministic
dropout/splice edges with a signal-derived sample-step bound. Wow measured
12.088..12.101 c and 40.947..40.951 c. Across 16 seeds, isolated dropouts produced
141 events and splices 59 per rate; worst 44.1 kHz steps were 0.035648 and 0.056046
against the 0.085769 analytic bound.

CPU is **not accepted yet**. The four-active-instance 48 kHz strict target remains
0.6% of one core. An initial concurrent-run result of 1.5585% fell to
0.8459..0.8809% after interleaved storage, inlined tap reading and cached
coefficients. These are advisory measurements during other workers' native runs,
not a quiet reference pass. `GAPFILL_PERF_ENFORCE=1` enforces the 0.6% component
target. More optimisation and a scheduled quiet run are required. Whole-plugin
source drain, TapeStop/protection/FL20 stacking, golden X20, native host and visual
checks remain open until the coordinator records their evidence.

## Macro V3 fallback for the released card

The new Macro V3 Tape Mix target routes to `tapeExtra.mix01` while the new
Chance is above zero, and otherwise to the released card's `mixOverride01`.
The released card now applies that explicit nonnegative finite override only
to its final wet blend. Negative or nonfinite sentinels take the original two
expressions verbatim; Amount continues to control depth and disabling.

Two native cases prove independent dry/full/quarter blends at all four rates,
negative/nonfinite sentinel equality, Amount 0 identity and AllocGuard. Running
the same new tests against the actual `1432394` card source fails on the missing
dry override as expected. A separate comparison compiles the original source as
an independent renamed class: 8,552,250 floats match bit for bit across four rates,
three Amounts, three active control sets, five seeds and 64 versus 17-sample chunks.
This is an adapter for the opt-in Macro V3 target; it does not remap saved `td*`
parameters or enable the new Tape Damage stage.
