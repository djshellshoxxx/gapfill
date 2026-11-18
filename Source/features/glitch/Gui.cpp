#include "Gui.h"

#include "GlitchRack.h"
#include "../../PluginProcessor.h"
#include "../../gui/LookAndFeel.h"
#include "../../gui/SlotHost.h"
#include "../info/ParamInfo.h"
#include "../i18n/Strings.h"

namespace gapfill::glitch
{
namespace
{
    using K = ControlSpec::Kind;

    constexpr int kKnobW = 64, kKnobH = 76, kComboW = 104, kComboH = 40, kToggleW = 92, kToggleH = 28;
    constexpr int kGap = 8, kTitleH = 20, kStatusH = 20;

    juce::String tooltipFor (const char* id)
    {
        if (const auto* info = info::ParamInfoRegistry::get().find (id))
            return info->oneLine;
        return {};
    }

    juce::Point<int> sizeOf (K kind)
    {
        switch (kind)
        {
            case K::Knob:   return { kKnobW, kKnobH };
            case K::Combo:  return { kComboW, kComboH };
            case K::Toggle: return { kToggleW, kToggleH };
            case K::Stab:   return { kToggleW, kKnobH };
        }
        return { kKnobW, kKnobH };
    }
}

const std::vector<SectionSpec>& sectionSpecs()
{
    static const std::vector<SectionSpec> specs {
        { "RACK", { { K::Toggle, "fg01_on", "GLITCH ON" }, { K::Combo, "fg01_target", "TARGET" },
                    { K::Knob, "fg01_mix", "MIX" }, { K::Knob, "fg01_swing", "SWING" },
                    { K::Knob, "fg01_timeVar", "TIME VAR" } } },
        { "STUTTER", { { K::Combo, "fg01_stutMode", "MODE" }, { K::Combo, "fg01_stutDiv", "SLICE" },
                       { K::Knob, "fg01_stutMs", "SLICE MS" }, { K::Knob, "fg01_stutRepeats", "REPEATS" },
                       { K::Knob, "fg01_stutProb", "PROB" }, { K::Knob, "fg01_stutRev", "REVERSE" },
                       { K::Knob, "fg01_stutVar", "VARIATION" }, { K::Knob, "fg01_stutPick", "PICK" },
                       { K::Knob, "fg01_stutGate", "GATE" }, { K::Knob, "fg01_stutDecay", "DECAY" },
                       { K::Knob, "fg01_stutSmooth", "SMOOTH" } } },
        { "STAB", { { K::Stab, "fg01_stab", "STAB" }, { K::Toggle, "fg01_stabInf", "INFINITE" },
                    { K::Combo, "fg01_stabDiv", "SLICE" }, { K::Knob, "fg01_stabRepeats", "REPEATS" },
                    { K::Knob, "fg01_stabDecay", "DECAY" }, { K::Knob, "fg01_stabGate", "GATE" },
                    { K::Knob, "fg01_stabRelease", "RELEASE" } } },
        { "REVERSE", { { K::Combo, "fg01_revMode", "MODE" }, { K::Combo, "fg01_revDiv", "LENGTH" },
                       { K::Knob, "fg01_revMs", "TIME" }, { K::Knob, "fg01_revProb", "PROB" },
                       { K::Knob, "fg01_revMix", "MIX" }, { K::Knob, "fg01_revFade", "FADE" } } },
        { "MICROGATOR", { { K::Combo, "fg01_gateMode", "MODE" }, { K::Combo, "fg01_gateDiv", "RATE" },
                          { K::Combo, "fg01_gatePattern", "PATTERN" }, { K::Knob, "fg01_gateHz", "FREE HZ" },
                          { K::Knob, "fg01_gateLen", "LENGTH" }, { K::Knob, "fg01_gateDepth", "DEPTH" },
                          { K::Knob, "fg01_gateAttack", "ATTACK" }, { K::Knob, "fg01_gateRelease", "RELEASE" },
                          { K::Knob, "fg01_gateVar", "VARIATION" }, { K::Knob, "fg01_gateStereo", "STEREO" } } },
        { "SWEEP FILTER", { { K::Combo, "fg01_filtMode", "FILTER" }, { K::Toggle, "fg01_filtSync", "SYNC" },
                            { K::Combo, "fg01_filtDiv", "CYCLE" }, { K::Combo, "fg01_filtShape", "SHAPE" },
                            { K::Knob, "fg01_filtHz", "FREE HZ" }, { K::Knob, "fg01_filtMin", "MIN" },
                            { K::Knob, "fg01_filtMax", "MAX" }, { K::Knob, "fg01_filtDepth", "DEPTH" },
                            { K::Knob, "fg01_filtRes", "RESO" }, { K::Knob, "fg01_filtStereo", "STEREO" } } },
        { "PHASER", { { K::Combo, "fg01_phStages", "STAGES" }, { K::Toggle, "fg01_phSync", "SYNC" },
                      { K::Combo, "fg01_phDiv", "CYCLE" }, { K::Knob, "fg01_phMix", "MIX" },
                      { K::Knob, "fg01_phHz", "RATE" }, { K::Knob, "fg01_phDepth", "DEPTH" },
                      { K::Knob, "fg01_phFb", "FEEDBACK" }, { K::Knob, "fg01_phCentre", "CENTRE" },
                      { K::Knob, "fg01_phStereo", "STEREO" } } },
        { "PHASE / PAN", { { K::Toggle, "fg01_polL", "INVERT L" }, { K::Toggle, "fg01_polR", "INVERT R" },
                           { K::Combo, "fg01_flipMode", "FLIP" }, { K::Combo, "fg01_flipDiv", "FLIP RATE" },
                           { K::Knob, "fg01_panInv", "PAN INVERT" }, { K::Knob, "fg01_flipProb", "FLIP PROB" } } },
    };
    return specs;
}

//==============================================================================
StabPad::StabPad (juce::RangedAudioParameter* p)
    : juce::TextButton ("STAB"), param (p)
{
    setTitle ("STAB");
    setDescription ("Press to grab and repeat the sound; with Infinite on it repeats while held");
    setTooltip (tooltipFor ("fg01_stab"));
    setWantsKeyboardFocus (false);
}

StabPad::~StabPad()
{
    if (down && param != nullptr)
    {
        param->setValueNotifyingHost (0.0f);
        param->endChangeGesture();
    }
}

void StabPad::buttonStateChanged()
{
    juce::TextButton::buttonStateChanged();
    const bool nowDown = isDown();

    if (param == nullptr || nowDown == down)
        return;

    down = nowDown;

    // Momentary: a gesture spans the press so hosts record press and release as one edit.
    if (down)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (1.0f);
    }
    else
    {
        param->setValueNotifyingHost (0.0f);
        param->endChangeGesture();
    }
}

