#pragma once

// U-2 Fill Export: the message-thread half (file names, WAV writing, error text).
// docs/dev/05-SPEC-USABILITY-VALUE.md U-2. Never called from the audio thread.

#include "ExportService.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace gapfill::exporting
{

/** `GapFill_<Algo>_<bpm>bpm_<yyyyMMdd-HHmmss>.wav`; the tempo part is left out when the host gave none. */
juce::String defaultFileName (const juce::String& algoName, double bpm, juce::Time when);

/** 24-bit WAV, stereo, at `sampleRate`. False and a user-facing reason on any failure. */
bool writeWav24 (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate, juce::String& error);

/** User-facing text for a failed export ("That fill is too old", ...). */
juce::String failureText (Failure);
juce::String requestText (RequestResult);

} // namespace gapfill::exporting
