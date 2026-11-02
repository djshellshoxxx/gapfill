#include "Gui.h"
#include "../../PluginProcessor.h"
#include "../../gui/Knob.h"
#include "../../gui/SlotHost.h"
#include "features/i18n/ComponentTranslator.h"
#include "features/i18n/Strings.h"

#include <cmath>

namespace gapfill::tapedamage
{
namespace
{
    class TapeDamagePanel final : public juce::Component, private juce::Timer
    {
    public:
        explicit TapeDamagePanel (juce::AudioProcessorValueTreeState& parameters)
            : state (parameters),
              chance (state, "fm20_amount", "Chance", "Probability per fill; zero leaves Tape Damage off.", colours::secondary),
              wow (state, "fm20_wow", "Wow", "Slow pitch motion; 100 percent requests 40 cents.", colours::secondary),
              flutter (state, "fm20_flutter", "Flutter", "Faster pitch motion; 100 percent requests 15 cents.", colours::secondary),
              instability (state, "fm20_instab", "Instability", "Bounded speed drift; 100 percent stays within half a percent.", colours::secondary),
              saturation (state, "fm20_sat", "Saturation", "Level-compensated tape saturation.", colours::secondary),
              dropRate (state, "fm20_dropDensity", "Drop rate", "Dropout rate; 100 percent is ten events per second.", colours::secondary),
              dropDepth (state, "fm20_dropDepth", "Depth", "Level lost inside each dropout; 100 percent reaches silence.", colours::secondary),
              splice (state, "fm20_splice", "Splice", "Read-tap splices; 100 percent is four events per second.", colours::secondary),
              mix (state, "fm20_mix", "Mix", "Independent wet blend, sampled at fill start.", colours::secondary)
        {
            setTitle ("Tape Damage controls");
            setDescription ("Motion, damage and mix for new fills. Active Tape Damage starts the fill four milliseconds later inside its original deadline.");
            setFocusContainerType (FocusContainerType::keyboardFocusContainer);
            title.setText ("Tape Damage", juce::dontSendNotification);
            title.setFont (fonts::label (13));
            enabled.setButtonText ("Enabled");
            enabled.setTitle ("Tape Damage enable");
            enabled.setTooltip ("Switch Tape Damage off or restore its previous chance; the first enable uses 50 percent.");
            enabled.setClickingTogglesState (false);
            enabled.onClick = [this]
            {
                const auto current = value ("fm20_amount");
                if (current > 0) rememberedChance = current;
                if (auto* parameter = state.getParameter ("fm20_amount"))
                {
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (current > 0 ? 0.0f : rememberedChance));
                    parameter->endChangeGesture();
                }
                refresh();
            };
            fold.setTitle ("Tape Damage show or hide controls");
            fold.onClick = [this] { folded = ! folded; updateFold(); };
            for (auto* label : { &motionTitle, &damageTitle, &mixTitle })
            {
                label->setFont (fonts::label (11));
                label->setColour (juce::Label::textColourId, colours::textMuted);
                body.addAndMakeVisible (*label);
            }
            motionTitle.setText ("Motion", juce::dontSendNotification);
            damageTitle.setText ("Damage", juce::dontSendNotification);
            mixTitle.setText ("Mix and chance", juce::dontSendNotification);
            for (auto* knob : { &wow, &flutter, &instability, &saturation, &dropRate, &dropDepth, &splice, &chance, &mix })
                body.addAndMakeVisible (*knob);
            int focus = 1;
            for (auto* control : std::initializer_list<juce::Component*> { &enabled, &fold, &wow.getSlider(), &flutter.getSlider(),
                    &instability.getSlider(), &saturation.getSlider(), &dropRate.getSlider(), &dropDepth.getSlider(),
                    &splice.getSlider(), &chance.getSlider(), &mix.getSlider() })
            {
                control->setWantsKeyboardFocus (true);
                control->setExplicitFocusOrder (focus++);
            }
            const char* names[] { "Tape Damage wow", "Tape Damage flutter", "Tape Damage instability", "Tape Damage saturation",
                                  "Tape Damage dropout rate", "Tape Damage dropout depth", "Tape Damage splice", "Tape Damage chance", "Tape Damage mix" };
            int index = 0;
            for (auto* knob : { &wow, &flutter, &instability, &saturation, &dropRate, &dropDepth, &splice, &chance, &mix })
                knob->getSlider().setTitle (names[index++]);
            summary.setFont (fonts::label (11));
            summary.setColour (juce::Label::textColourId, colours::textMuted);
            summary.setJustificationType (juce::Justification::topLeft);
            summary.setMinimumHorizontalScale (1.0f);
            for (auto* child : std::initializer_list<juce::Component*> { &title, &enabled, &fold, &summary, &body })
                addAndMakeVisible (*child);
            setSize (528, 416);
            updateFold();
            refresh();
            translator = std::make_unique<i18n::ComponentTranslator> (*this);
            startTimerHz (4);
        }
        ~TapeDamagePanel() override { stopTimer(); }

