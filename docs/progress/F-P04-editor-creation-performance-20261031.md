# F-P04 editor creation performance

Matched native snapshot: `652592ca0ead7981738833cea0453fa9fa61e0c8`.
The immutable source/build was preserved. Diagnostic copies of Editor and
ComponentTranslator were compiled against that snapshot's matching headers and
cached product/JUCE objects; a private fixture measured constructor stages.
Production contains none of those timing/logging hooks. Measurements were taken
while other team work was active; they are not reference-machine budget results.

The added language-refresh path unconditionally shaped the entire Help corpus
before HelpContent had a width. At construction, `getWidth() - 32` was negative,
so the fallback shaped roughly 60000 characters at 120 pixels, then shaped them
again at their actual display width. Warm member-construction time was about
1.57 seconds, while the translation tree walk took 3–5 milliseconds and warm
font-coverage checks took less than 1 millisecond. Sampled profiling also showed
TextLayout shaping/string traversal as the dominant work.

The Help fix defers shaping until a positive text width exists. Registered Help
content, font coverage, live language changes and actual glyph-bound checks are
unchanged. In the same snapshot, the Help/Options member stage fell to 2.2–2.7
milliseconds and warm editor creation fell from about 2.22 seconds to
0.65–0.67 seconds. Constructor geometry, live English/pseudo changes and shaped
glyph bounds at 320/700/1250 pixels pass all three focused tests; the original
zero-width behavior fails its new test (two passed, one failed).

A second direct benchmark measured repeated `availableLanguages()` calls at
291/277/282 milliseconds, matching the approximately 284-millisecond SlotHost
stage. That function reparsed both immutable embedded catalogs for each editor.
The separate metadata change retains complete validation once per loaded
module, preserves selection-time validation and returns independently owned
language vectors. With the cache, the first query took 273 milliseconds and
subsequent queries about 0–1 microseconds. SlotHost fell to 4.5–5.8 milliseconds
and warm editor creation to 356–360 milliseconds. The native selection and
owned-copy-isolation regression passes (one test). These are comparative
observations, not a new timing assertion under CI load.

Both changes together passed the unchanged 200-cycle EditorLifecycle stress in
263.78 seconds, within its original 600-second deadline. The run delivered 200
timer and 200 async callbacks, processed 638531 audio blocks and performed
1202167395 parameter writes (approximately 4.56 million/second). Its executable
used the original 652 Editor, translator, TestMain and lifecycle-test objects;
only the HelpView and Strings production translation units were replaced, with
matching unchanged headers. It contained none of the diagnostic timing hooks.
Binary SHA-256: `5a722a9672a6a785b02fec169ce05bc2169591c8f8bc1dee171eb00976a15324`.

No test iteration, automation rate, Help entry, rendering bound or deadline has
been reduced. The original lifecycle source hash and modified source hashes
are retained in the run evidence. Final candidate/platform evidence is separate
from this matched-snapshot verification.
