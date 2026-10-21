#include "bandtype.h"
#include "ParamBuilders.h"
#include "../Parameters.h"

namespace gapfill
{

void addBandtypeParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // C-9: Auto (0) = today's algorithm choice; otherwise the lane is forced to Algo (index - 1).
    juce::StringArray types { "Auto" };

    for (int i = 0; i < numAlgos; ++i)
        types.add (algoName ((Algo) i));

    layout.add (std::make_unique<APC> (makeID ("bandLowType", 2),  "Low Band Fill Type",  types, 0));
    layout.add (std::make_unique<APC> (makeID ("bandMidType", 2),  "Mid Band Fill Type",  types, 0));
    layout.add (std::make_unique<APC> (makeID ("bandHighType", 2), "High Band Fill Type", types, 0));
}

void addBandtypePerformanceIds (juce::StringArray& ids)
{
    for (const char* id : { "bandLowType", "bandMidType", "bandHighType" })
        ids.add (id);
}

} // namespace gapfill
