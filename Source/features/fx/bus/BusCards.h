#pragma once

// The WP9 bus cards, owned by FxChain (docs/dev/12 FX-0: one explicit line per card
// in FxChain::build()). Permanent chain order: formant, resonator, fsdelay, dub,
// convo, spfreeze (CardId order).

#include "ConvoCard.h"
#include "DubCard.h"
#include "FormantCard.h"
#include "FsDelayCard.h"
#include "ResonatorCard.h"
#include "SpFreezeCard.h"

namespace gapfill::fx::bus
{

struct BusCards
{
    FormantCard formant;
    ResonatorCard resonator;
    FsDelayCard fsdelay;
    DubCard dub;
    ConvoCard convo;
    SpFreezeCard spfreeze;
};

} // namespace gapfill::fx::bus
