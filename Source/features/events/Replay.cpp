#include "Replay.h"

#include "../../PluginProcessor.h"
#include "../explain/ExplainState.h"
#include "../reasons/Reasons.h"

namespace gapfill::events
{

namespace
{
    /** Transport state driven by Transport records; ppq advances with the audio. */
    class ReplayPlayHead : public juce::AudioPlayHead
    {
    public:
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;

            if (valid)
            {
                info.setBpm (bpm);
                info.setPpqPosition (ppq);
                info.setIsPlaying (true);

                if (tsNum > 0 && tsDen > 0)
                    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { tsNum, tsDen });
            }
            else
            {
                info.setIsPlaying (false);
            }

            info.setTimeInSamples (timeInSamples);
            return info;
        }

        void advance (int numSamples, double sampleRate)
        {
            timeInSamples += numSamples;

            if (valid && bpm > 0.0)
                ppq += (double) numSamples / (60.0 / bpm * sampleRate);
        }

        bool valid = false;
        double bpm = 120.0, ppq = 0.0;
        int tsNum = 0, tsDen = 0;
        juce::int64 timeInSamples = 0;
    };
}

std::vector<OutcomeKey> outcomesOf (const ReplayData& d)
{
    std::vector<OutcomeKey> out;

    for (const auto& r : d.records)
        if (r.type == (std::uint8_t) RecType::Outcome)
            out.push_back ({ (r.flags & 1) != 0, (std::uint8_t) (r.index & 0xFF), r.pos });

    return out;
}

int findFirstDivergence (const std::vector<OutcomeKey>& a, const std::vector<OutcomeKey>& b) noexcept
{
    const auto n = std::min (a.size(), b.size());

    for (size_t i = 0; i < n; ++i)
        if (! (a[i] == b[i]))
            return (int) i;

    return a.size() == b.size() ? -1 : (int) n;
}

ReplayReport replay (const ReplayData& d, const juce::AudioBuffer<float>& audio)
{
    ReplayReport report;

    if (audio.getNumSamples() <= 0 || audio.getNumChannels() <= 0)
    {
        report.error = "No audio was supplied";
        return report;
    }

    auto proc = std::make_unique<GapFillAudioProcessor>();

    if (const auto xml = juce::parseXML (d.initialState))
    {
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*xml, block);
        proc->setStateInformation (block.getData(), (int) block.getSize());
    }
    else
    {
        report.error = "The recording has no readable initial state";
        return report;
    }

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::disabled());
    proc->setBusesLayout (layout);
    proc->setRateAndBufferSizeDetails (d.sampleRate, d.preparedBlockSize);
    proc->prepareToPlay (d.sampleRate, d.preparedBlockSize);

    ReplayPlayHead head;
    proc->setPlayHead (&head);

    auto& recorder = proc->getExplainState().recorder;
    recorder.start (d.initialState, d.sampleRate, d.preparedBlockSize, d.pluginVersion, d.stateVersion);

    const auto& params = proc->getParameters();
    juce::AudioBuffer<float> work (2, juce::jmax (1, d.preparedBlockSize));
    juce::MidiBuffer midi;

    size_t next = 0;
    int blockSize = d.preparedBlockSize;
    int position = 0;

    for (int block = 0; block < d.numBlocks; ++block)
    {
        while (next < d.records.size() && (int) d.records[next].block == block)
        {
            const auto& r = d.records[next++];

            switch ((RecType) r.type)
            {
                case RecType::ParamChange:
                    if (r.index < params.size())
                        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (params[(int) r.index]))
                            p->setValueNotifyingHost (p->convertTo0to1 (bitsFloat (r.p0)));
                    break;

                case RecType::Command:
                {
                    Command c;
                    c.type = (CommandType) r.flags;
                    c.index = (std::int16_t) r.index;
                    c.velocity = (std::uint8_t) r.p0;
                    proc->postCommand (c);
                    break;
                }

                case RecType::Transport:
                    head.valid = (r.flags & 1) != 0;
                    head.bpm = (double) bitsFloat (r.p0);
                    head.ppq = (double) r.pos * 1.0e-6;
                    head.tsNum = (r.index >> 8) & 0xFF;
                    head.tsDen = r.index & 0xFF;
                    break;

                case RecType::BlockSize:
                    blockSize = juce::jlimit (1, d.preparedBlockSize, (int) r.p0);
                    break;

                case RecType::Outcome:
                case RecType::None:
                    break;
            }
        }

        if (position + blockSize > audio.getNumSamples())
            break;

        work.clear();

        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, audio, juce::jmin (ch, audio.getNumChannels() - 1), position, blockSize);

        juce::AudioBuffer<float> slice (work.getArrayOfWritePointers(), 2, blockSize);
        proc->processBlock (slice, midi);

        head.advance (blockSize, d.sampleRate);
        position += blockSize;
        ++report.blocksReplayed;
    }

    recorder.stop();
    proc->setPlayHead (nullptr);
    report.outcomes = outcomesOf (recorder.snapshot());
    report.ran = true;

    // Compare only what the replay could reach (a short supplied audio stops it early).
    int limitBlock = report.blocksReplayed;
    std::vector<OutcomeKey> expectedReached;

    for (const auto& r : d.records)
        if (r.type == (std::uint8_t) RecType::Outcome && (int) r.block < limitBlock)
            expectedReached.push_back ({ (r.flags & 1) != 0, (std::uint8_t) (r.index & 0xFF), r.pos });

    report.firstDivergence = findFirstDivergence (expectedReached, report.outcomes);

    if (report.firstDivergence >= 0)
    {
        const auto i = (size_t) report.firstDivergence;
        auto text = [] (const std::vector<OutcomeKey>& v, size_t at) -> juce::String
        {
            if (at >= v.size())
                return "(no further gap)";

            return juce::String (v[at].started ? "filled" : "skipped: " + reasons::label ((RejectReason) v[at].reason))
                   + " at sample " + juce::String (v[at].pos);
        };

        report.divergenceText = "Gap " + juce::String (report.firstDivergence + 1) + ": recorded " + text (expectedReached, i)
                                + ", replay " + text (report.outcomes, i);
    }
    return report;
}

} // namespace gapfill::events
