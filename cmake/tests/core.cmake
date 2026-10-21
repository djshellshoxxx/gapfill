# Core test sources. A feature adds its own cmake/tests/<feature>.cmake.
list(APPEND GAPFILL_TEST_SOURCES
    Tests/TestMain.cpp
    Tests/TestFramework.cpp
    Tests/TestSignals.cpp
    Tests/TestDsp.cpp
    Tests/TestPlugin.cpp
    Tests/TestCombinatorial.cpp
    Tests/support/TestRig.cpp
    Tests/support/AllocGuard.cpp
    Tests/features/TestGolden.cpp
    Tests/features/TestStateFixtures.cpp
)
