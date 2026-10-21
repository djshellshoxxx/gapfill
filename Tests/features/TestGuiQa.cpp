// Headless GUI QA (docs/audits/2026-10-21-qa-report.md §5).
//
// Builds the real editor at the minimum / default / large sizes, in Simple and Advanced view, on every tab,
// at UI scales 100..200 % and with the Effects drawer open, then audits the component tree:
//   * every visible control lies inside its parent (no clipping by layout),
//   * no two interactive siblings overlap,
//   * text of labels and buttons fits its box,
//   * with keyboard navigation on, every focusable control is reachable by Tab and none is a dead end.
// When GF_SCREENSHOT_DIR is set, a PNG of each main screen is written there (docs/audits/screenshots).

#include "../support/TestRig.h"
#include "UiTestUtil.h"

#include "PluginEditor.h"
#include "features/presetbrowser/GlobalSettings.h"

#include <iostream>
#include <set>

using namespace gapfill;
using namespace gftest;

namespace
{
    struct ScopedSettings
    {
        ScopedSettings()
        {
            file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("gapfill-guiqa-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml");
            settings::GlobalSettings::useFileForTests (file);
        }

        ~ScopedSettings()
        {
            settings::GlobalSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                         .getChildFile ("gapfill-guiqa-reset.xml"));
            file.deleteFile();
        }

