// SP-0 Shared spectral processor (docs/dev/12 section 3, WP9).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "dsp/spectral/Stft.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using namespace gapfill;
using namespace gftest;

namespace
{
    struct Identity : spectral::SpectralCallback
    {
        int frames = 0;
        void processFrame (spectral::SpectralFrame&) noexcept override { ++frames; }
    };

    std::vector<std::vector<float>> makeNoiseSignal (int channels, int n, int seed = 5)
    {
        juce::Random rnd (seed);
        std::vector<std::vector<float>> v ((size_t) channels, std::vector<float> ((size_t) n));

        for (auto& ch : v)
            for (auto& x : ch)
                x = (rnd.nextFloat() * 2.0f - 1.0f) * 0.5f;

        return v;
    }

    /** Runs the signal through the processor in blocks of `block`. */
    std::vector<std::vector<float>> run (spectral::Stft& st, spectral::SpectralCallback& cb,
                                         const std::vector<std::vector<float>>& in, int block)
    {
        const auto channels = (int) in.size();
        const auto n = (int) in[0].size();
        auto out = in;

        for (int pos = 0; pos < n; pos += block)
        {
            const auto len = std::min (block, n - pos);
            const float* ip[2];
            float* op[2];

            for (int c = 0; c < channels; ++c)
            {
                ip[c] = in[(size_t) c].data() + pos;
                op[c] = out[(size_t) c].data() + pos;
            }

            st.process (ip, op, len, cb);
        }

        return out;
    }

    double errorDb (const std::vector<float>& ref, const std::vector<float>& out, int latency, int from, int to)
    {
        double e = 0.0, s = 0.0;

        for (int i = from; i < to; ++i)
        {
            const double a = ref[(size_t) i], b = out[(size_t) (i + latency)];
            e += (a - b) * (a - b);
            s += a * a;
        }

        return 10.0 * std::log10 (std::max (1.0e-30, e / std::max (1.0e-30, s)));
    }
}

GF_TEST (Stft, identity_callback_reconstructs)
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        spectral::Stft st;
        st.prepare (sr, 2, spectral::Stft::defaultSize (sr));
        Identity cb;
        const auto n = st.size() * 8;
        const auto in = makeNoiseSignal (2, n + st.latencySamples());
        const auto out = run (st, cb, in, 480);

        for (int c = 0; c < 2; ++c)
        {
            const auto db = errorDb (in[(size_t) c], out[(size_t) c], st.latencySamples(), 0, n);
            GF_EXPECT_MSG (db < -90.0, "sr " + juce::String (sr) + " error " + juce::String (db) + " dB");
        }
    }
}

GF_TEST (Stft, latency_exact)
{
    for (int size : { 512, 1024, 2048, 4096 })
    {
        spectral::Stft st;
        st.prepare (48000.0, 1, size);
        Identity cb;
        const int pos = 3000;
        std::vector<std::vector<float>> in (1, std::vector<float> ((size_t) (pos + size * 2), 0.0f));
        in[0][(size_t) pos] = 1.0f;
        const auto out = run (st, cb, in, 100);

        int argmax = 0;

        for (int i = 0; i < (int) out[0].size(); ++i)
            if (std::abs (out[0][(size_t) i]) > std::abs (out[0][(size_t) argmax]))
                argmax = i;

        GF_EXPECT (st.latencySamples() == size - 1);
        GF_EXPECT_MSG (argmax == pos + st.latencySamples(), "size " + juce::String (size));
        GF_EXPECT_NEAR (out[0][(size_t) argmax], 1.0, 1.0e-4);
    }
}

GF_TEST (Stft, block_size_invariant)
{
    const auto in = makeNoiseSignal (2, 20000);
    std::vector<std::vector<std::vector<float>>> outs;

    for (int block : { 1, 7, 64, 480, 4096 })
    {
        spectral::Stft st;
        st.prepare (48000.0, 2, 1024);
        Identity cb;
        outs.push_back (run (st, cb, in, block));
    }

    for (size_t i = 1; i < outs.size(); ++i)
        GF_EXPECT (outs[i] == outs[0]);                       // bit-exact
}

GF_TEST (Stft, sr_change_reprepare)
{
    spectral::Stft st;
    Identity cb;

    for (double sr : { 48000.0, 96000.0, 192000.0, 44100.0 })
    {
        st.prepare (sr, 2, spectral::Stft::defaultSize (sr));
        const auto n = st.size() * 4;
        const auto in = makeNoiseSignal (2, n + st.latencySamples(), 9);
        const auto out = run (st, cb, in, 256);
        GF_EXPECT (errorDb (in[0], out[0], st.latencySamples(), 0, n) < -90.0);
    }
}

GF_TEST (Stft, impulse_no_nan)
{
    spectral::Stft st;
    st.prepare (48000.0, 2, 2048);
    Identity cb;
    std::vector<std::vector<float>> in (2, std::vector<float> (20000, 0.0f));
    in[0][100] = 1.0e6f;
    in[1][5000] = -1.0f;

    for (int i = 9000; i < 9500; ++i)
        in[0][(size_t) i] = 1.0e-30f;                          // denormal territory

    const auto out = run (st, cb, in, 333);

    for (const auto& ch : out)
        for (float x : ch)
            GF_EXPECT (std::isfinite (x));
}

