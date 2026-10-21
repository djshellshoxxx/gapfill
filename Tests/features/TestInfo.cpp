// U-9: parameter info registry, tooltips and the status line.

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/health/Gui.h"
#include "features/info/HoverStatus.h"
#include "features/info/ParamInfo.h"
#include "features/info/StatusBoard.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    juce::StringArray readAllowlist()
    {
        juce::StringArray ids;

        for (auto line : juce::StringArray::fromLines (testDataDir().getChildFile ("param_info_allowlist.txt").loadFileAsString()))
        {
            line = line.trim();

            if (line.isNotEmpty() && ! line.startsWith ("#"))
                ids.add (line);
        }

        return ids;
    }

    void collect (juce::Component& root, std::vector<juce::Component*>& out)
    {
        for (auto* c : root.getChildren())
        {
            out.push_back (c);
            collect (*c, out);
        }
    }
}

GF_TEST (Info, every_parameter_has_info)
{
    const auto allow = readAllowlist();
    const auto missing = info::idsMissingInfo();

    for (const auto& id : missing)
        GF_EXPECT_MSG (allow.contains (id), "parameter without a ParamInfo entry (add Source/features/<f>/Info.cpp): " + id);

    // Core ids must never be allow-listed.
    for (const auto& id : { "fill", "density", "bypass", "capture", "wTapeStop", "protSC", "output" })
        GF_EXPECT_MSG (! missing.contains (id), id);

    for (const auto& e : info::ParamInfoRegistry::get().all())
    {
        GF_EXPECT_MSG (juce::String (e.oneLine).length() <= info::kMaxOneLineLength, e.id);
        GF_EXPECT_MSG (juce::String (e.oneLine).isNotEmpty() && juce::String (e.shortName).isNotEmpty(), e.id);
    }
}

GF_TEST (Info, allowlist_only_shrinks)
{
    const auto missing = info::idsMissingInfo();

    for (const auto& id : readAllowlist())
        GF_EXPECT_MSG (missing.contains (id), "stale allow-list line (already has info, or no such parameter): " + id);
}

GF_TEST (Info, tooltip_text_format)
{
    const info::ParamInfo density { "density", "Density", "How often an eligible gap gets filled", "40-70", "%" };
    GF_EXPECT (info::tooltipFor (density) == "Density - How often an eligible gap gets filled. Typical: 40-70 %");

    const info::ParamInfo noRange { "x", "X", "Does a thing", "", "" };
    GF_EXPECT (info::tooltipFor (noRange) == "X - Does a thing");
    GF_EXPECT (info::tooltipFor ("noSuchId", "fallback") == "fallback");
}

GF_TEST (Info, tooltip_matches_table)
{
    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1100, 700);

    std::vector<juce::Component*> all;
    collect (editor, all);

    int checked = 0;

    for (auto* c : all)
    {
        const auto id = info::HoverStatus::parameterIdFor (c);

        if (id.isEmpty() || ! c->getProperties().contains (kParamIdProperty))
            continue;

        auto* tip = dynamic_cast<juce::TooltipClient*> (c);

        if (tip == nullptr || tip->getTooltip().isEmpty())
            continue;

        const auto* entry = info::ParamInfoRegistry::get().find (id);

        if (entry == nullptr)
        {
            GF_EXPECT_MSG (readAllowlist().contains (id), "no info entry and not allow-listed: " + id);
            continue;                                         // feature controls keep their own tooltip until their Info.cpp exists
        }

        GF_EXPECT_MSG (tip->getTooltip() == info::tooltipFor (*entry), id);
        ++checked;
    }

    GF_EXPECT_MSG (checked >= 55, "tooltips checked: " + juce::String (checked));   // knobs, combos, toggles, bars, bypass
}

GF_TEST (Info, status_priority_order)
{
    using K = info::StatusKind;
    info::StatusBoard b;
    GF_EXPECT (! b.top().has_value());

    b.set (K::LastGap, "gap");
    GF_EXPECT (b.top()->kind == K::LastGap);

    b.set (K::Hover, "Density 65 %");
    GF_EXPECT_MSG (b.top()->kind == K::Hover, "hover outranks only the last gap line (documented deviation)");

    b.set (K::Fixit, "fix");
    GF_EXPECT (b.top()->kind == K::Fixit);
    b.set (K::Health, "health");
    GF_EXPECT (b.top()->kind == K::Health);
    b.set (K::Capture, "capture");
    GF_EXPECT (b.top()->kind == K::Capture);
    b.set (K::Preview, "preview");
    GF_EXPECT (b.top()->kind == K::Preview);
    b.set (K::Export, "export");
    GF_EXPECT (b.top()->kind == K::Export);
    b.set (K::Learn, "learn");
    GF_EXPECT (b.top()->kind == K::Learn && b.top()->text == "learn");

    // Peel them off in the spec order: learn > export > preview > capture > health > fix-it.
    const K expected[] = { K::Export, K::Preview, K::Capture, K::Health, K::Fixit, K::Hover, K::LastGap };

    for (const auto k : expected)
    {
        b.clear (b.top()->kind);
        GF_EXPECT (b.top()->kind == k);
    }

    // The footer owns health / fix-it / last gap: it asks for what outranks health.
    b.set (K::Learn, "learn");
    b.set (K::Health, "health");
    GF_EXPECT (b.topAbove (K::Health)->kind == K::Learn);
    b.clear (K::Learn);
    GF_EXPECT (! b.topAbove (K::Health).has_value());
}

