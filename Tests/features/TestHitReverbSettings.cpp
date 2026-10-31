#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
#endif
#include "../../Source/features/convo/HitReverbSettings.h"
#include <limits>
using namespace gapfill::convreverb;

GF_TEST (HitReverbSettings, defaults_keep_released_convolution_selected)
{
    const Settings value;
    GF_EXPECT (! value.enabled && value.mix01 == 0 && ! value.hold);
    GF_EXPECT (value.source == Source::LastHit && value.stereo == Stereo::Stereo);
    GF_EXPECT (value.lengthMs == 250 && value.preDelayMs == 0 && value.dampHz == 12000 && value.decay01 == 0.4f);
}
GF_TEST (HitReverbSettings, malformed_optional_controls_cannot_create_unbounded_dsp_work)
{
    Settings value;
    value.enabled = true; value.hold = true;
    value.mix01 = std::numeric_limits<float>::quiet_NaN();
    value.lengthMs = std::numeric_limits<float>::infinity();
    value.preDelayMs = -100; value.dampHz = 192000; value.decay01 = 2;
    value.source = static_cast<Source> (255); value.stereo = static_cast<Stereo> (255);
    value = sanitise (value);
    GF_EXPECT (value.enabled && value.hold && value.mix01 == 0);
    GF_EXPECT (value.lengthMs == 250 && value.preDelayMs == 0 && value.dampHz == 20000 && value.decay01 == 1);
    GF_EXPECT (value.source == Source::LastHit && value.stereo == Stereo::Stereo);
}
#if defined (GF_CONTEXT_STANDALONE)
int main()
{
    int failures = 0;
    for (const auto& [name, fn] : contexttest::cases())
        try { fn(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
    return failures != 0 ? 1 : 0;
}
#endif
