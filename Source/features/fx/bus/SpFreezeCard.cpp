#include "SpFreezeCard.h"

#include "../../Salts.h"
#include "../../../dsp/DspUtils.h"

namespace gapfill::fx::bus
{

namespace
{
    constexpr int kCaptureFrames = 4;
}

std::size_t SpFreezeCard::preallocatedBytes() const noexcept
{
    const auto bins = (std::size_t) stft.numBins();
    return stft.preallocatedBytes()
         + (window.size() + fftBuf.size() + accMag.size() + mixMag.size() + mixDev.size() + outMag.size()
            + prefix.size() + widthOff.size() + refPhase.size()) * sizeof (float)
         + (std::size_t) 2 * 2 * 3 * bins * sizeof (float) + 2 * bins * sizeof (float)
         + (std::size_t) capBuf.getNumChannels() * (std::size_t) capBuf.getNumSamples() * sizeof (float)
         + peaks.size() * sizeof (int);
}

void SpFreezeCard::prepareCard()
{
    const auto N = spectral::Stft::defaultSize (sr);
    stft.prepare (sr, 2, N);
    const auto bins = (size_t) stft.numBins();

    int order = 0;

    while ((1 << order) < N)
        ++order;

    cfft = std::make_unique<juce::dsp::FFT> (order);
    window.assign ((size_t) N, 0.0f);

    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (spectral::kTwoPi * (float) i / (float) N));

    fftBuf.assign ((size_t) N * 2, 0.0f);
    accMag.assign (bins, 0.0f);
    mixMag.assign (bins, 0.0f);
    mixDev.assign (bins, 0.0f);
    outMag.assign (bins, 0.0f);
    prefix.assign (bins + 1, 0.0f);
    widthOff.assign (bins, 0.0f);
    refPhase.assign (bins, 0.0f);
    peaks.assign (bins, 0);
    synth[0].assign (bins, 0.0f);
    synth[1].assign (bins, 0.0f);
    capBuf.setSize (2, N + (kCaptureFrames - 1) * stft.hop(), false, true, false);

    for (auto& f : frames)
        for (int c = 0; c < 2; ++c)
        {
            f.mag[c].assign (bins, 0.0f);
            f.phase[c].assign (bins, 0.0f);
            f.dev[c].assign (bins, 0.0f);
        }
}

void SpFreezeCard::resetCard() noexcept
{
    stft.reset();
    active = false;
    releasing = false;
    sounding.store (false, std::memory_order_release);
    elapsed = 0;
    level = 1.0f;
}

void SpFreezeCard::captureInto (Frame& dst, const BusFxContext& ctx, std::int64_t anchor) noexcept
{
    const auto N = stft.size();
    const auto hop = stft.hop();
    const auto bins = stft.numBins();
    const auto span = N + (kCaptureFrames - 1) * hop;
    const auto* hist = ctx.history;

    capBuf.clear();

    if (hist != nullptr)
    {
        anchor = std::min (anchor, hist->getWritePosition());
        hist->copyTo (capBuf, anchor - span, span);
    }

    for (int c = 0; c < 2; ++c)
    {
        const auto* src = capBuf.getReadPointer (std::min (c, capBuf.getNumChannels() - 1));
        std::fill (accMag.begin(), accMag.end(), 0.0f);
        float* lastPhase = dst.phase[c].data();
        float prevPhase[4096 / 2 + 1];

        for (int j = 0; j < kCaptureFrames; ++j)
        {
            const auto* seg = src + j * hop;

            for (int i = 0; i < N; ++i)
                fftBuf[(size_t) i] = seg[i] * window[(size_t) i];

            std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);
            cfft->performRealOnlyForwardTransform (fftBuf.data(), true);

            for (int k = 0; k < bins; ++k)
            {
                const auto re = fftBuf[(size_t) (2 * k)], im = fftBuf[(size_t) (2 * k + 1)];
                accMag[(size_t) k] += std::sqrt (re * re + im * im);
                const auto ph = std::atan2 (im, re);

                if (j == kCaptureFrames - 2)
                    prevPhase[k] = ph;

                if (j == kCaptureFrames - 1)
                    lastPhase[k] = ph;
            }
        }

        for (int k = 0; k < bins; ++k)
        {
            dst.mag[c][(size_t) k] = accMag[(size_t) k] / (float) kCaptureFrames;
            dst.dev[c][(size_t) k] = std::clamp (spectral::instantaneousBin (k, lastPhase[k], prevPhase[k], N, hop) - (float) k, -2.0f, 2.0f);
        }
    }
}

