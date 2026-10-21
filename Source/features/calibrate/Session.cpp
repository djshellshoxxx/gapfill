#include "Session.h"

#include "../../PluginProcessor.h"

namespace gapfill::calibrate
{

AnalysisSettings currentAnalysisSettings (GapFillAudioProcessor& proc)
{
    auto& s = proc.getValueTreeState();
    const auto v = [&s] (const char* id) { return s.getRawParameterValue (id)->load(); };

    AnalysisSettings a;
    a.mode = (DetMode) juce::jlimit (0, 2, (int) std::lround (v ("detMode")));
    a.source = DetSource::Main;
    a.sensitivity01 = v ("sensitivity") * 0.01f;
    a.gapThresh01 = v ("gapThresh") * 0.01f;
    a.protectHoldMs = v ("protectHold");
    a.retrigGuardMs = v ("retrigGuard");
    a.gapReleaseMs = v ("gapRelease");
    a.xoverLowMid = v ("xoverLM");
    a.xoverMidHigh = juce::jmax (a.xoverLowMid * 1.5f, v ("xoverMH"));
    return a;
}

//==============================================================================
class CalibrationSession::Job : public juce::Thread
{
public:
    Job (CalibInput in, std::vector<float> s, std::atomic<bool>& cancelRef)
        : juce::Thread ("GapFill calibration"), input (in), samples (std::move (s)), cancel (cancelRef) {}

    void run() override
    {
        input.mono = samples.data();
        input.numSamples = (int) samples.size();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        result = calibrate (input, cancel, [this] (float p) { progress.store (p, std::memory_order_relaxed); });
        elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;
        done.store (true, std::memory_order_release);
    }

