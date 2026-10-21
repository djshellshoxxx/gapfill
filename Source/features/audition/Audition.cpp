#include "Audition.h"
#include "../../dsp/FillEngine.h"

#include <juce_core/juce_core.h>

namespace gapfill::audition
{

bool handleCommand (FillEngine& engine, const Command& command, std::uint32_t nowMs) noexcept
{
    if (command.type != CommandType::Preview && command.type != CommandType::PreviewRecipe)
        return false;

    if (isStalePreviewCommand (command.issuedMs, nowMs))
        return true;                                  // handled: too old to honour

    const auto velocity = command.velocity != 0 ? command.velocity : (std::uint8_t) 100;

    if (command.type == CommandType::Preview)
        engine.requestPreview (velocity);
    else
        engine.requestPreviewRecipe (command.recipe, velocity);

    return true;
}

bool handleCommand (FillEngine& engine, const Command& command) noexcept
{
    return handleCommand (engine, command, juce::Time::getMillisecondCounter());
}

} // namespace gapfill::audition
