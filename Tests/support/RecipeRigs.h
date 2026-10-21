#pragma once

// Shared rigs for the WP4 recipe feature tests (audition, lock, like, history, export, A/B).
//
//   EngineRig  FillEngine + AnalysisEngine + SourceHistory driven the way the processor does it
//              (40 ms lookahead), 512 samples per step. Fine-grained engine assertions.
//   ProcRun    GapFillAudioProcessor driven block by block with commands and MIDI. End-to-end.

#include "TestRig.h"

#include "dsp/FillEngine.h"
#include "features/audition/PreviewChannel.h"

#include <functional>
#include <vector>

namespace recipetest
{
using namespace gapfill;
using namespace gftest;

    inline constexpr double kSr = 48000.0;
    inline constexpr int kBlock = 512;

    inline AnalysisSettings analysisDefaults()
    {
        AnalysisSettings s;
        s.mode = DetMode::Transient;
        s.sensitivity01 = 0.55f;
        s.gapThresh01 = 0.55f;
        s.protectHoldMs = 30.0f;
        s.retrigGuardMs = 30.0f;
        s.gapReleaseMs = 50.0f;
        return s;
    }

    /** Engine + analysis + history driven exactly the way the processor does it
        (40 ms lookahead), 512 samples at a time. */
    struct EngineRig
    {
        AnalysisEngine analysis;
        SourceHistory history;
        FillEngine engine;
        EngineSettings settings;
        AnalysisSettings as = analysisDefaults();
        FillEventQueue events;
        juce::AudioBuffer<float> wet { 2, kBlock };
        std::int64_t processed = 0;
        std::int64_t lastOutStart = 0;
        double sr = kSr;

        explicit EngineRig (double sampleRate = kSr)
            : sr (sampleRate)
        {
            analysis.prepare (sr, kBlock);
            history.prepare (2, (int) (sr * 6.0));
            engine.prepare (sr, kBlock, 2);

            settings.fill01 = 0.0f;                  // no natural fills unless a test wants them
            settings.maxFillMs = 400.0f;
            settings.lookaheadMs = 40.0f;

            for (auto& w : settings.weights)
                w = 0.7f;
        }

        /** Feeds block `index` of `input` and renders the wet bus (copy returned). */
        const juce::AudioBuffer<float>& step (const juce::AudioBuffer<float>& input, int index)
        {
            const auto pos = index * kBlock;
            juce::AudioBuffer<float> block (2, kBlock);
            block.clear();

            const auto n = juce::jlimit (0, kBlock, input.getNumSamples() - pos);

            for (int ch = 0; ch < 2; ++ch)
                if (n > 0)
                    block.copyFrom (ch, 0, input, ch, pos, n);

            std::vector<float> mono ((size_t) kBlock);

            for (int i = 0; i < kBlock; ++i)
                mono[(size_t) i] = 0.5f * (block.getSample (0, i) + block.getSample (1, i));

            history.write (block, 0, kBlock);
            analysis.processBlock (mono.data(), nullptr, kBlock, as);

            const auto lookahead = (std::int64_t) (settings.lookaheadMs * 0.001 * sr);
            const auto outStart = processed - lookahead;
            lastOutStart = outStart;
            wet.clear();

            if (outStart >= 0)
            {
                analysis.discardEventsBefore (outStart);
                engine.process (wet, 0, kBlock, outStart, analysis, history, settings, nullptr, &events);
            }

            processed += kBlock;
            return wet;
        }

        float wetEnergy() const { return regionEnergy (wet, 0, kBlock); }
    };

    inline int countEvents (FillEventQueue& q, FillEventType type)
    {
        FillEvent e[16];
        int n = 0;

        for (;;)
        {
            const auto got = q.pop (e, 16);

            if (got <= 0)
                break;

            for (int i = 0; i < got; ++i)
                if (e[i].type == type)
                    ++n;
        }

        return n;
    }

    /** A processor driven block by block with an optional MIDI buffer. */
    struct ProcRun
    {
        GapFillAudioProcessor proc;
        TestPlayHead playHead;
        juce::AudioBuffer<float> work { 2, kBlock };
        juce::MidiBuffer midi;
        int blockIndex = 0;

        explicit ProcRun (std::initializer_list<std::pair<const char*, float>> params, double sr = kSr)
        {
            setParams (proc, params);
            prepareProcessor (proc, sr, kBlock, false);
            playHead.bpm = 150.0;
            proc.setPlayHead (&playHead);
        }

        ~ProcRun() { proc.setPlayHead (nullptr); }

        /** Processes the next block of `in`; the output is left in `work`. */
        const juce::AudioBuffer<float>& step (const juce::AudioBuffer<float>& in)
        {
            const auto pos = blockIndex * kBlock;
            work.clear();
            const auto n = juce::jlimit (0, kBlock, in.getNumSamples() - pos);

            for (int ch = 0; ch < 2; ++ch)
                if (n > 0)
                    work.copyFrom (ch, 0, in, ch, pos, n);

            proc.processBlock (work, midi);
            midi.clear();
            playHead.advance (kBlock, kSr);
            ++blockIndex;
            return work;
        }

        void post (CommandType type, std::uint8_t velocity = 100)
        {
            Command c;
            c.type = type;
            c.velocity = velocity;
            GF_EXPECT (proc.postCommand (c));
        }

        PreviewStatus status()
        {
            return previewStatusOf ((std::uint32_t) proc.getMeterState().previewStatus.load());
        }
    };

    /** Pops everything currently queued. */
    inline std::vector<FillEvent> drainEvents (FillEventQueue& q)
    {
        std::vector<FillEvent> all;
        FillEvent batch[16];

        for (;;)
        {
            const auto n = q.pop (batch, 16);

            if (n <= 0)
                break;

            all.insert (all.end(), batch, batch + n);
        }

        return all;
    }

    /** Runs `blocks` steps of `input`, collecting the Started recipes (in order) of natural fills. */
    inline std::vector<FillRecipe> runCollectingFills (EngineRig& rig, const juce::AudioBuffer<float>& input,
                                                       int blocks, std::vector<FillEvent>* allEvents = nullptr)
    {
        std::vector<FillRecipe> started;

        for (int i = 0; i < blocks; ++i)
        {
            rig.step (input, i);

            for (const auto& e : drainEvents (rig.events))
            {
                if (e.type == FillEventType::Started)
                    started.push_back (e.recipe);

                if (allEvents != nullptr)
                    allEvents->push_back (e);
            }
        }

        return started;
    }

    /** Settings that make a drum loop fill nearly every gap. */
    inline void makeBusy (EngineSettings& s)
    {
        s.fill01 = 1.0f;
        s.density01 = 1.0f;
        s.complexity01 = 0.5f;
    }

    inline juce::AudioBuffer<float> steadyNoise (double seconds = 4.0)
    {
        return makeNoise (kSr, seconds, 0.3f, 2);
    }

} // namespace recipetest
