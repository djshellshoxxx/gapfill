#include "glitch.h"
#include "ParamBuilders.h"
#include "../features/glitch/GlitchTypes.h"

namespace gapfill
{
namespace
{
    // Parameters added after 0.0.2 carry AU version hint 4.
    constexpr int kHint = 4;

    juce::StringArray glitchDivisionNames()
    {
        juce::StringArray names;
        for (const auto* name : glitch::kDivNames)
            names.add (name);
        return names;
    }

    std::unique_ptr<juce::AudioParameterFloat> pct (const char* id, const char* name, float lo, float hi, float def)
    {
        using namespace builders;
        return std::make_unique<APF> (makeID (id, kHint), name, juce::NormalisableRange<float> (lo, hi, 0.1f), def,
                                      Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int)
                                          { return juce::String (v, 0) + " %"; }));
    }

    std::unique_ptr<juce::AudioParameterFloat> ms (const char* id, const char* name, float lo, float hi, float def,
                                                   float centre)
    {
        using namespace builders;
        juce::NormalisableRange<float> range (lo, hi);
        range.setSkewForCentre (centre);
        return std::make_unique<APF> (makeID (id, kHint), name, range, def,
                                      Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
                                          { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }));
    }

    std::unique_ptr<juce::AudioParameterFloat> hz (const char* id, const char* name, float lo, float hi, float def,
                                                   float centre)
    {
        using namespace builders;
        juce::NormalisableRange<float> range (lo, hi);
        range.setSkewForCentre (centre);
        return std::make_unique<APF> (makeID (id, kHint), name, range, def,
                                      Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
                                      {
                                          if (v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
                                          return juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
                                      }));
    }

    std::unique_ptr<juce::AudioParameterFloat> degrees (const char* id, const char* name, float def)
    {
        using namespace builders;
        return std::make_unique<APF> (makeID (id, kHint), name, juce::NormalisableRange<float> (0.0f, 180.0f, 1.0f), def,
                                      Attr().withLabel ("deg").withStringFromValueFunction ([] (float v, int)
                                          { return juce::String (v, 0) + " deg"; }));
    }
}

