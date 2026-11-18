#include "../info/ParamInfo.h"

namespace gapfill::info
{
namespace
{
const ParamInfoRegistrar midiExtendedInfo
{
    { "fl11_on", "Extended MIDI Output", "Generate Hit, Gap, Fill and confidence MIDI instead of legacy gap/fill output", "", "" },
    { "fl11_chan", "MIDI Event Channel", "Channel of the generated detector events", "1-16", "" },
    { "fl11_hit_on", "MIDI Hit Events", "Send a short note for each detected onset", "", "" },
    { "fl11_gap_on", "MIDI Gap Events", "Hold a note during an eligible gap", "", "" },
    { "fl11_fill_on", "MIDI Fill Events", "Hold a note for the planned fill duration", "", "" },
    { "fl11_note_hit", "MIDI Hit Note", "Note number for detected hits; displayed using C3 = 60", "0-127", "" },
    { "fl11_note_gap", "MIDI Gap Note", "Note number held during a gap", "0-127", "" },
    { "fl11_note_fill", "MIDI Fill Note", "Note number for fill events", "0-127", "" },
    { "fl11_vel", "MIDI Event Velocity", "Follow detector strength or use fixed velocity 100", "", "" },
    { "fl11_len_ms", "MIDI Hit Length", "Duration of a detected-hit note", "10-500 ms", "" },
    { "fl11_cc", "MIDI Confidence CC", "Confidence controller; 0 is Off, channel-mode numbers are refused", "0-119", "" },
    { "fl11_timing", "MIDI Hit Timing", "Detection-early moves Hit notes; Gap and Fill stay aligned to output audio", "", "" }
};
}
}
