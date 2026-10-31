#include "Strings.h"
#include "ComponentTranslator.h"
#include "../../gui/LookAndFeel.h"
#include "../../gui/SlotHost.h"
#include "../../gui/HelpRegistry.h"

namespace gapfill::i18n
{
namespace
{
    class LanguageOptions final : public juce::Component, private juce::ChangeListener
    {
    public:
        LanguageOptions() : languages (availableLanguages())
        {
            setTitle ("Language");
            label.setFont (fonts::ui (12.5f));
            hint.setFont (fonts::ui (11.0f));
            hint.setMinimumHorizontalScale (1.0f);
            label.setColour (juce::Label::textColourId, colours::text);
            hint.setColour (juce::Label::textColourId, colours::textMuted);
            combo.setTitle ("Language");
            combo.setDescription ("Choose the language for this interface.");
            markUserContent (combo);             // native locale names are intentional identities
            for (size_t i = 0; i < languages.size(); ++i)
                combo.addItem (languages[i].name, (int) i + 1);
            combo.onChange = [this]
            {
                const auto i = combo.getSelectedId() - 1;
                if (juce::isPositiveAndBelow (i, (int) languages.size()))
                    setLanguage (languages[(size_t) i].code, true);
            };
            addAndMakeVisible (label);
            addAndMakeVisible (combo);
            addAndMakeVisible (hint);
            languageChanges().addChangeListener (this);
            refresh();
            setSize (400, 88);
        }
        ~LanguageOptions() override { languageChanges().removeChangeListener (this); }
        void resized() override
        {
            auto bounds = getLocalBounds();
            auto row = bounds.removeFromTop (28);
            label.setBounds (row.removeFromLeft (150));
            combo.setBounds (row.reduced (2, 1));
            hint.setBounds (bounds.reduced (0, 2));
        }
    private:
        void refresh()
        {
            label.setText (text ("Language"), juce::dontSendNotification);
            hint.setText (text ("Choose the language for this interface. Saved projects and audio stay the same."), juce::dontSendNotification);
            for (size_t i = 0; i < languages.size(); ++i)
                if (languages[i].code == currentLanguage())
                    combo.setSelectedId ((int) i + 1, juce::dontSendNotification);
        }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        std::vector<Language> languages;
        juce::Label label, hint;
        juce::ComboBox combo;
    };

    const SlotRegistrar languageOptions ({ Slot::OptionsExtra, 850,
        [] (GapFillAudioProcessor&) -> std::unique_ptr<juce::Component> { return std::make_unique<LanguageOptions>(); } });
    const HelpRegistrar languageHelp (8330, "Language", [] (HelpWriter& w)
    {
        w.body ("Options > Language changes interface labels, menus and help. "
                "English is the default; language is never detected automatically. "
                "The choice is saved for this user and does not change audio or saved projects. "
                "Pseudo (+40%) is a layout-testing language. Missing translations use English.");
    });
}
}
