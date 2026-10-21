// E-2 help: "What follows tempo" (docs/dev/03 E-2). Order 5100 (WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpSync (5100, "What follows tempo", [] (HelpWriter& w)
    {
        w.body ("These parts of a fill are locked to the song tempo. Everything else follows the fill length.");
        w.entry ("Stutter slice length", "follows tempo when Sync is on (Stutter division).");
        w.entry ("Delay tap spacing", "follows tempo when Sync is on (Delay division).");
        w.entry ("Maximum fill length", "follows tempo when Max Fill Sync is on (Max Fill division).");
        w.entry ("Fill length grid", "Quantise Length rounds a fill down to a whole number of 1/4, 1/8, 1/16 or "
                                      "1/32 notes while the host tempo is known. Off keeps the free length.");
        w.entry ("Tremolo, Pan Motion and Scramble slices", "follow tempo when Sync is on and Sync Mods is on.");
        w.entry ("Everything else", "grain rate, comb pitch, filter sweeps and pitch ramps never follow tempo - "
                                     "they follow the fill length.");
        w.body ("When a synced time would fall outside its limits (stutter 8-500 ms, delay 10-1000 ms, max fill "
                "20-2000 ms) GapFill halves or doubles the note value instead of silently clipping the "
                "milliseconds, so a 1/2 note at 60 BPM plays as 1/4. A tempo change during a fill does not "
                "retime that fill; the next fill uses the new tempo.");
        w.body ("If the host reports no tempo (Standalone, or some hosts while stopped) GapFill uses the last "
                "tempo the host reported, otherwise the fallback tempo from Options (120 BPM by default).");
    });
}

} // namespace gapfill