// F-G01 (docs/dev/features/F-G01-glitch-rack.md). Neutral = fg01_on off; every module is off by default too.
void addGlitchParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace builders;
    const auto divisions = glitchDivisionNames();
    const juce::StringArray clockModes { "Off", "Tempo", "Free" };
    const auto choice = [&] (const char* id, const char* name, const juce::StringArray& items, int def)
    { layout.add (std::make_unique<APC> (makeID (id, kHint), name, items, def)); };
    const auto toggle = [&] (const char* id, const char* name, bool def)
    { layout.add (std::make_unique<APB> (makeID (id, kHint), name, def)); };
    const auto integer = [&] (const char* id, const char* name, int lo, int hi, int def)
    { layout.add (std::make_unique<APInt> (makeID (id, kHint), name, lo, hi, def)); };

    // Rack and timing.
    toggle ("fg01_on", "Glitch On", false);
    choice ("fg01_target", "Glitch Target", { "Fills only", "Whole output" }, 0);
    layout.add (pct ("fg01_mix", "Glitch Mix", 0.0f, 100.0f, 100.0f));
    layout.add (pct ("fg01_swing", "Glitch Swing", 0.0f, 100.0f, 0.0f));
    layout.add (pct ("fg01_timeVar", "Glitch Timing Variation", 0.0f, 100.0f, 0.0f));

    // Stutter.
    choice ("fg01_stutMode", "Stutter Mode", clockModes, 0);
    choice ("fg01_stutDiv", "Stutter Slice", divisions, (int) glitch::Div::D16);
    layout.add (ms ("fg01_stutMs", "Stutter Slice Time", 10.0f, 2000.0f, 125.0f, 150.0f));
    integer ("fg01_stutRepeats", "Stutter Repeats", 1, 16, 4);
    layout.add (pct ("fg01_stutProb", "Stutter Probability", 0.0f, 100.0f, 50.0f));
    layout.add (pct ("fg01_stutRev", "Stutter Reverse", 0.0f, 100.0f, 0.0f));
    layout.add (pct ("fg01_stutVar", "Stutter Variation", 0.0f, 100.0f, 0.0f));
    layout.add (pct ("fg01_stutPick", "Stutter Slice Pick", 0.0f, 100.0f, 0.0f));
    layout.add (pct ("fg01_stutGate", "Stutter Gate", 5.0f, 100.0f, 100.0f));
    layout.add (pct ("fg01_stutDecay", "Stutter Decay", 0.0f, 100.0f, 0.0f));
    layout.add (ms ("fg01_stutSmooth", "Stutter Smoothing", 0.5f, 20.0f, 3.0f, 4.0f));

    // STAB macro.
    toggle ("fg01_stab", "STAB", false);
    toggle ("fg01_stabInf", "STAB Infinite", false);
    choice ("fg01_stabDiv", "STAB Slice", divisions, (int) glitch::Div::D16);
    integer ("fg01_stabRepeats", "STAB Repeats", 1, 32, 4);
    layout.add (pct ("fg01_stabDecay", "STAB Decay", 0.0f, 100.0f, 0.0f));
    layout.add (pct ("fg01_stabGate", "STAB Gate", 5.0f, 100.0f, 100.0f));
    layout.add (ms ("fg01_stabRelease", "STAB Release", 1.0f, 500.0f, 30.0f, 50.0f));

    // Reverse.
    choice ("fg01_revMode", "Reverse Mode", clockModes, 0);
    choice ("fg01_revDiv", "Reverse Length", divisions, (int) glitch::Div::D4);
    layout.add (ms ("fg01_revMs", "Reverse Time", 20.0f, 4000.0f, 500.0f, 400.0f));
    layout.add (pct ("fg01_revProb", "Reverse Probability", 0.0f, 100.0f, 100.0f));
    layout.add (pct ("fg01_revMix", "Reverse Mix", 0.0f, 100.0f, 100.0f));
    layout.add (ms ("fg01_revFade", "Reverse Fade", 1.0f, 50.0f, 8.0f, 10.0f));

    // Microgator.
    choice ("fg01_gateMode", "Microgator Mode", clockModes, 0);
    choice ("fg01_gateDiv", "Microgator Rate", divisions, (int) glitch::Div::D32);
    layout.add (hz ("fg01_gateHz", "Microgator Free Rate", 0.5f, 64.0f, 16.0f, 8.0f));
    layout.add (pct ("fg01_gateLen", "Microgator Length", 5.0f, 100.0f, 50.0f));
    layout.add (pct ("fg01_gateDepth", "Microgator Depth", 0.0f, 100.0f, 100.0f));
    layout.add (ms ("fg01_gateAttack", "Microgator Attack", 0.1f, 20.0f, 1.0f, 2.0f));
    layout.add (ms ("fg01_gateRelease", "Microgator Release", 0.1f, 50.0f, 3.0f, 5.0f));
    choice ("fg01_gatePattern", "Microgator Pattern",
            { "Straight", "Offbeat", "Dotted", "Tresillo", "Euclid 5/8", "Gallop", "Random" }, 0);
    layout.add (pct ("fg01_gateVar", "Microgator Variation", 0.0f, 100.0f, 0.0f));
    layout.add (pct ("fg01_gateStereo", "Microgator Stereo Offset", 0.0f, 100.0f, 0.0f));

    // HP/LP auto-sweep.
    choice ("fg01_filtMode", "Sweep Filter", { "Off", "Low-pass", "High-pass", "Band-pass" }, 0);
    toggle ("fg01_filtSync", "Sweep Tempo Sync", true);
    choice ("fg01_filtDiv", "Sweep Cycle", divisions, (int) glitch::Div::Bar1);
    layout.add (hz ("fg01_filtHz", "Sweep Free Rate", 0.01f, 20.0f, 0.25f, 0.5f));
    layout.add (hz ("fg01_filtMin", "Sweep Minimum", 20.0f, 20000.0f, 200.0f, 1000.0f));
    layout.add (hz ("fg01_filtMax", "Sweep Maximum", 20.0f, 20000.0f, 8000.0f, 1000.0f));
    layout.add (pct ("fg01_filtDepth", "Sweep Depth", 0.0f, 100.0f, 100.0f));
    layout.add (pct ("fg01_filtRes", "Sweep Resonance", 0.0f, 100.0f, 20.0f));
    choice ("fg01_filtShape", "Sweep Shape", { "Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Random" }, 0);
    layout.add (degrees ("fg01_filtStereo", "Sweep Stereo Phase", 0.0f));

    // Phaser.
    layout.add (pct ("fg01_phMix", "Phaser Mix", 0.0f, 100.0f, 0.0f));
    choice ("fg01_phStages", "Phaser Stages", { "2", "4", "6", "8", "12" }, 1);
    toggle ("fg01_phSync", "Phaser Tempo Sync", false);
    choice ("fg01_phDiv", "Phaser Cycle", divisions, (int) glitch::Div::Bar1);
    layout.add (hz ("fg01_phHz", "Phaser Rate", 0.01f, 10.0f, 0.3f, 0.5f));
    layout.add (pct ("fg01_phDepth", "Phaser Depth", 0.0f, 100.0f, 70.0f));
    layout.add (pct ("fg01_phFb", "Phaser Feedback", -95.0f, 95.0f, 30.0f));
    layout.add (hz ("fg01_phCentre", "Phaser Centre", 50.0f, 5000.0f, 800.0f, 800.0f));
    layout.add (degrees ("fg01_phStereo", "Phaser Stereo Phase", 90.0f));

    // Phase and pan inversion.
    toggle ("fg01_polL", "Invert Left Polarity", false);
    toggle ("fg01_polR", "Invert Right Polarity", false);
    layout.add (pct ("fg01_panInv", "Pan Inversion", 0.0f, 100.0f, 0.0f));
    choice ("fg01_flipMode", "Rhythmic Flip", { "Off", "Swap L/R", "Invert left", "Invert right", "Invert both" }, 0);
    choice ("fg01_flipDiv", "Flip Rate", divisions, (int) glitch::Div::D8);
    layout.add (pct ("fg01_flipProb", "Flip Probability", 0.0f, 100.0f, 50.0f));
}

void addGlitchPerformanceIds (juce::StringArray& ids)
{
    // The STAB trigger is a momentary performance gesture: Random, Surprise and
    // Undo snapshots must never fire or release it.
    ids.add ("fg01_stab");
}

} // namespace gapfill
