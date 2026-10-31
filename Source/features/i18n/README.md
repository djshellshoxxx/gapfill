# F-P04 interface language

English is the deterministic default. `Strings` is message-thread-only and
never runs in DSP, host serialization, parameter identity or file protocols.
Language is a per-user `GlobalSettings` preference, shared by editors in this
loaded plug-in module; it is not project/preset data.

`Resources/lang/<code>.json` is a flat UTF-8 dictionary, at most 256 KiB. The
source fragment embeds catalog bytes in a generated header used only by
`Strings.cpp`, so adding a locale is a data-only change and does not change
legacy/JUCE compiler flags. Catalogs validate completely before activation;
unknown/missing translations use the embedded English table.

Use `text("A complete English sentence.")` for known static presentation,
`format("Saved {0}.", {filename})` for complete sentences, and `tr("stable.key")`
when a semantic catalog key is needed. Arguments remain verbatim. Numbered
placeholders may be reordered but not dropped/added by a translation. Literal
braces use `{{` and `}}`. Persistent names, paths, IDs and protocol tokens must
not be passed through translation as data.

`ComponentTranslator` attaches after editor children, caches English originals
and last-applied values, handles new children, and purges dead SafePointers.
English unchanged refresh returns early; unchanged pseudo fields allocate
nothing. Mark user/factory identities and device/profile names with
`markUserContent(control)` before attachment, even if a name equals an English
catalog entry. It preserves text/item IDs and skips editable user text; static
field metadata may translate. Call `translateMenu(menu, preserveItemPredicate)`
before showing dynamic menus, preserving user-data item IDs explicitly.

Options, Help writer sinks and reason/legend presentation use the service.
`fontForText` checks nominal glyph coverage and falls back to JUCE default sans
when the chosen face is missing a character. Renderer owners must use it where
they select fonts for custom drawing. English reason enum labels/advice remain
canonical data for replay/diagnostics; presentation helpers localize them.

Run `python3 Source/features/i18n/build_catalog.py` after UI changes and `--check`
in validation. `INVENTORY.json` inventories every C++ literal with source/line,
key and an estimated sink category. **Pending categories are not coverage.**
Review runtime fragments, menus and custom drawing explicitly; the catalog's
presence cannot prove a string is wired. The pseudo catalog accents and expands
complete strings by at least 40 percent, retaining placeholders unchanged.

`I18n` tests cover validation, English fallback, placeholder preservation,
project-state neutrality, user-data collisions, item IDs, language round trips,
dynamic children and allocation-free unchanged refresh. Actual multilingual
layout/native glyph/host evidence must still be recorded; use `ScopedLanguage`
in tests so locale selection cannot contaminate later golden/layout cases.
