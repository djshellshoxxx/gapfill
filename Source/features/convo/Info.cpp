// U-9: help text for the reserved F-L21 detailed Hit Reverb controls (L21EXT, host indices 286-294).
// The detailed engine is not wired yet (docs/progress/F-L21-EXTENSION-CONTRACT-20261031.md); the released
// cvAmount / cvSize / cvDamp card is unchanged, so each line says the control is not active yet.

#include "../info/ParamInfo.h"

namespace gapfill::info
{
namespace
{
    const ParamInfoRegistrar hitReverbExtensionInfo
    {
        { "fl21_mode",     "Hit Reverb Mode",      "Reserved: Off keeps the released Hit Reverb; the detailed mode is not active yet", "", "" },
        { "fl21_mix",      "Hit Reverb Mix",       "Reserved: return level of the detailed Hit Reverb; not active yet", "", "%" },
        { "fl21_source",   "Hit Reverb Source",    "Reserved: last hit, strongest hit or manual capture; not active yet", "", "" },
        { "fl21_hold",     "Hit Reverb Hold",      "Reserved: keeps the current impulse and blocks captures; not active yet", "", "" },
        { "fl21_length",   "Hit Reverb Length",    "Reserved: length of the captured impulse; not active yet", "50-800", "ms" },
        { "fl21_preDelay", "Hit Reverb Pre-delay", "Reserved: delay before the impulse peak; not active yet", "0-50", "ms" },
        { "fl21_damp",     "Hit Reverb Damp",      "Reserved: cutoff that darkens the impulse; not active yet", "2000-20000", "Hz" },
        { "fl21_decay",    "Hit Reverb Decay",     "Reserved: how quickly the impulse tail fades; not active yet", "0-100", "%" },
        { "fl21_stereo",   "Hit Reverb Stereo",    "Reserved: keep the impulse stereo or fold it to mono; not active yet", "", "" },
    };
}
} // namespace gapfill::info
