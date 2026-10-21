// Integrated stress / fuzz QA of the whole plug-in (docs/audits/2026-10-21-qa-report.md, docs/dev/07 §QA).
//
// Everything on at once, driven by a seeded generator so a failure is reproducible from its printed
// scenario line:
//   * every parameter fuzzed (single tweaks, storms, "everything on"), automation between blocks,
//   * commands, MIDI (notes / CC / program change), transport start / stop / loop / seek / tempo,
//   * bypass toggling, undo / random / surprise / reset spam, preset load storms, panic spam,
//   * state save / load in the middle of playback,
//   * sample rates 22.05 k .. 192 k, block sizes 1 .. 8192 (fixed and random), mono / stereo /
//     sidechain / Wet bus layouts.
// Invariants on every block: no allocation on the audio thread, finite output, no sub-normal output,
// bounded level (+6 dBFS with Wet Safety on, Output <= 0 dB and a full-scale input).
// The concurrent test additionally runs the audio callback on its own thread while the message
// thread opens / closes editors, loads state and fuzzes parameters (meaningful under TSan).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include <atomic>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <thread>

#if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }
#endif

using namespace gapfill;
using namespace gftest;

namespace
{
    struct QaRng
    {
        explicit QaRng (unsigned seed) : g (seed) {}
        std::mt19937 g;

        int  below (int n)          { return n <= 1 ? 0 : (int) (g() % (unsigned) n); }
        float unit()                { return (float) (g() & 0xFFFFFF) / (float) 0x1000000; }
        bool chance (float p)       { return unit() < p; }
        float extremeBiased()
        {
            const auto k = below (10);
            return k == 0 ? 0.0f : k == 1 ? 1.0f : unit();
        }
    };

    /** Runs the message queue (editor / processor timers) for about `ms` milliseconds. The tests are built with
        JUCE_MODAL_LOOPS_PERMITTED=0, so runDispatchLoopUntil is unavailable. */
    void pumpMessages (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;

        while (juce::Time::getMillisecondCounter() < end)
        {
           #if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
            if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
           #endif
                juce::Thread::sleep (1);
        }
    }

    /** Mostly small blocks (where per-block bookkeeping bites), sometimes up to `maxBlock`, log-uniform. */
    int randomBlockSize (QaRng& rng, int maxBlock)
    {
        if (rng.chance (0.4f))
            return 1 + rng.below (juce::jmin (maxBlock, 64));

        const auto lg = std::log2 ((double) maxBlock);
        return juce::jlimit (1, maxBlock, (int) std::exp2 (rng.unit() * lg));
    }

    struct Layout
    {
        bool mono = false, sidechain = false, wetBus = false;
        juce::String describe() const
        {
            return juce::String (mono ? "mono" : "stereo") + (sidechain ? "+sc" : "") + (wetBus ? "+wet" : "");
        }
    };

    struct Scenario
    {
        double sampleRate = 48000.0;
        int maxBlock = 512;
        int fixedBlock = 0;            // 0 = random sizes up to maxBlock
        Layout layout;
        unsigned seed = 1;
        int totalSamples = 48000 * 3;
        bool everythingOn = true;
        bool chaos = true;             // actions between blocks
        bool stateChurn = true;        // save/load state, preset storms, undo/random/surprise
    };

    struct Stats
    {
        long blocks = 0, countedBlocks = 0, allocBlocks = 0, allocs = 0;
        long nonFinite = 0, subnormal = 0, tooLoud = 0;
        float peak = 0.0f;
        juce::String firstProblem;

        void problem (const juce::String& what)
        {
            if (firstProblem.isEmpty())
                firstProblem = what;
        }
    };

    constexpr float kMaxAbs = 2.0f;      // +6 dBFS
    constexpr int kWarmupBlocks = 12;

    // All parameters once, minus the ones the driver owns.
    struct ParamSet
    {
        explicit ParamSet (juce::AudioProcessor& proc)
        {
            for (auto* p : proc.getParameters())
            {
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                {
                    const auto id = withId->paramID;

                    if (id == "bypass")       bypass = p;
                    else if (id == "output")  output = p;
                    else if (id == "wetSafety") wetSafety = p;
                    else
                    {
                        if (id == "bandSolo")
                            bandSolo = p;

                        all.push_back (p);
                    }
                }
            }
        }

