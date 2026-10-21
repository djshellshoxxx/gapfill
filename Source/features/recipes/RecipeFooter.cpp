// The WP4 footer status line (Slot::FooterStatus). One line, highest priority wins
// (docs/dev/11: "show only the highest-priority live message"):
//
//   1. MIDI learn countdown for the audition trigger
//   2. a transient message (rejections for 2 s, "Start playback to audition", load/save results)
//   3. "Preview: <Algorithm>" while an audition plays
//   4. "Locked: <recipe>" / "Lock armed" / "Fill Type overrides Lock"
//   5. "LIKED x3"
//
// Message thread only; reads atomics and the processor's message-thread mirrors.

#include "RecipeServices.h"
#include "../audition/PreviewChannel.h"
#include "../history/Describe.h"
#include "../../gui/LookAndFeel.h"
#include "../../gui/SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    class RecipeFooter : public juce::Component,
                         private juce::Timer
    {
    public:
        explicit RecipeFooter (GapFillAudioProcessor& p)
            : proc (p)
        {
            setInterceptsMouseClicks (false, false);
            setTitle ("Recipe status");
            lastSeq = previewSeqOf ((std::uint32_t) proc.getMeterState().previewStatus.load (std::memory_order_relaxed));
            startTimerHz (15);
        }

        ~RecipeFooter() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            if (text.isEmpty())
                return;

            g.setColour (warn ? colours::warning : colours::accentGreen);
            g.setFont (fonts::mono (9.5f));
            g.drawText (text, getLocalBounds(), juce::Justification::centredLeft, true);
        }

    private:
        void timerCallback() override
        {
            auto& services = proc.getRecipes();
            const auto code = (std::uint32_t) proc.getMeterState().previewStatus.load (std::memory_order_relaxed);

            // A new outcome (the sequence number moved) that is a rejection shows for 2 s.
            if (previewSeqOf (code) != lastSeq)
            {
                lastSeq = previewSeqOf (code);

                if (auto* why = previewRejectionText (previewStatusOf (code)))
                    services.postMessage (why);
            }

            juce::String next;
            bool isWarning = false;
            auto& trig = services.trigger;
            const auto message = services.getMessage();

            if (trig.getLearn() != audition::MidiTrigger::Learn::None)
            {
                const auto seconds = (trig.getLearnTicksRemaining() + audition::MidiTrigger::timerHz - 1)
                                       / audition::MidiTrigger::timerHz;
                next = juce::String ("MIDI LEARN  |  Fill Audition  |  ")
                     + (trig.getLearn() == audition::MidiTrigger::Learn::Note ? "play a note" : "move a controller")
                     + "  |  " + juce::String (seconds) + "s";
            }
            else if (message.isNotEmpty())
            {
                next = message;
                isWarning = true;
            }
            else if (previewStatusOf (code) == PreviewStatus::Playing)
            {
                const auto algo = previewAlgoOf (code);
                next = algo >= 0 && algo < numAlgos ? "Preview: " + juce::String (algoName ((Algo) algo))
                                                    : juce::String ("Preview");
            }
            else
            {
                next = lockSummary(isWarning);

                if (next.isEmpty() && services.likeRemaining.load (std::memory_order_relaxed) > 0)
                    next = "LIKED x" + juce::String (services.likeRemaining.load (std::memory_order_relaxed));
            }

            if (next != text || isWarning != warn)
            {
                text = next;
                warn = isWarning;
                repaint();
            }
        }

        juce::String lockSummary (bool& isWarning) const
        {
            auto& state = proc.getValueTreeState();
            auto* lockParam = state.getRawParameterValue ("fillLock");

            if (lockParam == nullptr || lockParam->load() < 0.5f)
                return {};

            auto* fillType = state.getRawParameterValue ("fillType");

            if (fillType != nullptr && fillType->load() > 0.5f)
            {
                isWarning = true;
                return "Fill Type overrides Lock";
            }

            const auto& mirror = proc.getRecipes().lockMirror;
            const auto& t = history::strings();

            if (mirror.valid && ! proc.getRecipes().lockArmed.load (std::memory_order_relaxed))
                return juce::String (t.locked) + ": " + history::describeRecipe (mirror.recipe, 0.0);

            return juce::String (t.armed) + ": the next fill will be locked";
        }

        GapFillAudioProcessor& proc;
        juce::String text;
        bool warn = false;
        std::uint32_t lastSeq = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecipeFooter)
    };

    const SlotRegistrar recipeFooter ({ Slot::FooterStatus, 100,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<RecipeFooter> (proc);
        } });
}

} // namespace gapfill
