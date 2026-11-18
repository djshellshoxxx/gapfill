#include "../../PluginProcessor.h"

namespace gapfill
{

void GapFillAudioProcessor::beginHostCallbackTiming()
{
    hostTiming = {};
    auto& bpm = hostTiming.bpm;
    auto& tempoValid = hostTiming.tempoValid;
    auto& playing = hostTiming.playing;
    auto& ppq = hostTiming.ppq;
    auto& ppqValid = hostTiming.ppqValid;

    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            hostTiming.havePosition = true;
            if (const auto b = pos->getBpm())
            {
                if (*b > 1.0 && *b < 1000.0)
                {
                    bpm = *b;
                    tempoValid = true;
                }
            }

            if (const auto p = pos->getPpqPosition())
            {
                ppq = *p;
                ppqValid = std::isfinite (ppq);
            }

            playing = pos->getIsPlaying();

            if (const auto ts = pos->getTimeSignature())
            {
                hostTiming.tsNum = ts->numerator;
                hostTiming.tsDen = ts->denominator;
            }
        }
    }

    // Compare this callback's position with the end predicted by the PREVIOUS
    // complete host callback. Using the current chunk length is wrong when a
    // DAW changes buffer sizes or processBlock splits oversized host buffers.
    {
        const auto kind = transportTracker.update ({ playing, ppqValid, ppq, bpm, hostBlockSamples });

        if (kind == transport::JumpKind::Start || kind == transport::JumpKind::Discontinuity)
        {
            startTimelineEpoch();
            engine.noteTransportJump();
            coopMember.noteTransportJump();
            contextState.onTransportJump();
            panicWet();
           #ifdef GAPFILL_UNIT_TESTS
            ++transportJumpCountForTests;
           #endif
        }
        else if (kind == transport::JumpKind::Stop)
        {
            coopMember.cancelGroupScenes();
            panicWet();
        }
    }

}

void GapFillAudioProcessor::updateTempoInfo (EngineSettings& s, int startSample, int /*numSamples*/)
{
    const auto bpm = hostTiming.bpm;
    const auto tempoValid = hostTiming.tempoValid;
    const auto samplesPerBeat = 60.0 / bpm * currentSampleRate;
    s.samplesPerBeat = samplesPerBeat;
    s.ppqAtBlockStart = hostTiming.ppq + (double) startSample / samplesPerBeat;
    s.transportValid = hostTiming.playing && hostTiming.ppqValid && tempoValid;
    s.perf.tsNum = hostTiming.tsNum;
    s.perf.tsDen = hostTiming.tsDen;

    // Tempo-synced lengths (E-2): the host tempo, else the last one it reported,
    // else the Options fallback (120 BPM by default). Out-of-range lengths are
    // clamped by halving/doubling the DIVISION, not by clipping milliseconds.
    const auto beatSeconds = 60.0 / shaping.tempo.resolve (tempoValid, bpm, startSample == 0 ? (int) hostBlockSamples : 0);
    bool syncClamped = false;

    if (pb (params.syncEnable))
    {
        const auto st = sync::clampToDivision ((float) (divisionInBeats (pi (params.stutterDiv)) * beatSeconds),
                                               0.008f, 0.5f, pi (params.stutterDiv));
        const auto dl = sync::clampToDivision ((float) (divisionInBeats (pi (params.delayDiv)) * beatSeconds),
                                               0.01f, 1.0f, pi (params.delayDiv));
        s.stutterSeconds = st.seconds;
        s.delaySeconds   = dl.seconds;
        syncClamped = st.clamped || dl.clamped;

        if (s.perf.modSync)
            s.perf.modSyncSeconds = s.stutterSeconds;
    }
    else
    {
        s.stutterSeconds = 0.0625f;
        s.delaySeconds   = 0.125f;
        s.perf.modSyncSeconds = 0.0f;
    }

    const bool maxFillSync = s.perf.sceneMaxFillSync >= 0 ? s.perf.sceneMaxFillSync != 0 : pb (params.maxFillSync);
    const auto maxFillDivision = s.perf.sceneMaxFillDivision >= 0 ? s.perf.sceneMaxFillDivision : pi (params.maxFillDiv);
    if (maxFillSync)
    {
        const auto mf = sync::clampToDivision ((float) (divisionInBeats (maxFillDivision) * beatSeconds),
                                               0.02f, 2.0f, maxFillDivision);
        s.maxFillMs = mf.seconds * 1000.0f;
        syncClamped = syncClamped || mf.clamped;
    }

    meters.syncClamped.store (syncClamped, std::memory_order_relaxed);
}

} // namespace gapfill
