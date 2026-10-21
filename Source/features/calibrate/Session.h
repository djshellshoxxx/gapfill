#pragma once

// U-6 / F-L05 calibration session: the message-thread state machine around RecordTap, the background
// analysis job and the three-parameter apply (with an Undo snapshot and host gestures).
//
//   Idle -> Recording -> Analysing -> Result -> (Apply -> Applied -> Revert) | Discard -> Idle
//                   \-> Idle on Cancel / sample-rate change / refusal
//
// The session owns the worker thread and stops it in its destructor (editor close, processor teardown), so
// nothing outlives the editor. The editor drives tick() from its timer; all callbacks are polled, never
// posted, so a destroyed editor cannot be reached from the worker.

#include "Calibrator.h"
#include "RecordTap.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::calibrate
{

enum class Phase { Idle, Recording, Analysing, Result, Applied };

class CalibrationSession
{
public:
    explicit CalibrationSession (GapFillAudioProcessor&);
    ~CalibrationSession();

    /** Starts listening. Returns false and sets `message` when refused ("Try again", offline render ...). */
    bool start (int seconds = kDefaultSeconds);
    void cancel();

    /** Message thread, from a timer: advances Recording -> Analysing -> Result. Returns true when the phase changed. */
    bool tick();

    /** Result -> Applied. Pushes ONE Undo snapshot, then sets Sensitivity, Gap Threshold and Min Gap with gestures. */
    bool apply();
    void discard();
    /** Applied -> Idle through the Undo snapshot; false (and disabled) once other undoable actions happened. */
    bool revert();
    bool canRevert() const;

    Phase getPhase() const noexcept             { return phase; }
    float getProgress() const noexcept;         ///< 0..1 for the current phase
    double getSecondsLeft() const noexcept;
    const juce::String& getMessage() const noexcept { return message; }   ///< status / result / refusal text
    const CalibResult& getResult() const noexcept   { return result; }
    bool hasResult() const noexcept             { return phase == Phase::Result || phase == Phase::Applied; }

    /** Warning shown when the detector source is not Main ("Calibrated from the main input only"). */
    juce::String getSourceWarning() const;

    /** Test seam: analyse a prepared recording directly (as if the tap had delivered it). */
    void analyseForTests (std::vector<float> samples, double sampleRate);

private:
    class Job;

    CalibInput makeInput (const std::vector<float>& samples, double sampleRate) const;
    void startJob (std::vector<float> samples, double sampleRate);
    void stopJob();

    GapFillAudioProcessor& proc;
    Phase phase = Phase::Idle;
    juce::String message;
    CalibResult result;
    CalibInput lastInput;
    double recordedRate = 0.0;
    int undoDepthAfterApply = -1;

    std::vector<float> samplesHeld;             ///< kept for the worker (copy of the recording)
    std::unique_ptr<Job> job;
    std::atomic<bool> cancelFlag { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalibrationSession)
};

/** AnalysisSettings from the current parameter values (the calibration "base"). */
AnalysisSettings currentAnalysisSettings (GapFillAudioProcessor&);

} // namespace gapfill::calibrate
