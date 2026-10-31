# Opt-in extended macros

This module implements the detailed F-M23 mode beside the released four-macro
engine. Legacy MacroConfig, all36 target indices, its four LFOs, codecs and
schemas1/2 entry points retain their behavior. Explicit MacroModel enable/return
switches mode through one ConfigSnapshot publication; the old configuration is
retained, including its original target engine, mappings, names and LFOs.

Sources.cpp evaluates two global random-access LFOs and additive signed Energy
from exact decision positions. ExtendedState.cpp owns a bounded8KiB MacrosV3
codec with16-byte little-endian float records. Targets.cpp owns the48 physical
and virtual target conversions; indices0..35 remain shipped identities,
36..46 append missing domains, and47 controls the new detailed Tape amount.
Runtime.cpp resolves at most64 claims first, then applies Tape selector47 before
mix42 and the other targets, independent of mapping order. Lower macro wins.

Runtime configuration and inputs belong to the audio thread. LiveControl must
pin config/source inputs once in preflight, project an applicable new scene
through OverlayView::applyModulationTo, and choose the control split from those
same inputs. Gathering must not consume a second publication or reread sources.
Knob/static width does not force extra cuts. Modulated continuous targets use
32-sample output-clock cells, evaluated at their canonical midpoint. The caller
obtains analysis Energy at that exact sample. Width rejects Energy even when
host automation bypasses the UI rule, and uses the existing20ms output smoother.

Effective's block adapter installs staged inputs and ownership without applying
V3 decision targets early. Runtime::projectContextTargets temporarily supplies
Tension/Energy/Scale/Groove amounts before ContextState.onChunk; restoreContextBase
then restores nominal targets while retaining derived tracker results. Decisions
apply Scene/Battle then V3 macros before Evolve/Breathing/Tension/Phrase. A mapped
EnergyAdapt keeps its tracker demanded even at instantaneous zero.

The scheduler must retain raw FX in V3. Continuous bus preparation uses a separate
settings copy; decision copies apply the combined Tension and macro send once to
voice FX. Schemas1/2 retain their existing block-level mutation. New Tape send
scales its independent mix rather than event probability; target42 selects the
effective detailed path when target47 enables it, otherwise its legacy wet
fallback. The new PSOLA mix is scaled independently of its mode and pitch-choice metadata.

Evaluation has fixed loops, no allocation, locks, publication, framework/host
access or mutable oscillator/RNG clock. Startup resolves field references before
audio. Config/source publication is pinned by the caller; absent-module targets
are inert through the availability mask. Every lifecycle event recomputes source
phase from PPQ/outputPos; only external width/card smoothers retain audio state.

Tests: TestModulation covers source clocks/ranges, exact codec bytes/maximumsize,
legacy return, typed domains, selector precedence, context restore, pinned control
cells and allocation. TestMacroV3 adds processor project/bank persistence;
TestModulationPipeline checks decision ordering/send composition. Pure tests do
not establish live preflight, processor frame/timing wiring, card interpolation,
real host operation or performance acceptance; those require integrated evidence.
