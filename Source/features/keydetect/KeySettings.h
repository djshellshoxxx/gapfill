#pragma once

// F-X03 settings as the scheduler / context layer sees them (light header, no JUCE).

namespace gapfill::keydetect
{

struct KeySettings
{
    bool  autoOn = false;           ///< scaleFollow
    float windowSeconds = 12.0f;    ///< fx03_window
    float minConfidence01 = 0.4f;   ///< fx03_minConf
    bool  hold = false;             ///< fx03_hold
};

} // namespace gapfill::keydetect