        std::vector<juce::AudioProcessorParameter*> all;
        juce::AudioProcessorParameter *bypass = nullptr, *output = nullptr, *wetSafety = nullptr, *bandSolo = nullptr;
    };

    void setNormalised (juce::AudioProcessorParameter* p, float v01)
    {
        if (p != nullptr)
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v01));
    }

    void applyEverythingOn (ParamSet& ps, QaRng& rng)
    {
        for (auto* p : ps.all)
        {
            if (p->isBoolean())
                p->setValueNotifyingHost (1.0f);
            else if (p->isDiscrete())
                p->setValueNotifyingHost (rng.unit());
            else
                p->setValueNotifyingHost (0.4f + 0.6f * rng.unit());
        }
    }

    CommandType randomCommand (QaRng& rng)
    {
        return (CommandType) (1 + rng.below (32));
    }

    void addRandomMidi (juce::MidiBuffer& midi, QaRng& rng, int blockLen)
    {
        const auto count = 1 + rng.below(4);

        for (int i = 0; i < count; ++i)
        {
            const auto pos = rng.below (juce::jmax (1, blockLen));
            const auto ch = 1 + rng.below (16);

            switch (rng.below (7))
            {
                case 0: midi.addEvent (juce::MidiMessage::noteOn (ch, rng.below (128), (juce::uint8) (1 + rng.below (127))), pos); break;
                case 1: midi.addEvent (juce::MidiMessage::noteOff (ch, rng.below (128)), pos); break;
                case 2:
                case 3: midi.addEvent (juce::MidiMessage::controllerEvent (ch, rng.below (128), rng.below (128)), pos); break;
                case 4: midi.addEvent (juce::MidiMessage::programChange (ch, rng.below (128)), pos); break;
                case 5: midi.addEvent (juce::MidiMessage::pitchWheel (ch, rng.below (16384)), pos); break;
                default: midi.addEvent (juce::MidiMessage::allNotesOff (ch), pos); break;
            }
        }
    }

    /** Fills `work` (all bus channels) with source material that exercises gaps, hot signals,
        silence, DC and sub-normal input. */
    class SourceFeeder
    {
    public:
        SourceFeeder (double sr, QaRng& r) : rng (r)
        {
            loop = makeDrumLoop (sr, 4.0 * 4.0 * 60.0 / 150.0, 150.0, 2);   // 4 bars, gaps included
            side = makeDrumLoop (sr, 4.0 * 4.0 * 60.0 / 150.0, 150.0, 2);
        }

        void fill (juce::AudioBuffer<float>& work, int mainCh, int scCh, int n, bool torture)
        {
            const auto len = loop.getNumSamples();

            if (torture && n > 0 && pendingMode == 0 && rng.chance (0.02f))
            {
                pendingMode = 1 + rng.below (5);
                modeBlocks = 1 + rng.below (40);
            }

            for (int ch = 0; ch < mainCh; ++ch)
                for (int i = 0; i < n; ++i)
                    work.setSample (ch, i, sampleAt (loop, ch, (pos + i) % len));

            for (int ch = 0; ch < scCh; ++ch)
                for (int i = 0; i < n; ++i)
                    work.setSample (mainCh + ch, i, sampleAt (side, ch, (pos + len / 3 + i) % len));

            if (pendingMode != 0)
            {
                for (int ch = 0; ch < mainCh; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        float v = 0.0f;
                        switch (pendingMode)
                        {
                            case 1: v = 0.0f; break;                                          // silence (gap)
                            case 2: v = rng.unit() * 2.0f - 1.0f; break;                      // full-scale noise
                            case 3: v = 1.0f; break;                                          // DC at full scale
                            case 4: v = (rng.unit() - 0.5f) * 1e-30f; break;                  // tiny / near-denormal
                            default: v = (i & 1) ? 1.0f : -1.0f; break;                       // Nyquist square
                        }
                        work.setSample (ch, i, v);
                    }

                if (--modeBlocks <= 0)
                    pendingMode = 0;
            }

            pos = (pos + n) % len;
        }

    private:
        static float sampleAt (const juce::AudioBuffer<float>& b, int ch, int i)
        {
            return b.getSample (juce::jmin (ch, b.getNumChannels() - 1), i);
        }

        QaRng& rng;
        juce::AudioBuffer<float> loop, side;
        int pos = 0, pendingMode = 0, modeBlocks = 0;
    };

    void checkOutput (const juce::AudioBuffer<float>& work, int firstCh, int numCh, int n, bool levelBound, Stats& st,
                      const juce::String& where)
    {
        for (int ch = firstCh; ch < firstCh + numCh; ++ch)
        {
            const auto* d = work.getReadPointer (ch);

            for (int i = 0; i < n; ++i)
            {
                const float v = d[i];

                if (! std::isfinite (v))
                {
                    ++st.nonFinite;
                    st.problem ("non-finite output ch" + juce::String (ch) + " @" + juce::String (i) + " " + where);
                    return;
                }

                const auto a = std::abs (v);
                st.peak = juce::jmax (st.peak, a);

                if (a < FLT_MIN && a > 0.0f)
                {
                    ++st.subnormal;
                    st.problem ("sub-normal output " + where);
                }

                if (levelBound && a > kMaxAbs)
                {
                    ++st.tooLoud;
                    st.problem ("output " + juce::String (a) + " exceeds +6 dBFS " + where);
                }
            }
        }
    }

    /** Context printed with a level failure: the gains that decide the contract. */
    juce::String levelContext (GapFillAudioProcessor& proc)
    {
        auto& v = proc.getValueTreeState();
        auto raw = [&] (const char* id) { return v.getRawParameterValue (id)->load(); };
        return " [output=" + juce::String (raw ("output"), 2) + "dB dry=" + juce::String (raw ("dry"), 1) + " wet=" + juce::String (raw ("wet"), 1)
             + " wetSafety=" + juce::String (raw ("wetSafety"), 0) + " matchTrim=" + juce::String (proc.getRecipes().match.getTrimDb(), 2) + "dB bandSolo=" + juce::String (raw ("bandSolo"), 0) + " audition=" + juce::String (raw ("audition"), 0)
             + " wetSolo=" + juce::String (raw ("wetSolo"), 0) + " xoverLM=" + juce::String (raw ("xoverLM"), 0) + " xoverMH=" + juce::String (raw ("xoverMH"), 0) + "]";
    }

    Stats runScenario (const Scenario& sc)
    {
        Stats st;
        QaRng rng (sc.seed);
        const juce::String tag = "sr=" + juce::String (sc.sampleRate) + " blk=" + (sc.fixedBlock > 0 ? juce::String (sc.fixedBlock) : "rand<=" + juce::String (sc.maxBlock))
                               + " " + sc.layout.describe() + " seed=" + juce::String ((int) sc.seed);

        GapFillAudioProcessor proc;
        prepareProcessor (proc, sc.sampleRate, sc.maxBlock, sc.layout.sidechain, sc.layout.mono, sc.layout.wetBus);

        ParamSet ps (proc);
        const int mainCh = sc.layout.mono ? 1 : 2;
        const int scCh   = sc.layout.sidechain ? 2 : 0;
        const int wetCh  = sc.layout.wetBus ? 2 : 0;
        const int total  = juce::jmax (mainCh + scCh, mainCh + wetCh);

        // Level contract: Wet Safety on, Output <= 0 dB.
        setNormalised (ps.wetSafety, 1.0f);
        setNormalised (ps.output, 24.0f / 36.0f);

        if (sc.everythingOn)
            applyEverythingOn (ps, rng);

        TestPlayHead ph;
        ph.bpm = 150.0;
        proc.setPlayHead (&ph);

        SourceFeeder feeder (sc.sampleRate, rng);
        juce::AudioBuffer<float> work (total, sc.maxBlock);
        juce::MidiBuffer midi;
        midi.ensureSize (16384);
        bool hasPlayHead = true;
        int done = 0;

        while (done < sc.totalSamples)
        {
            const int n = sc.fixedBlock > 0 ? sc.fixedBlock : randomBlockSize (rng, sc.maxBlock);
            const bool afterWarmup = st.blocks >= kWarmupBlocks;
            midi.clear();

            if (sc.chaos)
            {
                // parameter automation
                if (rng.chance (0.35f))
                    for (int k = 1 + rng.below (6); k > 0; --k)
                        setNormalised (ps.all[(size_t) rng.below ((int) ps.all.size())], rng.extremeBiased());

                if (rng.chance (0.03f))
                    for (auto* p : ps.all)
                        if (rng.chance (0.4f))
                            p->setValueNotifyingHost (rng.extremeBiased());

                if (rng.chance (0.05f))
                    setNormalised (ps.output, rng.unit() * (24.0f / 36.0f));           // never above 0 dB

                if (rng.chance (0.002f))
                    setNormalised (ps.wetSafety, 1.0f);

                if (rng.chance (0.04f))
                {
                    Command c;
                    c.type = randomCommand (rng);
                    c.index = (std::int16_t) rng.below (40);
                    c.velocity = (std::uint8_t) rng.below (128);
                    proc.postCommand (c);
                }

                if (rng.chance (0.2f))
                    addRandomMidi (midi, rng, n);

                // transport: start / stop / loop / seek / tempo / no host info
                if (rng.chance (0.02f)) ph.playing = ! ph.playing;
                if (rng.chance (0.015f)) ph.ppq = juce::jmax (0.0, ph.ppq - (double) (1 + rng.below (16)));   // loop back
                if (rng.chance (0.01f)) ph.ppq = (double) rng.below (4000) * 0.25;                            // seek
                if (rng.chance (0.01f)) ph.bpm = 20.0 + (double) rng.below (281);
                if (rng.chance (0.005f)) { ph.tsNum = 1 + rng.below (13); ph.tsDen = 1 << rng.below (5); }
                if (rng.chance (0.005f)) { hasPlayHead = ! hasPlayHead; proc.setPlayHead (hasPlayHead ? &ph : nullptr); }

                // bypass
                if (ps.bypass != nullptr && rng.chance (0.02f))
                    ps.bypass->setValueNotifyingHost (ps.bypass->getValue() > 0.5f ? 0.0f : 1.0f);

                if (sc.stateChurn)
                {
                    if (rng.chance (0.004f)) proc.randomise (rng.chance (0.3f));
                    if (rng.chance (0.004f)) proc.surprise();
                    if (rng.chance (0.004f)) proc.undo();
                    if (rng.chance (0.002f)) proc.resetAllParameters();

                    if (rng.chance (0.006f))
                        proc.setCurrentProgram (rng.below (juce::jmax (1, proc.getNumPrograms())));

                    if (rng.chance (0.005f))
                    {
                        juce::MemoryBlock saved;
                        proc.getStateInformation (saved);
                        proc.setStateInformation (saved.getData(), (int) saved.getSize());
                    }

                    if (rng.chance (0.002f))
                        applyEverythingOn (ps, rng);
                }

                // Keep the level contract whatever the fuzz did: Randomise / presets / undo / state loads may move
                // Output above 0 dB or Wet Safety off, which is legitimate but outside the +6 dBFS contract.
                setNormalised (ps.wetSafety, 1.0f);

                if (ps.output != nullptr && ps.output->getValue() > 24.0f / 36.0f)
                    setNormalised (ps.output, 24.0f / 36.0f);
            }

            work.clear();
            feeder.fill (work, mainCh, scCh, n, sc.chaos);

            juce::AudioBuffer<float> slice (work.getArrayOfWritePointers(), total, n);
            const bool bypassed = ps.bypass != nullptr && ps.bypass->getValue() > 0.5f;
            const bool wetFed = sc.layout.wetBus;
            (void) wetFed;

            {
                AllocGuard guard;

                if (bypassed)
                    proc.processBlockBypassed (slice, midi);
                else
                    proc.processBlock (slice, midi);

                if (afterWarmup)
                {
                    ++st.countedBlocks;

                    if (guard.count() != 0)
                    {
                        ++st.allocBlocks;
                        st.allocs += guard.count();
                        st.problem ("audio thread allocated " + juce::String (guard.count()) + "x ("
                                    + juce::String (guard.bytes()) + " B) block#" + juce::String ((int) st.blocks)
                                    + " n=" + juce::String (n) + " " + tag);
                    }
                }
            }

            // The level contract only holds while the user-visible gains are sane: dry <= 100 %, width etc. are
            // inside the limiter. A bypassed block is a plain pass-through of a <= full-scale input.
            // Band Solo is limited by dsp::soloLimit (it used to overshoot 6.7x on a hostile input while the
            // crossover moved), so it is inside the bound like every other mode.
            const bool levelBound = true;
            checkOutput (work, 0, mainCh, n, levelBound, st, tag + levelContext (proc));

            if (wetCh > 0)
                checkOutput (work, mainCh, wetCh, n, levelBound, st, tag + " (wet bus)");

            ph.advance (n, sc.sampleRate);
            done += n;
            ++st.blocks;
        }

        proc.setPlayHead (nullptr);
        return st;
    }

    void expectClean (const Stats& st, const Scenario& sc)
    {
        const juce::String line = "  scenario sr=" + juce::String (sc.sampleRate) + " blk=" + (sc.fixedBlock > 0 ? juce::String (sc.fixedBlock) : juce::String ("rand")) + "/" + juce::String (sc.maxBlock)
                                + " " + sc.layout.describe() + " seed=" + juce::String ((int) sc.seed)
                                + " blocks=" + juce::String ((int) st.blocks) + " peak=" + juce::String (st.peak, 3);
        std::cout << line << std::endl;

        GF_EXPECT_MSG (st.firstProblem.isEmpty(), st.firstProblem + " | " + line);
        GF_EXPECT_MSG (st.allocs == 0, line);
        GF_EXPECT_MSG (st.nonFinite == 0, line);
        GF_EXPECT_MSG (st.subnormal == 0, line);
        GF_EXPECT_MSG (st.tooLoud == 0, line);
    }

    const double kRates[] = { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const Layout kLayouts[] = { { false, false, false }, { true, false, false }, { false, true, false },
                                { false, true, true }, { true, true, true }, { false, false, true } };
}