GF_TEST (Info, footer_selection_with_info_and_hover)
{
    using F = health::FooterKind;
    GF_EXPECT (health::selectFooter ({ true, true, true, true, true }) == F::Info);       // learn etc. beat health
    GF_EXPECT (health::selectFooter ({ true, true, true, false, true }) == F::Health);
    GF_EXPECT (health::selectFooter ({ false, true, true, false, true }) == F::Fixit);
    GF_EXPECT (health::selectFooter ({ false, false, true, false, true }) == F::Hover);
    GF_EXPECT (health::selectFooter ({ false, false, true, false, false }) == F::LastGap);
    GF_EXPECT (health::selectFooter ({}) == F::Nothing);
}

GF_TEST (Info, footer_component_shows_the_board_message)
{
    GapFillAudioProcessor proc;
    auto& board = proc.getUiState().status;

    board.set (info::StatusKind::Hover, "Density 65 %");
    auto view = health::computeFooterView (proc);
    GF_EXPECT (view.kind == health::FooterKind::Hover && view.text == "Density 65 %");

    board.set (info::StatusKind::Learn, "MIDI LEARN | Density");
    view = health::computeFooterView (proc);
    GF_EXPECT (view.kind == health::FooterKind::Info && view.text == "MIDI LEARN | Density");

    board.clear (info::StatusKind::Learn);
    board.clear (info::StatusKind::Hover);
    GF_EXPECT (health::computeFooterView (proc).kind == health::FooterKind::Nothing);
}

GF_TEST (Info, status_updates_rate_limited)
{
    info::UpdateLimiter limiter (50);
    GF_EXPECT (limiter.allow (1000));
    GF_EXPECT (! limiter.allow (1010));
    GF_EXPECT (! limiter.allow (1049));
    GF_EXPECT (limiter.allow (1050));
    GF_EXPECT (! limiter.allow (1099));
    GF_EXPECT (limiter.allow (1100));

    // 1 kHz of mouse moves for one second can write at most 20 lines.
    info::UpdateLimiter l2 (50);
    int writes = 0;
    for (juce::uint32 t = 0; t < 1000; ++t)
        writes += l2.allow (t) ? 1 : 0;
    GF_EXPECT (writes == 20);

    l2.reset();
    GF_EXPECT (l2.allow (5));
}

GF_TEST (Info, hover_describes_name_value_and_effective)
{
    GapFillAudioProcessor proc;
    GapKnob knob (proc.getValueTreeState(), "density", "Density", "x", colours::accentWarm);
    info::HoverStatus hover (proc.getValueTreeState(), proc.getUiState().status);

    GF_EXPECT (info::HoverStatus::parameterIdFor (&knob) == "density");
    GF_EXPECT (info::HoverStatus::parameterIdFor (&knob.getSlider()) == "density");

    hover.hoverForTests (&knob, 0);
    const auto text = proc.getUiState().status.get (info::StatusKind::Hover);
    GF_EXPECT_MSG (text.startsWith ("Density 65"), text);
    GF_EXPECT (! text.contains ("->"));

    // A feature that modulates density installs an effective-value provider.
    info::ParamInfoRegistry::get().setEffectiveProvider ([] (const juce::String& id) -> std::optional<float>
    {
        return id == "density" ? std::optional<float> (90.0f) : std::nullopt;
    });
    hover.hoverForTests (&knob, 100);
    const auto modulated = proc.getUiState().status.get (info::StatusKind::Hover);
    info::ParamInfoRegistry::get().setEffectiveProvider (nullptr);

    GF_EXPECT_MSG (modulated.contains ("->") && modulated.contains ("90"), modulated);

    juce::Component plain;
    hover.hoverForTests (&plain, 200);
    GF_EXPECT_MSG (proc.getUiState().status.get (info::StatusKind::Hover).isEmpty(), "no parameter under the pointer clears the line");
}
