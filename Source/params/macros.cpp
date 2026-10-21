#include "macros.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addMacrosParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // Same order as GAPFILL_PARAMS_MACROS. Defaults are the creative spec's 50/25/25/15;
    // macrosOn defaults to Off (revision R1) so older sessions sound the same.
    layout.add (percent ("macroActivity",    "Macro Activity",    50.0f));
    layout.add (percent ("macroChaos",       "Macro Chaos",       25.0f));
    layout.add (percent ("macroSpace",       "Macro Space",       25.0f));
    layout.add (percent ("macroDestruction", "Macro Destruction", 15.0f));
    layout.add (std::make_unique<APB> (makeID ("macrosOn", 2), "Macros On", false));
}

void addMacrosPerformanceIds (juce::StringArray& ids)
{
    ids.add ("macrosOn");       // never randomised (docs/dev/02 R1)
}

} // namespace gapfill
