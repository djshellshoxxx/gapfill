#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace gapfill
{

//==============================================================================
/** The product version, taken from project() in CMakeLists.txt so the build
    file stays the single source of truth. The fallback only matters for a
    translation unit compiled outside that build. */
#ifndef GAPFILL_VERSION
 #define GAPFILL_VERSION "0.0.0"
#endif

inline juce::String versionString()    { return juce::String (GAPFILL_VERSION); }

//==============================================================================
/** The GapFill palette.

    These values come straight from the shared visual identity spec (theme.md).
    The neutrals, the two accents and the status colours are common to every
    plugin in the range; GapFill keeps the house burnt-orange as its primary. */
namespace colours
{
    // --- core palette (identical across the plugin range) -------------------
    const juce::Colour background   { 0xff0e1116 };   // near-black, blue undertone
    const juce::Colour panel        { 0xff171b22 };   // raised sections
    const juce::Colour edge         { 0xff2a303a };   // 1px strokes / separators
    const juce::Colour primary      { 0xffe8532a };   // burnt orange
    const juce::Colour secondary    { 0xff4fb6c4 };   // muted teal
    const juce::Colour text         { 0xffe6e8ec };
    const juce::Colour textMuted    { 0xff8a929e };
    const juce::Colour success      { 0xff7bc96f };
    const juce::Colour warning      { 0xfff2c14e };
    const juce::Colour clip         { 0xffe5484d };
    const juce::Colour shadow       { 0x8c000000 };   // rgba(0,0,0,0.55)

    // --- knob body gradient -------------------------------------------------
    const juce::Colour knobTop      { 0xff232833 };
    const juce::Colour knobBottom   { 0xff14181f };

    // --- semantic aliases used by the panels --------------------------------
    // GapFill's own reading of the palette: space/detection is the secondary
    // teal, generated fills are the primary accent, protection is the warning
    // colour and "signal present" is the success colour.
    const juce::Colour panelRaised  = knobTop;
    const juce::Colour outline      = edge;
    const juce::Colour textDim      = textMuted;
    const juce::Colour accent       = secondary;      // detection / usable space
    const juce::Colour accentWarm   = primary;        // generated fills
    const juce::Colour accentAmber  = warning;        // protection
    const juce::Colour accentGreen  = success;        // signal on
    const juce::Colour waveform     { 0xff5a6472 };
    const juce::Colour gapFillTint  { 0x334fb6c4 };

    inline juce::Colour bandColour (int band)
    {
        switch (band)
        {
            case 0:  return primary;     // low
            case 1:  return success;     // mid
            default: return secondary;   // high
        }
    }
}

//==============================================================================
/** Shared metrics. Everything is a multiple of the 8px base grid (or 4px for
    fine detail), so panels line up without each one inventing its own spacing. */
namespace metrics
{
    constexpr int   grid           = 8;
    constexpr int   halfGrid       = 4;
    constexpr int   edgePadding    = 16;   // window edge -> first control
    constexpr int   headerHeight   = 32;
    constexpr int   buttonHeight   = 28;

    constexpr int   knobSmall      = 36;
    constexpr int   knobDefault    = 48;
    constexpr int   knobLarge      = 64;

    // The spec asks for 6px on the main window. A hosted editor cannot honour
    // it - the host owns the window frame and rounding our own corners would
    // just expose whatever is behind them - so this is kept for reference and
    // deliberately unused.
    constexpr float windowRadius   = 6.0f;
    constexpr float panelRadius    = 4.0f;
    constexpr float controlRadius  = 2.0f;

    constexpr int   tooltipDelayMs = 400;
    constexpr int   animationMs    = 80;
    constexpr int   flashMs        = 100;

    /** 270 degree sweep, centred on 6 o'clock. */
    constexpr float rotaryStart    = juce::MathConstants<float>::pi * 1.25f;
    constexpr float rotaryEnd      = juce::MathConstants<float>::pi * 2.75f;
}

//==============================================================================
/** Typography per the identity spec.

    The spec asks for Inter (falling back to Space Grotesk then the system sans)
    and JetBrains Mono (falling back to IBM Plex Mono then the system mono). We
    resolve those names against the fonts actually installed once, at startup,
    and fall through the chain rather than bundling binary font data. */
namespace fonts
{
    /** UI text. `semiBold` gives the weight-600 used by section headers. */
    juce::Font ui (float height, bool semiBold = false);

    /** Numeric readouts - monospaced, so digits do not jitter as values change. */
    juce::Font mono (float height);

    /** 11px uppercase control label with the +0.08em tracking from the spec. */
    juce::Font label (float height = 11.0f);

    /** Uppercase section header, weight 600. */
    juce::Font sectionHeader (float height = 11.0f);

    /** Mixed-case value readout, no tracking. */
    juce::Font value (float height = 13.0f);
}

//==============================================================================
class GapFillLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GapFillLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    //==========================================================================
    // Shared drawing helpers, so every panel agrees on the house style.

    /** The framed panels used everywhere: 4px radius, 1px edge, section header
        with the 2x12px accent bar to its left. */
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds,
                           const juce::String& title, juce::Colour tint);

    /** A 1px horizontal separator - the only thing we use to divide sections. */
    static void drawSeparator (juce::Graphics&, juce::Rectangle<float> area);

    /** The house brand mark: a 2px accent diagonal notch, 12px long, in the
        top-left corner of the main window. */
    static void drawSignatureNotch (juce::Graphics&, juce::Rectangle<float> windowBounds);

    /** Soft drop shadow matching the spec (8px blur, y+2). */
    static void drawShadow (juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius);

    /** The knob/thumb body gradient, shared by knobs and slider thumbs. */
    static juce::ColourGradient bodyGradient (juce::Rectangle<float> bounds);
};

} // namespace gapfill
