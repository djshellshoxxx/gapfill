#include "GrooveState.h"

#include <algorithm>
#include <cmath>

namespace gapfill::groove
{

void GrooveState::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    reset();
}

void GrooveState::reset() noexcept
{
    // A held / learned groove survives lifecycle resets; everything else restarts.
    if (! heldValid)
        trk.reset();

    havePrevPpq = false;
    scannedValid = false;
    learning = false;
    learnRemaining.store (0, std::memory_order_relaxed);
}

void GrooveState::onTransportJump() noexcept
{
    havePrevPpq = false;
    scannedValid = false;

    if (! heldValid)
        trk.onTransportJump();
}

void GrooveState::onCommand (const Command& c) noexcept
{
    if (c.type == CommandType::GrooveReset)
    {
        resetRequested.store (true, std::memory_order_release);
    }
    else if (c.type == CommandType::GrooveLearn)
    {
        trk.reset();
        heldValid = false;
        latched.store (false, std::memory_order_relaxed);
        learning = true;
        learnBeatsLeft = -1.0;              // set from the time signature on the next chunk
    }
}

void GrooveState::requestLoad (const GrooveSnapshot& s, bool latchedByLearn) noexcept
{
    for (int i = 0; i < kSlots; ++i)
        loadOffsets[i].store (s.offsetSteps[i], std::memory_order_relaxed);

    loadLatched.store (latchedByLearn, std::memory_order_relaxed);
    loadPending.store (true, std::memory_order_release);
}

void GrooveState::publish (const GrooveSnapshot& s) noexcept
{
    seq.fetch_add (1, std::memory_order_acq_rel);                  // odd: writing

    for (int i = 0; i < kSlots; ++i)
        pubOffsets[i].store (s.offsetSteps[i], std::memory_order_relaxed);

    pubConfidence.store (s.confidence, std::memory_order_relaxed);
    pubSwing.store (s.swingPercent, std::memory_order_relaxed);
    pubValid.store (s.valid, std::memory_order_relaxed);
    seq.fetch_add (1, std::memory_order_release);                  // even: stable
}

bool GrooveState::readSnapshot (GrooveSnapshot& out) const noexcept
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto a = seq.load (std::memory_order_acquire);

        if ((a & 1u) != 0)
            continue;

        for (int i = 0; i < kSlots; ++i)
            out.offsetSteps[i] = pubOffsets[i].load (std::memory_order_relaxed);

        out.confidence = pubConfidence.load (std::memory_order_relaxed);
        out.swingPercent = pubSwing.load (std::memory_order_relaxed);
        out.valid = pubValid.load (std::memory_order_relaxed);

        if (seq.load (std::memory_order_acquire) == a)
            return true;
    }

    return false;
}

void GrooveState::onChunk (const AnalysisEngine& analysis, std::int64_t chunkInputStart, int numSamples,
                           EngineSettings& s) noexcept
{
    auto& g = s.perf.groove;
    g.snap = {};

    // A different grid makes everything learned on the old one meaningless.
    if ((g.grid == 0 ? 0 : 1) != trk.grid())
    {
        trk.setGrid (g.grid);
        heldValid = false;
        latched.store (false, std::memory_order_relaxed);
    }

    // ---- requests from other threads -------------------------------------------
    if (resetRequested.exchange (false, std::memory_order_acq_rel))
    {
        trk.reset();
        heldValid = false;
        latched.store (false, std::memory_order_relaxed);
        learning = false;
        learnRemaining.store (0, std::memory_order_relaxed);
    }

    if (loadPending.exchange (false, std::memory_order_acquire))
    {
        GrooveSnapshot loaded;

        for (int i = 0; i < kSlots; ++i)
            loaded.offsetSteps[i] = loadOffsets[i].load (std::memory_order_relaxed);

        trk.load (loaded);
        heldValid = true;
        latched.store (loadLatched.load (std::memory_order_relaxed), std::memory_order_relaxed);
    }

    const bool frozen = g.hold || heldValid;
    const bool measuring = (g.amount01 > 0.0f || learning) && ! frozen;

    // Nothing to do (and no per-event work) while the feature is off.
    if (g.amount01 <= 0.0f && ! learning && ! heldValid)
        return;

    const bool havePpq = s.transportValid && s.samplesPerBeat > 1.0;
    waiting.store (! havePpq, std::memory_order_relaxed);

    if (! havePpq)
    {
        havePrevPpq = false;
        scannedValid = false;
        return;                                      // measurement pauses, offsets are 0
    }

    const double ppq = s.ppqAtBlockStart;
    const double chunkBeats = (double) numSamples / s.samplesPerBeat;

    // Jump detected from the PPQ itself as well (tempo-ramp stable: 0.25 beat tolerance).
    if (havePrevPpq && std::abs (ppq - expectedPpq) > 0.25)
    {
        scannedValid = false;

        if (! frozen)
            trk.onTransportJump();
    }

    havePrevPpq = true;
    expectedPpq = ppq + chunkBeats;
    trk.tick (ppq, true);

    if (measuring)
    {
        const auto chunkEnd = chunkInputStart + numSamples;
        auto from = scannedValid ? std::max (lastScanned, chunkInputStart - 2 * (std::int64_t) sr / 10) : chunkInputStart;

        for (auto pos = analysis.nextDetectionEvent (from); pos >= 0 && pos < chunkEnd; pos = analysis.nextDetectionEvent (pos + 1))
        {
            const auto eventPpq = ppq + (double) (pos - chunkInputStart) / s.samplesPerBeat;
            trk.onEvent (eventPpq, analysis.frameAt (pos).transient);
        }

        lastScanned = chunkEnd;
        scannedValid = true;
    }
    else
    {
        scannedValid = false;
    }

    // ---- learn countdown (8 bars of the host time signature) -------------------
    if (learning)
    {
        const double beatsPerBar = s.perf.tsNum > 0 && s.perf.tsDen > 0 ? 4.0 * (double) s.perf.tsNum / (double) s.perf.tsDen : 4.0;

        if (learnBeatsLeft < 0.0)
            learnBeatsLeft = (double) kLearnBars * beatsPerBar;

        learnBeatsLeft -= chunkBeats;
        learnRemaining.store ((int) std::ceil (std::max (0.0, learnBeatsLeft) / beatsPerBar), std::memory_order_relaxed);

        if (learnBeatsLeft <= 0.0)
        {
            learning = false;
            heldSnap = trk.snapshot();

            if (heldSnap.valid)
            {
                trk.load (heldSnap);
                heldValid = true;
                latched.store (true, std::memory_order_relaxed);
            }
        }
    }

    auto snap = trk.snapshot();
    publish (snap);

    // Measure-only while learning: no offsets are applied until the groove is frozen.
    g.snap = learning ? GrooveSnapshot {} : snap;
}

} // namespace gapfill::groove
