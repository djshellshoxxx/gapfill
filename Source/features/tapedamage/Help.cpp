#include "../../gui/HelpRegistry.h"

namespace gapfill::tapedamage
{
namespace
{
    const HelpRegistrar tapeHelp (1820, "Tape damage", [] (HelpWriter& help)
    {
        help.body ("TAPE DAMAGE opens a separate per-fill card in Advanced view. Chance is the probability that a new fill uses it; zero is off. "
                   "Enable restores the last chance or starts at 50 percent. The nine controls are sampled at fill start, so automation affects future fills.");
        help.entry ("Motion", "Wow requests up to 40 cents of slow pitch movement; Flutter requests up to 15 cents of faster movement. "
                    "Instability adds bounded speed drift up to half a percent. Higher wow depth can use a faster slow motion to keep the delay inside its safe range.");
        help.entry ("Damage", "Saturation uses a level-compensated curve. Drop rate controls up to ten dropouts per second; Depth independently sets the level lost, "
                    "up to silence. Splice adds up to four read-position jumps per second. Dropouts and splices have at least one millisecond of smoothing.");
        help.entry ("Mix and timing", "Mix blends processed and original delayed fill independently of Chance. Zero Mix skips the stage. Active Tape Damage starts the fill "
                    "four milliseconds later inside its original deadline. Gaps shorter than 40 milliseconds, or too short for the delay and source, use the original fill. "
                    "Voice Formant can add its own delay afterward; the source ends early so both stages finish before protection. Host latency does not change.");
        help.body ("The older Tape Destruction card keeps its saved behaviour and controls. Macro version 3 Tape Mix controls Tape Damage Mix when its Chance is above zero; "
                   "otherwise it controls the older card's independent mix. It never routes to both at once. Tape Damage also works with the Tape Stop fill algorithm.");
        help.body ("Use Tab and Shift-Tab to reach Enable, Show or Hide controls and the knobs. Arrow keys edit the focused value; the shared right-click menu offers typed entry, "
                   "reset and MIDI learn. The summary updates as text and has no motion animation. Saved replay keeps the recorded decision and settings; "
                   "the current FX Send and band routing still apply. Older recipes without a Tape Damage decision stay unchanged.");
    });
}
}
