#pragma once

namespace gapfill::battlev2
{
/** Host values for the explicit detailed model. Legacy battleEvery and its
    runtime are separate; these neutral defaults never activate either path. */
struct Settings
{
    bool on = false;
    int unitBars = 2;
    int first = 0;
};
class Runtime;
} // namespace gapfill::battlev2
