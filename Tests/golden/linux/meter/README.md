# Released meter pixel references

These are additive T-01 component references, captured from the unchanged released LevelMeter header (`e89aecd`, file Git blob `c40555d0edd091e735d2f3840c9454fe7b10df31`, SHA-256 `56e8588733402284999bceaaa2c9d2d9c1446602e0d62ed8829c131a149279c4`). They do not replace or regenerate any audio golden.

Capture used the immutable baseline `14323940431fc3709c6be0d3ec8428039dafbcaf` object set in a separate UiReference executable, with JUCE 8.0.15, Linux x86_64, a 6000x4000 Xvfb display, the Standard palette and a fresh meter at each fixed level. The meter header was verified byte-identical to release `e89aecd`. Full commands, source hashes and raw capture results are retained in the execution evidence.

`MeterPixelsLinux.meter_pixels_unchanged_golden` compares the entire 25x100 meter, including rounded alpha edges, gradient and held peak line, at silence/-18/-6/0 dB. Every RGBA channel must match within one 8-bit step. Width 25 intentionally avoids platform/font-dependent numeric glyphs; the separate raster test verifies that the readout is hidden at width 25 and visible at width 26.

These references establish Linux rendering only. Windows/macOS font and native-renderer evidence must be captured on those platforms; these files are not substituted for native acceptance.