//==============================================================================
GF_TEST (IntegratedStress, everything_on_matrix_sample_rates_layouts_and_block_sizes)
{
    // Every sample rate against a rotating layout / block-size mode.
    const int fixed[] = { 0, 1, 7, 64, 512, 0, 1000, 8192, 0 };
    int i = 0;

    for (const auto sr : kRates)
        for (int k = 0; k < 3; ++k, ++i)
        {
            Scenario sc;
            sc.sampleRate = sr;
            sc.layout = kLayouts[i % 6];
            sc.fixedBlock = fixed[i % 9];
            sc.maxBlock = sc.fixedBlock > 0 ? sc.fixedBlock : (i % 2 == 0 ? 8192 : 1024);
            sc.seed = (unsigned) (1000 + i);
            sc.totalSamples = sc.fixedBlock == 1 ? 12000
                            : (int) juce::jmin (sr * 2.5, (double) 300000);
            expectClean (runScenario (sc), sc);
        }
}

GF_TEST (IntegratedStress, random_block_sizes_with_many_seeds_and_state_churn)
{
    for (unsigned seed = 1; seed <= 6; ++seed)
    {
        Scenario sc;
        sc.sampleRate = seed % 2 ? 44100.0 : 96000.0;
        sc.layout = kLayouts[seed % 6];
        sc.maxBlock = 4096;
        sc.seed = 77u * seed;
        sc.totalSamples = (int) (sc.sampleRate * 3.0);
        expectClean (runScenario (sc), sc);
    }
}

