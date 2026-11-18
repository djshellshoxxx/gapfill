# Additive detailed Beat Battles contract

The integration coordinator approved this contract on2026-10-31. Detailed
F-M18's creative-only Style1/Style2 model conflicts with the shipped doc12
Battle's A/B full-scope declaration. A separate BattleV2 implements the detailed
default and an explicit expanded-scope option; neither changes shipped behavior.

Existing battleEvery, BattleModel, BattleRuntime, old captures and decision
arithmetic remain the legacy path. No saved legacy style is automatically
expanded or activated. New host controls append after the current L21 tail:
fm18_on boolOff, fm18_unit {1,2,4,8} default2bars, fm18_first {Style1,Style2}
defaultStyle1. The coordinator reserves permanent indices295..297 and owns the
shared parameter registration. They are performance controls, MIDI-learnable,
excluded from Random/Surprise/Undo and in the new scene Musical scope.

The new message-owned Model(APVTS&) publishes one coherent fixed-size packet.
It contains selected detailed mode, the next-capture expanded option and two
styles. Default style capture contains exactly Fill/Intensity/Complexity/Chaos/
Density/FillType/PingPong plus15weights; an empty side reads live knobs. Explicit
expanded capture uses the current scene scope2 creative/weights/FX/musical
whitelist, excluding all Battle controls to avoid self-modifying selection.
Cloud/Groove model versions accompany expanded styles in their corresponding
domains. Taste/Arrangement remain session domains. Stored defaults are explicit,
so later knob changes never alter the stored side.

Detailed selected mode suppresses legacy Battle even while its On switch is
off. A new On automation value can explicitly activate the detailed runtime.
Both empty, Off, invalid/nonfinite transport or a non-power-of-two denominator
are byte-identical early-outs. Side=floor(PPQ/(bars*numerator*4/denominator))
plus First modulo2, including negative preroll. No RNG draw or mutable phase is
used. Status/countdown are atomic readouts; seek/reset recomputes from position.

Default creative styles apply only at a decision, after Personality and legacy
block macros. Extended MacroV3 then modulates the selected style before Evolve,
Breathing, Tension and Phrase. Default styles require no processor chunk split.
Expanded styles project the raw base before Effective/context and bus effects;
the adapter records a neutral battleV2BaseApplied flag to prevent reapplication
over the resulting macros. Splits occur only when an enabled expanded style
exists, exactly at the next window's first sample. Preflight pins configuration
and inputs once; processing never consumes a second configuration publication.
No APVTS writes occur on audio or while alternating styles.

Persistence uses a distinct optional BattleV2 tree, explicit version2 and
little-endian6-byte index/IEEEfloat32-normalized records, bounded8KiB escaped
XML. The full subtree validates before assignment; any bad version, shape,
duplicate side/index, nonfinite/range value or prohibited/out-of-scope id drops
the whole optional subtree and leaves the rest of the project loadable. New
readers accept absent data as legacy/default, while old readers ignore the new
type. This is project state; it is excluded from portable parameter files.
The aggregate optional-state budget remains384KiB and root stateVersion2.

Root owns registry/LiveControl/preflight/base/bus and Effective hot wiring.
The state owner supplies feature model/runtime/codec, descriptors, GUI/help/info
and tests. Initial root tails are Settings detailedBattle, const Runtime* battleV2
and bool battleV2BaseApplied=false in PerformanceSettings, plus Settings in the
pinned ModulationInputs. Their exact adapter signatures are reviewed before use.

Acceptance requires default legacy active audio identity; new empty/Off/invalid
memcmp identity; independent64-bar/meter/negative-PPQ/seek/parity oracle; full
scope dispatch and precise bus boundaries; no allocation; complete bounded
atomic codec/max-size/project/scene/Undo integration; UI1/2 names, summaries,
first-side control, narrow layout/keyboard/status; and actual processor renders.
This initial checkpoint defines contracts and host builders only.
