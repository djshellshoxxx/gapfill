#pragma once

// F-D02 message-thread helpers behind the diagnostics drawer (Record, Save, size,
// privacy note) and Command 29 (EventRecordToggle).

#include <juce_core/juce_core.h>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::events
{

/** Exactly what a recording stores. Shown next to the Record button. */
extern const char* const kPrivacyNote;

bool isRecording (const GapFillAudioProcessor&);

/** Starts a recording of the current state. Fails while one is running. */
juce::Result startRecording (GapFillAudioProcessor&);

void stopRecording (GapFillAudioProcessor&);

/** Toggles (Command 29 / the Record button). */
void toggleRecording (GapFillAudioProcessor&);

/** "Not recording", "Recording: 1,204 events (0.04 MB)" or the full-buffer notice. */
juce::String describeRecording (GapFillAudioProcessor&);

/** Stops, then writes the .gfevents file. */
juce::Result saveRecording (GapFillAudioProcessor&, const juce::File&);

} // namespace gapfill::events
