#pragma once

// U-8 / F-S03 UI scale: pure helpers. The preference itself is the per-user global
// setting `uiScalePercent`; the editor composes host scale x user scale and applies it
// with AudioProcessorEditor::setScaleFactor. The saved editor size stays logical.

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace gapfill::uiscale
{

inline constexpr std::array<int, 5> kSteps { 100, 125, 150, 175, 200 };

/** Nearest valid step; anything unusable becomes 100. */
int snapScalePercent (int requested) noexcept;

/** Largest step <= snap(requested) such that the scaled editor fits 95 % of the work area.
    Falls back to 100 when even that does not fit (the window may then exceed the screen). */
int clampScalePercent (int requested, juce::Rectangle<int> workArea, juce::Point<int> logicalSize, float hostScale) noexcept;

float effectiveScale (float hostScale, int percent) noexcept;

} // namespace gapfill::uiscale
