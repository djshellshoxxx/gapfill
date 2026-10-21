#include "Presets.h"
#include <algorithm>

namespace gapfill
{

namespace
{
    constexpr float kAuto = 0.0f;   // fillType index for AUTO

    float algoChoice (Algo a) { return (float) ((int) a + 1); }
}

const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets =
    {
        { "Init", "Plain defaults - a good neutral starting point.", {} },

        { "Subtle Ear Candy",
          "Occasional short reverses and filtered echoes that never fight the groove.",
          { { "fill", 18.0f }, { "intensity", 28.0f }, { "complexity", 15.0f },
            { "chaos", 12.0f }, { "density", 45.0f }, { "fillType", kAuto },
            { "minGap", 90.0f }, { "maxFill", 260.0f }, { "sensitivity", 48.0f },
            { "protTrans", 75.0f }, { "protNext", 85.0f }, { "protLow", 90.0f },
            { "wet", 62.0f }, { "wGranular", 20.0f }, { "wSpectral", 0.0f },
            { "wCrush", 0.0f }, { "wDistort", 0.0f }, { "wComb", 0.0f } } },

        { "Drum Gap Glitch",
          "Classic between-the-hits glitching for loops and drum buses.",
          { { "fill", 52.0f }, { "intensity", 58.0f }, { "complexity", 48.0f },
            { "chaos", 35.0f }, { "density", 70.0f }, { "minGap", 55.0f },
            { "maxFill", 320.0f }, { "sensitivity", 58.0f }, { "gapThresh", 58.0f },
            { "protTrans", 80.0f }, { "protNext", 85.0f }, { "protLow", 80.0f },
            { "stutterDiv", (float) Division::ThirtySecond },
            { "wStutter", 95.0f }, { "wReverse", 80.0f }, { "wScramble", 70.0f },
            { "wSpectral", 0.0f }, { "wFreeze", 10.0f } } },

        { "DnB Micro Fills",
          "Fast micro-edits between breakbeat hits with the attacks left intact.",
          { { "fill", 62.0f }, { "intensity", 62.0f }, { "complexity", 55.0f },
            { "chaos", 40.0f }, { "density", 78.0f }, { "minGap", 35.0f },
            { "maxFill", 190.0f }, { "sensitivity", 66.0f }, { "gapThresh", 62.0f },
            { "retrigGuard", 18.0f }, { "protectHold", 22.0f }, { "gapRelease", 35.0f },
            { "lookahead", 30.0f }, { "protTrans", 90.0f }, { "protNext", 92.0f },
            { "protLow", 110.0f }, { "stutterDiv", (float) Division::ThirtySecond },
            { "delayDiv", (float) Division::ThirtySecond },
            { "wStutter", 100.0f }, { "wReverse", 85.0f }, { "wCrush", 55.0f },
            { "wScramble", 75.0f }, { "wFreeze", 0.0f }, { "wSpectral", 0.0f } } },

        { "Breakcore Destroyer",
          "Everything on, everywhere - still keeps the original hits punching through.",
          { { "fill", 92.0f }, { "intensity", 88.0f }, { "complexity", 86.0f },
            { "chaos", 78.0f }, { "density", 95.0f }, { "minGap", 22.0f },
            { "maxFill", 420.0f }, { "sensitivity", 72.0f }, { "gapThresh", 72.0f },
            { "retrigGuard", 14.0f }, { "protectHold", 16.0f }, { "gapRelease", 22.0f },
            { "protTrans", 85.0f }, { "protNext", 70.0f }, { "protLow", 70.0f },
            { "stutterDiv", (float) Division::ThirtySecond },
            { "wCrush", 85.0f }, { "wDistort", 80.0f }, { "wScramble", 95.0f },
            { "wStutter", 100.0f }, { "wPitchRise", 70.0f }, { "wPitchFall", 70.0f } } },

        { "Ghost Reverses",
          "Reverse swells rising into each hit.",
          { { "fill", 45.0f }, { "intensity", 40.0f }, { "complexity", 22.0f },
            { "chaos", 18.0f }, { "density", 60.0f }, { "fillType", algoChoice (Algo::Reverse) },
            { "minGap", 110.0f }, { "maxFill", 600.0f }, { "lookahead", 90.0f },
            { "protTrans", 70.0f }, { "protNext", 100.0f }, { "wet", 70.0f } } },

        { "Industrial Debris",
          "Metallic comb resonances and crushed shrapnel in the spaces.",
          { { "fill", 58.0f }, { "intensity", 78.0f }, { "complexity", 62.0f },
            { "chaos", 48.0f }, { "density", 66.0f }, { "minGap", 70.0f },
            { "maxFill", 480.0f }, { "protLow", 120.0f }, { "protTrans", 78.0f },
            { "wComb", 95.0f }, { "wCrush", 85.0f }, { "wDistort", 70.0f },
            { "wTexture", 70.0f }, { "wReverse", 30.0f }, { "wDelay", 30.0f },
            { "wGranular", 45.0f }, { "wFreeze", 25.0f } } },

        { "Vocal Ghosts",
          "Phrase-aware granular and reverse tails after vocal lines.",
          { { "detMode", (float) DetMode::Phrase }, { "fill", 38.0f }, { "intensity", 45.0f },
            { "complexity", 52.0f }, { "chaos", 30.0f }, { "density", 55.0f },
            { "minGap", 180.0f }, { "maxFill", 900.0f }, { "sensitivity", 42.0f },
            { "gapThresh", 45.0f }, { "gapRelease", 160.0f }, { "protectHold", 90.0f },
            { "retrigGuard", 140.0f }, { "lookahead", 110.0f },
            { "protTrans", 60.0f }, { "protNext", 95.0f },
            { "wGranular", 95.0f }, { "wReverse", 80.0f }, { "wSpectral", 70.0f },
            { "wFreeze", 60.0f }, { "wStutter", 15.0f }, { "wCrush", 0.0f },
            { "wDistort", 0.0f }, { "wScramble", 10.0f }, { "wet", 65.0f } } },

        { "Vocal Delay Fills",
          "Rhythmic ping-pong echoes that appear only where the singer breathes.",
          { { "detMode", (float) DetMode::Phrase }, { "fill", 46.0f }, { "intensity", 42.0f },
            { "complexity", 26.0f }, { "chaos", 16.0f }, { "density", 60.0f },
            { "fillType", algoChoice (Algo::Delay) },
            { "minGap", 200.0f }, { "maxFill", 1200.0f }, { "gapRelease", 150.0f },
            { "protectHold", 80.0f }, { "lookahead", 120.0f },
            { "delayDiv", (float) Division::EighthD }, { "pingPong", 1.0f },
            { "protNext", 100.0f }, { "protTrans", 55.0f }, { "wet", 58.0f } } },

        { "Bass Between Kicks",
          "Sidechain the drums in: fills appear only where the kick is not.",
          { { "detSource", (float) DetSource::Sidechain }, { "fill", 50.0f },
            { "intensity", 45.0f }, { "complexity", 38.0f }, { "chaos", 22.0f },
            { "density", 65.0f }, { "minGap", 90.0f }, { "maxFill", 380.0f },
            { "protLow", 110.0f }, { "protSC", 95.0f }, { "protNext", 90.0f },
            { "lookahead", 60.0f }, { "wGranular", 40.0f }, { "wSpectral", 15.0f },
            { "wComb", 0.0f }, { "wCrush", 20.0f }, { "wet", 70.0f } } },

        { "Granular Spaces",
          "Wide granular clouds filling the longer gaps.",
          { { "fill", 55.0f }, { "intensity", 52.0f }, { "complexity", 72.0f },
            { "chaos", 42.0f }, { "density", 58.0f }, { "fillType", algoChoice (Algo::Granular) },
            { "minGap", 160.0f }, { "maxFill", 1100.0f }, { "lookahead", 100.0f },
            { "protNext", 88.0f }, { "protCenter", 35.0f }, { "width", 140.0f },
            { "wet", 66.0f } } },

        { "Trance Transitions",
          "Long filtered and pitched risers between phrases.",
          { { "detMode", (float) DetMode::Phrase }, { "fill", 42.0f }, { "intensity", 66.0f },
            { "complexity", 58.0f }, { "chaos", 26.0f }, { "density", 50.0f },
            { "minGap", 260.0f }, { "maxFill", 1800.0f }, { "maxFillSync", 1.0f },
            { "maxFillDiv", (float) Division::Half }, { "gapRelease", 180.0f },
            { "lookahead", 140.0f }, { "protNext", 95.0f }, { "protLow", 100.0f },
            { "wPitchRise", 100.0f }, { "wFilter", 95.0f }, { "wSpectral", 70.0f },
            { "wFreeze", 55.0f }, { "wStutter", 25.0f }, { "wCrush", 0.0f },
            { "wScramble", 10.0f }, { "width", 130.0f } } },

        { "Trip Hop Dust",
          "Quiet, dusty debris in the spaces of a slow beat.",
          { { "fill", 26.0f }, { "intensity", 32.0f }, { "complexity", 34.0f },
            { "chaos", 24.0f }, { "density", 42.0f }, { "minGap", 140.0f },
            { "maxFill", 520.0f }, { "sensitivity", 45.0f }, { "gapThresh", 45.0f },
            { "protLow", 140.0f }, { "protTrans", 70.0f }, { "protCenter", 45.0f },
            { "wTexture", 90.0f }, { "wCrush", 60.0f }, { "wReverse", 55.0f },
            { "wGranular", 55.0f }, { "wSpectral", 35.0f }, { "wet", 48.0f } } },

        { "Spectral High Fill",
          "Leaves the kick alone and drops glitches into the empty top end.",
          { { "detMode", (float) DetMode::Spectral }, { "fill", 55.0f },
            { "intensity", 55.0f }, { "complexity", 50.0f }, { "chaos", 34.0f },
            { "density", 72.0f }, { "minGap", 45.0f }, { "maxFill", 300.0f },
            { "bandLowOn", 0.0f }, { "bandMidOn", 0.0f }, { "bandHighOn", 1.0f },
            { "bandHighProb", 100.0f }, { "bandHighAmt", 100.0f }, { "bandHighInt", 62.0f },
            { "xoverMH", 2800.0f }, { "protLow", 120.0f }, { "protTrans", 78.0f } } },

        { "Spectral Mid Mutation",
          "Mutates the midrange while lows and highs carry on untouched.",
          { { "detMode", (float) DetMode::Spectral }, { "fill", 58.0f },
            { "intensity", 62.0f }, { "complexity", 68.0f }, { "chaos", 45.0f },
            { "density", 70.0f }, { "minGap", 70.0f }, { "maxFill", 520.0f },
            { "bandLowOn", 0.0f }, { "bandMidOn", 1.0f }, { "bandHighOn", 0.0f },
            { "bandMidProb", 100.0f }, { "bandMidInt", 70.0f },
            { "xoverLM", 220.0f }, { "xoverMH", 4200.0f },
            { "wSpectral", 80.0f }, { "wGranular", 80.0f }, { "wScramble", 70.0f },
            { "protLow", 130.0f } } },

        { "Sparse Texture",
          "Rare, synthesised textures shaped by the track's own spectrum.",
          { { "fill", 20.0f }, { "intensity", 38.0f }, { "complexity", 40.0f },
            { "chaos", 30.0f }, { "density", 28.0f }, { "fillType", algoChoice (Algo::Texture) },
            { "minGap", 200.0f }, { "maxFill", 800.0f }, { "protCenter", 55.0f },
            { "protNext", 92.0f }, { "width", 150.0f }, { "wet", 55.0f } } },

        { "Extreme Chaos",
          "Maximum variation. Bounded, but only just.",
          { { "fill", 100.0f }, { "intensity", 95.0f }, { "complexity", 95.0f },
            { "chaos", 100.0f }, { "density", 100.0f }, { "minGap", 18.0f },
            { "maxFill", 700.0f }, { "sensitivity", 78.0f }, { "gapThresh", 80.0f },
            { "retrigGuard", 10.0f }, { "protectHold", 10.0f }, { "gapRelease", 15.0f },
            { "protTrans", 55.0f }, { "protNext", 45.0f }, { "protLow", 60.0f },
            { "wet", 85.0f } } },
    };

