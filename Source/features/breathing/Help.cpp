// C-6 help (order 5130, WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpBreathing (5130, "Density breathing", [] (HelpWriter& w)
    {
        w.body ("Density Breathing makes the Density control swell and ease off over a cycle of 1, 2, 4 or 8 bars, "
                "so fills come in waves instead of at a constant rate. Depth sets the size of the swing: at 100 % "
                "Density moves up to half its range either side of your setting.");
        w.entry ("Sine", "smooth up-and-down.");
        w.entry ("Rise-Fall", "a straight ramp up, then down.");
        w.entry ("Pulse", "half the cycle busy, half the cycle calm.");
        w.body ("The cycle is locked to the song position, so loops and jumps always land in the same place. "
                "Breathing needs a playing transport and a known time signature; without them Density is used as set.");
    });
}

} // namespace gapfill
