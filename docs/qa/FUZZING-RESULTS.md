# Fuzzing results

## 2026-11-18, first campaign (Linux, clang 18, ASan+UBSan, libFuzzer)

Three passes: 60 s per target, then 300 s per target (stopped early by the owner's release
request). Executions per target in the 300 s pass: state 8,044; preset 8,436; recipe
2,639,211; profile, catalog and events stopped at a finding; ratings not reached.

| # | Target | Finding | Root cause | Fix | Regression test |
|---|---|---|---|---|---|
| 1 | recipe | timeout (77 bytes) | invalid UTF-8 loops `juce::XmlDocument::skipNextWhiteSpace` | `state::parseUntrustedXml` validates UTF-8 first | `FuzzRegressions.invalid_utf8_*` |
| 2 | recipe | timeout | `<?...?>` inside element content: JUCE skips it as whitespace, rewinds, loops | the guard rejects `<?` after the root starts (also host state) | `processing_instruction_*`, `host_state_*` |
| 3 | profile | UBSan signed overflow | `juce::JSON` accumulates long integers in int64 | `state::parseUntrustedJson`: digit runs <= 18, nesting <= 64 | `json_integer_overflow_*` |
| 4 | profile | ASan heap over-read | `String::getNumBytesAsUTF8()` walks invalid UTF-8 past the end | guards and untrusted length caps use `strlen` | covered by #1/#3 inputs |
| 5 | catalog | UBSan null member call | `var::isObject()` is true for arrays; no DynamicObject | explicit null check | `a_json_array_is_not_a_catalog` |
| 6 | events | timeout | finding 1/2 through `juce::parseXML` in the event loader; bank import had the same pattern | event recorder, replay and bank import use the guard | `every_xml_import_path_*` |

Findings 1, 2 and 6 hang the message thread, and so the host UI, when a user opens a
crafted recipe, preset, scene bank, rating library, event recording or settings file, or
scans a preset folder that contains one. Finding 2 is also reachable from host session state.
Not covered yet: longer campaigns, the ratings target, and corpus minimisation. Rerun with
`tools/fuzz/run_all.sh 1800` before 1.0.
