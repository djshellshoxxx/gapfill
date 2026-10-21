#pragma once

// F-D04 accessibility and contrast presets (docs/dev/12 F-D04).
//
//  Contrast      Standard / High / Dim-venue palettes (token sets from docs/theme.md)
//  Control size  Standard / Large: hit targets >= 32 px and fonts about 2 px bigger, done through the
//                U-8 scale path (the editor multiplies its scale by controlScaleFactor)
//  Focus ring    2 px / 3 px
//
// All three live in the per-user global settings file (never in a project). The palettes are plain
// data so the contrast ratios can be computed in tests: High meets WCAG 2.2 AA (text >= 4.5:1,
// UI components and focus >= 3:1); Dim-venue keeps text >= 4.5:1 with a peak luminance of about 60 %.

#include "../presetbrowser/GlobalSettings.h"

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace gapfill::access
{

enum class Contrast { Standard = 0, High = 1, DimVenue = 2 };
enum class ControlSize { Standard = 0, Large = 1 };

inline constexpr const char* kKeyContrast = settings::GlobalSettings::keyContrast;
inline constexpr const char* kKeyControlSize = settings::GlobalSettings::keyControlSize;
inline constexpr const char* kKeyFocusRing = settings::GlobalSettings::keyFocusRing;

/** Editor scale multiplier for Large controls: 28 px buttons become >= 32 px, 11 px text about 13 px. */
inline constexpr float kLargeControlFactor = 1.18f;

struct Palette
{
    juce::Colour background, panel, edge, primary, secondary, text, textMuted, success, warning, clip,
                 knobTop, knobBottom, waveform;
};

const Palette& paletteFor (Contrast) noexcept;

/** Assigns the `colours::` tokens (and their aliases). Message thread only. */
void applyPalette (Contrast);

/** WCAG relative luminance and contrast ratio. */
double relativeLuminance (juce::Colour) noexcept;
double contrastRatio (juce::Colour, juce::Colour) noexcept;

/** A foreground/background token pair with the ratio WCAG asks for. */
struct TokenPair
{
    const char* name;
    juce::Colour Palette::* fg;
    juce::Colour Palette::* bg;
    double minimum;                 ///< 4.5 text, 3.0 UI component / focus indicator
    bool highOnly;                  ///< only required for the High preset (e.g. component outlines)
};

const std::vector<TokenPair>& tokenPairs();

/** Highest relative luminance of any foreground token (Dim-venue is capped relative to Standard). */
double peakLuminance (const Palette&) noexcept;

//--- settings access -----------------------------------------------------------
Contrast readContrast (const settings::GlobalSettings&);
ControlSize readControlSize (const settings::GlobalSettings&);
int readFocusRingPx (const settings::GlobalSettings&);       ///< 2 or 3

/** Reads the three settings and applies them to the tokens and the focus ring. Returns the control-size
    scale multiplier (1.0 or kLargeControlFactor) for the editor's scale path. */
float applyFromSettings (const settings::GlobalSettings&);

} // namespace gapfill::access
