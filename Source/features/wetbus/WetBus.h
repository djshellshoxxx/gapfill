#pragma once

// F-L12 Optional "Wet" output bus (docs/dev/12 F-L12).
//
// A second, stereo OUTPUT bus named "Wet", disabled by default, that carries the
// same signal as the F-M17 wet-only output: the engine's wet bus after protection,
// width, mono-wet and wetSafety, at unity, aligned with the main output (no extra
// latency). It is multiplied only by the Panic ramp and (1 - bypass), exactly like
// the wet term of the main mix, so `wetSolo` at Output 0 dB and the Wet bus are the
// same samples. The Dry / Wet / Output knobs do not touch it. Bypass silences it.
//
// Compatibility rules (plugin identity is unchanged):
//  * input buses and their order are untouched: Input, Sidechain;
//  * outputs are Output (bus 0) and Wet (bus 1, disabled by default);
//  * isBusesLayoutSupported keeps every previous rule and additionally accepts the
//    Wet bus as disabled or stereo (never mono, never anything else);
//  * the bus is declared at construction time only when the build option
//    GAPFILL_WET_BUS is on and the wrapper is not Audio Unit (see isDeclared);
//  * bus enable state belongs to the host; GapFill stores nothing about it.
//
// Channel aliasing: JUCE hands the plug-in ONE in-place buffer with
// max (input channels, output channels) channels. Wet-bus channels therefore
// share storage with the sidechain input channels. The processor copies the
// sidechain to its own mono buffer at the top of every chunk, before anything is
// written to an output, and the mix loop overwrites every sample of both channels of
// the bus, so no sidechain data can leak into the Wet output.
//
// Pure helpers only: no state, no allocation, safe on the audio thread.

#include <juce_audio_processors/juce_audio_processors.h>

#ifndef GAPFILL_WET_BUS
 #define GAPFILL_WET_BUS 1
#endif

namespace gapfill::wetbus
{

inline constexpr int kWetBusIndex = 1;    ///< index among the OUTPUT buses

/** True when the "Wet" bus is declared for this wrapper type. AU stays off until auval + Logic pass. */
inline bool isDeclared (juce::AudioProcessor::WrapperType wrapper = juce::PluginHostType::getPluginLoadedAs()) noexcept
{
   #if GAPFILL_WET_BUS
    return wrapper != juce::AudioProcessor::wrapperType_AudioUnit
        && wrapper != juce::AudioProcessor::wrapperType_AudioUnitv3;
   #else
    juce::ignoreUnused (wrapper);
    return false;
   #endif
}

/** Layout rule for the Wet bus: absent, disabled or stereo. At most one extra output bus. */
inline bool isLayoutSupported (const juce::AudioProcessor::BusesLayout& layouts) noexcept
{
    if (layouts.outputBuses.size() > 2)
        return false;

    if (layouts.outputBuses.size() > 1)
    {
        const auto wet = layouts.getChannelSet (false, kWetBusIndex);

        if (! wet.isDisabled() && wet != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

} // namespace gapfill::wetbus
