#include "RecipeStatus.h"
#include "RecipeServices.h"
#include "../audition/PreviewChannel.h"
#include "../history/Describe.h"
#include "../../PluginProcessor.h"

namespace gapfill::recipes
{

RecipeStatus::RecipeStatus (GapFillAudioProcessor& p)
    : proc (p)
{
    lastSeq = previewSeqOf ((std::uint32_t) proc.getMeterState().previewStatus.load (std::memory_order_relaxed));
}

juce::String RecipeStatus::lockSummary (GapFillAudioProcessor& proc, bool& isWarning)
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

RecipeStatus::Line RecipeStatus::update()
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

    Line line;
    auto& trig = services.trigger;
    const auto message = services.getMessage();

    if (trig.getLearn() != audition::MidiTrigger::Learn::None)
    {
        const auto seconds = (trig.getLearnTicksRemaining() + audition::MidiTrigger::timerHz - 1)
                               / audition::MidiTrigger::timerHz;
        line.text = juce::String ("MIDI LEARN  |  Fill Audition  |  ")
                  + (trig.getLearn() == audition::MidiTrigger::Learn::Note ? "play a note" : "move a controller")
                  + "  |  " + juce::String (seconds) + "s";
    }
    else if (message.isNotEmpty())
    {
        line.text = message;
        line.warning = true;
    }
    else if (previewStatusOf (code) == PreviewStatus::Playing)
    {
        const auto algo = previewAlgoOf (code);
        line.text = algo >= 0 && algo < numAlgos ? "Preview: " + juce::String (algoName ((Algo) algo))
                                                 : juce::String ("Preview");
    }
    else if (services.match.isEnabled() && abcompare::trimText (services.match.getTrimDb()).isNotEmpty())
    {
        line.text = abcompare::trimText (services.match.getTrimDb());
    }
    else
    {
        line.text = lockSummary (proc, line.warning);

        if (line.text.isEmpty() && services.likeRemaining.load (std::memory_order_relaxed) > 0)
            line.text = "LIKED x" + juce::String (services.likeRemaining.load (std::memory_order_relaxed));
    }

    return line;
}

} // namespace gapfill::recipes
