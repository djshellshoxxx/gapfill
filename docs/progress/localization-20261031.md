# F-P04 localization implementation evidence

Language is a per-user preference, with deterministic English default and no
automatic locale detection. English/pseudo catalogs remain bounded UTF-8 JSON
files of at most 256 KiB; validation is atomic, missing entries fall back to
English, and numbered arguments remain verbatim. No host parameter ID, state
schema, preset/scene/macro name, path, MIDI identity or audio operation is
translated. The service is called only from message-thread presentation paths.

The editor adapter caches known English originals, observes new children, purges
dead SafePointers, preserves choice IDs and explicitly skips user-content
subtrees. Registered parameter tooltips use exact metadata aliases and complete
templates. Native popup/dialog sinks use explicit `text`/`format`; separate
preset-browser and Mutation Lab call-outs own their own adapters. Preset chip
filtering now uses canonical category properties instead of displayed text.

This slice wires Options, Help, reason/legend presentation, parameter menus,
audition menus, mutation descriptions/announcements, preset browser descriptions
and dialogs, recipe chips/status/exports, spectral meters, Panic, Scrub and
Arrangement/Routing/Effects readouts. The display recipe formatter leaves the
canonical formatter used by saved filenames unchanged. The coordinator and
other owners wire editor dialogs, timeline/Explain/Waveform, state/MIDI UI,
co-op and the remaining readout panels before the final catalog regeneration.

Validation completed locally:

- Repository preflight, layout literal ratchet and completeness audit passed
  (0 audit failures; 8 pre-existing exemptions on this branch baseline).
- 60 Python tool tests passed, including source/catalog freshness, byte caps,
  protected literals and placeholder/+40% pseudo invariants. The independent
  19 release-assurance tests passed with a temporary synthetic GPG identity.
- Every changed UI/i18n C++ translation unit and the new I18n tests passed native
  syntax checks using the existing JUCE toolchain/module configuration.
- A fresh native component probe, linked only against current i18n/settings
  objects and existing JUCE modules, passed dynamic/repeated/English/pseudo
  ComboBox selection checks: ID 73 retained, text synchronized, zero callbacks.
- A fresh shaped Help probe (core, recipe and audition sections, including the
  longest known help paragraph) passed English and pseudo glyph-bound checks
  at 320, 700 and 1250 pixels. Registered metadata tooltip and file-error path
  preservation probes also passed. The full integrated Help registry remains
  covered by the native `I18n.layout_no_clip_longest_string` test.

The real Help probe found that padding a whole pseudo paragraph at its end made
one unbreakable token overflow at every width. Pseudo padding now distributes
across breakable words while preserving placeholders and at least 40 percent
codepoint expansion. One accented letter per word exercises Unicode within the
byte cap. The generated inventory prunes structure/thread/numeric literals and
catalogs plain HelpRegistrar bodies at their actual split-paragraph sinks.

`INVENTORY.json` is an exhaustive literal inventory with heuristic sink labels,
not a claim that every string is localized. Its pending rows include canonical
model strings and identity data as well as UI work; inspect the actual sink
before disposition. Regenerate from the **final integrated source**, run the
freshness test and retain the final source/binary SHA with EN/pseudo minimum,
default and large layouts at all supported scales. Native full-suite runs,
screen-reader behavior, real host displays and multilingual glyph coverage on
Windows/macOS remain separate evidence. No human acceptance is inferred from
these synthetic/native probes.

The subsequent readout slice wires calibration/suggestion summaries, shaping
and capture, performance pad painting, groove, scale, energy, battles,
call/response, tension editor/status and hover/effective-value accessibility.
The calibration UI reads two original scalar settings for its complete result
sentence; analysis results, storage and audio execution remain unchanged.
Known uppercase aliases fix labels whose constructors uppercase the canonical
display name. The current native alias probe passes while retaining canonical
metadata and path arguments. All 17 affected translation units/tests passed
native syntax checks across this slice. The final source inventory additionally
prunes factory identities, serialized state/reason/command keys and compiler
diagnostics; English is 165,056 bytes and pseudo is 232,204 bytes on this branch,
before the coordinator incorporates other feature owners' new text.
