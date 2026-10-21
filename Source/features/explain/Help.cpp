// Help section for the "Explain & assist" package (registered through the help
// registry, orders 1000-8999 belong to features).

#include "../../gui/HelpRegistry.h"

namespace gapfill
{

namespace
{
    const HelpRegistrar helpExplain (3000, "Status, skipped gaps and suggestions", [] (HelpWriter& w)
    {
        w.body ("The strip at the bottom of the window shows ONE message, the most important one. "
                "Click it (or STATUS) for the details. When everything is fine it stays quiet.");

        w.entry ("Last gap: skipped", "why the most recent gap was not filled, with the numbers GapFill used "
                                      "(for example: Gap too short, 38 ms free, need 60 ms). Hover a hollow diamond "
                                      "in the visualiser to see the same explanation and what to change.");
        w.entry ("Diamond with a letter", "a skipped gap. The letter is the reason: F Fill is 0, S no source, "
                                           "T too soon, V voices busy, G gap too short, E guard used the space, "
                                           "B no band, D density roll, A no algorithm fits, L locked fill, "
                                           "N bar budget, Q quiet zone, C sidechain gate, P panic, Y co-op yield, "
                                           "Z forced fill unsafe, X fill could not be built.");
        w.entry ("Dashed line", "the gap threshold. Quiet stretches below it are not gaps, so they get no marker.");
        w.entry ("Legend", "every state is drawn with a colour AND a pattern. Click LEGEND to fold it away.");
        w.entry ("Why so few fills?", "in the status details: the three most common reasons gaps were skipped "
                                      "this session, each with the control to change.");
        w.entry ("Health messages", "No sidechain, silent sidechain, transport unavailable, high CPU, dropped "
                                    "events and bypass. A message appears only after the condition has lasted a moment "
                                    "and leaves a moment after it is gone, so the line never flickers. "
                                    "You can hide most messages in the details; CPU-critical and panic always show.");
        w.entry ("Suggestion", "when GapFill is doing nothing for a clear reason it proposes ONE change, for example "
                               "Min Gap 120 -> 85 ms. APPLY makes the change (one undo step) and UNDO puts the exact "
                               "previous values back. Changes that weaken protection ask for a confirmation in the "
                               "details first. NOT NOW hides a suggestion for five minutes; DON'T SUGGEST THIS turns "
                               "that rule off. Switch all suggestions off with Show suggestions.");
        w.entry ("Timeline", "the TIMELINE button above the visualiser opens a time axis of events, protected zones, "
                             "gaps, fills and skipped gaps. PAUSE freezes only the picture. The list on the right "
                             "repeats the last 20 events as text for keyboard and screen-reader use.");
        w.entry ("Latency and CPU", "the latency GapFill reports to the host (PDC) and an ESTIMATE of how much of each "
                                    "audio block GapFill uses. It is not your DAW's CPU meter.");
        w.entry ("Diagnostics", "RECORD keeps the parameter changes, commands, transport position, block sizes and "
                                "the result of every gap - never audio, file names or paths - so a problem can be "
                                "replayed. SAVE writes a .gfevents file of at most 2 MB. In the standalone app, "
                                "OPEN REPLAY runs a recording against built-in test audio.");
    });
}

} // namespace gapfill
