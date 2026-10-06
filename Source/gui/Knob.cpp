#include "Knob.h"
#include "../MidiLearn.h"

namespace gapfill
{

namespace
{
    // Vertical drag distances. The spec asks for shift = coarse and
    // ctrl/cmd = ultra-fine, so a plain drag sits between the two.
    constexpr int kNormalSensitivity = 240;
    constexpr int kCoarseSensitivity = 90;
    constexpr int kUltraFineSensitivity = 4000;

    constexpr int kMenuReset      = 1;
    constexpr int kMenuSetValue   = 2;
    constexpr int kMenuMidiLearn  = 3;
    constexpr int kMenuMidiClear  = 4;
    constexpr int kMenuMidiClearAll = 5;

    std::atomic<bool> reducedMotion { false };

    /** Asks the user for a value and applies it to the parameter.

        The parameter is captured by pointer: it is owned by the processor and
        therefore outlives both the window and the editor. */
    void promptForValue (juce::RangedAudioParameter& parameter)
    {
        auto* window = new juce::AlertWindow ("Set " + parameter.getName (32),
                                              "Enter a new value:",
                                              juce::MessageBoxIconType::NoIcon);
        applyPaletteTo (*window);

        window->addTextEditor ("value", parameter.getCurrentValueAsText(), {});
        window->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        // Modal loops are disabled in this build, so everything is async.
        window->enterModalState (true, juce::ModalCallbackFunction::create (
            [window, target = &parameter] (int result)
            {
                const auto text = window->getTextEditorContents ("value").trim();
                delete window;

                if (result != 1 || text.isEmpty())
                    return;

                const auto normalised = target->getValueForText (text);

                if (! std::isfinite (normalised))
                    return;

                target->beginChangeGesture();
                target->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
                target->endChangeGesture();
            }), false);
    }
}

//==============================================================================
void applyPaletteTo (juce::AlertWindow& window)
{
    window.setColour (juce::AlertWindow::backgroundColourId, colours::panel);
    window.setColour (juce::AlertWindow::textColourId,       colours::text);
    window.setColour (juce::AlertWindow::outlineColourId,    colours::edge);
}

void setReducedMotionEnabled (bool enabled) noexcept
{
    reducedMotion.store (enabled, std::memory_order_relaxed);
}

bool isReducedMotionEnabled() noexcept
{
    return reducedMotion.load (std::memory_order_relaxed);
}

//==============================================================================
void FlashButton::clicked()
{
    juce::TextButton::clicked();

    if (isReducedMotionEnabled())
    {
        stopTimer();
        getProperties().set (flashProperty, false);
        repaint();
        return;
    }

    getProperties().set (flashProperty, true);
    repaint();
    startTimer (metrics::flashMs);
}

void FlashButton::timerCallback()
{
    stopTimer();
    getProperties().set (flashProperty, false);
    repaint();
}

//==============================================================================
void showParameterMenu (juce::Component& owner,
                        juce::AudioProcessorValueTreeState& state,
                        const juce::String& parameterID)
{
    auto* parameter = state.getParameter (parameterID);

    if (parameter == nullptr)
        return;

    auto* midiLearn = findMidiLearnManager (state);
    const auto mappedCC = midiLearn != nullptr ? midiLearn->getMappedCC (parameterID) : -1;

    // The menu picks up the owner's LookAndFeel via withTargetComponent, so we
    // never store a pointer to it.
    juce::PopupMenu menu;

    menu.addSectionHeader (parameter->getName (32));

    const auto defaultText = parameter->getText (parameter->getDefaultValue(), 24);
    menu.addItem (kMenuReset, "Reset to default (" + defaultText + ")");
    menu.addItem (kMenuSetValue, "Set value...");

    if (midiLearn != nullptr)
    {
        menu.addSeparator();

        if (midiLearn->isLearning() && midiLearn->getLearningParameterID() == parameterID)
            menu.addItem (kMenuMidiLearn, "Waiting for a MIDI controller... (click to cancel)");
        else
            menu.addItem (kMenuMidiLearn, "MIDI learn");

        menu.addItem (kMenuMidiClear, mappedCC >= 0 ? "Forget MIDI CC " + juce::String (mappedCC)
                                                    : juce::String ("Forget MIDI CC"),
                      mappedCC >= 0);

        menu.addItem (kMenuMidiClearAll, "Clear all MIDI mappings",
                      midiLearn->getNumMappings() > 0);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner),
                        [parameter, midiLearn, parameterID] (int result)
    {
        switch (result)
        {
            case kMenuReset:
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
                parameter->endChangeGesture();
                break;

            case kMenuSetValue:
                promptForValue (*parameter);
                break;

            case kMenuMidiLearn:
                if (midiLearn != nullptr)
                {
                    if (midiLearn->isLearning() && midiLearn->getLearningParameterID() == parameterID)
                        midiLearn->stopLearning();
                    else
                        midiLearn->startLearning (parameterID);
                }
                break;

            case kMenuMidiClear:
                if (midiLearn != nullptr)
                    midiLearn->clearMapping (parameterID);
                break;

            case kMenuMidiClearAll:
                if (midiLearn != nullptr)
                    midiLearn->clearAllMappings();
                break;

            default:
                break;
        }
    });
}

