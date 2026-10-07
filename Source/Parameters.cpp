#include "Parameters.h"

namespace gapfill
{

const char* algoName (Algo a) noexcept
{
    switch (a)
    {
        case Algo::Reverse:   return "Reverse";
        case Algo::Stutter:   return "Stutter";
        case Algo::Delay:     return "Delay";
        case Algo::Granular:  return "Granular";
        case Algo::Freeze:    return "Freeze";
        case Algo::PitchRise: return "Pitch Rise";
        case Algo::PitchFall: return "Pitch Fall";
        case Algo::Filter:    return "Filter Sweep";
        case Algo::Texture:   return "Texture";
        case Algo::Crush:     return "Bit Crush";
        case Algo::Distort:   return "Distort";
        case Algo::Scramble:  return "Scramble";
        case Algo::Spectral:  return "Spectral Smear";
        case Algo::Comb:      return "Comb";
        case Algo::TapeStop:  return "Tape Stop";
        case Algo::NumAlgos:
        default:              return "-";
    }
}

const char* algoWeightID (Algo a) noexcept
{
    static constexpr const char* ids[numAlgos] =
    {
        "wReverse", "wStutter", "wDelay", "wGranular", "wFreeze", "wPitchRise", "wPitchFall",
        "wFilter", "wTexture", "wCrush", "wDistort", "wScramble", "wSpectral", "wComb", "wTapeStop"
    };

    const auto i = (int) a;
    return i >= 0 && i < numAlgos ? ids[i] : "";
}

double divisionInBeats (int index) noexcept
{
    switch ((Division) index)
    {
        case Division::Whole:         return 4.0;
        case Division::Half:          return 2.0;
        case Division::Quarter:       return 1.0;
        case Division::Eighth:        return 0.5;
        case Division::Sixteenth:     return 0.25;
        case Division::ThirtySecond:  return 0.125;
        case Division::QuarterT:      return 1.0  * 2.0 / 3.0;
        case Division::EighthT:       return 0.5  * 2.0 / 3.0;
        case Division::SixteenthT:    return 0.25 * 2.0 / 3.0;
        case Division::QuarterD:      return 1.0  * 1.5;
        case Division::EighthD:       return 0.5  * 1.5;
        case Division::SixteenthD:    return 0.25 * 1.5;
        case Division::NumDivisions:
        default:                      return 0.5;
    }
}

juce::StringArray divisionNames()
{
    return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
             "1/4T", "1/8T", "1/16T", "1/4.", "1/8.", "1/16." };
}

//==============================================================================
namespace
{
    using APF   = juce::AudioParameterFloat;
    using APC   = juce::AudioParameterChoice;
    using APB   = juce::AudioParameterBool;
    using APInt = juce::AudioParameterInt;
    using Attr  = juce::AudioParameterFloatAttributes;

    constexpr int kVersionHint = 1;

    juce::ParameterID makeID (const char* id) { return { id, kVersionHint }; }

