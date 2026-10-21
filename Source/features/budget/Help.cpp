// F-D01 help (order 5150, WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpBudget (5150, "Fill budget and quiet zones", [] (HelpWriter& w)
    {
        w.body ("Fills Per Bar limits how many fills may start in one bar, so a busy section cannot be taken over by "
                "ear candy. Quiet Zone keeps fills from starting on part of the bar.");
        w.entry ("Beat 1 / Last beat / Last 2 beats", "no fill may start in that part of every bar.");
        w.entry ("Last bar of phrase", "no fill may start in the last bar of the phrase set in Phrase Length.");
        w.body ("A fill that is already playing is never cut short by a quiet zone. Both need a playing transport and a "
                "known time signature; without them GapFill fills as if they were Off. A gap skipped by these rules "
                "is reported as 'Fill budget for this bar used' or 'Inside a quiet zone'.");
    });
}

} // namespace gapfill
