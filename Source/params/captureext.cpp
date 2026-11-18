#include "captureext.h"
#include "ParamBuilders.h"

namespace gapfill
{
void addCaptureextParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    layout.add (std::make_unique<APB> (makeID ("fm08_detail", 2), "Detailed recapture", false));
    layout.add (percent ("fm08_scan", "Capture Scan", 0.0f, 2));
}

void addCaptureextPerformanceIds (juce::StringArray& ids)
{
    ids.add ("fm08_detail");
    ids.add ("fm08_scan");
}
} // namespace gapfill