GF_TEST (IntegratedStress, defaults_with_bypass_and_transport_churn_only)
{
    // No parameter fuzz: factory defaults, only host actions.
    for (const auto sr : { 44100.0, 48000.0, 192000.0 })
    {
        Scenario sc;
        sc.sampleRate = sr;
        sc.layout = { false, true, false };
        sc.maxBlock = 512;
        sc.everythingOn = false;
        sc.stateChurn = false;
        sc.seed = 4242;
        sc.totalSamples = (int) (sr * 3.0);
        expectClean (runScenario (sc), sc);
    }
}

GF_TEST (IntegratedStress, panic_and_undo_spam_every_block)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 256, true, false, true);
    QaRng rng (99);
    ParamSet ps (proc);
    setNormalised (ps.wetSafety, 1.0f);
    setNormalised (ps.output, 24.0f / 36.0f);
    applyEverythingOn (ps, rng);

    TestPlayHead ph;
    ph.bpm = 150.0;
    proc.setPlayHead (&ph);
    SourceFeeder feeder (48000.0, rng);
    juce::AudioBuffer<float> work (4, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (16384);
    Stats st;

    for (int b = 0; b < 1200; ++b)
    {
        Command c;
        c.type = b % 2 ? CommandType::Panic : CommandType::Recover;
        proc.postCommand (c);

        if (b % 3 == 0)
            proc.randomise (b % 9 == 0);

        if (b % 5 == 0)
            proc.surprise();

        if (b % 7 == 0)
            proc.undo();

        midi.clear();
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 64 + (b % 30), b & 127), 0);
        work.clear();
        feeder.fill (work, 2, 2, 256, true);
        {
            AllocGuard g;
            proc.processBlock (work, midi);

            if (b >= kWarmupBlocks && g.count() != 0)
                st.problem ("allocation in panic spam, block " + juce::String (b));
        }
        checkOutput (work, 0, 2, 256, true, st, "panic spam");
        ph.advance (256, 48000.0);
    }

    proc.setPlayHead (nullptr);
    GF_EXPECT_MSG (st.firstProblem.isEmpty(), st.firstProblem);
}

