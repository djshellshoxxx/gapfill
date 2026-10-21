// UI-0: Performance tab / Effects section / status shell (docs/dev/12 UI-0).

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "gui/SlotHost.h"
#include "gui/SlotPanels.h"
#include "gui/slots/EffectsSection.h"
#include "UiTestUtil.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    // Inert unless a test switches it on, so every other test sees empty slots.
    bool& uiSlotsEnabled() { static bool on = false; return on; }

    struct ScopedUiSlots
    {
        ScopedUiSlots()  { uiSlotsEnabled() = true; }
        ~ScopedUiSlots() { uiSlotsEnabled() = false; }
    };

    class TestCard : public juce::Component
    {
    public:
        TestCard (const char* name, int height) : tall (height)
        {
            setName (name);
            setSize (10, height);
            button.setButtonText (name);
            button.onClick = [this] { setSize (getWidth(), getHeight() == tall ? 30 : tall); };
            addAndMakeVisible (button);
        }

        void resized() override { button.setBounds (getLocalBounds().removeFromTop (24)); }

        juce::TextButton button;
        int tall;
    };

    std::unique_ptr<juce::Component> makeCard (const char* name, int height)
    {
        if (! uiSlotsEnabled())
            return nullptr;

        return std::make_unique<TestCard> (name, height);
    }

    const SlotRegistrar perf1 ({ Slot::PerformanceTab, 10, [] (GapFillAudioProcessor&) { return makeCard ("Perf card A", 80); } });
    const SlotRegistrar perf2 ({ Slot::PerformanceTab, 20, [] (GapFillAudioProcessor&) { return makeCard ("Perf card B", 120); } });

    void collect (juce::Component& root, std::vector<juce::Component*>& out)
    {
        for (auto* c : root.getChildren())
        {
            out.push_back (c);
            collect (*c, out);
        }
    }

    juce::Component* find (juce::Component& root, const juce::String& name)
    {
        std::vector<juce::Component*> all;
        collect (root, all);

        for (auto* c : all)
            if (c->getName() == name)
                return c;

        return nullptr;
    }
}

GF_TEST (Ui0, slots_empty_layout_identical_to_baseline)
{
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);

    for (const auto size : { juce::Point<int> (1020, 620), juce::Point<int> (1100, 700), juce::Point<int> (1500, 900) })
    {
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (size.x, size.y);

        GF_EXPECT (editor.getPerformancePanelForTests() == nullptr);      // nothing registered for the Performance tab
        GF_EXPECT (! editor.isPerformanceTabForTests());
        // WP8 registers the Effects section and the scrub strip: the drawer header (22 px) and the
        // 72 px strip + 6 px gap come out of the visualiser; everything else is the old layout.
        GF_EXPECT (editor.getEffectsDrawerForTests() != nullptr);

        // The pre-UI-0 formulas (kTopBarHeight 46, kMixHeight 124, kFooterHeight 22, 8 px gaps).
        const auto inner = juce::Rectangle<int> (0, 46, size.x, size.y - 46).reduced (10, 8).withTrimmedBottom (22);
        const auto rest = inner.getHeight() - 124 - 8;
        const auto controlHeight = juce::jlimit (250, 360, (int) ((float) rest * 0.54f)) + EffectsDrawer::headerHeight;
        auto visHeight = rest - 8 - controlHeight;
        if (visHeight >= 72 + 70 + 6)
            visHeight -= 72 + 6;
        const auto expectedVis = juce::Rectangle<int> (inner.getX(), inner.getY(), inner.getWidth(), visHeight);

        GF_EXPECT_MSG (editor.getVisualiserBoundsForTests() == expectedVis, "visualiser rectangle unchanged");

        const auto t0 = editor.getTabBoundsForTests (0), t1 = editor.getTabBoundsForTests (1), t2 = editor.getTabBoundsForTests (2);
        // 130/150/130 (shrunk evenly only when the SIMPLE | ADVANCED switch leaves less room), minus 1 px insets.
        GF_EXPECT (t0.getWidth() >= 100 && t1.getWidth() == t0.getWidth() + 20 && t2.getWidth() == t0.getWidth());
        GF_EXPECT (editor.getTabBoundsForTests (3).isEmpty());
    }
}

GF_TEST (Ui0, plan_tabs_never_shrinks_the_existing_three)
{
    GF_EXPECT (! planTabs (2000, false).performanceAsTab);              // nothing registered: no tab at all
    GF_EXPECT (! planTabs (494, true).performanceAsTab);                // minimum window: menu entry
    GF_EXPECT (! planTabs (529, true).performanceAsTab);
    GF_EXPECT (planTabs (530, true).performanceAsTab);
    GF_EXPECT (planTabs (700, true).performanceTabWidth == 120);
}

