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
