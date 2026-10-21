#pragma once

#include "DspUtils.h"
#include "AnalysisEngine.h"
#include "SpectralSmear.h"
#include "../features/cloud/CloudMode.h"
#include "../features/fx/FxSettings.h"
#include "../features/fx/VoiceFxChain.h"
#include "../features/swell/SwellMode.h"

namespace gapfill
{

//==============================================================================
/** Extra transformations that Complexity stacks on top of a base algorithm. */
enum class Modifier { None = 0, FilterSweep, Crush, Distort, Tremolo, PanMotion, NumModifiers };

//==============================================================================
/** Everything a voice needs to know at the moment it is triggered.
    Filled by the scheduler; the voice never reads parameters itself. */
struct VoiceSetup
{
    Algo algo = Algo::Reverse;

    double sampleRate   = 48000.0;
    int    lengthSamples = 0;
    int    fadeInSamples = 0;
    int    fadeOutSamples = 0;

    float amp         = 0.6f;
    float intensity01 = 0.5f;
    float complexity01 = 0.4f;
    float chaos01     = 0.25f;

    /** Absolute position in the source history that the fill is cut from
        (the capture ends here and extends backwards). */
    std::int64_t sourceAnchor = 0;

    /** How long ago the event that opened this gap happened, in samples.
        Slice-based algorithms use it to line their material up with the hit
        instead of with an arbitrary point in the capture. */
    int sinceEventSamples = 0;

    /** -1 = broadband, 0/1/2 = low/mid/high spectral lane. */
    int   band      = -1;
    float xoverLM   = 180.0f;
    float xoverMH   = 3500.0f;

    float stutterSeconds = 0.125f;   // musical subdivision for stutter
    float delaySeconds   = 0.125f;   // musical subdivision for delay taps
    bool  pingPong       = true;

    float bandEnergy[3] { 0.3f, 0.3f, 0.3f };   // shapes the Texture algorithm

    std::uint64_t rngSeed = 1;

    /** Recipe overrides (F0-3). Neutral by default: the voice derives both
        choices itself, exactly as before. */
    /** Bit per Modifier (bit0 = FilterSweep .. bit4 = PanMotion) that replaces
        the complexity-derived stack; 0xFF = derive. At most maxModifiers bits
        are honoured, lowest first. The voice still consumes the same RNG draws. */
    std::uint8_t modifierOverride = 0xFF;
    /** Pitch Rise / Pitch Fall target in signed semitones; 127 = derive. */
    std::int8_t  pitchOverride = 127;

    /** Effect cards and render modes (docs/dev/12 FX-0, WP8). Neutral by default. */
    fx::VoiceFxStart    fxStart;
    fx::VoiceRenderModes modes;
    bool excludeFromBus = false;     ///< F-L14: band voice whose effects are switched off
};

//==============================================================================
/** One generated fill.

    A voice owns every buffer it needs (allocated in prepare()), reads its
    material from the shared source history, and renders additively into the wet
    bus. Nothing here allocates, locks or logs. */
class FillVoice
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Starts a fill. Returns false if the setup cannot be honoured. */
    bool start (const VoiceSetup& setup, const SourceHistory& history,
                SpectralSmear* spectral);

    /** Asks the voice to fade out over `fadeSamples` and finish. */
    void requestStop (int fadeSamples) noexcept;

    /** Applies a duck (0..1) that follows the protection engine. */
    void setDuck (float g) noexcept { duckTarget = juce::jlimit (0.0f, 1.0f, g); }

    bool isActive() const noexcept { return active; }
    Algo getAlgo()  const noexcept { return setupData.algo; }
    int  getBand()  const noexcept { return setupData.band; }
    int  getRemainingSamples() const noexcept { return juce::jmax (0, totalLength - elapsed); }
    int  getLengthSamples()    const noexcept { return totalLength; }
    bool isExcludedFromBus() const noexcept { return setupData.excludeFromBus; }

