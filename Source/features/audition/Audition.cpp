#include "Audition.h"
#include "../../dsp/FillEngine.h"

#include <juce_core/juce_core.h>

namespace gapfill::audition
{

bool handleCommand (FillEngine& engine, const Command& command, std::uint32_t nowMs) noexcept
{
    switch (command.type)
    {
        case CommandType::Preview:
            if (! isStalePreviewCommand (command.issuedMs, nowMs))
                engine.requestPreview (command.velocity != 0 ? command.velocity : (std::uint8_t) 100);
            return true;

        case CommandType::PreviewRecipe:
            if (! isStalePreviewCommand (command.issuedMs, nowMs))
                engine.requestPreviewRecipe (command.recipe, command.velocity != 0 ? command.velocity : (std::uint8_t) 100);
            return true;

        default:
            return false;
    }
}

bool handleCommand (FillEngine& engine, const Command& command) noexcept
{
    return handleCommand (engine, command, juce::Time::getMillisecondCounter());
}

} // namespace gapfill::audition
