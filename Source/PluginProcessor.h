#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "Presets.h"
#include "dsp/AnalysisEngine.h"
#include "dsp/FillEngine.h"
#include "dsp/VisualisationState.h"

namespace gapfill
{

//==============================================================================
class GapFillAudioProcessor  : public juce::AudioProcessor,
                               private juce::Timer
{
public:
    GapFillAudioProcessor();
    ~GapFillAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                      { return true; }

    const juce::String getName() const override          { return "GapFill"; }
    bool acceptsMidi() const override                    { return false; }
    bool producesMidi() const override                   { return false; }
    bool isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override         { return 2.5; }

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override                     { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Editor-facing helpers (message thread only)
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    MeterState& getMeterState() noexcept                            { return meters; }
    VisualisationFifo& getVisualisationFifo() noexcept              { return viz; }

    void resetAllParameters();
    /** Randomises creative (or, with `everything`, a wider) parameter set.
        Always produces a different result from the previous press. */
    void randomise (bool everything);

    juce::String getCurrentPresetName() const;
    void selectPreset (int index);

    /** Editor size persistence. */
    juce::Point<int> getSavedEditorSize() const noexcept { return savedEditorSize; }
    void setSavedEditorSize (juce::Point<int> size) noexcept { savedEditorSize = size; }

    //==========================================================================
    // Test-facing accessors
    const FillEngine& getFillEngine() const noexcept  { return engine; }
    const AnalysisEngine& getAnalysis() const noexcept { return analysis; }
    int getActiveLatencySamples() const noexcept { return meters.latencySamples.load(); }

private:
    void timerCallback() override;
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, bool bypassed);
    EngineSettings gatherEngineSettings() const;
    AnalysisSettings gatherAnalysisSettings (bool sidechainActive) const;
    void updateTempoInfo (EngineSettings& settings, int startSample, int numSamples);

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    ParamCache params;
    juce::AudioProcessorParameter* bypassParameter = nullptr;

    AnalysisEngine analysis;
    SourceHistory  history;
    FillEngine     engine;

    MeterState meters;
    VisualisationFifo viz;

    // --- buffers (all sized in prepareToPlay) ---
    juce::AudioBuffer<float> mainBuffer, wetBuffer, delayRing;
    std::vector<float> monoMain, monoSide;

    int   delayRingSize = 0;
    int   delayWrite = 0;
    int   currentLatency = 0;
    int   targetLatency = 0;
    int   latencyCrossfade = 0;
    int   latencyCrossfadeLength = 0;
    int   pendingLatencyFrom = 0;

    int   maxLookaheadSamples = 0;
    int   preparedBlockSize = 512;
    double currentSampleRate = 48000.0;
    int   mainChannels = 2;

    std::int64_t samplesProcessed = 0;

    HighPass2 lowProtect[2];
    float lastLowProtectHz = -1.0f;

    BandSplitter soloSplitter;

    juce::SmoothedValue<float> dryGain, wetGain, outGain, centreProtect, widthAmount, bypassMix;

    double lastPpqPosition = 0.0;
    bool   lastTransportPlaying = false;

    float meterIn = 0.0f, meterOut = 0.0f, meterWet = 0.0f;

    int   sidechainSilentSamples = 0;
    bool  sidechainBusEnabled = false;
    bool  sidechainHasSignal = false;
    float lastSoloXoverLM = -1.0f, lastSoloXoverMH = -1.0f;

    int currentProgram = 0;
    juce::Point<int> savedEditorSize { 1100, 700 };

    juce::Random randomSource;
    std::atomic<int> latencyToReport { 0 };
    std::atomic<bool> prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GapFillAudioProcessor)
};

} // namespace gapfill
