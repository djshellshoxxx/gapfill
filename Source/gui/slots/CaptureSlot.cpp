// Worked example for the editor slot mechanism (docs/dev/02 F0-9): the CAPTURE
// button lives in Slot::CreativeExtra instead of being hand-placed by the
// Creative panel. Same control, tooltip and parameter attachment as before.

#include "../Knob.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    const SlotRegistrar captureButton ({ Slot::CreativeExtra, 100,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<GapToggle> (
                proc.getValueTreeState(), "capture", "CAPTURE",
                "Freeze the source: fills keep using the audio heard when Capture was switched on, "
                "while detection, protection and the dry signal follow the live input. Switch off to release.");
        } });
}

} // namespace gapfill