        juce::File file;
    };

    float textWidthOf (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, text, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }

    bool isInteractive (const juce::Component* c)
    {
        return dynamic_cast<const juce::Button*> (c) != nullptr || dynamic_cast<const juce::Slider*> (c) != nullptr
            || dynamic_cast<const juce::ComboBox*> (c) != nullptr || dynamic_cast<const juce::TextEditor*> (c) != nullptr;
    }

    juce::String describe (const juce::Component* c)
    {
        juce::String s = juce::String (typeid (*c).name()) + "'" + c->getName() + "'";

        if (auto* b = dynamic_cast<const juce::Button*> (c))
            s << "[" << b->getButtonText() << "]";
        else if (auto* l = dynamic_cast<const juce::Label*> (c))
            s << "[" << l->getText() << "]";

        return s + " " + c->getBounds().toString();
    }

    juce::String textOf (const juce::Component* c)
    {
        if (auto* b = dynamic_cast<const juce::TextButton*> (c))
            return b->getButtonText();

        if (auto* l = dynamic_cast<const juce::Label*> (c))
            return l->getText();

        return {};
    }

    /** Components the layout deliberately lets overflow their parent or overlay each other. */
    bool isExempt (const juce::Component* c)
    {
        if (c->getName().containsIgnoreCase ("overlay") || c->getName().containsIgnoreCase ("tour"))
            return true;

        for (auto* p = c->getParentComponent(); p != nullptr; p = p->getParentComponent())
            if (dynamic_cast<const juce::Viewport*> (p) != nullptr || p->getName().containsIgnoreCase ("tour"))
                return true;

        return false;
    }

    void audit (juce::Component& root, const juce::String& ctx, juce::StringArray& out)
    {
        std::vector<juce::Component*> all;
        collectAll (root, all);

        for (auto* c : all)
        {
            if (! visibleInTree (*c, root) || c->getWidth() <= 0 || c->getHeight() <= 0 || isExempt (c))
                continue;

            auto* parent = c->getParentComponent();

            if (parent != nullptr && parent != &root && ! parent->getLocalBounds().contains (c->getBounds()))
                out.add (ctx + " | outside parent: " + describe (c) + " in " + describe (parent));

            if (parent == &root && ! root.getLocalBounds().contains (c->getBounds()))
                out.add (ctx + " | outside editor: " + describe (c));

            if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
                if (cb->getText().isEmpty() && cb->getTextWhenNothingSelected().isEmpty())
                    out.add (ctx + " | combo box shows nothing (" + juce::String (cb->getNumItems()) + " items): " + describe (c));

            // text fit: the LookAndFeel squeezes text horizontally down to a minimum scale (labels 0.7 by the JUCE
            // default, buttons 0.85 in GapFillLookAndFeel::drawButtonText) and only then cuts it with an ellipsis.
            const auto text = textOf (c);

            if (text.isNotEmpty() && c->getWidth() > 0)
            {
                float textWidth = 0.0f, avail = (float) c->getWidth(), minScale = 1.0f;

                if (auto* l = dynamic_cast<juce::Label*> (c))
                {
                    textWidth = textWidthOf (l->getLookAndFeel().getLabelFont (*l), text);
                    avail -= (float) l->getBorderSize().getLeftAndRight();
                    minScale = l->getMinimumHorizontalScale() > 0.0f ? l->getMinimumHorizontalScale() : 0.7f;
                }
                else if (auto* b = dynamic_cast<juce::TextButton*> (c))
                {
                    textWidth = textWidthOf (b->getLookAndFeel().getTextButtonFont (*b, b->getHeight()), b->getButtonText());
                    avail -= 8.0f;
                    minScale = 0.85f;
                }

                if (textWidth * minScale > avail + 0.5f)
                    out.add (ctx + " | text is cut with an ellipsis (needs " + juce::String (textWidth * minScale, 1) + " px at its minimum scale, has "
                             + juce::String (avail, 1) + "): " + describe (c));
            }
        }

        // interactive siblings must not overlap
        for (auto* c : all)
        {
            if (! isInteractive (c) || ! visibleInTree (*c, root) || isExempt (c) || c->getWidth() <= 0 || c->getHeight() <= 0)
                continue;

            auto* parent = c->getParentComponent();

            if (parent == nullptr)
                continue;

            for (auto* sib : parent->getChildren())
            {
                if (sib <= c || ! isInteractive (sib) || ! sib->isVisible() || isExempt (sib))
                    continue;

                const auto inter = c->getBounds().getIntersection (sib->getBounds());

                if (inter.getWidth() * inter.getHeight() >= 20)
                    out.add (ctx + " | overlapping controls: " + describe (c) + " x " + describe (sib));
            }
        }
    }

    void auditFocus (juce::Component& root, const juce::String& ctx, juce::StringArray& out)
    {
        std::vector<juce::Component*> all;
        collectAll (root, all);

        std::set<juce::Component*> wanted;

        for (auto* c : all)
            if (c->getWantsKeyboardFocus() && c->isEnabled() && visibleInTree (*c, root) && c->getWidth() > 0 && c->getHeight() > 0 && ! isExempt (c))
                wanted.insert (c);

        juce::KeyboardFocusTraverser traverser;
        std::set<juce::Component*> reached;

        for (auto* c : traverser.getAllComponents (&root))
            reached.insert (c);

        for (auto* c : wanted)
            if (reached.count (c) == 0)
                out.add (ctx + " | focusable control not reachable by Tab: " + describe (c));

        if (wanted.empty())
            out.add (ctx + " | no focusable control at all with keyboard navigation on");
    }

    struct Config
    {
        const char* name;
        ui::ViewMode mode;
        int tab;
        int w, h;
        int scale;
        bool drawer;
        bool scrub = false;      ///< Freeze & Scrub switched on (its Position / Width knobs show in the drawer)
    };

    void maybeSnapshot (juce::Component& editor, const juce::String& file)
    {
        const auto dir = juce::SystemStats::getEnvironmentVariable ("GF_SCREENSHOT_DIR", {});

        if (dir.isEmpty())
            return;

        juce::File target (dir);
        target.createDirectory();
        auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream out (target.getChildFile (file + ".png"));

        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            png.writeImageToStream (image, out);
        }
    }

    juce::StringArray runConfigs (const std::vector<Config>& configs, bool keyboard)
    {
        ScopedSettings scoped;
        juce::StringArray problems;

        for (const auto& cfg : configs)
        {
            settings::GlobalSettings::shared()->setInt (settings::GlobalSettings::keyUiScale, cfg.scale);

            GapFillAudioProcessor proc;
            proc.getUiState().setViewMode (cfg.mode);
            proc.setKeyboardNavigationEnabled (keyboard);

            if (cfg.scrub)
                setParams (proc, { { "scrubOn", 1.0f } });

            GapFillAudioProcessorEditor editor (proc);
            editor.setSize (cfg.w, cfg.h);
            editor.applyScaleForTests();
            editor.applyViewModeForTests();
            if (cfg.tab > 0 && ! editor.isTabVisibleForTests (cfg.tab))
                continue;                                    // that tab does not exist in this view / width

            editor.selectTabForTests (cfg.tab);

            if (keyboard)
                editor.applyKeyboardNavigationSettingForTests();

            if (cfg.drawer)
                if (auto* d = editor.getEffectsDrawerForTests())
                    d->setExpanded (true);

            editor.resized();

            const auto ctx = juce::String (cfg.name) + " " + juce::String (cfg.w) + "x" + juce::String (cfg.h)
                           + " scale" + juce::String (cfg.scale) + (cfg.mode == ui::ViewMode::Simple ? " simple" : " advanced");

            audit (editor, ctx, problems);

            if (keyboard)
                auditFocus (editor, ctx, problems);

        }

        return problems;
    }

    void report (const juce::StringArray& problems)
    {
        juce::StringArray unique;
        unique.addArray (problems);
        unique.removeDuplicates (false);

        for (int i = 0; i < unique.size() && i < 40; ++i)
            std::cout << "  GUI-QA: " << unique[i] << std::endl;

        GF_EXPECT_MSG (unique.isEmpty(), juce::String (unique.size()) + " GUI layout problem(s); first: " + (unique.isEmpty() ? juce::String() : unique[0]));
    }
}

