#pragma once

// F-M21 curve editing (docs/dev/features/F-M21 s3 "Editor model"): the pure edit operations behind the
// Tension curve editor popover. No JUCE, no allocation. Every operation leaves the lane valid
// (first / last point pinned at x = 0 / 1000, x strictly increasing with >= kMinSpacing, y in 0..1000,
// 2..kMaxPoints points), so the result can always be handed to TensionService::publishCurves.

#include "Curves.h"

namespace gapfill::tension::edit
{

/** Moves point `index` to (x, y) per-mille. The end points keep their x (only y moves); inner points are
    held kMinSpacing away from their neighbours. Returns the index (unchanged), or -1 when `index` is bad. */
int movePoint (TensionLane&, int index, int x, int y) noexcept;

/** Adds a point at (x, y), clamped so it keeps kMinSpacing from its neighbours. Returns its index, or -1
    when the lane is full or there is no room at x. */
int addPoint (TensionLane&, int x, int y) noexcept;

/** Removes an inner point. The two end points and a lane with only two points are never removed. */
bool removePoint (TensionLane&, int index) noexcept;

/** Replaces the lane by a sampled copy of a built-in shape (Build ... Swell); Custom resets to flat.
    Uses Linear interpolation so the sampled drop of Build+Drop stays a steep ramp, not a smooth blur. */
void copyShape (TensionLane&, int shape) noexcept;

/** Flat 0.5 line (no change), the default of an untouched lane. */
void resetLane (TensionLane&) noexcept;

/** Index of the point nearest to (x, y) in per-mille, scaled by the canvas aspect (x weighted by `xWeight`). */
int nearestPoint (const TensionLane&, int x, int y, float xWeight = 1.0f) noexcept;

/** Moves the selection by a keyboard step: dx / dy in per-mille (Shift = 10x is the caller's job). */
int nudgePoint (TensionLane&, int index, int dx, int dy) noexcept;

} // namespace gapfill::tension::edit