    return presets;
}

//==============================================================================
void resetToDefaults (juce::AudioProcessorValueTreeState& state)
{
    for (const auto& id : allParameterIDs())
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
    }
}

void applyPreset (juce::AudioProcessorValueTreeState& state, int index)
{
    const auto& presets = getFactoryPresets();

    if (index < 0 || index >= (int) presets.size())
        return;

    resetToDefaults (state);

    for (const auto& v : presets[(size_t) index].values)
    {
        if (auto* p = state.getParameter (v.id))
            p->setValueNotifyingHost (p->convertTo0to1 (v.value));
    }
}

//==============================================================================
int randomiseParameters (juce::AudioProcessorValueTreeState& state,
                         bool everything,
                         juce::Random& random)
{
    const auto ids = everything ? randomisableParameterIDs() : creativeParameterIDs();

    for (const auto& id : ids)
    {
        auto* p = state.getParameter (id);

        if (p == nullptr)
            continue;

        if (id == "seed")
            continue;   // handled below so that the value is always new

        // Keep the macros in a musically useful window rather than letting them
        // land anywhere; the result should be playable, not broken.
        float normalised;

        if (id == "fill")
            normalised = random.nextFloat() * 0.75f + 0.2f;
        else if (id == "intensity" || id == "complexity" || id == "density")
            normalised = random.nextFloat() * 0.85f + 0.1f;
        else if (id == "chaos")
            normalised = random.nextFloat() * 0.9f;
        else if (id == "minGap" || id == "maxFill" || id == "gapRelease"
                  || id == "protectHold" || id == "retrigGuard")
            normalised = random.nextFloat() * 0.8f + 0.1f;
        else if (id.startsWith ("prot"))
            normalised = random.nextFloat() * 0.6f + 0.35f;   // stay protective
        else
            normalised = random.nextFloat();

        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    }

    // Guarantee at least a few algorithms remain available in Auto mode.
    if (auto* fillType = state.getParameter ("fillType"))
    {
        const auto isAuto = fillType->getValue() <= 0.001f;

        if (isAuto)
        {
            int enabled = 0;

            for (int i = 0; i < numAlgos; ++i)
                if (auto* w = state.getParameter (algoWeightID ((Algo) i)))
                    if (w->getValue() > 0.05f)
                        ++enabled;

            if (enabled < 3)
            {
                for (const char* id : { "wReverse", "wStutter", "wDelay" })
                    if (auto* w = state.getParameter (id))
                        w->setValueNotifyingHost (0.6f + random.nextFloat() * 0.4f);
            }
        }
    }

    // A fresh seed every time, so pressing Random twice never repeats.
    int newSeed = 1;

    if (auto* seedParam = state.getParameter ("seed"))
    {
        const auto current = (int) std::lround (seedParam->convertFrom0to1 (seedParam->getValue()));

        do { newSeed = random.nextInt ({ 0, 10000 }); }
        while (newSeed == current);

        seedParam->setValueNotifyingHost (seedParam->convertTo0to1 ((float) newSeed));
    }

    return newSeed;
}

