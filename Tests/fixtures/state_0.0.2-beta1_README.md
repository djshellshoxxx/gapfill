# Immutable v0.0.2-beta1 compatibility fixtures

These 45 state blobs were generated on 2026-10-31 by the actual released
`v0.0.2-beta1` production sources at
`e89aecd218253141cfcd35ad3d9ddfe76c8891e3`. No new codec or processor implementation
participated in their generation. The auxiliary test writer was added to a
separate checkout, and `git diff HEAD -- Source CMakeLists.txt cmake/sources`
was empty before and after generation. `production-tree.txt` records the exact
released production git blob IDs; `fixture-writer.cpp.txt` preserves the writer.

The manifest records 41 factory programs, two non-default sessions, every
released reader's 216 parameter values, and old-reader render fingerprints.
The complete session includes legacy MIDI, macros, Lock, History, Scenes,
command bindings, Groove, and Taste subtrees. The wet-only session covers
routing/master controls. Two additional deliberately damaged blobs test atomic
rejection. Render fingerprints are supplementary same-toolchain checks; other
platforms still run parameter, structural migration, finite-render, and numeric
legacy golden coverage.

Generation command (release test executable compiled from that tag):

```sh
GF_RELEASE_FIXTURE_DIR=/workspace/work/evidence/beta-fixtures \
    GapFillTests ReleaseFixtureWriter
```

The executed writer reported `1 passed, 0 failed`. SHA256SUMS covers all 45
blobs and the generated manifest. Existing fixtures must never be regenerated
from the current implementation. Add another immutable release set when a new
compatibility baseline is needed.
