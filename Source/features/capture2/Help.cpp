// C-10 help (order 5170, WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpCapture2 (5170, "Capture length and recapture", [] (HelpWriter& w)
    {
        w.body ("Capture freezes the audio your fills are built from. Capture Length (250 ms to 2 s, default 1 s) "
                "sets how much of the material before the freeze point the fills may use. Recapture refreshes the "
                "frozen material with the latest stretch of live audio while Capture stays on.");
        w.body ("A recapture takes a moment: the status shows 'Capture: pending' while the new material is copied in "
                "small steps, and the previous capture keeps being used until the copy is finished. Switching "
                "Capture off cancels a pending recapture and returns to live audio. Changing the sample rate "
                "clears the capture.");
    });
}

} // namespace gapfill
