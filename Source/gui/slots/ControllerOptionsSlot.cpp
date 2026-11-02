// F-L24 controller profile and LED device chooser, F-P02 Fill Now timing (docs/dev/12, docs/dev/11).
// Registered in Slot::OptionsExtra (Column). Compact: one profile row and one device / timing row.
//
// The MIDI output device mirrors LED state and only exists in the Standalone build; in a plug-in host the
// device box is disabled and says so.

#include "../LookAndFeel.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"
#include "../../features/i18n/ComponentTranslator.h"

namespace gapfill
{

namespace
{
    class ControllerOptions : public juce::Component
    {
    public:
        explicit ControllerOptions (GapFillAudioProcessor& p) : proc (p)
        {
            setTitle ("Controllers and live options");
            i18n::markUserContent (profileBox);
            i18n::markUserContent (deviceBox);

            profileLabel.setText ("Controller", juce::dontSendNotification);
            deviceLabel.setText ("LED output", juce::dontSendNotification);
            fillLabel.setText ("Fill Now", juce::dontSendNotification);
            programLabel.setText ("Program Change", juce::dontSendNotification);
            for (auto* l : { &profileLabel, &deviceLabel, &fillLabel, &programLabel })
            {
                l->setFont (fonts::label (10.0f));
                l->setColour (juce::Label::textColourId, colours::textMuted);
                addAndMakeVisible (*l);
            }

            profileBox.setTitle ("Controller profile");
            profileBox.setTooltip ("Installs the note and CC bindings of a known controller (replaces the command bindings).");
            profileBox.onChange = [this] { chooseProfile(); };
            addAndMakeVisible (profileBox);

            deviceBox.setTitle ("LED output device");
            deviceBox.setTooltip ("MIDI output that mirrors Panic, hold and scene state on the controller's lights. Standalone only.");
            deviceBox.onChange = [this] { chooseDevice(); };
            addAndMakeVisible (deviceBox);

            fillBox.setTitle ("Fill Now timing");
            fillBox.setTooltip ("When the Fill Now command lands: right away, on the next beat or on the next bar.");
            fillBox.addItem ("Immediately", 1);
            fillBox.addItem ("Next beat", 2);
            fillBox.addItem ("Next bar", 3);
            fillBox.onChange = [this]
            {
                auto o = proc.getLiveControl().getPerfOptions();
                o.fillNowQuant = (live::FillNowQuant) juce::jlimit (0, 2, fillBox.getSelectedId() - 1);
                proc.getLiveControl().setPerfOptions (o);
            };
            addAndMakeVisible (fillBox);

            programBox.setTitle ("Program Change target");
            programBox.setTooltip ("Choose one automatic Program Change action. Explicit command bindings take precedence.");
            programBox.addItem ("Off", 1);
            programBox.addItem ("Scenes", 2);
            programBox.addItem ("Factory presets", 3);
            programChannel.setTitle ("Program Change channel");
            programChannel.setTooltip ("Only accept automatic Program Change on this channel; Any accepts channels 1 through 16.");
            programChannel.addItem ("Any channel", 1);
            for (int ch = 1; ch <= 16; ++ch)
                programChannel.addItem ("Channel " + juce::String (ch), ch + 1);
            auto updateProgramChange = [this]
            {
                if (! loading)
                    proc.getLiveControl().bindings().setProgramChangeTarget (
                        (commands::ProgramChangeTarget) (programBox.getSelectedId() - 1), programChannel.getSelectedId() - 1);
            };
            programBox.onChange = updateProgramChange;
            programChannel.onChange = updateProgramChange;
            addAndMakeVisible (programBox);
            addAndMakeVisible (programChannel);

            status.setFont (fonts::ui (10.0f));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (status);

            load();
            setSize (400, 112);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto a = r.removeFromTop (26);
            profileLabel.setBounds (a.removeFromLeft (70));
            profileBox.setBounds (a.reduced (2, 1));
            auto b = r.removeFromTop (26);
            deviceLabel.setBounds (b.removeFromLeft (70));
            fillBox.setBounds (b.removeFromRight (juce::jmin (110, b.getWidth() / 3)).reduced (2, 1));
            fillLabel.setBounds (b.removeFromRight (50));
            deviceBox.setBounds (b.reduced (2, 1));
            auto c = r.removeFromTop (26);
            programLabel.setBounds (c.removeFromLeft (110));
            programChannel.setBounds (c.removeFromRight (110).reduced (2, 1));
            programBox.setBounds (c.reduced (2, 1));
            status.setBounds (r.removeFromTop (18));
        }

