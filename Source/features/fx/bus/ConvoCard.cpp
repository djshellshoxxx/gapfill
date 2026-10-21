#include "ConvoCard.h"

#include "../../../dsp/DspUtils.h"

namespace gapfill::fx::bus
{

namespace
{
    constexpr double kHistorySeconds = 4.0;
    constexpr double kPreRollMs = 2.0;
    constexpr int kWorkerSleepMs = 20;
}

ConvoCard::ConvoCard() = default;

ConvoCard::~ConvoCard()
{
    stopWorker.store (true, std::memory_order_release);

    if (worker.joinable())
        worker.join();
}

int ConvoCard::findStrongestEvent (const float* mono, int n, int onsetWindow) noexcept
{
    int best = -1;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const auto a = std::abs (mono[i]);

        if (std::isfinite (a) && a > peak)
        {
            peak = a;
            best = i;
        }
    }

    if (best < 0 || peak < 1.0e-6f)
        return -1;

    for (int i = std::max (0, best - onsetWindow); i < best; ++i)
        if (std::abs (mono[i]) >= 0.25f * peak)
            return i;

    return best;
}

bool ConvoCard::shapeIr (juce::AudioBuffer<float>& ir, int len, double sr, float damp01) noexcept
{
    const auto channels = ir.getNumChannels();
    len = std::min (len, ir.getNumSamples());

    if (len < 8)
        return false;

    const auto fadeStart = (int) (len * 0.6);

    for (int c = 0; c < channels; ++c)
    {
        auto* d = ir.getWritePointer (c);
        Svf hp;
        hp.set (80.0f, 0.7071f, sr);
        float lpState = 0.0f;
        float a = 1.0f;

        for (int i = 0; i < len; ++i)
        {
            auto x = d[i];

            if (! std::isfinite (x))
                return false;

            float lo, hi;
            hp.process (x, lo, hi);
            x = hi;

            if (damp01 > 0.0f)
            {
                if ((i & 15) == 0)
                {
                    const auto fc = 18000.0f * std::pow (600.0f / 18000.0f, damp01 * (float) i / (float) len);
                    a = onePoleCoeff (fc, sr);
                }

                lpState += a * (x - lpState);
                x = lpState;
            }

            if (i >= fadeStart)
            {
                const auto t = (float) (i - fadeStart) / (float) (len - fadeStart);
                x *= 0.5f * (1.0f + std::cos (kPi * t));         // half-Hann fade-out
            }

            d[i] = x;
        }
    }

    double sum = 0.0;

    for (int c = 0; c < channels; ++c)
    {
        const auto* d = ir.getReadPointer (c);

        for (int i = 0; i < len; ++i)
            sum += (double) d[i] * (double) d[i];
    }

    const auto rms = std::sqrt (sum / (double) (len * channels));

    if (! std::isfinite (rms) || rms < 1.0e-6)
        return false;

    const auto gain = (float) (std::pow (10.0, (double) kIrTargetDb / 20.0) / rms);

    for (int c = 0; c < channels; ++c)
        ir.applyGain (c, 0, len, gain);

    return true;
}

void ConvoCard::prepareCard()
{
    // A re-prepare (sample-rate / block-size change) reallocates `mailbox` and rewrites `sr`, both of which the
    // worker reads while it copies a captured IR. Quiesce the worker first; it is restarted below.
    if (worker.joinable())
    {
        stopWorker.store (true, std::memory_order_release);
        worker.join();
        prepared = false;
    }

    const auto maxBlock = std::max (blockMax, BusCardBase::kChunk);
    spec = { sr, (juce::uint32) maxBlock, 2 };
    conv.prepare (spec);
    scratch.setSize (2, maxBlock, false, true, false);

    const auto irMax = (int) (sr * (kMaxIrSeconds + 0.004)) + 16;
    mailbox.setSize (2, irMax, false, true, false);
    capScratch.setSize (2, (int) (sr * kHistorySeconds) + 16, false, true, false);
    monoScratch.assign ((size_t) capScratch.getNumSamples(), 0.0f);
    slot.store (Empty, std::memory_order_release);

    if (! prepared)
    {
        prepared = true;
        stopWorker.store (false, std::memory_order_release);
        worker = std::thread ([this] { workerLoop(); });
    }
}

