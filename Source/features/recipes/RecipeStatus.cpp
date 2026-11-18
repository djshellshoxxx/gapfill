#include "RecipeStatus.h"
#include "RecipeServices.h"
#include "../audition/PreviewChannel.h"
#include "../history/Describe.h"
#include "../../PluginProcessor.h"
#include "../i18n/Strings.h"
#include "../i18n/RecipePresentation.h"

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
    auto* lockParam = state.getRawParameterValue ("fl01_lock");

    if (lockParam == nullptr || lockParam->load() < 0.5f)
        return {};

    auto* fillType = state.getRawParameterValue ("fillType");

    if (fillType != nullptr && fillType->load() > 0.5f)
    {
        isWarning = true;
        return i18n::text ("Fill Type overrides Lock");
    }

    const auto& mirror = proc.getRecipes().lockMirror;
    if (mirror.valid && ! proc.getRecipes().lockArmed.load (std::memory_order_relaxed))
        return i18n::format ("Locked: {0}", { i18n::describeRecipe (mirror.recipe) });

    return i18n::text ("Lock armed: the next fill will be locked");
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
        line.text = trig.getLearn() == audition::MidiTrigger::Learn::Note
            ? i18n::format ("MIDI LEARN  |  Fill Audition  |  play a note  |  {0}s", { juce::String (seconds) })
            : i18n::format ("MIDI LEARN  |  Fill Audition  |  move a controller  |  {0}s", { juce::String (seconds) });
    }
    else if (message.isNotEmpty())
    {
        line.text = i18n::errorText (message);
        line.warning = true;
    }
    else if (previewStatusOf (code) == PreviewStatus::Playing)
    {
        const auto algo = previewAlgoOf (code);
        line.text = algo >= 0 && algo < numAlgos ? i18n::format ("Preview: {0}", { i18n::text (algoName ((Algo) algo)) })
                                                 : i18n::text ("Preview");
    }
    else if (services.match.isEnabled() && abcompare::trimText (services.match.getTrimDb()).isNotEmpty())
    {
        line.text = i18n::matchTrim (services.match.getTrimDb());
    }
    else
    {
        line.text = lockSummary (proc, line.warning);

        if (line.text.isEmpty() && services.likeRemaining.load (std::memory_order_relaxed) > 0)
            line.text = i18n::format ("LIKED x{0}", { juce::String (services.likeRemaining.load (std::memory_order_relaxed)) });
    }

    return line;
}

} // namespace gapfill::recipes
