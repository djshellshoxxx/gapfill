#include "lock.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addLockParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // Same order as GAPFILL_PARAMS_LOCK. Off: the scheduler chooses a fresh algorithm every gap.
    layout.add (std::make_unique<APB> (makeID ("fillLock", 2), "Fill Lock", false));
}

void addLockPerformanceIds (juce::StringArray& ids)
{
    // Fill Lock is live performance state: Random, Surprise and Undo must never flip it.
    ids.add ("fillLock");
}

} // namespace gapfill
