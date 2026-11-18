#pragma once
#include "../commands/ControllerProfile.h"
#include <juce_data_structures/juce_data_structures.h>

namespace gapfill::controller
{
inline constexpr int kControllerStateBytes = 8 * 1024;
struct ControllerState
{
    commands::Profile profile;
};
/** No CC/id map or command map is encoded here: MIDIMAP and Cmd remain authoritative. */
bool decodeControllerState (const juce::ValueTree&, ControllerState&) noexcept;
juce::ValueTree encodeControllerState (const ControllerState&);
}
