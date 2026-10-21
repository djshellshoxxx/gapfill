// Help section for E-1 / U-3 (registers itself; HelpView.cpp stays frozen).
// WP4 help orders: 4010 audition, 4020 recipes (history, lock, like), 4030 export, 4040 A/B match.

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar auditionHelp (4010, "Fill Audition",
        "AUDITION plays one fill right now, built from the last few seconds of audio with your current "
        "settings, so you can hear a change without waiting for a gap. It needs audio playing through the plugin; "
        "in a stopped host it says \"Start playback to audition\".\n"
        "A preview is shown in green in the visualiser and is capped at the shorter of Max Fill and 2 seconds. "
        "It obeys wet level, width, output trim and the protection ducks, but it never counts as a real fill: "
        "it is not added to the history, is not used by Fill Lock or More Like That, and never changes what "
        "the plugin does at the next real gap. Pressing it again restarts with a new variation.\n"
        "Right-click the button to map a MIDI CC (a rising edge to 64 or more fires once, and it re-arms below 48) "
        "or a trigger note and channel. A NoteOn at the chosen note fires one preview, harder notes are louder. "
        "Learn waits up to 10 seconds. These settings are saved with the project and are not host-automatable.");
}

} // namespace gapfill
