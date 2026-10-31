#include "../../gui/HelpRegistry.h"

namespace gapfill::timeline
{
namespace
{
    const HelpRegistrar timelineHelp (1210, "Detailed gap timeline", [] (HelpWriter& writer)
    {
        writer.body ("TIMELINE expands the visualiser inside the existing window. Mix, bypass and panic remain available. "
                     "The lanes show incoming audio and detected events, gap confidence, protection, fill outcomes and, "
                     "in wide Spectral views, low/mid/high confidence. The dashed gap line is the engine's entry threshold.");
        writer.entry ("Protection", "Hit protection is hatched, the next-event guard is dotted and lookahead is dashed. "
                                    "Bar numbers use the host's recorded time signature; without position information the view uses seconds.");
        writer.entry ("Filled and skipped gaps", "Solid blocks show the recorded fill algorithm and planned duration; protection can shorten the audible tail. "
                                                "Hollow markers carry the engine's skip reason. Preview fills use a separate hatch.");
        writer.entry ("Freeze and inspect", "FREEZE pauses the display while audio and recording continue. Drag the frozen view or use Left/Right "
                                           "to scrub 100 ms; hold Shift for one second. Home returns to live. Hover, or press I, to inspect a recorded outcome.");
        writer.entry ("Event list", "EVENT LIST replaces the canvas with recent outcomes as text for keyboard and screen-reader use. "
                                   "The inspector repeats the recorded reason and available/needed time; it does not guess why a fill was skipped.");
        writer.entry ("Keyboard", "With Keyboard Navigation enabled, F toggles Freeze, L toggles Event list, I toggles the inspector and Esc closes. "
                                 "Ctrl/Command/Alt shortcuts pass through to the host. With Reduced Motion, the display steps four times per second.");
        writer.entry ("Incomplete history", "Striped missing regions mean display records were dropped; seams mark a reset or transport discontinuity. "
                                           "They describe missing display history, not an audio-processing failure.");
    });
}
} // namespace gapfill::timeline
