#pragma once

#include <cstdint>

namespace gapfill::models
{
/** Sound-affecting model choices, saved with parameter snapshots. Old states
    without this metadata always select legacy, even when a feature is off. */
struct ModelVersions
{
    std::uint8_t cloud = 2;
    std::uint8_t taste = 2;
    std::uint8_t arrangement = 2;

    static constexpr ModelVersions legacy() noexcept { return { 1, 1, 1 }; }

    constexpr bool valid() const noexcept
    {
        return cloud >= 1 && cloud <= 2 && taste >= 1 && taste <= 2
            && arrangement >= 1 && arrangement <= 2;
    }

    /** One atomic uint32 can publish a coherent selection to the audio thread. */
    constexpr std::uint32_t packed() const noexcept
    {
        return valid() ? (std::uint32_t) cloud | ((std::uint32_t) taste << 8u)
                                              | ((std::uint32_t) arrangement << 16u)
                       : legacy().packed();
    }

    static constexpr ModelVersions fromPacked (std::uint32_t value) noexcept
    {
        const ModelVersions result { (std::uint8_t) (value & 0xffu),
                                     (std::uint8_t) ((value >> 8u) & 0xffu),
                                     (std::uint8_t) ((value >> 16u) & 0xffu) };
        return (value & 0xff000000u) == 0 && result.valid() ? result : legacy();
    }

    bool operator== (const ModelVersions&) const noexcept = default;
};
} // namespace gapfill::models
