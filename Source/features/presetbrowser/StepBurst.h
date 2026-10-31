#pragma once

// F-S11 preset-step Undo coalescing (extracted from GapFillAudioProcessorEditor::stepPreset, behaviour unchanged).
//
// Contract: pure state, no JUCE types beyond the integer widths, no clock of its own (callers pass the millisecond
// counter), no processor reference. Message thread only.
//
// A "burst" is a run of preset steps where each step starts within kWindowMs of the previous one and nothing else
// pushed an Undo entry in between. The editor keeps only the snapshot taken before the first step of a burst, so
// browsing 20 presets does not push real work out of the 32-entry Undo stack.
//
//   isContinuation (now, pushCount)  true when a step at `now` continues the burst (call before the step loads);
//   record (now, pushCount)          call after the step loaded, with the Undo push count then;
//   forget()                         the next step starts a new burst.
//
// Invariants: time is compared as unsigned 32-bit differences, so a wrapped millisecond counter still works;
// `now` of 0 is stored as 1 because 0 means "no previous step".

#include <cstdint>

namespace gapfill::presets
{

class StepBurst
{
public:
    static constexpr std::uint32_t kWindowMs = 3000;

    bool isContinuation (std::uint32_t nowMs, unsigned undoPushCount) const noexcept
    {
        return lastStepMs != 0 && nowMs - lastStepMs <= kWindowMs && undoPushCount == pushCountAfterLastStep;
    }

    void record (std::uint32_t nowMs, unsigned undoPushCount) noexcept
    {
        lastStepMs = nowMs == 0 ? 1u : nowMs;
        pushCountAfterLastStep = undoPushCount;
    }

    void forget() noexcept
    {
        lastStepMs = 0;
        pushCountAfterLastStep = 0;
    }

private:
    std::uint32_t lastStepMs = 0;
    unsigned pushCountAfterLastStep = 0;
};

} // namespace gapfill::presets
