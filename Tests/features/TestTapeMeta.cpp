#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/tapedamage/TapeMetaCodec.h"

#include <cmath>
#include <limits>

using namespace gapfill;
using namespace gapfill::tapedamage;

GF_TEST (TapeMeta, portable_encoding_roundtrips_decision_seed_and_all_latched_controls)
{
    fx::TapeExtraSettings settings;
    settings.amount01 = 1.0f;
    settings.mix01 = 0.876543f;
    auto meta = latchTapeSettings (settings, 0x1122334455667788ull);
    meta.chosen = 1;
    std::array<std::uint8_t, kTapeMetaBytes> bytes {};
    GF_EXPECT (encodeTapeMeta (meta, bytes));
    GF_EXPECT (bytes[0] == 1 && bytes[1] == 1 && bytes[2] == 0x88 && bytes[9] == 0x11);
    GF_EXPECT (bytes[10] == 0x10 && bytes[11] == 0x27); // Amount10000, not POD memcpy order
    GF_EXPECT (bytes[12] == 0xb8 && bytes[13] == 0x0b); // Wow3000
    TapeMeta restored;
    GF_EXPECT (decodeTapeMeta (bytes, restored) && restored == meta);
    const auto canonical = settingsFromTapeMeta (restored);
    GF_EXPECT (canonical.mix01 == 8765.0f / 10000.0f);
    GF_EXPECT (canonical.amount01 == 1.0f);
    meta.chosen = 0; // retain an explicit unsuccessful chance decision
    GF_EXPECT (encodeTapeMeta (meta, bytes) && decodeTapeMeta (bytes, restored) && restored == meta);
}

GF_TEST (TapeMeta, corrupt_or_truncated_metadata_is_rejected_atomically)
{
    fx::TapeExtraSettings settings;
    settings.amount01 = 0.5f;
    auto meta = latchTapeSettings (settings, 77);
    meta.chosen = 1;
    std::array<std::uint8_t, kTapeMetaBytes> bytes {};
    GF_EXPECT (encodeTapeMeta (meta, bytes));
    for (const int field : { 0, 1, 11, 13, 26, 27 })
    {
        auto invalid = bytes;
        invalid[(std::size_t) field] = 255;
        auto restored = meta;
        GF_EXPECT (! decodeTapeMeta (invalid, restored));
        GF_EXPECT (restored == meta);
    }
    auto restored = meta;
    GF_EXPECT (! decodeTapeMeta (std::span<const std::uint8_t> (bytes).first (27), restored));
    GF_EXPECT (restored == meta);
    auto unchanged = bytes;
    GF_EXPECT (! encodeTapeMeta (meta, std::span<std::uint8_t> (unchanged).first (27)));
    GF_EXPECT (unchanged == bytes);
    auto impossible = meta;
    impossible.values[Mix] = 0;
    GF_EXPECT (! encodeTapeMeta (impossible, unchanged) && unchanged == bytes);
}

GF_TEST (TapeMeta, neutral_invalid_inputs_and_unselected_replay_are_explicit)
{
    fx::TapeExtraSettings settings;
    const auto absent = latchTapeSettings (settings, 999);
    GF_EXPECT (absent == TapeMeta {} && isValidTapeMeta (absent));
    std::array<std::uint8_t, kTapeMetaBytes> bytes {};
    GF_EXPECT (encodeTapeMeta (absent, bytes));
    for (const auto byte : bytes) GF_EXPECT (byte == 0);
    auto decoded = latchTapeSettings (fx::TapeExtraSettings { 1.0f }, 10);
    GF_EXPECT (decodeTapeMeta (bytes, decoded) && decoded == TapeMeta {});
    settings.amount01 = std::numeric_limits<float>::quiet_NaN();
    GF_EXPECT (latchTapeSettings (settings, 1) == TapeMeta {});
    settings.amount01 = 0.5f;
    settings.wow01 = std::numeric_limits<float>::quiet_NaN();
    const auto latched = latchTapeSettings (settings, 1);
    GF_EXPECT (latched.version == 1 && latched.chosen == 0 && latched.values[Wow] == 3000);
}

GF_TEST (TapeMeta, codec_and_quantisation_are_bounded_and_allocation_free)
{
    fx::TapeExtraSettings settings;
    settings.amount01 = 0.654321f;
    std::array<std::uint8_t, kTapeMetaBytes> bytes {};
    gftest::AllocGuard guard;
    for (std::uint64_t seed = 0; seed < 10000; ++seed)
    {
        auto meta = latchTapeSettings (settings, seed);
        meta.chosen = 1;
        TapeMeta decoded;
        GF_EXPECT (encodeTapeMeta (meta, bytes) && decodeTapeMeta (bytes, decoded));
        GF_EXPECT (decoded == meta && settingsFromTapeMeta (decoded).amount01 == 6543.0f / 10000.0f);
    }
    GF_EXPECT (guard.count() == 0);
}
