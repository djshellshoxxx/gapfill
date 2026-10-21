#pragma once

// Helpers shared by the parameter group builders (Source/params/<group>.cpp and
// Parameters.cpp): thin factories that give every parameter the same units,
// text conversion and version hint. Pure construction, no state.

#include <juce_audio_processors/juce_audio_processors.h>

namespace gapfill::builders
{

using APF   = juce::AudioParameterFloat;
using APC   = juce::AudioParameterChoice;
using APB   = juce::AudioParameterBool;
using APInt = juce::AudioParameterInt;
using Attr  = juce::AudioParameterFloatAttributes;

/** AU version hint. Parameters added after 0.0.1 should pass a higher hint. */
inline constexpr int kVersionHint = 1;

inline juce::ParameterID makeID (const char* id, int versionHint = kVersionHint)
{
    return { id, versionHint };
}

inline std::unique_ptr<APF> percent (const char* id, const char* name, float def, int versionHint = kVersionHint)
{
    return std::make_unique<APF> (makeID (id, versionHint), name,
                                  juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), def,
                                  Attr().withLabel ("%")
                                        .withStringFromValueFunction ([] (float v, int)
                                            { return juce::String (v, 1) + " %"; }));
}

inline std::unique_ptr<APF> millis (const char* id, const char* name, float lo, float hi,
                                    float def, float centre)
{
    auto r = juce::NormalisableRange<float> (lo, hi);
    r.setSkewForCentre (centre);
    return std::make_unique<APF> (makeID (id), name, r, def,
                                  Attr().withLabel ("ms")
                                        .withStringFromValueFunction ([] (float v, int)
                                            { return juce::String (v, v < 100.0f ? 1 : 0) + " ms"; }));
}

inline std::unique_ptr<APF> hertz (const char* id, const char* name, float lo, float hi,
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
                                        })
                                        .withValueFromStringFunction ([offAtMinimum, lo] (const juce::String& text)
                                        {
                                            const auto t = text.trim();

                                            if (offAtMinimum && t.equalsIgnoreCase ("off"))
                                                return lo;

                                            const auto v = t.getFloatValue();
                                            return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
                                        }));
}

} // namespace gapfill::builders
