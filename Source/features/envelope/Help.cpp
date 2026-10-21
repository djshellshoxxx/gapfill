// C-4 help (order 5110, WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpEnvelope (5110, "Fill intensity envelope", [] (HelpWriter& w)
    {
        w.body ("Fill Shape changes how a fill develops over its own length. Amount sets how strong the effect is; "
                "at 0 % or with the Flat shape nothing changes at all.");
        w.entry ("Rise", "starts quiet and dark, ends at full level and brightness.");
        w.entry ("Fall", "starts at full level, then fades and darkens towards the end.");
        w.entry ("Rise-Fall", "swells to full level in the middle of the fill and eases out again.");
        w.entry ("Random Walk", "wanders between five random levels, repeatable for the same Seed and bar position.");
        w.body ("The shape follows the fill's own age, not the clock, so a short fill and a long fill both play the "
                "whole curve. Protection, ducking and fade lengths are unchanged.");
    });
}

} // namespace gapfill
