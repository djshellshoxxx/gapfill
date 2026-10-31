#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
#endif
#include "../../Source/features/harmonic/psola/Metadata.h"
#include <array>
#include <limits>
using namespace gapfill::harmonic::psola;
GF_TEST (HarmonicMetadata, endian_layout_is_fixed_and_active_decisions_round_trip)
{
    DecisionMetadata original { 1, Outcome::Used, 2, 4, 220, 0.875f };
    std::array<std::uint8_t, 12> bytes {};
    GF_EXPECT (encode (original, bytes));
    constexpr std::array<std::uint8_t, 12> expected { 1,1,2,4,0,0,0x5c,0x43,0,0,0x60,0x3f };
    GF_EXPECT (bytes == expected);
    DecisionMetadata restored;
    GF_EXPECT (decode (bytes, restored) && restored == original);
    for (auto outcome : { Outcome::UnvoicedFallback, Outcome::EpochFallback })
    {
        original.outcome = outcome;
        original.f0Hz = outcome == Outcome::UnvoicedFallback ? 0 : 440;
        GF_EXPECT (encode (original, bytes) && decode (bytes, restored) && restored == original);
    }
}
GF_TEST (HarmonicMetadata, absence_is_explicit_and_unknown_or_invalid_payload_is_dropped)
{
    std::array<std::uint8_t, 12> bytes {};
    DecisionMetadata decoded;
    GF_EXPECT (decode (bytes, decoded) && decoded == DecisionMetadata {});
    bytes[7] = 0x80; // negative zero is not the all-zero absent representation
    GF_EXPECT (! decode (bytes, decoded));
    const DecisionMetadata good { 1, Outcome::Used, 2, 4, 220, 0.875f };
    GF_EXPECT (encode (good, bytes));
    bytes[0] = 2;
    decoded = good;
    GF_EXPECT (! decode (bytes, decoded) && decoded == DecisionMetadata {});
    GF_EXPECT (! decode (std::span<const std::uint8_t> (bytes.data(), 11), decoded));
    bytes[0] = 1; bytes[1] = 99;
    GF_EXPECT (! decode (bytes, decoded));
    GF_EXPECT (encode (good, bytes));
    bytes[4] = 0; bytes[5] = 0; bytes[6] = 0xc0; bytes[7] = 0x7f; // NaN f0
    GF_EXPECT (! decode (bytes, decoded));
}
GF_TEST (HarmonicMetadata, invalid_encode_is_atomic_and_never_reinterprets_legacy_flags)
{
    DecisionMetadata value { 1, Outcome::Used, 0, 1, 220, 1 };
    std::array<std::uint8_t, 12> bytes; bytes.fill (0xaa);
    const auto before = bytes;
    for (float bad : { -1.0f, 49.0f, 1001.0f, std::numeric_limits<float>::infinity() })
    {
        value.f0Hz = bad;
        GF_EXPECT (! encode (value, bytes) && bytes == before);
    }
    value.f0Hz = 220; value.voices = 0;
    GF_EXPECT (! encode (value, bytes) && bytes == before);
    value.voices = 1; value.clarity = 0.79f;
    GF_EXPECT (! encode (value, bytes) && bytes == before);
    value = {}; value.intervals = 1;
    GF_EXPECT (! encode (value, bytes) && bytes == before);
}
#if defined (GF_CONTEXT_STANDALONE)
int main()
{
    int failures = 0;
    for (const auto& [name, fn] : contexttest::cases())
        try { fn(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    return failures != 0 ? 1 : 0;
}
#endif
