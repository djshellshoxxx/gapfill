#pragma once

// Allocation counter for the test binary (docs/dev/06 §1).
//
// Tests/support/AllocGuard.cpp replaces the global operator new/delete so that
// a scope object can fail a test when the guarded thread allocates. Only the
// thread that owns the guard is counted, so JUCE message/timer threads running
// in parallel never cause false positives.
//
//     warmUp();                // JUCE internals that allocate on first use
//     gftest::AllocGuard guard;
//     proc.processBlock (...); // must not allocate
//     GF_EXPECT (guard.count() == 0);

namespace gftest
{

class AllocGuard
{
public:
    AllocGuard() noexcept;
    ~AllocGuard();

    AllocGuard (const AllocGuard&) = delete;
    AllocGuard& operator= (const AllocGuard&) = delete;

    /** Allocations made by this thread since the guard was constructed. */
    long count() const noexcept;

    /** Bytes requested by those allocations. */
    long bytes() const noexcept;

private:
    long startCount, startBytes;
};

} // namespace gftest