    std::unique_ptr<APF> percent (const char* id, const char* name, float def)
    {
        return std::make_unique<APF> (makeID (id), name,
                                      juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), def,
                                      Attr().withLabel ("%")
                                            .withStringFromValueFunction ([] (float v, int)
                                                { return juce::String (v, 1) + " %"; }));
    }

    std::unique_ptr<APF> millis (const char* id, const char* name, float lo, float hi,
                                 float def, float centre)
    {
        auto r = juce::NormalisableRange<float> (lo, hi);
        r.setSkewForCentre (centre);
        return std::make_unique<APF> (makeID (id), name, r, def,
                                      Attr().withLabel ("ms")
                                            .withStringFromValueFunction ([] (float v, int)
                                                { return juce::String (v, v < 100.0f ? 1 : 0) + " ms"; }));
    }

    std::unique_ptr<APF> hertz (const char* id, const char* name, float lo, float hi,
                                float def, float centre, bool offAtMinimum)
    {
        auto r = juce::NormalisableRange<float> (lo, hi);
        r.setSkewForCentre (centre);
        return std::make_unique<APF> (makeID (id), name, r, def,
                                      Attr().withLabel ("Hz")
                                            .withStringFromValueFunction ([offAtMinimum] (float v, int)
                                            {
                                                if (offAtMinimum && v < 20.5f) return juce::String ("Off");
                                                if (v >= 1000.0f)              return juce::String (v / 1000.0f, 2) + " kHz";
                                                return juce::String (v, 0) + " Hz";
                                            }));
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&layout] (auto p) { layout.add (std::move (p)); };

    // ---------------- detection ----------------
    add (std::make_unique<APC> (makeID ("detMode"), "Detector Mode",
                                juce::StringArray { "Transient", "Phrase", "Spectral" }, 0));
    add (std::make_unique<APC> (makeID ("detSource"), "Detector Source",
                                juce::StringArray { "Main", "Sidechain", "Hybrid" }, 0));
    add (percent ("sensitivity", "Sensitivity", 50.0f));
    add (percent ("gapThresh",   "Gap Threshold", 50.0f));
    add (millis  ("minGap",      "Minimum Gap",     10.0f,  500.0f,  60.0f, 100.0f));
    add (millis  ("maxFill",     "Max Fill Length", 20.0f, 2000.0f, 400.0f, 400.0f));
    add (std::make_unique<APB> (makeID ("maxFillSync"), "Max Fill Sync", false));
    add (std::make_unique<APC> (makeID ("maxFillDiv"), "Max Fill Division", divisionNames(),
                                (int) Division::Quarter));
    add (millis  ("gapRelease",  "Gap Confirm",     5.0f,  500.0f,  80.0f, 100.0f));
    add (millis  ("protectHold", "Protect Hold",    0.0f,  300.0f,  40.0f,  60.0f));
    add (millis  ("retrigGuard", "Retrigger Guard", 5.0f,  250.0f,  30.0f,  50.0f));
    add (millis  ("lookahead",   "Lookahead",       0.0f,  250.0f,  40.0f,  60.0f));
    add (std::make_unique<APB> (makeID ("audition"), "Audition Detector", false));

    // ---------------- creative macros ----------------
    add (percent ("fill",       "Fill",       35.0f));
    add (percent ("intensity",  "Intensity",  50.0f));
    add (percent ("complexity", "Complexity", 40.0f));
    add (percent ("chaos",      "Chaos",      25.0f));
    add (percent ("density",    "Density",    65.0f));

    juce::StringArray fillTypes { "Auto" };
    for (int i = 0; i < numAlgos; ++i)
        fillTypes.add (algoName ((Algo) i));
    add (std::make_unique<APC> (makeID ("fillType"), "Fill Type", fillTypes, 0));

    add (std::make_unique<APInt> (makeID ("seed"), "Seed", 0, 9999, 1));
    add (std::make_unique<APB> (makeID ("syncEnable"), "Tempo Sync", true));
    add (std::make_unique<APC> (makeID ("stutterDiv"), "Stutter Division", divisionNames(),
                                (int) Division::Sixteenth));
    add (std::make_unique<APC> (makeID ("delayDiv"), "Delay Division", divisionNames(),
                                (int) Division::Sixteenth));
    add (std::make_unique<APB> (makeID ("pingPong"), "Ping Pong", true));
    add (std::make_unique<APB> (makeID ("wetMono"), "Mono Wet", false));
    add (std::make_unique<APF> (makeID ("width"), "Wet Width",
                                juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                Attr().withLabel ("%")));

    // ---------------- auto-mode algorithm weights ----------------
    struct WeightDef { const char* id; const char* name; float def; };
    static const WeightDef weights[] =
    {
        { "wReverse",   "Weight Reverse",    70.0f },
        { "wStutter",   "Weight Stutter",    70.0f },
        { "wDelay",     "Weight Delay",      60.0f },
        { "wGranular",  "Weight Granular",   55.0f },
        { "wFreeze",    "Weight Freeze",     35.0f },
        { "wPitchRise", "Weight Pitch Rise", 40.0f },
        { "wPitchFall", "Weight Pitch Fall", 40.0f },
        { "wFilter",    "Weight Filter",     50.0f },
        { "wTexture",   "Weight Texture",    35.0f },
        { "wCrush",     "Weight Crush",      30.0f },
        { "wDistort",   "Weight Distort",    30.0f },
        { "wScramble",  "Weight Scramble",   40.0f },
        { "wSpectral",  "Weight Spectral",   30.0f },
        { "wComb",      "Weight Comb",       20.0f },
        { "wTapeStop",  "Weight Tape Stop",  30.0f },
    };
    static_assert (sizeof (weights) / sizeof (weights[0]) == (size_t) numAlgos,
                   "weight table must cover every algorithm");
    for (const auto& w : weights)
        add (percent (w.id, w.name, w.def));

    // ---------------- protection ----------------
    add (percent ("protTrans",  "Transient Protect",  60.0f));
    add (hertz   ("protLow",    "Low Protect", 20.0f, 500.0f, 20.0f, 120.0f, true));
    add (percent ("protCenter", "Center Protect",      0.0f));
    add (percent ("protNext",   "Next Event Protect", 70.0f));
    add (percent ("protSC",     "Sidechain Protect",  70.0f));

    // ---------------- spectral lanes ----------------
    add (hertz ("xoverLM", "Low/Mid Crossover",  60.0f,  1000.0f,  180.0f,  250.0f, false));
    add (hertz ("xoverMH", "Mid/High Crossover", 900.0f, 12000.0f, 3500.0f, 3000.0f, false));

    add (std::make_unique<APB> (makeID ("bandLowOn"),  "Low Band On",  true));
    add (std::make_unique<APB> (makeID ("bandMidOn"),  "Mid Band On",  true));
    add (std::make_unique<APB> (makeID ("bandHighOn"), "High Band On", true));

    add (percent ("bandLowAmt",   "Low Fill Amount",   80.0f));
    add (percent ("bandMidAmt",   "Mid Fill Amount",  100.0f));
    add (percent ("bandHighAmt",  "High Fill Amount", 100.0f));
    add (percent ("bandLowInt",   "Low Intensity",     50.0f));
    add (percent ("bandMidInt",   "Mid Intensity",     50.0f));
    add (percent ("bandHighInt",  "High Intensity",    50.0f));
    add (percent ("bandLowProb",  "Low Probability",   45.0f));
    add (percent ("bandMidProb",  "Mid Probability",   75.0f));
    add (percent ("bandHighProb", "High Probability",  90.0f));

    add (std::make_unique<APC> (makeID ("bandSolo"), "Band Solo",
                                juce::StringArray { "Off", "Low", "Mid", "High" }, 0));

    // ---------------- mix ----------------
    add (percent ("dry", "Dry", 100.0f));
    add (percent ("wet", "Wet", 100.0f));
    add (std::make_unique<APF> (makeID ("output"), "Output",
                                juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
                                Attr().withLabel ("dB")
                                      .withStringFromValueFunction ([] (float v, int)
                                          { return juce::String (v, 1) + " dB"; })));
    add (std::make_unique<APB> (makeID ("bypass"), "Bypass", false));

    // ---------------- performance ----------------
    add (std::make_unique<APB> (makeID ("capture"), "Capture", false));

    return layout;
}

juce::StringArray allParameterIDs()
{
    juce::StringArray ids;
   #define GAPFILL_ID(member, id) ids.add (id);
    GAPFILL_PARAMS (GAPFILL_ID)
   #undef GAPFILL_ID
    return ids;
}

juce::StringArray creativeParameterIDs()
{
    juce::StringArray ids { "fill", "intensity", "complexity", "chaos", "density",
                            "fillType", "seed", "stutterDiv", "delayDiv", "pingPong", "width" };

    for (int i = 0; i < numAlgos; ++i)
        ids.add (algoWeightID ((Algo) i));

    return ids;
}

juce::StringArray randomisableParameterIDs()
{
    auto ids = creativeParameterIDs();

    for (const char* extra : { "detMode", "sensitivity", "gapThresh", "minGap", "maxFill",
                               "gapRelease", "protectHold", "retrigGuard",
                               "protTrans", "protCenter", "protNext",
                               "bandLowAmt", "bandMidAmt", "bandHighAmt",
                               "bandLowInt", "bandMidInt", "bandHighInt",
                               "bandLowProb", "bandMidProb", "bandHighProb" })
        ids.add (extra);

    return ids;
}

} // namespace gapfill
