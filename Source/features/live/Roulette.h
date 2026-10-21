#pragma once

// F-S08 Glitch Roulette (docs/dev/12 F-S08). Message thread only.
//
// "Spin" picks one of the existing Surprise profiles with an INJECTED random
// source (production: the system random, tests: a fixed seed), never the same
// profile twice in a row, and applies it through the existing Surprise path:
// one Undo snapshot first, and the same randomisable-parameter rules, because
// the code that moves the parameters is Presets.cpp's surpriseParameters().
// "Restore" is the existing Undo.

#include <juce_audio_processors/juce_audio_processors.h>

namespace gapfill
{

class GapFillAudioProcessor;

namespace live
{

class GlitchRoulette
{
public:
    explicit GlitchRoulette (juce::Random& randomSource) : rng (randomSource) {}

    struct Result
    {
        int profile = -1;
        juce::String name;
    };

    /** Applies the next profile. Pushes exactly one Undo snapshot. */
    Result spin (GapFillAudioProcessor&);

    /** Frames of the 3-slot reel animation; 0 under Reduced Motion. */
    static int animationFrames (bool reducedMotion) noexcept   { return reducedMotion ? 0 : 3; }

    int getLastProfile() const noexcept                         { return last; }
    const juce::String& getLastName() const noexcept            { return lastName; }

private:
    juce::Random& rng;
    int last = -1;
    juce::String lastName;
};

} // namespace live
} // namespace gapfill
