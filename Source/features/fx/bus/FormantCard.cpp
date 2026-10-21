#include "FormantCard.h"

namespace gapfill::fx::bus
{

namespace
{
    constexpr float kVowels[6][3] = {
        { 0, 0, 0 },
        { 800.0f, 1200.0f, 2800.0f },      // A
        { 400.0f, 2000.0f, 2600.0f },      // E
        { 300.0f, 2200.0f, 3000.0f },      // I
        { 450.0f, 800.0f, 2800.0f },       // O
        { 325.0f, 700.0f, 2700.0f }        // U
    };
    constexpr float kVowelQ = 5.0f;
    constexpr float kLn10Over20 = 0.115129255f;
}

const float (&FormantCard::vowelFormants (int vowel) noexcept)[3]
{
    return kVowels[std::clamp (vowel, 0, 5)];
}

float FormantCard::snapShift (float semitones, std::uint16_t mask) noexcept
{
    if (mask == 0)
        return semitones;

    const auto nearest = (int) std::lround (semitones);

    for (int d = 0; d <= 12; ++d)
        for (int sign : { 1, -1 })
        {
            const auto cand = nearest + sign * d;
            const auto rel = ((cand % 12) + 12) % 12;

            if (cand >= -12 && cand <= 12 && ((mask >> rel) & 1))
                return (float) cand;
        }

    return semitones;
}

void FormantCard::prepareCard()
{
    stft.prepare (sr, 2, sr <= 48000.0 ? 1024 : 2048);
    const auto N = stft.size();
    int order = 0;

    while ((1 << order) < N)
        ++order;

    cfft = std::make_unique<juce::dsp::FFT> (order);
    cepBuf.assign ((size_t) N * 2, 0.0f);
    logMag.assign ((size_t) stft.numBins(), 0.0f);
    envLog.assign ((size_t) stft.numBins(), 0.0f);
    target.assign ((size_t) stft.numBins(), 0.0f);
    prevMag[0].assign ((size_t) stft.numBins(), 0.0f);
    prevMag[1].assign ((size_t) stft.numBins(), 0.0f);
    lifterQ = std::max (4, (int) std::lround (sr / 1000.0));

    for (auto& d : dry)
        d.allocate (stft.latencySamples() + 16);
}

void FormantCard::resetCard() noexcept
{
    stft.reset();

    for (auto& d : dry)
        d.clear();

    for (auto& p : prevMag)
        std::fill (p.begin(), p.end(), 0.0f);
}

std::size_t FormantCard::preallocatedBytes() const noexcept
{
    return stft.preallocatedBytes() + (cepBuf.size() + logMag.size() * 4) * sizeof (float)
         + (std::size_t) 2 * dry[0].capacity() * sizeof (float);
}

void FormantCard::blockSettings (const FxSettings& fs, const BusFxContext& ctx) noexcept
{
    const auto& s = fs.formant;
    shiftSt = snapShift (s.shiftSt, ctx.scaleMask);
    tiltDb = 6.0f * s.tilt01;
    vowel = s.vowel;
}

void FormantCard::processFrame (spectral::SpectralFrame& f) noexcept
{
    const int bins = f.numBins;
    const int N = f.fftSize;
    auto* mag = f.mag;
    auto& prev = prevMag[f.channel & 1];

    if (shiftSt == 0.0f && tiltDb == 0.0f && vowel == 0)
        return;

    // transient guard: large spectral flux leaves the frame untouched
    float flux = 0.0f, total = 1.0e-9f;

    for (int k = 0; k < bins; ++k)
    {
        flux += std::max (0.0f, mag[k] - prev[(size_t) k]);
        total += mag[k];
        prev[(size_t) k] = mag[k];
    }

    if (flux / total > kFluxBypass)
    {
        ++bypassed;
        return;
    }

    // ---- cepstral envelope ---------------------------------------------------
    for (int k = 0; k < bins; ++k)
        logMag[(size_t) k] = std::log (std::max (mag[k], 1.0e-9f));

    auto* cb = cepBuf.data();
    const float invN = 1.0f / (float) N;
    const int taperStart = (int) (lifterQ * 0.75f);

    // log spectrum is real and even: its transform is the (real) cepstrum
    std::fill (cepBuf.begin(), cepBuf.end(), 0.0f);

    for (int k = 0; k < bins; ++k)
    {
        cb[k] = logMag[(size_t) k];

        if (k > 0 && k < N / 2)
            cb[N - k] = logMag[(size_t) k];
    }

    cfft->performRealOnlyForwardTransform (cb, true);

    for (int n = 0; n <= N / 2; ++n)
        target[(size_t) n] = cb[2 * n] * invN;      // c[n] (target[] is scratch here)

    // lifter: keep quefrencies < Q with a half-cosine taper over the last 25 %
    std::fill (cepBuf.begin(), cepBuf.end(), 0.0f);

    for (int n = 0; n < lifterQ && n <= N / 2; ++n)
    {
        const float w = n < taperStart ? 1.0f
                                       : 0.5f * (1.0f + std::cos (spectral::kPi * (float) (n - taperStart) / (float) (lifterQ - taperStart)));
        cb[n] = target[(size_t) n] * w;

        if (n > 0)
            cb[N - n] = cb[n];
    }

    cfft->performRealOnlyForwardTransform (cb, true);

    for (int k = 0; k < bins; ++k)
        envLog[(size_t) k] = cb[2 * k];

    // ---- target envelope --------------------------------------------------------
    const float alpha = std::exp2 (shiftSt / 12.0f);
    const float binHz = (float) f.sampleRate / (float) N;

    for (int k = 0; k < bins; ++k)
    {
        const float src = std::clamp ((float) k / alpha, 0.0f, (float) (bins - 1));
        const int i0 = (int) src;
        const int i1 = std::min (i0 + 1, bins - 1);
        const float fr = src - (float) i0;
        target[(size_t) k] = envLog[(size_t) i0] * (1.0f - fr) + envLog[(size_t) i1] * fr;
    }

    if (vowel > 0)
    {
        const auto& fm = kVowels[vowel];
        float meanV = 0.0f, meanE = 0.0f;

        for (int k = 0; k < bins; ++k)
        {
            const float hz = (float) k * binHz;
            float v = 0.05f;

            for (int i = 0; i < 3; ++i)
            {
                const float fc = fm[i] * alpha;
                const float bw = fc / kVowelQ;
                const float x = (hz - fc) / (0.5f * bw);
                v += 1.0f / (1.0f + x * x);
            }

            target[(size_t) k] = std::log (v);
            meanV += target[(size_t) k];
            meanE += envLog[(size_t) k];
        }

        const float offset = (meanE - meanV) / (float) bins;

        for (int k = 0; k < bins; ++k)
            target[(size_t) k] += offset;
    }

    const float maxLog = kMaxGainDb * kLn10Over20;

    for (int k = 0; k < bins; ++k)
    {
        const float hz = std::max ((float) k * binHz, 50.0f);
        float t = target[(size_t) k];

        if (tiltDb != 0.0f)
            t += kLn10Over20 * tiltDb * std::log2 (hz / 1000.0f);

        const float g = std::clamp (t - envLog[(size_t) k], -maxLog, maxLog);
        mag[k] *= std::exp (g);
    }
}

void FormantCard::renderChunk (float* l, float* r, int n, const float* amt, const float* duck,
                               const BusFxContext&, const FxSettings&) noexcept
{
    const float* in[2] { l, r };
    float* out[2] { outL, outR };
    float* data[2] { l, r };
    float* outs[2] { outL, outR };

    // the dry path must be delayed by the same latency, so capture it before overwriting
    float dryD[2][kMaxIn];

    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            dry[c].push (data[c][i]);
            dryD[c][i] = dry[c].readInt (stft.latencySamples() + 1);
        }

    stft.process (in, out, n, *this);

    for (int i = 0; i < n; ++i)
    {
        const auto k = duck != nullptr ? duck[i] : 1.0f;

        for (int c = 0; c < 2; ++c)
            data[c][i] = ((1.0f - amt[i]) * dryD[c][i] + amt[i] * outs[c][i]) * k;
    }
}

} // namespace gapfill::fx::bus
