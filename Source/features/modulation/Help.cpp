#include "../../gui/HelpRegistry.h"

namespace gapfill
{
namespace
{
const HelpRegistrar extendedMacroHelp (1230, "Macros and modulation", [] (HelpWriter& writer)
{
    writer.body ("ENABLE 8 selects eight macros and two shared LFOs. Your four-macro setup is kept, including its LFOs; "
                 "RETURN 4 restores that setup. The extra four knobs are behind +4. MACROS switches either selected setup on or off.");
    writer.body ("Select M1 to M8 to edit a macro. Knob uses its knob directly. LFO A, LFO B and Energy add movement around the knob. "
                 "Depth controls how far it moves; negative depth reverses the movement. Energy follows the incoming audio. "
                 "Energy cannot drive a width mapping; choose a knob or LFO for width.");
    writer.body ("The two shared LFOs have Sine, Triangle, Saw, Square, Random step and Random glide shapes. "
                 "They follow the host position and musical division when transport is available. Without it, Free Rate sets their speed. "
                 "Random shapes repeat with the same seed and position. A bar follows the host time signature.");
    writer.body ("Each macro owns up to eight targets. A target belongs to one macro; Min, Max and Curve set its response. "
                 "The eight-macro setup samples fill controls when a fill is chosen, so a running fill keeps its settings. "
                 "Width and continuous effects are smoothed. The old four-macro LFO and mapping-smoothing setup is retained for RETURN 4.");
    writer.body ("FX send multiplies the Tension effect-send lane. Tape mix follows the detailed Tape path while its amount is above zero, "
                 "otherwise the original Tape effect. A macro can enable detailed Tape and set its mix together. "
                 "Resonator decay uses milliseconds. Macro names and assignments are saved in the project and optional bank mappings; "
                 "parameter-only presets do not carry assignments.");
});
}
} // namespace gapfill
