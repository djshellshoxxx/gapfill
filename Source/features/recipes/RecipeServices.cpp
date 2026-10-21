#include "RecipeServices.h"
#include "../../dsp/FillEngine.h"

namespace gapfill::recipes
{

void RecipeServices::onMidi (FillEngine& engine, const juce::MidiBuffer& midi) noexcept
{
    std::uint8_t velocity = 100;

    if (trigger.scan (midi, velocity))
        engine.requestPreview (velocity);          // collapses with a pending UI request
}

void RecipeServices::prepare (double sampleRate)
{
    exportService.prepare (sampleRate);
}

void RecipeServices::afterWet (const juce::AudioBuffer<float>& wet, int startSample, int numSamples,
                               std::int64_t outputPos) noexcept
{
    if (numSamples > 0)
        exportService.process (wet, startSample, numSamples, outputPos);
}

void RecipeServices::publish (const FillEngine& engine, double bpm) noexcept
{
    exportService.bpm.store ((float) bpm, std::memory_order_relaxed);
    likeRemaining.store (engine.getLikeState().remaining, std::memory_order_relaxed);
    lockArmed.store (engine.getLockState().engaged && engine.getLockState().armed, std::memory_order_relaxed);
}

bool RecipeServices::tick() noexcept
{
    return trigger.advanceLearnTimeout();
}

bool RecipeServices::onFillEvent (const FillEvent& e) noexcept
{
    switch (e.type)
    {
        case FillEventType::LockSet:
            lockMirror = { true, e.recipe };
            return true;

        case FillEventType::LockCleared:
            lockMirror = {};
            return true;

        case FillEventType::PreviewStarted:
            lastPreviewSpan = { e.pos, e.lengthSamples };
            lastPreviewRecipe = e.recipe;
            return true;                      // consumed: never listed in history or the outcome log

        default:
            return false;
    }
}

void RecipeServices::postMessage (const juce::String& text, std::uint32_t durationMs)
{
    message = text;
    messageUntilMs = juce::Time::getMillisecondCounter() + durationMs;
}

juce::String RecipeServices::getMessage() const
{
    return (std::int32_t) (juce::Time::getMillisecondCounter() - messageUntilMs) < 0 ? message : juce::String();
}

} // namespace gapfill::recipes
