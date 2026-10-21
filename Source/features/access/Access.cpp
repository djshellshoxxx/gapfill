#include "Access.h"

#include "../../PluginProcessor.h"
#include "../../gui/HelpRegistry.h"
#include "../../gui/Knob.h"
#include "../../gui/SlotHost.h"

namespace gapfill::access
{

namespace
{
    Palette make (juce::uint32 bg, juce::uint32 panel, juce::uint32 edge, juce::uint32 primary, juce::uint32 secondary,
                  juce::uint32 text, juce::uint32 muted, juce::uint32 success, juce::uint32 warning, juce::uint32 clip,
                  juce::uint32 knobTop, juce::uint32 knobBottom, juce::uint32 waveform)
    {
        return { juce::Colour (0xff000000 | bg), juce::Colour (0xff000000 | panel), juce::Colour (0xff000000 | edge),
                 juce::Colour (0xff000000 | primary), juce::Colour (0xff000000 | secondary), juce::Colour (0xff000000 | text),
                 juce::Colour (0xff000000 | muted), juce::Colour (0xff000000 | success), juce::Colour (0xff000000 | warning),
                 juce::Colour (0xff000000 | clip), juce::Colour (0xff000000 | knobTop), juce::Colour (0xff000000 | knobBottom),
                 juce::Colour (0xff000000 | waveform) };
    }
}

const Palette& paletteFor (Contrast c) noexcept
{
    // Standard is docs/theme.md. High lifts text to white, brightens the accents and draws component
    // outlines at >= 3:1. Dim-venue scales the backgrounds and brightest tokens down to about 60 % luminance
    // while keeping every text pair above 4.5:1.
    static const Palette standard = make (0x0e1116, 0x171b22, 0x2a303a, 0xe8532a, 0x4fb6c4, 0xe6e8ec, 0x8a929e,
                                          0x7bc96f, 0xf2c14e, 0xe5484d, 0x232833, 0x14181f, 0x5a6472);
    static const Palette high     = make (0x000000, 0x0a0c10, 0x7f8896, 0xff7a52, 0x6fd3e2, 0xffffff, 0xb4bbc6,
                                          0x8fe083, 0xffd566, 0xff6b70, 0x161a21, 0x0a0c10, 0x9aa3b0);
    static const Palette dim      = make (0x080a0d, 0x0d1014, 0x2a303a, 0xcc6141, 0x539ea9, 0xb8b9bb, 0x8c919a,
                                          0x5f9e57, 0xb08d38, 0xc4484c, 0x15181e, 0x0c0e12, 0x4a525c);

    switch (c)
    {
        case Contrast::High:     return high;
        case Contrast::DimVenue: return dim;
        case Contrast::Standard:
        default:                 return standard;
    }
}

void applyPalette (Contrast c)
{
    const auto& p = paletteFor (c);

    colours::background = p.background;
    colours::panel = p.panel;
    colours::edge = p.edge;
    colours::primary = p.primary;
    colours::secondary = p.secondary;
    colours::text = p.text;
    colours::textMuted = p.textMuted;
    colours::success = p.success;
    colours::warning = p.warning;
    colours::clip = p.clip;
    colours::shadow = juce::Colour (0x8c000000);
    colours::knobTop = p.knobTop;
    colours::knobBottom = p.knobBottom;

    colours::panelRaised = p.knobTop;
    colours::outline = p.edge;
    colours::textDim = p.textMuted;
    colours::accent = p.secondary;
    colours::accentWarm = p.primary;
    colours::accentAmber = p.warning;
    colours::accentGreen = p.success;
    colours::waveform = p.waveform;
    colours::gapFillTint = p.secondary.withAlpha (0.2f);
}

double relativeLuminance (juce::Colour c) noexcept
{
    const auto lin = [] (float v)
    {
        return v <= 0.03928f ? (double) v / 12.92 : std::pow (((double) v + 0.055) / 1.055, 2.4);
    };

    return 0.2126 * lin (c.getFloatRed()) + 0.7152 * lin (c.getFloatGreen()) + 0.0722 * lin (c.getFloatBlue());
}

double contrastRatio (juce::Colour a, juce::Colour b) noexcept
{
    auto la = relativeLuminance (a), lb = relativeLuminance (b);

    if (la < lb)
        std::swap (la, lb);

    return (la + 0.05) / (lb + 0.05);
}

const std::vector<TokenPair>& tokenPairs()
{
    static const std::vector<TokenPair> pairs =
    {
        // text, 4.5:1
        { "text on background",        &Palette::text,      &Palette::background, 4.5, false },
        { "text on panel",             &Palette::text,      &Palette::panel,      4.5, false },
        { "text on knob",              &Palette::text,      &Palette::knobTop,    4.5, false },
        { "muted text on background",  &Palette::textMuted, &Palette::background, 4.5, false },
        { "muted text on panel",       &Palette::textMuted, &Palette::panel,      4.5, false },
        { "muted text on knob",        &Palette::textMuted, &Palette::knobTop,    4.5, false },
        // UI components and the focus indicator, 3:1
        { "primary on background",     &Palette::primary,   &Palette::background, 3.0, false },
        { "primary on panel",          &Palette::primary,   &Palette::panel,      3.0, false },
        { "secondary on background",   &Palette::secondary, &Palette::background, 3.0, false },
        { "secondary on panel",        &Palette::secondary, &Palette::panel,      3.0, false },
        { "success on panel",          &Palette::success,   &Palette::panel,      3.0, false },
        { "warning on panel",          &Palette::warning,   &Palette::panel,      3.0, false },
        { "clip on panel",             &Palette::clip,      &Palette::panel,      3.0, false },
        { "component outline on panel", &Palette::edge,     &Palette::panel,      3.0, true  },
        { "component outline on background", &Palette::edge, &Palette::background, 3.0, true },
    };

    return pairs;
}

double peakLuminance (const Palette& p) noexcept
{
    double peak = 0.0;

    for (const auto& c : { p.text, p.textMuted, p.primary, p.secondary, p.success, p.warning, p.clip })
        peak = juce::jmax (peak, relativeLuminance (c));

    return peak;
}

//==============================================================================
Contrast readContrast (const settings::GlobalSettings& s)
{
    const auto v = s.getInt (kKeyContrast, 0);
    return v == 1 ? Contrast::High : v == 2 ? Contrast::DimVenue : Contrast::Standard;
}

ControlSize readControlSize (const settings::GlobalSettings& s)
{
    return s.getInt (kKeyControlSize, 0) == 1 ? ControlSize::Large : ControlSize::Standard;
}

int readFocusRingPx (const settings::GlobalSettings& s)
{
    return s.getInt (kKeyFocusRing, 2) == 3 ? 3 : 2;
}

float applyFromSettings (const settings::GlobalSettings& s)
{
    applyPalette (readContrast (s));
    focusRingWidth = (float) readFocusRingPx (s);
    return readControlSize (s) == ControlSize::Large ? kLargeControlFactor : 1.0f;
}

//==============================================================================
namespace
{
    class AccessOptions : public juce::Component
    {
    public:
        explicit AccessOptions (GapFillAudioProcessor&) : settingsFile (settings::GlobalSettings::shared())
        {
            setTitle ("Accessibility");

            addRow (contrastLabel, contrastBox, "Contrast", { "Standard", "High", "Dim venue" },
                    "Standard, High (WCAG AA colours) or Dim venue (about 60 % brightness).",
                    (int) readContrast (*settingsFile), kKeyContrast);
            addRow (sizeLabel, sizeBox, "Control size", { "Standard", "Large" },
                    "Large makes controls at least 32 px tall and text about 2 px bigger.",
                    (int) readControlSize (*settingsFile), kKeyControlSize);
            addRow (focusLabel, focusBox, "Focus ring", { "2 px", "3 px" },
                    "Thickness of the keyboard focus outline.", readFocusRingPx (*settingsFile) == 3 ? 1 : 0, kKeyFocusRing);
        }