void ConvoCard::resetCard() noexcept
{
    conv.reset();
}

void ConvoCard::workerLoop()
{
    while (! stopWorker.load (std::memory_order_acquire))
    {
        if (slot.load (std::memory_order_acquire) == Ready)
        {
            const auto len = mailboxLength;
            juce::AudioBuffer<float> copy (2, len);

            for (int c = 0; c < 2; ++c)
                copy.copyFrom (c, 0, mailbox, c, 0, len);

            slot.store (Empty, std::memory_order_release);
            queueIr (std::move (copy), sr);
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (kWorkerSleepMs));
    }
}

void ConvoCard::queueIr (juce::AudioBuffer<float>&& ir, double irRate)
{
    const auto len = ir.getNumSamples();

    {
        const juce::ScopedLock sl (irLock);
        currentIr.makeCopyOf (ir);
        currentIrRate = irRate;
    }

    conv.loadImpulseResponse (std::move (ir), irRate, juce::dsp::Convolution::Stereo::yes,
                              juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    irLength.store (len, std::memory_order_release);
    irLoaded.store (true, std::memory_order_release);
    irSeq.fetch_add (1, std::memory_order_acq_rel);
}

bool ConvoCard::loadIrFromBuffer (const juce::AudioBuffer<float>& in, double irRate)
{
    if (irRate < 8000.0 || in.getNumChannels() < 1 || in.getNumChannels() > 2 || in.getNumSamples() < 8)
    {
        rejectedCount.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    const auto maxLen = (int) (irRate * kMaxIrSeconds);
    const auto len = std::min (in.getNumSamples(), maxLen);
    juce::AudioBuffer<float> ir (2, len);

    for (int c = 0; c < 2; ++c)
    {
        const auto* src = in.getReadPointer (std::min (c, in.getNumChannels() - 1));

        for (int i = 0; i < len; ++i)
        {
            if (! std::isfinite (src[i]))
            {
                rejectedCount.fetch_add (1, std::memory_order_relaxed);
                return false;
            }

            ir.setSample (c, i, src[i]);
        }
    }

    if (in.getNumSamples() > maxLen)                           // trimmed: fade the cut
    {
        const auto fade = std::min (len, (int) (irRate * 0.01));

        for (int c = 0; c < 2; ++c)
            ir.applyGainRamp (c, len - fade, fade, 1.0f, 0.0f);
    }

    queueIr (std::move (ir), irRate);
    return true;
}

bool ConvoCard::loadIrFromFile (const juce::File& f)
{
    if (! f.existsAsFile() || f.getSize() > 1024 * 1024 || ! f.hasFileExtension ("wav"))
    {
        rejectedCount.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (new juce::FileInputStream (f), true));

    if (reader == nullptr || reader->lengthInSamples <= 0
        || (double) reader->lengthInSamples > reader->sampleRate * 1.0 || reader->numChannels > 2)
    {
        rejectedCount.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    return loadIrFromBuffer (buf, reader->sampleRate);
}

bool ConvoCard::saveIrToFile (const juce::File& f) const
{
    juce::AudioBuffer<float> copy;
    double rate;

    {
        const juce::ScopedLock sl (irLock);
        copy.makeCopyOf (currentIr);
        rate = currentIrRate;
    }

    if (copy.getNumSamples() == 0)
        return false;

    juce::WavAudioFormat wav;
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> out (f.createOutputStream());

    if (out == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), rate, (unsigned) copy.getNumChannels(), 24, {}, 0));

    if (writer == nullptr)
        return false;

    out.release();                                       // the writer owns the stream now
    return writer->writeFromAudioSampleBuffer (copy, 0, copy.getNumSamples());
}

void ConvoCard::clearIr()
{
    {
        const juce::ScopedLock sl (irLock);
        currentIr.setSize (0, 0);
    }

    conv.loadImpulseResponse (juce::AudioBuffer<float> (2, 16), sr, juce::dsp::Convolution::Stereo::yes,
                              juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    irLoaded.store (false, std::memory_order_release);
    irLength.store (0, std::memory_order_release);
    irSeq.fetch_add (1, std::memory_order_acq_rel);
}

void ConvoCard::captureNow (const BusFxContext& ctx, const ConvoSettings& s) noexcept
{
    const auto* hist = ctx.history;

    if (hist == nullptr || slot.load (std::memory_order_acquire) != Empty)
    {
        rejectedCount.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    const auto writePos = hist->getWritePosition();
    const auto win = (int) std::min<std::int64_t> ({ (std::int64_t) capScratch.getNumSamples(), (std::int64_t) hist->getSize() - 2, writePos });

    if (win < 64)
    {
        rejectedCount.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    hist->copyTo (capScratch, writePos - win, win);

    for (int i = 0; i < win; ++i)
        monoScratch[(size_t) i] = 0.5f * (capScratch.getSample (0, i) + capScratch.getSample (std::min (1, capScratch.getNumChannels() - 1), i));

    const auto ev = findStrongestEvent (monoScratch.data(), win, (int) (0.005 * sr));

    if (ev < 0)
    {
        rejectedCount.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    const auto pre = (int) (kPreRollMs * 0.001 * sr);
    const auto from = std::max (0, ev - pre);
    const auto want = pre + (int) (s.sizeMs * 0.001 * sr);
    const auto len = std::min ({ want, win - from, mailbox.getNumSamples() });

    if (len < (int) (0.008 * sr))
    {
        rejectedCount.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    slot.store (Writing, std::memory_order_release);

    for (int c = 0; c < 2; ++c)
        mailbox.copyFrom (c, 0, capScratch, std::min (c, capScratch.getNumChannels() - 1), from, len);

    if (shapeIr (mailbox, len, sr, s.damp01))
    {
        mailboxLength = len;
        capturesTaken.fetch_add (1, std::memory_order_relaxed);
        slot.store (Ready, std::memory_order_release);
    }
    else
    {
        rejectedCount.fetch_add (1, std::memory_order_relaxed);
        slot.store (Empty, std::memory_order_release);
    }
}

void ConvoCard::blockSettings (const FxSettings& fs, const BusFxContext& ctx) noexcept
{
    const auto& s = fs.convo;
    const auto requests = captureRequests.load (std::memory_order_acquire);

    if (requests != captureHandled)
    {
        captureHandled = requests;
        captureNow (ctx, s);
    }

    if (autoCapture.load (std::memory_order_relaxed) && ! irLoaded.load (std::memory_order_acquire)
        && ctx.history != nullptr && slot.load (std::memory_order_acquire) == Empty)
    {
        const auto writePos = ctx.history->getWritePosition();

        if (pendingCaptureAt < 0)
        {
            // first strong hit in the samples since the last look (<= one block)
            const auto look = (int) std::min<std::int64_t> (writePos - lastScanPos, 4096);

            if (look > 0 && lastScanPos > 0)
            {
                float peak = 0.0f;

                for (int i = 0; i < look; ++i)
                    peak = std::max (peak, std::abs (ctx.history->read (0, (double) (writePos - 1 - i))));

                if (peak >= 0.25f)
                    pendingCaptureAt = writePos + (std::int64_t) (s.sizeMs * 0.001 * sr);
            }
        }
        else if (writePos >= pendingCaptureAt)
        {
            pendingCaptureAt = -1;
            captureNow (ctx, s);
        }

        lastScanPos = writePos;
    }
    else
    {
        pendingCaptureAt = -1;
        lastScanPos = ctx.history != nullptr ? ctx.history->getWritePosition() : 0;
    }
}

void ConvoCard::renderChunk (float* l, float* r, int n, const float* amt, const float* duck,
                             const BusFxContext&, const FxSettings&) noexcept
{
    if (! irLoaded.load (std::memory_order_acquire))
        return;                                          // no IR: pass dry

    auto* sl = scratch.getWritePointer (0);
    auto* sr2 = scratch.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        sl[i] = l[i];
        sr2[i] = r[i];
    }

    juce::dsp::AudioBlock<float> block (scratch);
    auto sub = block.getSubBlock (0, (size_t) n);
    conv.process (juce::dsp::ProcessContextReplacing<float> (sub));

    for (int i = 0; i < n; ++i)
    {
        const auto k = duck != nullptr ? duck[i] : 1.0f;
        l[i] += amt[i] * sl[i] * k;
        r[i] += amt[i] * sr2[i] * k;
    }
}

} // namespace gapfill::fx::bus
