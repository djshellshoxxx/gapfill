#include "AllocGuard.h"

#include <cstdlib>
#include <new>

// Optional diagnostics (Linux/glibc, not under a sanitizer, which interposes the allocator itself):
//  * juce::HeapBlock / juce::Array / juce::MidiBuffer allocate with malloc/calloc/realloc, not operator new,
//    so the C allocator entry points are counted too;
//  * GF_ALLOC_TRAP=1 prints a raw backtrace for every guarded allocation (resolve with `addr2line -f -C -e`).
#if defined (__linux__) && defined (__GLIBC__) && ! defined (__SANITIZE_ADDRESS__) && ! defined (__SANITIZE_THREAD__) \
     && ! defined (GAPFILL_NO_MALLOC_INTERPOSE)
 #define GF_INTERPOSE_MALLOC 1
 #include <execinfo.h>
 #include <unistd.h>
 extern "C" void* __libc_malloc (std::size_t);
 extern "C" void* __libc_calloc (std::size_t, std::size_t);
 extern "C" void* __libc_realloc (void*, std::size_t);
#endif

namespace
{
    // Constant-initialised thread-locals: no dynamic initialisation, so they are
    // safe to touch from operator new at any point of start-up.
    thread_local long tlAllocCount = 0;
    thread_local long tlAllocBytes = 0;
    thread_local int  tlGuardDepth = 0;

    thread_local bool tlInTrap = false;

    inline void trap ([[maybe_unused]] std::size_t size) noexcept
    {
       #if defined (GF_INTERPOSE_MALLOC)
        static const bool enabled = [] { const char* e = std::getenv ("GF_ALLOC_TRAP"); return e != nullptr && *e == '1'; }();

        if (enabled && ! tlInTrap)
        {
            tlInTrap = true;
            void* frames[40];
            const int n = backtrace (frames, 40);
            const char head[] = "[alloc-trap]\n";
            if (write (2, head, sizeof (head) - 1) < 0) {}
            backtrace_symbols_fd (frames, n, 2);
            tlInTrap = false;
        }
       #endif
    }

    inline void note (std::size_t size) noexcept
    {
        if (tlGuardDepth > 0)
        {
            ++tlAllocCount;
            tlAllocBytes += (long) size;
            trap (size);
        }
    }

    inline void* allocate (std::size_t size)
    {
       #if ! defined (GF_INTERPOSE_MALLOC)
        note (size);        // with the C allocator interposed, malloc() below does the counting
       #endif
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

#if defined (GF_INTERPOSE_MALLOC)
// operator new above calls std::malloc, which lands here too: count it once.
extern "C"
{
void* malloc (std::size_t size)
{
    if (tlGuardDepth > 0 && ! tlInTrap)
        note (size);

    return __libc_malloc (size);
}

void* calloc (std::size_t n, std::size_t size)
{
    if (tlGuardDepth > 0 && ! tlInTrap)
        note (n * size);

    return __libc_calloc (n, size);
}

void* realloc (void* p, std::size_t size)
{
    if (tlGuardDepth > 0 && ! tlInTrap)
        note (size);

    return __libc_realloc (p, size);
}
}
#endif
