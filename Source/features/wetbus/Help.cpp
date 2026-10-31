#include "../../gui/HelpRegistry.h"

namespace gapfill::wetbus
{
namespace
{
    // 1210 is already assigned to the timeline. Keep the existing section's
    // order and put this additive routing explanation next to Routing/system.
    const HelpRegistrar wetBusHelp (6410, "Separate wet output", [] (HelpWriter& writer)
    {
        writer.body ("The optional Wet output carries only GapFill's generated sound after protection and width, with the same latency as Main. "
                     "Enable the extra output in your DAW's plug-in routing; it is off in new projects. Mono uses half the sum of left and right. "
                     "Dry, Wet and Output leave this output alone. Wet bus level trims it from -24 to +12 dB; bypass and Panic silence it.");
        writer.body ("Main output mode defaults to Dry + Wet. Dry only removes the fills from Main while the extra Wet output is enabled, "
                     "so you can rebuild the mix on separate tracks. If that output is disabled, the fills stay in Main. "
                     "Wet Only overrides Dry only and still sends the generated sound to Main. Mode and level changes use a 20 ms fade.");
        writer.body ("In REAPER, give the track four channels and map the plug-in's Wet output pins to channels 3/4, then route those channels to a separate track. "
                     "In Bitwig or Cubase, enable the plug-in's additional Wet output in the device or routing controls. "
                     "Other hosts expose extra effect outputs differently; consult your host's routing guide. "
                     "The extra output is unavailable in Audio Unit builds.");
    });
}
} // namespace gapfill::wetbus