GF_TEST (IntegratedStress, concurrent_audio_thread_with_editor_cycles_state_loads_and_fuzz)
{
    // The audio callback runs on its own thread, exactly as a host does, while the message thread
    // opens / closes editors, loads state and presets and fuzzes parameters. Run under TSan / ASan.
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 512, true, false, true);

    ParamSet ps (proc);
    QaRng setup (5);
    setNormalised (ps.wetSafety, 1.0f);
    setNormalised (ps.output, 24.0f / 36.0f);
    applyEverythingOn (ps, setup);

    TestPlayHead ph;
    ph.bpm = 150.0;
    proc.setPlayHead (&ph);

    std::atomic<bool> stop { false };
    std::atomic<long> blocks { 0 };
    Stats st;

    std::thread audio ([&]
    {
        QaRng rng (31337);
        SourceFeeder feeder (sr, rng);
        juce::AudioBuffer<float> work (4, 512);
        juce::MidiBuffer midi;
        midi.ensureSize (16384);
        long calmBlocks = 0;

        while (! stop.load())
        {
            const int n = randomBlockSize (rng, 512);
            midi.clear();

            if (rng.chance (0.2f))
                addRandomMidi (midi, rng, n);

            work.clear();
            feeder.fill (work, 2, 2, n, true);
            juce::AudioBuffer<float> slice (work.getArrayOfWritePointers(), 4, n);

            {
                AllocGuard g;
                proc.processBlock (slice, midi);

                if (blocks.load() >= kWarmupBlocks && g.count() != 0)
                    st.problem ("audio thread allocated " + juce::String (g.count()) + "x while the UI was busy");
            }

            // The message thread legitimately randomises / loads state in this test, which can raise Output above 0 dB,
            // switch Wet Safety off or enable Band Solo. The +6 dBFS bound applies once the contract has held for a while
            // (gain smoothing has settled); finite / no-allocation checks always apply.
            auto& tree = proc.getValueTreeState();
            const bool contract = tree.getRawParameterValue ("output")->load() <= 0.001f && tree.getRawParameterValue ("wetSafety")->load() > 0.5f;
            calmBlocks = contract ? calmBlocks + 1 : 0;
            checkOutput (work, 0, 2, n, calmBlocks > 600, st, "concurrent");

            if (rng.chance (0.05f)) { ph.playing = ! ph.playing; }
            blocks.fetch_add (1);
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    });

    QaRng rng (2024);
    int cycles = 0;

    // GF_STRESS_EDITOR_CYCLES shortens the run under TSan (about 10x slower); the default (36) is the contract.
    const int wantedCycles = juce::SystemStats::getEnvironmentVariable ("GF_STRESS_EDITOR_CYCLES", "36").getIntValue();

    for (; cycles < wantedCycles; ++cycles)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        GF_EXPECT (editor != nullptr);

        if (editor != nullptr)
        {
            editor->setSize (800 + rng.below (600), 560 + rng.below (360));
            editor->setVisible (true);
        }

        // let the editor's timers and the processor's 15 Hz timer run against the audio thread
        pumpMessages (20 + rng.below (30));

        for (int k = 0; k < 6; ++k)
            setNormalised (ps.all[(size_t) rng.below ((int) ps.all.size())], rng.extremeBiased());

        switch (cycles % 6)
        {
            case 0: { juce::MemoryBlock b; proc.getStateInformation (b); proc.setStateInformation (b.getData(), (int) b.getSize()); break; }
            case 1: proc.setCurrentProgram (rng.below (juce::jmax (1, proc.getNumPrograms()))); break;
            case 2: proc.randomise (false); break;
            case 3: proc.surprise(); break;
            case 4: proc.undo(); break;
            default: { Command c; c.type = randomCommand (rng); proc.postCommand (c); break; }
        }

        setNormalised (ps.wetSafety, 1.0f);                        // hand the level contract back
        setNormalised (ps.bandSolo, 0.0f);
        if (ps.output->getValue() > 24.0f / 36.0f)
            setNormalised (ps.output, 24.0f / 36.0f);

        pumpMessages (10);
        editor.reset();                                          // destroyed while audio keeps running
        pumpMessages (5);
    }

    stop = true;
    audio.join();
    proc.setPlayHead (nullptr);

    std::cout << "  concurrent: editor cycles=" << cycles << " audio blocks=" << blocks.load() << " peak=" << st.peak << std::endl;
    GF_EXPECT (cycles >= juce::jmin (30, wantedCycles));
    GF_EXPECT (blocks.load() > 100);
    GF_EXPECT_MSG (st.firstProblem.isEmpty(), st.firstProblem);
}