//==============================================================================
FineSlider::FineSlider()
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (metrics::rotaryStart, metrics::rotaryEnd, true);
    setMouseDragSensitivity (kNormalSensitivity);
    setWantsKeyboardFocus (false);
    setScrollWheelEnabled (true);
    setVelocityBasedMode (false);

    // The spec asks for a vertical-resize cursor over knobs and sliders.
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void FineSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick != nullptr)
            onRightClick();

        return;
    }

    setMouseDragSensitivity (e.mods.isCommandDown() ? kUltraFineSensitivity
                                                    : (e.mods.isShiftDown() ? kCoarseSensitivity
                                                                            : kNormalSensitivity));
    juce::Slider::mouseDown (e);
}

void FineSlider::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    setMouseDragSensitivity (kNormalSensitivity);
}

void FineSlider::mouseEnter (const juce::MouseEvent& e)
{
    juce::Slider::mouseEnter (e);

    if (onHoverChanged != nullptr)
        onHoverChanged (true);
}

void FineSlider::mouseExit (const juce::MouseEvent& e)
{
    juce::Slider::mouseExit (e);

    if (onHoverChanged != nullptr)
        onHoverChanged (false);
}

float FineSlider::targetProportion() const
{
    const auto range = getRange();

    if (range.getLength() <= 0.0)
        return 0.0f;

    // Slider::valueToProportionOfLength is non-const, so go via the range
    // directly - it honours the same skew.
    return (float) juce::jlimit (0.0, 1.0, getNormalisableRange().convertTo0to1 (getValue()));
}

void FineSlider::snapToValue()
{
    animatedPos = targetProportion();
    initialised = true;
    stopTimer();
    repaint();
}

void FineSlider::valueChanged()
{
    juce::Slider::valueChanged();

    if (! initialised)
    {
        snapToValue();
        return;
    }

    if (isReducedMotionEnabled())
    {
        snapToValue();
        return;
    }

    // Dragging should track the mouse exactly; everything else eases in.
    if (isMouseButtonDown())
    {
        animatedPos = targetProportion();
        repaint();
        return;
    }

    if (! isTimerRunning())
        startTimerHz (60);
}

void FineSlider::timerCallback()
{
    const auto target = targetProportion();
    const auto delta = target - animatedPos;

    if (std::abs (delta) < 0.0005f)
    {
        animatedPos = target;
        stopTimer();
    }
    else
    {
        // Exponential ease-out that settles in roughly the 80ms the spec asks
        // for at this timer rate.
        animatedPos += delta * 0.465f;
    }

    repaint();
}

