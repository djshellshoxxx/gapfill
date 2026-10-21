#pragma once

// Runtime state shared between the audio thread, the message thread and the
// editor for the WP5 "shaping and decisions" tickets (E-2, C-0, C-6, C-8 ...).
// The processor owns exactly one instance (`GapFillAudioProcessor::getShaping()`),
// so the tickets add no members to the processor.
//
//   tempo        E-2   last known host tempo + fallback tempo (Options)
//   capture      C-10  Capture v2 latch (live tap, snapshots, Pending status)
//   effective    C-0   block-stage output of the macro-targetable controls,
//                      written once per block by the audio thread, read by the
//                      editor timer (relaxed atomics, values in parameter units)

#include "../../dsp/EngineSettings.h"
#include "../capture2/CaptureLatch.h"
#include "../sync/Sync.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace gapfill
{

/** Controls whose effective value may differ from the parameter (personality,
    macros, evolve). Order is the C-0 index; ids in effectiveControlId(). */
struct EffectiveCreative
{
    static constexpr int numControls = 20;   // fill, intensity, complexity, chaos, density + 15 weights

    std::array<std::atomic<float>, numControls> value {};
    std::atomic<bool> valid { false };

    /** Audio thread, once per block: stores the block-stage result (0..1 fractions). */
    void publish (const EngineSettings& s) noexcept;

    float get (int index) const noexcept
    {
        return value[(std::size_t) index].load (std::memory_order_relaxed);
    }
};

/** Parameter id of effective-control `index`, or nullptr. */
const char* effectiveControlId (int index) noexcept;

/** Index of a parameter id in EffectiveCreative, or -1 when it is not shown. */
int effectiveControlIndex (const char* parameterId) noexcept;

struct ShapingRuntime
{
    sync::TempoSource tempo;
    EffectiveCreative effective;
    CaptureLatch capture;               // C-10 (audio-thread owned; status atomics for the editor)
};

} // namespace gapfill