//==============================================================================
GlitchPanel::GlitchPanel (GapFillAudioProcessor& p)
    : proc (p)
{
    setTitle ("Glitch rack");
    auto& state = proc.getValueTreeState();

    for (const auto& spec : sectionSpecs())
    {
        auto section = std::make_unique<Section>();
        section->title.setText (i18n::text (spec.title), juce::dontSendNotification);
        section->title.setFont (fonts::sectionHeader (11.0f));
        section->title.setColour (juce::Label::textColourId, colours::textMuted);
        addAndMakeVisible (section->title);

        for (const auto& c : spec.controls)
        {
            std::unique_ptr<juce::Component> comp;
            const auto tip = tooltipFor (c.paramId);

            switch (c.kind)
            {
                case K::Knob:   comp = std::make_unique<GapKnob> (state, c.paramId, c.label, tip, colours::primary); break;
                case K::Combo:  comp = std::make_unique<GapCombo> (state, c.paramId, c.label, tip); break;
                case K::Toggle: comp = std::make_unique<GapToggle> (state, c.paramId, c.label, tip); break;
                case K::Stab:   comp = std::make_unique<StabPad> (state.getParameter (c.paramId)); break;
            }

            comp->setComponentID (c.paramId);
            addAndMakeVisible (*comp);
            section->members.push_back (comp.get());
            controls.push_back (std::move (comp));
        }

        sections.push_back (std::move (section));
    }

    status.setFont (fonts::ui (10.0f));
    status.setColour (juce::Label::textColourId, colours::textMuted);
    status.setTitle ("Glitch status");
    addAndMakeVisible (status);

    setSize (600, heightForWidth (600));
    timerCallback();
    startTimerHz (15);
}

GlitchPanel::~GlitchPanel() { stopTimer(); }

int GlitchPanel::heightForWidth (int width) const
{
    return const_cast<GlitchPanel*> (this)->layout (width, false);
}

int GlitchPanel::layout (int width, bool apply)
{
    width = juce::jmax (kKnobW + 2 * kGap, width);
    int y = 0;

    if (apply)
        status.setBounds (0, y, width, kStatusH);
    y += kStatusH + kGap;

    for (auto& section : sections)
    {
        if (apply)
            section->title.setBounds (0, y, width, kTitleH);
        y += kTitleH;

        int x = 0, rowH = 0;

        for (auto* member : section->members)
        {
            const auto spec = [&]
            {
                for (const auto& s : sectionSpecs())
                    for (const auto& c : s.controls)
                        if (member->getComponentID() == c.paramId)
                            return c.kind;
                return K::Knob;
            }();
            const auto size = sizeOf (spec);

            if (x > 0 && x + size.x > width)
            {
                x = 0;
                y += rowH + kGap;
                rowH = 0;
            }

            if (apply)
                member->setBounds (x, y + (spec == K::Toggle ? (kKnobH - kToggleH) / 2 : 0), size.x, size.y);

            x += size.x + kGap;
            rowH = juce::jmax (rowH, kKnobH);
        }

        y += rowH + 2 * kGap;
    }

    return y;
}

void GlitchPanel::resized()
{
    const auto needed = layout (getWidth(), true);

    // Stack slot: the panel owns its height; the host re-lays out when it changes.
    if (needed != getHeight())
        setSize (getWidth(), needed);
}

void GlitchPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::edge);

    for (auto& section : sections)
        g.fillRect (section->title.getBounds().removeFromBottom (1));   // hairline under each section title
}

void GlitchPanel::timerCallback()
{
    const auto& m = proc.getGlitchMeters();
    juce::StringArray parts;

    if (! m.rackActive.load (std::memory_order_relaxed))
        parts.add (i18n::text ("Glitch rack idle"));
    else
    {
        parts.add (i18n::text ("Glitch rack active"));

        if (m.stutterActive.load (std::memory_order_relaxed))
            parts.add (i18n::text ("stutter repeating"));

        if (m.stabRunning.load (std::memory_order_relaxed))
            parts.add (i18n::format ("STAB repeat {0}", { juce::String (m.stabRepeat.load (std::memory_order_relaxed)) }));

        if (const auto hz = m.cutoffHz.load (std::memory_order_relaxed); hz > 0.0f)
            parts.add (i18n::format ("cutoff {0} Hz", { juce::String (juce::roundToInt (hz)) }));
    }

    status.setText (parts.joinIntoString ("  |  "), juce::dontSendNotification);
}

namespace
{
    const SlotRegistrar glitchTab ({ Slot::GlitchTab, 100,
        [] (GapFillAudioProcessor& p) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<GlitchPanel> (p);
        } });
}

} // namespace gapfill::glitch
