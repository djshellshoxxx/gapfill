#include "PitchSyncGui.h"
#include "../../PluginProcessor.h"
#include "../../gui/Knob.h"
#include "../../gui/SlotHost.h"
#include "../info/ParamInfo.h"
#include <cmath>
#include "features/i18n/Strings.h"
#include "features/i18n/ComponentTranslator.h"

namespace gapfill::harmonic
{
namespace
{
    class PitchSyncPanel final : public juce::Component, private juce::Timer
    {
    public:
        explicit PitchSyncPanel (GapFillAudioProcessor& processor)
            : proc (processor),
              mode (proc.getValueTreeState(), "fx01_mode", "Mode", "Opt into pitch-synchronous Granular; Off preserves the original Harmonic controls."),
              intervals (proc.getValueTreeState(), "fx01_intervals", "Intervals", "Choose the harmony intervals; Scale uses the current known key."),
              voices (proc.getValueTreeState(), "fx01_voices", "Voices", "One to four pitch-synchronous harmony layers.", colours::secondary),
              motion (proc.getValueTreeState(), "fx01_motion", "Motion", "Slow source movement and pitch variation.", colours::secondary),
              scatter (proc.getValueTreeState(), "fx01_scatter", "Scatter", "Seeded variation of source epochs and onset timing.", colours::secondary),
              tone (proc.getValueTreeState(), "fx01_tone", "Tone", "Darker to brighter pitch-synchronous layers.", colours::secondary),
              snap (proc.getValueTreeState(), "fx01_snap", "Key Snap", "Move interval pitches toward notes of the current known key.", colours::secondary)
        {
            setTitle ("Pitch Sync controls");
            setDescription ("Values are sampled at fill start. Unpitched or irregular sources use plain Granular.");
            setFocusContainerType (FocusContainerType::keyboardFocusContainer);
            for (auto* control : std::initializer_list<juce::Component*> { &mode, &intervals, &voices, &motion, &scatter, &tone, &snap, &status })
                addAndMakeVisible (*control);
            int order = 1;
            for (auto* control : std::initializer_list<juce::Component*> { &mode.getComboBox(), &intervals.getComboBox(),
                    &voices.getSlider(), &motion.getSlider(), &scatter.getSlider(), &tone.getSlider(), &snap.getSlider() })
            {
                control->setWantsKeyboardFocus (true);
                control->setExplicitFocusOrder (order++);
            }
            status.setFont (fonts::label (11));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            status.setJustificationType (juce::Justification::topLeft);
            setSize (448, 224);
            mode.getComboBox().onChange = [this] { refresh(); };
            refresh();
            translator = std::make_unique<i18n::ComponentTranslator> (*this);
            startTimerHz (4);
        }
        ~PitchSyncPanel() override { stopTimer(); }
        void resized() override
        {
            auto area = getLocalBounds().reduced (8);
            if (! bodyVisible)
            {
                mode.setBounds (area.removeFromTop (48).removeFromLeft (144));
                area.removeFromTop (8);
                status.setBounds (area);
                return;
            }
            auto top = area.removeFromTop (80);
            mode.setBounds (top.removeFromLeft (144).withHeight (48));
            top.removeFromLeft (8);
            intervals.setBounds (top.removeFromLeft (160).withHeight (48));
            top.removeFromLeft (8);
            voices.setBounds (top);
            area.removeFromTop (8);
            auto bottom = area.removeFromTop (80);
            const int width = bottom.getWidth() / 4;
            for (auto* control : { &motion, &scatter, &tone, &snap }) control->setBounds (bottom.removeFromLeft (width));
            area.removeFromTop (8);
            status.setBounds (area);
        }
    private:
        void timerCallback() override { refresh(); }
        void refresh()
        {
            const auto* selected = proc.getValueTreeState().getRawParameterValue ("fx01_mode");
            const bool enabled = selected != nullptr && selected->load (std::memory_order_relaxed) >= 0.5f;
            if (enabled != bodyVisible)
            {
                bodyVisible = enabled;
                for (auto* control : std::initializer_list<juce::Component*> { &intervals, &voices, &motion, &scatter, &tone, &snap })
                    control->setVisible (enabled);
                setSize (448, enabled ? 224 : 104);
            }
            const auto raw = [this] (const char* id)
            {
                const auto* value = proc.getValueTreeState().getRawParameterValue (id);
                return value != nullptr ? (int) std::lround (value->load (std::memory_order_relaxed)) : 0;
            };
            const auto& key = proc.getContextState().autoKey();
            const bool knownKey = keydetect::resolveKey (raw ("scaleRoot"), raw ("scaleType"), raw ("scaleFollow") != 0,
                raw ("fx03_hold") != 0, key.heldKey(), key.estimate()).rootPc >= 0;
            snap.setEnabled (knownKey);
            snap.getSlider().setTooltip (knownKey ? i18n::text (info::tooltipFor ("fx01_snap", {})) : i18n::text ("Set a key in Key settings"));
            juce::String message;
            if (! enabled)
                message = i18n::text ("Pitch Sync is off; the original Harmonic controls apply.");
            else
            {
                const auto& meters = proc.getFxMeters();
                const int state = meters.hgState.load (std::memory_order_relaxed);
                const float hz = meters.hgF0.load (std::memory_order_relaxed);
                if (state == 1 && std::isfinite (hz) && hz >= 50 && hz <= 1000)
                    message = i18n::format ("Source: {0} ({1} Hz), clarity {2}%", {
                        juce::MidiMessage::getMidiNoteName ((int) std::lround (69 + 12 * std::log2 (hz / 440)), true, true, 4),
                        juce::String (hz, 1),
                        juce::String (100 * meters.hgConfidence.load (std::memory_order_relaxed), 0) });
                else if (state == 2)
                    message = i18n::text ("Source not pitched: plain Granular");
                else if (state == 3)
                    message = i18n::text ("Irregular pitch marks: plain Granular");
                else message = i18n::text ("Pitch Sync: waiting for a pitched source");
            }
            if (status.getText() != message)
            {
                status.setText (message, juce::dontSendNotification);
                status.setTitle (message);
            }
        }
        GapFillAudioProcessor& proc;
        bool bodyVisible = true;
        GapCombo mode, intervals;
        GapKnob voices, motion, scatter, tone, snap;
        juce::Label status;
        std::unique_ptr<i18n::ComponentTranslator> translator;
    };

