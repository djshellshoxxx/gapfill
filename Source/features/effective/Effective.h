#pragma once

// Effective-settings pipeline (docs/dev/02-FOUNDATIONS.md F0-5).
//
// Pure functions over EngineSettings: no JUCE UI, no threads, no allocation,
// fully unit-testable. Two stages so a decision never depends on where the host
// happened to cut the block:
//
//   block stage     parameters only      applyPersonality -> applyMacros
//   decision stage  needs the PPQ of the decision sample, called by the
//                   scheduler on a local copy: Evolve -> Breathing -> Phrase
//
// CONTRACT: every stage returns immediately and leaves its input byte-for-byte
// untouched when its feature is at the neutral setting (Effective tests check
// both applyBlock and applyAtDecision with memcmp). A stage never draws from
// the legacy Rng (docs/dev/06 section 2).
//
// A feature owns exactly one stage file (Stage<Feature>.cpp) and edits nothing
// else here; the fixed call order below lives in Effective.cpp.

#include "../../dsp/EngineSettings.h"

namespace gapfill::Effective
{

/** Host transport as seen at the decision sample. */
struct TransportInfo
{
    bool   valid = false;
    double ppq = 0.0;
    int    tsNum = 0, tsDen = 0;
    double bpm = 0.0;
};

//==============================================================================
// Block-stage inputs. Add a field here when a block-stage feature needs a new
// parameter; keep the default neutral.

/** Macro knobs (C-2). When `on` is false the macro stage is skipped, so
    sessions saved before macros existed sound the same. */
struct MacroState
{
    bool  on = false;
    float activity = 50.0f, chaos = 25.0f, space = 25.0f, destruction = 15.0f;   // percent

    /** Mappings and smoothing state (audio-thread owned, set by LiveControl); null = no mappings. */
    macros::MacroRuntime* runtime = nullptr;
};

/** Values the macro stage hands to later processing stages (C-2). */
struct MacroOutputs
{
    float space01 = 0.0f;
    float destruction01 = 0.0f;
};

struct ModulationInputs
{
    int personality = 0;           ///< 0 = Off (C-1)
    MacroState macros;
};

//==============================================================================
// Block stage (parameters only).

void applyPersonality (EngineSettings&, int personality);                          // C-1  StagePersonality.cpp
void applyMacros      (EngineSettings&, const MacroState&, MacroOutputs&);         // C-2  StageMacros.cpp

/** Runs the block stages in their fixed order. */
void applyBlock (EngineSettings&, const ModulationInputs&, MacroOutputs&);

//==============================================================================
// Decision stage (PPQ of the decision sample; ts from the block). The scene
// overlay (F-L07) and LFO macros (F-M23) run first: they set the base values the
// slow stages below then modulate.

void applyScene     (EngineSettings&, double ppq, const TransportInfo&);           // F-L07 StageScene.cpp
void applyMacroLfos (EngineSettings&, double ppq, const TransportInfo&);           // F-M23 StageMacros.cpp
void applyEvolve    (EngineSettings&, double ppq, const TransportInfo&);           // C-8  StageEvolve.cpp
void applyBreathing (EngineSettings&, double ppq, const TransportInfo&);           // C-6  StageBreathing.cpp
void applyPhrase    (EngineSettings&, double ppq, const TransportInfo&);           // C-7  StagePhrase.cpp

/** Runs the decision stages in their fixed order on the scheduler's local copy. */
void applyAtDecision (EngineSettings& sLocal, double ppq, const TransportInfo&);

} // namespace gapfill::Effective