GF_TEST (IntegratedStress, alloc_guard_sees_c_allocations_used_by_juce_containers)
{
   #if defined (__linux__) && defined (__GLIBC__) && ! defined (__SANITIZE_ADDRESS__) && ! defined (__SANITIZE_THREAD__)
    juce::HeapBlock<char> block;
    juce::MidiBuffer midi;
    AllocGuard guard;
    block.malloc (256);                                         // malloc, not operator new
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100), 1);
    GF_EXPECT_MSG (guard.count() >= 2, "AllocGuard no longer sees malloc-based allocations (HeapBlock / MidiBuffer)");
   #endif
}

GF_TEST (IntegratedStress, soak_many_seeds)
{
    // GF_STRESS_SEEDS=N runs N extra randomised scenarios (default 4); GF_STRESS_SEED0 shifts the seed range.
    const auto count = juce::SystemStats::getEnvironmentVariable ("GF_STRESS_SEEDS", "4").getIntValue();
    const auto first = juce::SystemStats::getEnvironmentVariable ("GF_STRESS_SEED0", "5000").getIntValue();

    for (int i = 0; i < count; ++i)
    {
        QaRng pick ((unsigned) (first + i));
        Scenario sc;
        sc.sampleRate = kRates[pick.below (6)];
        sc.layout = kLayouts[pick.below (6)];
        sc.maxBlock = 1 << (1 + pick.below (13));                                  // 2 .. 8192
        sc.fixedBlock = pick.chance (0.3f) ? 1 + pick.below (sc.maxBlock) : 0;
        sc.seed = (unsigned) (first + i);
        sc.everythingOn = pick.chance (0.7f);
        sc.totalSamples = (int) juce::jmin (sc.sampleRate * 6.0, 600000.0);

        if (sc.maxBlock <= 32 || (sc.fixedBlock > 0 && sc.fixedBlock <= 32))
            sc.totalSamples = juce::jmin (sc.totalSamples, 60000);              // tiny blocks: many calls, keep the run short
        expectClean (runScenario (sc), sc);
    }
}