        void resized() override
        {
            auto b = getLocalBounds();

            for (auto [label, box] : { std::pair<juce::Label*, juce::ComboBox*> { &contrastLabel, &contrastBox },
                                       { &sizeLabel, &sizeBox }, { &focusLabel, &focusBox } })
            {
                auto row = b.removeFromTop (28);
                label->setBounds (row.removeFromLeft (150));
                box->setBounds (row.removeFromLeft (130));
            }
        }

    private:
        void addRow (juce::Label& label, juce::ComboBox& box, const juce::String& name, const juce::StringArray& items,
                     const juce::String& tip, int selected, const char* key)
        {
            label.setText (name, juce::dontSendNotification);
            label.setFont (fonts::ui (12.5f));
            label.setColour (juce::Label::textColourId, colours::text);
            addAndMakeVisible (label);

            box.addItemList (items, 1);
            box.setSelectedId (selected + 1, juce::dontSendNotification);
            box.setTitle (name);
            box.setTooltip (tip);
            box.setDescription (tip);
            box.onChange = [this, key, &box]
            {
                const auto index = box.getSelectedId() - 1;
                const juce::String k (key);
                const auto value = k == kKeyFocusRing ? (index == 1 ? 3 : 2) : index;
                settingsFile->setInt (key, value);          // the editor applies it on its next tick
            };
            addAndMakeVisible (box);
        }

        std::shared_ptr<settings::GlobalSettings> settingsFile;
        juce::Label contrastLabel, sizeLabel, focusLabel;
        juce::ComboBox contrastBox, sizeBox, focusBox;
    };

    const SlotRegistrar accessOptions ({ Slot::OptionsExtra, 200,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            auto c = std::make_unique<AccessOptions> (proc);
            c->setSize (400, 90);
            return c;
        } });

    const HelpRegistrar helpAccess (1040, "Accessibility", [] (HelpWriter& w)
    {
        w.body ("Options > Accessibility changes how GapFill looks for you only (it is never saved in a project). "
                "Contrast: Standard, High (text at least 4.5:1 and controls at least 3:1, WCAG 2.2 AA) or Dim venue "
                "(the same readability at about 60 percent brightness). Control size: Large makes controls at least 32 px tall and text "
                "about 2 px bigger. Focus ring: 2 or 3 px keyboard outline. Switch on \"Enable keyboard navigation\" to reach every control with Tab.");
    });
}

} // namespace gapfill::access
