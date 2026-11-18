#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
namespace gapfill { class GapFillAudioProcessor; }
namespace gapfill::harmonic
{
/** Message-thread owned transient panel. It owns all parameter attachments and
    reads only published meter atomics; no source or pitch-frame access. */
std::unique_ptr<juce::Component> makePitchSyncPanel (GapFillAudioProcessor&);
}
