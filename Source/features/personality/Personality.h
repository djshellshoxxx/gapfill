#pragma once

// C-1 Fill Personalities (docs/dev/04-SPEC-CREATIVE.md C-1).
//
// A personality is a block-stage overlay: it multiplies the algorithm weights and
// four creative scalars of the EngineSettings copy the scheduler uses. It never
// writes parameters and never touches protection, detector, mix, latency, seed
// or the Fill Type. Pure data + functions, usable on the audio thread.

#include "../../dsp/EngineSettings.h"

namespace gapfill::personality
{

enum Id { Off = 0, Subtle, Percussive, Glitchy, Psychedelic, Dub, Aggressive, Ambient, Unhinged, NumPersonalities };

inline constexpr int numNamed = 7;           // Subtle .. Ambient have a fixed overlay row

struct Overlay
{
    float weight[numAlgos];                  // multiplier per algorithm (Algo order)
    float density, complexity, chaos, intensity;
};

/** Overlay row for Subtle..Ambient (1..7); nullptr for Off, Unhinged and out of range. */
const Overlay* overlayFor (int personality) noexcept;

const char* name (int personality) noexcept;

/** Minimum effective mistakes amount forced by Unhinged (once C-3 exists). */
inline constexpr float kUnhingedMistakes01 = 0.35f;

/** Applies the overlay in place. Identity (byte-for-byte) for Off / out of range. */
void apply (EngineSettings&, int personality) noexcept;

} // namespace gapfill::personality