GF_TEST (GuiQa, layout_has_no_clipping_overlap_or_truncated_text_across_sizes_modes_tabs_and_scales)
{
    std::vector<Config> configs;
    const int sizes[][2] = { { 1020, 620 }, { 1100, 700 }, { 1800, 1000 } };
    const int scales[] = { 100, 125, 150, 200 };
    int n = 0;

    for (const auto mode : { ui::ViewMode::Simple, ui::ViewMode::Advanced })
        for (int tab = 0; tab < 4; ++tab)
            for (const auto& sz : sizes)
            {
                const int scale = scales[n++ % 4];
                configs.push_back ({ "matrix", mode, tab, sz[0], sz[1], scale, false });
            }

    for (const auto& sz : sizes)
    {
        configs.push_back ({ "drawer", ui::ViewMode::Advanced, 0, sz[0], sz[1], 100, true });
        configs.push_back ({ "drawer+scrub", ui::ViewMode::Advanced, 0, sz[0], sz[1], 100, true, true });   // Scrub card unfolded
    }

    configs.push_back ({ "drawer+scrub simple", ui::ViewMode::Simple, 0, 1020, 620, 100, true, true });

    report (runConfigs (configs, false));
}

GF_TEST (GuiQa, every_focusable_control_is_reachable_by_keyboard)
{
    std::vector<Config> configs;

    for (const auto mode : { ui::ViewMode::Simple, ui::ViewMode::Advanced })
        for (int tab = 0; tab < 4; ++tab)
            configs.push_back ({ "keyboard", mode, tab, 1100, 700, 100, tab == 0 });

    report (runConfigs (configs, true));
}

GF_TEST (GuiQa, screenshots_of_the_main_screens)
{
    // Only does something when GF_SCREENSHOT_DIR is set; always checks that a snapshot is non-empty and not flat.
    ScopedSettings scoped;
    settings::GlobalSettings::shared()->setInt (settings::GlobalSettings::keyUiScale, 100);

    struct Shot { const char* name; ui::ViewMode mode; int tab; int w, h; bool drawer; bool tour; bool scrub = false; };
    const Shot shots[] = {
        { "main-simple-creative-1100x700", ui::ViewMode::Simple,   0, 1100, 700, false, false },
        { "main-advanced-creative-1100x700", ui::ViewMode::Advanced, 0, 1100, 700, false, false },
        { "main-advanced-weights-1100x700", ui::ViewMode::Advanced, 1, 1100, 700, false, false },
        { "main-advanced-spectral-1100x700", ui::ViewMode::Advanced, 2, 1100, 700, false, false },
        { "main-advanced-performance-1300x800", ui::ViewMode::Advanced, 3, 1300, 800, false, false },
        { "main-advanced-min-1020x620", ui::ViewMode::Advanced, 0, 1020, 620, false, false },
        { "main-advanced-effects-drawer-1300x900", ui::ViewMode::Advanced, 0, 1300, 900, true, false },
        { "main-advanced-large-1800x1000", ui::ViewMode::Advanced, 0, 1800, 1000, false, false },
        { "main-advanced-effects-drawer-scrub-1300x900", ui::ViewMode::Advanced, 0, 1300, 900, true, false, true },
        { "main-advanced-effects-drawer-scrub-min-1020x620", ui::ViewMode::Advanced, 0, 1020, 620, true, false, true },
        { "main-simple-effects-drawer-1100x700", ui::ViewMode::Simple, 0, 1100, 700, true, false, true },
        { "tour-1100x700", ui::ViewMode::Simple, 0, 1100, 700, false, true },
    };

    for (const auto& s : shots)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, 48000.0, 512, false);
        proc.getUiState().setViewMode (s.mode);

        if (s.scrub)
            setParams (proc, { { "scrubOn", 1.0f } });

        // give the visualiser something to show
        TestPlayHead ph;
        ph.bpm = 150.0;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, drumLoop (48000.0, 150.0, 2), 512, 48000.0, o);

        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (s.w, s.h);
        editor.applyViewModeForTests();
        editor.selectTabForTests (s.tab);

        if (s.drawer)
            if (auto* d = editor.getEffectsDrawerForTests())
                d->setExpanded (true);

        if (s.tour)
            editor.showTour (true);

        editor.resized();

        if (juce::SystemStats::getEnvironmentVariable ("GF_COMBO_DUMP", {}).isNotEmpty() && s.tab == 0)
        {
            std::vector<juce::Component*> all;
            collectAll (editor, all);

            for (auto* c : all)
                if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
                    std::cout << "  combo '" << cb->getName() << "' text='" << cb->getText() << "' id=" << cb->getSelectedId()
                              << " items=" << cb->getNumItems() << " " << cb->getBounds().toString() << " visible=" << (int) visibleInTree (*cb, editor)
                              << " textColour=" << cb->findColour (juce::ComboBox::textColourId).toString() << std::endl;
        }

        auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        GF_EXPECT (image.getWidth() == s.w && image.getHeight() == s.h);

        // not a blank / single-colour page
        std::set<juce::uint32> colours;

        for (int y = 0; y < image.getHeight(); y += 7)
            for (int x = 0; x < image.getWidth(); x += 7)
                colours.insert (image.getPixelAt (x, y).getARGB());

        GF_EXPECT_MSG (colours.size() > 20, juce::String (s.name) + " renders nearly flat");
        maybeSnapshot (editor, s.name);
    }
}