GF_TEST (IntegratedStress, two_coop_instances_on_two_audio_threads_while_the_group_is_reconfigured)
{
    // F-X06: the registry is process-wide shared plain data. Two instances run on their own audio threads while
    // this thread flips group / mode, creates and destroys a third member and tears one instance down mid-run.
    constexpr double sr = 48000.0;
    GapFillAudioProcessor a, b;

    for (auto* p : { &a, &b })
    {
        prepareProcessor (*p, sr, 256, false);
        setParams (*p, { { "coopGroup", 1.0f }, { "coopMode", 0.0f }, { "fill", 90.0f }, { "density", 90.0f } });
    }

    std::atomic<bool> stop { false };
    std::atomic<long> blocks { 0 };
    std::atomic<long> allocBlocks { 0 };
    std::atomic<long> nonFinite { 0 };

    auto run = [&] (GapFillAudioProcessor* p, unsigned seed)
    {
        QaRng rng (seed);
        SourceFeeder feeder (sr, rng);
        TestPlayHead ph;
        ph.bpm = 150.0;
        p->setPlayHead (&ph);
        juce::AudioBuffer<float> work (2, 256);
        juce::MidiBuffer midi;
        midi.ensureSize (16384);
        long local = 0;

        while (! stop.load())
        {
            const int n = randomBlockSize (rng, 256);
            work.clear();
            feeder.fill (work, 2, 0, n, true);
            juce::AudioBuffer<float> slice (work.getArrayOfWritePointers(), 2, n);
            {
                AllocGuard g;
                p->processBlock (slice, midi);

                if (local > kWarmupBlocks && g.count() != 0)
                    allocBlocks.fetch_add (1);
            }

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    if (! std::isfinite (work.getSample (ch, i)))
                        nonFinite.fetch_add (1);

            ph.advance (n, sr);
            ++local;
            blocks.fetch_add (1);
        }

        p->setPlayHead (nullptr);
    };

    std::thread ta (run, &a, 11u), tb (run, &b, 22u);
    QaRng rng (303);

    for (int i = 0; i < 120; ++i)
    {
        setParams (rng.chance (0.5f) ? a : b, { { "coopGroup", (float) rng.below (5) }, { "coopMode", (float) rng.below (3) } });

        if (i % 10 == 0)
        {
            GapFillAudioProcessor third;                          // claims / releases a slot on this thread
            prepareProcessor (third, sr, 128, false);
            setParams (third, { { "coopGroup", 1.0f } });
            juce::Thread::sleep (3);
        }

        juce::Thread::sleep (4);
    }

    stop = true;
    ta.join();
    tb.join();

    GF_EXPECT (blocks.load() > 200);
    GF_EXPECT_MSG (allocBlocks.load() == 0, "a co-op instance allocated on its audio thread");
    GF_EXPECT_MSG (nonFinite.load() == 0, "non-finite output with co-op active");
}

