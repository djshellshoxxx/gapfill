#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace gapfill::tapedamage
{
/** Message-thread panel factory shared by the launcher and visual checks. */
std::unique_ptr<juce::Component> makeTapeDamagePanel (juce::AudioProcessorValueTreeState&);
}
