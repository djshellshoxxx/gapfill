#pragma once

// F-L07 / N-VAL message-thread controller: owns the SceneBank, turns "go to
// scene n / next / prev" into overlays for the audio-thread SceneEngine, and
// commits the values into the APVTS (with host gestures) once the audio thread
// reports that the boundary was reached.

#include "SceneOverlay.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace gapfill::scenes
{

class SceneController
{
public:
    explicit SceneController (juce::AudioProcessorValueTreeState& apvts);

    SceneBank& getBank() noexcept                   { return bank; }
    const SceneBank& getBank() const noexcept       { return bank; }
    TripleBuffer<Overlay>& publisher() noexcept     { return pub; }

    //------------------------------------------------------------------------
    // Navigation (all quantised by the audio thread).

    /** Requests scene `index`. False (status set) when the index is out of range
        or the scene is invalid; the previous state is then kept. */
    bool gotoScene (int index);
    bool next();
    bool prev();

    /** Discards a pending request. */
    void cancel();

    /** Re-recalls the last committed scene. */
    bool gotoLastGood();

    /** Saves the current sound into slot `index` (or appends when index == size). */
    bool saveCurrent (int index, const juce::String& name, std::uint16_t scope = kDefaultScope);

    //------------------------------------------------------------------------
    // From the audio thread (through LiveControl's UiRequest queue).
    void onApplied (std::uint32_t requestId);

    /** Drops overlay entries whose parameter the user moved since the request,
        and finishes a commit that raced a Clear. Call from the timer. */
    void poll();

    //------------------------------------------------------------------------
    int  getPendingIndex() const noexcept           { return pendingIndex; }
    int  getCurrentIndex() const noexcept           { return bank.current; }
    int  getLastGoodIndex() const noexcept          { return lastGood; }
    std::uint32_t getPendingId() const noexcept     { return pendingId; }
    const juce::String& getStatus() const noexcept  { return status; }

    /** Number of scene commits performed (tests). */
    int getCommitCount() const noexcept             { return commits; }

    /** Replaces the bank (import / state load) after cancelling any request. */
    void replaceBank (const SceneBank& newBank);

    /** Undo for the last import. */
    bool canUndoImport() const noexcept             { return hasUndo; }
    void rememberForUndo();
    bool undoImport();

    void reset();     ///< clears bank and request state

private:
    bool request (int index);
    int  clampStep (int from, int delta) const;
    void publishOverlay (Overlay::Kind kind);

    juce::AudioProcessorValueTreeState& apvts;
    TripleBuffer<Overlay> pub;
    SceneBank bank;
    SceneBank undoBank;
    bool hasUndo = false;

    int pendingIndex = -1;
    int lastGood = -1;
    std::uint32_t pendingId = 0;
    std::uint32_t idCounter = 0;
    std::uint8_t pendingQuant = (std::uint8_t) Quant::Immediate;
    std::uint8_t pendingWait = 0;

    std::vector<ResolvedParam> resolved;       ///< entries still governed by the overlay
    std::vector<float> baseline01;             ///< parameter value when the request was made
    bool committing = false;
    int commits = 0;
    juce::String status;
};

} // namespace gapfill::scenes
