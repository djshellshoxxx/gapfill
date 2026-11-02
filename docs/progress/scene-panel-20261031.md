# N-VAL scene-bank panel completion

Implemented eight slots per page / eight pages; literal bank naming; selected
scene rename/duplicate/move/delete; wrap and no-transport choices; selected or
whole export with optional mappings; and explicit complete import preview with
scrollable skipped-scene details, unknown counts and digest warning. Import
requires an explicit scenes-only or scenes-plus-mappings choice; the latter
warns about active macros. Undo includes an exact versioned mapping snapshot
and survives editor closure because the callback captures only processor-owned
models. New import/reset discards the sidecar; undo runs once.

Controller edit wrappers cancel stale pending recalls and track last-good
scene identity. Destructive/overwrite dialogs check their original scene is
still current before applying. Every popup/file/modal callback is guarded by a
SafePointer. Export confirms the actual extension-adjusted destination and
refuses an unconfirmed newly-created target. UI timer checks names/current/
language generation rather than serializing every dense scene.

Production/UI and new controller tests pass JUCE syntax checks. An executed
actual-JUCE controller probe passes editing, last-good tracking, pending cancel
and exact-once undo sidecar behavior, together with earlier codec/model tests.
The live panel's screenshots, keyboard/modal interaction and screen-reader
inspection remain pending the integrated GUI run; syntax is not UI validation.

The scope submenu configures the next STORE without mutating an existing
snapshot; ordinary STORE preserves that scene's scope and quantise override.
Arrow keys move focus/selection across pages without recalling; Enter recalls,
Shift+Enter stores, Delete asks, Shift+F10 opens actions, Escape cancels pending.
Native/modal strings use explicit text/format templates; dynamic user labels
refresh on language-generation changes while model names stay verbatim.
