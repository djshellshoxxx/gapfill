# Scene model-version metadata

Depends on model-version POD/codec 18653fd and additive Cloud selector68948c3
(their cherry copies in this worktree are a04f1f3/fe77497). ScenesV2 now stores
an explicit Cloud model 1/2 outside the raw parameter table; old/missing fields
select1. Capturing FX scope reads the canonical APVTS child. Coherent overlay
metadata changes the Cloud selector only at the recall boundary; no Taste or
arrangement domain is captured. Canonical detailed F-L07 and doc12 scope lists
do not include section-aware arrangement settings, so adding those would be
an unapproved scene-scope expansion.

Message commit merges only Cloud into the current canonical child and invokes
an injected callback; root must install callback->syncModelVersions(). Manual
model changes while pending relinquish only that overlay domain, preserving
those changes through commit. Portable bank v2 roundtrip/digest includes the
Cloud selector; v1 bank digests remain unchanged. Five focused tests cover
capture, missing/invalid metadata, project/portable codecs, exact boundary
projection, message merge/notification and manual-upgrade precedence.

All changed scene/controller/bank sources and five new plus existing focused
test files pass JUCE syntax checks. An actual compiled cached-JUCE probe with
an APVTS and controller executes capture, both codecs, missing/invalid metadata,
Cloud-only canonical commit, callback and manual upgrade precedence: PASS.
The same probe executes a model-only overlay at PPQ8, preserving the old prefix
and applying at the exact computed240-sample split: PASS. Integrated processor
and whole-suite execution remain pending the shared native build.
