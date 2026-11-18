#pragma once
#include "ControllerTypes.h"

namespace gapfill::controller
{
/** Message-thread pickup state. A non-MIDI parameter change re-arms pickup;
    a crossing or a distance <=2/127 accepts the controller. */
class Takeover
{
public:
    void reset() noexcept;
    bool accepts (TakeoverMode, float input01, float current01) noexcept;
    void applied (float actual01) noexcept;
    bool isWaiting() const noexcept { return waiting; }
private:
    float previousInput = 0.0f;
    float lastApplied = 0.0f;
    bool havePrevious = false;
    bool haveApplied = false;
    bool waiting = true;
};
}
