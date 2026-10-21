#include "phrase.h"
#include "ParamBuilders.h"

namespace gapfill
{

void addPhraseParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;

    // C-7: Off / bias 0 are neutral. phraseOffset (doc 12 F-L08 note) shifts the phrase start in bars.
    layout.add (std::make_unique<APC> (makeID ("phraseBars", 2), "Phrase Length",
                                       juce::StringArray { "Off", "2 bars", "4 bars", "8 bars", "16 bars" }, 0));
    layout.add (percent ("phraseBias", "Phrase Bias", 0.0f, 2));
    layout.add (std::make_unique<APInt> (makeID ("phraseOffset", 2), "Phrase Offset", 0, 15, 0));
}

void addPhrasePerformanceIds (juce::StringArray& ids)
{
    for (const char* id : { "phraseBars", "phraseBias", "phraseOffset" })
        ids.add (id);
}

} // namespace gapfill
