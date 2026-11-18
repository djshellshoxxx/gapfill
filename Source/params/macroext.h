#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace gapfill
{
void addMacroExtParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addMacroExtPerformanceIds (juce::StringArray&);
}

#define GAPFILL_PARAMS_MACROEXT(X) \
    X (macroExt5, "fm23_macro5") \
    X (macroExt6, "fm23_macro6") \
    X (macroExt7, "fm23_macro7") \
    X (macroExt8, "fm23_macro8") \
    X (macroSource1, "fm23_src1") \
    X (macroSource2, "fm23_src2") \
    X (macroSource3, "fm23_src3") \
    X (macroSource4, "fm23_src4") \
    X (macroSource5, "fm23_src5") \
    X (macroSource6, "fm23_src6") \
    X (macroSource7, "fm23_src7") \
    X (macroSource8, "fm23_src8") \
    X (macroDepth1, "fm23_depth1") \
    X (macroDepth2, "fm23_depth2") \
    X (macroDepth3, "fm23_depth3") \
    X (macroDepth4, "fm23_depth4") \
    X (macroDepth5, "fm23_depth5") \
    X (macroDepth6, "fm23_depth6") \
    X (macroDepth7, "fm23_depth7") \
    X (macroDepth8, "fm23_depth8") \
    X (macroLfoAShape, "fm23_lfoAShape") \
    X (macroLfoBShape, "fm23_lfoBShape") \
    X (macroLfoADiv, "fm23_lfoADiv") \
    X (macroLfoBDiv, "fm23_lfoBDiv") \
    X (macroLfoAHz, "fm23_lfoAHz") \
    X (macroLfoBHz, "fm23_lfoBHz")
