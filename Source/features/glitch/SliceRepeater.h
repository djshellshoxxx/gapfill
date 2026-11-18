#pragma once

// F-G01 Glitch rack: bounded slice capture and repeat (B stutter, I STAB).
//
// Contract
//  - prepare() is the only allocating call; process() and every control call are
//    allocation-free, lock-free and O(1) per sample (beginFromHistory copies at
//    most one slice, bounded by kMaxSliceSeconds).
//  - Memory never grows with repeat count: a STAB held forever reads the same
//    fixed slice buffer.
//  - Every change of what is heard (live -> repeat, repeat -> repeat, repeat ->
//    live) is an equal-gain raised-cosine crossfade of `fadeSamples`.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gapfill::glitch
{

/** Stereo ring of the most recent input samples. */
class HistoryRing
{
public:
    void prepare (int capacitySamples)
    {
        size = std::max (16, capacitySamples);
        left.assign ((std::size_t) size, 0.0f);
        right.assign ((std::size_t) size, 0.0f);
        reset();
    }

    /** O(1): stale samples are never read because read() is bounded by available(). */
    void reset() noexcept
    {
        write = 0;
        written = 0;
    }

    void push (float l, float r) noexcept
    {
        left[(std::size_t) write] = l;
        right[(std::size_t) write] = r;
        write = write + 1 == size ? 0 : write + 1;
        written = std::min<std::int64_t> (written + 1, size);
    }

    /** distance 1 = the sample pushed last. Out-of-range distances read silence. */
    void read (int distance, float& l, float& r) const noexcept
    {
        if (distance < 1 || distance > written)
        {
            l = r = 0.0f;
            return;
        }

        auto i = write - distance;
        if (i < 0)
            i += size;

        l = left[(std::size_t) i];
        r = right[(std::size_t) i];
    }

    std::int64_t available() const noexcept { return written; }
    int capacity() const noexcept { return size; }

private:
    std::vector<float> left, right;
    int size = 16, write = 0;
    std::int64_t written = 0;
};

class SliceRepeater
{
public:
    void prepare (int maxSliceSamples)
    {
        capacity = std::max (16, maxSliceSamples);
        sliceL.assign ((std::size_t) capacity, 0.0f);
        sliceR.assign ((std::size_t) capacity, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        cur = {};
        prev = {};
        fadePos = fadeLen = 0;
        sliceLen = captured = 0;
        capturing = false;
    }

    void setFadeSamples (int n) noexcept { fadeSamples = std::max (1, n); }

    bool isActive() const noexcept { return cur.kind != Kind::Live || fadePos < fadeLen; }
    bool isRepeating() const noexcept { return cur.kind == Kind::Slice; }
    int  getSliceLength() const noexcept { return sliceLen; }

    /** Records the live input into the slice while it keeps sounding (the first unit). */
    void beginLiveCapture (int length) noexcept
    {
        sliceLen = std::clamp (length, 16, capacity);
        captured = 0;
        capturing = true;
        switchTo ({ Kind::Live, 0, false, 1.0f, 0, 0 });
    }

    /** Copies `length` samples that started `distanceBack` samples ago and starts repeating them. */
    bool beginFromHistory (const HistoryRing& ring, int distanceBack, int length, bool reverse, float gain,
                           int gateSamples) noexcept
    {
        length = std::clamp (length, 16, capacity);

        if (distanceBack < length || distanceBack > ring.available())
            return false;

        for (int i = 0; i < length; ++i)
            ring.read (distanceBack - i, sliceL[(std::size_t) i], sliceR[(std::size_t) i]);

        sliceLen = captured = length;
        capturing = false;
        startRepeat (reverse, gain, gateSamples);
        return true;
    }

    /** Starts the next repeat of the slice. Ignored while too little has been captured. */
    void startRepeat (bool reverse, float gain, int gateSamples) noexcept
    {
        if (capturing)
        {
            capturing = false;
            sliceLen = captured;
        }

        if (sliceLen < 16)
            return;

        switchTo ({ Kind::Slice, reverse ? sliceLen - 1 : 0, reverse, gain < 1.0e-6f ? 0.0f : gain,
                    std::clamp (gateSamples, 1, 1 << 30), 0 });
    }

    /** Returns to the live input with a crossfade of `fade` samples. */
    void release (int fade) noexcept
    {
        capturing = false;

        if (cur.kind == Kind::Live)
            return;

        const auto keep = fadeSamples;
        fadeSamples = std::max (1, fade);
        switchTo ({ Kind::Live, 0, false, 1.0f, 0, 0 });
        fadeSamples = keep;
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        if (capturing)
        {
            if (captured < sliceLen)
            {
                sliceL[(std::size_t) captured] = inL;
                sliceR[(std::size_t) captured] = inR;
                ++captured;
            }
            else
            {
                capturing = false;
            }
        }

        float cl, cr;
        render (cur, inL, inR, cl, cr);

        if (fadePos < fadeLen)
        {
            float pl, pr;
            render (prev, inL, inR, pl, pr);
            const auto t = (float) (fadePos + 1) / (float) (fadeLen + 1);
            const auto a = 0.5f - 0.5f * std::cos (3.14159265358979f * t);   // raised cosine, a + (1 - a) == 1
            cl = pl + (cl - pl) * a;
            cr = pr + (cr - pr) * a;
            ++fadePos;
        }

        outL = cl;
        outR = cr;
    }

private:
    enum class Kind { Live, Slice };

    struct Reader
    {
        Kind kind = Kind::Live;
        int pos = 0;
        bool reverse = false;
        float gain = 1.0f;
        int gate = 0;
        int elapsed = 0;
    };

    void switchTo (const Reader& next) noexcept
    {
        prev = cur;
        cur = next;
        fadeLen = fadeSamples;
        fadePos = 0;
    }

    void render (Reader& r, float inL, float inR, float& l, float& r2) noexcept
    {
        if (r.kind == Kind::Live)
        {
            l = inL;
            r2 = inR;
            return;
        }

        const auto i = (std::size_t) std::clamp (r.pos, 0, std::max (0, sliceLen - 1));
        auto g = r.gain;

        // Gate: full level for `gate` samples, then a fade of fadeSamples to silence.
        if (r.elapsed >= r.gate)
        {
            const auto over = r.elapsed - r.gate;
            g *= over >= fadeSamples ? 0.0f : 1.0f - (float) over / (float) fadeSamples;
        }

        l = sliceL[i] * g;
        r2 = sliceR[i] * g;

        ++r.elapsed;
        if (r.reverse)
            r.pos = r.pos <= 0 ? sliceLen - 1 : r.pos - 1;
        else
            r.pos = r.pos + 1 >= sliceLen ? 0 : r.pos + 1;
    }

    std::vector<float> sliceL, sliceR;
    int capacity = 16, sliceLen = 0, captured = 0;
    bool capturing = false;
    Reader cur, prev;
    int fadePos = 0, fadeLen = 0, fadeSamples = 128;
};

} // namespace gapfill::glitch
