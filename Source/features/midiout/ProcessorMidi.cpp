#include "../../PluginProcessor.h"

namespace gapfill
{
midiout::MidiOutSettings GapFillAudioProcessor::gatherExtendedMidiOutSettings() const noexcept
{
    midiout::MidiOutSettings s;
    s.on = pb (params.midiExtOn);
    s.channel = pi (params.midiExtChan);
    s.hitOn = pb (params.midiExtHitOn);
    s.gapOn = pb (params.midiExtGapOn);
    s.fillOn = pb (params.midiExtFillOn);
    s.noteHit = pi (params.midiExtNoteHit);
    s.noteGap = pi (params.midiExtNoteGap);
    s.noteFill = pi (params.midiExtNoteFill);
    s.velFollow = pi (params.midiExtVel) == 0;
    s.hitLenMs = pv (params.midiExtLengthMs);
    s.cc = pi (params.midiExtCC);
    s.early = pi (params.midiExtTiming) != 0;
    s.offsetSamples = (int) std::lround (getMidiOutOffsetMs() * 0.001 * currentSampleRate);
    return s;
}

void GapFillAudioProcessor::beginMidiOutput (juce::MidiBuffer& midi, int n, bool bypassed) noexcept
{
    auto extended = gatherExtendedMidiOutSettings();
    extendedMidiOwner = extended.on;
    const bool stopped = hostTiming.havePosition && ! hostTiming.playing;
    if (bypassed || liveControl.panic().isPanicked() || stopped)
    {
        extended.on = false;
        extendedMidiOut.requestFlush();
        midiOut.requestFlush();
    }

    // Input learn/commands have consumed this buffer. Exactly one owner writes
    // new notes; the previous owner's flush is emitted first at offset zero.
    auto legacy = gatherMidiOutSettings();
    if (extendedMidiOwner || legacy.mode != midiout::ModeOff || midiOut.hasSoundingNotes()
        || extendedMidiOut.hasSoundingNotes())
        midi.clear();
    if (extendedMidiOwner)
    {
        legacy.mode = midiout::ModeOff;
        midiOut.beginBlock (legacy, n);
        const auto previousOffs = midiOut.finish (midi, false);
        extendedMidiOut.beginBlock (extended, samplesProcessed - targetLatency, n, samplesProcessed,
                                   midiout::kSchedulerMaxEvents - previousOffs);
    }
    else
    {
        extendedMidiOut.beginBlock (extended, samplesProcessed - targetLatency, n, samplesProcessed);
        extendedMidiOut.finish (midi, false);
        if (bypassed || liveControl.panic().isPanicked() || stopped)
            legacy.mode = midiout::ModeOff;
        midiOut.beginBlock (legacy, n);
    }
}

void GapFillAudioProcessor::finishMidiOutput (juce::MidiBuffer& midi) noexcept
{
    if (extendedMidiOwner)
        extendedMidiOut.finish (midi, false);
    else
        midiOut.finish (midi, false);
}

void GapFillAudioProcessor::collectEarlyMidiHits (std::int64_t start, int n) noexcept
{
    if (! extendedMidiOwner || ! pb (params.midiExtHitOn) || pi (params.midiExtTiming) == 0)
        return;
    earlyMidiEvents.clear();
    const auto end = start + n;
    // Analysis labels the START of a completed hop. Detection-early output
    // belongs to the causal completion sample, including callbacks < hopSize.
    constexpr auto hop = AnalysisEngine::hopSize;
    const auto first = juce::jmax ((std::int64_t) 0, start - hop + 1);
    for (auto pos = analysis.nextDetectionEvent (first); pos >= first && pos + hop - 1 < end;)
    {
        if (pos + hop - 1 >= start)
            earlyMidiEvents.push ({ midiout::OutEvent::Hit, pos + hop - 1, analysis.frameAt (pos).transient });
        if (earlyMidiEvents.count == midiout::EngineEventBuffer::capacity)
            break;
        pos = analysis.nextDetectionEvent (pos + 1);
    }
    extendedMidiOut.consumeEarlyHits (earlyMidiEvents);
}
} // namespace gapfill
