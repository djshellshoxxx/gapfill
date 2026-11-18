// Scalar e89aecd characterization, independent of the optimized lag grouping.
#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
#endif
#include "../../Source/features/harmonic/Yin.h"
#include <bit>
#include <cstdint>
#include <vector>

GF_TEST (Harmonic, parallel_lags_match_the_released_scalar_trace_at_four_rates)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto word = [&] (std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i) { hash ^= (value >> (i * 8)) & 255u; hash *= 1099511628211ull; }
    };
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const int decimation = gapfill::harmonic::yinDecimation (sr);
        std::vector<float> input ((std::size_t) (1024 * decimation));
        for (int kind = 0; kind < 32; ++kind)
        {
            std::uint32_t random = 74123u;
            const int period = 13 + kind * 7;
            for (int i = 0; i < 1024; ++i)
            {
                random = random * 1664525u + 1013904223u;
                int value = (i % period) * 512 / period - 256;
                if (kind % 4 == 1) value = (int) ((random >> 16) & 65535u) - 32768;
                if (kind % 4 == 2) value = (i / 64) % 2 != 0 ? 0 : value;
                if (kind % 4 == 3) value = 32;
                for (int j = 0; j < decimation; ++j)
                    input[(std::size_t) (i * decimation + j)] = (float) value / 32768.0f;
            }
            const auto result = gapfill::harmonic::yinDetect (input.data(), (int) input.size(), sr);
            word (std::bit_cast<std::uint32_t> (result.f0Hz));
            word (std::bit_cast<std::uint32_t> (result.confidence));
            word (result.voiced ? 1u : 0u);
        }
    }
    // Recorded by separately compiling unmodified e89aecd. Input uses binary
    // 16-bit fractions, no sin/exp/libm synthesis. Each squared-difference sum
    // fits double precision exactly, so SIMD/FMA contraction cannot change it.
    GF_EXPECT (hash == 13952978631397629858ull);
}
#if defined (GF_CONTEXT_STANDALONE)
int main()
{
    try { for (const auto& [name, fn] : contexttest::cases()) { fn(); std::cout << "PASS " << name << '\n'; } }
    catch (const std::exception& e) { std::cout << "FAIL " << e.what() << '\n'; return 1; }
    return 0;
}
#endif
