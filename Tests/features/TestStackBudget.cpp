// Stack budget guard (Windows default stack is 1 MB; audio threads are smaller). Tests construct a few
// processors on the stack, so the big objects must stay small by value and keep their buffers on the heap.

#include "../support/TestRig.h"

#include "dsp/FillEngine.h"
#include "dsp/FillVoice.h"

#include <iostream>

using namespace gapfill;
using namespace gftest;

GF_TEST (StackBudget, big_objects_stay_small_by_value)
{
    std::cout << "         sizeof: processor " << sizeof (GapFillAudioProcessor) << " engine " << sizeof (FillEngine)
              << " voice " << sizeof (FillVoice) << " explain " << sizeof (explain::ExplainState)
              << " fxChain " << sizeof (fx::FxChain) << " shaping " << sizeof (ShapingRuntime) << std::endl;

    std::cout << "         sizeof: analysis " << sizeof (AnalysisEngine) << " viz " << sizeof (VisualisationFifo)
              << " liveControl " << sizeof (live::LiveControl) << " recipes " << sizeof (recipes::RecipeServices)
              << " ui " << sizeof (ui::UiState) << " midiLearn " << sizeof (MidiLearnManager)
              << " history " << sizeof (RecipeHistory) << " outcomes " << sizeof (GapOutcomeLog)
              << " events " << sizeof (FillEventQueue) << " fxUi " << sizeof (fx::FxUiState) << std::endl;

    GF_EXPECT_MSG (sizeof (FillVoice) <= 24 * 1024, "FillVoice is " + juce::String ((int) sizeof (FillVoice)));
    GF_EXPECT_MSG (sizeof (FillEngine) <= 96 * 1024, "FillEngine is " + juce::String ((int) sizeof (FillEngine)));
    GF_EXPECT_MSG (sizeof (GapFillAudioProcessor) <= 192 * 1024, "processor is " + juce::String ((int) sizeof (GapFillAudioProcessor)));
}