//==============================================================================
namespace
{
    struct Range { float lo, hi; };

    /** A coherent creative direction: macro windows plus the algorithms that
        carry it. Everything not listed gets a low background weight so Auto
        always has a fallback for gaps the core algorithms do not fit. */
    struct SurpriseProfile
    {
        const char* name;
        Range fill, intensity, complexity, chaos, density;
        std::vector<Algo> core;
        std::vector<int> divisions;   // candidate stutter/delay divisions
        Range width;
    };

    const std::vector<SurpriseProfile>& surpriseProfiles()
    {
        using D = Division;
        static const std::vector<SurpriseProfile> profiles =
        {
            { "Subtle Sparkle",  { 0.30f, 0.50f }, { 0.25f, 0.45f }, { 0.20f, 0.40f }, { 0.05f, 0.25f }, { 0.40f, 0.60f },
              { Algo::Reverse, Algo::Delay, Algo::Filter },
              { (int) D::Eighth, (int) D::Sixteenth, (int) D::EighthD }, { 110.0f, 150.0f } },
            { "Drum Edits",      { 0.55f, 0.80f }, { 0.45f, 0.70f }, { 0.35f, 0.60f }, { 0.20f, 0.45f }, { 0.60f, 0.85f },
              { Algo::Stutter, Algo::Reverse, Algo::Scramble, Algo::TapeStop },
              { (int) D::Sixteenth, (int) D::ThirtySecond, (int) D::SixteenthT }, { 80.0f, 120.0f } },
            { "Glitch Storm",    { 0.70f, 0.95f }, { 0.65f, 0.90f }, { 0.60f, 0.90f }, { 0.55f, 0.85f }, { 0.75f, 1.00f },
              { Algo::Stutter, Algo::Crush, Algo::Scramble, Algo::Distort },
              { (int) D::ThirtySecond, (int) D::SixteenthT, (int) D::Sixteenth }, { 100.0f, 180.0f } },
            { "Dub Echoes",      { 0.40f, 0.65f }, { 0.35f, 0.60f }, { 0.25f, 0.50f }, { 0.10f, 0.35f }, { 0.45f, 0.70f },
              { Algo::Delay, Algo::Filter, Algo::Comb },
              { (int) D::EighthD, (int) D::QuarterT, (int) D::Eighth }, { 130.0f, 190.0f } },
            { "Ambient Haze",    { 0.40f, 0.70f }, { 0.30f, 0.55f }, { 0.55f, 0.85f }, { 0.20f, 0.45f }, { 0.40f, 0.65f },
              { Algo::Granular, Algo::Freeze, Algo::Spectral, Algo::Texture },
              { (int) D::Quarter, (int) D::Eighth, (int) D::QuarterD }, { 140.0f, 200.0f } },
            { "Pitch Bender",    { 0.45f, 0.75f }, { 0.50f, 0.80f }, { 0.35f, 0.65f }, { 0.25f, 0.55f }, { 0.50f, 0.75f },
              { Algo::PitchRise, Algo::PitchFall, Algo::TapeStop, Algo::Reverse },
              { (int) D::Sixteenth, (int) D::Eighth }, { 90.0f, 140.0f } },
            { "Industrial Grit", { 0.55f, 0.85f }, { 0.70f, 0.95f }, { 0.40f, 0.70f }, { 0.30f, 0.60f }, { 0.60f, 0.90f },
              { Algo::Distort, Algo::Crush, Algo::Comb, Algo::Stutter },
              { (int) D::Sixteenth, (int) D::ThirtySecond }, { 70.0f, 110.0f } },
            { "Tape Breakdown",  { 0.50f, 0.80f }, { 0.45f, 0.75f }, { 0.30f, 0.55f }, { 0.15f, 0.40f }, { 0.55f, 0.80f },
              { Algo::TapeStop, Algo::Reverse, Algo::Filter, Algo::Delay },
              { (int) D::Eighth, (int) D::Sixteenth, (int) D::EighthT }, { 90.0f, 130.0f } },
        };

        return profiles;
    }

