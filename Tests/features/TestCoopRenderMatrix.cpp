#include "../support/TestRig.h"
#include "features/coop/Deterministic.h"
#include <array>
#include <barrier>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

using namespace gapfill;
using namespace gftest;
namespace
{
    constexpr int tracks = 4, bars = 64, samplesPerBeat = 24000, samplesPerBar = 4 * samplesPerBeat;
    constexpr int totalSamples = bars * samplesPerBar;
    constexpr double rate = 48000;
    struct Start
    {
        std::int64_t sample = 0;
        int length = 0;
        bool operator== (const Start&) const noexcept = default;
    };
    struct TrackResult
    {
        std::vector<float> pcm; // only the reference retains PCM (196.6MB for all four tracks)
        std::vector<Start> starts;
        std::uint64_t digest = 1469598103934665603ULL;
        std::int64_t firstDifferentSample = -1;
        int fills = 0, wrongOwners = 0, droppedTransitions = 0;
        double energy = 0;
        bool finite = true;
        std::exception_ptr exception;
    };
    struct MatrixResult
    {
        std::array<TrackResult, tracks> track;
        std::array<std::uint8_t, bars> barStarts {};
    };

    MatrixResult renderMatrix (int blockSize, int threadCount, bool shuffled,
                               const MatrixResult* reference = nullptr)
    {
        MatrixResult result;
        std::array<std::unique_ptr<GapFillAudioProcessor>, tracks> processors;
        std::array<TestPlayHead, tracks> heads;
        std::array<juce::AudioBuffer<float>, tracks> work;
        std::array<juce::MidiBuffer, tracks> midi;
        std::array<int, tracks> latency {}, order { 0, 1, 2, 3 };
        const auto input = drumLoop (rate, 120, 1);
        coop::deterministic::Snapshot expectedRoster;
        expectedRoster.generation = 1; expectedRoster.group = 8; expectedRoster.count = tracks;

        // All processors join during prepare, before ANY callback. No editor,
        // synthetic CoopView, forced fill, or manual beginDeterministicChunk call.
        for (int i = 0; i < tracks; ++i)
        {
            const auto index = (std::size_t) i;
            processors[index] = std::make_unique<GapFillAudioProcessor>();
            auto& processor = *processors[index];
            setParams (processor, { { "coopDeterministic", 1 }, { "coopDetGroup", 8 }, { "coopSlot", (float) (i + 1) },
                { "coopPriority", 50 }, { "coopYield", 100 }, { "coopGrid", 2 },
                { "seed", (float) (410 + i) }, { "fill", 100 }, { "density", 100 }, { "intensity", 65 },
                { "complexity", 0 }, { "chaos", 0 }, { "fillType", (float) ((int) Algo::Stutter + 1) },
                { "minGap", 20 }, { "maxFill", 140 }, { "gapRelease", 20 }, { "lookahead", 40 },
                { "protectHold", 0 }, { "qualityMode", 0 }, { "wetSolo", 1 } });
            processor.setNonRealtime (true);
            prepareProcessor (processor, rate, blockSize, false);
            heads[index].bpm = 120; heads[index].tsNum = heads[index].tsDen = 4;
            processor.setPlayHead (&heads[index]);
            latency[index] = processor.getLatencySamples();
            work[index].setSize (2, blockSize);
            midi[index].ensureSize (2048);
            result.track[index].starts.reserve (1024);
            if (reference == nullptr) result.track[index].pcm.resize ((std::size_t) totalSamples * 2);
            expectedRoster.members[index] = { (std::uint64_t) (i + 1), (std::uint8_t) (i + 1), 50 };
        }

        int first = 0, count = 0;
        const auto processOne = [&] (int member) noexcept
        {
            const auto index = (std::size_t) member;
            auto& observed = result.track[index];
            if (observed.exception != nullptr) return;
            try
            {
                juce::AudioBuffer<float> slice (work[index].getArrayOfWritePointers(), 2, count);
                processors[index]->processBlock (slice, midi[index]);
                // Observations and vector growth happen AFTER the actual audio callback.
                const auto& transitions = processors[index]->getFillEngine().getTransitions();
                observed.droppedTransitions += transitions.dropped;
                for (int j = 0; j < transitions.count; ++j)
                    if (transitions.items[j].type == Transition::FillStart)
                    {
                        const auto& start = transitions.items[j];
                        observed.starts.push_back ({ start.pos, start.lengthSamples });
                        // Engine decisions are on the latency-compensated output
                        // timeline; updateTempoInfo anchors PPQ at callback input.
                        const auto musicalSample = start.pos + latency[index];
                        const auto bar = musicalSample / samplesPerBar;
                        if (musicalSample < 0 || bar < 0 || bar >= bars
                            || coop::deterministic::ownerSlot (expectedRoster, bar) != member + 1)
                            ++observed.wrongOwners;
                    }
                for (int sample = 0; sample < count; ++sample)
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        const float value = work[index].getSample (channel, sample);
                        const auto position = (std::size_t) (first + sample) * 2 + (std::size_t) channel;
                        const auto bits = std::bit_cast<std::uint32_t> (value);
                        observed.digest = (observed.digest ^ bits) * 1099511628211ULL;
                        observed.finite = observed.finite && std::isfinite (value);
                        observed.energy += (double) value * value;
                        if (reference == nullptr) observed.pcm[position] = value;
                        else if (observed.firstDifferentSample < 0
                            && bits != std::bit_cast<std::uint32_t> (reference->track[index].pcm[position]))
                            observed.firstDifferentSample = first + sample;
                    }
            }
            catch (...) { observed.exception = std::current_exception(); }
        };

