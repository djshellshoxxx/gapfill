// C-7 help (order 5140, WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpPhrase (5140, "Phrase awareness", [] (HelpWriter& w)
    {
        w.body ("Phrase Length tells GapFill how long your musical phrase is (2, 4, 8 or 16 bars). In the last bar of "
                "each phrase the chance of a fill rises, up to 2.5 times at full Bias, so the phrase ends with a fill "
                "the way a drummer would play it.");
        w.entry ("Bias", "how strongly the last bar is favoured. 0 % turns the feature off.");
        w.entry ("Offset", "shifts where the phrase starts, in bars, if your arrangement does not start on bar 1.");
        w.body ("Phrase awareness only raises the probability of a fill in a gap that already qualifies. It never "
                "creates a gap and never overrides Minimum Gap or protection. It needs a playing transport and a "
                "known time signature.");
    });
}

} // namespace gapfill