    class PitchSyncButton final : public juce::TextButton
    {
    public:
        explicit PitchSyncButton (GapFillAudioProcessor& proc) : juce::TextButton ("PITCH SYNC")
        {
            setTitle ("Pitch Sync");
            setDescription ("Open the optional pitch-synchronous harmony controls.");
            setTooltip ("Pitch-synchronous Granular controls; Off preserves the original Harmonic controls.");
            setWantsKeyboardFocus (true);
            onClick = [this, owner = &proc]
            {
                if (auto* parent = getTopLevelComponent())
                    juce::CallOutBox::launchAsynchronously (makePitchSyncPanel (*owner),
                        parent->getLocalArea (this, getLocalBounds()), parent);
            };
        }
    };
    class PitchSyncLauncher final : public juce::Component, private juce::Timer
    {
    public:
        explicit PitchSyncLauncher (GapFillAudioProcessor& processor) : proc (processor), button (processor)
        {
            addAndMakeVisible (button);
            refresh();
            startTimerHz (4);
        }
        ~PitchSyncLauncher() override { stopTimer(); }
        void resized() override { button.setBounds (getLocalBounds()); }
    private:
        void refresh() { button.setVisible (proc.getUiState().getViewMode() == ui::ViewMode::Advanced); }
        void timerCallback() override { refresh(); }
        GapFillAudioProcessor& proc;
        PitchSyncButton button;
    };
    const SlotRegistrar pitchSyncButton ({ Slot::CreativeExtra, 145,
        [] (GapFillAudioProcessor& proc) { return std::make_unique<PitchSyncLauncher> (proc); } });
}
std::unique_ptr<juce::Component> makePitchSyncPanel (GapFillAudioProcessor& processor)
{
    return std::make_unique<PitchSyncPanel> (processor);
}
}
