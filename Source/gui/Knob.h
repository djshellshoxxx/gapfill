#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"

namespace gapfill
{

class MidiLearnManager;

//==============================================================================
/** The right-click menu every GapFill control shares.

    Offers MIDI mapping, reset-to-default and typed value entry, as required by
    the shared feature spec. Safe to call with an unknown parameter id or with
    no MIDI-learn manager - the affected items simply do not appear.

    The menu inherits its look from `owner`, and any window it opens is themed
    by colour id rather than by borrowing a LookAndFeel, so nothing outlives the
    editor that created it. */
void showParameterMenu (juce::Component& owner,
                        juce::AudioProcessorValueTreeState& state,
                        const juce::String& parameterID);

/** Applies the GapFill palette to a transient window by colour id. */
void applyPaletteTo (juce::AlertWindow& window);

/** Global editor preference for suppressing nonessential UI animation. */
void setReducedMotionEnabled (bool enabled) noexcept;
bool isReducedMotionEnabled() noexcept;

//==============================================================================
/** Rotary/linear slider with the house feel:
      - vertical drag, shift for coarse, ctrl/cmd for ultra-fine
      - double-click resets to the parameter default
      - right-click opens the shared parameter menu
      - value changes ease over 80ms instead of jumping */
class FineSlider : public juce::Slider,
                   private juce::Timer
{
public:
    FineSlider();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void paint (juce::Graphics&) override;
    void valueChanged() override;

    /** Called for a right-click (or ctrl-click on macOS). */
    std::function<void()> onRightClick;

    /** Called when the pointer enters or leaves, so the owner can show or hide
        the value readout. */
    std::function<void (bool)> onHoverChanged;

    /** Skips the animation - used when the whole UI jumps, e.g. preset loads. */
    void snapToValue();

   #ifdef GAPFILL_UNIT_TESTS
    bool isAnimatingForTests() const noexcept { return isTimerRunning(); }
   #endif

private:
    void timerCallback() override;
    float targetProportion() const;

    float animatedPos = 0.0f;
    bool  initialised = false;
};

//==============================================================================
/** A labelled knob bound to an APVTS parameter.

    Per the identity spec the label sits below the knob and the value readout
    sits above it, appearing only while the knob is hovered or being dragged. */
class GapKnob : public juce::Component
{
public:
    GapKnob (juce::AudioProcessorValueTreeState& state,
             const juce::String& parameterID,
             const juce::String& displayName,
             const juce::String& tooltip,
             juce::Colour accent,
             bool large = false);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }
    void setAccentColour (juce::Colour c);

private:
    void refreshValueText();
    void setReadoutVisible (bool shouldBeVisible);

    juce::AudioProcessorValueTreeState& state;
    juce::String paramID;
    juce::RangedAudioParameter* parameter = nullptr;
    FineSlider slider;
    juce::Label nameLabel, valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool large = false;
    bool readoutVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapKnob)
};

//==============================================================================
/** A combo box that forwards right-clicks to the shared parameter menu. */
class ParamComboBox : public juce::ComboBox
{
public:
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onRightClick;
};

/** A momentary button that flashes the accent colour for 100ms when clicked.

    The spec asks for the flash on a press rather than while held, so it is
    driven by a timer here and read back by the LookAndFeel through a component
    property - that keeps all the actual drawing in one place. */
class FlashButton : public juce::TextButton,
                    private juce::Timer
{
public:
    using juce::TextButton::TextButton;

    static constexpr const char* flashProperty = "gapfillFlash";

    void clicked() override;

private:
    void timerCallback() override;
};

//==============================================================================
/** A text button that forwards right-clicks to the shared parameter menu. */
class ParamButton : public juce::TextButton
{
public:
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onRightClick;
};

//==============================================================================
/** A labelled combo box bound to a choice parameter. */
class GapCombo : public juce::Component
{
public:
    GapCombo (juce::AudioProcessorValueTreeState& state,
              const juce::String& parameterID,
              const juce::String& displayName,
              const juce::String& tooltip);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::ComboBox& getComboBox() noexcept { return combo; }

private:
    ParamComboBox combo;
    juce::Label nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapCombo)
};

//==============================================================================
/** A toggle button bound to a bool parameter. */
class GapToggle : public juce::Component
{
public:
    GapToggle (juce::AudioProcessorValueTreeState& state,
               const juce::String& parameterID,
               const juce::String& displayName,
               const juce::String& tooltip,
               juce::Colour accent = colours::primary);

    void resized() override;

    juce::TextButton& getButton() noexcept { return button; }

private:
    ParamButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapToggle)
};

//==============================================================================
/** A compact horizontal slider with a caption, used in dense lists. */
class GapBar : public juce::Component
{
public:
    GapBar (juce::AudioProcessorValueTreeState& state,
            const juce::String& parameterID,
            const juce::String& displayName,
            const juce::String& tooltip,
            juce::Colour accent);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void refreshValueText();

    juce::AudioProcessorValueTreeState& state;
    juce::String paramID;
    juce::RangedAudioParameter* parameter = nullptr;
    FineSlider slider;
    juce::Label nameLabel, valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapBar)
};

} // namespace gapfill
