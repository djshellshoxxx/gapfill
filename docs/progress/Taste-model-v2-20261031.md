# F-X05 calibrated model 2 — 2026-10-31

Model 1 keeps its original smoothed global log-likelihood formula and 0.06 float slew. Saved `ModelVersions.taste` selects model1/2; absent metadata means1. Quantised saved profiles are preserved exactly on load and on version selection; only subsequent explicit ratings or Rebuild compute a new profile. Explicit Skip has always been stored as -1; unrated fills add nothing. The DECISIONS wording is corrected to the canonical F-X05/WP7 rule without reinterpreting existing data.

Model2 is a transparent per-feature binomial evidence model. Under 80%-consistent ratings, a Like contributes log(4) and a Skip subtracts log(4), with conservative prior odds1:2. Thus `score=(likesForFeature-skipsForFeature)*log(4)-log(2)`. Unlike model1's globally normalized ratio, repeated dislikes do not raise every unobserved feature. Readiness remains ≥3Likes, ≥3Skips and ≥8total. Confidence reaches1 at16ratings instead of40. Strength still scales log gain, multipliers remain clamped0.5..2, and zero weights/locked/forced exclusions are unchanged.

Model2 slews after int8 quantisation: the nearest target q is moved toward the previous q until the actual exponential multiplier delta is <0.099. This retains the strict <0.1 per-rating requirement including quantisation and in-place updates. No clamp is widened. The sequence-locked audio publication, read behavior and RNG use do not change. Rebuild is an explicit full recomputation, as before.

Native Linux x86_64 GCC Release component tests against pinned JUCE8.0.15:4 passed,0failed. M1 uses two disjoint populations of10,000 users, each hidden liked-set3/15,30uniformly sampled ratings,80%consistent feedback,Strength100. Every user contributes, including profiles never ready. Tests use the same quantised per-rating publication function as production, with a separate actual TasteService parity regression.

- Seeds1..10000: liked share0.352417,95%CI±0.001477,9991ready.
- Unseen seeds1000001..1010000:0.352134±0.001483,9993ready.
- Baseline0.2; both lower95%CI bounds exceed0.35, meeting ≥15percentage points.
- Maximum observed multiplier change0.0989475. Exhaustive starting-q[-44,44] and extreme target tests also remain<0.1.
- Legacy formula equality, new-model readiness/neutral strength, and preservation of loaded profile on version change pass.

Whole-plugin future-fill selection, project/preset/Undo routing, full existing Taste regressions, native UI interaction, M3 explanation population and cross-platform results remain integration validation. Local scratch binaries/logs are under `/workspace/work/runtime-probes`; source tests are committed, not scratch-only proof.

## Follow-up: explanation, publication and controls

A separate 1,000-user population at150ratings (the existing engaged-user M3 scenario) measures the unchanged canonical supported-item truth criterion: at least two reported toward items and every reported item in the hidden liked set. Model2 achieves951/1000; the original canonical first100seeds achieve97/100. Forcing exactly three reported/correct items is a different, stricter metric:867/1000 (88/100). That limitation remains visible; the UI does not invent a third claim when evidence is weak. The initial new test accidentally asserted this stronger all-three requirement; the final assertion follows the existing TestTaste criterion without changing any existing test.

Full-band-only ratings now leave unrated spectral-band multipliers exactly1 in model2, avoiding an unrelated band bias. A service regression confirms unrated/invalid fills change neither counts nor publication revision, while explicit Skip adds one dislike. The publication serial lets the processor mark queued rating changes as non-parameter state changes on the message thread.

PROFILE opens a480×352callout with all15 algorithm multipliers labelled with names and numeric values, counts, the same model's explanation, Rebuild from local history, and separate Reset Profile / Clear Library actions with confirmation. These actions notify the host of non-parameter changes. Updates are immediate at2Hz with no animation. The compact strip retains Like/Skip/Undo and gains accessible action titles. Options' Sound models row shows each selected version and offers the explicit undoable upgrade. Native JUCE syntax checks pass; interaction/visual accessibility verification on the integrated candidate remains pending.
