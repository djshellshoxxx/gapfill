#include "../info/ParamInfo.h"

namespace gapfill::info
{
namespace
{
const ParamInfoRegistrar modulationInfo {
    { "fm23_macro5", "Macro 5", "Extra macro control for the selected eight-macro setup", "0-100", "%" },
    { "fm23_macro6", "Macro 6", "Extra macro control for the selected eight-macro setup", "0-100", "%" },
    { "fm23_macro7", "Macro 7", "Extra macro control for the selected eight-macro setup", "0-100", "%" },
    { "fm23_macro8", "Macro 8", "Extra macro control for the selected eight-macro setup", "0-100", "%" },
    { "fm23_src1", "Macro 1 Source", "Choose a knob, shared LFO or input-energy source for this macro", "", "" },
    { "fm23_src2", "Macro 2 Source", "Choose a knob, shared LFO or input-energy source for this macro", "", "" },
    { "fm23_src3", "Macro 3 Source", "Choose a knob, shared LFO or input-energy source for this macro", "", "" },
    { "fm23_src4", "Macro 4 Source", "Choose a knob, shared LFO or input-energy source for this macro", "", "" },
    { "fm23_src5", "Macro 5 Source", "Choose a knob, shared LFO or input-energy source for this macro", "", "" },
    { "fm23_src6", "Macro 6 Source", "Choose a knob, shared LFO or input-energy source for this macro", "", "" },
    { "fm23_src7", "Macro 7 Source", "Choose a knob, shared LFO or input-energy source for this macro", "", "" },
    { "fm23_src8", "Macro 8 Source", "Choose a knob, shared LFO or input-energy source for this macro", "", "" },
    { "fm23_depth1", "Macro 1 Depth", "Moves the macro around its knob value; negative depth reverses the source", "-50 to 50", "%" },
    { "fm23_depth2", "Macro 2 Depth", "Moves the macro around its knob value; negative depth reverses the source", "-50 to 50", "%" },
    { "fm23_depth3", "Macro 3 Depth", "Moves the macro around its knob value; negative depth reverses the source", "-50 to 50", "%" },
    { "fm23_depth4", "Macro 4 Depth", "Moves the macro around its knob value; negative depth reverses the source", "-50 to 50", "%" },
    { "fm23_depth5", "Macro 5 Depth", "Moves the macro around its knob value; negative depth reverses the source", "-50 to 50", "%" },
    { "fm23_depth6", "Macro 6 Depth", "Moves the macro around its knob value; negative depth reverses the source", "-50 to 50", "%" },
    { "fm23_depth7", "Macro 7 Depth", "Moves the macro around its knob value; negative depth reverses the source", "-50 to 50", "%" },
    { "fm23_depth8", "Macro 8 Depth", "Moves the macro around its knob value; negative depth reverses the source", "-50 to 50", "%" },
    { "fm23_lfoAShape", "LFO A Shape", "Shape of the first shared LFO; random shapes repeat for the same seed", "", "" },
    { "fm23_lfoBShape", "LFO B Shape", "Shape of the second shared LFO; random shapes repeat for the same seed", "", "" },
    { "fm23_lfoADiv", "LFO A Division", "Cycle length of LFO A while the host supplies tempo and position", "", "" },
    { "fm23_lfoBDiv", "LFO B Division", "Cycle length of LFO B while the host supplies tempo and position", "", "" },
    { "fm23_lfoAHz", "LFO A Free Rate", "Speed of LFO A when the host supplies no usable position or tempo", "0.1-2", "Hz" },
    { "fm23_lfoBHz", "LFO B Free Rate", "Speed of LFO B when the host supplies no usable position or tempo", "0.1-2", "Hz" }
};
}
} // namespace gapfill::info
