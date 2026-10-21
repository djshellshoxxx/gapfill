// C-1 help (order 5120, WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpPersonality (5120, "Fill personalities", [] (HelpWriter& w)
    {
        w.body ("A personality tilts which fill types are chosen and nudges Density, Complexity, Chaos and Intensity "
                "for the character you want. It never moves your knobs: the knobs with a thin inner arc show the value "
                "GapFill is really using. A weight you set to 0 stays off, and a manually chosen Fill Type is respected "
                "(only the four nudges still apply).");
        w.entry ("Subtle", "gentle, sparse, soft fills.");
        w.entry ("Percussive", "stutters and scrambles that follow the groove.");
        w.entry ("Glitchy", "crushed, granular, scrambled.");
        w.entry ("Psychedelic", "spectral, frozen and pitch-bending.");
        w.entry ("Dub", "delay-heavy with filter sweeps.");
        w.entry ("Aggressive", "distorted, crushed, loud.");
        w.entry ("Ambient", "textures, freezes and spectral washes, played sparsely.");
        w.entry ("Unhinged", "flattens your weights towards equal chances for maximum variety and adds a little "
                             "controlled mistake chance.");
    });
}

} // namespace gapfill
