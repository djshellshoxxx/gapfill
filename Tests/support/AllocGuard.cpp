#include "AllocGuard.h"

#include <cstdlib>
#include <new>

namespace
{
    // Constant-initialised thread-locals: no dynamic initialisation, so they are
    // safe to touch from operator new at any point of start-up.
    thread_local long tlAllocCount = 0;
    thread_local long tlAllocBytes = 0;
    thread_local int  tlGuardDepth = 0;

    inline void note (std::size_t size) noexcept
    {
        if (tlGuardDepth > 0)
        {
            ++tlAllocCount;
            tlAllocBytes += (long) size;
        }
    }

    inline void* allocate (std::size_t size)
    {
        note (size);
        void* p = std::malloc (size != 0 ? size : 1);

        if (p == nullptr)
            throw std::bad_alloc();

        return p;
    }

    inline void* allocateAligned (std::size_t size, std::size_t alignment)
    {
        note (size);

        if (alignment < sizeof (void*))
            alignment = sizeof (void*);

        // aligned_alloc requires size to be a multiple of the alignment.
        const auto rounded = ((size != 0 ? size : 1) + alignment - 1) / alignment * alignment;
       #if defined (_MSC_VER)
        void* p = _aligned_malloc (rounded, alignment);
       #else
        void* p = std::aligned_alloc (alignment, rounded);
       #endif

        if (p == nullptr)
            throw std::bad_alloc();

        return p;
    }

    inline void release (void* p) noexcept            { std::free (p); }

    inline void releaseAligned (void* p) noexcept
    {
       #if defined (_MSC_VER)
        _aligned_free (p);
       #else
        std::free (p);
       #endif
    }
}

namespace gftest
{

AllocGuard::AllocGuard() noexcept
{
    ++tlGuardDepth;
    startCount = tlAllocCount;
    startBytes = tlAllocBytes;
}

AllocGuard::~AllocGuard()                    { --tlGuardDepth; }
long AllocGuard::count() const noexcept      { return tlAllocCount - startCount; }
long AllocGuard::bytes() const noexcept      { return tlAllocBytes - startBytes; }

} // namespace gftest

void* operator new (std::size_t size)                                   { return allocate (size); }
void* operator new[] (std::size_t size)                                 { return allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}
void* operator new (std::size_t size, std::align_val_t a)               { return allocateAligned (size, (std::size_t) a); }
void* operator new[] (std::size_t size, std::align_val_t a)             { return allocateAligned (size, (std::size_t) a); }

void operator delete (void* p) noexcept                                 { release (p); }
void operator delete[] (void* p) noexcept                               { release (p); }
void operator delete (void* p, std::size_t) noexcept                    { release (p); }
void operator delete[] (void* p, std::size_t) noexcept                  { release (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept          { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept       { release (p); }
void operator delete (void* p, std::align_val_t) noexcept               { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t) noexcept             { releaseAligned (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept  { releaseAligned (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { releaseAligned (p); }
