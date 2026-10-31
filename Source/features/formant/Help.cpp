#include "../../gui/HelpRegistry.h"

namespace gapfill::formant
{
namespace
{
    const HelpRegistrar formantHelp (1200, "Voice Formant", [] (HelpWriter& help)
    {
        help.body ("FORMANT opens per-fill Amount, Shift, Tilt, Vowel, Preserve and Detail. Values are sampled at fill start; "
                   "automation changes the next fill. This is separate from the existing Formant bus effect and both can be used deliberately.");
        help.entry ("Amount", "0 is off and uses no voice processor. Active processing delays the generated onset by about 11 ms "
                    "(512 samples at 44.1/48 kHz, 1024 at 88.2/96 kHz and 2048 at 176.4/192 kHz). Host latency does not change.");
        help.entry ("Shift / Tilt", "Moves resonances by semitones without retuning the underlying sound, or brightens/darkens its envelope in dB per octave.");
        help.entry ("Vowel / Preserve / Detail", "A/E/I/O/U add vowel-like resonances, not words. Preserve blends the original envelope back in. "
                    "Detail selects 0.5, 1 or 2 ms of cepstral resolution.");
        help.body ("Only two processed voices run at once. A third voice, or a gap too short for the onset delay and its algorithm, "
                   "plays unprocessed. Source generation ends early so delayed audio finishes before the protected event. Panic still fades the output.");
    });
}
} // namespace gapfill::formant
