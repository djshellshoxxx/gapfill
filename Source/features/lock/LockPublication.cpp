#include "LockPublication.h"

#include <cstring>
#include <limits>

namespace gapfill::lock
{
namespace
{
    constexpr size_t kBytes = 80;
    void put64 (std::uint8_t* bytes, std::uint64_t value) noexcept
    {
        for (int i = 0; i < 8; ++i)
            bytes[i] = (std::uint8_t) (value >> (i * 8));
    }
    std::uint64_t get64 (const std::uint8_t* bytes) noexcept
    {
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
            value |= (std::uint64_t) bytes[i] << (i * 8);
        return value;
    }
    LockSnapshot normalized (LockSnapshot value) noexcept
    {
        if (! value.valid)
        { value.recipe = {}; value.extras = {}; }
        else
            value.extras = normalizeRecipeExtrasForAlgorithm (value.recipe.algo, value.extras);
        return value;
    }
    bool pack (const LockSnapshot& value, std::array<std::uint8_t, kBytes>& bytes) noexcept
    {
        if (! isValid (value.recipe) || ! isValid (value.extras))
            return false;
        put64 (bytes.data(), value.restoreEpoch);
        put64 (bytes.data() + 8, value.generation);
        bytes[16] = value.valid ? 1 : 0;
        bytes[17] = 1; // internal canonical schema; bytes18/19 remain zero
        std::uint8_t recipe[FillRecipe::serialisedBytes];
        gapfill::toBytes (value.recipe, recipe);
        std::memcpy (bytes.data() + 20, recipe, sizeof recipe);
        return encodeRecipeExtras (value.extras, std::span (bytes).subspan (36, kRecipeExtrasBytes));
    }
    bool unpack (const std::array<std::uint8_t, kBytes>& bytes, LockSnapshot& out) noexcept
    {
        if (bytes[16] > 1 || bytes[17] != 1 || bytes[18] != 0 || bytes[19] != 0)
            return false;
        LockSnapshot value;
        value.restoreEpoch = get64 (bytes.data());
        value.generation = get64 (bytes.data() + 8);
        value.valid = bytes[16] != 0;
        std::uint8_t recipe[FillRecipe::serialisedBytes];
        std::memcpy (recipe, bytes.data() + 20, sizeof recipe);
        value.recipe = gapfill::fromBytes (recipe);
        if (! isValid (value.recipe)
            || ! decodeRecipeExtras (std::span (bytes).subspan (36, kRecipeExtrasBytes), value.extras))
            return false;
        out = value;
        return true;
    }
}

bool LockPublication::Bank::write (const LockSnapshot& value) noexcept
{
    std::array<std::uint8_t, kBytes> bytes {};
    const auto before = sequence.load();
    if (before >= std::numeric_limits<std::uint64_t>::max() - 1 || ! pack (value, bytes))
        return false;
    // Sequential consistency supplies one order across sequence and payload,
    // including weak-memory targets. No reader touches non-atomic payload data.
    sequence.store (before + 1);
    for (size_t i = 0; i < kWords; ++i)
        words[i].store (get64 (bytes.data() + i * 8));
    sequence.store (before + 2);
    return true;
}

bool LockPublication::Bank::readOnce (LockSnapshot& out) const noexcept
{
    const auto before = sequence.load();
    if ((before & 1u) != 0)
        return false;
    if (before == 0)
    {
        if (sequence.load() != 0)
            return false;
        out = {};
        return true;
    }
    std::array<std::uint8_t, kBytes> bytes {};
    for (size_t i = 0; i < kWords; ++i)
        put64 (bytes.data() + i * 8, words[i].load());
    if (sequence.load() != before)
        return false;
    return unpack (bytes, out);
}

LockPublication::LockPublication() noexcept
{
    LockSnapshot initial;
    initial.restoreEpoch = 1;
    if (! restored.write (initial))
        captureFailed.store (true);
}

std::uint64_t LockPublication::beginRestore (const LockSnapshot& requested)
{
    const auto guard = std::scoped_lock (restoreWriter); // never called on audio
    auto value = normalized (requested);
    const auto previous = restoreEpoch.load();
    if (previous == std::numeric_limits<std::uint64_t>::max())
    { captureFailed.store (true); return 0; }
    value.restoreEpoch = previous + 1;
    value.generation = 0;
    if (! restored.write (value))
    { captureFailed.store (true); return 0; }
    restoreEpoch.store (value.restoreEpoch);
    captureFailed.store (false);
    return value.restoreEpoch;
}

bool LockPublication::publishAudio (const LockSnapshot& requested, std::uint64_t acceptedEpoch) noexcept
{
    if (acceptedEpoch != restoreEpoch.load())
        return false;
    auto value = normalized (requested);
    if (audioGeneration == std::numeric_limits<std::uint64_t>::max())
    { captureFailed.store (true); return false; }
    value.restoreEpoch = acceptedEpoch;
    value.generation = audioGeneration + 1;
    if (! audio.write (value))
    { captureFailed.store (true); return false; }
    audioGeneration = value.generation;
    captureFailed.store (false);
    return true;
}

bool LockPublication::pendingRestore (std::uint64_t acceptedEpoch, LockSnapshot& out) const noexcept
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto epoch = restoreEpoch.load();
        if (acceptedEpoch == epoch)
            return false;
        LockSnapshot value;
        if (restored.readOnce (value) && value.restoreEpoch == epoch && restoreEpoch.load() == epoch)
        { out = value; return true; }
    }
    return false;
}

bool LockPublication::tryLatest (LockSnapshot& out) const noexcept
{
    if (captureFailed.load())
        return false;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto epoch = restoreEpoch.load();
        LockSnapshot played, baseline;
        if (! audio.readOnce (played) || ! restored.readOnce (baseline) || restoreEpoch.load() != epoch)
            continue;
        if (captureFailed.load())
            return false;
        if (played.restoreEpoch == epoch && played.generation != 0)
        { out = played; return true; }
        if (baseline.restoreEpoch == epoch)
        { out = baseline; return true; }
    }
    return false;
}
} // namespace gapfill::lock
