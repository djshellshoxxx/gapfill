#pragma once

// Audio-safe dispatch for scene effect values. Indices are runtime-only; the
// persistent identity remains the append-only host parameter ID.

namespace gapfill
{
struct EngineSettings;

namespace fxTargets
{
int indexForId (const char* id) noexcept;
void apply (EngineSettings&, int index, float real) noexcept;
bool isBusEffect (int index) noexcept;
}
}
