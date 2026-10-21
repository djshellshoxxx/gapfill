#pragma once

// F-M21 Tension / release curves (docs/dev/features/F-M21): the pure model.
// Plain data, no JUCE, no allocation; every function is deterministic in its inputs.

#include <cstdint>

namespace gapfill::tension
{

inline constexpr int kLanes = 4;                 ///< Density, Intensity, Width, FxSend
inline constexpr int kMaxPoints = 16;
inline constexpr int kMinSpacing = 8;            ///< per-mille between neighbouring points

enum Lane { Density = 0, Intensity = 1, Width = 2, FxSend = 3 };
enum Shape { Build = 0, BuildDrop = 1, Breakdown = 2, Swell = 3, Custom = 4, NumShapes };
enum End { Loop = 0, Hold = 1, Release = 2 };
enum Interp { Linear = 0, Smooth = 1, Step = 2 };

struct TensionPoint { std::uint16_t x = 0, y = 500; };         ///< per-mille of curve length / lane range

struct TensionLane
{
    std::uint8_t numPoints = 2, interp = Smooth;
    TensionPoint pt[kMaxPoints] { { 0, 500 }, { 1000, 500 } };  ///< flat 0.5 line by default (no change)
};

struct TensionCurves { TensionLane lane[kLanes]; };

struct TensionSettings
{
    bool on = false;
    int bars = 8, startBar = 0, shape = BuildDrop, end = Loop;
    float depth[kLanes] { 0.4f, 0.3f, 0.0f, 0.0f };
    TensionCurves curves;                                       ///< used by the Custom shape

    bool active() const noexcept
    {
        return on && (depth[0] > 0.0f || depth[1] > 0.0f || depth[2] > 0.0f || depth[3] > 0.0f);
    }
};

/** Result for one position: c in [0,1] per lane (0.5 = neutral), phase in [0,1). */
struct TensionOut
{
    float c[kLanes] { 0.5f, 0.5f, 0.5f, 0.5f };
    bool  active = false;
    float phase = 0.0f;
};

/** Range / ordering / count checks used before a curve set is accepted from state. */
bool validate (const TensionCurves&) noexcept;

/** Repairs a lane after an edit: pins the end points, enforces strict spacing and clamps y. */
void normalise (TensionLane&) noexcept;

float evalLane (const TensionLane&, float phase01) noexcept;       ///< 0..1
float shapeValue (int shape, float phase01) noexcept;              ///< built-in shapes, 0..1

/** Position in the curve. Valid transport and time signature only (tsDen a power of two). */
TensionOut tensionAt (double ppq, bool transportValid, int tsNum, int tsDen, const TensionSettings&) noexcept;

} // namespace gapfill::tension