GF_TEST (IntegratedStress, nan_and_inf_input_bursts_do_not_poison_the_processor)
{
    // A misbehaving upstream plug-in can hand us NaN / Inf for a block. The block itself may be garbage, but the
    // processor must be clean again once finite audio returns (no state stuck at NaN: history, filters, FX tails).
    constexpr double sr = 48000.0;

    for (const auto badValue : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 3.0e38f })
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, true, false, true);
        QaRng rng (7);
        ParamSet ps (proc);
        applyEverythingOn (ps, rng);
        setNormalised (ps.wetSafety, 1.0f);
        setNormalised (ps.output, 24.0f / 36.0f);
        TestPlayHead ph;
        ph.bpm = 150.0;
        proc.setPlayHead (&ph);
        SourceFeeder feeder (sr, rng);
        juce::AudioBuffer<float> work (4, 512);
        juce::MidiBuffer midi;
        midi.ensureSize (16384);
        Stats st;
        long lastBadBlock = -1000;

        for (int b = 0; b < 700; ++b)
        {
            work.clear();
            feeder.fill (work, 2, 2, 512, false);

            if (b >= 60 && b < 63)
            {
                for (int ch = 0; ch < 4; ++ch)
                    for (int i = 0; i < 512; i += 3)
                        work.setSample (ch, i, badValue);

                lastBadBlock = b;
            }

            proc.processBlock (work, midi);

            // 2.5 s after the burst (the longest tail is 2.5 s) everything must be finite and bounded again
            if (b > lastBadBlock + 250)
                checkOutput (work, 0, 2, 512, true, st, "after non-finite input burst");

            ph.advance (512, sr);
        }

        proc.setPlayHead (nullptr);
        GF_EXPECT_MSG (st.firstProblem.isEmpty(), "input value " + juce::String (badValue) + ": " + st.firstProblem);
    }
}
