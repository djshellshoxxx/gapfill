// F-G01 Glitch rack: processor, parameter registry, state and GLITCH tab tests.

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/glitch/Gui.h"
#include "features/info/ParamInfo.h"

#include <cstring>
#include <set>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 512;

    juce::StringArray lockIds()
    {
        juce::StringArray ids;
        ids.addLines (testDataDir().getParentDirectory().getChildFile ("tools/param_ids/glitch.lock").loadFileAsString());
        ids.removeEmptyStrings();
        return ids;
    }

    bool sameAudio (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;

        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
                return false;

        return true;
    }

    juce::AudioBuffer<float> renderWith (std::initializer_list<std::pair<const char*, float>> values,
                                         const juce::AudioBuffer<float>& loop)
    {
        GapFillAudioProcessor proc;
        setParams (proc, values);
        return renderOffline (proc, loop, kBlock, kSr).out;
    }
}

GF_TEST (GlitchPlugin, parameters_are_appended_at_the_registry_tail_in_lock_order_with_info)
{
    const auto lock = lockIds();
    const auto all = allParameterIDs();
    GF_EXPECT (lock.size() == 64);
    GF_EXPECT (all.size() >= lock.size());

    const auto first = all.size() - lock.size();
    for (int i = 0; i < lock.size(); ++i)
    {
        GF_EXPECT_MSG (all[first + i] == lock[i], lock[i]);
        const auto* metadata = info::ParamInfoRegistry::get().find (lock[i]);
        GF_EXPECT_MSG (metadata != nullptr && juce::String (metadata->oneLine).length() <= info::kMaxOneLineLength, lock[i]);
    }

    GF_EXPECT (all.indexOf ("fg01_on") == first);
}

GF_TEST (GlitchPlugin, stab_is_performance_state_and_nothing_glitch_is_randomised)
{
    const auto performance = performanceParameterIDs();
    const auto random = randomisableParameterIDs();
    GF_EXPECT (performance.contains ("fg01_stab"));

    for (const auto& id : lockIds())
        GF_EXPECT_MSG (! random.contains (id), id);
}

GF_TEST (GlitchPlugin, defaults_and_switched_off_rack_render_bit_identically)
{
    const auto loop = drumLoop (kSr, 120.0, 2);
    const auto reference = renderWith ({}, loop);

    // Modules configured but the rack off: untouched.
    GF_EXPECT (sameAudio (renderWith ({ { "fg01_stutMode", 1.0f }, { "fg01_gateMode", 1.0f }, { "fg01_phMix", 80.0f },
                                        { "fg01_target", 1.0f } }, loop), reference));
    // Rack on, every module off: untouched.
    GF_EXPECT (sameAudio (renderWith ({ { "fg01_on", 1.0f }, { "fg01_target", 1.0f } }, loop), reference));
}

GF_TEST (GlitchPlugin, fills_target_leaves_the_dry_path_alone_and_output_target_processes_it)
{
    const auto loop = drumLoop (kSr, 120.0, 2);
    // Wet 0: nothing generated is heard, so a Fills-only rack cannot change the output.
    const auto dryOnly = renderWith ({ { "wet", 0.0f } }, loop);
    const auto fills = renderWith ({ { "wet", 0.0f }, { "fg01_on", 1.0f }, { "fg01_target", 0.0f },
                                     { "fg01_gateMode", 1.0f } }, loop);
    GF_EXPECT (sameAudio (fills, dryOnly));

    const auto whole = renderWith ({ { "wet", 0.0f }, { "fg01_on", 1.0f }, { "fg01_target", 1.0f },
                                     { "fg01_gateMode", 1.0f }, { "fg01_gateDepth", 100.0f } }, loop);
    GF_EXPECT (! sameAudio (whole, dryOnly));

    // The gate really chops the output: a quiet closed half of some 1/32 step.
    float peak = 0.0f, minStepPeak = 1.0f;
    const auto step = (int) (kSr * 0.0625);
    for (int k = 16; k < whole.getNumSamples() / step - 1; ++k)
    {
        float closed = 0.0f;
        for (int i = k * step + step * 5 / 8; i < k * step + step * 7 / 8; ++i)
            closed = std::max (closed, std::abs (whole.getSample (0, i)));
        minStepPeak = std::min (minStepPeak, closed);
        peak = std::max (peak, closed);
    }
    GF_EXPECT (minStepPeak < 1.0e-3f);
}

GF_TEST (GlitchPlugin, every_module_on_whole_output_stays_finite_at_every_rate_and_block)
{
    for (const auto sr : { 44100.0, 96000.0 })
        for (const auto block : { 64, 1031 })
        {
            GapFillAudioProcessor proc;
            setParams (proc, { { "fg01_on", 1.0f }, { "fg01_target", 1.0f }, { "fg01_stutMode", 1.0f }, { "fg01_stutProb", 100.0f },
                               { "fg01_revMode", 1.0f }, { "fg01_gateMode", 2.0f }, { "fg01_filtMode", 3.0f },
                               { "fg01_filtRes", 100.0f }, { "fg01_phMix", 100.0f }, { "fg01_phFb", 95.0f },
                               { "fg01_flipMode", 4.0f }, { "fg01_panInv", 50.0f }, { "fg01_stabInf", 1.0f } });
            RenderOptions options;
            options.onBlock = [&proc] (int index, int)
            {
                if (auto* p = proc.getValueTreeState().getParameter ("fg01_stab"))
                    p->setValueNotifyingHost (index % 40 < 20 ? 1.0f : 0.0f);
            };
            const auto out = renderOffline (proc, drumLoop (sr, 140.0, 2), block, sr, options).out;
            bool finite = true;
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    finite = finite && std::isfinite (out.getSample (ch, i)) && std::abs (out.getSample (ch, i)) <= 16.0f;
            GF_EXPECT_MSG (finite, juce::String (sr) + "/" + juce::String (block));
        }
}

