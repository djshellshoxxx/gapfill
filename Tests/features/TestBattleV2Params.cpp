// F-M18 detailed Beat Battles (BATTLEV2): registration contract of the three reserved tail controls
// (docs/progress/battle-v2-contract-20261031.md). The detailed runtime is not wired yet, so this pins only
// what is shipped: permanent host indices 295..297, defaults, and the Random / performance classification.

#include "../support/TestRig.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    juce::AudioProcessorParameterWithID* paramAt (GapFillAudioProcessor& p, int index)
    {
        const auto& params = p.getParameters();
        return index < params.size() ? dynamic_cast<juce::AudioProcessorParameterWithID*> (params[index]) : nullptr;
    }
}

GF_TEST (BattleV2Params, the_three_controls_hold_their_reserved_host_indices_and_defaults)
{
    GapFillAudioProcessor p;
    const char* const ids[] = { "fm18_on", "fm18_unit", "fm18_first" };

    for (int i = 0; i < 3; ++i)
    {
        auto* withId = paramAt (p, 295 + i);
        GF_EXPECT_MSG (withId != nullptr && withId->paramID == ids[i], "host index " + juce::String (295 + i) + " is not " + ids[i]);
    }

    auto& state = p.getValueTreeState();
    GF_EXPECT (state.getRawParameterValue ("fm18_on")->load() == 0.0f);      // Off
    GF_EXPECT (state.getRawParameterValue ("fm18_unit")->load() == 1.0f);    // 2 bars
    GF_EXPECT (state.getRawParameterValue ("fm18_first")->load() == 0.0f);   // Style 1
}

GF_TEST (BattleV2Params, controls_are_performance_state_and_never_randomised)
{
    const auto creative = creativeParameterIDs();
    const auto randomAll = randomisableParameterIDs();
    const auto performance = performanceParameterIDs();

    for (const auto* id : { "fm18_on", "fm18_unit", "fm18_first" })
    {
        GF_EXPECT_MSG (! creative.contains (id), juce::String (id) + " is in the Random list");
        GF_EXPECT_MSG (! randomAll.contains (id), juce::String (id) + " is in the Random All list");
        GF_EXPECT_MSG (performance.contains (id), juce::String (id) + " is not a performance parameter");
    }
}
