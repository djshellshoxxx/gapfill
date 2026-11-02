#pragma once

#include <cstdint>
#include <string>

namespace gapfill::controller
{
inline constexpr int kMaxParameterBindings = 64;
inline constexpr int kMaxIndicators = 16;
inline constexpr int kOutputQueueCapacity = 256;
inline constexpr int kMaxFeedbackOutputs = kMaxParameterBindings + kMaxIndicators;

enum class TakeoverMode : std::uint8_t { Jump = 0, Pickup = 1 };

/** Message-thread configuration. Parameter ids are persisted; audio uses cached indexes. */
struct ParameterBinding
{
    std::string id;
    std::uint8_t cc = 0;
    std::uint8_t channel = 0; // Omni, or 1..16.
    TakeoverMode takeover = TakeoverMode::Jump;
};

struct MappingOptions
{
    std::uint8_t channel = 0;
    TakeoverMode takeover = TakeoverMode::Jump;
};

/** Three wire bytes. The message thread produces; audio only drains within the
    processor's remaining shared 32-event callback budget, after note-offs. */
struct OutEvent
{
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};
}
