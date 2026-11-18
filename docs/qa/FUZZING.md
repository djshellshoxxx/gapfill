# Fuzzing (libFuzzer + ASan + UBSan)

`GapFillFuzz` (opt-in, clang only) feeds arbitrary bytes to every parser that accepts
outside data. It links the production sources (`GAPFILL_SOURCES`) and the harness in
`Tests/fuzz/FuzzTargets.cpp`. Normal builds and CI gates are unaffected.

| Target (`GF_FUZZ_TARGET`) | Entry point | Invariant beyond "no crash / no sanitizer report" |
|---|---|---|
| `state` | `GapFillAudioProcessor::setStateInformation` | Accepted state is a fixed point: save, load, save is byte-identical. |
| `preset` | `loadParameterStateFromFile` | Rejected file leaves the session unchanged and reports a reason. |
| `recipe` | `history::parseRecipeText` | - |
| `profile` | `commands::parseProfile` | Rejection reports a reason. |
| `catalog` | `i18n::Catalog::load` | Rejection reports a reason. |
| `events` | `events::loadFromFile` | - |
| `ratings` | `taste::RatingStore::parse` | - |

## Run

    cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DGAPFILL_FUZZ=ON
    cmake --build build-fuzz --target GapFillFuzz --parallel 4
    tools/fuzz/run_all.sh 300            # seconds per target

Seeds: `Tests/fuzz/corpus/<target>/` plus `Tests/fixtures/` for `state`. The generated
corpus and findings stay in `build-fuzz/` (`fuzz-corpus/`, `fuzz-findings/`). Reproduce a
finding with `GF_FUZZ_TARGET=<target> build-fuzz/artefacts/GapFillFuzz <file>`. Every fixed
finding gets a regression test in the normal suite.

On Ubuntu, libFuzzer needs `libclang-rt-<version>-dev`.

## Results

See `docs/qa/FUZZING-RESULTS.md` (per-campaign durations, executions, findings, fixes).
