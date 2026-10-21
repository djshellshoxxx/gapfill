// C-8 help (order 5190, WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpEvolve (5190, "Evolve", [] (HelpWriter& w)
    {
        w.body ("Evolve slowly changes the character of the fills on its own, so a long section keeps moving. Every "
                "8, 16 or 32 bars it glides towards the centre of one of the Surprise Me styles (for example Tape "
                "Breakdown or Glitch Storm), in an order that depends on the Seed and never repeats the same style "
                "twice in a row.");
        w.body ("Evolve only nudges Fill, Intensity, Complexity, Chaos, Density and which fill types are favoured, and "
                "only part of the way, so your own settings, macros and personality stay audible. A fill type you set "
                "to 0 stays off. Detector, protection, mix, latency and the Fill Type control are never touched. "
                "Evolve needs a playing transport and a known time signature; without them it does nothing. The "
                "current style is shown next to the Evolve control.");
    });
}

} // namespace gapfill
