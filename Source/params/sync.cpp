#include "sync.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addSyncParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // E-2: fill-length grid (Off = today's behaviour) and tempo-locked modifiers / slices.
    layout.add (std::make_unique<APC> (makeID ("fillQuant", 2), "Quantise Length",
                                       juce::StringArray { "Off", "1/4", "1/8", "1/16", "1/32" }, 0));
    layout.add (std::make_unique<APB> (makeID ("modSync", 2), "Sync Mods", false));
}

void addSyncPerformanceIds (juce::StringArray& ids)
{
    ids.add ("fillQuant");
    ids.add ("modSync");
}

} // namespace gapfill