    void setReal (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float pick (juce::Random& random, Range r) { return r.lo + random.nextFloat() * (r.hi - r.lo); }
}

int getNumSurpriseProfiles() noexcept
{
    return (int) surpriseProfiles().size();
}

const SurpriseTargets& surpriseTargets (int profile) noexcept
{
    // Built once from surpriseProfiles() (the table Surprise itself reads); never mutated afterwards.
    static const std::vector<SurpriseTargets> targets = []
    {
        std::vector<SurpriseTargets> out;

        for (const auto& p : surpriseProfiles())
        {
            SurpriseTargets t;
            t.name = p.name;
            t.fill       = 0.5f * (p.fill.lo + p.fill.hi);
            t.intensity  = 0.5f * (p.intensity.lo + p.intensity.hi);
            t.complexity = 0.5f * (p.complexity.lo + p.complexity.hi);
            t.chaos      = 0.5f * (p.chaos.lo + p.chaos.hi);
            t.density    = 0.5f * (p.density.lo + p.density.hi);

            for (int i = 0; i < numAlgos; ++i)
                t.weight[i] = std::find (p.core.begin(), p.core.end(), (Algo) i) != p.core.end() ? 0.80f : 0.06f;

            t.stutterDivision = p.divisions.empty() ? 0 : p.divisions.front();
            out.push_back (t);
        }

        return out;
    }();

    return targets[(size_t) juce::jlimit (0, (int) targets.size() - 1, profile)];
}

int surpriseParameters (juce::AudioProcessorValueTreeState& state,
                        juce::Random& random,
                        int previousProfile,
                        juce::String& name)
{
    const auto& profiles = surpriseProfiles();
    const auto count = (int) profiles.size();

    auto index = random.nextInt (count);

    if (index == previousProfile)
        index = (index + 1 + random.nextInt (count - 1)) % count;

    const auto& prof = profiles[(size_t) index];
    name = prof.name;

    setReal (state, "fill",       100.0f * pick (random, prof.fill));
    setReal (state, "intensity",  100.0f * pick (random, prof.intensity));
    setReal (state, "complexity", 100.0f * pick (random, prof.complexity));
    setReal (state, "chaos",      100.0f * pick (random, prof.chaos));
    setReal (state, "density",    100.0f * pick (random, prof.density));
    setReal (state, "width",      pick (random, prof.width));
    setReal (state, "fillType",   kAuto);

    for (int i = 0; i < numAlgos; ++i)
    {
        const auto isCore = std::find (prof.core.begin(), prof.core.end(), (Algo) i) != prof.core.end();
        setReal (state, algoWeightID ((Algo) i),
                 isCore ? 65.0f + random.nextFloat() * 35.0f : random.nextFloat() * 12.0f);
    }

    const auto div = [&] { return (float) prof.divisions[(size_t) random.nextInt ((int) prof.divisions.size())]; };
    setReal (state, "stutterDiv", div());
    setReal (state, "delayDiv",   div());
    setReal (state, "pingPong",   random.nextBool() ? 1.0f : 0.0f);

    if (auto* seedParam = state.getParameter ("seed"))
    {
        const auto current = (int) std::lround (seedParam->convertFrom0to1 (seedParam->getValue()));
        int newSeed;

        do { newSeed = random.nextInt ({ 0, 10000 }); }
        while (newSeed == current);

        seedParam->setValueNotifyingHost (seedParam->convertTo0to1 ((float) newSeed));
    }

    return index;
}

} // namespace gapfill
