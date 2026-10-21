// Help sections for the recipe features (registers itself; HelpView.cpp stays frozen).
// WP4 help orders: 5010 audition, 5020 recipes (history, lock, like), 5030 export, 5040 A/B match.

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar recipesHelp (5020, "Fill history, Lock and More Like That",
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

    const HelpRegistrar exportHelp (5030, "Export Fill",
        "EXPORT... in the recipes drawer saves the audio GapFill added for the selected fill (or for the latest fill or "
        "audition when none is selected) as a 24-bit stereo WAV at the session sample rate, with 20 ms of lead-in and the "
        "fade-out tail. Only the wet bus is exported: what GapFill generated after Low Protect, Center Protect and Wet Width, "
        "before the dry/wet mix, so the file is the same whatever the Dry and Wet knobs say. The file name is "
        "GapFill_<Algorithm>_<tempo>bpm_<date>-<time>.wav.\n"
        "Drag a chip out of the drawer into your DAW to drop the same audio on a track.\n"
        "The last eight seconds of the wet bus are kept while the editor is open (and for a minute after it closes); "
        "older fills, and fills played before the editor was opened, show \"That fill is too old\". Only one export runs at a time. "
        "Nothing is recorded when the editor has never been opened, so a plugin you only mix with costs nothing.");

    const HelpRegistrar matchHelp (5040, "A/B level matching",
        "A louder side wins every comparison. MATCH (in the recipes drawer, or right-click AUDITION) switches on level matching "
        "for the header A/B: each side measures the loudness of the wet signal it produces (a 100 Hz high-pass, 400 ms windows, "
        "silence ignored, averaged over 10 seconds). Once both sides have 3 seconds of fills measured, switching trims the output "
        "by up to 6 dB, over 50 ms, so the two sides sound equally loud, and the footer shows the trim, for example \"+1.4 dB match\".\n"
        "The trim is only an output level: it is never saved with the project or a preset, is not a parameter, and disappears "
        "when MATCH is switched off. Swapping A and B changes no parameter and adds no undo step.\n"
        "Recipe compare: select a fill chip and press SET A, select another and press SET B, then press A/B to alternate the "
        "audition between the two recipes. Each recipe's level is measured the same way and the same trim keeps them equally loud.");
}

} // namespace gapfill