        // Harness barriers are OUTSIDE processBlock. Four workers each process
        // one distinct processor per callback; no processor has concurrent calls.
        std::barrier phase (tracks + 1);
        bool stop = false;
        std::array<std::thread, tracks> workers;
        if (threadCount == 4)
            for (int rank = 0; rank < tracks; ++rank)
                workers[(std::size_t) rank] = std::thread ([&, rank]
                {
                    for (;;)
                    {
                        phase.arrive_and_wait();
                        if (stop) return;
                        processOne (order[(std::size_t) rank]);
                        phase.arrive_and_wait();
                    }
                });

        std::uint32_t random = 912731;
        for (first = 0; first < totalSamples; first += count)
        {
            count = std::min (blockSize, totalSamples - first);
            if (shuffled)
                for (int i = tracks - 1; i > 0; --i)
                {
                    random = random * 1664525u + 1013904223u;
                    std::swap (order[(std::size_t) i], order[(std::size_t) (random % (std::uint32_t) (i + 1))]);
                }
            for (int i = 0; i < tracks; ++i)
            {
                const auto index = (std::size_t) i;
                // Exact absolute PPQ avoids a host-side accumulation oracle error.
                heads[index].ppq = (double) first / samplesPerBeat;
                heads[index].timeInSamples = first;
                midi[index].clear();
                for (int sample = 0; sample < count; ++sample)
                    for (int channel = 0; channel < 2; ++channel)
                        work[index].setSample (channel, sample, input.getSample (channel, (first + sample) % input.getNumSamples()));
            }
            if (threadCount == 4) { phase.arrive_and_wait(); phase.arrive_and_wait(); }
            else for (int member : order) processOne (member);
        }
        if (threadCount == 4)
        {
            stop = true;
            phase.arrive_and_wait();
            for (auto& worker : workers) worker.join();
        }
        for (int i = 0; i < tracks; ++i)
        {
            const auto index = (std::size_t) i;
            auto& observed = result.track[index];
            observed.fills = processors[index]->getFillEngine().getFillsStarted();
            for (const auto& start : observed.starts)
            {
                const auto bar = (start.sample + latency[index]) / samplesPerBar;
                if (bar >= 0 && bar < bars)
                    result.barStarts[(std::size_t) bar] |= (std::uint8_t) (1u << i);
            }
            processors[index]->setPlayHead (nullptr);
            processors[index]->releaseResources();
        }
        return result;
    }

    void validateOwners (const MatrixResult& result)
    {
        int usedBars = 0;
        for (auto mask : result.barStarts)
        {
            GF_EXPECT (std::popcount (mask) <= 1);
            usedBars += mask != 0 ? 1 : 0;
        }
        GF_EXPECT_MSG (usedBars == bars, "All64 owner-eligible bars must contain actual natural fills; got" + juce::String (usedBars));
        for (int i = 0; i < tracks; ++i)
        {
            const auto& observed = result.track[(std::size_t) i];
            if (observed.exception != nullptr) std::rethrow_exception (observed.exception);
            GF_EXPECT_MSG (observed.fills > 0 && observed.fills == (int) observed.starts.size(), "No real fill transition evidence on track" + juce::String (i + 1));
            GF_EXPECT_MSG (observed.wrongOwners == 0, "Natural fill started in another track's bar on track" + juce::String (i + 1));
            GF_EXPECT (observed.finite && observed.energy > 0 && observed.droppedTransitions == 0);
        }
    }
}

GF_TEST (CoopRenderMatrix, four_real_processors_match_every_pcm_bit_for_order_threads_and_block_size_over_64_bars)
{
    const auto reference = renderMatrix (64, 1, false);
    validateOwners (reference);
    for (const auto config : { std::array<int, 2> { 64, 1 }, { 64, 4 }, { 512, 1 }, { 512, 4 } })
    {
        const auto result = renderMatrix (config[0], config[1], true, &reference);
        validateOwners (result);
        for (int i = 0; i < tracks; ++i)
        {
            const auto index = (std::size_t) i;
            const auto context = "block" + juce::String (config[0]) + " threads" + juce::String (config[1]) + " track" + juce::String (i + 1);
            GF_EXPECT_MSG (result.track[index].firstDifferentSample == -1,
                context + " first PCM mismatch at sample" + juce::String (result.track[index].firstDifferentSample));
            GF_EXPECT_MSG (result.track[index].starts == reference.track[index].starts, context + " fill starts or planned lengths changed");
            GF_EXPECT (result.track[index].digest == reference.track[index].digest);
        }
    }
}
