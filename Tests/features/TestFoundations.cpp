// Phase 0 seams (docs/dev/02-FOUNDATIONS.md F0-1).

#include "../support/TestRig.h"

#include "gui/HelpRegistry.h"

using namespace gapfill;
using namespace gftest;

GF_TEST (Foundations, parameter_set_and_order_are_unchanged_by_the_group_split)
{
    const auto ids = allParameterIDs();
    // The 66 shipped core IDs keep their indices; feature groups only append after them.
    GF_EXPECT (ids.size() >= 66);
    GF_EXPECT (ids[0] == "detMode");
    GF_EXPECT (ids[65] == "capture");

    GapFillAudioProcessor proc;
    GF_EXPECT ((int) proc.getParameters().size() == ids.size());

    for (int i = 0; i < ids.size(); ++i)
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.getParameters()[i]);
        GF_EXPECT_MSG (p != nullptr && p->getParameterID() == ids[i],
                       "parameter index " + juce::String (i) + " must stay " + ids[i]);
    }
}

GF_TEST (Foundations, performance_ids_exist_and_are_disjoint_from_randomisable_ids)
{
    GapFillAudioProcessor proc;
    const auto perf = performanceParameterIDs();
    const auto random = randomisableParameterIDs();

    for (const auto& id : perf)
    {
        GF_EXPECT_MSG (proc.getValueTreeState().getParameter (id) != nullptr, "unknown performance id " + id);
        GF_EXPECT_MSG (! random.contains (id), id + " must not be randomisable");
    }

    GF_EXPECT (perf.contains ("capture"));
}

GF_TEST (Foundations, help_registry_returns_sections_sorted_by_order)
{
    const auto sections = HelpRegistry::getSections();
    GF_EXPECT (sections.size() >= 16);
    GF_EXPECT (sections.front().order == 100);
    GF_EXPECT (sections.back().title == "About");

    for (size_t i = 1; i < sections.size(); ++i)
        GF_EXPECT_MSG (sections[i - 1].order < sections[i].order, "help sections must have unique, ascending orders");
}

GF_TEST (Foundations, help_registry_accepts_a_simple_feature_section)
{
    struct Capture : HelpWriter
    {
        juce::StringArray lines;
        void title (const juce::String& s) override        { lines.add ("T:" + s); }
        void versionLine (const juce::String& s) override  { lines.add ("V:" + s); }
        void body (const juce::String& s) override         { lines.add ("B:" + s); }
        void entry (const juce::String& t, const juce::String& d) override { lines.add ("E:" + t + "=" + d); }
    };

    static const HelpRegistrar reg (1234, "Test feature", "first paragraph\nsecond paragraph");

    bool found = false;

    for (const auto& s : HelpRegistry::getSections())
    {
        if (s.order != 1234)
            continue;

        found = true;
        Capture c;
        s.write (c);
        GF_EXPECT (s.title == "Test feature");
        GF_EXPECT (c.lines.size() == 2 && c.lines[0] == "B:first paragraph" && c.lines[1] == "B:second paragraph");
    }

    GF_EXPECT (found);
}