void FineSlider::paint (juce::Graphics& g)
{
    if (! initialised)
        snapToValue();

    auto& lf = getLookAndFeel();
    const auto style = getSliderStyle();
    const auto w = getWidth();
    const auto h = getHeight();

    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearVertical)
    {
        // Keep the 16px thumb fully inside the track at both ends.
        constexpr float inset = 8.0f;

        if (style == juce::Slider::LinearHorizontal)
        {
            const auto minPos = inset;
            const auto maxPos = juce::jmax (inset, (float) w - inset);
            const auto pos = minPos + animatedPos * (maxPos - minPos);
            lf.drawLinearSlider (g, 0, 0, w, h, pos, minPos, maxPos, style, *this);
        }
        else
        {
            const auto minPos = juce::jmax (inset, (float) h - inset);
            const auto maxPos = inset;
            const auto pos = minPos + animatedPos * (maxPos - minPos);
            lf.drawLinearSlider (g, 0, 0, w, h, pos, minPos, maxPos, style, *this);
        }
    }
    else
    {
        lf.drawRotarySlider (g, 0, 0, w, h, animatedPos,
                             metrics::rotaryStart, metrics::rotaryEnd, *this);
    }
}

//==============================================================================
GapKnob::GapKnob (juce::AudioProcessorValueTreeState& s,
                  const juce::String& parameterID,
                  const juce::String& displayName,
                  const juce::String& tooltip,
                  juce::Colour accent,
                  bool isLarge)
    : state (s), paramID (parameterID), large (isLarge)
{
    parameter = state.getParameter (parameterID);

    addAndMakeVisible (slider);
    slider.setName (displayName);
    slider.setDescription (tooltip);
    slider.setColour (juce::Slider::thumbColourId, accent);
    slider.setTooltip (tooltip);

    if (parameter != nullptr)
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    slider.onValueChange = [this] { refreshValueText(); };
    slider.onRightClick = [this] { showParameterMenu (*this, state, paramID); };
    slider.onHoverChanged = [this] (bool over) { setReadoutVisible (over); };

    // Label below the knob, always visible.
    nameLabel.setText (displayName.toUpperCase(), juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, colours::textMuted);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    // Value above the knob, revealed on hover or during a drag.
    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setColour (juce::Label::textColourId, colours::text);
    valueLabel.setInterceptsMouseClicks (false, false);
    valueLabel.setAlpha (0.0f);
    addAndMakeVisible (valueLabel);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);
    refreshValueText();
}

void GapKnob::setAccentColour (juce::Colour c)
{
    slider.setColour (juce::Slider::thumbColourId, c);
    repaint();
}

void GapKnob::setReadoutVisible (bool shouldBeVisible)
{
    if (readoutVisible == shouldBeVisible)
        return;

    readoutVisible = shouldBeVisible;

    auto& animator = juce::Desktop::getInstance().getAnimator();
    animator.cancelAnimation (&valueLabel, false);

    if (isReducedMotionEnabled())
    {
        valueLabel.setAlpha (shouldBeVisible ? 1.0f : 0.0f);
        return;
    }

    // 80ms fade, matching the rest of the interaction feel.
    animator.animateComponent (&valueLabel, valueLabel.getBounds(),
                               shouldBeVisible ? 1.0f : 0.0f,
                               (int) metrics::animationMs, false, 1.0, 0.0);
}

void GapKnob::refreshValueText()
{
    if (parameter != nullptr)
        valueLabel.setText (parameter->getCurrentValueAsText(), juce::dontSendNotification);
}

void GapKnob::resized()
{
    auto b = getLocalBounds();
    const auto valueHeight = large ? 16 : 14;
    const auto nameHeight  = large ? 15 : 13;

    valueLabel.setBounds (b.removeFromTop (valueHeight));
    nameLabel.setBounds (b.removeFromBottom (nameHeight));

    // The spec sizes knobs at 48px (64px large). The editor is resizable, so
    // rather than pinning them we cap growth at the same 1.6x the LookAndFeel
    // stops scaling the arc and indicator at - past that the dial would keep
    // growing while its details did not, and the proportions would drift.
    const auto maxDial = (int) std::lround ((large ? metrics::knobLarge
                                                   : metrics::knobDefault) * 1.6f);
    const auto size = juce::jmin (b.getWidth(), b.getHeight(), maxDial);
    slider.setBounds (b.withSizeKeepingCentre (size, size));

    // 11px uppercase label, 13-14px mixed-case mono value, per the spec.
    nameLabel.setFont (fonts::label ((float) nameHeight * 0.85f));
    valueLabel.setFont (fonts::value (large ? 14.0f : 12.5f));
}

