// C-9 help (order 5160, WP5 band 5100-5199).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpBandType (5160, "Fill type per band", [] (HelpWriter& w)
    {
        w.body ("In Spectral detector mode each lane (low, mid, high) can have its own fill type. Auto keeps the normal "
                "automatic choice; any other value forces that fill for gaps found in that lane.");
        w.body ("A fill type chosen in the main Fill Type control always wins over the lane types. If the forced fill is "
                "too long for the gap GapFill skips the gap ('No algorithm fits') instead of quietly playing something "
                "else. In Transient and Phrase detector modes the lane types have no effect.");
    });
}

} // namespace gapfill
