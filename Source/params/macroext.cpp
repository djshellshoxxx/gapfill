#include "macroext.h"
#include "ParamBuilders.h"

namespace gapfill
{
void addMacroExtParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    const auto percent = [&] (const char* id, const char* name, float low, float high, float initial)
    {
        layout.add (std::make_unique<APF> (makeID (id, 3), name,
            juce::NormalisableRange<float> (low, high, .01f), initial, Attr().withLabel ("%")));
    };
    percent ("fm23_macro5", "Macro 5", 0, 100, 0);
    percent ("fm23_macro6", "Macro 6", 0, 100, 0);
    percent ("fm23_macro7", "Macro 7", 0, 100, 0);
    percent ("fm23_macro8", "Macro 8", 0, 100, 0);
    const juce::StringArray sources { "Knob", "LFO A", "LFO B", "Energy" };
    for (int macro = 1; macro <= 8; ++macro)
    {
        const auto id = "fm23_src" + juce::String (macro);
        layout.add (std::make_unique<APC> (makeID (id.toRawUTF8(), 3),
            "Macro " + juce::String (macro) + " Source", sources, 0));
    }
    for (int macro = 1; macro <= 8; ++macro)
    {
        const auto id = "fm23_depth" + juce::String (macro);
        const auto name = "Macro " + juce::String (macro) + " Depth";
        percent (id.toRawUTF8(), name.toRawUTF8(), -100, 100, 50);
    }
    const juce::StringArray shapes { "Sine", "Triangle", "Saw", "Square", "Random step", "Random glide" };
    layout.add (std::make_unique<APC> (makeID ("fm23_lfoAShape", 3), "LFO A Shape", shapes, 0));
    layout.add (std::make_unique<APC> (makeID ("fm23_lfoBShape", 3), "LFO B Shape", shapes, 0));
    const juce::StringArray divisions { "1/16", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2",
                                        "1 bar", "2 bars", "4 bars", "8 bars", "1/32", "1/16T", "1/16.", "1/2T" };
    layout.add (std::make_unique<APC> (makeID ("fm23_lfoADiv", 3), "LFO A Division", divisions, 8));
    layout.add (std::make_unique<APC> (makeID ("fm23_lfoBDiv", 3), "LFO B Division", divisions, 8));
    juce::NormalisableRange<float> rate (.05f, 10.0f, .01f);
    rate.setSkewForCentre (1);
    layout.add (std::make_unique<APF> (makeID ("fm23_lfoAHz", 3), "LFO A Free Rate", rate, .5f, Attr().withLabel ("Hz")));
    layout.add (std::make_unique<APF> (makeID ("fm23_lfoBHz", 3), "LFO B Free Rate", rate, .5f, Attr().withLabel ("Hz")));
}

void addMacroExtPerformanceIds (juce::StringArray& ids)
{
   #define ADD_MACRO_EXT(member, id) ids.add (id);
    GAPFILL_PARAMS_MACROEXT (ADD_MACRO_EXT)
   #undef ADD_MACRO_EXT
}
} // namespace gapfill
