// The published host list and index-based scene list must remain the same
// prefix even when new feature groups are registered after the released tail.
#include "../support/TestRig.h"
#include "features/scenes/SceneModel.h"

using namespace gapfill;
using namespace gftest;

GF_TEST (ParameterCompatibility, released_host_indices_match_runtime_and_scene_tables)
{
    GapFillAudioProcessor proc;
    const auto manifest = testDataDir().getParentDirectory()
        .getChildFile ("tools/param_ids/HOST_ORDER");
    GF_EXPECT_MSG (manifest.existsAsFile(), "the independent release-order manifest exists");
    juce::StringArray released;
    for (auto line : juce::StringArray::fromLines (manifest.loadFileAsString()))
    {
        line = line.upToFirstOccurrenceOf ("#", false, false).trim();
        if (line.isNotEmpty())
            released.add (line);
    }
    GF_EXPECT (released.size() >= 216);
    const auto all = allParameterIDs();
    GF_EXPECT (proc.getParameters().size() == all.size());
    GF_EXPECT (all.size() >= released.size());
    scenes::initTables();
    for (int i = 0; i < all.size(); ++i)
    {
        const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*> (proc.getParameters()[i]);
        GF_EXPECT (parameter != nullptr);
        GF_EXPECT_MSG (parameter->paramID == all[i], "runtime registration differs at " + juce::String (i));
        GF_EXPECT_MSG (scenes::paramIdAt (i) == all[i], "scene index differs at " + juce::String (i));
        if (i < released.size())
            GF_EXPECT_MSG (parameter->paramID == released[i], "released host index moved at " + juce::String (i));
    }
}

GF_TEST (ParameterCompatibility, released_cooperation_choices_keep_normalized_automation)
{
    GapFillAudioProcessor proc;
    auto* group = proc.apvts.getParameter ("coopGroup");
    auto* mode = proc.apvts.getParameter ("coopMode");
    GF_EXPECT (group != nullptr && mode != nullptr);
    GF_EXPECT (group->getNumSteps() == 5);
    GF_EXPECT (mode->getNumSteps() == 3);
    group->setValueNotifyingHost (0.5f);
    mode->setValueNotifyingHost (0.5f);
    GF_EXPECT_NEAR (proc.apvts.getRawParameterValue ("coopGroup")->load(), 2.0f, 0.0f);
    GF_EXPECT_NEAR (proc.apvts.getRawParameterValue ("coopMode")->load(), 1.0f, 0.0f);
}

GF_TEST (ParameterCompatibility, voice_formant_is_appended_neutral_and_undoable)
{
    GapFillAudioProcessor proc;
    const auto all = allParameterIDs();
    GF_EXPECT (all.indexOf ("fl20_amount") == 216);
    GF_EXPECT_NEAR (proc.apvts.getRawParameterValue ("fl20_amount")->load(), 0.0f, 0.0f);
    GF_EXPECT_NEAR (proc.apvts.getRawParameterValue ("fl20_preserve")->load(), 100.0f, 0.0f);
    for (const char* id : { "fl20_amount", "fl20_shift", "fl20_tilt", "fl20_vowel", "fl20_preserve", "fl20_detail" })
    {
        GF_EXPECT (! randomisableParameterIDs().contains (id));
        GF_EXPECT (! performanceParameterIDs().contains (id));
    }
}