GF_TEST (Ui0, tabs_do_not_clip_at_min)
{
    ScopedUiSlots enabled;
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);

    for (const auto width : { 1020, 1100, 1300, 1800 })
    {
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (width, 620);

        GF_EXPECT (editor.getPerformancePanelForTests() != nullptr);

        juce::Array<juce::Rectangle<int>> tabs;
        for (int i = 0; i < 4; ++i)
            if (! editor.getTabBoundsForTests (i).isEmpty())
                tabs.add (editor.getTabBoundsForTests (i));

        GF_EXPECT (tabs.size() == (editor.isPerformanceTabForTests() ? 4 : 3));

        for (int i = 0; i < tabs.size(); ++i)
        {
            GF_EXPECT (editor.getLocalBounds().contains (tabs[i]));
            GF_EXPECT (tabs[i].getWidth() >= 90);

            for (int j = i + 1; j < tabs.size(); ++j)
                GF_EXPECT (! tabs[i].intersects (tabs[j]));
        }

        GF_EXPECT (tabs.getLast().getRight() <= editor.getWidth());
    }

    GapFillAudioProcessorEditor narrow (proc);
    narrow.setSize (1020, 620);
    GF_EXPECT_MSG (! narrow.isPerformanceTabForTests(), "no room for a 4th tab at the minimum size");

    GapFillAudioProcessorEditor wide (proc);
    wide.setSize (1800, 900);
    GF_EXPECT (wide.isPerformanceTabForTests());
}

GF_TEST (Ui0, performance_is_reachable_without_a_tab_at_min_size)
{
    ScopedUiSlots enabled;
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);

    GF_EXPECT (! editor.isPerformanceTabForTests());
    GF_EXPECT (! editor.isPerformanceShownForTests());

    editor.selectPerformanceForTests();                // what the FILE menu entry does
    GF_EXPECT (editor.isPerformanceShownForTests());

    auto* panel = editor.getPerformancePanelForTests();
    GF_EXPECT (panel->getWidth() > 300 && panel->getHeight() > 150);
    GF_EXPECT (editor.getLocalBounds().contains (panel->getBounds()));

    auto* a = find (*panel, "Perf card A");
    auto* b = find (*panel, "Perf card B");
    GF_EXPECT (a != nullptr && b != nullptr);
    GF_EXPECT (a->getWidth() == b->getWidth() && b->getY() == a->getBottom() + SlotHost::spacing);
    GF_EXPECT (a->getHeight() == 80 && b->getHeight() == 120);
}

GF_TEST (Ui0, effects_section_collapsed_by_default_and_unfolds_without_shrinking_controls)
{
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);

    auto* drawer = editor.getEffectsDrawerForTests();
    GF_EXPECT (drawer != nullptr && ! drawer->isExpanded());
    GF_EXPECT (drawer->getHeight() == EffectsDrawer::headerHeight);

    const auto collapsedVis = editor.getVisualiserBoundsForTests();
    drawer->setExpanded (true);
    const auto expandedVis = editor.getVisualiserBoundsForTests();

    GF_EXPECT (drawer->getHeight() > EffectsDrawer::headerHeight + 40);
    GF_EXPECT_MSG (expandedVis.getHeight() >= 70, "the visualiser keeps a usable height");
    GF_EXPECT (expandedVis.getHeight() < collapsedVis.getHeight());
    GF_EXPECT (editor.getLocalBounds().contains (drawer->getBounds()));

    std::vector<juce::Component*> all;
    collect (*drawer, all);
    bool sawCard = false;
    for (auto* c : all)
        sawCard = sawCard || (dynamic_cast<fxgui::EffectCardView*> (c) != nullptr && visibleInTree (*c, editor));
    int cards = 0, visibleCards = 0;
    for (auto* c : all)
        if (dynamic_cast<fxgui::EffectCardView*> (c) != nullptr)
        {
            ++cards;
            visibleCards += c->isVisible() ? 1 : 0;
        }
    GF_EXPECT_MSG (sawCard, "the WP8 effect cards are visible inside the unfolded drawer: cards " + juce::String (cards)
                            + ", own flag visible " + juce::String (visibleCards) + ", drawer " + drawer->getBounds().toString());

    drawer->setExpanded (false);
    GF_EXPECT (editor.getVisualiserBoundsForTests() == collapsedVis);
}

GF_TEST (Ui0, a_card_that_folds_itself_restacks_the_slot)
{
    ScopedUiSlots enabled;
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1300, 800);
    editor.selectPerformanceForTests();

    auto* panel = editor.getPerformancePanelForTests();
    auto* a = find (*panel, "Perf card A");
    auto* b = find (*panel, "Perf card B");
    GF_EXPECT (a != nullptr && b != nullptr);

    const auto yBefore = b->getY();
    a->setSize (a->getWidth(), 30);                       // card A folds
    GF_EXPECT (b->getY() == yBefore - 50);
    GF_EXPECT (a->getHeight() == 30);
}

GF_TEST (Ui0, keyboard_reaches_every_slot)
{
    ScopedUiSlots enabled;
    GapFillAudioProcessor proc;
    proc.getUiState().setViewMode (ui::ViewMode::Advanced);
    proc.setKeyboardNavigationEnabled (true);

    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1300, 800);
    editor.applyKeyboardNavigationSettingForTests();
    editor.getEffectsDrawerForTests()->setExpanded (true);
    editor.selectPerformanceForTests();

    juce::KeyboardFocusTraverser traverser;
    auto all = traverser.getAllComponents (&editor);

    for (const char* name : { "Perf card A", "Perf card B" })
    {
        auto* card = dynamic_cast<TestCard*> (find (editor, name));
        GF_EXPECT_MSG (card != nullptr && std::find (all.begin(), all.end(), &card->button) != all.end(), name);
    }

    GF_EXPECT (editor.getUnnamedKeyboardFocusableControlCountForTests() == 0);
}