void GapKnob::paint (juce::Graphics&) {}

//==============================================================================
void ParamComboBox::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick != nullptr)
    {
        onRightClick();
        return;
    }

    juce::ComboBox::mouseDown (e);
}

void ParamButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick != nullptr)
    {
        onRightClick();
        return;
    }

    juce::TextButton::mouseDown (e);
}

//==============================================================================
GapCombo::GapCombo (juce::AudioProcessorValueTreeState& state,
                    const juce::String& parameterID,
                    const juce::String& displayName,
                    const juce::String& tooltip)
{
    nameLabel.setText (displayName.toUpperCase(), juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, colours::textMuted);
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (nameLabel);

    combo.setName (displayName);
    combo.setDescription (tooltip);
    combo.setTooltip (tooltip);
    combo.setWantsKeyboardFocus (false);
    combo.onRightClick = [this, &state, parameterID]
    {
        showParameterMenu (*this, state, parameterID);
    };
    addAndMakeVisible (combo);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, parameterID, combo);
}

void GapCombo::resized()
{
    auto b = getLocalBounds();
    nameLabel.setBounds (b.removeFromTop (14));
    nameLabel.setFont (fonts::label (10.5f));
    combo.setBounds (b.reduced (0, 1));
}

void GapCombo::paint (juce::Graphics&) {}

//==============================================================================
GapToggle::GapToggle (juce::AudioProcessorValueTreeState& state,
                      const juce::String& parameterID,
                      const juce::String& displayName,
                      const juce::String& tooltip,
                      juce::Colour accent)
{
    button.setButtonText (displayName);
    button.setName (displayName);
    button.setDescription (tooltip);
    button.setClickingTogglesState (true);
    button.setTooltip (tooltip);
    button.setWantsKeyboardFocus (false);
    button.setColour (juce::TextButton::buttonOnColourId, accent);
    button.setColour (juce::TextButton::textColourOnId, accent);
    button.onRightClick = [this, &state, parameterID]
    {
        showParameterMenu (*this, state, parameterID);
    };
    addAndMakeVisible (button);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterID, button);
}

void GapToggle::resized()
{
    button.setBounds (getLocalBounds());
}

//==============================================================================
GapBar::GapBar (juce::AudioProcessorValueTreeState& s,
                const juce::String& parameterID,
                const juce::String& displayName,
                const juce::String& tooltip,
                juce::Colour accent)
    : state (s), paramID (parameterID)
{
    parameter = state.getParameter (parameterID);

    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setName (displayName);
    slider.setDescription (tooltip);
    slider.setColour (juce::Slider::thumbColourId, accent);
    slider.setTooltip (tooltip);
    slider.onValueChange = [this] { refreshValueText(); };
    slider.onRightClick = [this] { showParameterMenu (*this, state, paramID); };

    if (parameter != nullptr)
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    addAndMakeVisible (slider);

    nameLabel.setText (displayName.toUpperCase(), juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, colours::textMuted);
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setColour (juce::Label::textColourId, colours::text);
    valueLabel.setJustificationType (juce::Justification::centredRight);
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);
    refreshValueText();
}

void GapBar::refreshValueText()
{
    if (parameter != nullptr)
        valueLabel.setText (parameter->getCurrentValueAsText(), juce::dontSendNotification);
}

void GapBar::resized()
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop (juce::jmax (12, b.getHeight() / 2));
    valueLabel.setBounds (top.removeFromRight (juce::jmin (78, top.getWidth() / 2)));
    nameLabel.setBounds (top);
    nameLabel.setFont (fonts::label (10.5f));
    valueLabel.setFont (fonts::value (11.5f));
    slider.setBounds (b);
}

void GapBar::paint (juce::Graphics&) {}

} // namespace gapfill
