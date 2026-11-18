#pragma once

// Message-thread presentation adapters. The monitor, rule proposals and event
// recorder retain their English/schema data and never call the locale service.

#include "Health.h"

namespace gapfill::fixit { struct Proposal; }
namespace gapfill::events { class EventRecorder; }

namespace gapfill::health
{
Description displayDescription (HealthCode, const DescribeContext& = {});
juce::String proposalText (const fixit::Proposal&);
juce::String proposalSpoken (const fixit::Proposal&);
juce::String telemetryText (int latencySamples, double sampleRate, float cpu01, bool measured);
juce::String recordingText (events::EventRecorder&);
} // namespace gapfill::health