    private:
        void load()
        {
            const juce::ScopedValueSetter<bool> guard (loading, true);
            library = commands::scanLibrary (commands::defaultUserDirectory());

            profileBox.clear (juce::dontSendNotification);
            profileBox.addItem ("(none - keep my bindings)", 1);
            const auto current = juce::String (proc.getLiveControl().bindings().getProfileName());
            int selectedId = 1;
            for (size_t i = 0; i < library.profiles.size(); ++i)
            {
                profileBox.addItem (library.profiles[i].name + (library.profiles[i].builtIn ? "" : "  (user)"), (int) i + 2);
                if (library.profiles[i].name == current)
                    selectedId = (int) i + 2;
            }
            profileBox.setSelectedId (selectedId, juce::dontSendNotification);

            devices = juce::MidiOutput::getAvailableDevices();
            deviceBox.clear (juce::dontSendNotification);
            deviceBox.addItem ("(off)", 1);
            const auto wanted = juce::String (proc.getLiveControl().bindings().getFeedbackDevice());
            int deviceId = 1;
            for (int i = 0; i < devices.size(); ++i)
            {
                deviceBox.addItem (devices[i].name, i + 2);
                if (devices[i].name == wanted)
                    deviceId = i + 2;
            }
            deviceBox.setSelectedId (deviceId, juce::dontSendNotification);
            const bool available = live::LiveControl::feedbackAvailableInThisHost();
            deviceBox.setEnabled (available);
            if (! available)
                status.setText ("LED output is available in the Standalone app only.", juce::dontSendNotification);
            else
                status.setText (proc.getLiveControl().getFeedbackStatus(), juce::dontSendNotification);

            fillBox.setSelectedId ((int) proc.getLiveControl().getPerfOptions().fillNowQuant + 1, juce::dontSendNotification);
            programBox.setSelectedId ((int) proc.getLiveControl().bindings().getProgramChangeTarget() + 1, juce::dontSendNotification);
            programChannel.setSelectedId ((int) proc.getLiveControl().bindings().getTable().pcSceneChannel + 1, juce::dontSendNotification);

            if (library.problems.size() > 0)
                status.setText (library.problems[0], juce::dontSendNotification);
        }

        void chooseProfile()
        {
            const int id = profileBox.getSelectedId();
            if (loading || id < 2 || (size_t) (id - 2) >= library.profiles.size())
                return;
            juce::String message;
            proc.getLiveControl().applyProfile (library.profiles[(size_t) (id - 2)].profile, true, message);
            status.setText (message, juce::dontSendNotification);
        }

        void chooseDevice()
        {
            if (loading)
                return;
            const int id = deviceBox.getSelectedId();
            proc.getLiveControl().setFeedbackDevice (id >= 2 && id - 2 < devices.size() ? devices[id - 2].name : juce::String());
            status.setText (proc.getLiveControl().getFeedbackStatus(), juce::dontSendNotification);
        }

        GapFillAudioProcessor& proc;
        juce::Label profileLabel, deviceLabel, fillLabel, programLabel, status;
        juce::ComboBox profileBox, deviceBox, fillBox, programBox, programChannel;
        commands::LibraryScan library;
        juce::Array<juce::MidiDeviceInfo> devices;
        bool loading = false;
    };

    const SlotRegistrar controllerOptions ({ Slot::OptionsExtra, 300,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<ControllerOptions> (proc);
        } });

    const HelpRegistrar helpControllers (6240, "Controllers", [] (HelpWriter& w)
    {
        w.body ("Options > Controller installs the bindings of a known controller (notes and CCs for Panic, scenes, holds and Fill Now). "
                "Your own profiles can be dropped into Documents/GapFill/Controllers as small JSON files. "
                "LED output mirrors Panic, hold and scene state on the controller's lights; it works in the Standalone app only. "
                "Fill Now timing sets whether the Fill Now command lands at once, on the next beat or on the next bar. "
                "Program Change is off by default. Choose Scenes or Factory presets and a channel in Options; one message has one automatic target, "
                "and an explicit Program Change command binding takes precedence. Factory Program Change works with the editor closed. "
                "Controller profile version 2 adds Skip latest, Skip recipe and direct factory-preset commands; version 1 profiles keep their original command set.");
    });
}

} // namespace gapfill
