#include "TestFramework.h"

namespace
{
gftest::TestCase makeCase (const char* group, const char* name)
{
    return { group, name, [] {} };
}
}

GF_TEST (TestFramework, empty_filter_selects_all)
{
    GF_EXPECT (gftest::matchesFilter (makeCase ("Dsp", "ring_buffer_wraps"), ""));
}

GF_TEST (TestFramework, positive_filter_matches_group_and_name)
{
    const auto test = makeCase ("Dsp", "ring_buffer_wraps");
    GF_EXPECT (gftest::matchesFilter (test, "DSP"));
    GF_EXPECT (gftest::matchesFilter (test, "wraps"));
    GF_EXPECT (! gftest::matchesFilter (test, "Editor"));
}

GF_TEST (TestFramework, leading_dash_excludes_matching_group_or_name)
{
    const auto dsp = makeCase ("Dsp", "ring_buffer_wraps");
    const auto plugin = makeCase ("Plugin", "state_roundtrip");
    const auto combinatorial = makeCase ("Combinatorial", "all_settings_sweep");
    GF_EXPECT (gftest::matchesFilter (dsp, "-Combinatorial"));
    GF_EXPECT (! gftest::matchesFilter (combinatorial, "-Combinatorial"));
    GF_EXPECT (gftest::matchesFilter (plugin, "-Combinatorial"));
    GF_EXPECT (! gftest::matchesFilter (plugin, "-state"));
}

GF_TEST (TestFramework, empty_exclusion_term_matches_nothing)
{
    GF_EXPECT (! gftest::matchesFilter (makeCase ("Dsp", "ring_buffer_wraps"), "-"));
}

GF_TEST (TestFramework, zero_match_filter_is_a_failure)
{
    GF_EXPECT (gftest::Registry::get().run ("no-such-test-filter") != 0);
}
