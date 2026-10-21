// Help sections for the recipe features (registers itself; HelpView.cpp stays frozen).
// WP4 help orders: 4010 audition, 4020 recipes (history, lock, like), 4030 export, 4040 A/B match.

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar recipesHelp (4020, "Fill history, Lock and More Like That",
        "Open the RECIPES tab at the bottom-left of the visualiser to see the last eight fills GapFill played, newest first. "
        "Each chip shows the algorithm, the subdivision, the band and the length, with a letter for the family "
        "(R rhythmic, T textural, P pitch and tape, D destructive). Previews are never listed. Click a chip to select it, "
        "right-click for its actions.\n"
        "AUDITION plays the selected recipe through the preview voice. LOCK THIS locks it and switches Lock on. "
        "MORE LIKE biases the next four fills towards its algorithm family and band; it never forces a fill or enables an "
        "algorithm whose weight is zero.\n"
        "LOCK keeps the character of the latest fill (algorithm, band, modifiers, pitch) for every gap that is filled. "
        "It does not force a fill into every gap, and it never relaxes protection: a locked fill shortens to fit a gap and is "
        "skipped when it would be shorter than 60 percent of its minimum length. If its band is switched off the nearest enabled "
        "band is used. A forced Fill Type overrides Lock. Each locked fill gets a fresh seed, so repeats are not literal loops.\n"
        "SAVE... writes the selected recipe to a small .gfrecipe file in Documents/GapFill/Recipes. LOAD... reads one back into "
        "Lock with Lock switched off, so you decide when to engage it. Files that are too large, damaged or out of range are "
        "refused with a message and change nothing.");
}

} // namespace gapfill
