#pragma once
// Small test-only runner for JUCE-free context probes.
#include <stdexcept>
#include <utility>
#include <vector>
namespace contexttest
{
    using Case = std::pair<const char*, void (*)()>;
    inline std::vector<Case>& cases() { static std::vector<Case> value; return value; }
    struct Register { Register (const char* name, void (*fn)()) { cases().emplace_back (name, fn); } };
}
#define GF_TEST(group, name) \
    static void test_##group##_##name(); \
    static contexttest::Register register_##group##_##name (#group "." #name, test_##group##_##name); \
    static void test_##group##_##name()
#define GF_EXPECT(c) do { if (! (c)) throw std::runtime_error (#c); } while (false)