    /** Effect-chain telemetry sink (owned by the processor's FxChain). */
    void setFxMeters (fx::FxMeters* m) noexcept { fxChain.setMeters (m); }
    fx::VoiceFxChain& getFxChain() noexcept { return fxChain; }
    const fx::SwellMode& getSwellMode() const noexcept { return swellMode; }
    const fx::CloudMode& getCloudMode() const noexcept { return cloudMode; }
    fx::CloudMode& getCloudMode() noexcept { return cloudMode; }

    /** Renders and adds `numSamples` of this voice into `wet`. */
    void render (juce::AudioBuffer<float>& wet, int startSample, int numSamples,
                 const SourceHistory& history) noexcept;

private:
    void deactivate() noexcept;
    void captureSource (const SourceHistory& history, int lengthNeeded);
    void configureAlgorithm();
    void generateSample (float& left, float& right, const SourceHistory& history) noexcept;
    float readCapture (int channel, double index) const noexcept;

    /** Reads the looping "freeze" region at `position` (0 .. freezeLen) with an
        equal-power crossfade across the loop point, so that none of the
        algorithms built on it can click when the loop wraps. */
    void readFreezeLoop (double position, float& l, float& r) const noexcept;
    float envelopeAt (int t) const noexcept;
    void applyModifiers (float& l, float& r) noexcept;
    void renderWithFx (juce::AudioBuffer<float>& wet, int startSample, int numSamples,
                       const SourceHistory& history) noexcept;

    // --- base algorithm generators ---
    void genReverse (float& l, float& r) noexcept;
    void genStutter (float& l, float& r) noexcept;
    void genDelay (float& l, float& r) noexcept;
    void genGranular (float& l, float& r, const SourceHistory& history) noexcept;
    void genFreeze (float& l, float& r) noexcept;
    void genPitch (float& l, float& r) noexcept;
    void genFilter (float& l, float& r) noexcept;
    void genTexture (float& l, float& r) noexcept;
    void genCrush (float& l, float& r) noexcept;
    void genDistort (float& l, float& r) noexcept;
    void genScramble (float& l, float& r) noexcept;
    void genSpectral (float& l, float& r) noexcept;
    void genComb (float& l, float& r) noexcept;
    void genTapeStop (float& l, float& r) noexcept;

    //--------------------------------------------------------------------------
    VoiceSetup setupData;
    bool   active = false;
    double sr = 48000.0;
    int    channels = 2;

    juce::AudioBuffer<float> capture;
    int captureLength = 0;
    int maxCaptureSamples = 0;

    int elapsed = 0;
    int totalLength = 0;
    int fadeIn = 0, fadeOut = 0;

    float killGain = 1.0f, killStep = 0.0f;
    bool  killing = false;
    float duck = 1.0f, duckTarget = 1.0f, duckCoeff = 0.01f;

    Rng rng;

    /** Offset into the capture at which the event that opened the gap starts. */
    int sliceOffset = 0;

    // --- shared playback state ---
    double playPos = 0.0;
    double playRate = 1.0;

    // --- reverse ---
    double reversePos = 0.0;

    // --- stutter ---
    int   stutterSliceLen = 0;
    int   stutterIndex = 0;
    int   stutterPos = 0;
    int   stutterCurrentLen = 0;
    float stutterRatio = 1.0f;
    float stutterRate = 1.0f;
    float stutterRateStep = 1.0f;
    double stutterRead = 0.0;
    float stutterGain = 1.0f;
    float stutterGainStep = 1.0f;

    // --- delay ---
    static constexpr int maxTaps = 10;
    int   numTaps = 4;
    int   tapOffset[maxTaps] {};
    float tapGain[maxTaps] {};
    float tapPanL[maxTaps] {};
    float tapPanR[maxTaps] {};
    int   tapSliceLen = 0;

    // --- granular ---
    struct Grain
    {
        bool  active = false;
        double pos = 0.0;
        double rate = 1.0;
        int   age = 0;
        int   dur = 1;
        float panL = 0.7f, panR = 0.7f;
        float amp = 1.0f;
    };