GF_TEST (GlitchPlugin, glitch_settings_survive_a_session_round_trip_and_resave_identically)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fg01_on", 1.0f }, { "fg01_target", 1.0f }, { "fg01_mix", 73.0f }, { "fg01_swing", 40.0f },
                       { "fg01_stutDiv", 6.0f }, { "fg01_stutMs", 333.0f }, { "fg01_stabRepeats", 9.0f },
                       { "fg01_stabInf", 1.0f }, { "fg01_gateHz", 21.0f }, { "fg01_filtMin", 350.0f },
                       { "fg01_phFb", -40.0f }, { "fg01_polR", 1.0f }, { "fg01_flipMode", 2.0f } });
    juce::MemoryBlock first;
    proc.getStateInformation (first);

    GapFillAudioProcessor restored;
    restored.setStateInformation (first.getData(), (int) first.getSize());
    auto& state = restored.getValueTreeState();
    GF_EXPECT (state.getRawParameterValue ("fg01_on")->load() == 1.0f);
    GF_EXPECT_NEAR (state.getRawParameterValue ("fg01_mix")->load(), 73.0f, 0.051f);
    GF_EXPECT_NEAR (state.getRawParameterValue ("fg01_stutMs")->load(), 333.0f, 0.5f);
    GF_EXPECT (state.getRawParameterValue ("fg01_stabRepeats")->load() == 9.0f);
    GF_EXPECT (state.getRawParameterValue ("fg01_flipMode")->load() == 2.0f);
    GF_EXPECT_NEAR (state.getRawParameterValue ("fg01_phFb")->load(), -40.0f, 0.051f);

    juce::MemoryBlock second;
    restored.getStateInformation (second);
    GF_EXPECT (first == second);
}

GF_TEST (GlitchPlugin, glitch_tab_lists_every_parameter_once_without_overlap_or_clipping)
{
    std::set<juce::String> seen;
    for (const auto& section : glitch::sectionSpecs())
        for (const auto& c : section.controls)
            GF_EXPECT_MSG (seen.insert (c.paramId).second, c.paramId);

    const auto lock = lockIds();
    GF_EXPECT ((int) seen.size() == lock.size());
    for (const auto& id : lock)
        GF_EXPECT_MSG (seen.count (id) == 1, id);

    GapFillAudioProcessor proc;
    for (const auto width : { 300, 520, 900 })
    {
        glitch::GlitchPanel panel (proc);
        panel.setSize (width, 10);
        panel.resized();
        GF_EXPECT (panel.getHeight() == panel.heightForWidth (width));
        GF_EXPECT (panel.getNumControls() == lock.size());

        juce::Array<juce::Rectangle<int>> boxes;
        for (auto* child : panel.getChildren())
        {
            if (child->getComponentID().startsWith ("fg01_"))
            {
                GF_EXPECT_MSG (panel.getLocalBounds().contains (child->getBounds()), child->getComponentID());
                for (const auto& other : boxes)
                    GF_EXPECT_MSG (! other.intersects (child->getBounds()), child->getComponentID());
                boxes.add (child->getBounds());
            }
        }
    }
}

GF_TEST (GlitchPlugin, editor_reaches_the_glitch_tab_as_a_tab_or_from_the_file_menu)
{
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);

    for (const auto size : { juce::Point<int> (1020, 620), juce::Point<int> (1800, 900) })
    {
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (size.x, size.y);
        GF_EXPECT (editor.getGlitchPanelForTests() != nullptr);
        GF_EXPECT (! editor.isGlitchShownForTests());
        GF_EXPECT (editor.getTabBoundsForTests (4).isEmpty() == ! editor.isGlitchTabForTests());

        editor.selectGlitchForTests();
        GF_EXPECT (editor.isGlitchShownForTests());
        auto* panel = editor.getGlitchPanelForTests();
        GF_EXPECT (editor.getLocalBounds().contains (panel->getBounds()));
        GF_EXPECT (panel->getWidth() > 300 && panel->getHeight() > 150);

        if (editor.isGlitchTabForTests())
        {
            const auto glitchTab = editor.getTabBoundsForTests (4);
            for (int i = 0; i < 4; ++i)
                GF_EXPECT (! editor.getTabBoundsForTests (i).intersects (glitchTab));
            GF_EXPECT (glitchTab.getRight() <= editor.getWidth());
        }

        editor.selectTabForTests (0);
        GF_EXPECT (! editor.isGlitchShownForTests());
    }

    GapFillAudioProcessorEditor wide (proc);
    wide.setSize (1800, 900);
    GF_EXPECT (wide.isGlitchTabForTests());
}

GF_TEST (GlitchPlugin, every_glitch_value_resaves_byte_identically)
{
    // Defaults and 200 pseudo-random settings: save -> load -> save must not drift (skewed ranges snap to an interval).
    for (int round = 0; round <= 200; ++round)
    {
        GapFillAudioProcessor proc;
        juce::Random random (round);

        if (round > 0)
            for (const auto& id : lockIds())
                if (auto* p = proc.getValueTreeState().getParameter (id))
                    p->setValueNotifyingHost (random.nextFloat());

        juce::MemoryBlock first, second, third;
        proc.getStateInformation (first);
        GapFillAudioProcessor restored;
        restored.setStateInformation (first.getData(), (int) first.getSize());
        restored.getStateInformation (second);
        GapFillAudioProcessor again;
        again.setStateInformation (second.getData(), (int) second.getSize());
        again.getStateInformation (third);
        GF_EXPECT_MSG (first == second && second == third, "round " + juce::String (round));
    }
}
