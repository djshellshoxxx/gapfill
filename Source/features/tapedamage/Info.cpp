#include "../info/ParamInfo.h"

namespace gapfill::tapedamage
{
namespace
{
    const info::ParamInfoRegistrar tapeInfo {
        { "fm20_amount", "Tape Damage Chance", "Probability per fill; zero is off", "0-50", "%" },
        { "fm20_wow", "Tape Damage Wow", "Slow pitch motion; 100 percent requests 40 cents", "0-50", "%" },
        { "fm20_flutter", "Tape Damage Flutter", "Faster pitch motion; 100 percent requests 15 cents", "0-50", "%" },
        { "fm20_sat", "Tape Damage Saturation", "Level-compensated tape saturation", "0-50", "%" },
        { "fm20_dropDensity", "Tape Damage Dropout Density", "Dropout rate; 100 percent is ten events per second", "0-40", "%" },
        { "fm20_dropDepth", "Tape Damage Dropout Depth", "Level lost inside each dropout; 100 percent reaches silence", "0-80", "%" },
        { "fm20_splice", "Tape Damage Splice", "Read-tap splices; 100 percent is four events per second", "0-40", "%" },
        { "fm20_instab", "Tape Damage Instability", "Bounded speed drift; 100 percent stays within half a percent", "0-50", "%" },
        { "fm20_mix", "Tape Damage Mix", "Independent wet blend, sampled at fill start", "25-100", "%" }
    };
}
} // namespace gapfill::tapedamage