    CalibInput input;
    std::vector<float> samples;
    std::atomic<bool>& cancel;
    CalibResult result;
    double elapsedMs = 0.0;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> done { false };
};

//==============================================================================
CalibrationSession::CalibrationSession (GapFillAudioProcessor& p) : proc (p) {}

CalibrationSession::~CalibrationSession()
{
    cancel();
    stopJob();
}

void CalibrationSession::stopJob()
{
    if (job != nullptr)
    {
        cancelFlag.store (true);
        job->stopThread (5000);
        job.reset();
    }
}

CalibInput CalibrationSession::makeInput (const std::vector<float>&, double sampleRate) const
{
    auto& s = proc.getValueTreeState();
    CalibInput in;
    in.sampleRate = sampleRate;
    in.base = currentAnalysisSettings (proc);
    in.currentSensitivity = (int) std::lround (s.getRawParameterValue ("sensitivity")->load());
    in.currentGapThresh = (int) std::lround (s.getRawParameterValue ("gapThresh")->load());
    in.currentMinGapMs = (int) std::lround (s.getRawParameterValue ("minGap")->load());
    return in;
}

bool CalibrationSession::start (int seconds)
{
    if (phase == Phase::Recording || phase == Phase::Analysing)
        return false;

    if (proc.isNonRealtime())
    {
        message = "Calibrating needs real-time playback, not an offline render.";
        return false;
    }

    auto& tap = proc.getUiState().recordTap;
    const auto rate = proc.getSampleRate();

    if (rate <= 0.0)
    {
        message = "Calibrating needs playback - start your host's transport and try again.";
        return false;
    }

    // Allocation happens here, on the message thread; if it is not ready the request is refused.
    const bool wasReady = tap.isReadyFor (rate);
    tap.allocate (rate);

    if (! wasReady)
    {
        message = "Try again";
        return false;
    }

    tap.reset();
    cancelFlag.store (false);

    if (! tap.start (seconds, rate))
    {
        message = "Try again";
        return false;
    }

    recordedRate = rate;
    phase = Phase::Recording;
    message = "Listening... play representative audio through GapFill.";
    return true;
}

bool CalibrationSession::tick()
{
    auto& tap = proc.getUiState().recordTap;

    if (phase == Phase::Recording)
    {
        if (proc.getSampleRate() != recordedRate)
        {
            cancel();
            message = "The sample rate changed - calibration was cancelled.";
            return true;
        }

        if (tap.getState() == RecordTap::State::Done)
        {
            auto samples = tap.snapshot();
            tap.reset();
            startJob (std::move (samples), recordedRate);
            return true;
        }

        if (tap.getState() == RecordTap::State::Cancelled)
        {
            tap.reset();
            phase = Phase::Idle;
            message = "Cancelled";
            return true;
        }
    }
    else if (phase == Phase::Analysing && job != nullptr && job->done.load (std::memory_order_acquire))
    {
        job->stopThread (1000);
        result = job->result;
        lastInput = job->input;
        lastInput.mono = nullptr;
        job.reset();
        samplesHeld.clear();

        if (result.ok)
        {
            phase = Phase::Result;
            message = describe (result, lastInput);
        }
        else
        {
            phase = Phase::Idle;
            message = result.reason;
        }

        return true;
    }

    return false;
}

void CalibrationSession::startJob (std::vector<float> samples, double sampleRate)
{
    lastInput = makeInput (samples, sampleRate);
    cancelFlag.store (false);
    phase = Phase::Analysing;
    message = "Analysing...";
    job = std::make_unique<Job> (lastInput, std::move (samples), cancelFlag);
    job->startThread (juce::Thread::Priority::low);
}

void CalibrationSession::analyseForTests (std::vector<float> samples, double sampleRate)
{
    startJob (std::move (samples), sampleRate);
}

void CalibrationSession::cancel()
{
    proc.getUiState().recordTap.cancel();
    proc.getUiState().recordTap.reset();

    if (phase == Phase::Analysing)
        stopJob();

    if (phase == Phase::Recording || phase == Phase::Analysing)
    {
        phase = Phase::Idle;
        message = "Cancelled";
    }
}

float CalibrationSession::getProgress() const noexcept
{
    auto& tap = proc.getUiState().recordTap;

    if (phase == Phase::Recording)
        return tap.getTargetFrames() > 0 ? juce::jlimit (0.0f, 1.0f, (float) tap.getFramesWritten() / (float) tap.getTargetFrames()) : 0.0f;

    if (phase == Phase::Analysing && job != nullptr)
        return job->progress.load (std::memory_order_relaxed);

    return phase == Phase::Result || phase == Phase::Applied ? 1.0f : 0.0f;
}

double CalibrationSession::getSecondsLeft() const noexcept
{
    auto& tap = proc.getUiState().recordTap;

    if (phase != Phase::Recording || recordedRate <= 0.0)
        return 0.0;

    return juce::jmax (0.0, (double) (tap.getTargetFrames() - tap.getFramesWritten()) / recordedRate);
}

juce::String CalibrationSession::getSourceWarning() const
{
    const auto source = (int) std::lround (proc.getValueTreeState().getRawParameterValue ("detSource")->load());
    return source != (int) DetSource::Main ? juce::String ("Calibrated from the main input only") : juce::String();
}

bool CalibrationSession::apply()
{
    if (phase != Phase::Result || ! result.ok)
        return false;

    auto& s = proc.getValueTreeState();
    proc.pushUndoSnapshot();
    undoDepthAfterApply = proc.getUndoDepth();

    const auto set = [&s] (const char* id, int value)
    {
        if (auto* p = s.getParameter (id))
        {
            const auto v = p->convertTo0to1 ((float) value);
            p->beginChangeGesture();
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
            p->endChangeGesture();
        }
    };

    set ("sensitivity", result.sensitivity);
    set ("gapThresh", result.gapThresh);

    if (result.minGapChanged)
        set ("minGap", result.minGapMs);

    phase = Phase::Applied;
    message = "Applied. " + describe (result, lastInput);
    return true;
}

void CalibrationSession::discard()
{
    if (phase == Phase::Result)
    {
        phase = Phase::Idle;
        message = "Discarded - nothing was changed.";
    }
}

bool CalibrationSession::canRevert() const
{
    return phase == Phase::Applied && proc.canUndo() && proc.getUndoDepth() == undoDepthAfterApply;
}

bool CalibrationSession::revert()
{
    if (! canRevert())
        return false;

    const auto ok = proc.undo();
    phase = Phase::Idle;
    message = ok ? "Reverted to the previous settings." : juce::String();
    return ok;
}

} // namespace gapfill::calibrate