void SpFreezeCard::startSustain (std::uint32_t seed) noexcept
{
    const auto bins = stft.numBins();
    Rng rng;
    rng.seed (hashCombine ((std::uint64_t) seed, kSalt_SpFreeze));

    const auto& a = frames[0];

    for (int c = 0; c < 2; ++c)
    {
        auto& sp = synth[c];

        if (c == 0)
        {
            for (int k = 0; k < bins; ++k)
                sp[(size_t) k] = (rng.nextFloat() * 2.0f - 1.0f) * spectral::kPi;

            spectral::identityPhaseLock (a.mag[0].data(), a.phase[0].data(), sp.data(), bins, peaks.data());
        }
        else
        {
            sp = synth[0];
        }
    }

    for (int k = 0; k < bins; ++k)
        widthOff[(size_t) k] = (rng.nextFloat() * 2.0f - 1.0f) * spectral::kPi;

    stft.reset();
    active = true;
    releasing = false;
    elapsed = 0;
    level = 1.0f;
    fadeInLeft = std::max (1, (int) (0.005 * sr));
    sounding.store (true, std::memory_order_release);
}

void SpFreezeCard::trigger (const BusFxContext& ctx, std::int64_t anchor, std::uint32_t seed) noexcept
{
    if (! haveA)
    {
        captureInto (frames[0], ctx, anchor);
        haveA = true;
        haveB.store (false, std::memory_order_release);
    }
    else if (! haveB.load (std::memory_order_acquire))
    {
        captureInto (frames[1], ctx, anchor);
        haveB.store (true, std::memory_order_release);
    }
    else
    {
        std::swap (frames[0], frames[1]);                    // the newer capture becomes A, the latest is B
        captureInto (frames[1], ctx, anchor);
    }

    startSustain (seed);
    captureSeq.fetch_add (1, std::memory_order_acq_rel);
}

void SpFreezeCard::blockSettings (const FxSettings& fs, const BusFxContext& ctx) noexcept
{
    const auto& s = fs.spfreeze;

    lengthSamples = (std::int64_t) (s.lengthMs * 0.001 * sr);
    const auto rt60 = 0.1f * std::pow (40.0f, s.decay01);
    decayPerSample = std::pow (10.0f, -3.0f / (rt60 * (float) sr));
    morph = s.morph01;
    smear = s.smear01;
    pitchRatio = std::exp2 (s.pitchSt / 12.0f);
    width = s.width01;

    const auto rel = releaseReq.load (std::memory_order_acquire);

    if (rel != releaseHandled)
    {
        releaseHandled = rel;

        if (active && ! releasing)
        {
            releasing = true;
            releaseTotal = releaseLeft = std::max (1, (int) (0.020 * sr));
        }
    }

    const auto cap = captureReq.load (std::memory_order_acquire);

    if (cap != captureHandled)
    {
        captureHandled = cap;
        trigger (ctx, ctx.history != nullptr ? ctx.history->getWritePosition() : 0, cap);
        return;
    }

    if (autoCapture.load (std::memory_order_relaxed))
    {
        for (int i = 0; i < ctx.numStarts; ++i)
        {
            const auto& rec = ctx.fillStarts[i].recipe;

            if ((rec.flags & FillRecipe::flagPreview) != 0)
                continue;

            trigger (ctx, ctx.fillStarts[i].outputPos - ctx.historyOffset, rec.seed);
            break;
        }
    }
}

