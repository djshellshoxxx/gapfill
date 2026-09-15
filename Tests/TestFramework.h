#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace gftest
{

struct TestCase
{
    juce::String group, name;
    std::function<void()> fn;
};

/** Minimal assertion-based test registry. Tests throw on failure. */
class Registry
{
public:
    static Registry& get();
    void add (const juce::String& group, const juce::String& name, std::function<void()> fn);
    int run (const juce::String& filter);
    const std::vector<TestCase>& cases() const { return tests; }
private:
    std::vector<TestCase> tests;
};

struct Registrar
{
    Registrar (const char* group, const char* name, std::function<void()> fn)
    {
        Registry::get().add (group, name, std::move (fn));
    }
};

struct TestFailure : public std::exception
{
    explicit TestFailure (juce::String m) : message (std::move (m)) {}
    const char* what() const noexcept override { return message.toRawUTF8(); }
    juce::String message;
};

void failTest (const juce::String& message, const char* file, int line);

#define GF_TEST(group, name) \
    static void gfTestFn_##group##_##name(); \
    static gftest::Registrar gfTestReg_##group##_##name (#group, #name, gfTestFn_##group##_##name); \
    static void gfTestFn_##group##_##name()

#define GF_EXPECT(cond) \
    do { if (! (cond)) gftest::failTest (juce::String ("expected: ") + #cond, __FILE__, __LINE__); } while (false)

#define GF_EXPECT_MSG(cond, msg) \
    do { if (! (cond)) gftest::failTest (juce::String (msg) + " | " + #cond, __FILE__, __LINE__); } while (false)

#define GF_EXPECT_NEAR(a, b, tol) \
    do { const double va = (double) (a), vb = (double) (b); \
         if (! (std::abs (va - vb) <= (double) (tol))) \
            gftest::failTest (juce::String (#a) + " (" + juce::String (va) + ") != " + #b + " (" + juce::String (vb) + ")", __FILE__, __LINE__); } while (false)

} // namespace gftest