        void resized() override
        {
            auto area = getLocalBounds().reduced (8);
            auto header = area.removeFromTop (28);
            fold.setBounds (header.removeFromRight (152));
            header.removeFromRight (8);
            enabled.setBounds (header.removeFromRight (104));
            title.setBounds (header);
            area.removeFromTop (8);
            summary.setBounds (area.removeFromTop (56));
            area.removeFromTop (8);
            body.setBounds (area);
            auto content = body.getLocalBounds();
            const auto place = [&content] (juce::Label& label, std::initializer_list<GapKnob*> knobs)
            {
                label.setBounds (content.removeFromTop (16));
                auto row = content.removeFromTop (80);
                const int width = row.getWidth() / (int) knobs.size();
                for (auto* knob : knobs) knob->setBounds (row.removeFromLeft (width));
                content.removeFromTop (4);
            };
            place (motionTitle, { &wow, &flutter, &instability });
            place (damageTitle, { &saturation, &dropRate, &dropDepth, &splice });
            place (mixTitle, { &chance, &mix });
        }

    private:
        float value (const char* id) const noexcept
        {
            const auto* raw = state.getRawParameterValue (id);
            const float real = raw != nullptr ? raw->load (std::memory_order_relaxed) : 0;
            return std::isfinite (real) ? juce::jlimit (0.0f, 100.0f, real) : 0;
        }
        void updateFold()
        {
            body.setVisible (! folded);
            fold.setButtonText (i18n::text (folded ? "Show controls" : "Hide controls"));
            setSize (getWidth(), folded ? 112 : 416);
        }
        void timerCallback() override { refresh(); }
        void refresh()
        {
            const auto amount = value ("fm20_amount");
            enabled.setToggleState (amount > 0, juce::dontSendNotification);
            const auto text = amount <= 0 ? i18n::text ("Tape Damage is off. Chance enables it for future fills.")
                : i18n::format ("Chance {0}%, mix {1}%. Wow {2} cents; flutter {3} cents. Active fills start 4 ms later.", {
                    juce::String (amount, 0), juce::String (value ("fm20_mix"), 0),
                    juce::String (value ("fm20_wow") * 0.4f, 1), juce::String (value ("fm20_flutter") * 0.15f, 1) });
            if (summary.getText() != text)
            {
                summary.setText (text, juce::dontSendNotification);
                summary.setTitle (text);
            }
            fold.setButtonText (i18n::text (folded ? "Show controls" : "Hide controls"));
        }

        juce::AudioProcessorValueTreeState& state;
        GapKnob chance, wow, flutter, instability, saturation, dropRate, dropDepth, splice, mix;
        juce::Label title, motionTitle, damageTitle, mixTitle, summary;
        juce::TextButton enabled, fold;
        juce::Component body;
        bool folded = false;
        float rememberedChance = 50.0f;
        std::unique_ptr<i18n::ComponentTranslator> translator;
    };

    class TapeDamageLauncher final : public juce::Component, private juce::Timer
    {
    public:
        explicit TapeDamageLauncher (GapFillAudioProcessor& processor) : proc (processor)
        {
            button.setButtonText ("TAPE DAMAGE");
            button.setTitle ("Tape Damage");
            button.setDescription ("Open the optional Tape Damage motion, damage and mix controls.");
            button.setTooltip ("Tape Damage controls for future fills. Chance zero is off.");
            button.setWantsKeyboardFocus (true);
            button.onClick = [this]
            {
                if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
                    juce::CallOutBox::launchAsynchronously (makeTapeDamagePanel (proc.getValueTreeState()),
                        editor->getLocalArea (&button, button.getLocalBounds()), editor);
            };
            addAndMakeVisible (button);
            refresh();
            startTimerHz (4);
        }
        ~TapeDamageLauncher() override { stopTimer(); }
        void resized() override { button.setBounds (getLocalBounds()); }
    private:
        void timerCallback() override { refresh(); }
        void refresh() { button.setVisible (proc.getUiState().getViewMode() == ui::ViewMode::Advanced); }
        GapFillAudioProcessor& proc;
        juce::TextButton button;
    };
    const SlotRegistrar tapeLauncher ({ Slot::CreativeExtra, 150,
        [] (GapFillAudioProcessor& proc) { return std::make_unique<TapeDamageLauncher> (proc); } });
}
std::unique_ptr<juce::Component> makeTapeDamagePanel (juce::AudioProcessorValueTreeState& state)
{
    return std::make_unique<TapeDamagePanel> (state);
}
}
