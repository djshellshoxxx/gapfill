#pragma once

// Test-only shared harness for pure co-op probes; production has no test hooks.
#include <stdexcept>
#include <utility>
#include <vector>

namespace cooptest
{
    using Case = std::pair<const char*, void (*)()>;
    inline std::vector<Case>& cases() { static std::vector<Case> list; return list; }
    struct Register { Register (const char* n, void (*f)()) { cases().emplace_back (n, f); } };
}

#define GF_TEST(group, name) \
    static void test_##group##_##name(); \
    static cooptest::Register register_##group##_##name (#group "." #name, test_##group##_##name); \
    static void test_##group##_##name()
#define GF_EXPECT(c) do { if (! (c)) throw std::runtime_error (#c); } while (false)
