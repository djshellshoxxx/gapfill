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
    auto* group = proc.getValueTreeState().getParameter ("coopGroup");
    auto* mode = proc.getValueTreeState().getParameter ("coopMode");
    GF_EXPECT (group != nullptr && mode != nullptr);
    GF_EXPECT (group->getNumSteps() == 5);
    GF_EXPECT (mode->getNumSteps() == 3);
    group->setValueNotifyingHost (0.5f);
    mode->setValueNotifyingHost (0.5f);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("coopGroup")->load(), 2.0f, 0.0f);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("coopMode")->load(), 1.0f, 0.0f);
}

GF_TEST (ParameterCompatibility, voice_formant_is_appended_neutral_and_undoable)
{
    GapFillAudioProcessor proc;
    const auto all = allParameterIDs();
    GF_EXPECT (all.indexOf ("fl20_amount") == 216);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fl20_amount")->load(), 0.0f, 0.0f);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fl20_preserve")->load(), 100.0f, 0.0f);
    setParams (proc, { { "fl20_amount", 12.5f } });
    proc.pushUndoSnapshot();
    setParams (proc, { { "fl20_amount", 75.0f } });
    GF_EXPECT (proc.undo());
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fl20_amount")->load(), 12.5f, 0.0f);
    for (const char* id : { "fl20_amount", "fl20_shift", "fl20_tilt", "fl20_vowel", "fl20_preserve", "fl20_detail" })
    {
        GF_EXPECT (! randomisableParameterIDs().contains (id));
        GF_EXPECT (! performanceParameterIDs().contains (id));
    }
}

GF_TEST (ParameterCompatibility, extended_midi_output_is_opt_in_and_preserved_by_undo)
{
    GapFillAudioProcessor proc;
    const auto all = allParameterIDs();
    GF_EXPECT (all.indexOf ("fl11_on") == 222);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fl11_on")->load(), 0.0f, 0.0f);
    proc.pushUndoSnapshot();
    setParams (proc, { { "fl11_on", 1.0f }, { "fl11_chan", 12.0f } });
    GF_EXPECT (proc.undo());
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fl11_on")->load(), 1.0f, 0.0f);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fl11_chan")->load(), 12.0f, 0.0f);
    for (const char* id : { "fl11_on", "fl11_chan", "fl11_hit_on", "fl11_gap_on", "fl11_fill_on",
                           "fl11_note_hit", "fl11_note_gap", "fl11_note_fill", "fl11_vel", "fl11_len_ms",
                           "fl11_cc", "fl11_timing" })
    {
        GF_EXPECT (performanceParameterIDs().contains (id));
        GF_EXPECT (! randomisableParameterIDs().contains (id));
    }
}

GF_TEST (ParameterCompatibility, detailed_feature_tail_order_and_neutral_defaults_are_fixed)
{
    GapFillAudioProcessor proc;
    const auto ids = allParameterIDs();
    GF_EXPECT (ids.size() == 284);
    GF_EXPECT (ids.indexOf ("fm23_macro5") == 242);
    GF_EXPECT (ids.indexOf ("fx01_mode") == 268);
    GF_EXPECT (ids.indexOf ("fm20_amount") == 275);
    GF_EXPECT (ids.indexOf ("fm20_mix") == 283);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fx01_mode")->load(), 0.0f, 0.0f);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fm20_amount")->load(), 0.0f, 0.0f);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fm20_mix")->load(), 100.0f, 0.0f);
    for (const auto* id : { "fm23_macro5", "fm23_src1", "fm23_depth8", "fm23_lfoAHz", "fx01_mode" })
        GF_EXPECT (performanceParameterIDs().contains (id));
    for (const auto* id : { "fx01_motion", "fx01_scatter", "fx01_tone", "fm20_amount", "fm20_wow", "fm20_mix" })
    {
        GF_EXPECT (! performanceParameterIDs().contains (id));
        GF_EXPECT (! randomisableParameterIDs().contains (id));
    }
}