namespace
{
    /** Callback that deliberately writes NaN / Inf into the bins. */
    struct Poison : spectral::SpectralCallback
    {
        void processFrame (spectral::SpectralFrame& f) noexcept override
        {
            f.mag[3] = std::numeric_limits<float>::quiet_NaN();
            f.phase[5] = std::numeric_limits<float>::infinity();
        }
    };
}

GF_TEST (Stft, nan_bins_are_guarded)
{
    spectral::Stft st;
    st.prepare (48000.0, 1, 1024);
    Poison cb;
    const auto in = makeNoiseSignal (1, 10000);
    const auto out = run (st, cb, in, 128);

    for (float x : out[0])
        GF_EXPECT (std::isfinite (x));
}

GF_TEST (Stft, throughput_budget)
{
    spectral::Stft st;
    st.prepare (48000.0, 2, 2048);
    Identity cb;
    const auto in = makeNoiseSignal (2, 48000);

    const auto t0 = std::chrono::steady_clock::now();
    run (st, cb, in, 512);
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();

    std::printf ("      [SP-0] 1 s stereo at N=2048 / 48 kHz: %.2f ms (%.2f %% of real time)\n", ms, ms / 10.0);
    GF_EXPECT (ms < 250.0);
}

GF_TEST (Stft, no_alloc)
{
    spectral::Stft st;
    st.prepare (48000.0, 2, 2048);
    Identity cb;
    const auto in = makeNoiseSignal (2, 20000);
    auto out = in;
    const float* ip[2] { in[0].data(), in[1].data() };
    float* op[2] { out[0].data(), out[1].data() };
    st.process (ip, op, 4096, cb);                              // warm up

    AllocGuard guard;
    st.process (ip, op, 16000, cb);
    GF_EXPECT (guard.count() == 0);
}

GF_TEST (Stft, phase_helpers)
{
    using namespace spectral;

    for (float x : { -10.0f, -3.2f, 0.0f, 3.2f, 100.0f })
    {
        const auto w = wrapPi (x);
        GF_EXPECT (w >= -kPi - 1.0e-4f && w <= kPi + 1.0e-4f);
        GF_EXPECT_NEAR (std::cos (w), std::cos (x), 1.0e-3);
    }

    // instantaneous frequency of a sine between two bins is recovered from two frames
    const int N = 2048, hop = 512;
    const double sr = 48000.0;
    const double hz = 1000.0 + 0.3 * sr / N;                  // 0.3 bin above bin 42.67 -> not bin centred
    Stft st;
    st.prepare (sr, 1, N);

    struct Grab : SpectralCallback
    {
        std::vector<float> phases[2], mags;
        int count = 0;
        void processFrame (SpectralFrame& f) noexcept override
        {
            if (count >= 10 && count < 12)
                phases[count - 10].assign (f.phase, f.phase + f.numBins);

            if (count == 11)
                mags.assign (f.mag, f.mag + f.numBins);

            ++count;
        }
    } grab;

    std::vector<std::vector<float>> in (1, std::vector<float> (N * 6));

    for (size_t i = 0; i < in[0].size(); ++i)
        in[0][i] = (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / sr);

    run (st, grab, in, 512);

    int peak = 0;

    for (int k = 0; k < (int) grab.mags.size(); ++k)
        if (grab.mags[(size_t) k] > grab.mags[(size_t) peak])
            peak = k;

    const auto bin = instantaneousBin (peak, grab.phases[1][(size_t) peak], grab.phases[0][(size_t) peak], N, hop);
    GF_EXPECT_NEAR (bin * sr / N, hz, 2.0);
}

GF_TEST (Stft, identity_phase_lock_preserves_peak_relations)
{
    using namespace spectral;
    const int bins = 64;
    std::vector<float> mag ((size_t) bins, 0.01f), ana ((size_t) bins), syn ((size_t) bins, 0.0f);
    std::vector<int> peaks ((size_t) bins);

    for (int k = 0; k < bins; ++k)
        ana[(size_t) k] = 0.1f * (float) k;

    mag[20] = 1.0f;
    mag[21] = 0.5f;
    mag[19] = 0.5f;
    mag[40] = 0.8f;
    syn[20] = 1.0f;
    syn[40] = -2.0f;

    const auto np = identityPhaseLock (mag.data(), ana.data(), syn.data(), bins, peaks.data());
    GF_EXPECT (np == 2);
    GF_EXPECT_NEAR (syn[21], wrapPi (1.0f + ana[21] - ana[20]), 1.0e-5);
    GF_EXPECT_NEAR (syn[19], wrapPi (1.0f + ana[19] - ana[20]), 1.0e-5);
    GF_EXPECT_NEAR (syn[41], wrapPi (-2.0f + ana[41] - ana[40]), 1.0e-5);
    GF_EXPECT (syn[20] == 1.0f && syn[40] == -2.0f);
}