void SpFreezeCard::processFrame (spectral::SpectralFrame& f) noexcept
{
    const int bins = f.numBins;
    const int c = f.channel & 1;
    auto& A = frames[0];
    auto& B = frames[1];
    const bool useB = haveB.load (std::memory_order_acquire);
    const float m = morph;

    auto wb_dominates = [] (float wa, float wb) { return wb > wa; };

    for (int k = 0; k < bins; ++k)
    {
        const auto a = A.mag[c][(size_t) k];
        const auto b = useB ? B.mag[c][(size_t) k] : f.mag[k];
        mixMag[(size_t) k] = a * (1.0f - m) + b * m;
        // deviation: weighted by the contributing magnitudes so a partial keeps its own frequency
        const auto wa = a * (1.0f - m), wb = (useB ? b : 0.0f) * m;
        refPhase[(size_t) k] = wb_dominates (a * (1.0f - m), useB ? b * m : 0.0f) ? B.phase[c][(size_t) k] : A.phase[c][(size_t) k];
        mixDev[(size_t) k] = useB ? (A.dev[c][(size_t) k] * wa + B.dev[c][(size_t) k] * wb) / (wa + wb + 1.0e-12f)
                                  : A.dev[c][(size_t) k];
    }

    // smear: +-k bin box blur
    const int kb = (int) std::lround (16.0f * smear);

    if (kb > 0)
    {
        prefix[0] = 0.0f;

        for (int k = 0; k < bins; ++k)
            prefix[(size_t) k + 1] = prefix[(size_t) k] + mixMag[(size_t) k];

        for (int k = 0; k < bins; ++k)
        {
            const int lo = std::max (0, k - kb), hi = std::min (bins - 1, k + kb);
            outMag[(size_t) k] = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (float) (hi - lo + 1);
        }

        std::copy (outMag.begin(), outMag.end(), mixMag.begin());
    }

    // pitch: bin remap (output bin k reads source bin k / ratio)
    const float ratio = pitchRatio;
    auto& sp = synth[c];
    const float hopPhase = spectral::kTwoPi * (float) f.hop / (float) f.fftSize;

    for (int k = 0; k < bins; ++k)
    {
        float mg = 0.0f, dv = 0.0f;

        if (ratio == 1.0f)
        {
            mg = mixMag[(size_t) k];
            dv = mixDev[(size_t) k];
        }
        else
        {
            const float src = (float) k / ratio;

            if (src <= (float) (bins - 1))
            {
                const int i0 = (int) src;
                const int i1 = std::min (i0 + 1, bins - 1);
                const float fr = src - (float) i0;
                mg = mixMag[(size_t) i0] * (1.0f - fr) + mixMag[(size_t) i1] * fr;
                dv = (mixDev[(size_t) i0] * (1.0f - fr) + mixDev[(size_t) i1] * fr) * ratio;
            }
        }

        outMag[(size_t) k] = mg;
        sp[(size_t) k] = spectral::wrapPi (sp[(size_t) k] + hopPhase * ((float) k + dv));
    }

    spectral::identityPhaseLock (outMag.data(), refPhase.data(), sp.data(), bins, peaks.data());

    for (int k = 0; k < bins; ++k)
    {
        f.mag[k] = outMag[(size_t) k];
        f.phase[k] = c == 1 ? spectral::wrapPi (sp[(size_t) k] + widthOff[(size_t) k] * width) : sp[(size_t) k];
    }
}

void SpFreezeCard::renderChunk (float* l, float* r, int n, const float* amt, const float* duck,
                                const BusFxContext&, const FxSettings&) noexcept
{
    if (! active)
        return;

    const float* in[2] { l, r };
    float* out[2] { outL, outR };
    stft.process (in, out, n, *this);

    float* data[2] { l, r };
    const float* outs[2] { outL, outR };

    for (int i = 0; i < n; ++i)
    {
        float env = level;

        if (fadeInLeft > 0)
        {
            env *= 1.0f - (float) fadeInLeft / (float) std::max (1, (int) (0.005 * sr));
            --fadeInLeft;
        }

        const auto remaining = lengthSamples - elapsed;
        const auto tailFade = std::max (1, (int) (0.020 * sr));

        if (remaining < tailFade)
            env *= std::max (0.0f, (float) remaining / (float) tailFade);

        if (releasing)
        {
            env *= (float) std::max (0, releaseLeft) / (float) releaseTotal;
            --releaseLeft;
        }

        level *= decayPerSample;
        ++elapsed;

        const auto k = duck != nullptr ? duck[i] : 1.0f;

        for (int c = 0; c < 2; ++c)
            data[c][i] += amt[i] * outs[c][i] * env * k;
    }

    if (elapsed >= lengthSamples || level < 1.0e-4f || (releasing && releaseLeft <= 0))
    {
        active = false;
        releasing = false;
        sounding.store (false, std::memory_order_release);
    }
}

} // namespace gapfill::fx::bus