    static constexpr int maxGrains = 16;
    Grain grains[maxGrains];
    int   grainCountdown = 0;
    int   grainInterval = 512;
    int   grainDuration = 2048;
    float grainSpread = 0.5f;
    float grainPitchSpread = 0.0f;
    float grainReverseProb = 0.0f;
    double grainWindowSeconds = 0.4;

    // --- freeze ---
    int    freezeStart = 0;
    int    freezeLen = 1;
    int    freezeXfade = 1;
    double freezePos = 0.0;
    double freezeDrift = 1.0;

    // --- pitch ---
    double pitchRate = 1.0;
    double pitchRateEnd = 2.0;
    double pitchPos = 0.0;
    float  pitchXfadePos = 0.0f;

    // --- filter sweep ---
    SVF filterL, filterR;
    float filterStart = 200.0f, filterEnd = 6000.0f, filterQ = 1.0f;
    int   filterMode = 0;   // 0 = LP, 1 = HP, 2 = BP

    // --- texture ---
    SVF texL[3], texR[3];
    float texGain[3] { 0.0f, 0.0f, 0.0f };
    float texAttack = 0.02f, texDecay = 0.3f;
    Rng   noiseRng;

    // --- crush / distort ---
    int   crushBits = 8;
    int   crushDecimate = 1;
    int   crushCounter = 0;
    float crushHoldL = 0.0f, crushHoldR = 0.0f;
    float driveAmount = 1.0f;
    float distortNorm = 1.0f;

    // --- scramble ---
    static constexpr int maxSlices = 32;
    int   sliceOrder[maxSlices] {};
    int   numSlices = 8;
    int   sliceLen = 1;
    int   sliceIndex = 0;
    int   slicePos = 0;

    // --- comb ---
    juce::AudioBuffer<float> combBuffer;
    int   combSize = 0;
    int   combWrite = 0;
    int   combDelay = 64;
    float combFeedback = 0.9f;
    float combDamp = 0.3f;
    float combStateL = 0.0f, combStateR = 0.0f;
    int   combExcite = 0;

    // --- tape stop ---
    double tapePos = 0.0;
    float  tapeCurve = 1.5f;
    float  tapeWow = 0.0f, tapeWowPhase = 0.0f;
    float  tapeLpL = 0.0f, tapeLpR = 0.0f;

    // --- spectral ---
    SpectralSmear* spectralProcessor = nullptr;

    // --- modifiers ---
    static constexpr int maxModifiers = 3;
    Modifier modifiers[maxModifiers] { Modifier::None, Modifier::None, Modifier::None };
    int   numModifiers = 0;
    SVF   modFilterL, modFilterR;
    float modFilterStart = 300.0f, modFilterEnd = 8000.0f, modFilterQ = 1.2f;
    int   modFilterMode = 0;
    int   modCrushBits = 8, modCrushDecimate = 1, modCrushCounter = 0;
    float modCrushHoldL = 0.0f, modCrushHoldR = 0.0f;
    float modDrive = 2.0f;
    float modDistortNorm = 1.0f;
    float modTremRate = 8.0f, modTremDepth = 0.5f, modTremPhase = 0.0f;
    float modPanRate = 1.5f, modPanDepth = 0.6f, modPanPhase = 0.0f;

    // --- band isolation ---
    BandSplitter bandFilter;
    bool bandFilterActive = false;

    // --- effects (docs/dev/12 FX-0, WP8) ---
    fx::SwellMode swellMode;
    fx::CloudMode cloudMode;
    fx::VoiceFxChain fxChain;
    float fxPostGain[fx::kMaxVoiceBlock] {};     ///< duck x kill, applied after the chain
    float fxScratchL[fx::kMaxVoiceBlock] {};
    float fxScratchR[fx::kMaxVoiceBlock] {};
    float fxPostGainNow = 1.0f;
};

} // namespace gapfill
