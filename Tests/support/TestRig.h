#pragma once

// Shared offline-render rig for feature tests (docs/dev/02-FOUNDATIONS.md F0-7).
// New tests use these helpers instead of hand-rolling signal loops.

#include "../TestFramework.h"
#include "../TestSignals.h"

#include "PluginProcessor.h"

#include <functional>
#include <initializer_list>
#include <utility>

namespace gftest
{

/** Optional inputs for renderOffline. All pointers are borrowed for the call. */
struct RenderOptions
{
    /** Stereo sidechain material; null leaves the sidechain bus disabled. */
    const juce::AudioBuffer<float>* sidechain = nullptr;

    /** Transport; when null a playing 120 bpm transport starting at ppq 0 is used. */
    TestPlayHead* playHead = nullptr;

    /** Called before every block with the block index and its first sample
        (parameter automation, commands). */
    std::function<void (int blockIndex, int startSample)> onBlock;
};

struct RenderResult
{
    juce::AudioBuffer<float> out;
    int fills = 0;             ///< FillEngine::getFillsStarted() after the render
    int gapsTooShort = 0;      ///< FillEngine::getGapsRejectedTooShort() after the render
};

/** Prepares `proc` for `sampleRate` / `blockSize` and renders `in` through it
    one block at a time. Deterministic for a fixed processor state. */
RenderResult renderOffline (gapfill::GapFillAudioProcessor& proc,
                            const juce::AudioBuffer<float>& in,
                            int blockSize, double sampleRate,
                            const RenderOptions& options = {});

/** Kick/snare/hat loop with real gaps: `bars` bars of 4/4 at `bpm`. */
juce::AudioBuffer<float> drumLoop (double sampleRate, double bpm, int bars);

/** True when the binary was started with --write-golden: golden and state
    fixture tests (re)write their baselines instead of comparing. */
bool& writeGoldenMode();

/** Root of the checked-in test data (Tests/), from the GAPFILL_TEST_DATA_DIR
    compile definition. */
juce::File testDataDir();

/** "linux", "windows" or "macos": golden files are per platform because
    different compilers may differ in the last ulps. */
const char* goldenPlatform();

/** Golden scenario parameters shared by every test that renders against a baseline. */
inline constexpr double goldenSampleRate = 48000.0;
inline constexpr double goldenBpm        = 150.0;
inline constexpr int    goldenBars       = 4;
inline constexpr int    goldenBlockSize  = 512;

/** Tests/golden/<platform>/<fileName> */
juce::File goldenPath (const juce::String& fileName);

/** Planar float32 (channel 0 samples, then channel 1 ...). */
bool writeGoldenFile (const juce::File& file, const juce::AudioBuffer<float>& b);
bool readGoldenFile (const juce::File& file, juce::AudioBuffer<float>& b, int channels, int samples);

/** Scenario A (drum loop, playing 150 bpm transport, 512-sample blocks) on a
    processor that the caller has already configured. */
RenderResult renderGoldenScenarioA (gapfill::GapFillAudioProcessor& proc);

/** Sets parameters by id using real (not normalised) values. */
void setParams (gapfill::GapFillAudioProcessor& proc,
                std::initializer_list<std::pair<const char*, float>> values);

/** Largest and RMS absolute difference between two buffers (same shape), and
    the index of the first sample whose difference exceeds `maxTolerance`
    (-1 if none). */
struct BufferDiff { double maxAbs = 0.0, rms = 0.0; int firstBad = -1; int channel = -1; bool shapeMismatch = false; };
BufferDiff compareBuffers (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                           double maxTolerance);

} // namespace gftest
