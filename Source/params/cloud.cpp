#include "cloud.h"
#include "ParamBuilders.h"

namespace gapfill
{

// N-FX2 Granular Cloud (docs/dev/12 section 4). Neutral = cloudAmount 0.
void addCloudParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    auto linear = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi, 0.01f); };

    layout.add (std::make_unique<APF> (makeID ("cloudAmount", 2), "Cloud Amount", linear (0.0f, 100.0f), 0.0f,
                                       Attr().withLabel ("%")));

    auto density = linear (2.0f, 80.0f);
    density.setSkewForCentre (12.6f);   // logarithmic feel (geometric mean of the range)
    layout.add (std::make_unique<APF> (makeID ("cloudDensity", 2), "Cloud Density", density, 20.0f,
                                       Attr().withLabel ("grains/s")));

    layout.add (std::make_unique<APF> (makeID ("cloudSize", 2), "Cloud Grain Size", linear (10.0f, 250.0f), 60.0f,
                                       Attr().withLabel ("ms")));
    layout.add (std::make_unique<APF> (makeID ("cloudSpan", 2), "Cloud Span", linear (100.0f, 2000.0f), 600.0f,
                                       Attr().withLabel ("ms")));
    layout.add (std::make_unique<APF> (makeID ("cloudJitter", 2), "Cloud Jitter", linear (0.0f, 100.0f), 40.0f,
                                       Attr().withLabel ("%")));
    layout.add (std::make_unique<APF> (makeID ("cloudPitch", 2), "Cloud Pitch Spread", linear (0.0f, 1200.0f), 100.0f,
                                       Attr().withLabel ("cents")));
    layout.add (std::make_unique<APF> (makeID ("cloudSpread", 2), "Cloud Stereo Spread", linear (0.0f, 100.0f), 60.0f,
                                       Attr().withLabel ("%")));
}

void addCloudPerformanceIds (juce::StringArray& /*ids*/)
{
}

} // namespace gapfill
