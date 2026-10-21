#include "capture2.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addCapture2Parameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // C-10: how much audio before the freeze point a latched Capture may read (default 1 s = the 0.0.1 behaviour).
    layout.add (millis ("captureLength", "Capture Length", 250.0f, 2000.0f, 1000.0f, 800.0f, 2));
}

void addCapture2PerformanceIds (juce::StringArray& ids)
{
    ids.add ("captureLength");
}

} // namespace gapfill
