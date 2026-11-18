#include "../support/LiveRig.h"
#include "features/info/ParamInfo.h"
#include "params/macroext.h"

using namespace gapfill;
using namespace gftest;

GF_TEST (MacroExt, appended_controls_have_registered_order_ranges_defaults_and_complete_metadata)
{
    GapFillAudioProcessor processor;
    auto& state = processor.getValueTreeState();
   #define MACRO_EXT_ID(member, id) id,
    constexpr const char* ids[] { GAPFILL_PARAMS_MACROEXT (MACRO_EXT_ID) };
   #undef MACRO_EXT_ID
    static_assert (std::size (ids) == 26);
    const auto all = allParameterIDs();
    const auto performance = performanceParameterIDs();
    int previous = -1;
    for (const auto* id : ids)
    {
        GF_EXPECT (state.getParameter (id) != nullptr);
        GF_EXPECT (performance.contains (id));
        const auto index = all.indexOf (id);
        GF_EXPECT (index >= 216 && index > previous);
        previous = index;
        const auto* metadata = info::ParamInfoRegistry::get().find (id);
        GF_EXPECT (metadata != nullptr && juce::String (metadata->oneLine).length() <= info::kMaxOneLineLength);
    }
    GF_EXPECT_NEAR (state.getParameter ("fm23_macro8")->getValue(), 0, 1e-7f);
    GF_EXPECT_NEAR (state.getParameter ("fm23_src8")->getValue(), 0, 1e-7f);
    auto* depth = state.getParameter ("fm23_depth8");
    GF_EXPECT_NEAR (depth->getNormalisableRange().start, -100, 1e-6f);
    GF_EXPECT_NEAR (depth->convertFrom0to1 (depth->getDefaultValue()), 50, 1e-6f);
    auto* rate = state.getParameter ("fm23_lfoBHz");
    GF_EXPECT_NEAR (rate->getNormalisableRange().convertFrom0to1 (.5f), 1, 1e-5f);
    GF_EXPECT_NEAR (rate->convertFrom0to1 (rate->getDefaultValue()), .5f, 1e-5f);
    auto* division = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter ("fm23_lfoBDiv"));
    GF_EXPECT (division != nullptr && division->choices.size() == 16 && division->getIndex() == 8);
}
