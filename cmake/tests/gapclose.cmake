# Gap-closing pass after the 2026-10-21 completeness audit: F-M17 / F-M21 / F-X05 / F-S11 / F-M18 GUI closures,
# T-01 meter arithmetic, W-03 transport matrix.
list(APPEND GAPFILL_TEST_SOURCES
    Tests/features/TestGapClose.cpp
    Tests/features/TestMeter.cpp
    Tests/features/TestTransport.cpp
)
